/*
 * Game logic - rack, bag, scoring, turns
 */

#include "scrabble.h"
#include "kwg.h"

/* From libc.c */
extern void *memset(void *s, int c, unsigned long n);
extern void *memcpy(void *dest, const void *src, unsigned long n);

/* Simple 16-bit PRNG for shuffling - fast on 68000 */
static uint16_t rng_state = 12345;

static uint16_t rng_next(void) {
    /* 16-bit xorshift */
    rng_state ^= rng_state << 7;
    rng_state ^= rng_state >> 9;
    rng_state ^= rng_state << 8;
    return rng_state;
}

void rng_seed(uint32_t seed) {
    /* Mix the seed to ensure different values produce different states */
    /* Seed 0 and 1 must produce different states */
    uint32_t mixed = seed * 2654435761u + 1;  /* Knuth multiplicative hash + 1 */
    rng_state = (uint16_t)(mixed | 1);  /* Ensure non-zero (odd) */
}

/* Fast random number in range [0, n) using multiply-high */
static uint16_t rng_range(uint16_t n) {
    uint32_t product = (uint32_t)rng_next() * (uint32_t)n;
    return (uint16_t)(product >> 16);
}

/*
 * Rack functions
 */
void rack_init(Rack *rack) {
    memset(rack, 0, sizeof(Rack));
}

void rack_add_tile(Rack *rack, MachineLetter tile) {
    rack->counts[tile]++;
    rack->total++;
}

int rack_remove_tile(Rack *rack, MachineLetter tile) {
    if (rack->counts[tile] == 0) return 0;
    rack->counts[tile]--;
    rack->total--;
    return 1;
}

int rack_has_tile(const Rack *rack, MachineLetter tile) {
    return rack->counts[tile] > 0;
}

/* Get rack as string of letters (for display) */
void rack_to_string(const Rack *rack, char *buf) {
    int pos = 0;
    for (int letter = 0; letter < ALPHABET_SIZE; letter++) {
        for (int j = 0; j < rack->counts[letter]; j++) {
            if (letter == 0) {
                buf[pos++] = '?';  /* Blank */
            } else {
                buf[pos++] = 'A' + letter - 1;
            }
        }
    }
    buf[pos] = '\0';
}

/*
 * Bag functions
 */
void bag_init(Bag *bag) {
    bag->count = 0;

    /* Fill bag with standard tile distribution */
    for (int letter = 0; letter < ALPHABET_SIZE; letter++) {
        for (int j = 0; j < TILE_COUNTS[letter]; j++) {
            bag->tiles[bag->count++] = letter;
        }
    }
}

void bag_shuffle(Bag *bag) {
    /* Fisher-Yates shuffle using fast 16-bit RNG */
    for (int i = bag->count - 1; i > 0; i--) {
        int j = rng_range(i + 1);
        MachineLetter tmp = bag->tiles[i];
        bag->tiles[i] = bag->tiles[j];
        bag->tiles[j] = tmp;
    }
}

MachineLetter bag_draw(Bag *bag) {
    if (bag->count == 0) return ALPHABET_EMPTY_SQUARE_MARKER;  /* Bag empty */
    return bag->tiles[--bag->count];
}

void bag_refill_rack(Bag *bag, Rack *rack) {
    while (rack->total < RACK_SIZE && bag->count > 0) {
        MachineLetter tile = bag_draw(bag);
        rack_add_tile(rack, tile);
    }
}

/* Return tiles to bag (for exchanges) */
void bag_return_tiles(Bag *bag, const MachineLetter *tiles, int count) {
    for (int i = 0; i < count; i++) {
        bag->tiles[bag->count++] = tiles[i];
    }
    bag_shuffle(bag);
}

/*
 * Scoring
 */

/* Get letter multiplier based on bonus square (only if tile is fresh) */
static int get_letter_mult(uint8_t bonus) {
    switch (bonus) {
        case BONUS_DL: return 2;
        case BONUS_TL: return 3;
        default: return 1;
    }
}

/* Get word multiplier based on bonus square */
static int get_word_mult(uint8_t bonus) {
    switch (bonus) {
        case BONUS_DW:
        case BONUS_CENTER:
            return 2;
        case BONUS_TW:
            return 3;
        default:
            return 1;
    }
}

/*
 * Score a move on the board
 * Assumes the move is valid
 */
Equity score_move(const Board *board, const Move *move) {
    Equity main_word_score = 0;
    Equity cross_word_score = 0;
    int word_multiplier = 1;
    int tiles_played = 0;

    int row = move->row_start;
    int col = move->col_start;

    for (int i = 0; i < move->tiles_length; i++) {
        int r = (move->dir == DIR_HORIZONTAL) ? row : row + i;
        int c = (move->dir == DIR_HORIZONTAL) ? col + i : col;
        int idx = r * BOARD_DIM + c;

        MachineLetter tile = move->tiles[i];

        if (tile == PLAYED_THROUGH_MARKER) {
            /* Play-through: use existing tile, no bonus */
            MachineLetter existing = board->h_letters[idx];
            int tile_score = IS_BLANKED(existing) ? 0 : TILE_SCORES[UNBLANKED(existing)];
            main_word_score += tile_score;
        } else {
            /* Fresh tile: apply bonuses */
            tiles_played++;
            int tile_score = IS_BLANKED(tile) ? 0 : TILE_SCORES[UNBLANKED(tile)];
            uint8_t bonus = board->bonuses[idx];

            int letter_mult = get_letter_mult(bonus);
            main_word_score += tile_score * letter_mult;
            word_multiplier *= get_word_mult(bonus);

            /* Check for cross-word */
            int8_t cross_score;
            if (move->dir == DIR_HORIZONTAL) {
                cross_score = board->h_cross_scores[idx];
            } else {
                cross_score = board->v_cross_scores[idx];
            }

            if (cross_score >= 0) {
                /* There's a cross-word */
                int cross_total = tile_score * letter_mult + cross_score;
                cross_total *= get_word_mult(bonus);
                cross_word_score += cross_total;
            }
        }
    }

    Equity total = main_word_score * word_multiplier + cross_word_score;

    /* Bingo bonus */
    if (tiles_played == RACK_SIZE) {
        total += 50;
    }

    return total;
}

/*
 * Game initialization
 */
void game_init(GameState *game) {
    board_init(&game->board);
    bag_init(&game->bag);
    bag_shuffle(&game->bag);

    for (int p = 0; p < 2; p++) {
        rack_init(&game->players[p].rack);
        game->players[p].score = 0;
        game->players[p].player_num = p;
        bag_refill_rack(&game->bag, &game->players[p].rack);
    }

    game->current_player = 0;
    game->passes = 0;
    game->game_over = 0;
}

/*
 * Play a move
 * Returns 1 if successful, 0 if invalid
 */
int game_play_move(GameState *game, const Move *move) {
    if (game->game_over) return 0;

    Player *player = &game->players[game->current_player];

    /* Remove tiles from rack and place on board */
    for (int i = 0; i < move->tiles_length; i++) {
        MachineLetter tile = move->tiles[i];
        if (tile != PLAYED_THROUGH_MARKER) {
            MachineLetter rack_tile = IS_BLANKED(tile) ? BLANK_MACHINE_LETTER : tile;
            if (!rack_remove_tile(&player->rack, rack_tile)) {
                return 0;  /* Tile not in rack */
            }
        }
    }

    /* Apply move to board */
    board_apply_move(&game->board, move);

    /* Score the move */
    player->score += move->score;

    /* Refill rack from bag */
    bag_refill_rack(&game->bag, &player->rack);

    /* Reset pass counter */
    game->passes = 0;

    /* Switch to next player */
    game->current_player ^= 1;

    /* Check for game over (player emptied rack and bag empty) */
    if (player->rack.total == 0 && game->bag.count == 0) {
        game->game_over = 1;

        /* Add opponent's remaining tiles to winner */
        Player *opponent = &game->players[game->current_player];
        int remaining_value = 0;
        for (int letter = 0; letter < ALPHABET_SIZE; letter++) {
            remaining_value += opponent->rack.counts[letter] * TILE_SCORES[letter];
        }
        player->score += remaining_value * 2;  /* Double for going out */
    }

    return 1;
}

/*
 * Pass turn
 */
void game_pass(GameState *game) {
    if (game->game_over) return;

    game->passes++;
    game->current_player ^= 1;

    /* Game ends after 6 consecutive passes (3 per player) */
    if (game->passes >= 6) {
        game->game_over = 1;

        /* Subtract remaining tiles from each player */
        for (int p = 0; p < 2; p++) {
            Player *player = &game->players[p];
            int remaining = 0;
            for (int letter = 0; letter < ALPHABET_SIZE; letter++) {
                remaining += player->rack.counts[letter] * TILE_SCORES[letter];
            }
            player->score -= remaining;
        }
    }
}

/*
 * Exchange tiles
 * Returns 1 if successful
 */
int game_exchange(GameState *game, const MachineLetter *tiles, int count) {
    if (game->game_over) return 0;
    if (game->bag.count < RACK_SIZE) return 0;  /* Can't exchange if < 7 in bag */

    Player *player = &game->players[game->current_player];

    /* Remove tiles from rack */
    for (int i = 0; i < count; i++) {
        if (!rack_remove_tile(&player->rack, tiles[i])) {
            /* Tile not in rack - rollback and fail */
            for (int j = 0; j < i; j++) {
                rack_add_tile(&player->rack, tiles[j]);
            }
            return 0;
        }
    }

    /* Draw new tiles first */
    bag_refill_rack(&game->bag, &player->rack);

    /* Return exchanged tiles to bag */
    bag_return_tiles(&game->bag, tiles, count);

    /* Count as a pass for consecutive pass tracking */
    game->passes++;
    game->current_player ^= 1;

    return 1;
}

int game_is_over(const GameState *game) {
    return game->game_over;
}
