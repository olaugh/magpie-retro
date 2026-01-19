/*
 * Native test harness for movegen with sanitizers
 * Compile with: make test-native
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

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

/* RAM buffer for word_counts */
#define KLV_WORD_COUNTS_SIZE 2500
static uint32_t klv_word_counts[KLV_WORD_COUNTS_SIZE];
static KLV klv;

/* Game state */
static GameState game;
static MoveList moves;

/* External functions from game.c */
extern void rng_seed(uint32_t seed);

int main(int argc, char **argv) {
    int target_turn = 8;
    if (argc > 1) {
        target_turn = atoi(argv[1]);
    }

    printf("Initializing KLV...\n");
    klv_init(&klv, klv_data, klv_word_counts);

    printf("Starting game with seed 0...\n");
    rng_seed(0);

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
    while (!game_is_over(&game) && turn <= target_turn) {
        printf("\n=== Turn %d (Player %d) ===\n", turn, game.current_player);

        /* Show rack */
        char rack_str[16];
        rack_to_string(&game.players[game.current_player].rack, rack_str);
        printf("Rack: %s\n", rack_str);

        /* Generate moves with timing */
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        generate_moves(&game.board,
                      &game.players[game.current_player].rack,
                      kwg_data, &klv, &game.bag, &moves);

        clock_gettime(CLOCK_MONOTONIC, &end);
        long elapsed_us = (end.tv_sec - start.tv_sec) * 1000000L +
                          (end.tv_nsec - start.tv_nsec) / 1000L;
        printf("Move generation: %ld us\n", elapsed_us);

        if (moves.count > 0) {
            Move *best = &moves.moves[0];

            /* Show best move */
            printf("Best move: ");
            if (best->move_type == GAME_EVENT_EXCHANGE) {
                printf("Exchange %d tiles", best->tiles_played);
            } else {
                char word[16];
                for (int i = 0; i < best->tiles_length && i < 15; i++) {
                    MachineLetter ml = best->tiles[i];
                    uint8_t letter = ml & 0x7F;
                    if (letter >= 1 && letter <= 26) {
                        word[i] = 'A' + (letter - 1);
                        if (ml & 0x80) word[i] += 32; /* lowercase for blank */
                    } else {
                        word[i] = '.';
                    }
                }
                word[best->tiles_length] = '\0';
                printf("%s at %d,%d %s", word, best->row_start, best->col_start,
                       best->dir == 0 ? "H" : "V");
            }
            printf(" score=%d equity=%d\n", best->score, best->equity);

            /* Play the move */
            if (best->move_type == GAME_EVENT_EXCHANGE) {
                game_exchange(&game, best->tiles, best->tiles_played);
            } else {
                game_play_move(&game, best);
                board_update_cross_sets(&game.board, kwg_data);
            }
        } else {
            printf("No moves - passing\n");
            game_pass(&game);
        }

        turn++;
    }

    printf("\nTest completed successfully!\n");
    return 0;
}
