/*
 * Scrabble timing report generator - compares shadow vs noshadow performance
 * and generates an HTML report for analyzing hybrid movegen opportunities.
 *
 * Build ROMs with: make timing-builds
 * Run with: bazel run //tests/gxtest:scrabble_timing_report
 */

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <thread>
#include <ctime>

#include <gxtest.h>
#include "klv.h"

using gxtest::KLV;

// ROM and ELF paths
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
#ifndef ROM_NWL23_HYBRID_TIMING
#define ROM_NWL23_HYBRID_TIMING "out/scrabble-nwl23-hybrid-timing.bin"
#endif
#ifndef ROM_CSW24_HYBRID_TIMING
#define ROM_CSW24_HYBRID_TIMING "out/scrabble-csw24-hybrid-timing.bin"
#endif
#ifndef ELF_NWL23_HYBRID_TIMING
#define ELF_NWL23_HYBRID_TIMING "build/nwl23-hybrid-timing/scrabble.elf"
#endif
#ifndef ELF_CSW24_HYBRID_TIMING
#define ELF_CSW24_HYBRID_TIMING "build/csw24-hybrid-timing/scrabble.elf"
#endif
#ifndef KLV_NWL23
#define KLV_NWL23 "data/NWL23.klv16"
#endif
#ifndef KLV_CSW24
#define KLV_CSW24 "data/CSW24.klv16"
#endif

constexpr int DEFAULT_NUM_GAMES = 100;
constexpr int MAX_GAME_FRAMES = 100000;
constexpr int MAX_MOVE_STATS = 64;
constexpr uint32_t ADDR_TEST_SEED_OVERRIDE = 0xFFFFF0;

// Symbol addresses loaded from ELF
struct TimingSymbols {
    uint32_t test_game_over;
    uint32_t total_frames;
    uint32_t move_stats_count;
    uint32_t move_stats;
    bool valid;
};

// Per-move stats from ROM
struct MoveStats {
    uint16_t frames;
    uint8_t blank_count;
    uint8_t rack_size;
    uint8_t player;
    uint8_t padding;
    char rack[8];
};

// Per-game result
struct GameTimingResult {
    uint32_t seed;
    uint32_t total_frames;
    uint16_t move_count;
    bool completed;
    uint8_t padding;
    MoveStats moves[MAX_MOVE_STATS];
};

// Aggregated stats for a bucket
struct BucketStats {
    int count = 0;
    double sum = 0;
    double sum_sq = 0;
    std::vector<uint32_t> values;

    void Add(uint32_t v) {
        count++;
        sum += v;
        sum_sq += (double)v * v;
        values.push_back(v);
    }

    double Mean() const { return count > 0 ? sum / count : 0; }

    double Median() const {
        if (values.empty()) return 0;
        std::vector<uint32_t> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    double P90() const {
        if (values.empty()) return 0;
        std::vector<uint32_t> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() * 90 / 100];
    }

    uint32_t Max() const {
        if (values.empty()) return 0;
        return *std::max_element(values.begin(), values.end());
    }
};

// Helper to check if a letter is a vowel (A, E, I, O, U)
bool IsVowel(char c) {
    c = toupper(c);
    return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U';
}

// Analyze a 7-tile rack: count vowels, consonants, check for S
struct RackAnalysis {
    int vowels = 0;
    int consonants = 0;
    bool has_s = false;
    int blanks = 0;

    static RackAnalysis Analyze(const char* rack, int rack_size) {
        RackAnalysis r;
        for (int i = 0; i < rack_size && rack[i] != '\0'; i++) {
            char c = rack[i];
            if (c == '?') {
                r.blanks++;
            } else if (toupper(c) == 'S') {
                r.has_s = true;
                r.consonants++;
            } else if (IsVowel(c)) {
                r.vowels++;
            } else if (isalpha(c)) {
                r.consonants++;
            }
        }
        return r;
    }
};

// Stats for one variant (shadow or noshadow)
struct VariantStats {
    std::map<int, BucketStats> by_blanks;      // Key: blank count (0, 1, 2)
    std::map<int, BucketStats> by_rack_size;   // Key: rack size (1-7)
    std::map<int, BucketStats> by_leave;       // Key: leave bucket (2.5 pt increments)
    std::map<int, BucketStats> by_has_s;       // Key: 0=no S, 1=has S
    std::map<std::string, BucketStats> by_vcb; // Key: "V/C/B" fingerprint (e.g., "3/4/0")
    BucketStats overall;
    double min_leave = 1e9;
    double max_leave = -1e9;
};

// Results for one lexicon
struct LexiconResults {
    std::string name;
    VariantStats shadow;
    VariantStats noshadow;
    VariantStats hybrid;
};

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

TimingSymbols LoadTimingSymbols(const char* elf_path) {
    TimingSymbols syms = {0, 0, 0, 0, false};
    syms.test_game_over = LoadSymbolAddress(elf_path, "test_game_over");
    syms.total_frames = LoadSymbolAddress(elf_path, "total_frames");
    syms.move_stats_count = LoadSymbolAddress(elf_path, "move_stats_count");
    syms.move_stats = LoadSymbolAddress(elf_path, "move_stats");
    syms.valid = (syms.test_game_over != 0 && syms.total_frames != 0);
    return syms;
}

void RunGameToFd(const char* rom_path, const TimingSymbols& syms, uint32_t seed, int write_fd) {
    GameTimingResult result = {};
    result.seed = seed;
    result.completed = false;

    if (syms.valid) {
        GX::Emulator emu;
        if (emu.LoadRom(rom_path)) {
            emu.WriteLong(ADDR_TEST_SEED_OVERRIDE, seed);
            int frames = emu.RunUntilMemoryEquals(syms.test_game_over, 1, MAX_GAME_FRAMES);
            if (frames >= 0) {
                result.total_frames = emu.ReadLong(syms.total_frames);
                result.completed = true;
                if (syms.move_stats_count != 0 && syms.move_stats != 0) {
                    result.move_count = emu.ReadWord(syms.move_stats_count);
                    for (int i = 0; i < result.move_count && i < MAX_MOVE_STATS; i++) {
                        uint32_t addr = syms.move_stats + i * 14;
                        result.moves[i].frames = emu.ReadWord(addr);
                        result.moves[i].blank_count = emu.ReadByte(addr + 2);
                        result.moves[i].rack_size = emu.ReadByte(addr + 3);
                        result.moves[i].player = emu.ReadByte(addr + 4);
                        for (int j = 0; j < 8; j++) {
                            result.moves[i].rack[j] = emu.ReadByte(addr + 6 + j);
                        }
                    }
                }
            }
        }
    }
    write(write_fd, &result, sizeof(result));
    close(write_fd);
}

GameTimingResult ReadGameResult(int read_fd) {
    GameTimingResult result = {};
    read(read_fd, &result, sizeof(result));
    close(read_fd);
    return result;
}

// Run benchmark and collect stats
VariantStats RunBenchmarkVariant(const char* rom_path, const char* elf_path,
                                  const KLV& klv, const std::string& name, int num_games) {
    VariantStats stats;

    std::cerr << "  Running " << name << " (" << num_games << " games)... ";
    std::cerr.flush();

    TimingSymbols syms = LoadTimingSymbols(elf_path);
    if (!syms.valid) {
        std::cerr << "ERROR: symbols not found\n";
        return stats;
    }

    unsigned int max_workers = std::thread::hardware_concurrency();
    if (max_workers == 0) max_workers = 4;

    std::map<pid_t, std::pair<int, int>> active_workers;
    std::vector<GameTimingResult> results(num_games);
    int next_game = 0;
    int completed = 0;

    while (completed < num_games) {
        while (active_workers.size() < max_workers && next_game < num_games) {
            int pipefd[2];
            if (pipe(pipefd) == -1) { next_game++; continue; }

            pid_t pid = fork();
            if (pid == -1) {
                close(pipefd[0]);
                close(pipefd[1]);
                next_game++;
                continue;
            }
            if (pid == 0) {
                close(pipefd[0]);
                RunGameToFd(rom_path, syms, next_game, pipefd[1]);
                _exit(0);
            }
            close(pipefd[1]);
            active_workers[pid] = {next_game, pipefd[0]};
            next_game++;
        }

        int status;
        pid_t finished_pid = waitpid(-1, &status, 0);
        if (finished_pid > 0) {
            auto it = active_workers.find(finished_pid);
            if (it != active_workers.end()) {
                results[it->second.first] = ReadGameResult(it->second.second);
                active_workers.erase(it);
                completed++;
            }
        } else if (finished_pid == -1) {
            // Error or no children - break to avoid infinite loop
            break;
        }
    }

    // Collect stats
    int successful = 0;
    for (const auto& game : results) {
        if (!game.completed) continue;
        successful++;

        for (int i = 0; i < game.move_count && i < MAX_MOVE_STATS; i++) {
            const MoveStats& m = game.moves[i];
            stats.overall.Add(m.frames);
            stats.by_blanks[m.blank_count].Add(m.frames);
            stats.by_rack_size[m.rack_size].Add(m.frames);

            // Rack composition stats (all rack sizes)
            RackAnalysis ra = RackAnalysis::Analyze(m.rack, m.rack_size);
            stats.by_has_s[ra.has_s ? 1 : 0].Add(m.frames);

            // VCB fingerprint: "vowels/consonants/blanks"
            std::string vcb = std::to_string(ra.vowels) + "/" +
                              std::to_string(ra.consonants) + "/" +
                              std::to_string(ra.blanks);
            stats.by_vcb[vcb].Add(m.frames);

            if (m.rack_size == 7) {
                // Leave value stats (2.5 point buckets) - only for 7-tile racks
                double avg_leave = klv.GetAverage6TileLeave(m.rack);
                double avg_leave_points = avg_leave / 8.0;
                int bucket = (int)std::floor(avg_leave_points / 2.5);  // 2.5 point buckets
                stats.by_leave[bucket].Add(m.frames);
                if (avg_leave_points < stats.min_leave) stats.min_leave = avg_leave_points;
                if (avg_leave_points > stats.max_leave) stats.max_leave = avg_leave_points;
            }
        }
    }

    std::cerr << successful << "/" << num_games << " completed\n";
    return stats;
}

// Helper to generate a table row with modern styling
void WriteTableRow(std::ofstream& out, const std::string& label,
                   double s_mean, double s_med, uint32_t s_max,
                   double n_mean, double n_med, uint32_t n_max,
                   double h_mean,
                   int count, int max_count) {
    std::string faster = (s_mean < n_mean) ? "Shadow" : "NoShadow";
    double speedup = (s_mean < n_mean) ? (n_mean / std::max(s_mean, 1.0)) : (s_mean / std::max(n_mean, 1.0));
    std::string row_class = (s_mean < n_mean) ? "shadow" : "noshadow";
    if (std::abs(speedup - 1.0) < 0.05) { row_class = "neutral"; faster = "-"; }

    bool significant = speedup >= 1.15;
    double count_opacity = std::min(0.4, 0.1 + 0.3 * count / std::max(max_count, 1));
    int bar_width = std::min(100, (int)((speedup - 1.0) * 100));

    out << "<tr class=\"" << row_class << (significant ? " significant" : "") << "\" "
        << "data-speedup=\"" << std::fixed << std::setprecision(3) << speedup << "\" "
        << "data-count=\"" << count << "\">\n";
    out << "  <td>" << label << "</td>\n";
    out << "  <td class=\"count\" style=\"background:rgba(99,102,241," << std::setprecision(2) << count_opacity << ")\">" << count << "</td>\n";
    out << "  <td class=\"shadow-col\">" << std::setprecision(1) << s_mean << "</td>\n";
    out << "  <td class=\"shadow-col\">" << s_med << "</td>\n";
    out << "  <td class=\"shadow-col\">" << s_max << "</td>\n";
    out << "  <td class=\"noshadow-col\">" << n_mean << "</td>\n";
    out << "  <td class=\"noshadow-col\">" << n_med << "</td>\n";
    out << "  <td class=\"noshadow-col\">" << n_max << "</td>\n";
    out << "  <td><span class=\"winner " << row_class << "\">" << faster << "</span></td>\n";
    out << "  <td class=\"speedup-cell\">"
        << "<div class=\"speedup-bar\" style=\"width:" << bar_width << "%\"></div>"
        << "<span class=\"pill" << (speedup >= 1.5 ? " high" : "") << "\">" << std::setprecision(2) << speedup << "x</span>"
        << "</td>\n";
    out << "  <td class=\"hybrid-col\">" << std::setprecision(1) << h_mean << "</td>\n";
    out << "</tr>\n";
}

// Generate HTML report
void GenerateHTMLReport(const std::vector<LexiconResults>& results,
                        const std::string& output_path, int num_games) {
    std::ofstream out(output_path);
    if (!out) {
        std::cerr << "ERROR: Could not write to " << output_path << "\n";
        return;
    }

    time_t now = time(nullptr);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));

    // Write HTML header with modern CSS
    out << R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Scrabble Movegen Performance Dashboard</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap" rel="stylesheet">
<style>
* { box-sizing: border-box; }
body {
  font-family: 'Inter', system-ui, -apple-system, sans-serif;
  margin: 0; padding: 20px;
  background: linear-gradient(135deg, #f5f7fa 0%, #e4e8ec 100%);
  min-height: 100vh;
  color: #1f2937;
}
.container { max-width: 1200px; margin: 0 auto; }
header { text-align: center; margin-bottom: 30px; }
h1 { font-size: 1.8rem; font-weight: 700; color: #111827; margin: 0 0 8px 0; }
.meta { color: #6b7280; font-size: 0.875rem; }

/* Recommendation Box */
.recommendation {
  background: linear-gradient(135deg, #fef3c7 0%, #fde68a 100%);
  border: 1px solid #f59e0b;
  border-radius: 12px;
  padding: 16px 20px;
  margin-bottom: 24px;
  display: flex;
  align-items: flex-start;
  gap: 12px;
}
.recommendation .icon { font-size: 1.5rem; }
.recommendation h3 { margin: 0 0 6px 0; font-size: 0.95rem; font-weight: 600; color: #92400e; }
.recommendation p { margin: 0; font-size: 0.875rem; color: #78350f; line-height: 1.5; }

/* Tabs */
.tabs { display: flex; gap: 8px; margin-bottom: 20px; }
.tab {
  padding: 10px 24px;
  background: white;
  border: 1px solid #e5e7eb;
  border-radius: 8px 8px 0 0;
  cursor: pointer;
  font-weight: 500;
  color: #6b7280;
  transition: all 0.2s;
}
.tab:hover { background: #f9fafb; }
.tab.active { background: white; color: #111827; border-bottom-color: white; position: relative; z-index: 1; }
.tab-content { display: none; }
.tab-content.active { display: block; }

/* Cards */
.card {
  background: white;
  border-radius: 12px;
  box-shadow: 0 1px 3px rgba(0,0,0,0.1), 0 1px 2px rgba(0,0,0,0.06);
  padding: 20px;
  margin-bottom: 20px;
}
.card h3 { margin: 0 0 16px 0; font-size: 1rem; font-weight: 600; color: #374151; }

/* Summary Stats */
.stats-row { display: flex; gap: 16px; margin-bottom: 20px; flex-wrap: wrap; }
.stat-box {
  flex: 1;
  min-width: 200px;
  background: #f9fafb;
  border-radius: 8px;
  padding: 16px;
  text-align: center;
}
.stat-box .label { font-size: 0.75rem; color: #6b7280; text-transform: uppercase; letter-spacing: 0.05em; }
.stat-box .value { font-size: 1.5rem; font-weight: 700; color: #111827; margin-top: 4px; }
.stat-box.shadow .value { color: #166534; }
.stat-box.noshadow .value { color: #1e40af; }

/* Tables */
table { width: 100%; border-collapse: collapse; font-size: 0.875rem; }
th {
  text-align: left;
  padding: 10px 12px;
  background: #f9fafb;
  font-weight: 600;
  color: #374151;
  border-bottom: 2px solid #e5e7eb;
  cursor: pointer;
  user-select: none;
  white-space: nowrap;
}
th:hover { background: #f3f4f6; }
th.sorted-asc::after { content: ' ▲'; font-size: 0.7em; }
th.sorted-desc::after { content: ' ▼'; font-size: 0.7em; }
th.shadow-col { background: #dcfce7; border-bottom-color: #166534; }
th.noshadow-col { background: #dbeafe; border-bottom-color: #1e40af; }
th.hybrid-col { background: #fef3c7; border-bottom-color: #f59e0b; }
td { padding: 10px 12px; border-bottom: 1px solid #f3f4f6; }
td:not(:first-child) { text-align: right; }
td.shadow-col { background: rgba(220, 252, 231, 0.5); }
td.noshadow-col { background: rgba(219, 234, 254, 0.5); }
td.hybrid-col { background: rgba(254, 243, 199, 0.5); font-weight: 600; }
tr.shadow { background: #dcfce7; }
tr.noshadow { background: #dbeafe; }
tr.neutral { background: white; }
tr.shadow:hover { background: #bbf7d0; }
tr.noshadow:hover { background: #bfdbfe; }
tr.neutral:hover { background: #f9fafb; }
tr.significant { font-weight: 500; }

/* Winner badges */
.winner {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 0.75rem;
  font-weight: 600;
}
.winner.shadow { background: #dcfce7; color: #166534; }
.winner.noshadow { background: #dbeafe; color: #1e40af; }
.winner.neutral { background: #f3f4f6; color: #6b7280; }

/* Speedup pills with bar */
.speedup-cell { position: relative; min-width: 100px; }
.speedup-bar {
  position: absolute;
  left: 0; top: 50%;
  transform: translateY(-50%);
  height: 60%;
  background: rgba(99, 102, 241, 0.15);
  border-radius: 2px;
}
.pill {
  position: relative;
  display: inline-block;
  padding: 3px 10px;
  border-radius: 12px;
  font-size: 0.8rem;
  font-weight: 600;
  background: #e5e7eb;
  color: #374151;
}
.pill.high { background: #fbbf24; color: #78350f; }

/* Count heatmap handled via inline style */
.count { font-variant-numeric: tabular-nums; }

/* Filter */
.filter-bar {
  display: flex;
  gap: 12px;
  margin-bottom: 16px;
  align-items: center;
  flex-wrap: wrap;
}
.filter-bar label { font-size: 0.875rem; color: #6b7280; }
.filter-bar select, .filter-bar input {
  padding: 6px 12px;
  border: 1px solid #d1d5db;
  border-radius: 6px;
  font-size: 0.875rem;
}
</style>
</head>
<body>
<div class="container">
<header>
  <h1>Scrabble Movegen Performance Dashboard</h1>
  <p class="meta">Shadow vs NoShadow Comparison | Generated: )" << time_str << " | " << num_games << R"( games per variant</p>
</header>

<div class="recommendation">
  <span class="icon">💡</span>
  <div>
    <h3>Hybrid Strategy Recommendation</h3>
    <p><strong>Use NoShadow</strong> for 0 blanks (faster by ~15-20%). <strong>Switch to Shadow</strong> for 1+ blanks (faster by 20-130%). The crossover point is clear: blanks multiply the search space exponentially, making Shadow's precomputation worthwhile.</p>
    <p style="margin-top:8px;font-size:0.8rem;color:#92400e;"><em>Note: More granular filters (vowel/consonant counts, leave values, S presence) were analyzed but showed no consistent signal beyond blank count. The V/C/B breakdown tables are provided below for reference but the hybrid decision should use only blank count.</em></p>
  </div>
</div>

)";

    // Calculate overall stats across all lexicons using actual hybrid data
    double total_shadow = 0, total_noshadow = 0, total_hybrid = 0;
    int total_count = 0;
    for (const auto& lex : results) {
        total_shadow += lex.shadow.overall.sum;
        total_noshadow += lex.noshadow.overall.sum;
        total_hybrid += lex.hybrid.overall.sum;
        total_count += lex.hybrid.overall.count;
    }
    double overall_shadow = total_count > 0 ? total_shadow / total_count : 0;
    double overall_noshadow = total_count > 0 ? total_noshadow / total_count : 0;
    double overall_hybrid = total_count > 0 ? total_hybrid / total_count : 0;

    out << "<div class=\"card\" style=\"margin-bottom:20px;background:linear-gradient(135deg,#f0fdf4 0%,#dbeafe 100%);border:1px solid #86efac\">\n";
    out << "  <h3 style=\"margin-bottom:12px\">Overall Hybrid Performance (Actual)</h3>\n";
    out << "  <div class=\"stats-row\">\n";
    out << "    <div class=\"stat-box shadow\"><div class=\"label\">Shadow Only</div><div class=\"value\">"
        << std::fixed << std::setprecision(1) << overall_shadow << " frames</div></div>\n";
    out << "    <div class=\"stat-box noshadow\"><div class=\"label\">NoShadow Only</div><div class=\"value\">"
        << overall_noshadow << " frames</div></div>\n";
    out << "    <div class=\"stat-box\" style=\"background:#fef3c7\"><div class=\"label\">Hybrid (Actual)</div><div class=\"value\" style=\"color:#92400e\">"
        << overall_hybrid << " frames</div></div>\n";
    out << "  </div>\n";
    out << "  <p style=\"margin:12px 0 0 0;font-size:0.875rem;color:#374151\">Hybrid improves on Shadow-only by <strong>"
        << std::setprecision(1) << ((overall_shadow / overall_hybrid - 1) * 100) << "%</strong> and on NoShadow-only by <strong>"
        << ((overall_noshadow / overall_hybrid - 1) * 100) << "%</strong> across " << total_count << " moves.</p>\n";
    out << "</div>\n\n";

    out << "<div class=\"tabs\">\n";

    // Generate tabs
    for (size_t i = 0; i < results.size(); i++) {
        out << "  <div class=\"tab" << (i == 0 ? " active" : "") << "\" onclick=\"showTab(" << i << ")\">"
            << results[i].name << "</div>\n";
    }
    out << "</div>\n\n";

    // Generate content for each lexicon
    for (size_t idx = 0; idx < results.size(); idx++) {
        const auto& lex = results[idx];
        out << "<div class=\"tab-content" << (idx == 0 ? " active" : "") << "\" id=\"tab" << idx << "\">\n";

        // Use actual hybrid data
        double hybrid_mean = lex.hybrid.overall.Mean();

        // Summary stats
        double hybrid_vs_shadow = lex.shadow.overall.Mean() / std::max(hybrid_mean, 1.0);
        double hybrid_vs_noshadow = lex.noshadow.overall.Mean() / std::max(hybrid_mean, 1.0);
        out << "<div class=\"stats-row\">\n";
        out << "  <div class=\"stat-box shadow\"><div class=\"label\">Shadow Mean</div><div class=\"value\">"
            << std::fixed << std::setprecision(1) << lex.shadow.overall.Mean() << "</div></div>\n";
        out << "  <div class=\"stat-box noshadow\"><div class=\"label\">NoShadow Mean</div><div class=\"value\">"
            << lex.noshadow.overall.Mean() << "</div></div>\n";
        out << "  <div class=\"stat-box\" style=\"background:#fef3c7\"><div class=\"label\">Hybrid (Actual)</div><div class=\"value\" style=\"color:#92400e\">"
            << hybrid_mean << "</div></div>\n";
        out << "  <div class=\"stat-box\"><div class=\"label\">Hybrid vs Shadow</div><div class=\"value\">"
            << std::setprecision(2) << hybrid_vs_shadow << "x</div></div>\n";
        out << "  <div class=\"stat-box\"><div class=\"label\">Hybrid vs NoShadow</div><div class=\"value\">"
            << hybrid_vs_noshadow << "x</div></div>\n";
        out << "</div>\n\n";


        // Table header macro - with colored column groups
        auto writeTableHeader = [&out]() {
            out << "<thead><tr>"
                << "<th>Category</th><th>Count</th>"
                << "<th class=\"shadow-col\">Shadow Mean</th><th class=\"shadow-col\">Shadow Med</th><th class=\"shadow-col\">Shadow Max</th>"
                << "<th class=\"noshadow-col\">NoShadow Mean</th><th class=\"noshadow-col\">NoShadow Med</th><th class=\"noshadow-col\">NoShadow Max</th>"
                << "<th>Winner</th><th>Speedup</th>"
                << "<th class=\"hybrid-col\">Hybrid</th>"
                << "</tr></thead>\n<tbody>\n";
        };

        // By blank count card
        out << "<div class=\"card\">\n<h3>By Blank Count</h3>\n";
        out << "<table class=\"sortable\">\n";
        writeTableHeader();
        int max_blank_count = 0;
        for (int b = 0; b <= 2; b++) {
            auto sit = lex.shadow.by_blanks.find(b);
            if (sit != lex.shadow.by_blanks.end()) max_blank_count = std::max(max_blank_count, sit->second.count);
        }
        for (int blanks = 0; blanks <= 2; blanks++) {
            auto sit = lex.shadow.by_blanks.find(blanks);
            auto nit = lex.noshadow.by_blanks.find(blanks);
            if (sit == lex.shadow.by_blanks.end() && nit == lex.noshadow.by_blanks.end()) continue;
            double s_mean = (sit != lex.shadow.by_blanks.end()) ? sit->second.Mean() : 0;
            double s_med = (sit != lex.shadow.by_blanks.end()) ? sit->second.Median() : 0;
            uint32_t s_max = (sit != lex.shadow.by_blanks.end()) ? sit->second.Max() : 0;
            double n_mean = (nit != lex.noshadow.by_blanks.end()) ? nit->second.Mean() : 0;
            double n_med = (nit != lex.noshadow.by_blanks.end()) ? nit->second.Median() : 0;
            uint32_t n_max = (nit != lex.noshadow.by_blanks.end()) ? nit->second.Max() : 0;
            auto hit = lex.hybrid.by_blanks.find(blanks);
            double h_mean = (hit != lex.hybrid.by_blanks.end()) ? hit->second.Mean() : 0;
            int count = std::max((sit != lex.shadow.by_blanks.end()) ? sit->second.count : 0,
                                  (nit != lex.noshadow.by_blanks.end()) ? nit->second.count : 0);
            WriteTableRow(out, std::to_string(blanks) + " blank" + (blanks != 1 ? "s" : ""),
                          s_mean, s_med, s_max, n_mean, n_med, n_max, h_mean, count, max_blank_count);
        }
        out << "</tbody></table>\n</div>\n\n";

        // By VCB fingerprint card (main analysis table)
        out << "<div class=\"card\">\n<h3>By Rack Composition (Vowels/Consonants/Blanks)</h3>\n";
        out << "<div class=\"filter-bar\">\n";
        out << "  <label>Filter:</label>\n";
        out << "  <select onchange=\"filterTable(this, 'vcb" << idx << "')\">\n";
        out << "    <option value=\"all\">All rows</option>\n";
        out << "    <option value=\"significant\">Significant only (>1.15x)</option>\n";
        out << "    <option value=\"shadow\">Shadow faster</option>\n";
        out << "    <option value=\"noshadow\">NoShadow faster</option>\n";
        out << "  </select>\n";
        out << "</div>\n";
        out << "<table class=\"sortable\" id=\"vcb" << idx << "\">\n";
        writeTableHeader();

        std::set<std::string> all_vcb;
        for (const auto& [vcb, _] : lex.shadow.by_vcb) all_vcb.insert(vcb);
        for (const auto& [vcb, _] : lex.noshadow.by_vcb) all_vcb.insert(vcb);

        std::vector<std::string> sorted_vcb(all_vcb.begin(), all_vcb.end());
        std::sort(sorted_vcb.begin(), sorted_vcb.end(), [](const std::string& a, const std::string& b) {
            int av, ac, ab, bv, bc, bb;
            sscanf(a.c_str(), "%d/%d/%d", &av, &ac, &ab);
            sscanf(b.c_str(), "%d/%d/%d", &bv, &bc, &bb);
            if (ab != bb) return ab < bb;
            if (av != bv) return av < bv;
            return ac < bc;
        });

        int max_vcb_count = 0;
        for (const auto& vcb : sorted_vcb) {
            auto sit = lex.shadow.by_vcb.find(vcb);
            if (sit != lex.shadow.by_vcb.end()) max_vcb_count = std::max(max_vcb_count, sit->second.count);
        }

        for (const std::string& vcb : sorted_vcb) {
            auto sit = lex.shadow.by_vcb.find(vcb);
            auto nit = lex.noshadow.by_vcb.find(vcb);
            auto hit = lex.hybrid.by_vcb.find(vcb);
            double s_mean = (sit != lex.shadow.by_vcb.end()) ? sit->second.Mean() : 0;
            double s_med = (sit != lex.shadow.by_vcb.end()) ? sit->second.Median() : 0;
            uint32_t s_max = (sit != lex.shadow.by_vcb.end()) ? sit->second.Max() : 0;
            double n_mean = (nit != lex.noshadow.by_vcb.end()) ? nit->second.Mean() : 0;
            double n_med = (nit != lex.noshadow.by_vcb.end()) ? nit->second.Median() : 0;
            uint32_t n_max = (nit != lex.noshadow.by_vcb.end()) ? nit->second.Max() : 0;
            double h_mean = (hit != lex.hybrid.by_vcb.end()) ? hit->second.Mean() : 0;
            int count = std::max((sit != lex.shadow.by_vcb.end()) ? sit->second.count : 0,
                                  (nit != lex.noshadow.by_vcb.end()) ? nit->second.count : 0);
            if (count < 5) continue;
            WriteTableRow(out, vcb, s_mean, s_med, s_max, n_mean, n_med, n_max, h_mean, count, max_vcb_count);
        }
        out << "</tbody></table>\n</div>\n\n";

        // By leave value card
        out << "<div class=\"card\">\n<h3>By Average 6-Tile Leave (7-tile racks)</h3>\n";
        out << "<p style=\"color:#6b7280;font-size:0.875rem;margin-bottom:12px;\">Leave range: "
            << std::fixed << std::setprecision(1) << std::min(lex.shadow.min_leave, lex.noshadow.min_leave)
            << " to " << std::max(lex.shadow.max_leave, lex.noshadow.max_leave) << " points</p>\n";
        out << "<table class=\"sortable\">\n";
        writeTableHeader();

        std::set<int> all_buckets;
        for (const auto& [bucket, _] : lex.shadow.by_leave) all_buckets.insert(bucket);
        for (const auto& [bucket, _] : lex.noshadow.by_leave) all_buckets.insert(bucket);

        int max_leave_count = 0;
        for (int b : all_buckets) {
            auto sit = lex.shadow.by_leave.find(b);
            if (sit != lex.shadow.by_leave.end()) max_leave_count = std::max(max_leave_count, sit->second.count);
        }

        for (int bucket : all_buckets) {
            auto sit = lex.shadow.by_leave.find(bucket);
            auto nit = lex.noshadow.by_leave.find(bucket);
            auto hit = lex.hybrid.by_leave.find(bucket);
            double s_mean = (sit != lex.shadow.by_leave.end()) ? sit->second.Mean() : 0;
            double s_med = (sit != lex.shadow.by_leave.end()) ? sit->second.Median() : 0;
            uint32_t s_max = (sit != lex.shadow.by_leave.end()) ? sit->second.Max() : 0;
            double n_mean = (nit != lex.noshadow.by_leave.end()) ? nit->second.Mean() : 0;
            double n_med = (nit != lex.noshadow.by_leave.end()) ? nit->second.Median() : 0;
            uint32_t n_max = (nit != lex.noshadow.by_leave.end()) ? nit->second.Max() : 0;
            double h_mean = (hit != lex.hybrid.by_leave.end()) ? hit->second.Mean() : 0;
            int count = std::max((sit != lex.shadow.by_leave.end()) ? sit->second.count : 0,
                                  (nit != lex.noshadow.by_leave.end()) ? nit->second.count : 0);
            if (count < 3) continue;
            double bucket_low = bucket * 2.5;
            double bucket_high = (bucket + 1) * 2.5;
            std::ostringstream label;
            label << std::fixed << std::setprecision(1) << bucket_low << " to " << bucket_high;
            WriteTableRow(out, label.str(), s_mean, s_med, s_max, n_mean, n_med, n_max, h_mean, count, max_leave_count);
        }
        out << "</tbody></table>\n</div>\n\n";

        out << "</div>\n";  // tab-content
    }

    // JavaScript for tabs, sorting, and filtering
    out << R"(
<script>
function showTab(idx) {
  document.querySelectorAll('.tab').forEach((t,i) => t.classList.toggle('active', i===idx));
  document.querySelectorAll('.tab-content').forEach((c,i) => c.classList.toggle('active', i===idx));
}

function filterTable(select, tableId) {
  const table = document.getElementById(tableId);
  const rows = table.querySelectorAll('tbody tr');
  const filter = select.value;
  rows.forEach(row => {
    const isSignificant = row.classList.contains('significant');
    const isShadow = row.classList.contains('shadow');
    const isNoShadow = row.classList.contains('noshadow');
    let show = true;
    if (filter === 'significant') show = isSignificant;
    else if (filter === 'shadow') show = isShadow;
    else if (filter === 'noshadow') show = isNoShadow;
    row.style.display = show ? '' : 'none';
  });
}

document.querySelectorAll('table.sortable th').forEach((th, colIdx) => {
  th.addEventListener('click', () => {
    const table = th.closest('table');
    const tbody = table.querySelector('tbody');
    const rows = Array.from(tbody.querySelectorAll('tr'));
    const isAsc = th.classList.contains('sorted-asc');
    table.querySelectorAll('th').forEach(h => h.classList.remove('sorted-asc', 'sorted-desc'));
    th.classList.add(isAsc ? 'sorted-desc' : 'sorted-asc');
    rows.sort((a, b) => {
      let aVal = a.children[colIdx]?.textContent.trim() || '';
      let bVal = b.children[colIdx]?.textContent.trim() || '';
      const aNum = parseFloat(aVal.replace(/[^0-9.-]/g, ''));
      const bNum = parseFloat(bVal.replace(/[^0-9.-]/g, ''));
      if (!isNaN(aNum) && !isNaN(bNum)) {
        return isAsc ? bNum - aNum : aNum - bNum;
      }
      return isAsc ? bVal.localeCompare(aVal) : aVal.localeCompare(bVal);
    });
    rows.forEach(row => tbody.appendChild(row));
  });
});
</script>

</div>
</body>
</html>
)";

    out.close();
    std::cerr << "Report written to: " << output_path << "\n";
}

int main(int argc, char* argv[]) {
    int num_games = DEFAULT_NUM_GAMES;
    std::string output_path = "timing_report.html";

    // Parse args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-n" && i + 1 < argc) {
            num_games = std::atoi(argv[++i]);
        } else if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cerr << "Usage: " << argv[0] << " [-n NUM_GAMES] [-o OUTPUT_FILE]\n";
            std::cerr << "  -n NUM_GAMES   Number of games per variant (default: 100)\n";
            std::cerr << "  -o OUTPUT_FILE Output HTML file (default: timing_report.html)\n";
            return 0;
        }
    }

    std::cerr << "Scrabble Timing Report Generator\n";
    std::cerr << "Games per variant: " << num_games << "\n";
    std::cerr << "Output: " << output_path << "\n\n";

    // Load KLV files
    KLV klv_nwl, klv_csw;
    if (!klv_nwl.Load(KLV_NWL23)) {
        std::cerr << "ERROR: Could not load " << KLV_NWL23 << "\n";
        return 1;
    }
    if (!klv_csw.Load(KLV_CSW24)) {
        std::cerr << "ERROR: Could not load " << KLV_CSW24 << "\n";
        return 1;
    }

    std::vector<LexiconResults> results;

    // NWL23
    std::cerr << "NWL23:\n";
    LexiconResults nwl;
    nwl.name = "NWL23";
    nwl.shadow = RunBenchmarkVariant(ROM_NWL23_SHADOW_TIMING, ELF_NWL23_SHADOW_TIMING,
                                      klv_nwl, "Shadow", num_games);
    nwl.noshadow = RunBenchmarkVariant(ROM_NWL23_NOSHADOW_TIMING, ELF_NWL23_NOSHADOW_TIMING,
                                        klv_nwl, "NoShadow", num_games);
    nwl.hybrid = RunBenchmarkVariant(ROM_NWL23_HYBRID_TIMING, ELF_NWL23_HYBRID_TIMING,
                                      klv_nwl, "Hybrid", num_games);
    results.push_back(nwl);

    // CSW24
    std::cerr << "\nCSW24:\n";
    LexiconResults csw;
    csw.name = "CSW24";
    csw.shadow = RunBenchmarkVariant(ROM_CSW24_SHADOW_TIMING, ELF_CSW24_SHADOW_TIMING,
                                      klv_csw, "Shadow", num_games);
    csw.noshadow = RunBenchmarkVariant(ROM_CSW24_NOSHADOW_TIMING, ELF_CSW24_NOSHADOW_TIMING,
                                        klv_csw, "NoShadow", num_games);
    csw.hybrid = RunBenchmarkVariant(ROM_CSW24_HYBRID_TIMING, ELF_CSW24_HYBRID_TIMING,
                                      klv_csw, "Hybrid", num_games);
    results.push_back(csw);

    // Generate report
    std::cerr << "\n";
    GenerateHTMLReport(results, output_path, num_games);

    // Generate summary JSON for CI comparison
    std::string json_path = output_path;
    size_t dot_pos = json_path.rfind('.');
    if (dot_pos != std::string::npos) {
        json_path = json_path.substr(0, dot_pos) + "_summary.json";
    } else {
        json_path += "_summary.json";
    }

    std::ofstream json_out(json_path);
    if (json_out) {
        json_out << std::fixed << std::setprecision(1);
        json_out << "{\n";
        json_out << "  \"num_games\": " << num_games << ",\n";
        json_out << "  \"lexicons\": [\n";
        for (size_t i = 0; i < results.size(); i++) {
            const auto& lex = results[i];
            json_out << "    {\n";
            json_out << "      \"name\": \"" << lex.name << "\",\n";
            json_out << "      \"shadow_mean\": " << lex.shadow.overall.Mean() << ",\n";
            json_out << "      \"noshadow_mean\": " << lex.noshadow.overall.Mean() << ",\n";
            json_out << "      \"hybrid_mean\": " << lex.hybrid.overall.Mean() << ",\n";
            json_out << "      \"move_count\": " << lex.hybrid.overall.count << "\n";
            json_out << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        json_out << "  ]\n";
        json_out << "}\n";
        json_out.close();
        std::cerr << "Summary JSON written to: " << json_path << "\n";
    } else {
        std::cerr << "ERROR: Failed to open JSON summary output file: " << json_path << "\n";
    }

    return 0;
}
