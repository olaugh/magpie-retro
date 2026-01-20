/*
 * Scrabble for Sega Genesis - Simplified main for debugging
 */

#include "scrabble.h"
#include "kwg.h"
#include "klv.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* VDP direct access for debugging */
#define VDP_DATA    (*((volatile unsigned short *)0xC00000))
#define VDP_CTRL    (*((volatile unsigned short *)0xC00004))

/* External graphics functions */
extern void vdp_init(void);
extern void init_palettes(void);
extern void init_tiles(void);
extern void clear_screen(void);
extern void draw_string(int x, int y, const char *str, int pal);
extern void draw_char(int x, int y, char c, int pal);
extern void draw_number(int x, int y, int num, int pal);
extern void draw_hex(int x, int y, uint32_t num, int pal);
extern void wait_vblank(void);
extern void update_display(const GameState *game, const void *history, int history_count, uint32_t move_frames);

/* External game functions */
extern void rng_seed(uint32_t seed);
extern void board_init(Board *board);
extern void bag_init(Bag *bag);
extern void bag_shuffle(Bag *bag);
extern void rack_init(Rack *rack);
extern void bag_refill_rack(Bag *bag, Rack *rack);
extern void rack_to_string(const Rack *rack, char *buf);
extern void game_init(GameState *game);
extern int game_play_move(GameState *game, const Move *move);
extern void game_pass(GameState *game);
extern int game_is_over(const GameState *game);
extern void board_update_cross_sets(Board *board, const uint32_t *kwg);
extern void board_update_cross_sets_for_move(Board *board, const uint32_t *kwg, const Move *move);
extern void generate_moves(const Board *board, const Rack *rack, const uint32_t *kwg,
                           const KLV *klv, const Bag *bag, MoveList *moves);
extern void sort_moves_by_score(MoveList *moves);

/* Debug globals from movegen.c */
extern volatile uint16_t debug_anchors_total;
extern volatile uint16_t debug_anchors_processed;
extern volatile uint16_t debug_wair_anchor_exists;
extern volatile int16_t debug_wair_shadow_equity;
extern volatile uint16_t debug_wair_anchor_popped;
extern volatile int16_t debug_wair_best_after;
extern volatile int16_t debug_cutoff_best;
extern volatile int16_t debug_cutoff_anchor;
extern volatile int16_t debug_first_popped_equity;
extern volatile int16_t debug_heap_max_equity;
extern volatile uint16_t debug_l9_heap_index;
extern volatile int16_t debug_l9_equity_after_insert;
extern volatile uint16_t debug_l9_insert_index;
extern volatile int16_t debug_l9_equity_before_insert;
extern volatile uint16_t debug_anchor_size;
extern volatile int16_t debug_gen_equity_at_assign;
extern volatile uint16_t debug_l9_right_extent;
extern volatile uint16_t debug_l9_cross_set_m9;
/* Debug for shadow_record calculation at L9 */
extern volatile uint16_t debug_l9_tiles_played;
extern volatile uint16_t debug_l9_num_unrestricted;
extern volatile int16_t debug_l9_tiles_score;
extern volatile int16_t debug_l9_main_score;
extern volatile int16_t debug_l9_perp_score;
extern volatile uint16_t debug_l9_word_mult;
extern volatile int16_t debug_l9_final_score;
extern volatile int16_t debug_l9_record_equity;
extern volatile int8_t debug_l9_ts0, debug_l9_ts1, debug_l9_ts2, debug_l9_ts3;
extern volatile uint16_t debug_l9_em0, debug_l9_em1, debug_l9_em2, debug_l9_em3;
extern volatile uint16_t debug_l9_record_count;
extern volatile int16_t debug_l9_max_equity;
extern volatile uint16_t debug_l9_is_playthrough;
extern volatile int16_t debug_l9_equity_after_shadow;

/* Frame counter (from boot.s vblank interrupt) */
extern volatile uint32_t frame_counter;

/* KWG lexicon (embedded in ROM) */
extern const uint32_t kwg_data[];

/* KLV leave values (embedded in ROM) */
extern const uint8_t klv_data[];

/* Lexicon name (set via -DLEXICON_NAME in Makefile) */
#ifndef LEXICON_NAME
#define LEXICON_NAME "UNKNOWN"
#endif

/* RAM buffer for word_counts (computed at startup) */
/* NWL23 KLV has 2209 nodes, need 2209 * 4 = ~9KB */
#define KLV_WORD_COUNTS_SIZE 2500
static uint32_t klv_word_counts[KLV_WORD_COUNTS_SIZE];
static KLV klv;

/* Controller reading */
#define CTRL_DATA_1 (*((volatile unsigned char *)0xA10003))
#define CTRL_CTRL_1 (*((volatile unsigned char *)0xA10009))

#define BTN_UP      0x01
#define BTN_DOWN    0x02
#define BTN_LEFT    0x04
#define BTN_RIGHT   0x08
#define BTN_B       0x10
#define BTN_C       0x20
#define BTN_A       0x40
#define BTN_START   0x80

static unsigned char read_controller(void) {
    unsigned char state = 0;

    CTRL_DATA_1 = 0x40;
    for (volatile int i = 0; i < 10; i++);
    unsigned char hi = CTRL_DATA_1;

    CTRL_DATA_1 = 0x00;
    for (volatile int i = 0; i < 10; i++);
    unsigned char lo = CTRL_DATA_1;

    if (!(hi & 0x01)) state |= BTN_UP;
    if (!(hi & 0x02)) state |= BTN_DOWN;
    if (!(hi & 0x04)) state |= BTN_LEFT;
    if (!(hi & 0x08)) state |= BTN_RIGHT;
    if (!(hi & 0x10)) state |= BTN_B;
    if (!(hi & 0x20)) state |= BTN_C;
    if (!(lo & 0x10)) state |= BTN_A;
    if (!(lo & 0x20)) state |= BTN_START;

    return state;
}

/* Game state */
static GameState game;
static MoveList moves;

/* Move history for display */
#define MAX_HISTORY 28
typedef struct {
    char word[16];     /* The word played (all uppercase) */
    uint16_t blanks;   /* Bitmask: bit i set if position i is a blank */
    int16_t score;
    int16_t equity;    /* Equity in eighths of a point */
    uint16_t frames;   /* Frames elapsed finding this move */
    uint8_t player;
} HistoryEntry;

static HistoryEntry history[MAX_HISTORY];
static int history_count = 0;
static uint32_t last_move_frames = 0;
static uint32_t total_frames = 0;

/* Add move to history - needs board to look up playthrough letters */
static void add_to_history(const Move *m, int player, const Board *board, uint16_t frames) {
    /* Shift history if full */
    if (history_count >= MAX_HISTORY) {
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            history[i] = history[i + 1];
        }
        history_count = MAX_HISTORY - 1;
    }

    HistoryEntry *h = &history[history_count++];
    h->score = m->score;
    h->equity = m->equity;
    h->frames = frames;
    h->player = player;
    h->blanks = 0;

    /* Extract word from tiles, looking at board for playthrough letters */
    int len = m->tiles_length;
    if (len > 15) len = 15;
    for (int i = 0; i < len; i++) {
        MachineLetter ml = m->tiles[i];
        uint8_t letter = UNBLANKED(ml);

        /* If tile is empty/invalid, look at the board */
        if (letter < 1 || letter > 26) {
            /* Calculate board position for this letter */
            int r = m->row_start;
            int c = m->col_start;
            if (m->dir == DIR_HORIZONTAL) {
                c += i;
            } else {
                r += i;
            }
            /* Get letter from board */
            int idx = r * BOARD_DIM + c;
            ml = board->h_letters[idx];
            letter = UNBLANKED(ml);
        }

        if (letter >= 1 && letter <= 26) {
            h->word[i] = 'A' + (letter - 1);
            /* Track blanks in bitmask */
            if (IS_BLANKED(ml)) {
                h->blanks |= (1 << i);
            }
        } else {
            h->word[i] = '?';
        }
    }
    h->word[len] = '\0';
}

/* draw_history is called through update_display */

static uint32_t game_number = 0;
static uint32_t current_seed = 0;

/* Draw status bar at bottom left: lexicon and seed */
static void draw_status_bar(void) {
    /* Row 27 (bottom of screen): "NWL23 #0" or "CSW24 #123" */
    draw_string(0, 27, LEXICON_NAME, 0);
    draw_string(6, 27, "#", 0);
    draw_number(7, 27, current_seed, 0);
}

int main(void) {
    /* Initialize controller port */
    CTRL_CTRL_1 = 0x40;

    /* Initialize VDP */
    vdp_init();
    init_palettes();
    init_tiles();

    /* Initialize KLV (leave values) - only once */
    clear_screen();
    draw_string(10, 12, "INITIALIZING...", 0);
    wait_vblank();
    klv_init(&klv, klv_data, klv_word_counts);

new_game:
    /* Seed RNG deterministically: 0, 1, 2, ... */
    current_seed = game_number;
    rng_seed(game_number);
    game_number++;

    /* Initialize game state */
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
    history_count = 0;

    /* Start timing from here - includes cross-sets, rendering, everything */
    uint32_t game_start_frames = frame_counter;

    board_update_cross_sets(&game.board, kwg_data);
    clear_screen();
    draw_status_bar();

    /* Auto-play loop */
    while (!game_is_over(&game)) {
        /* Show current state before generating moves */
        update_display(&game, history, history_count, last_move_frames);

        /* Generate moves for current player, tracking frame count */
        uint32_t start_frames = frame_counter;
        generate_moves(&game.board,
                      &game.players[game.current_player].rack,
                      kwg_data, &klv, &game.bag, &moves);
        last_move_frames = frame_counter - start_frames;

        if (moves.count > 0) {
            /* Play the best move (always index 0) */
            Move *best = &moves.moves[0];
            int current = game.current_player;

            /* Check if this is an exchange move */
            if (best->move_type == GAME_EVENT_EXCHANGE) {
                /* Exchange tiles */
                if (game_exchange(&game, best->tiles, best->tiles_played)) {
                    /* Add exchange to history as "-ABCDEF" */
                    HistoryEntry *h = &history[history_count < MAX_HISTORY ? history_count++ : MAX_HISTORY - 1];
                    h->word[0] = '-';
                    int len = best->tiles_played;
                    if (len > 14) len = 14;  /* Leave room for '-' and null */
                    for (int i = 0; i < len; i++) {
                        MachineLetter ml = best->tiles[i];
                        uint8_t letter = UNBLANKED(ml);
                        if (letter >= 1 && letter <= 26) {
                            h->word[1 + i] = 'A' + (letter - 1);
                        } else {
                            h->word[1 + i] = '?';  /* Blank */
                        }
                    }
                    h->word[1 + len] = '\0';
                    h->blanks = 0;
                    h->score = 0;
                    h->equity = best->equity;
                    h->frames = (uint16_t)last_move_frames;
                    h->player = current;
                }
            } else if (game_play_move(&game, best)) {
                /* Add to history (after move is played, so board has the tiles) */
                add_to_history(best, current, &game.board, (uint16_t)last_move_frames);

                /* Update cross sets for next move (incremental) */
                board_update_cross_sets_for_move(&game.board, kwg_data, best);
            }
        } else {
            /* No legal moves - pass */
            game_pass(&game);
        }

        /* Update display after move is played and added to history */
        update_display(&game, history, history_count, last_move_frames);
    }

    /* Game over - show final state with total time */
    update_display(&game, history, history_count, last_move_frames);

    /* Calculate total frames (all-inclusive: cross-sets, rendering, movegen) */
    total_frames = frame_counter - game_start_frames;

    /* Display total frames below rack (row 22) */
    draw_string(0, 22, "FRAMES:", 0);
    draw_hex(8, 22, total_frames, 0);

    /* Wait for button press to restart */
    while (1) {
        wait_vblank();
        unsigned char buttons = read_controller();
        if (buttons != 0) {
            /* Wait for button release */
            while (read_controller() != 0) {
                wait_vblank();
            }
            goto new_game;
        }
    }

    return 0;
}
