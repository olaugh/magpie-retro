/*
 * Batch test harness - runs many games and outputs moves for diffing
 * Usage: test_batch <start_seed> <end_seed>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Include the game headers */
#include "inc/scrabble.h"
#include "inc/kwg.h"
#include "inc/klv.h"

/* Stubs for Genesis-specific functions */
void draw_char(int x, int y, char c, int pal) { (void)x; (void)y; (void)c; (void)pal; }
void draw_number(int x, int y, int num, int pal) { (void)x; (void)y; (void)num; (void)pal; }

/* External data */
extern const uint32_t kwg_data[];
extern const uint8_t klv_data[];

/* Shadow debug counters (when built with USE_SHADOW_DEBUG=1) */
#ifdef USE_SHADOW_DEBUG
typedef struct {
    int row;
    int col;
    int dir;
    int16_t bound;
    int16_t best_before;
    int16_t best_after;
} BadCutoffInfo;
extern int shadow_debug_bad_cutoff_count;
extern BadCutoffInfo shadow_debug_last_bad_cutoff;

/* Callback to print bad cutoffs as they happen */
void shadow_debug_report_bad_cutoff(BadCutoffInfo *info);
#endif

/* RAM buffer for word_counts */
#define KLV_WORD_COUNTS_SIZE 2500
static uint32_t klv_word_counts[KLV_WORD_COUNTS_SIZE];
static KLV klv;

/* Game state */
static GameState game;
static MoveList moves;

/* External functions from game.c */
extern void rng_seed(uint32_t seed);

static void format_move(Move *m, char *buf) {
    if (m->dir == 0xFF) {
        /* Exchange */
        sprintf(buf, "X%d", m->tiles_played);
    } else {
        /* Regular move */
        char word[16];
        for (int i = 0; i < m->tiles_length && i < 15; i++) {
            MachineLetter ml = m->tiles[i];
            uint8_t letter = ml & 0x7F;
            if (letter >= 1 && letter <= 26) {
                word[i] = 'A' + (letter - 1);
                if (ml & 0x80) word[i] += 32; /* lowercase for blank */
            } else {
                word[i] = '.';
            }
        }
        word[m->tiles_length] = '\0';
        sprintf(buf, "%s@%d,%d%c:%d/%d", word, m->row, m->col,
                m->dir == 0 ? 'H' : 'V', m->score, m->equity);
    }
}

static void play_game(uint32_t seed) {
    rng_seed(seed);

    /* Initialize game */
    board_init(&game.board);
    bag_init(&game.bag);
    bag_shuffle(&game.bag);

    rack_init(&game.players[0].rack);
    game.players[0].score = 0;
    game.players[0].player_num = 0;
    bag_refill_rack(&game.bag, &game.players[0].rack);

    rack_init(&game.players[1].rack);
    game.players[1].score = 0;
    game.players[1].player_num = 1;
    bag_refill_rack(&game.bag, &game.players[1].rack);

    game.current_player = 0;
    game.passes = 0;
    game.game_over = 0;

    board_update_cross_sets(&game.board, kwg_data);

    int turn = 1;
    while (!game_is_over(&game) && turn <= 200) {  /* Max 200 turns safety */
        generate_moves(&game.board,
                      &game.players[game.current_player].rack,
                      kwg_data, &klv, &game.bag, &moves);

        if (moves.count > 0) {
            Move *best = &moves.moves[0];
            char move_str[64];
            format_move(best, move_str);
            printf("%u:%d:%s\n", seed, turn, move_str);

            /* Play the move */
            if (best->dir == 0xFF) {
                game_exchange(&game, best->tiles, best->tiles_played);
            } else {
                game_play_move(&game, best);
                board_update_cross_sets(&game.board, kwg_data);
            }
        } else {
            printf("%u:%d:PASS\n", seed, turn);
            game_pass(&game);
        }

        turn++;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <start_seed> <end_seed>\n", argv[0]);
        return 1;
    }

    uint32_t start_seed = (uint32_t)atoi(argv[1]);
    uint32_t end_seed = (uint32_t)atoi(argv[2]);

    /* Initialize KLV once */
    klv_init(&klv, klv_data, klv_word_counts);

    for (uint32_t seed = start_seed; seed <= end_seed; seed++) {
        play_game(seed);
    }

#ifdef USE_SHADOW_DEBUG
    /* Get shadow stats from movegen */
    extern int shadow_debug_record_calls;
    extern int shadow_debug_leave_added;
    fprintf(stderr, "SHADOW_STATS: record_calls=%d leave_added=%d\n",
            shadow_debug_record_calls, shadow_debug_leave_added);

    if (shadow_debug_bad_cutoff_count > 0) {
        fprintf(stderr, "BAD CUTOFFS: %d (last: anchor(%d,%d,%c) bound=%d before=%d after=%d)\n",
                shadow_debug_bad_cutoff_count,
                shadow_debug_last_bad_cutoff.row,
                shadow_debug_last_bad_cutoff.col,
                shadow_debug_last_bad_cutoff.dir ? 'V' : 'H',
                shadow_debug_last_bad_cutoff.bound,
                shadow_debug_last_bad_cutoff.best_before,
                shadow_debug_last_bad_cutoff.best_after);
    } else {
        fprintf(stderr, "NO BAD CUTOFFS\n");
    }
#elif USE_SHADOW
    /* Get shadow cutoff stats from movegen */
    extern int shadow_total_anchors;
    extern int shadow_cutoff_anchors;
    int total = shadow_total_anchors + shadow_cutoff_anchors;
    fflush(stdout);  /* Ensure stdout is flushed before printing to stderr */
    fprintf(stderr, "SHADOW_CUTOFF: processed=%d cutoff=%d total=%d (%.1f%% cutoff)\n",
            shadow_total_anchors, shadow_cutoff_anchors, total,
            total > 0 ? 100.0 * shadow_cutoff_anchors / total : 0.0);
#endif

#if USE_TIMING
    extern void print_timing_stats(void);
    print_timing_stats();
#endif

    return 0;
}
