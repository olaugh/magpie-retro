/*
 * Scrabble timing benchmark - runs 1000 games each for CSW and NWL shadow
 * and collects per-move timing statistics.
 *
 * Requires ROMs built with COLLECT_MOVE_STATS=1:
 *   make nwl23-shadow-timing csw24-shadow-timing
 *
 * Outputs histograms and statistics to stdout in text format.
 */

#include <gtest/gtest.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <map>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <thread>

#include <gxtest.h>
#include "klv.h"

using gxtest::KLV;

// ROM and ELF paths for timing builds (with COLLECT_MOVE_STATS=1)
#ifndef ROM_NWL23_SHADOW_TIMING
#define ROM_NWL23_SHADOW_TIMING "out/scrabble-nwl23-shadow-timing.bin"
#endif
#ifndef ROM_CSW24_SHADOW_TIMING
#define ROM_CSW24_SHADOW_TIMING "out/scrabble-csw24-shadow-timing.bin"
#endif
#ifndef ROM_NWL23_NOSHADOW_TIMING
#define ROM_NWL23_NOSHADOW_TIMING "out/scrabble-nwl23-noshadow-timing.bin"
#endif
#ifndef ROM_CSW24_NOSHADOW_TIMING
#define ROM_CSW24_NOSHADOW_TIMING "out/scrabble-csw24-noshadow-timing.bin"
#endif
#ifndef ELF_NWL23_SHADOW_TIMING
#define ELF_NWL23_SHADOW_TIMING "build/nwl23-shadow-timing/scrabble.elf"
#endif
#ifndef ELF_CSW24_SHADOW_TIMING
#define ELF_CSW24_SHADOW_TIMING "build/csw24-shadow-timing/scrabble.elf"
#endif
#ifndef ELF_NWL23_NOSHADOW_TIMING
#define ELF_NWL23_NOSHADOW_TIMING "build/nwl23-noshadow-timing/scrabble.elf"
#endif
#ifndef ELF_CSW24_NOSHADOW_TIMING
#define ELF_CSW24_NOSHADOW_TIMING "build/csw24-noshadow-timing/scrabble.elf"
#endif

// KLV data files for leave value computation
#ifndef KLV_NWL23
#define KLV_NWL23 "data/NWL23.klv16"
#endif
#ifndef KLV_CSW24
#define KLV_CSW24 "data/CSW24.klv16"
#endif

constexpr int NUM_GAMES = 1000;
constexpr int MAX_GAME_FRAMES = 100000;
constexpr int MAX_MOVE_STATS = 64;

// Fixed symbol addresses (from linker script, don't change between builds)
constexpr uint32_t ADDR_TEST_SEED_OVERRIDE = 0xFFFFF0;  // Test hook area at end of RAM

// Symbol addresses that vary per build (loaded from ELF)
struct TimingSymbols {
    uint32_t test_game_over;
    uint32_t total_frames;
    uint32_t move_stats_count;
    uint32_t move_stats;
    bool valid;
};

// Load symbol address by name using nm
uint32_t LoadSymbolAddress(const char* elf_path, const char* symbol_name) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "nm %s 2>/dev/null | grep ' %s$' | head -1", elf_path, symbol_name);
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return 0;

    uint32_t addr = 0;
    char line[256];
    if (fgets(line, sizeof(line), pipe)) {
        sscanf(line, "%x", &addr);
    }
    pclose(pipe);
    return addr;
}

// Load required symbols from ELF
TimingSymbols LoadTimingSymbols(const char* elf_path) {
    TimingSymbols syms = {0, 0, 0, 0, false};

    syms.test_game_over = LoadSymbolAddress(elf_path, "test_game_over");
    syms.total_frames = LoadSymbolAddress(elf_path, "total_frames");
    syms.move_stats_count = LoadSymbolAddress(elf_path, "move_stats_count");
    syms.move_stats = LoadSymbolAddress(elf_path, "move_stats");

    // Core symbols required for game completion detection
    syms.valid = (syms.test_game_over != 0 && syms.total_frames != 0);

    if (!syms.valid) {
        std::cerr << "Warning: Could not load core symbols from " << elf_path << std::endl;
        std::cerr << "  test_game_over: 0x" << std::hex << syms.test_game_over << std::endl;
        std::cerr << "  total_frames: 0x" << syms.total_frames << std::dec << std::endl;
    }
    if (syms.move_stats_count == 0 || syms.move_stats == 0) {
        std::cerr << "Note: move_stats symbols not found - stats collection disabled" << std::endl;
    }

    return syms;
}

// MoveStats structure matching the ROM (14 bytes)
struct MoveStats {
    uint16_t frames;
    uint8_t blank_count;
    uint8_t rack_size;
    uint8_t player;
    uint8_t padding;
    char rack[8];  // ASCII rack string, e.g. "RETINAS" or "PO?LISH"
};

// Serializable per-game result (fixed size for pipe transfer)
struct GameTimingResult {
    uint32_t seed;
    uint32_t total_frames;
    uint16_t move_count;
    bool completed;
    uint8_t padding;
    MoveStats moves[MAX_MOVE_STATS];
};

// Run a single game in a child process and write result to pipe
void RunGameToFd(const char* rom_path, const TimingSymbols& syms, uint32_t seed, int write_fd) {
    GameTimingResult result = {};
    result.seed = seed;
    result.completed = false;

    if (syms.valid) {
        GX::Emulator emu;
        if (emu.LoadRom(rom_path)) {
            // Patch seed before first frame (fixed address in test hook area)
            emu.WriteLong(ADDR_TEST_SEED_OVERRIDE, seed);

            // Run until game completes
            int frames = emu.RunUntilMemoryEquals(syms.test_game_over, 1, MAX_GAME_FRAMES);
            if (frames >= 0) {
                result.total_frames = emu.ReadLong(syms.total_frames);
                result.completed = true;

                // Read move stats if available (14 bytes per entry)
                if (syms.move_stats_count != 0 && syms.move_stats != 0) {
                    result.move_count = emu.ReadWord(syms.move_stats_count);
                    for (int i = 0; i < result.move_count && i < MAX_MOVE_STATS; i++) {
                        uint32_t addr = syms.move_stats + i * 14;
                        result.moves[i].frames = emu.ReadWord(addr);
                        result.moves[i].blank_count = emu.ReadByte(addr + 2);
                        result.moves[i].rack_size = emu.ReadByte(addr + 3);
                        result.moves[i].player = emu.ReadByte(addr + 4);
                        result.moves[i].padding = 0;
                        // Read rack string (8 bytes at offset 6)
                        for (int j = 0; j < 8; j++) {
                            result.moves[i].rack[j] = emu.ReadByte(addr + 6 + j);
                        }
                    }
                } else {
                    result.move_count = 0;
                }
            }
        }
    }

    write(write_fd, &result, sizeof(result));
    close(write_fd);
}

// Read result from a forked game
GameTimingResult ReadGameResult(int read_fd) {
    GameTimingResult result = {};
    read(read_fd, &result, sizeof(result));
    close(read_fd);
    return result;
}

// Print a histogram
void PrintHistogram(const std::string& title, const std::vector<uint32_t>& values,
                    int num_buckets = 20) {
    if (values.empty()) {
        std::cout << title << ": No data\n";
        return;
    }

    uint32_t min_val = *std::min_element(values.begin(), values.end());
    uint32_t max_val = *std::max_element(values.begin(), values.end());
    double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();

    // Sort for median and percentiles
    std::vector<uint32_t> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    double median = sorted[sorted.size() / 2];
    double p90 = sorted[sorted.size() * 90 / 100];
    double p99 = sorted[sorted.size() * 99 / 100];

    std::cout << "\n" << title << "\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << "Count: " << values.size() << "\n";
    std::cout << "Min: " << min_val << ", Max: " << max_val << "\n";
    std::cout << "Mean: " << std::fixed << std::setprecision(1) << mean << "\n";
    std::cout << "Median: " << median << ", P90: " << p90 << ", P99: " << p99 << "\n\n";

    // Build histogram
    double bucket_size = (max_val - min_val + 1.0) / num_buckets;
    if (bucket_size < 1) bucket_size = 1;

    std::vector<int> buckets(num_buckets, 0);
    for (uint32_t v : values) {
        int bucket = std::min((int)((v - min_val) / bucket_size), num_buckets - 1);
        buckets[bucket]++;
    }

    int max_count = *std::max_element(buckets.begin(), buckets.end());
    int bar_width = 40;

    for (int i = 0; i < num_buckets; i++) {
        uint32_t bucket_start = min_val + (uint32_t)(i * bucket_size);
        uint32_t bucket_end = min_val + (uint32_t)((i + 1) * bucket_size) - 1;
        int bar_len = (buckets[i] * bar_width) / std::max(max_count, 1);

        std::cout << std::setw(6) << bucket_start << "-" << std::setw(6) << bucket_end
                  << " |" << std::string(bar_len, '#')
                  << std::string(bar_width - bar_len, ' ')
                  << "| " << buckets[i] << "\n";
    }
}

// Print stats by category
void PrintStatsByCategory(const std::string& title,
                          const std::map<int, std::vector<uint32_t>>& data) {
    std::cout << "\n" << title << "\n";
    std::cout << std::string(60, '-') << "\n";
    std::cout << std::setw(10) << "Category"
              << std::setw(10) << "Count"
              << std::setw(12) << "Mean"
              << std::setw(12) << "Median"
              << std::setw(12) << "P90" << "\n";

    for (const auto& [cat, values] : data) {
        if (values.empty()) continue;

        double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
        std::vector<uint32_t> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        double median = sorted[sorted.size() / 2];
        double p90 = sorted[sorted.size() * 90 / 100];

        std::cout << std::setw(10) << cat
                  << std::setw(10) << values.size()
                  << std::setw(12) << std::fixed << std::setprecision(1) << mean
                  << std::setw(12) << median
                  << std::setw(12) << p90 << "\n";
    }
}

// Run benchmark for a lexicon
void RunBenchmark(const char* rom_path, const char* elf_path, const char* klv_path,
                  const std::string& lexicon_name, int num_games = NUM_GAMES) {
    std::cout << "\n" << std::string(70, '=') << "\n";
    std::cout << "BENCHMARK: " << lexicon_name << " (" << num_games << " games)\n";
    std::cout << std::string(70, '=') << "\n";

    // Load KLV for leave value computation
    KLV klv;
    bool have_klv = klv.Load(klv_path);
    if (have_klv) {
        std::cout << "Loaded KLV from " << klv_path << "\n";
    } else {
        std::cout << "Warning: Could not load KLV from " << klv_path << " - leave stats disabled\n";
    }

    // Load symbols from ELF
    std::cout << "Loading symbols from " << elf_path << "...\n";
    TimingSymbols syms = LoadTimingSymbols(elf_path);
    if (!syms.valid) {
        std::cerr << "ERROR: Could not load required symbols. Make sure ROMs are built with COLLECT_MOVE_STATS=1\n";
        return;
    }
    std::cout << "Symbols loaded successfully.\n";

    // Limit concurrent processes to available CPU cores
    unsigned int max_workers = std::thread::hardware_concurrency();
    if (max_workers == 0) max_workers = 4;  // Fallback if detection fails
    std::cout << "Using " << max_workers << " parallel workers\n";

    // Track active workers: pid -> (game_index, read_fd)
    std::map<pid_t, std::pair<int, int>> active_workers;
    std::vector<GameTimingResult> results(num_games);
    int next_game = 0;
    int completed = 0;

    std::cout << "Running games";
    std::cout.flush();

    while (completed < num_games) {
        // Spawn workers up to the limit
        while (active_workers.size() < max_workers && next_game < num_games) {
            int pipefd[2];
            if (pipe(pipefd) == -1) {
                std::cerr << "pipe() failed\n";
                next_game++;
                continue;
            }

            pid_t pid = fork();
            if (pid == -1) {
                close(pipefd[0]);
                close(pipefd[1]);
                next_game++;
                continue;
            }

            if (pid == 0) {
                // Child process
                close(pipefd[0]);
                RunGameToFd(rom_path, syms, next_game, pipefd[1]);
                _exit(0);
            }

            // Parent: track this worker
            close(pipefd[1]);
            active_workers[pid] = {next_game, pipefd[0]};
            next_game++;
        }

        // Wait for any child to complete
        int status;
        pid_t finished_pid = waitpid(-1, &status, 0);
        if (finished_pid > 0) {
            auto it = active_workers.find(finished_pid);
            if (it != active_workers.end()) {
                int game_idx = it->second.first;
                int read_fd = it->second.second;
                results[game_idx] = ReadGameResult(read_fd);
                active_workers.erase(it);
                completed++;

                if (completed % 100 == 0) {
                    std::cout << ".";
                    std::cout.flush();
                }
            }
        } else if (finished_pid == -1) {
            // Error or no children - break to avoid infinite loop
            break;
        }
    }
    std::cout << " done\n";

    // Count successfully completed games (game logic finished, not just process finished)
    int successful = 0;
    for (const auto& r : results) {
        if (r.completed) successful++;
    }
    std::cout << "Completed: " << successful << "/" << num_games << "\n";

    // Collect statistics
    std::vector<uint32_t> player_game_frames[2];  // Per player-game totals
    std::vector<uint32_t> play_frames;             // Per-play frames
    std::map<int, std::vector<uint32_t>> frames_by_blanks;
    std::map<int, std::vector<uint32_t>> frames_by_rack_size;
    std::map<int, std::vector<uint32_t>> frames_by_avg_leave;  // Bucketed by avg 6-tile leave
    double min_avg_leave = 1e9, max_avg_leave = -1e9;

    for (const auto& game : results) {
        if (!game.completed) continue;

        // Per-player totals for this game
        uint32_t player_totals[2] = {0, 0};

        for (int i = 0; i < game.move_count && i < MAX_MOVE_STATS; i++) {
            const MoveStats& m = game.moves[i];
            play_frames.push_back(m.frames);
            player_totals[m.player] += m.frames;

            // Stats by blank count and rack size
            frames_by_blanks[m.blank_count].push_back(m.frames);
            frames_by_rack_size[m.rack_size].push_back(m.frames);

            // Stats by average 6-tile leave value (only for 7-tile racks)
            if (have_klv && m.rack_size == 7) {
                double avg_leave = klv.GetAverage6TileLeave(m.rack);
                // Convert from eighths to points and bucket by 2.5 point increments
                double avg_leave_points = avg_leave / 8.0;
                int bucket = (int)std::floor(avg_leave_points / 2.5);  // 2.5 point buckets
                frames_by_avg_leave[bucket].push_back(m.frames);
                if (avg_leave_points < min_avg_leave) min_avg_leave = avg_leave_points;
                if (avg_leave_points > max_avg_leave) max_avg_leave = avg_leave_points;
            }
        }

        player_game_frames[0].push_back(player_totals[0]);
        player_game_frames[1].push_back(player_totals[1]);
    }

    // Combine both players' per-game times
    std::vector<uint32_t> all_player_game_frames;
    all_player_game_frames.insert(all_player_game_frames.end(),
                                   player_game_frames[0].begin(),
                                   player_game_frames[0].end());
    all_player_game_frames.insert(all_player_game_frames.end(),
                                   player_game_frames[1].begin(),
                                   player_game_frames[1].end());

    // Print histograms
    PrintHistogram("Time per player-game (frames)", all_player_game_frames);
    PrintHistogram("Time per play (frames)", play_frames);

    // Print stats by category
    PrintStatsByCategory("Time by blank count on rack", frames_by_blanks);
    PrintStatsByCategory("Time by rack size", frames_by_rack_size);

    // Print stats by average 6-tile leave value
    if (have_klv && !frames_by_avg_leave.empty()) {
        std::cout << "\nTime by avg 6-tile leave (7-tile racks only)\n";
        std::cout << std::string(60, '-') << "\n";
        std::cout << "Leave range: " << std::fixed << std::setprecision(1)
                  << min_avg_leave << " to " << max_avg_leave << " points\n";
        std::cout << std::setw(14) << "Leave Range"
                  << std::setw(10) << "Count"
                  << std::setw(12) << "Mean"
                  << std::setw(12) << "Median"
                  << std::setw(12) << "P90" << "\n";

        for (const auto& [bucket, values] : frames_by_avg_leave) {
            if (values.empty()) continue;

            double bucket_low = bucket * 2.5;
            double bucket_high = (bucket + 1) * 2.5;

            double mean = std::accumulate(values.begin(), values.end(), 0.0) / values.size();
            std::vector<uint32_t> sorted = values;
            std::sort(sorted.begin(), sorted.end());
            double median = sorted[sorted.size() / 2];
            double p90 = sorted[sorted.size() * 90 / 100];

            std::cout << std::setw(6) << std::fixed << std::setprecision(1) << bucket_low
                      << "-" << std::setw(5) << bucket_high
                      << std::setw(10) << values.size()
                      << std::setw(12) << std::setprecision(1) << mean
                      << std::setw(12) << median
                      << std::setw(12) << p90 << "\n";
        }
    }
}

// 100-game quick tests for validation
TEST(Timing, NWL23_Shadow_100Games) {
    RunBenchmark(ROM_NWL23_SHADOW_TIMING, ELF_NWL23_SHADOW_TIMING, KLV_NWL23,
                 "NWL23 Shadow", 100);
}

TEST(Timing, NWL23_NoShadow_100Games) {
    RunBenchmark(ROM_NWL23_NOSHADOW_TIMING, ELF_NWL23_NOSHADOW_TIMING, KLV_NWL23,
                 "NWL23 NoShadow", 100);
}

TEST(Timing, CSW24_Shadow_100Games) {
    RunBenchmark(ROM_CSW24_SHADOW_TIMING, ELF_CSW24_SHADOW_TIMING, KLV_CSW24,
                 "CSW24 Shadow", 100);
}

TEST(Timing, CSW24_NoShadow_100Games) {
    RunBenchmark(ROM_CSW24_NOSHADOW_TIMING, ELF_CSW24_NOSHADOW_TIMING, KLV_CSW24,
                 "CSW24 NoShadow", 100);
}

// 1000-game full benchmarks
TEST(Timing, NWL23_Shadow_1000Games) {
    RunBenchmark(ROM_NWL23_SHADOW_TIMING, ELF_NWL23_SHADOW_TIMING, KLV_NWL23,
                 "NWL23 Shadow", 1000);
}

TEST(Timing, NWL23_NoShadow_1000Games) {
    RunBenchmark(ROM_NWL23_NOSHADOW_TIMING, ELF_NWL23_NOSHADOW_TIMING, KLV_NWL23,
                 "NWL23 NoShadow", 1000);
}

TEST(Timing, CSW24_Shadow_1000Games) {
    RunBenchmark(ROM_CSW24_SHADOW_TIMING, ELF_CSW24_SHADOW_TIMING, KLV_CSW24,
                 "CSW24 Shadow", 1000);
}

TEST(Timing, CSW24_NoShadow_1000Games) {
    RunBenchmark(ROM_CSW24_NOSHADOW_TIMING, ELF_CSW24_NOSHADOW_TIMING, KLV_CSW24,
                 "CSW24 NoShadow", 1000);
}
