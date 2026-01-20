#pragma once

// Auto-generated symbol table from Genesis ELF
// Generated: 2026-01-20T09:01:17.618064
// Source: stdin

#include <cstdint>

namespace Scrabble {

    constexpr uint32_t data_start = 0xFF0000;  // original: _data_start
    constexpr uint32_t rng_state = 0xFF0000;
    constexpr uint32_t bss_start = 0xFF0004;  // original: _bss_start
    constexpr uint32_t data_end = 0xFF0004;  // original: _data_end
    constexpr uint32_t frame_counter = 0xFF0004;
    constexpr uint32_t BONUS_LAYOUT = 0xFF0008;
    constexpr uint32_t layout_initialized_0 = 0xFF00EA;  // original: layout_initialized.0
    constexpr uint32_t klv_kwg_buf = 0xFF00F0;
    constexpr uint32_t current_seed = 0xFF2800;
    constexpr uint32_t game_number = 0xFF2804;
    constexpr uint32_t total_frames = 0xFF2808;
    constexpr uint32_t test_game_over = 0xFF280C;
    constexpr uint32_t test_player1_score = 0xFF280E;
    constexpr uint32_t test_player0_score = 0xFF2810;
    constexpr uint32_t game = 0xFF2812;
    constexpr uint32_t last_move_frames = 0xFF423C;
    constexpr uint32_t history_count = 0xFF4240;
    constexpr uint32_t history = 0xFF4244;
    constexpr uint32_t moves = 0xFF451C;
    constexpr uint32_t klv = 0xFF5F1E;
    constexpr uint32_t klv_word_counts = 0xFF5F32;
    constexpr uint32_t shadow_last_move_cutoff = 0xFF8644;
    constexpr uint32_t shadow_last_move_processed = 0xFF8648;
    constexpr uint32_t shadow_cutoff_anchors = 0xFF864C;
    constexpr uint32_t shadow_total_anchors = 0xFF8650;
    constexpr uint32_t bss_end = 0xFF8654;  // original: _bss_end

    // Test hook area at end of RAM (not cleared by boot.s)
    constexpr uint32_t test_seed_override = 0xFFFFF0;

}  // namespace Scrabble

