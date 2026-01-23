/*
 * Scrabble for Sega Genesis
 * Core definitions and data structures
 */

#ifndef SCRABBLE_H
#define SCRABBLE_H

#include <stdint.h>

/* Board dimensions */
#define BOARD_DIM 15
#define BOARD_SIZE (BOARD_DIM * BOARD_DIM)

/* Rack and tile limits */
#define RACK_SIZE 7
#define MAX_TILES_IN_BAG 100
#define ALPHABET_SIZE 27  /* A-Z + blank */

/* Machine letter encoding - names match original magpie (letter_distribution_defs.h) */
typedef uint8_t MachineLetter;

enum {
    ALPHABET_EMPTY_SQUARE_MARKER = 0,  /* Empty board square */
    BLANK_MACHINE_LETTER = 0,          /* Blank tile index in rack/counts */
    BLANK_MASK = 0x80,                 /* Bit set on blanked letters */
    UNBLANK_MASK = 0x7F,               /* Mask to get unblanked letter */
    PLAYED_THROUGH_MARKER = 0xFF,      /* Marker for played-through tiles in Move.tiles */
};

#define ML_BLANK 0
#define ML_A 1
#define ML_Z 26
#define ML_SEPARATOR 0  /* GADDAG separator */

/* Blanked letter macros - use BLANK_MASK for consistency with original magpie */
#define IS_BLANKED(ml) ((ml) & BLANK_MASK)
#define UNBLANKED(ml) ((ml) & UNBLANK_MASK)
#define BLANKED(ml) ((ml) | BLANK_MASK)

/* Direction */
#define DIR_HORIZONTAL 0
#define DIR_VERTICAL 1

/* Game event types - matches original magpie game_event_t */
typedef enum {
    GAME_EVENT_TILE_PLACEMENT_MOVE,
    GAME_EVENT_PASS,
    GAME_EVENT_EXCHANGE,
} game_event_t;

/* Bonus square types */
typedef enum {
    BONUS_NONE = 0,
    BONUS_DL,   /* Double letter */
    BONUS_TL,   /* Triple letter */
    BONUS_DW,   /* Double word */
    BONUS_TW,   /* Triple word */
    BONUS_CENTER /* Center star (also DW) */
} BonusType;

/* USE_BONUS_TRANSPOSE: when 1, add h_bonuses/v_bonuses arrays for pointer-based
 * access in cache_row instead of computing transposed indices each time.
 * Default 0 to match baseline. Override with -DUSE_BONUS_TRANSPOSE=1. */
#ifndef USE_BONUS_TRANSPOSE
#define USE_BONUS_TRANSPOSE 0
#endif

/* Convert points to eighths for equity calculation */
#define TO_EIGHTHS(x) ((x) * 8)

/*
 * Static evaluation constants (matching original magpie static_eval_defs.h)
 * Converted from EQUITY_RESOLUTION=1000 to EQUITY_RESOLUTION=8 (eighths)
 */
/* OPENING_HOTSPOT_PENALTY: -0.7 points in eighths (-6/8 = -0.75, closest to -0.7) */
#define OPENING_HOTSPOT_PENALTY (-6)
/* NON_OUTPLAY_CONSTANT_PENALTY: 10.0 points in eighths */
#define NON_OUTPLAY_CONSTANT_PENALTY TO_EIGHTHS(10)

/* Cross-set: bitmap of valid letters for a square */
typedef uint32_t CrossSet;
#define TRIVIAL_CROSS_SET 0xFFFFFFFE  /* All letters valid (bit 0 unused) */
#define EMPTY_CROSS_SET 0

/* Equity type - used for both scores and equity values */
typedef int16_t Equity;

/*
 * Board state - Structure of Arrays (SoA) with horizontal and vertical views.
 *
 * INTENTIONAL DIVERGENCE FROM ORIGINAL MAGPIE: Original magpie uses Array of
 * Structures (AoS) with a Square struct per board position. Genesis uses SoA
 * with separate h_* and v_* arrays to enable the 68000's (A0)+ auto-increment
 * addressing mode, providing ~2% speedup (validated: AoS=0x3A2C, SoA=0x38F7).
 *
 * Layout:
 * - Horizontal scans use h_* arrays (row-major: index = row*15 + col)
 * - Vertical scans use v_* arrays (transposed: index = col*15 + row)
 *
 * Both views are kept in sync when tiles are placed.
 */
typedef struct {
    /* ---------------------------------------------------------
     * HORIZONTAL VIEW (row-major: index = row*15 + col)
     * Used for horizontal move generation
     * --------------------------------------------------------- */
    uint8_t  h_letters[BOARD_SIZE];      /* Tiles on board */
    CrossSet h_cross_sets[BOARD_SIZE];   /* Cross-sets for horizontal plays */
    int16_t  h_cross_scores[BOARD_SIZE]; /* Cross-word scores for horizontal (in eighths) */
    CrossSet h_leftx[BOARD_SIZE];        /* Left extension sets */
    CrossSet h_rightx[BOARD_SIZE];       /* Right extension sets */

    /* ---------------------------------------------------------
     * VERTICAL VIEW (transposed: index = col*15 + row)
     * Used for vertical move generation - same data, different layout
     * --------------------------------------------------------- */
    uint8_t  v_letters[BOARD_SIZE];      /* Tiles (transposed) */
    CrossSet v_cross_sets[BOARD_SIZE];   /* Cross-sets for vertical plays */
    int16_t  v_cross_scores[BOARD_SIZE]; /* Cross-word scores for vertical (in eighths) */
    CrossSet v_leftx[BOARD_SIZE];        /* Left extension sets (transposed) */
    CrossSet v_rightx[BOARD_SIZE];       /* Right extension sets (transposed) */

    /* ---------------------------------------------------------
     * SHARED DATA (not direction-specific)
     * --------------------------------------------------------- */
    uint8_t bonuses[BOARD_SIZE];         /* Bonus squares (DL, TL, DW, TW) - row major */

#if USE_BONUS_TRANSPOSE
    /* Transposed bonus arrays for pointer-based access in cache_row.
     * h_bonuses is row-major (same as bonuses), v_bonuses is transposed. */
    uint8_t h_bonuses[BOARD_SIZE];       /* Horizontal view bonuses */
    uint8_t v_bonuses[BOARD_SIZE];       /* Vertical view bonuses (transposed) */
#endif

    /* Opening move penalties for vowels on hotspot squares.
     * Index: dir * BOARD_DIM + col (horizontal) or dir * BOARD_DIM + row (vertical).
     * Applied when placing vowels adjacent to center on opening move. */
    int8_t opening_move_penalties[BOARD_DIM * 2];

    uint8_t tiles_played;
} Board;

/* Player rack */
typedef struct {
    uint8_t counts[ALPHABET_SIZE];  /* Count of each letter */
    uint8_t total;                   /* Total tiles in rack */
} Rack;

/* Tile bag */
typedef struct {
    MachineLetter tiles[MAX_TILES_IN_BAG];
    uint8_t count;
} Bag;

/* A move - matches original magpie Move struct */
#define MAX_MOVE_TILES 15

typedef struct {
    Equity score;
    Equity equity;
    game_event_t move_type;
    uint8_t row_start;
    uint8_t col_start;
    uint8_t tiles_played;  /* Number of tiles played or exchanged */
    uint8_t tiles_length;  /* Equal to tiles_played for exchanges */
    uint8_t dir;
    MachineLetter tiles[MAX_MOVE_TILES];
} Move;

/* Move list for generation */
#define MAX_MOVES 256

typedef struct {
    Move moves[MAX_MOVES];
    uint16_t count;
} MoveList;

/* Player state */
typedef struct {
    Rack rack;
    Equity score;
    uint8_t player_num;
} Player;

/* Game state */
typedef struct {
    Board board;
    Player players[2];
    Bag bag;
    uint8_t current_player;
    uint8_t passes;
    uint8_t game_over;
} GameState;

/* Tile scores (standard Scrabble values) */
extern const uint8_t TILE_SCORES[ALPHABET_SIZE];

/* Vowel detection: 1 if letter is a vowel (A, E, I, O, U), 0 otherwise */
extern const uint8_t IS_VOWEL[ALPHABET_SIZE];

/* Tile distribution (standard Scrabble) */
extern const uint8_t TILE_COUNTS[ALPHABET_SIZE];

/* Board bonus layout (computed at init from symmetric quarter) */
extern uint8_t BONUS_LAYOUT[BOARD_SIZE];

/* Function prototypes */

/* Board functions */
void board_init(Board *board);
void board_place_tile(Board *board, uint8_t row, uint8_t col, MachineLetter tile);
MachineLetter board_get_tile(const Board *board, uint8_t row, uint8_t col);
int board_is_empty(const Board *board, uint8_t row, uint8_t col);
void board_update_cross_sets(Board *board, const uint32_t *kwg);
void board_update_cross_sets_for_move(Board *board, const uint32_t *kwg, const Move *move);
void board_apply_move(Board *board, const Move *move);

/* Rack functions */
void rack_init(Rack *rack);
void rack_add_tile(Rack *rack, MachineLetter tile);
int rack_remove_tile(Rack *rack, MachineLetter tile);
int rack_has_tile(const Rack *rack, MachineLetter tile);
void rack_to_string(const Rack *rack, char *buf);

/* Bag functions */
void bag_init(Bag *bag);
void bag_shuffle(Bag *bag);
MachineLetter bag_draw(Bag *bag);
void bag_refill_rack(Bag *bag, Rack *rack);
void bag_return_tiles(Bag *bag, const MachineLetter *tiles, int count);

/* Forward declaration for KLV */
struct KLV;
typedef struct KLV KLV;

/* Move generation */
void generate_moves(const Board *board, const Rack *rack, const Rack *opp_rack,
                    const uint32_t *kwg, const KLV *klv, const Bag *bag,
                    MoveList *moves);
void sort_moves_by_score(MoveList *moves);

/* Scoring */
Equity score_move(const Board *board, const Move *move);

/* Game logic */
void game_init(GameState *game);
int game_play_move(GameState *game, const Move *move);
void game_pass(GameState *game);
int game_exchange(GameState *game, const MachineLetter *tiles, int count);
int game_is_over(const GameState *game);

/* RNG */
void rng_seed(uint32_t seed);

#endif /* SCRABBLE_H */
