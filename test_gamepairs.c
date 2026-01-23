/*
 * Gamepairs comparison test
 *
 * Runs gamepairs to compare old vs new static evaluation strategies.
 * For each seed, runs two games:
 *   - Game A: P0 uses new strategy, P1 uses old strategy
 *   - Game B: P0 uses old strategy, P1 uses new strategy
 *
 * This cancels out first-player advantage and measures strategy strength.
 *
 * Usage: ./test_gamepairs <start_seed> <end_seed>
 * Output format (one line per seed):
 *   seed:p0a:p1a:p0b:p1b:new_spread
 *
 * where new_spread = (P0_A - P1_A) + (P1_B - P0_B) = net advantage of new strategy
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "inc/scrabble.h"
#include "inc/klv.h"

/* Stubs for Genesis-specific functions */
void draw_char(int x, int y, char c, int pal) { (void)x; (void)y; (void)c; (void)pal; }
void draw_number(int x, int y, int num, int pal) { (void)x; (void)y; (void)num; (void)pal; }

/* External data */
extern const uint32_t kwg_data[];
extern const uint8_t klv_data[];

/* KLV word counts buffer */
#define KLV_WORD_COUNTS_SIZE 2500
static uint32_t klv_word_counts[KLV_WORD_COUNTS_SIZE];

/* External functions */
extern void rng_seed(uint32_t seed);
extern void game_init(GameState *game);
extern int game_is_over(const GameState *game);
extern void board_update_cross_sets(Board *board, const uint32_t *kwg);
extern void board_update_cross_sets_for_move(Board *board, const uint32_t *kwg, const Move *move);
extern void generate_moves_ex(const Board *board, const Rack *rack, const Rack *opp_rack,
                              const uint32_t *kwg, const KLV *klv, const Bag *bag,
                              MoveGenFlags flags, MoveList *moves);

static void apply_move(GameState *game, Move *move, const uint32_t *kwg) {
    Player *player = &game->players[game->current_player];
    Player *opponent = &game->players[1 - game->current_player];

    if (move->move_type == GAME_EVENT_TILE_PLACEMENT_MOVE) {
        /* Remove tiles from rack */
        for (int i = 0; i < move->tiles_length; i++) {
            MachineLetter tile = move->tiles[i];
            if (tile != PLAYED_THROUGH_MARKER) {
                MachineLetter rack_tile = IS_BLANKED(tile) ? BLANK_MACHINE_LETTER : tile;
                if (player->rack.counts[rack_tile] > 0) {
                    player->rack.counts[rack_tile]--;
                    player->rack.total--;
                }
            }
        }

        /* Place tiles on board */
        board_apply_move(&game->board, move);

        /* Update cross-sets */
        board_update_cross_sets_for_move(&game->board, kwg, move);

        /* Add score (convert from eighths to points) */
        player->score += move->score / 8;

        /* Refill rack from bag */
        bag_refill_rack(&game->bag, &player->rack);

        /* Reset pass counter */
        game->passes = 0;

        /* Check for game over: player used all tiles and bag is empty */
        if (player->rack.total == 0 && game->bag.count == 0) {
            /* Add opponent's remaining tile values to player's score */
            int remaining_value = 0;
            for (int letter = 0; letter < ALPHABET_SIZE; letter++) {
                remaining_value += opponent->rack.counts[letter] * (TILE_SCORES[letter] / 8);
            }
            player->score += 2 * remaining_value;
            game->game_over = 1;
        }
    } else if (move->move_type == GAME_EVENT_EXCHANGE) {
        /* Return exchanged tiles to bag, draw new ones */
        for (int i = 0; i < move->tiles_played; i++) {
            MachineLetter tile = move->tiles[i];
            if (player->rack.counts[tile] > 0) {
                player->rack.counts[tile]--;
                player->rack.total--;
                game->bag.tiles[game->bag.count++] = tile;
            }
        }
        bag_shuffle(&game->bag);
        bag_refill_rack(&game->bag, &player->rack);
        game->passes = 0;
    } else {
        /* Pass */
        game->passes++;
        if (game->passes >= 6) {
            /* 6 consecutive passes = game over */
            /* Subtract remaining tile values from each player */
            for (int p = 0; p < 2; p++) {
                int remaining = 0;
                for (int letter = 0; letter < ALPHABET_SIZE; letter++) {
                    remaining += game->players[p].rack.counts[letter] * (TILE_SCORES[letter] / 8);
                }
                game->players[p].score -= remaining;
            }
            game->game_over = 1;
        }
    }

    /* Switch players */
    game->current_player = 1 - game->current_player;
}

/*
 * Run a single game with specified strategies per player.
 * p0_flags: flags for player 0's move generation
 * p1_flags: flags for player 1's move generation
 */
static void run_game_with_strategies(uint32_t seed, KLV *klv,
                                     MoveGenFlags p0_flags, MoveGenFlags p1_flags,
                                     int *p0_score, int *p1_score) {
    GameState game;
    MoveList moves;

    rng_seed(seed);
    game_init(&game);
    board_update_cross_sets(&game.board, kwg_data);

    int turn = 0;
    while (!game_is_over(&game) && turn < 200) {
        MoveGenFlags flags = (game.current_player == 0) ? p0_flags : p1_flags;
        const Rack *opp_rack = &game.players[1 - game.current_player].rack;

        generate_moves_ex(&game.board,
                         &game.players[game.current_player].rack,
                         opp_rack,
                         kwg_data, klv, &game.bag, flags, &moves);

        if (moves.count > 0) {
            apply_move(&game, &moves.moves[0], kwg_data);
        } else {
            /* No moves - pass */
            Move pass_move = {0};
            pass_move.move_type = GAME_EVENT_PASS;
            apply_move(&game, &pass_move, kwg_data);
        }
        turn++;
    }

    *p0_score = game.players[0].score;
    *p1_score = game.players[1].score;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <start_seed> <end_seed>\n", argv[0]);
        fprintf(stderr, "Output: seed:p0a:p1a:p0b:p1b:new_spread\n");
        fprintf(stderr, "  Game A: P0=new, P1=old\n");
        fprintf(stderr, "  Game B: P0=old, P1=new\n");
        fprintf(stderr, "  new_spread = net advantage of new strategy\n");
        return 1;
    }

    uint32_t start_seed = (uint32_t)atoi(argv[1]);
    uint32_t end_seed = (uint32_t)atoi(argv[2]);

    /* Initialize KLV */
    KLV klv;
    klv_init(&klv, klv_data, klv_word_counts);

    /* Flags for old (no adjustments) and new (with adjustments) strategies */
    MoveGenFlags old_flags = MOVEGEN_FLAG_NO_STATIC_ADJUSTMENTS;
    MoveGenFlags new_flags = MOVEGEN_FLAG_NONE;

    /* Accumulators for summary statistics */
    long long total_new_spread = 0;
    int new_wins = 0, old_wins = 0, ties = 0;

    /* Run gamepairs */
    for (uint32_t seed = start_seed; seed <= end_seed; seed++) {
        int p0a, p1a, p0b, p1b;

        /* Game A: P0=new, P1=old */
        run_game_with_strategies(seed, &klv, new_flags, old_flags, &p0a, &p1a);

        /* Game B: P0=old, P1=new */
        run_game_with_strategies(seed, &klv, old_flags, new_flags, &p0b, &p1b);

        /* new_spread = (P0_A - P1_A) + (P1_B - P0_B)
         * = advantage new has over old when going first + advantage when going second */
        int new_spread = (p0a - p1a) + (p1b - p0b);

        printf("%u:%d:%d:%d:%d:%d\n", seed, p0a, p1a, p0b, p1b, new_spread);

        total_new_spread += new_spread;
        if (new_spread > 0) new_wins++;
        else if (new_spread < 0) old_wins++;
        else ties++;
    }

    /* Print summary to stderr */
    int num_pairs = end_seed - start_seed + 1;
    fprintf(stderr, "\n=== Summary (%d gamepairs) ===\n", num_pairs);
    fprintf(stderr, "New strategy wins: %d (%.1f%%)\n", new_wins, 100.0 * new_wins / num_pairs);
    fprintf(stderr, "Old strategy wins: %d (%.1f%%)\n", old_wins, 100.0 * old_wins / num_pairs);
    fprintf(stderr, "Ties: %d (%.1f%%)\n", ties, 100.0 * ties / num_pairs);
    fprintf(stderr, "Average spread per pair: %.2f\n", (double)total_new_spread / num_pairs);
    fprintf(stderr, "Total spread: %lld\n", total_new_spread);

    return 0;
}
