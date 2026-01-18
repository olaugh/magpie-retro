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
        board->squares[i].letter = EMPTY_SQUARE;
        board->squares[i].bonus = BONUS_LAYOUT[i];
        board->squares[i].cross_set_h = TRIVIAL_CROSS_SET;
        board->squares[i].cross_set_v = TRIVIAL_CROSS_SET;
        board->squares[i].cross_score_h = -1;  /* -1 = no cross word */
        board->squares[i].cross_score_v = -1;
    }
    board->tiles_on_board = 0;
}

/*
 * Place a tile on the board
 */
void board_place_tile(Board *board, uint8_t row, uint8_t col, MachineLetter tile) {
    int idx = row * BOARD_DIM + col;
    if (board->squares[idx].letter == EMPTY_SQUARE) {
        board->tiles_on_board++;
    }
    board->squares[idx].letter = tile;
}

/*
 * Get tile at position
 */
MachineLetter board_get_tile(const Board *board, uint8_t row, uint8_t col) {
    return board->squares[row * BOARD_DIM + col].letter;
}

/*
 * Check if position is empty
 */
int board_is_empty(const Board *board, uint8_t row, uint8_t col) {
    return board->squares[row * BOARD_DIM + col].letter == EMPTY_SQUARE;
}

/*
 * Update cross-sets after a move is played
 * This recomputes cross-sets for all empty squares adjacent to tiles
 */
void board_update_cross_sets(Board *board, const uint32_t *kwg) {
    for (int row = 0; row < BOARD_DIM; row++) {
        for (int col = 0; col < BOARD_DIM; col++) {
            int idx = row * BOARD_DIM + col;
            Square *sq = &board->squares[idx];

            if (sq->letter != EMPTY_SQUARE) {
                /* Occupied squares don't need cross-sets */
                sq->cross_set_h = 0;
                sq->cross_set_v = 0;
                continue;
            }

            /* Compute horizontal cross-set (letters above/below) */
            MachineLetter prefix[BOARD_DIM];
            MachineLetter suffix[BOARD_DIM];
            int prefix_len = 0;
            int suffix_len = 0;

            /* Check vertical neighbors for horizontal cross-set */
            /* Keep blank flag intact so compute_cross_set can score blanks as 0 */
            for (int r = row - 1; r >= 0; r--) {
                MachineLetter ml = board->squares[r * BOARD_DIM + col].letter;
                if (ml == EMPTY_SQUARE) break;
                prefix[prefix_len++] = ml;
            }
            for (int r = row + 1; r < BOARD_DIM; r++) {
                MachineLetter ml = board->squares[r * BOARD_DIM + col].letter;
                if (ml == EMPTY_SQUARE) break;
                suffix[suffix_len++] = ml;
            }

            if (prefix_len == 0 && suffix_len == 0) {
                /* No vertical neighbors - all letters valid */
                sq->cross_set_h = TRIVIAL_CROSS_SET;
                sq->cross_score_h = -1;
            } else {
                /* Reverse prefix (it was collected top-to-bottom) */
                for (int i = 0; i < prefix_len / 2; i++) {
                    MachineLetter tmp = prefix[i];
                    prefix[i] = prefix[prefix_len - 1 - i];
                    prefix[prefix_len - 1 - i] = tmp;
                }
                sq->cross_set_h = compute_cross_set(kwg, prefix, prefix_len,
                                                     suffix, suffix_len,
                                                     &sq->cross_score_h);
            }

            /* Compute vertical cross-set (letters left/right) */
            prefix_len = 0;
            suffix_len = 0;

            /* Keep blank flag intact so compute_cross_set can score blanks as 0 */
            for (int c = col - 1; c >= 0; c--) {
                MachineLetter ml = board->squares[row * BOARD_DIM + c].letter;
                if (ml == EMPTY_SQUARE) break;
                prefix[prefix_len++] = ml;
            }
            for (int c = col + 1; c < BOARD_DIM; c++) {
                MachineLetter ml = board->squares[row * BOARD_DIM + c].letter;
                if (ml == EMPTY_SQUARE) break;
                suffix[suffix_len++] = ml;
            }

            if (prefix_len == 0 && suffix_len == 0) {
                sq->cross_set_v = TRIVIAL_CROSS_SET;
                sq->cross_score_v = -1;
            } else {
                /* Reverse prefix */
                for (int i = 0; i < prefix_len / 2; i++) {
                    MachineLetter tmp = prefix[i];
                    prefix[i] = prefix[prefix_len - 1 - i];
                    prefix[prefix_len - 1 - i] = tmp;
                }
                sq->cross_set_v = compute_cross_set(kwg, prefix, prefix_len,
                                                     suffix, suffix_len,
                                                     &sq->cross_score_v);
            }
        }
    }
}

/*
 * Apply a move to the board
 */
void board_apply_move(Board *board, const Move *move) {
    int row = move->row;
    int col = move->col;
    int dir = move->dir;

    for (int i = 0; i < move->tiles_length; i++) {
        MachineLetter tile = move->tiles[i];
        if (tile != 0xFF) {  /* Not a play-through marker */
            int r = (dir == DIR_HORIZONTAL) ? row : row + i;
            int c = (dir == DIR_HORIZONTAL) ? col + i : col;
            board_place_tile(board, r, c, tile);
        }
    }
}
