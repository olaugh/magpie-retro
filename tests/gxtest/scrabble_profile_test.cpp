/**
 * Scrabble Genesis ROM Profiling Test
 *
 * Runs multiple games in parallel (via fork) with CPU cycle profiling
 * and aggregates the results.
 */

#include <gxtest.h>
#include <profiler.h>
#include "scrabble_symbols.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int MAX_GAME_FRAMES = 30000;
constexpr int NUM_GAMES = 4;

// Structure to pass results from child to parent via pipe
struct GameResult {
    int game_id;
    int16_t score_p0;
    int16_t score_p1;
    uint32_t frames;
    uint64_t total_cycles;
    uint32_t sample_rate;
};

// Run a single game and write results to pipe
void RunGameInChild(int game_id, const char* rom_path, const char* elf_path,
                    uint32_t sample_rate, int write_fd) {
    GX::Emulator emu;
    if (!emu.LoadRom(rom_path)) {
        _exit(1);
    }

    GX::Profiler profiler;
    if (profiler.LoadSymbolsFromELF(elf_path) <= 0) {
        _exit(1);
    }

    emu.WriteLong(Scrabble::test_seed_override, game_id);

    GX::ProfileOptions opts;
    opts.mode = GX::ProfileMode::Simple;
    opts.sample_rate = sample_rate;
    profiler.Start(opts);
    int result = emu.RunUntilMemoryEquals(Scrabble::test_game_over, 1, MAX_GAME_FRAMES);
    profiler.Stop();

    if (result < 0) {
        _exit(1);
    }

    // Write basic game info
    GameResult gr;
    gr.game_id = game_id;
    gr.score_p0 = static_cast<int16_t>(emu.ReadWord(Scrabble::test_player0_score));
    gr.score_p1 = static_cast<int16_t>(emu.ReadWord(Scrabble::test_player1_score));
    gr.frames = emu.ReadLong(Scrabble::total_frames);
    gr.total_cycles = profiler.GetTotalCycles();
    gr.sample_rate = profiler.GetSampleRate();

    if (write(write_fd, &gr, sizeof(gr)) != sizeof(gr)) {
        _exit(1);
    }

    // Write function stats
    const auto& stats = profiler.GetAllStats();
    uint32_t num_funcs = stats.size();
    if (write(write_fd, &num_funcs, sizeof(num_funcs)) != sizeof(num_funcs)) {
        _exit(1);
    }

    for (const auto& kv : stats) {
        uint32_t addr = kv.first;
        uint64_t cycles = kv.second.cycles_exclusive;
        uint64_t calls = kv.second.call_count;
        if (write(write_fd, &addr, sizeof(addr)) != sizeof(addr) ||
            write(write_fd, &cycles, sizeof(cycles)) != sizeof(cycles) ||
            write(write_fd, &calls, sizeof(calls)) != sizeof(calls)) {
            _exit(1);
        }
    }

    close(write_fd);
    _exit(0);
}

struct AggregatedFuncStats {
    uint64_t total_cycles = 0;
    uint64_t total_calls = 0;
};

void RunParallelProfile(const char* rom_path, const char* elf_path,
                        const char* name, uint32_t sample_rate = 1) {
    std::cout << "\n======================================" << std::endl;
    std::cout << name << " - " << NUM_GAMES << " Game Parallel Profile" << std::endl;
    std::cout << "======================================" << std::endl;

    // Load symbols once to get function names for reporting
    GX::Profiler symbol_lookup;
    int sym_count = symbol_lookup.LoadSymbolsFromELF(elf_path);
    if (sym_count <= 0) {
        std::cerr << "Failed to load symbols from: " << elf_path << std::endl;
        return;
    }
    std::cout << "Loaded " << sym_count << " symbols" << std::endl;
    std::cout << "Sample rate: 1/" << sample_rate;
    if (sample_rate > 1) {
        std::cout << " (estimated cycles)";
    }
    std::cout << std::endl;

    // Create pipes and fork children
    std::vector<int> read_fds(NUM_GAMES);
    std::vector<pid_t> pids(NUM_GAMES);

    for (int i = 0; i < NUM_GAMES; i++) {
        int pipefd[2];
        if (pipe(pipefd) < 0) {
            perror("pipe");
            return;
        }
        read_fds[i] = pipefd[0];

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return;
        } else if (pid == 0) {
            // Child
            close(pipefd[0]);  // Close read end
            RunGameInChild(i, rom_path, elf_path, sample_rate, pipefd[1]);
            _exit(0);  // Should not reach here
        } else {
            // Parent
            close(pipefd[1]);  // Close write end
            pids[i] = pid;
        }
    }

    // Collect results from all children
    uint64_t total_frames = 0;
    uint64_t grand_total_cycles = 0;
    std::map<uint32_t, AggregatedFuncStats> aggregated_stats;

    for (int i = 0; i < NUM_GAMES; i++) {
        // Wait for child
        int status;
        waitpid(pids[i], &status, 0);

        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            std::cerr << "Game " << i << " failed" << std::endl;
            close(read_fds[i]);
            continue;
        }

        // Read game result
        GameResult gr;
        if (read(read_fds[i], &gr, sizeof(gr)) != sizeof(gr)) {
            std::cerr << "Failed to read result for game " << i << std::endl;
            close(read_fds[i]);
            continue;
        }

        std::cout << "Game " << gr.game_id << ": " << gr.score_p0 << "-" << gr.score_p1
                  << " (" << gr.frames << " frames, " << gr.total_cycles << " cycles)" << std::endl;

        total_frames += gr.frames;
        grand_total_cycles += gr.total_cycles;

        // Read function stats
        uint32_t num_funcs;
        if (read(read_fds[i], &num_funcs, sizeof(num_funcs)) != sizeof(num_funcs)) {
            close(read_fds[i]);
            continue;
        }

        for (uint32_t j = 0; j < num_funcs; j++) {
            uint32_t addr;
            uint64_t cycles, calls;
            if (read(read_fds[i], &addr, sizeof(addr)) != sizeof(addr) ||
                read(read_fds[i], &cycles, sizeof(cycles)) != sizeof(cycles) ||
                read(read_fds[i], &calls, sizeof(calls)) != sizeof(calls)) {
                break;
            }

            aggregated_stats[addr].total_cycles += cycles;
            aggregated_stats[addr].total_calls += calls;
        }

        close(read_fds[i]);
    }

    // Print summary
    std::cout << "\n--- Summary ---" << std::endl;
    std::cout << "Total games: " << NUM_GAMES << std::endl;
    std::cout << "Total frames: " << total_frames << std::endl;
    std::cout << "Avg frames/game: " << static_cast<double>(total_frames) / NUM_GAMES << std::endl;
    std::cout << "Total cycles: " << grand_total_cycles << std::endl;
    std::cout << "Avg cycles/game: " << static_cast<double>(grand_total_cycles) / NUM_GAMES << std::endl;

    // Build sorted report by cycles
    struct FuncReport {
        std::string name;
        uint32_t addr;
        uint64_t cycles;
        uint64_t calls;
    };

    std::vector<FuncReport> report;

    // Use nm to get function names by address
    // Validate path contains only safe characters (defense-in-depth)
    for (const char* p = elf_path; *p; p++) {
        if (!std::isalnum(*p) && *p != '/' && *p != '.' && *p != '-' && *p != '_') {
            std::cerr << "Invalid character in elf_path: " << *p << std::endl;
            return;
        }
    }
    std::string cmd = std::string("nm -S --defined-only '") + elf_path + "' 2>/dev/null";
    FILE* nm_pipe = popen(cmd.c_str(), "r");
    std::map<uint32_t, std::string> addr_to_name;
    if (nm_pipe) {
        constexpr size_t LINE_BUFFER_SIZE = 512;
        constexpr size_t FUNC_NAME_SIZE = 256;
        char line[LINE_BUFFER_SIZE];
        while (fgets(line, sizeof(line), nm_pipe)) {
            uint32_t addr, size;
            char type;
            char func_name[FUNC_NAME_SIZE];
            // %255s leaves room for null terminator in 256-byte buffer
            if (sscanf(line, "%x %x %c %255s", &addr, &size, &type, func_name) >= 3 ||
                sscanf(line, "%x %c %255s", &addr, &type, func_name) >= 2) {
                if (type == 'T' || type == 't') {
                    addr_to_name[addr] = func_name;
                }
            }
        }
        pclose(nm_pipe);
    }

    for (const auto& kv : aggregated_stats) {
        if (kv.second.total_cycles > 0) {
            std::string name = addr_to_name.count(kv.first) ? addr_to_name[kv.first] : "???";
            report.push_back({name, kv.first, kv.second.total_cycles, kv.second.total_calls});
        }
    }

    std::sort(report.begin(), report.end(),
        [](const FuncReport& a, const FuncReport& b) {
            return a.cycles > b.cycles;
        });

    // Print top 20
    std::cout << "\n--- Top 20 Functions by Cycle Count ---" << std::endl;
    std::cout << std::setw(30) << std::left << "Function"
              << std::setw(15) << std::right << "Cycles"
              << std::setw(12) << "Calls"
              << std::setw(8) << "%"
              << std::setw(12) << "Cyc/Call"
              << std::endl;
    std::cout << std::string(77, '-') << std::endl;

    size_t max_show = std::min(report.size(), size_t(20));
    for (size_t i = 0; i < max_show; i++) {
        const auto& r = report[i];
        double pct = grand_total_cycles > 0 ? 100.0 * r.cycles / grand_total_cycles : 0.0;
        uint64_t per_call = r.calls > 0 ? r.cycles / r.calls : 0;

        std::cout << std::setw(30) << std::left << r.name
                  << std::setw(15) << std::right << r.cycles
                  << std::setw(12) << r.calls
                  << std::setw(7) << std::fixed << std::setprecision(2) << pct << "%"
                  << std::setw(12) << per_call
                  << std::endl;
    }

    std::cout << std::string(77, '-') << std::endl;
    std::cout << std::setw(30) << std::left << "Total"
              << std::setw(15) << std::right << grand_total_cycles
              << std::endl;
}

// Full profiling (every instruction)
TEST(ScrabbleProfile, ShadowParallel) {
    RunParallelProfile(ROM_NWL23_SHADOW, "build/nwl23-shadow/scrabble.elf", "NWL23 Shadow");
}

TEST(ScrabbleProfile, NoShadowParallel) {
    RunParallelProfile(ROM_NWL23_NOSHADOW, "build/nwl23-noshadow/scrabble.elf", "NWL23 NoShadow");
}

TEST(ScrabbleProfile, ShadowVsNoShadowParallel) {
    RunParallelProfile(ROM_NWL23_SHADOW, "build/nwl23-shadow/scrabble.elf", "NWL23 Shadow");
    RunParallelProfile(ROM_NWL23_NOSHADOW, "build/nwl23-noshadow/scrabble.elf", "NWL23 NoShadow");
}

// Sampled profiling (1/10) - moderate speedup with good accuracy
TEST(ScrabbleProfile, ShadowVsNoShadowSampled10) {
    RunParallelProfile(ROM_NWL23_SHADOW, "build/nwl23-shadow/scrabble.elf", "NWL23 Shadow", 10);
    RunParallelProfile(ROM_NWL23_NOSHADOW, "build/nwl23-noshadow/scrabble.elf", "NWL23 NoShadow", 10);
}

// Sampled profiling (1/100) - fast but less accurate
TEST(ScrabbleProfile, ShadowVsNoShadowSampled100) {
    RunParallelProfile(ROM_NWL23_SHADOW, "build/nwl23-shadow/scrabble.elf", "NWL23 Shadow", 100);
    RunParallelProfile(ROM_NWL23_NOSHADOW, "build/nwl23-noshadow/scrabble.elf", "NWL23 NoShadow", 100);
}

} // namespace
