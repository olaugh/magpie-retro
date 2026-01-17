/*
 * GADDAG-based move generation for Scrabble
 *
 * This implements the Gordon/Appel algorithm using GADDAG structure:
 * 1. For each anchor (empty square adjacent to a tile), generate moves
 * 2. Start from anchor, traverse GADDAG going left first
 * 3. After hitting separator node, continue right from anchor
 * 4. Record valid words when accepts flag is set
 */

#include "scrabble.h"
#include "kwg.h"

/* From libc.c */
extern void *memset(void *s, int c, unsigned long n);
extern void *memcpy(void *dest, const void *src, unsigned long n);

/* Move generation state */
typedef struct {
    const Board *board;
    const uint32_t *kwg;
    Rack rack;           /* Mutable copy of player rack */
    Move *best_move;     /* Track best move found so far */
    Score best_score;    /* Score of best move */
    uint16_t move_count; /* Total moves found */
    uint32_t rack_bits;  /* Bitmap of letters in rack for fast lookup */

    /* Current row/column being processed */
    uint8_t current_row;
    uint8_t dir;

    /* Current anchor position */
    uint8_t anchor_col;
    uint8_t last_anchor_col;

    /* Tiles being placed */
    MachineLetter strip[BOARD_DIM];
    uint8_t tiles_played;

    /* Left/right extension sets for current anchor */
    uint32_t left_ext_set;
    uint32_t right_ext_set;

    /* Scoring accumulators */
    int16_t main_word_score;
    int16_t cross_score;
    uint8_t word_multiplier;

    /* Cache of current row */
    MachineLetter row_letters[BOARD_DIM];
    uint8_t row_bonuses[BOARD_DIM];
    CrossSet row_cross_sets[BOARD_DIM];
    int8_t row_cross_scores[BOARD_DIM];
    uint8_t row_is_anchor[BOARD_DIM];
} MoveGenState;

/* Tile scores for calculating move scores */
static const uint8_t tile_scores[ALPHABET_SIZE] = {
    0,  /* Blank */
    1,  /* A */
    3,  /* B */
    3,  /* C */
    2,  /* D */
    1,  /* E */
    4,  /* F */
    2,  /* G */
    4,  /* H */
    1,  /* I */
    8,  /* J */
    5,  /* K */
    1,  /* L */
    3,  /* M */
    1,  /* N */
    1,  /* O */
    3,  /* P */
    10, /* Q */
    1,  /* R */
    1,  /* S */
    1,  /* T */
    1,  /* U */
    4,  /* V */
    4,  /* W */
    8,  /* X */
    4,  /* Y */
    10  /* Z */
};

/* Get letter multiplier from bonus type */
static inline uint8_t get_letter_mult(uint8_t bonus) {
    if (bonus == BONUS_DL) return 2;
    if (bonus == BONUS_TL) return 3;
    return 1;
}

/* Get word multiplier from bonus type */
static inline uint8_t get_word_mult(uint8_t bonus) {
    if (bonus == BONUS_DW || bonus == BONUS_CENTER) return 2;
    if (bonus == BONUS_TW) return 3;
    return 1;
}

/* Get tile score (blanks score 0) */
static inline int8_t get_tile_score(MachineLetter ml) {
    if (IS_BLANKED(ml)) return 0;
    return tile_scores[ml];
}

/* Check if column is empty */
static inline int is_empty(const MoveGenState *gen, int col) {
    return gen->row_letters[col] == EMPTY_SQUARE;
}

/* Check if column is an anchor */
static inline int is_anchor(const MoveGenState *gen, int col) {
    return gen->row_is_anchor[col];
}

/* Check if letter is in cross-set */
static inline int in_cross_set(CrossSet cs, MachineLetter ml) {
    return (cs & (1U << UNBLANKED(ml))) != 0;
}

/* Record a valid move - only keeps best */
static void record_move(MoveGenState *gen, int leftstrip, int rightstrip) {
    gen->move_count++;

    /* Calculate final score */
    int16_t score = gen->main_word_score * gen->word_multiplier + gen->cross_score;

    /* Add bingo bonus for using all 7 tiles */
    if (gen->tiles_played == RACK_SIZE) {
        score += 50;
    }

    /* Only record if better than current best */
    if (score <= gen->best_score) return;

    Move *move = gen->best_move;
    gen->best_score = score;

    /* For horizontal: current_row is row, leftstrip is col
     * For vertical: current_row is actually col, leftstrip is row */
    if (gen->dir == DIR_HORIZONTAL) {
        move->row = gen->current_row;
        move->col = leftstrip;
    } else {
        move->row = leftstrip;
        move->col = gen->current_row;
    }
    move->dir = gen->dir;
    move->tiles_played = gen->tiles_played;
    move->tiles_length = rightstrip - leftstrip + 1;
    move->score = score;

    /* Copy tiles from strip */
    for (int i = leftstrip; i <= rightstrip; i++) {
        move->tiles[i - leftstrip] = gen->strip[i];
    }
}

/* Forward declarations */
static void recursive_gen(MoveGenState *gen, int col, uint32_t node_index,
                          int leftstrip, int rightstrip);
static void show_recursion_count(void);

/* Track recursion calls for debugging */
static uint16_t recursion_counter = 0;

/*
 * go_on - Continue move generation after placing/traversing a letter
 *
 * This handles:
 * 1. Incremental score tracking
 * 2. Recording valid moves
 * 3. Continuing recursion left, or crossing separator to go right
 */
static void go_on(MoveGenState *gen, int col, MachineLetter letter,
                  uint32_t next_node_index, int accepts,
                  int leftstrip, int rightstrip) {

    uint8_t bonus = gen->row_bonuses[col];
    uint8_t letter_mult = 1;
    uint8_t word_mult = 1;
    int fresh_tile = 0;

    /* Track whether this is a placed tile or play-through */
    if (is_empty(gen, col)) {
        gen->strip[col] = letter;
        fresh_tile = 1;
        letter_mult = get_letter_mult(bonus);
        word_mult = get_word_mult(bonus);
    } else {
        /* Playing through existing tile - mark in strip */
        gen->strip[col] = 0xFF;  /* Marker for play-through */
    }

    /* Update score accumulators */
    int prev_word_mult = gen->word_multiplier;
    int prev_main_score = gen->main_word_score;
    int prev_cross_score = gen->cross_score;

    gen->word_multiplier *= word_mult;
    gen->main_word_score += get_tile_score(letter) * letter_mult;

    /* Add cross-word score if placing fresh tile that forms cross-word */
    if (fresh_tile && gen->row_cross_scores[col] >= 0) {
        int cross_word_score = get_tile_score(letter) * letter_mult +
                               gen->row_cross_scores[col];
        gen->cross_score += cross_word_score * word_mult;
    }

    if (col <= gen->anchor_col) {
        /* Going left of anchor */
        leftstrip = col;

        int no_letter_left = (col == 0) || is_empty(gen, col - 1);
        /* Check if there are letters to the right of anchor that we must traverse */
        int no_letter_right_of_anchor = (gen->anchor_col == BOARD_DIM - 1) ||
                                         is_empty(gen, gen->anchor_col + 1);

        /* Record move if this is a valid word ending */
        /* Only record if no letters on either side - otherwise must cross separator first */
        if (accepts && no_letter_left && no_letter_right_of_anchor && gen->tiles_played > 0) {
            record_move(gen, leftstrip, rightstrip);
        }

        /* Continue left if possible */
        if (next_node_index != 0 && col > 0 && col - 1 != gen->last_anchor_col) {
            recursive_gen(gen, col - 1, next_node_index, leftstrip, rightstrip);
        }

        /* Try crossing separator to go right */
        if (next_node_index != 0 && no_letter_left) {
            uint32_t sep_node = kwg_follow_arc(gen->kwg, next_node_index, ML_SEPARATOR);
            if (sep_node != 0 && gen->anchor_col < BOARD_DIM - 1) {
                recursive_gen(gen, gen->anchor_col + 1, sep_node, leftstrip, rightstrip);
            }
        }
    } else {
        /* Going right of anchor */
        rightstrip = col;

        int no_letter_right = (col == BOARD_DIM - 1) || is_empty(gen, col + 1);

        /* Record move if this is a valid word ending */
        if (accepts && no_letter_right && gen->tiles_played > 0) {
            record_move(gen, leftstrip, rightstrip);
        }

        /* Continue right if possible */
        if (next_node_index != 0 && col < BOARD_DIM - 1) {
            recursive_gen(gen, col + 1, next_node_index, leftstrip, rightstrip);
        }
    }

    /* Restore state */
    gen->word_multiplier = prev_word_mult;
    gen->main_word_score = prev_main_score;
    gen->cross_score = prev_cross_score;
}

/*
 * recursive_gen - Main recursive GADDAG traversal
 *
 * From current column, try all valid letter placements:
 * - If square has existing tile, follow that arc in GADDAG
 * - If square is empty, try all rack tiles that are in cross-set
 */
static void recursive_gen(MoveGenState *gen, int col, uint32_t node_index,
                          int leftstrip, int rightstrip) {

    MachineLetter current_letter = gen->row_letters[col];

    /* Compute valid letters for this position */
    CrossSet cross_set = gen->row_cross_sets[col];

    /* Apply left extension set constraint */
    if (col <= gen->anchor_col) {
        cross_set &= gen->left_ext_set;
    }

    /* Apply right extension set at first position right of anchor */
    if (gen->tiles_played == 0 && col == gen->anchor_col + 1) {
        cross_set &= gen->right_ext_set;
    }

    if (current_letter != EMPTY_SQUARE) {
        /* Square has existing tile - must follow it in GADDAG */
        MachineLetter raw = UNBLANKED(current_letter);

        /* Search for matching arc */
        for (uint32_t i = node_index; ; i++) {
            uint32_t node = kwg_get_node(gen->kwg, i);
            if (KWG_TILE(node) == raw) {
                go_on(gen, col, current_letter, KWG_ARC_INDEX(node),
                      KWG_ACCEPTS(node), leftstrip, rightstrip);
                break;
            }
            if (KWG_IS_END(node)) {
                break;
            }
        }
    } else if (gen->rack.total > 0) {
        /* Empty square - try rack tiles */
        for (uint32_t i = node_index; ; i++) {
            uint32_t node = kwg_get_node(gen->kwg, i);
            MachineLetter tile = KWG_TILE(node);

            if (tile != 0) {  /* Skip separator */
                /* Check if tile is in rack and cross-set */
                int has_tile = gen->rack.counts[tile] > 0;
                int has_blank = gen->rack.counts[BLANK_TILE] > 0;

                if ((has_tile || has_blank) && in_cross_set(cross_set, tile)) {
                    uint32_t next_index = KWG_ARC_INDEX(node);
                    int accepts = KWG_ACCEPTS(node);

                    /* Try with actual tile */
                    if (has_tile) {
                        gen->rack.counts[tile]--;
                        gen->rack.total--;
                        gen->tiles_played++;

                        go_on(gen, col, tile, next_index, accepts,
                              leftstrip, rightstrip);

                        gen->tiles_played--;
                        gen->rack.total++;
                        gen->rack.counts[tile]++;
                    }

                    /* Try with blank */
                    if (has_blank) {
                        gen->rack.counts[BLANK_TILE]--;
                        gen->rack.total--;
                        gen->tiles_played++;

                        go_on(gen, col, BLANKED(tile), next_index, accepts,
                              leftstrip, rightstrip);

                        gen->tiles_played--;
                        gen->rack.total++;
                        gen->rack.counts[BLANK_TILE]++;
                    }
                }
            }

            if (KWG_IS_END(node)) {
                break;
            }
        }
    }
}

/*
 * Generate moves for a single row/column
 * (called twice per row - once horizontal, once vertical with transposed access)
 */
static void gen_for_row(MoveGenState *gen) {
    /* Find all anchors in this row */
    gen->last_anchor_col = BOARD_DIM;  /* Sentinel: no previous anchor */

    for (int col = 0; col < BOARD_DIM; col++) {
        if (!is_anchor(gen, col)) continue;

        gen->anchor_col = col;
        gen->tiles_played = 0;
        gen->main_word_score = 0;
        gen->cross_score = 0;
        gen->word_multiplier = 1;

        /* Compute left/right extension sets for this anchor */
        /* For now, allow all letters - full constraint would come from
           analyzing prefix/suffix patterns. This is a simplification. */
        gen->left_ext_set = TRIVIAL_CROSS_SET;
        gen->right_ext_set = TRIVIAL_CROSS_SET;

        /* Get GADDAG root */
        uint32_t root = kwg_get_gaddag_root(gen->kwg);

        /* Start recursive generation from anchor going left */
        recursive_gen(gen, col, root, col, col);

        gen->last_anchor_col = col;
    }
}

/*
 * Cache a row from the board for faster access during generation
 */
static void cache_row(MoveGenState *gen, int row, int dir) {
    gen->current_row = row;
    gen->dir = dir;

    for (int col = 0; col < BOARD_DIM; col++) {
        int board_row, board_col;
        if (dir == DIR_HORIZONTAL) {
            board_row = row;
            board_col = col;
        } else {
            board_row = col;
            board_col = row;
        }

        int idx = board_row * BOARD_DIM + board_col;
        const Square *sq = &gen->board->squares[idx];

        gen->row_letters[col] = sq->letter;
        gen->row_bonuses[col] = sq->bonus;

        if (dir == DIR_HORIZONTAL) {
            gen->row_cross_sets[col] = sq->cross_set_h;
            gen->row_cross_scores[col] = sq->cross_score_h;
        } else {
            gen->row_cross_sets[col] = sq->cross_set_v;
            gen->row_cross_scores[col] = sq->cross_score_v;
        }

        /* Compute anchor status: empty square adjacent to a tile */
        gen->row_is_anchor[col] = 0;
        if (sq->letter == EMPTY_SQUARE) {
            /* Check all 4 neighbors */
            int has_neighbor = 0;
            if (board_row > 0 && gen->board->squares[(board_row-1)*BOARD_DIM + board_col].letter != EMPTY_SQUARE) has_neighbor = 1;
            if (board_row < BOARD_DIM-1 && gen->board->squares[(board_row+1)*BOARD_DIM + board_col].letter != EMPTY_SQUARE) has_neighbor = 1;
            if (board_col > 0 && gen->board->squares[board_row*BOARD_DIM + board_col-1].letter != EMPTY_SQUARE) has_neighbor = 1;
            if (board_col < BOARD_DIM-1 && gen->board->squares[board_row*BOARD_DIM + board_col+1].letter != EMPTY_SQUARE) has_neighbor = 1;

            if (has_neighbor) {
                gen->row_is_anchor[col] = 1;
            }

            /* Special case: center square is anchor on empty board */
            if (board_row == 7 && board_col == 7 && gen->board->tiles_on_board == 0) {
                gen->row_is_anchor[col] = 1;
            }
        }
    }
}

/*
 * Main entry point: generate all legal moves
 */
/* Use draw_char from graphics.c for progress display */
extern void draw_char(int x, int y, char c, int pal);

static void show_progress(int n, int phase) {
    /* Show H00-H14/V00-V14 at bottom right - row 26 to avoid board overlap */
    /* Avoid division - use lookup table for 0-14 */
    static const char tens[] = "000000000011111";
    static const char ones[] = "012345678901234";

    draw_char(36, 26, (phase == 0) ? 'H' : 'V', 0);
    draw_char(37, 26, tens[n], 0);
    draw_char(38, 26, ones[n], 0);
}

static void show_recursion_count(void) {
    /* Show recursion counter - avoid division, just show low byte in hex */
    uint8_t n = (uint8_t)recursion_counter;
    draw_char(33, 26, "0123456789ABCDEF"[(n >> 4) & 0xF], 0);
    draw_char(34, 26, "0123456789ABCDEF"[n & 0xF], 0);
}

void generate_moves(const Board *board, const Rack *rack, const uint32_t *kwg,
                   MoveList *moves) {
    MoveGenState gen;
    memset(&gen, 0, sizeof(gen));

    gen.board = board;
    gen.kwg = kwg;

    /* Use first slot in moves array as best_move storage */
    gen.best_move = &moves->moves[0];
    gen.best_score = -32768;  /* Start with minimum score */
    gen.move_count = 0;

    /* Reset recursion counter */
    recursion_counter = 0;

    /* Copy rack (we modify it during generation) */
    memcpy(&gen.rack, rack, sizeof(Rack));

    /* Generate horizontal moves */
    for (int row = 0; row < BOARD_DIM; row++) {
        show_progress(row, 0);
        cache_row(&gen, row, DIR_HORIZONTAL);
        gen_for_row(&gen);
    }

    /* Generate vertical moves */
    for (int col = 0; col < BOARD_DIM; col++) {
        show_progress(col, 1);
        cache_row(&gen, col, DIR_VERTICAL);
        gen_for_row(&gen);
    }

    /* Set count: 0 if no moves found, 1 if best move found */
    moves->count = (gen.best_score > -32768) ? 1 : 0;
}

/*
 * Compare moves by score (for sorting)
 */
static int compare_moves(const void *a, const void *b) {
    const Move *ma = (const Move *)a;
    const Move *mb = (const Move *)b;
    return mb->score - ma->score;  /* Descending */
}

/*
 * Sort move list by score - swaps best move to position 0
 * (Full sort is too slow on 68000, just find max)
 */
void sort_moves_by_score(MoveList *moves) {
    if (moves->count <= 1) return;

    /* Find index of best move */
    int best_idx = 0;
    Score best_score = moves->moves[0].score;

    for (int i = 1; i < moves->count; i++) {
        if (moves->moves[i].score > best_score) {
            best_score = moves->moves[i].score;
            best_idx = i;
        }
    }

    /* Swap best move to position 0 */
    if (best_idx != 0) {
        Move tmp = moves->moves[0];
        moves->moves[0] = moves->moves[best_idx];
        moves->moves[best_idx] = tmp;
    }
}
