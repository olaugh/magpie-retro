/**
 * Scrabble Genesis ROM Tests
 *
 * Tests that verify correct game behavior by asserting on final scores
 * and frame counts for deterministic seeds. These tests catch regressions
 * in move generation, scoring, game logic, and performance.
 *
 * Each test loads a fresh ROM and sets a seed via test_seed_override.
 */

#include <gxtest.h>
#include "scrabble_symbols.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>

namespace {

// Maximum frames to wait for a game to complete (~8 minutes at 60fps)
constexpr int MAX_GAME_FRAMES = 30000;

// Number of seeds to test
constexpr int NUM_SEEDS = 10;

// ---------------------------------------------------------------------------
// Expected Results for NWL23 (baseline from known-good build)
// ---------------------------------------------------------------------------

struct ExpectedResult {
    int16_t p0_score;
    int16_t p1_score;
    uint32_t shadow_frames;
    uint32_t noshadow_frames;
};

// Expected results for seeds 0-9 (scores must match between shadow/noshadow)
// Frame counts reflect score-in-eighths, MULT_SMALL, and word_multiplier switch optimizations
constexpr ExpectedResult NWL23_EXPECTED[NUM_SEEDS] = {
    {431, 467, 14204, 13953},  // Seed 0
    {456, 463,  8906,  8775},  // Seed 1
    {620, 344,  5766,  5788},  // Seed 2
    {433, 411,  9983,  9428},  // Seed 3
    {415, 451,  6702,  5692},  // Seed 4
    {361, 458, 11700, 12419},  // Seed 5
    {365, 506,  9940,  9536},  // Seed 6
    {522, 440, 12387, 12096},  // Seed 7
    {569, 308,  9002, 14290},  // Seed 8
    {406, 483, 11731, 11521},  // Seed 9
};

// ---------------------------------------------------------------------------
// Test Helper
// ---------------------------------------------------------------------------

struct GameResult {
    uint32_t seed;
    int16_t p0_score;
    int16_t p1_score;
    uint32_t frames;
    bool completed;
};

// Run a single game and write the result to a file descriptor
void RunGameToFd(const char* rom_path, uint32_t seed, int write_fd) {
    GameResult result = {seed, 0, 0, 0, false};

    GX::Emulator emu;
    if (emu.LoadRom(rom_path)) {
        // Patch seed before first frame
        emu.WriteLong(Scrabble::test_seed_override, seed);

        // Run until game completes
        int frames = emu.RunUntilMemoryEquals(Scrabble::test_game_over, 1, MAX_GAME_FRAMES);
        if (frames >= 0) {
            // Scores are stored in eighths (×8); convert to display points
            result.p0_score = static_cast<int16_t>(emu.ReadWord(Scrabble::test_player0_score)) >> 3;
            result.p1_score = static_cast<int16_t>(emu.ReadWord(Scrabble::test_player1_score)) >> 3;
            result.frames = emu.ReadLong(Scrabble::total_frames);
            result.completed = true;
        }
    }

    // Write result to pipe
    write(write_fd, &result, sizeof(result));
    close(write_fd);
}

// Fork a process to run a game, returns read fd for result
int ForkGame(const char* rom_path, uint32_t seed) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(pipefd[0]);  // Close read end
        RunGameToFd(rom_path, seed, pipefd[1]);
        _exit(0);
    }

    // Parent process
    close(pipefd[1]);  // Close write end
    return pipefd[0];  // Return read end
}

// Read result from a forked game
GameResult ReadGameResult(int read_fd) {
    GameResult result = {0, 0, 0, 0, false};
    read(read_fd, &result, sizeof(result));
    close(read_fd);
    return result;
}

// ---------------------------------------------------------------------------
// NWL23 Tests - Shadow and NoShadow with score and frame assertions
// ---------------------------------------------------------------------------

TEST(NWL23, AllSeeds) {
    // Fork all games in parallel (each in separate process to avoid global state conflicts)
    std::vector<int> shadow_fds;
    std::vector<int> noshadow_fds;

    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        shadow_fds.push_back(ForkGame(ROM_NWL23_SHADOW, seed));
        noshadow_fds.push_back(ForkGame(ROM_NWL23_NOSHADOW, seed));
    }

    // Wait for all child processes
    while (wait(nullptr) > 0) {}

    // Collect results from pipes
    std::vector<GameResult> shadow_results, noshadow_results;
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        shadow_results.push_back(ReadGameResult(shadow_fds[seed]));
        noshadow_results.push_back(ReadGameResult(noshadow_fds[seed]));
    }

    // Print results
    std::cout << "\n=== NWL23 Shadow vs No-Shadow ===" << std::endl;
    std::cout << std::setw(6) << "Seed"
              << std::setw(8) << "P0"
              << std::setw(8) << "P1"
              << std::setw(10) << "Shadow"
              << std::setw(10) << "NoShadow"
              << std::setw(8) << "Diff" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    uint32_t shadow_total = 0;
    uint32_t noshadow_total = 0;

    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        const auto& shadow = shadow_results[seed];
        const auto& noshadow = noshadow_results[seed];

        ASSERT_TRUE(shadow.completed) << "Shadow game " << seed << " did not complete";
        ASSERT_TRUE(noshadow.completed) << "NoShadow game " << seed << " did not complete";

        const auto& expected = NWL23_EXPECTED[seed];
        int diff = static_cast<int>(noshadow.frames) - static_cast<int>(shadow.frames);

        std::cout << std::setw(6) << seed
                  << std::setw(8) << shadow.p0_score
                  << std::setw(8) << shadow.p1_score
                  << std::setw(10) << shadow.frames
                  << std::setw(10) << noshadow.frames
                  << std::setw(8) << diff;

        bool ok = (shadow.p0_score == expected.p0_score &&
                   shadow.p1_score == expected.p1_score &&
                   shadow.frames == expected.shadow_frames &&
                   noshadow.frames == expected.noshadow_frames);
        if (!ok) std::cout << " FAIL";
        std::cout << std::endl;

        shadow_total += shadow.frames;
        noshadow_total += noshadow.frames;

        // Assertions
        EXPECT_EQ(shadow.p0_score, expected.p0_score) << "Seed " << seed << " shadow P0";
        EXPECT_EQ(shadow.p1_score, expected.p1_score) << "Seed " << seed << " shadow P1";
        EXPECT_EQ(shadow.p0_score, noshadow.p0_score) << "Seed " << seed << " P0 mismatch";
        EXPECT_EQ(shadow.p1_score, noshadow.p1_score) << "Seed " << seed << " P1 mismatch";
        EXPECT_EQ(shadow.frames, expected.shadow_frames) << "Seed " << seed << " shadow frames";
        EXPECT_EQ(noshadow.frames, expected.noshadow_frames) << "Seed " << seed << " noshadow frames";
    }

    std::cout << std::string(50, '-') << std::endl;
    std::cout << std::setw(6) << "Total"
              << std::setw(8) << ""
              << std::setw(8) << ""
              << std::setw(10) << shadow_total
              << std::setw(10) << noshadow_total
              << std::setw(8) << (static_cast<int>(noshadow_total) - static_cast<int>(shadow_total))
              << std::endl;

    double speedup = 100.0 * (static_cast<int>(noshadow_total) - static_cast<int>(shadow_total)) / noshadow_total;
    std::cout << "\nShadow speedup: " << std::fixed << std::setprecision(2)
              << speedup << "%" << std::endl;
}

// ---------------------------------------------------------------------------
// CSW24 Tests - Run and report (no expected values yet)
// ---------------------------------------------------------------------------

TEST(CSW24, AllSeeds) {
    // Fork all games in parallel (each in separate process to avoid global state conflicts)
    std::vector<int> shadow_fds;
    std::vector<int> noshadow_fds;

    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        shadow_fds.push_back(ForkGame(ROM_CSW24_SHADOW, seed));
        noshadow_fds.push_back(ForkGame(ROM_CSW24_NOSHADOW, seed));
    }

    // Wait for all child processes
    while (wait(nullptr) > 0) {}

    // Collect results from pipes
    std::vector<GameResult> shadow_results, noshadow_results;
    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        shadow_results.push_back(ReadGameResult(shadow_fds[seed]));
        noshadow_results.push_back(ReadGameResult(noshadow_fds[seed]));
    }

    // Print results
    std::cout << "\n=== CSW24 Shadow vs No-Shadow ===" << std::endl;
    std::cout << std::setw(6) << "Seed"
              << std::setw(8) << "P0"
              << std::setw(8) << "P1"
              << std::setw(10) << "Shadow"
              << std::setw(10) << "NoShadow"
              << std::setw(8) << "Diff" << std::endl;
    std::cout << std::string(50, '-') << std::endl;

    uint32_t shadow_total = 0;
    uint32_t noshadow_total = 0;

    for (int seed = 0; seed < NUM_SEEDS; seed++) {
        const auto& shadow = shadow_results[seed];
        const auto& noshadow = noshadow_results[seed];

        ASSERT_TRUE(shadow.completed) << "Shadow game " << seed << " did not complete";
        ASSERT_TRUE(noshadow.completed) << "NoShadow game " << seed << " did not complete";

        // Scores must match between shadow and noshadow
        EXPECT_EQ(shadow.p0_score, noshadow.p0_score) << "Seed " << seed << " P0 mismatch";
        EXPECT_EQ(shadow.p1_score, noshadow.p1_score) << "Seed " << seed << " P1 mismatch";

        int diff = static_cast<int>(noshadow.frames) - static_cast<int>(shadow.frames);

        std::cout << std::setw(6) << seed
                  << std::setw(8) << shadow.p0_score
                  << std::setw(8) << shadow.p1_score
                  << std::setw(10) << shadow.frames
                  << std::setw(10) << noshadow.frames
                  << std::setw(8) << diff << std::endl;

        shadow_total += shadow.frames;
        noshadow_total += noshadow.frames;
    }

    std::cout << std::string(50, '-') << std::endl;
    std::cout << std::setw(6) << "Total"
              << std::setw(8) << ""
              << std::setw(8) << ""
              << std::setw(10) << shadow_total
              << std::setw(10) << noshadow_total
              << std::setw(8) << (static_cast<int>(noshadow_total) - static_cast<int>(shadow_total))
              << std::endl;

    double speedup = 100.0 * (static_cast<int>(noshadow_total) - static_cast<int>(shadow_total)) / noshadow_total;
    std::cout << "\nShadow speedup: " << std::fixed << std::setprecision(2)
              << speedup << "%" << std::endl;
}

} // namespace
