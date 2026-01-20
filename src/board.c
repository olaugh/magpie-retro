/*
 * Board representation and management
 */

#include "scrabble.h"
#include "kwg.h"

/* From libc.c */
extern void *memset(void *s, int c, unsigned long n);

/* Standard Scrabble tile scores */
const uint8_t TILE_SCORES[ALPHABET_SIZE] = {
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

/* Standard Scrabble tile counts */
const uint8_t TILE_COUNTS[ALPHABET_SIZE] = {
    2,  /* Blank */
    9,  /* A */
    2,  /* B */
    2,  /* C */
    4,  /* D */
    12, /* E */
    2,  /* F */
    3,  /* G */
    2,  /* H */
    9,  /* I */
    1,  /* J */
    1,  /* K */
    4,  /* L */
    2,  /* M */
    6,  /* N */
    8,  /* O */
    2,  /* P */
    1,  /* Q */
    6,  /* R */
    4,  /* S */
    6,  /* T */
    4,  /* U */
    2,  /* V */
    2,  /* W */
    1,  /* X */
    2,  /* Y */
    1   /* Z */
};

/*
 * Standard Scrabble board bonus layout
 * Using 1/4 of the board (top-left quadrant) - symmetric
 *
 * TW = Triple Word, DW = Double Word
 * TL = Triple Letter, DL = Double Letter
 * C  = Center (Double Word)
 */
static const uint8_t BONUS_QUARTER[64] = {
    /* Row 0 */ BONUS_TW, BONUS_NONE, BONUS_NONE, BONUS_DL, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_TW,
    /* Row 1 */ BONUS_NONE, BONUS_DW, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_TL, BONUS_NONE, BONUS_NONE,
    /* Row 2 */ BONUS_NONE, BONUS_NONE, BONUS_DW, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_DL, BONUS_NONE,
    /* Row 3 */ BONUS_DL, BONUS_NONE, BONUS_NONE, BONUS_DW, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_DL,
    /* Row 4 */ BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_DW, BONUS_NONE, BONUS_NONE, BONUS_NONE,
    /* Row 5 */ BONUS_NONE, BONUS_TL, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_TL, BONUS_NONE, BONUS_NONE,
    /* Row 6 */ BONUS_NONE, BONUS_NONE, BONUS_DL, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_DL, BONUS_NONE,
    /* Row 7 */ BONUS_TW, BONUS_NONE, BONUS_NONE, BONUS_DL, BONUS_NONE, BONUS_NONE, BONUS_NONE, BONUS_CENTER
};

/* Full board layout (computed at init from quarter) */
uint8_t BONUS_LAYOUT[BOARD_SIZE];

/* Initialize bonus layout from quarter */
static void init_bonus_layout(void) {
    for (int row = 0; row < BOARD_DIM; row++) {
        for (int col = 0; col < BOARD_DIM; col++) {
            int qr = row < 8 ? row : (14 - row);
            int qc = col < 8 ? col : (14 - col);
            BONUS_LAYOUT[row * BOARD_DIM + col] = BONUS_QUARTER[qr * 8 + qc];
        }
    }
}

/*
 * Initialize a board to empty state with bonus squares
 */
void board_init(Board *board) {
    static int layout_initialized = 0;
    if (!layout_initialized) {
        init_bonus_layout();
        layout_initialized = 1;
    }

    for (int i = 0; i < BOARD_SIZE; i++) {
        /* Horizontal view */
        board->h_letters[i] = ALPHABET_EMPTY_SQUARE_MARKER;
        board->h_cross_sets[i] = TRIVIAL_CROSS_SET;
        board->h_cross_scores[i] = -1;  /* -1 = no cross word */
        board->h_leftx[i] = TRIVIAL_CROSS_SET;
        board->h_rightx[i] = TRIVIAL_CROSS_SET;

        /* Vertical view (same initial values) */
        board->v_letters[i] = ALPHABET_EMPTY_SQUARE_MARKER;
        board->v_cross_sets[i] = TRIVIAL_CROSS_SET;
        board->v_cross_scores[i] = -1;
        board->v_leftx[i] = TRIVIAL_CROSS_SET;
        board->v_rightx[i] = TRIVIAL_CROSS_SET;

        /* Shared */
        board->bonuses[i] = BONUS_LAYOUT[i];
    }
    board->tiles_on_board = 0;
}

/*
 * Place a tile on the board (updates both horizontal and vertical views)
 */
void board_place_tile(Board *board, uint8_t row, uint8_t col, MachineLetter tile) {
    int h_idx = row * BOARD_DIM + col;      /* Horizontal view index */
    int v_idx = col * BOARD_DIM + row;      /* Vertical view index (transposed) */

    if (board->h_letters[h_idx] == ALPHABET_EMPTY_SQUARE_MARKER) {
        board->tiles_on_board++;
    }
    board->h_letters[h_idx] = tile;
    board->v_letters[v_idx] = tile;
}

/*
 * Get tile at position
 */
MachineLetter board_get_tile(const Board *board, uint8_t row, uint8_t col) {
    return board->h_letters[row * BOARD_DIM + col];
}

/*
 * Check if position is empty
 */
int board_is_empty(const Board *board, uint8_t row, uint8_t col) {
    return board->h_letters[row * BOARD_DIM + col] == ALPHABET_EMPTY_SQUARE_MARKER;
}

/*
 * Update cross-sets and extension sets after a move is played.
 * This recomputes for all empty squares adjacent to tiles.
 *
 * Cross-sets are for the PERPENDICULAR direction (checking cross-words).
 * Extension sets are for the MAIN word direction (checking word extensions).
 *
 * For horizontal plays (h_* arrays):
 *   - h_cross_sets comes from VERTICAL neighbors (cross-words above/below)
 *   - h_leftx, h_rightx come from HORIZONTAL neighbors (main word left/right)
 *
 * For vertical plays (v_* arrays):
 *   - v_cross_sets comes from HORIZONTAL neighbors (cross-words left/right)
 *   - v_leftx, v_rightx come from VERTICAL neighbors (main word above/below)
 */
void board_update_cross_sets(Board *board, const uint32_t *kwg) {
    for (int row = 0; row < BOARD_DIM; row++) {
        for (int col = 0; col < BOARD_DIM; col++) {
            int h_idx = row * BOARD_DIM + col;  /* Horizontal view index */
            int v_idx = col * BOARD_DIM + row;  /* Vertical view index (transposed) */

            if (board->h_letters[h_idx] != ALPHABET_EMPTY_SQUARE_MARKER) {
                /* Occupied squares: no cross-sets, no extension sets */
                board->h_cross_sets[h_idx] = 0;
                board->h_leftx[h_idx] = 0;
                board->h_rightx[h_idx] = 0;
                board->v_cross_sets[v_idx] = 0;
                board->v_leftx[v_idx] = 0;
                board->v_rightx[v_idx] = 0;
                continue;
            }

            MachineLetter prefix[BOARD_DIM];
            MachineLetter suffix[BOARD_DIM];
            int prefix_len = 0;
            int suffix_len = 0;

            /*
             * VERTICAL neighbors: compute h_cross_sets AND v_leftx/v_rightx
             *
             * For h_cross_sets: prefix=above, suffix=below, find valid middle letters
             * For extension sets (vertical plays):
             *   - v_leftx (front hooks) = letters that can START a word ending in suffix
             *   - v_rightx (back hooks) = letters that can CONTINUE a word starting with prefix
             */
            for (int r = row - 1; r >= 0; r--) {
                MachineLetter ml = board->h_letters[r * BOARD_DIM + col];
                if (ml == ALPHABET_EMPTY_SQUARE_MARKER) break;
                prefix[prefix_len++] = ml;
            }
            for (int r = row + 1; r < BOARD_DIM; r++) {
                MachineLetter ml = board->h_letters[r * BOARD_DIM + col];
                if (ml == ALPHABET_EMPTY_SQUARE_MARKER) break;
                suffix[suffix_len++] = ml;
            }

            if (prefix_len == 0 && suffix_len == 0) {
                board->h_cross_sets[h_idx] = TRIVIAL_CROSS_SET;
                board->h_cross_scores[h_idx] = -1;
                board->v_leftx[v_idx] = TRIVIAL_CROSS_SET;
                board->v_rightx[v_idx] = TRIVIAL_CROSS_SET;
            } else {
                /* Reverse prefix (collected bottom-to-top, need top-to-bottom) */
                for (int i = 0; i < prefix_len / 2; i++) {
                    MachineLetter tmp = prefix[i];
                    prefix[i] = prefix[prefix_len - 1 - i];
                    prefix[prefix_len - 1 - i] = tmp;
                }
                board->h_cross_sets[h_idx] = compute_cross_set(kwg, prefix, prefix_len,
                                                                suffix, suffix_len,
                                                                &board->h_cross_scores[h_idx]);
                /* Extension sets for vertical plays:
                 * prefix = tiles above, suffix = tiles below
                 * v_leftx = front hooks for suffix
                 * v_rightx = back hooks for prefix */
                compute_extension_sets(kwg, prefix, prefix_len, suffix, suffix_len,
                                        &board->v_leftx[v_idx], &board->v_rightx[v_idx]);
            }

            /*
             * HORIZONTAL neighbors: compute v_cross_sets AND h_leftx/h_rightx
             */
            prefix_len = 0;
            suffix_len = 0;

            for (int c = col - 1; c >= 0; c--) {
                MachineLetter ml = board->h_letters[row * BOARD_DIM + c];
                if (ml == ALPHABET_EMPTY_SQUARE_MARKER) break;
                prefix[prefix_len++] = ml;
            }
            for (int c = col + 1; c < BOARD_DIM; c++) {
                MachineLetter ml = board->h_letters[row * BOARD_DIM + c];
                if (ml == ALPHABET_EMPTY_SQUARE_MARKER) break;
                suffix[suffix_len++] = ml;
            }

            if (prefix_len == 0 && suffix_len == 0) {
                board->v_cross_sets[v_idx] = TRIVIAL_CROSS_SET;
                board->v_cross_scores[v_idx] = -1;
                board->h_leftx[h_idx] = TRIVIAL_CROSS_SET;
                board->h_rightx[h_idx] = TRIVIAL_CROSS_SET;
            } else {
                /* Reverse prefix (collected right-to-left, need left-to-right) */
                for (int i = 0; i < prefix_len / 2; i++) {
                    MachineLetter tmp = prefix[i];
                    prefix[i] = prefix[prefix_len - 1 - i];
                    prefix[prefix_len - 1 - i] = tmp;
                }
                board->v_cross_sets[v_idx] = compute_cross_set(kwg, prefix, prefix_len,
                                                                suffix, suffix_len,
                                                                &board->v_cross_scores[v_idx]);
                /* Extension sets for horizontal plays:
                 * prefix = tiles left, suffix = tiles right
                 * h_leftx = front hooks for suffix
                 * h_rightx = back hooks for prefix */
                compute_extension_sets(kwg, prefix, prefix_len, suffix, suffix_len,
                                        &board->h_leftx[h_idx], &board->h_rightx[h_idx]);
            }
        }
    }
}

/*
 * Apply a move to the board
 */
void board_apply_move(Board *board, const Move *move) {
    int row = move->row_start;
    int col = move->col_start;
    int dir = move->dir;

    for (int i = 0; i < move->tiles_length; i++) {
        MachineLetter tile = move->tiles[i];
        if (tile != PLAYED_THROUGH_MARKER) {
            int r = (dir == DIR_HORIZONTAL) ? row : row + i;
            int c = (dir == DIR_HORIZONTAL) ? col + i : col;
            board_place_tile(board, r, c, tile);
        }
    }
}
