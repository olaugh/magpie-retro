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
    uint32_t hybrid_frames;
};

// Expected results for seeds 0-9 (scores must match between all variants).
// Frame counts reflect the latest optimizations and are asserted to guard
// against unexpected performance regressions.
// Note: hybrid isn't guaranteed to be faster than shadow or noshadow on every game.
constexpr ExpectedResult NWL23_EXPECTED[NUM_SEEDS] = {
    {430, 515, 12232, 12107, 13319},  // Seed 0
    {447, 464,  7266,  7312,  8111},  // Seed 1
    {620, 344,  4831,  5094,  5080},  // Seed 2
    {438, 398,  8352,  8197,  9185},  // Seed 3
    {417, 445,  5614,  5023,  5792},  // Seed 4
    {365, 429,  9523, 10511, 10648},  // Seed 5
    {365, 506,  8531,  8504,  9236},  // Seed 6
    {485, 442, 10343, 10274, 11999},  // Seed 7
    {555, 310,  7590, 12447,  7941},  // Seed 8
    {406, 483,  9941, 10230, 10864},  // Seed 9
};

// CSW24 expected results (hybrid isn't guaranteed faster on every game)
constexpr ExpectedResult CSW24_EXPECTED[NUM_SEEDS] = {
    {437, 462, 11913, 14679, 10524},  // Seed 0
    {460, 383,  9979, 12445, 12479},  // Seed 1
    {544, 287,  6539, 11422,  6902},  // Seed 2
    {508, 372,  6349,  7592,  6834},  // Seed 3
    {502, 384,  4632,  5430,  4789},  // Seed 4
    {391, 424,  6019,  6361,  6479},  // Seed 5
    {472, 432,  6044,  6437,  6682},  // Seed 6
    {559, 520,  6359,  6552,  7156},  // Seed 7
    {548, 347,  5561,  9778,  5966},  // Seed 8
    {529, 450, 12709, 15204, 14153},  // Seed 9
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

        bool score_ok = (shadow.p0_score == expected.p0_score &&
                         shadow.p1_score == expected.p1_score);
#ifdef STRICT_FRAME_ASSERTIONS
        bool frames_ok = (shadow.frames == expected.shadow_frames &&
                          noshadow.frames == expected.noshadow_frames);
        if (!score_ok) std::cout << " SCORE!";
        if (!frames_ok) std::cout << " FRAMES!";
#else
        if (!score_ok) std::cout << " SCORE!";
#endif
        std::cout << std::endl;

        shadow_total += shadow.frames;
        noshadow_total += noshadow.frames;

        // Score assertions (always checked)
        EXPECT_EQ(shadow.p0_score, expected.p0_score) << "Seed " << seed << " shadow P0";
        EXPECT_EQ(shadow.p1_score, expected.p1_score) << "Seed " << seed << " shadow P1";
        EXPECT_EQ(shadow.p0_score, noshadow.p0_score) << "Seed " << seed << " P0 mismatch";
        EXPECT_EQ(shadow.p1_score, noshadow.p1_score) << "Seed " << seed << " P1 mismatch";

#ifdef STRICT_FRAME_ASSERTIONS
        // Frame assertions (only with GCC 15 builds)
        EXPECT_EQ(shadow.frames, expected.shadow_frames) << "Seed " << seed << " shadow frames";
        EXPECT_EQ(noshadow.frames, expected.noshadow_frames) << "Seed " << seed << " noshadow frames";
#endif
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

        const auto& expected = CSW24_EXPECTED[seed];
        int diff = static_cast<int>(noshadow.frames) - static_cast<int>(shadow.frames);

        std::cout << std::setw(6) << seed
                  << std::setw(8) << shadow.p0_score
                  << std::setw(8) << shadow.p1_score
                  << std::setw(10) << shadow.frames
                  << std::setw(10) << noshadow.frames
                  << std::setw(8) << diff;

        bool score_ok = (shadow.p0_score == expected.p0_score &&
                         shadow.p1_score == expected.p1_score);
#ifdef STRICT_FRAME_ASSERTIONS
        bool frames_ok = (shadow.frames == expected.shadow_frames &&
                          noshadow.frames == expected.noshadow_frames);
        if (!score_ok) std::cout << " SCORE!";
        if (!frames_ok) std::cout << " FRAMES!";
#else
        if (!score_ok) std::cout << " SCORE!";
#endif
        std::cout << std::endl;

        shadow_total += shadow.frames;
        noshadow_total += noshadow.frames;

        // Score assertions (always checked)
        EXPECT_EQ(shadow.p0_score, expected.p0_score) << "Seed " << seed << " CSW24 P0";
        EXPECT_EQ(shadow.p1_score, expected.p1_score) << "Seed " << seed << " CSW24 P1";
        EXPECT_EQ(shadow.p0_score, noshadow.p0_score) << "Seed " << seed << " P0 mismatch";
        EXPECT_EQ(shadow.p1_score, noshadow.p1_score) << "Seed " << seed << " P1 mismatch";

#ifdef STRICT_FRAME_ASSERTIONS
        // Frame assertions (only with GCC 15 builds)
        EXPECT_EQ(shadow.frames, expected.shadow_frames) << "Seed " << seed << " shadow frames";
        EXPECT_EQ(noshadow.frames, expected.noshadow_frames) << "Seed " << seed << " noshadow frames";
#endif
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
// 100-Game Validation Test (for MULT_SMALL debug assertions)
// Run with: bazel test //tests/gxtest:scrabble_validation_test
// ---------------------------------------------------------------------------

constexpr int VALIDATION_NUM_GAMES = 100;

TEST(Validation, NWL23_100Games) {
    // Run 100 games on NWL23 shadow ROM to validate MULT_SMALL assertions
    // If built with MULT_SMALL_DEBUG=1, any invalid multiplier will crash
    std::vector<int> fds;

    for (int seed = 0; seed < VALIDATION_NUM_GAMES; seed++) {
        fds.push_back(ForkGame(ROM_NWL23_SHADOW, seed));
    }

    // Wait for all child processes
    while (wait(nullptr) > 0) {}

    // Collect results
    int completed = 0;
    uint32_t total_frames = 0;

    for (int seed = 0; seed < VALIDATION_NUM_GAMES; seed++) {
        GameResult result = ReadGameResult(fds[seed]);
        if (result.completed) {
            completed++;
            total_frames += result.frames;
        }
    }

    std::cout << "\n=== NWL23 Validation: " << completed << "/" << VALIDATION_NUM_GAMES
              << " games completed ===" << std::endl;
    std::cout << "Total frames: " << total_frames << std::endl;

    EXPECT_EQ(completed, VALIDATION_NUM_GAMES) << "All games should complete";
}

TEST(Validation, CSW24_100Games) {
    // Run 100 games on CSW24 shadow ROM to validate MULT_SMALL assertions
    std::vector<int> fds;

    for (int seed = 0; seed < VALIDATION_NUM_GAMES; seed++) {
        fds.push_back(ForkGame(ROM_CSW24_SHADOW, seed));
    }

    // Wait for all child processes
    while (wait(nullptr) > 0) {}

    // Collect results
    int completed = 0;
    uint32_t total_frames = 0;

    for (int seed = 0; seed < VALIDATION_NUM_GAMES; seed++) {
        GameResult result = ReadGameResult(fds[seed]);
        if (result.completed) {
            completed++;
            total_frames += result.frames;
        }
    }

    std::cout << "\n=== CSW24 Validation: " << completed << "/" << VALIDATION_NUM_GAMES
              << " games completed ===" << std::endl;
    std::cout << "Total frames: " << total_frames << std::endl;

    EXPECT_EQ(completed, VALIDATION_NUM_GAMES) << "All games should complete";
}

// ---------------------------------------------------------------------------
// Hybrid ROM Validation Tests
// Validates that hybrid ROMs produce identical scores to shadow/noshadow
// and have total frames no higher than the faster variant across all games.
// Per-game variance is allowed; only overall performance is enforced.
// Run with: bazel test //tests/gxtest:scrabble_hybrid_test
// ---------------------------------------------------------------------------

constexpr int HYBRID_NUM_SEEDS = 100;

#ifdef ROM_NWL23_HYBRID

TEST(Hybrid, NWL23_ScoresMatch) {
    // Fork all games in parallel for all three variants
    std::vector<int> shadow_fds;
    std::vector<int> noshadow_fds;
    std::vector<int> hybrid_fds;

    for (int seed = 0; seed < HYBRID_NUM_SEEDS; seed++) {
        shadow_fds.push_back(ForkGame(ROM_NWL23_SHADOW, seed));
        noshadow_fds.push_back(ForkGame(ROM_NWL23_NOSHADOW, seed));
        hybrid_fds.push_back(ForkGame(ROM_NWL23_HYBRID, seed));
    }

    // Wait for all child processes
    while (wait(nullptr) > 0) {}

    // Collect results
    std::vector<GameResult> shadow_results, noshadow_results, hybrid_results;
    for (int seed = 0; seed < HYBRID_NUM_SEEDS; seed++) {
        shadow_results.push_back(ReadGameResult(shadow_fds[seed]));
        noshadow_results.push_back(ReadGameResult(noshadow_fds[seed]));
        hybrid_results.push_back(ReadGameResult(hybrid_fds[seed]));
    }

    // Print results table
    std::cout << "\n=== NWL23 Hybrid Validation (" << HYBRID_NUM_SEEDS << " games) ===" << std::endl;
    std::cout << std::setw(6) << "Seed"
              << std::setw(8) << "P0"
              << std::setw(8) << "P1"
              << std::setw(10) << "Shadow"
              << std::setw(10) << "NoShadow"
              << std::setw(10) << "Hybrid"
              << std::setw(8) << "Best"
              << std::setw(8) << "Margin" << std::endl;
    std::cout << std::string(68, '-') << std::endl;

    uint32_t shadow_total = 0, noshadow_total = 0, hybrid_total = 0;
    int hybrid_wins = 0, hybrid_ties = 0, hybrid_losses = 0;
    int max_loss = 0;

    for (int seed = 0; seed < HYBRID_NUM_SEEDS; seed++) {
        const auto& shadow = shadow_results[seed];
        const auto& noshadow = noshadow_results[seed];
        const auto& hybrid = hybrid_results[seed];

        ASSERT_TRUE(shadow.completed) << "Shadow game " << seed << " did not complete";
        ASSERT_TRUE(noshadow.completed) << "NoShadow game " << seed << " did not complete";
        ASSERT_TRUE(hybrid.completed) << "Hybrid game " << seed << " did not complete";

        // Scores must match across all three variants
        EXPECT_EQ(shadow.p0_score, noshadow.p0_score) << "Seed " << seed << " shadow/noshadow P0 mismatch";
        EXPECT_EQ(shadow.p1_score, noshadow.p1_score) << "Seed " << seed << " shadow/noshadow P1 mismatch";
        EXPECT_EQ(shadow.p0_score, hybrid.p0_score) << "Seed " << seed << " shadow/hybrid P0 mismatch";
        EXPECT_EQ(shadow.p1_score, hybrid.p1_score) << "Seed " << seed << " shadow/hybrid P1 mismatch";

        uint32_t best_baseline = std::min(shadow.frames, noshadow.frames);
        int margin = static_cast<int>(hybrid.frames) - static_cast<int>(best_baseline);

        if (margin < 0) hybrid_wins++;
        else if (margin == 0) hybrid_ties++;
        else {
            hybrid_losses++;
            if (margin > max_loss) max_loss = margin;
        }

        // Only print first 20 and any failures
        bool score_ok = (shadow.p0_score == hybrid.p0_score && shadow.p1_score == hybrid.p1_score);
        bool speed_ok = (hybrid.frames <= best_baseline);
#ifdef STRICT_FRAME_ASSERTIONS
        const auto& expected = NWL23_EXPECTED[seed];
        bool frames_ok = (seed >= NUM_SEEDS) || (shadow.frames == expected.shadow_frames &&
                          noshadow.frames == expected.noshadow_frames &&
                          hybrid.frames == expected.hybrid_frames);
#endif
        if (seed < 20 || !score_ok || !speed_ok) {
            std::cout << std::setw(6) << seed
                      << std::setw(8) << hybrid.p0_score
                      << std::setw(8) << hybrid.p1_score
                      << std::setw(10) << shadow.frames
                      << std::setw(10) << noshadow.frames
                      << std::setw(10) << hybrid.frames
                      << std::setw(8) << best_baseline
                      << std::setw(8) << margin;
            if (!score_ok) std::cout << " SCORE!";
            if (!speed_ok) std::cout << " SLOW!";
#ifdef STRICT_FRAME_ASSERTIONS
            if (!frames_ok) std::cout << " FRAMES!";
#endif
            std::cout << std::endl;
        }

        shadow_total += shadow.frames;
        noshadow_total += noshadow.frames;
        hybrid_total += hybrid.frames;

#ifdef STRICT_FRAME_ASSERTIONS
        // Frame assertions for seeds 0-9 (only with GCC 15 builds)
        if (seed < NUM_SEEDS) {
            EXPECT_EQ(shadow.frames, expected.shadow_frames) << "Seed " << seed << " shadow frames";
            EXPECT_EQ(noshadow.frames, expected.noshadow_frames) << "Seed " << seed << " noshadow frames";
            EXPECT_EQ(hybrid.frames, expected.hybrid_frames) << "Seed " << seed << " hybrid frames";
        }
#endif
    }

    std::cout << std::string(68, '-') << std::endl;
    std::cout << std::setw(6) << "Total"
              << std::setw(8) << ""
              << std::setw(8) << ""
              << std::setw(10) << shadow_total
              << std::setw(10) << noshadow_total
              << std::setw(10) << hybrid_total << std::endl;

    uint32_t best_total = std::min(shadow_total, noshadow_total);
    double savings = 100.0 * (static_cast<int>(best_total) - static_cast<int>(hybrid_total)) / best_total;

    std::cout << "\nHybrid vs best baseline: " << std::fixed << std::setprecision(2)
              << savings << "% " << (savings >= 0 ? "faster" : "slower") << std::endl;
    std::cout << "Per-game: " << hybrid_wins << " faster, " << hybrid_ties << " tied, "
              << hybrid_losses << " slower (max loss: " << max_loss << " frames)" << std::endl;

    // Overall hybrid total must be <= best baseline total
    // (per-game variance is expected, but overall it should be at least as fast)
    EXPECT_LE(hybrid_total, best_total)
        << "Hybrid total (" << hybrid_total << ") slower than best baseline total ("
        << best_total << ")";
}

#endif // ROM_NWL23_HYBRID

#ifdef ROM_CSW24_HYBRID

TEST(Hybrid, CSW24_ScoresMatch) {
    // Fork all games in parallel for all three variants
    std::vector<int> shadow_fds;
    std::vector<int> noshadow_fds;
    std::vector<int> hybrid_fds;

    for (int seed = 0; seed < HYBRID_NUM_SEEDS; seed++) {
        shadow_fds.push_back(ForkGame(ROM_CSW24_SHADOW, seed));
        noshadow_fds.push_back(ForkGame(ROM_CSW24_NOSHADOW, seed));
        hybrid_fds.push_back(ForkGame(ROM_CSW24_HYBRID, seed));
    }

    // Wait for all child processes
    while (wait(nullptr) > 0) {}

    // Collect results
    std::vector<GameResult> shadow_results, noshadow_results, hybrid_results;
    for (int seed = 0; seed < HYBRID_NUM_SEEDS; seed++) {
        shadow_results.push_back(ReadGameResult(shadow_fds[seed]));
        noshadow_results.push_back(ReadGameResult(noshadow_fds[seed]));
        hybrid_results.push_back(ReadGameResult(hybrid_fds[seed]));
    }

    // Print results table
    std::cout << "\n=== CSW24 Hybrid Validation (" << HYBRID_NUM_SEEDS << " games) ===" << std::endl;
    std::cout << std::setw(6) << "Seed"
              << std::setw(8) << "P0"
              << std::setw(8) << "P1"
              << std::setw(10) << "Shadow"
              << std::setw(10) << "NoShadow"
              << std::setw(10) << "Hybrid"
              << std::setw(8) << "Best"
              << std::setw(8) << "Margin" << std::endl;
    std::cout << std::string(68, '-') << std::endl;

    uint32_t shadow_total = 0, noshadow_total = 0, hybrid_total = 0;
    int hybrid_wins = 0, hybrid_ties = 0, hybrid_losses = 0;
    int max_loss = 0;

    for (int seed = 0; seed < HYBRID_NUM_SEEDS; seed++) {
        const auto& shadow = shadow_results[seed];
        const auto& noshadow = noshadow_results[seed];
        const auto& hybrid = hybrid_results[seed];

        ASSERT_TRUE(shadow.completed) << "Shadow game " << seed << " did not complete";
        ASSERT_TRUE(noshadow.completed) << "NoShadow game " << seed << " did not complete";
        ASSERT_TRUE(hybrid.completed) << "Hybrid game " << seed << " did not complete";

        // Scores must match across all three variants
        EXPECT_EQ(shadow.p0_score, noshadow.p0_score) << "Seed " << seed << " shadow/noshadow P0 mismatch";
        EXPECT_EQ(shadow.p1_score, noshadow.p1_score) << "Seed " << seed << " shadow/noshadow P1 mismatch";
        EXPECT_EQ(shadow.p0_score, hybrid.p0_score) << "Seed " << seed << " shadow/hybrid P0 mismatch";
        EXPECT_EQ(shadow.p1_score, hybrid.p1_score) << "Seed " << seed << " shadow/hybrid P1 mismatch";

        uint32_t best_baseline = std::min(shadow.frames, noshadow.frames);
        int margin = static_cast<int>(hybrid.frames) - static_cast<int>(best_baseline);

        if (margin < 0) hybrid_wins++;
        else if (margin == 0) hybrid_ties++;
        else {
            hybrid_losses++;
            if (margin > max_loss) max_loss = margin;
        }

        // Only print first 20 and any failures
        bool score_ok = (shadow.p0_score == hybrid.p0_score && shadow.p1_score == hybrid.p1_score);
        bool speed_ok = (hybrid.frames <= best_baseline);
#ifdef STRICT_FRAME_ASSERTIONS
        const auto& expected = CSW24_EXPECTED[seed];
        bool frames_ok = (seed >= NUM_SEEDS) || (shadow.frames == expected.shadow_frames &&
                          noshadow.frames == expected.noshadow_frames &&
                          hybrid.frames == expected.hybrid_frames);
#endif
        if (seed < 20 || !score_ok || !speed_ok) {
            std::cout << std::setw(6) << seed
                      << std::setw(8) << hybrid.p0_score
                      << std::setw(8) << hybrid.p1_score
                      << std::setw(10) << shadow.frames
                      << std::setw(10) << noshadow.frames
                      << std::setw(10) << hybrid.frames
                      << std::setw(8) << best_baseline
                      << std::setw(8) << margin;
            if (!score_ok) std::cout << " SCORE!";
            if (!speed_ok) std::cout << " SLOW!";
#ifdef STRICT_FRAME_ASSERTIONS
            if (!frames_ok) std::cout << " FRAMES!";
#endif
            std::cout << std::endl;
        }

        shadow_total += shadow.frames;
        noshadow_total += noshadow.frames;
        hybrid_total += hybrid.frames;

#ifdef STRICT_FRAME_ASSERTIONS
        // Frame assertions for seeds 0-9 (only with GCC 15 builds)
        if (seed < NUM_SEEDS) {
            EXPECT_EQ(shadow.frames, expected.shadow_frames) << "Seed " << seed << " shadow frames";
            EXPECT_EQ(noshadow.frames, expected.noshadow_frames) << "Seed " << seed << " noshadow frames";
            EXPECT_EQ(hybrid.frames, expected.hybrid_frames) << "Seed " << seed << " hybrid frames";
        }
#endif
    }

    std::cout << std::string(68, '-') << std::endl;
    std::cout << std::setw(6) << "Total"
              << std::setw(8) << ""
              << std::setw(8) << ""
              << std::setw(10) << shadow_total
              << std::setw(10) << noshadow_total
              << std::setw(10) << hybrid_total << std::endl;

    uint32_t best_total = std::min(shadow_total, noshadow_total);
    double savings = 100.0 * (static_cast<int>(best_total) - static_cast<int>(hybrid_total)) / best_total;

    std::cout << "\nHybrid vs best baseline: " << std::fixed << std::setprecision(2)
              << savings << "% " << (savings >= 0 ? "faster" : "slower") << std::endl;
    std::cout << "Per-game: " << hybrid_wins << " faster, " << hybrid_ties << " tied, "
              << hybrid_losses << " slower (max loss: " << max_loss << " frames)" << std::endl;

    // Overall hybrid total must be <= best baseline total
    // (per-game variance is expected, but overall it should be at least as fast)
    EXPECT_LE(hybrid_total, best_total)
        << "Hybrid total (" << hybrid_total << ") slower than best baseline total ("
        << best_total << ")";
}

#endif // ROM_CSW24_HYBRID

} // namespace
