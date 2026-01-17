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
#define BLANK_TILE 0
#define ALPHABET_SIZE 27  /* A-Z + blank */

/* Machine letter encoding (0 = blank/separator, 1-26 = A-Z) */
typedef uint8_t MachineLetter;

#define ML_BLANK 0
#define ML_A 1
#define ML_Z 26
#define ML_SEPARATOR 0  /* GADDAG separator */
#define EMPTY_SQUARE 0xFF

/* Blanked letter has bit 7 set */
#define BLANK_BIT 0x80
#define IS_BLANKED(ml) ((ml) & BLANK_BIT)
#define UNBLANKED(ml) ((ml) & 0x7F)
#define BLANKED(ml) ((ml) | BLANK_BIT)

/* Direction */
#define DIR_HORIZONTAL 0
#define DIR_VERTICAL 1

/* Bonus square types */
typedef enum {
    BONUS_NONE = 0,
    BONUS_DL,   /* Double letter */
    BONUS_TL,   /* Triple letter */
    BONUS_DW,   /* Double word */
    BONUS_TW,   /* Triple word */
    BONUS_CENTER /* Center star (also DW) */
} BonusType;

/* Cross-set: bitmap of valid letters for a square */
typedef uint32_t CrossSet;
#define TRIVIAL_CROSS_SET 0xFFFFFFFE  /* All letters valid (bit 0 unused) */
#define EMPTY_CROSS_SET 0

/* Score type */
typedef int16_t Score;

/* Square on the board */
typedef struct {
    MachineLetter letter;    /* Placed tile or EMPTY_SQUARE */
    uint8_t bonus;           /* BonusType */
    CrossSet cross_set_h;    /* Cross-set for horizontal plays */
    CrossSet cross_set_v;    /* Cross-set for vertical plays */
    int8_t cross_score_h;    /* Cross-word score for horizontal */
    int8_t cross_score_v;    /* Cross-word score for vertical */
} Square;

/* Board state */
typedef struct {
    Square squares[BOARD_SIZE];
    uint8_t tiles_on_board;
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

/* A move (tile placement) */
#define MAX_MOVE_TILES 15

typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t dir;
    uint8_t tiles_played;
    uint8_t tiles_length;
    Score score;
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
    Score score;
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
void generate_moves(const Board *board, const Rack *rack, const uint32_t *kwg,
                    const KLV *klv, const Bag *bag, MoveList *moves);
void sort_moves_by_score(MoveList *moves);

/* Scoring */
Score score_move(const Board *board, const Move *move);

/* Game logic */
void game_init(GameState *game);
int game_play_move(GameState *game, const Move *move);
void game_pass(GameState *game);
int game_exchange(GameState *game, const MachineLetter *tiles, int count);
int game_is_over(const GameState *game);

/* RNG */
void rng_seed(uint32_t seed);

#endif /* SCRABBLE_H */
