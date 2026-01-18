/*
 * GADDAG-based move generation for Scrabble
 *
 * This implements the Gordon/Appel algorithm using GADDAG structure:
 * 1. For each anchor (empty square adjacent to a tile), generate moves
 * 2. Start from anchor, traverse GADDAG going left first
 * 3. After hitting separator node, continue right from anchor
 * 4. Record valid words when accepts flag is set
 *
 * Move selection uses equity = score*8 + leave_value (in eighths of a point)
 * to prefer moves that leave good tiles for future turns.
 */

#include "scrabble.h"
#include "kwg.h"
#include "klv.h"
#include "anchor.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Set to 0 to disable shadow algorithm (for validation) */
/* Can be overridden via compiler flag: -DUSE_SHADOW=0 */
#ifndef USE_SHADOW
#define USE_SHADOW 1
#endif

/* Debug mode for tracking bad shadow cutoffs */
#ifndef USE_SHADOW_DEBUG
#define USE_SHADOW_DEBUG 0
#endif

#if USE_SHADOW_DEBUG
/* Info about a bad cutoff for debugging */
typedef struct {
    int row;
    int col;
    int dir;
    Equity bound;
    Equity best_before;
    Equity best_after;
} BadCutoffInfo;

/* Global debug counters (accessible from test harness) */
int shadow_debug_bad_cutoff_count = 0;
BadCutoffInfo shadow_debug_last_bad_cutoff = {0};
int shadow_debug_record_calls = 0;
int shadow_debug_leave_added = 0;
static int shadow_debug_turn = 0;  /* Tracks turns for correlating debug output */

/* Declaration for printf when building native debug */
extern int printf(const char *fmt, ...);
#endif

/* From libc.c */
extern void *memset(void *s, int c, unsigned long n);
extern void *memcpy(void *dest, const void *src, unsigned long n);

/*
 * UnrestrictedMultiplier: tracks multipliers at positions where
 * any tile could be placed during shadow play
 */
typedef struct {
    uint8_t multiplier;  /* Combined multiplier value */
    uint8_t column;      /* Column position */
} UnrestrictedMultiplier;

/* Move generation state */
typedef struct {
    const Board *board;
    const uint32_t *kwg;
    const KLV *klv;      /* Leave values (may be NULL) */
    Rack rack;           /* Mutable copy of player rack */
    Rack rack_shadow_right_copy;  /* Save rack state before shadow right */
    Move *best_move;     /* Track best move found so far */
    Equity best_equity;  /* Equity of best move (in eighths) */
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

    /* Leave tracking */
    LeaveMap leave_map;  /* For O(1) leave lookup during generation */
    LeaveMap leave_map_shadow_right_copy;  /* Save leave map for shadow right */

    /* Cache of current row */
    MachineLetter row_letters[BOARD_DIM];
    uint8_t row_bonuses[BOARD_DIM];
    CrossSet row_cross_sets[BOARD_DIM];
    int8_t row_cross_scores[BOARD_DIM];
    uint8_t row_is_anchor[BOARD_DIM];

    /* ===== Shadow algorithm state ===== */

    /* Shadow score tracking */
    Equity shadow_mainword_restricted_score;   /* Main word score (before word mult) */
    Equity shadow_perpendicular_additional_score;  /* Cross-word scores */
    uint8_t shadow_word_multiplier;            /* Accumulated word multiplier */

    /* Best shadow values found for current anchor */
    Equity highest_shadow_equity;
    int16_t highest_shadow_score;

    /* Shadow traversal position */
    int8_t shadow_left_col;
    int8_t shadow_right_col;

    /* Descending tile scores for computing upper bounds */
    /* Index 0 = highest, filled with rack tiles sorted descending */
    int8_t descending_tile_scores[RACK_SIZE];
    int8_t descending_tile_scores_copy[RACK_SIZE];  /* For restore after shadow right */

    /* Unrestricted multipliers (positions where any tile can go) */
    /* Kept sorted descending by effective multiplier */
    UnrestrictedMultiplier descending_cross_word_multipliers[RACK_SIZE];
    uint16_t descending_effective_letter_multipliers[RACK_SIZE];
    uint8_t num_unrestricted_multipliers;
    uint8_t last_word_multiplier;

    /* Copies for restore after shadow_play_right */
    UnrestrictedMultiplier desc_xw_muls_copy[RACK_SIZE];
    uint16_t desc_eff_letter_muls_copy[RACK_SIZE];

    /* Anchor heap for best-first processing */
    AnchorHeap anchor_heap;

    /* Bag count for leave calculation */
    uint8_t tiles_in_bag;

    /* Original rack total for shadow tile limit checks */
    uint8_t shadow_original_rack_total;

    /* Best leave values seen for each leave size (for shadow upper bound).
     * Index 0 = best 1-tile leave, index 6 = best 7-tile leave.
     * Updated during move generation, used by shadow_record. */
    Equity best_leaves[RACK_SIZE];
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

/*
 * ===== Shadow Algorithm Implementation =====
 *
 * Shadow playing computes an upper bound on the equity for each anchor.
 * By processing anchors in best-first order (using the anchor heap),
 * we can cut off move generation early when remaining anchors can't
 * possibly beat the current best move.
 *
 * The key insight: for "unrestricted" positions (where multiple tiles
 * could fit), assume the highest-scoring tiles go in the best multipliers.
 * This gives an upper bound on the score.
 */

/* Forward declaration - cache_row is defined later */
static void cache_row(MoveGenState *gen, int row, int dir);

/* Build descending tile scores array from rack */
static void build_descending_tile_scores(MoveGenState *gen) {
    int count = 0;

    /* Collect all tile scores from rack */
    int8_t scores[RACK_SIZE];
    for (MachineLetter ml = 0; ml < ALPHABET_SIZE && count < RACK_SIZE; ml++) {
        for (int i = 0; i < gen->rack.counts[ml] && count < RACK_SIZE; i++) {
            scores[count++] = tile_scores[ml];
        }
    }

    /* Sort descending (simple insertion sort - rack is small) */
    for (int i = 1; i < count; i++) {
        int8_t key = scores[i];
        int j = i - 1;
        while (j >= 0 && scores[j] < key) {
            scores[j + 1] = scores[j];
            j--;
        }
        scores[j + 1] = key;
    }

    /* Copy to gen and zero-fill rest */
    for (int i = 0; i < RACK_SIZE; i++) {
        gen->descending_tile_scores[i] = (i < count) ? scores[i] : 0;
    }
}

/* Build rack_bits: bitmap of tiles in rack (for cross-set intersection) */
static uint32_t build_rack_cross_set(const Rack *rack) {
    uint32_t bits = 0;
    for (MachineLetter ml = 0; ml < ALPHABET_SIZE; ml++) {
        if (rack->counts[ml] > 0) {
            bits |= (1U << ml);
        }
    }
    return bits;
}

/* Check if exactly one bit is set (tile is restricted to single letter) */
static inline int is_single_bit(uint32_t x) {
    return x && !(x & (x - 1));
}

/* Get index of lowest set bit */
static inline int get_lowest_bit_index(uint32_t x) {
    int index = 0;
    while ((x & 1) == 0) {
        x >>= 1;
        index++;
    }
    return index;
}

/* Remove a score from descending tile scores array */
static void remove_score_from_descending(MoveGenState *gen, int8_t score) {
    int count = gen->rack.total;
    for (int i = count; i-- > 0; ) {
        if (gen->descending_tile_scores[i] == score) {
            /* Shift left */
            for (int j = i; j < count; j++) {
                gen->descending_tile_scores[j] = gen->descending_tile_scores[j + 1];
            }
            gen->descending_tile_scores[count] = 0;
            return;
        }
    }
}

/* Insert multiplier into descending list for unrestricted positions */
static void insert_unrestricted_cross_word_mult(MoveGenState *gen, uint8_t mult, uint8_t col) {
    int insert_idx = gen->num_unrestricted_multipliers;
    for (; insert_idx > 0 &&
           gen->descending_cross_word_multipliers[insert_idx - 1].multiplier < mult;
         insert_idx--) {
        gen->descending_cross_word_multipliers[insert_idx] =
            gen->descending_cross_word_multipliers[insert_idx - 1];
    }
    gen->descending_cross_word_multipliers[insert_idx].multiplier = mult;
    gen->descending_cross_word_multipliers[insert_idx].column = col;
}

static void insert_unrestricted_eff_letter_mult(MoveGenState *gen, uint16_t mult) {
    int insert_idx = gen->num_unrestricted_multipliers;
    for (; insert_idx > 0 &&
           gen->descending_effective_letter_multipliers[insert_idx - 1] < mult;
         insert_idx--) {
        gen->descending_effective_letter_multipliers[insert_idx] =
            gen->descending_effective_letter_multipliers[insert_idx - 1];
    }
    gen->descending_effective_letter_multipliers[insert_idx] = mult;
}

/* Recalculate effective letter multipliers when word multiplier changes */
static void maybe_recalc_effective_multipliers(MoveGenState *gen) {
    if (gen->last_word_multiplier == gen->shadow_word_multiplier) {
        return;
    }
    gen->last_word_multiplier = gen->shadow_word_multiplier;

    int orig_count = gen->num_unrestricted_multipliers;
    gen->num_unrestricted_multipliers = 0;

    for (int i = 0; i < orig_count; i++) {
        uint8_t xw_mult = gen->descending_cross_word_multipliers[i].multiplier;
        uint8_t col = gen->descending_cross_word_multipliers[i].column;
        uint8_t letter_mult = get_letter_mult(gen->row_bonuses[col]);
        uint16_t eff_mult = gen->shadow_word_multiplier * letter_mult + xw_mult;
        insert_unrestricted_eff_letter_mult(gen, eff_mult);
        gen->num_unrestricted_multipliers++;
    }
}

/* Insert unrestricted multipliers for a column */
static void insert_unrestricted_multipliers(MoveGenState *gen, int col) {
    maybe_recalc_effective_multipliers(gen);

    uint8_t bonus = gen->row_bonuses[col];
    uint8_t this_word_mult = get_word_mult(bonus);
    uint8_t letter_mult = get_letter_mult(bonus);

    /* Check if there's a cross-word at this position */
    int is_cross_word = (gen->row_cross_scores[col] >= 0);

    uint8_t eff_xw_mult = letter_mult * this_word_mult * is_cross_word;
    insert_unrestricted_cross_word_mult(gen, eff_xw_mult, col);

    uint16_t main_word_mult = gen->shadow_word_multiplier * letter_mult;
    insert_unrestricted_eff_letter_mult(gen, main_word_mult + eff_xw_mult);

    gen->num_unrestricted_multipliers++;
}

/*
 * Try to restrict a tile to a single letter.
 * If only one tile can go here, remove it from rack and add its score.
 * If we don't have the letter but have a blank, use the blank (score 0).
 * Returns 1 if restricted, 0 if unrestricted.
 */
static int try_restrict_tile(MoveGenState *gen, uint32_t possible_tiles,
                              uint8_t letter_mult, uint8_t word_mult, int col) {
    if (!is_single_bit(possible_tiles)) {
        return 0;  /* Multiple tiles possible - unrestricted */
    }

    MachineLetter ml = get_lowest_bit_index(possible_tiles);
    int8_t score;

    /* Check if we have this letter, or need to use a blank */
    if (gen->rack.counts[ml] > 0) {
        /* Have the letter - use it */
        gen->rack.counts[ml]--;
        gen->rack.total--;
        if (gen->rack.counts[ml] == 0) {
            gen->rack_bits &= ~(1U << ml);
        }
        score = tile_scores[ml];
    } else if (gen->rack.counts[BLANK_TILE] > 0) {
        /* Use blank as this letter */
        gen->rack.counts[BLANK_TILE]--;
        gen->rack.total--;
        if (gen->rack.counts[BLANK_TILE] == 0) {
            gen->rack_bits &= ~(1U << BLANK_TILE);
        }
        score = 0;  /* Blank scores 0 */
    } else {
        /* Can't play here - shouldn't happen if possible_tiles was computed correctly */
        return 0;
    }

    remove_score_from_descending(gen, score);

    int16_t lsm = score * letter_mult;
    gen->shadow_mainword_restricted_score += lsm;

    /* Add perpendicular score if there's a cross-word */
    if (gen->row_cross_scores[col] >= 0) {
        gen->shadow_perpendicular_additional_score += lsm * word_mult;
    }

    return 1;
}

/* Get best leave value for a given number of tiles remaining.
 * Uses the best_leaves array which is updated during move generation. */
static Equity get_best_leave_for_tiles_remaining(const MoveGenState *gen, int tiles_remaining) {
    if (tiles_remaining <= 0 || tiles_remaining > RACK_SIZE) {
        return 0;
    }
    /* best_leaves is 0-indexed: index 0 = 1-tile leave, index 6 = 7-tile leave */
    return gen->best_leaves[tiles_remaining - 1];
}

/*
 * Record shadow score - updates highest_shadow_equity/score if this is better
 */
static void shadow_record(MoveGenState *gen) {
    /* Compute upper bound score: pair highest tiles with highest multipliers.
     * Loop over all RACK_SIZE positions - unexplored positions have multiplier 0. */
    Equity tiles_played_score = 0;
    for (int i = 0; i < RACK_SIZE; i++) {
        tiles_played_score += gen->descending_tile_scores[i] *
                              gen->descending_effective_letter_multipliers[i];
    }

    Equity bingo_bonus = (gen->tiles_played >= RACK_SIZE) ? 50 : 0;

    Equity score = tiles_played_score +
                   (gen->shadow_mainword_restricted_score * gen->shadow_word_multiplier) +
                   gen->shadow_perpendicular_additional_score + bingo_bonus;

    /* For equity, add best possible leave value.
     * Match actual move generation: add leave if KLV is available.
     * (Original checked tiles_in_bag > 0, but actual gen doesn't) */
    Equity equity = score * 8;  /* Convert to eighths */
#if USE_SHADOW_DEBUG
    shadow_debug_record_calls++;
#endif
    if (gen->klv != NULL) {
        /* Compute leave size: original rack count minus tiles played */
        int tiles_remaining = gen->shadow_original_rack_total - gen->tiles_played;
        Equity leave = get_best_leave_for_tiles_remaining(gen, tiles_remaining);
        equity += leave;
#if USE_SHADOW_DEBUG
        shadow_debug_leave_added++;
#endif
    }

    if (equity > gen->highest_shadow_equity) {
#if USE_SHADOW_DEBUG
        /* Debug for specific turns */
        if (shadow_debug_turn == 783) {
            printf("T1202 SHADOW_RECORD: score=%d tiles_played=%d main=%d wm=%d perp=%d leave=%d equity=%d->%d\n",
                   (int)score, gen->tiles_played,
                   (int)gen->shadow_mainword_restricted_score,
                   (int)gen->shadow_word_multiplier,
                   (int)gen->shadow_perpendicular_additional_score,
                   (int)(gen->klv ? get_best_leave_for_tiles_remaining(gen, gen->shadow_original_rack_total - gen->tiles_played) : 0),
                   (int)gen->highest_shadow_equity, (int)equity);
        }
#endif
        gen->highest_shadow_equity = equity;
#if USE_SHADOW_DEBUG
        /* Track when equity improves significantly */
        static int update_count = 0;
        update_count++;
#endif
    }
    if (score > gen->highest_shadow_score) {
        gen->highest_shadow_score = score;
    }
}

/* Forward declarations for shadow functions */
static void shadow_play_right(MoveGenState *gen, int is_unique);
static void nonplaythrough_shadow_play_left(MoveGenState *gen, int is_unique);
static void playthrough_shadow_play_left(MoveGenState *gen, int is_unique);

/*
 * Shadow play rightward from anchor
 */
static void shadow_play_right(MoveGenState *gen, int is_unique) {
#if USE_SHADOW_DEBUG
    if (shadow_debug_turn == 783) {
        int phys_row = (gen->dir == DIR_HORIZONTAL) ? gen->current_row : gen->shadow_right_col;
        int phys_col = (gen->dir == DIR_HORIZONTAL) ? gen->shadow_right_col : gen->current_row;
        printf("T1202 PLAY_RIGHT_ENTRY: phys(%d,%d,%c) right_col=%d tiles_played=%d rack_total=%d main=%d\n",
               phys_row, phys_col, gen->dir ? 'V' : 'H',
               gen->shadow_right_col, gen->tiles_played, gen->shadow_original_rack_total,
               (int)gen->shadow_mainword_restricted_score);
        /* Print row_letters for cols 0-5 */
        printf("T1202   row_letters[0-5]: ");
        for (int c = 0; c <= 5 && c < BOARD_DIM; c++) {
            printf("%d ", gen->row_letters[c]);
        }
        printf("\n");
    }
#endif
    /* Save state to restore after shadow right */
    Equity orig_main = gen->shadow_mainword_restricted_score;
    Equity orig_perp = gen->shadow_perpendicular_additional_score;
    uint8_t orig_wordmul = gen->shadow_word_multiplier;

    /* Save rack state */
    Rack rack_copy;
    memcpy(&rack_copy, &gen->rack, sizeof(Rack));
    uint32_t orig_rack_bits = gen->rack_bits;

    /* Save descending tile scores */
    int8_t desc_scores_copy[RACK_SIZE];
    memcpy(desc_scores_copy, gen->descending_tile_scores, sizeof(desc_scores_copy));

    /* Save unrestricted multiplier state */
    uint8_t orig_num_unrestricted = gen->num_unrestricted_multipliers;
    UnrestrictedMultiplier xw_copy[RACK_SIZE];
    uint16_t eff_copy[RACK_SIZE];
    memcpy(xw_copy, gen->descending_cross_word_multipliers, sizeof(xw_copy));
    memcpy(eff_copy, gen->descending_effective_letter_multipliers, sizeof(eff_copy));

    int orig_right_col = gen->shadow_right_col;
    int orig_tiles_played = gen->tiles_played;
    int restricted_any = 0;
    int changed_multipliers = 0;

    /* Extend rightward */
    for (;;) {
        gen->shadow_right_col++;

        if (gen->shadow_right_col >= BOARD_DIM) {
            break;
        }

        /* Check if there's a playthrough tile first */
        MachineLetter existing = gen->row_letters[gen->shadow_right_col];
        if (existing != EMPTY_SQUARE) {
            /* Play through existing tile - doesn't use a rack tile */
#if USE_SHADOW_DEBUG
            if (shadow_debug_turn == 783) {
                printf("T1202 PLAY_RIGHT_PLAYTHROUGH: col=%d letter=%d score=%d main=%d->%d\n",
                       gen->shadow_right_col, existing, get_tile_score(existing),
                       (int)gen->shadow_mainword_restricted_score,
                       (int)(gen->shadow_mainword_restricted_score + get_tile_score(existing)));
            }
#endif
            gen->shadow_mainword_restricted_score += get_tile_score(existing);
            continue;  /* Keep going right - no tile consumed */
        }

        /* Empty square - need a tile from rack */
        gen->tiles_played++;
        if (gen->tiles_played > gen->shadow_original_rack_total) {
            /* Rack exhausted. But if we already extended through playthroughs,
             * we should record that score. Check if there are MORE playthroughs
             * ahead (forming a longer word). */
            int scan_col = gen->shadow_right_col + 1;
            Equity pre_scan_main = gen->shadow_mainword_restricted_score;
            while (scan_col < BOARD_DIM) {
                MachineLetter ml = gen->row_letters[scan_col];
                if (ml == EMPTY_SQUARE) break;
                gen->shadow_mainword_restricted_score += get_tile_score(ml);
                scan_col++;
            }
#if USE_SHADOW_DEBUG
            if (shadow_debug_turn == 783 && gen->shadow_mainword_restricted_score > pre_scan_main) {
                printf("T1202 PLAY_RIGHT_EXTEND: right_col=%d scan_col=%d main=%d->%d\n",
                       gen->shadow_right_col, scan_col,
                       (int)pre_scan_main, (int)gen->shadow_mainword_restricted_score);
            }
#endif
            /* Record if we played at least 1 tile (the anchor) */
            if (gen->tiles_played >= 1) {
                maybe_recalc_effective_multipliers(gen);
                shadow_record(gen);
            }
            break;
        }

        /* Empty square - check what can go here */
        CrossSet cross_set = gen->row_cross_sets[gen->shadow_right_col];
        uint32_t cross_ext = cross_set & gen->right_ext_set;
        /* If rack has blank, blank can play as any letter in cross_set */
        uint32_t possible = (gen->rack.counts[BLANK_TILE] > 0)
            ? cross_ext
            : (cross_ext & gen->rack_bits);
        gen->right_ext_set = TRIVIAL_CROSS_SET;

        if (possible == 0) {
            break;
        }

        uint8_t bonus = gen->row_bonuses[gen->shadow_right_col];
        uint8_t letter_mult = get_letter_mult(bonus);
        uint8_t word_mult = get_word_mult(bonus);

        /* Add perpendicular cross-word score */
        if (gen->row_cross_scores[gen->shadow_right_col] >= 0) {
            gen->shadow_perpendicular_additional_score +=
                gen->row_cross_scores[gen->shadow_right_col] * word_mult;
        }
        gen->shadow_word_multiplier *= word_mult;

        if (try_restrict_tile(gen, possible, letter_mult, word_mult,
                              gen->shadow_right_col)) {
            restricted_any = 1;
        } else {
            insert_unrestricted_multipliers(gen, gen->shadow_right_col);
            changed_multipliers = 1;
        }

        if (cross_set == TRIVIAL_CROSS_SET) {
            is_unique = 1;
        }

        /* Record if valid play.
         * For shadow, always record - duplicate recordings are harmless
         * (they just compare the same equity and update if better).
         * The is_unique check is for actual move recording, not shadow bounds. */
        if (gen->tiles_played >= 1) {
            /* Before recording, scan forward for adjacent playthroughs.
             * These MUST be part of the word if we stop here. */
            Equity saved_main = gen->shadow_mainword_restricted_score;
            int scan_col = gen->shadow_right_col + 1;
            while (scan_col < BOARD_DIM) {
                MachineLetter scan_letter = gen->row_letters[scan_col];
                if (scan_letter == EMPTY_SQUARE) break;
                gen->shadow_mainword_restricted_score += get_tile_score(scan_letter);
                scan_col++;
            }
            maybe_recalc_effective_multipliers(gen);
            shadow_record(gen);
            /* Restore main score for continued exploration */
            gen->shadow_mainword_restricted_score = saved_main;
        }
    }

    /* Restore state */
    gen->shadow_mainword_restricted_score = orig_main;
    gen->shadow_perpendicular_additional_score = orig_perp;
    gen->shadow_word_multiplier = orig_wordmul;

    if (restricted_any) {
        memcpy(&gen->rack, &rack_copy, sizeof(Rack));
        gen->rack_bits = orig_rack_bits;
        memcpy(gen->descending_tile_scores, desc_scores_copy, sizeof(desc_scores_copy));
    }

    if (changed_multipliers) {
        gen->num_unrestricted_multipliers = orig_num_unrestricted;
        memcpy(gen->descending_cross_word_multipliers, xw_copy, sizeof(xw_copy));
        memcpy(gen->descending_effective_letter_multipliers, eff_copy, sizeof(eff_copy));
    }

    gen->shadow_right_col = orig_right_col;
    gen->tiles_played = orig_tiles_played;

    maybe_recalc_effective_multipliers(gen);
}

/*
 * Shadow play leftward for non-playthrough anchor
 */
static void nonplaythrough_shadow_play_left(MoveGenState *gen, int is_unique) {
    int has_blank = gen->rack.counts[BLANK_TILE] > 0;

#if USE_SHADOW_DEBUG
    if (shadow_debug_turn == 783 && gen->shadow_left_col == 0 && gen->dir == DIR_HORIZONTAL && gen->current_row == 2) {
        printf("T1178 NONPLAY_LEFT_ENTRY: left_col=%d right_col=%d tiles_played=%d rack_total=%d\n",
               gen->shadow_left_col, gen->shadow_right_col, gen->tiles_played, gen->shadow_original_rack_total);
        printf("T1178   right_ext_set=0x%x rack_bits=0x%x has_blank=%d\n",
               gen->right_ext_set, gen->rack_bits, has_blank);
        printf("T1178   row_letters[0-8]: ");
        for (int c = 0; c <= 8 && c < BOARD_DIM; c++) {
            printf("%d ", gen->row_letters[c]);
        }
        printf("\n");
    }
#endif

    for (;;) {
        /* Try extending right first */
        /* If rack has blank, blank can play as any letter */
        uint32_t possible_right = has_blank
            ? gen->right_ext_set
            : (gen->right_ext_set & gen->rack_bits);
#if USE_SHADOW_DEBUG
        if (shadow_debug_turn == 783 && gen->shadow_left_col == 0 && gen->dir == DIR_HORIZONTAL && gen->current_row == 2) {
            printf("T1178 NONPLAY_LEFT_LOOP: possible_right=0x%x\n", possible_right);
        }
#endif
        if (possible_right != 0) {
            shadow_play_right(gen, is_unique);
        }
        gen->right_ext_set = TRIVIAL_CROSS_SET;

        /* Check if we can extend left.
         * Note: Don't use last_anchor_col restriction here.
         * Actual move generation resets last_anchor_col = BOARD_DIM for each anchor,
         * so shadow must also allow full left extension for accurate bounds. */
        if (gen->shadow_left_col == 0) {
            return;
        }

        /* If rack is exhausted, check if there's a playthrough tile to extend through.
         * We can still form valid words by extending through playthroughs and then
         * playing on the right side (via shadow_play_right called above). */
        if (gen->tiles_played >= gen->shadow_original_rack_total) {
            /* Check if next position left is a playthrough */
            MachineLetter left_letter = gen->row_letters[gen->shadow_left_col - 1];
            if (left_letter == EMPTY_SQUARE) {
                return;  /* No playthrough, can't extend */
            }
            /* Extend through playthrough tiles, accumulating their scores */
            while (gen->shadow_left_col > 0) {
                gen->shadow_left_col--;
                MachineLetter ml = gen->row_letters[gen->shadow_left_col];
                if (ml == EMPTY_SQUARE) {
                    gen->shadow_left_col++;  /* Restore - went too far */
                    break;
                }
                gen->shadow_mainword_restricted_score += get_tile_score(ml);
            }
            /* Record the extended play - playthroughs contribute to main word */
            maybe_recalc_effective_multipliers(gen);
            shadow_record(gen);
            return;
        }

        /* First check if next position left is a playthrough tile.
         * If so, we extend through it without consuming a rack tile.
         * This is analogous to what shadow_play_right does. */
        MachineLetter left_ml = gen->row_letters[gen->shadow_left_col - 1];
        if (left_ml != EMPTY_SQUARE) {
            /* Playthrough tile - add its score and continue */
            gen->shadow_left_col--;
            gen->shadow_mainword_restricted_score += get_tile_score(left_ml);
            /* Continue loop to try extending further left */
            continue;
        }

        /* Empty square to the left - need to place a tile from rack */

        /* If rack has blank, blank can play as any letter */
        uint32_t possible_left = has_blank
            ? gen->left_ext_set
            : (gen->left_ext_set & gen->rack_bits);
        if (possible_left == 0) {
            return;
        }
        gen->left_ext_set = TRIVIAL_CROSS_SET;

        gen->shadow_left_col--;
        gen->tiles_played++;

        uint8_t bonus = gen->row_bonuses[gen->shadow_left_col];
        uint8_t letter_mult = get_letter_mult(bonus);
        uint8_t word_mult = get_word_mult(bonus);

        gen->shadow_word_multiplier *= word_mult;

        CrossSet cross_set = gen->row_cross_sets[gen->shadow_left_col];
        possible_left &= cross_set;

        if (!try_restrict_tile(gen, possible_left, letter_mult, word_mult,
                               gen->shadow_left_col)) {
            insert_unrestricted_multipliers(gen, gen->shadow_left_col);
        }

        shadow_record(gen);
    }
}

/*
 * Shadow play leftward for playthrough anchor
 */
static void playthrough_shadow_play_left(MoveGenState *gen, int is_unique) {
    int has_blank = gen->rack.counts[BLANK_TILE] > 0;

    for (;;) {
        /* Try extending right first */
        /* If rack has blank, blank can play as any letter */
        uint32_t possible_right = has_blank
            ? gen->right_ext_set
            : (gen->right_ext_set & gen->rack_bits);
        if (possible_right != 0) {
            shadow_play_right(gen, is_unique);
        }
        gen->right_ext_set = TRIVIAL_CROSS_SET;

        /* If rack has blank, blank can play as any letter */
        uint32_t possible_left = has_blank
            ? gen->left_ext_set
            : (gen->left_ext_set & gen->rack_bits);
        gen->left_ext_set = TRIVIAL_CROSS_SET;

        /* Check bounds.
         * Note: Don't use last_anchor_col restriction for same reason as above. */
        if (gen->shadow_left_col == 0) {
            break;
        }

        /* If rack is exhausted, extend through any remaining playthrough tiles */
        if (gen->tiles_played >= gen->shadow_original_rack_total) {
            MachineLetter left_letter = gen->row_letters[gen->shadow_left_col - 1];
            if (left_letter == EMPTY_SQUARE) {
                break;  /* No more playthroughs */
            }
            /* Extend through remaining playthrough tiles */
            while (gen->shadow_left_col > 0) {
                gen->shadow_left_col--;
                MachineLetter ml = gen->row_letters[gen->shadow_left_col];
                if (ml == EMPTY_SQUARE) {
                    gen->shadow_left_col++;
                    break;
                }
                gen->shadow_mainword_restricted_score += get_tile_score(ml);
            }
            maybe_recalc_effective_multipliers(gen);
            shadow_record(gen);
            break;
        }

        /* First check if next position left is a playthrough tile.
         * If so, we extend through it without consuming a rack tile.
         * This is analogous to what shadow_play_right does. */
        MachineLetter left_ml = gen->row_letters[gen->shadow_left_col - 1];
        if (left_ml != EMPTY_SQUARE) {
            /* Playthrough tile - add its score and continue */
            gen->shadow_left_col--;
            gen->shadow_mainword_restricted_score += get_tile_score(left_ml);
            /* Continue loop to try extending further left */
            continue;
        }

        /* Empty square to the left - need to place a tile from rack */

        if (possible_left == 0) {
            break;
        }

        gen->shadow_left_col--;
        gen->tiles_played++;

        CrossSet cross_set = gen->row_cross_sets[gen->shadow_left_col];
        possible_left &= cross_set;

        if (possible_left == 0) {
            break;
        }

        uint8_t bonus = gen->row_bonuses[gen->shadow_left_col];
        uint8_t letter_mult = get_letter_mult(bonus);
        uint8_t word_mult = get_word_mult(bonus);

        /* Add perpendicular score */
        if (gen->row_cross_scores[gen->shadow_left_col] >= 0) {
            gen->shadow_perpendicular_additional_score +=
                gen->row_cross_scores[gen->shadow_left_col] * word_mult;
        }

        gen->shadow_word_multiplier *= word_mult;

        if (!try_restrict_tile(gen, possible_left, letter_mult, word_mult,
                               gen->shadow_left_col)) {
            insert_unrestricted_multipliers(gen, gen->shadow_left_col);
        }

        if (cross_set == TRIVIAL_CROSS_SET) {
            is_unique = 1;
        }

        /* Record if valid play.
         * For shadow, always record - duplicate recordings are harmless
         * (they just compare the same equity and update if better).
         * The is_unique check is for actual move recording, not shadow bounds. */
        if (gen->tiles_played >= 1) {
            maybe_recalc_effective_multipliers(gen);
            shadow_record(gen);
        }
    }
}

/*
 * Start shadow play for a non-playthrough anchor (empty square)
 */
static void shadow_start_nonplaythrough(MoveGenState *gen) {
    CrossSet cross_set = gen->row_cross_sets[gen->shadow_left_col];
    /* If rack has blank, blank can play as any letter in cross_set */
    uint32_t possible = (gen->rack.counts[BLANK_TILE] > 0)
        ? cross_set
        : (cross_set & gen->rack_bits);

    if (possible == 0) {
#if USE_SHADOW_DEBUG
        /* Check if there's a playthrough tile to the left */
        int has_left_playthrough = (gen->shadow_left_col > 0 &&
            gen->row_letters[gen->shadow_left_col - 1] != EMPTY_SQUARE);
        if (has_left_playthrough) {
            /* Convert to physical coordinates for comparison with BAD_CUTOFF */
            int phys_row = (gen->dir == DIR_HORIZONTAL)
                ? gen->current_row : gen->shadow_left_col;
            int phys_col = (gen->dir == DIR_HORIZONTAL)
                ? gen->shadow_left_col : gen->current_row;
            printf("T%d SHADOW_EARLY: phys(%d,%d,%c) cross_set=0x%x rack_bits=0x%x\n",
                   shadow_debug_turn, phys_row, phys_col, gen->dir ? 'V' : 'H',
                   cross_set, gen->rack_bits);
        }
#endif
        return;
    }

    uint8_t bonus = gen->row_bonuses[gen->shadow_left_col];
    uint8_t letter_mult = get_letter_mult(bonus);
    uint8_t word_mult = get_word_mult(bonus);

    /* Add perpendicular score */
    if (gen->row_cross_scores[gen->shadow_left_col] >= 0) {
        gen->shadow_perpendicular_additional_score =
            gen->row_cross_scores[gen->shadow_left_col] * word_mult;
    }

    /* For single-tile plays, word multiplier applies to the single tile.
     * Set it before try_restrict_tile so shadow_record computes correctly. */
    gen->shadow_word_multiplier = word_mult;
    if (!try_restrict_tile(gen, possible, letter_mult, word_mult,
                           gen->shadow_left_col)) {
        insert_unrestricted_multipliers(gen, gen->shadow_left_col);
    }
    gen->tiles_played++;

    /* Before recording, scan right for adjacent playthrough tiles.
     * These tiles are part of the word formed by placing the single tile.
     * Without this, we'd miss the playthroughs and record too low a bound. */
    int scan_col = gen->shadow_left_col + 1;
    while (scan_col < BOARD_DIM) {
        MachineLetter ml = gen->row_letters[scan_col];
        if (ml == EMPTY_SQUARE) break;
        gen->shadow_mainword_restricted_score += get_tile_score(ml);
        scan_col++;
    }

    /* Record single-tile play (including adjacent playthroughs).
     * For shadow, always record regardless of direction - duplicate recordings
     * are harmless. The direction-based dedup is for actual move recording. */
    shadow_record(gen);
    maybe_recalc_effective_multipliers(gen);

    nonplaythrough_shadow_play_left(gen, gen->dir == DIR_HORIZONTAL);
}

/*
 * Start shadow play for a playthrough anchor (existing tile)
 */
static void shadow_start_playthrough(MoveGenState *gen, MachineLetter current_letter) {
    /* Traverse all playthrough tiles */
    for (;;) {
        gen->shadow_mainword_restricted_score += get_tile_score(current_letter);

        /* Note: Don't use last_anchor_col restriction for same reason as above. */
        if (gen->shadow_left_col == 0) {
            break;
        }

        gen->shadow_left_col--;
        current_letter = gen->row_letters[gen->shadow_left_col];

        if (current_letter == EMPTY_SQUARE) {
            gen->shadow_left_col++;
            break;
        }
    }

    playthrough_shadow_play_left(gen, gen->dir == DIR_HORIZONTAL);
}

/*
 * Shadow play for a single anchor
 */
static void shadow_play_for_anchor(MoveGenState *gen, int col) {
    /* Initialize shadow state for this anchor */
    gen->shadow_left_col = col;
    gen->shadow_right_col = col;
    gen->tiles_played = 0;
    gen->shadow_original_rack_total = gen->rack.total;  /* Save before any tiles consumed */

#if USE_SHADOW_DEBUG
    int entry_rack_total = gen->rack.total;
    int entry_counts_sum = 0;
    for (int ml = 0; ml < ALPHABET_SIZE; ml++) {
        entry_counts_sum += gen->rack.counts[ml];
    }
    /* Print for specific turns to reduce output */
    if (shadow_debug_turn == 425 || shadow_debug_turn == 783) {
        int phys_row = (gen->dir == DIR_HORIZONTAL) ? gen->current_row : col;
        int phys_col = (gen->dir == DIR_HORIZONTAL) ? col : gen->current_row;
        uint32_t entry_rack_bits = build_rack_cross_set(&gen->rack);
        printf("T%d ANCHOR_ENTRY: phys(%d,%d,%c) total=%d counts=%d rack_bits=0x%x\n",
               shadow_debug_turn, phys_row, phys_col, gen->dir ? 'V' : 'H',
               entry_rack_total, entry_counts_sum, entry_rack_bits);
        /* Print row_letters for anchor(2,2,H) in T783 */
        if (shadow_debug_turn == 783 && phys_row == 2 && phys_col == 2 && gen->dir == DIR_HORIZONTAL) {
            printf("T783   row_letters[0-14]: ");
            for (int c = 0; c < BOARD_DIM; c++) {
                printf("%d ", gen->row_letters[c]);
            }
            printf("\n");
            printf("T783   cross_sets[0-14]: ");
            for (int c = 0; c < BOARD_DIM; c++) {
                printf("0x%x ", gen->row_cross_sets[c]);
            }
            printf("\n");
        }
    }
#endif

    gen->shadow_mainword_restricted_score = 0;
    gen->shadow_perpendicular_additional_score = 0;
    gen->shadow_word_multiplier = 1;
    gen->num_unrestricted_multipliers = 0;
    gen->last_word_multiplier = 1;

    /* Zero effective multipliers - unexplored positions contribute 0 to score */
    memset(gen->descending_effective_letter_multipliers, 0,
           sizeof(gen->descending_effective_letter_multipliers));

    /* Initialize to 0, not EQUITY_INITIAL_VALUE.
     * Original magpie does this - if shadow_record is never called
     * (e.g., vertical anchor with no valid extensions), we want
     * a reasonable lower bound (0) not a sentinel that triggers cutoff. */
    gen->highest_shadow_equity = 0;
    gen->highest_shadow_score = 0;

    /* Reset extension sets to trivial */
    gen->left_ext_set = TRIVIAL_CROSS_SET;
    gen->right_ext_set = TRIVIAL_CROSS_SET;

    /* Build rack cross set */
    gen->rack_bits = build_rack_cross_set(&gen->rack);

#if USE_SHADOW_DEBUG
    if (gen->rack.total != entry_rack_total) {
        int phys_row = (gen->dir == DIR_HORIZONTAL) ? gen->current_row : col;
        int phys_col = (gen->dir == DIR_HORIZONTAL) ? col : gen->current_row;
        printf("T%d RACK_CHANGED_BEFORE_START: phys(%d,%d,%c) entry=%d now=%d rack_bits=0x%x\n",
               shadow_debug_turn, phys_row, phys_col, gen->dir ? 'V' : 'H',
               entry_rack_total, gen->rack.total, gen->rack_bits);
    }
#endif

    MachineLetter current_letter = gen->row_letters[col];
    if (current_letter == EMPTY_SQUARE) {
        shadow_start_nonplaythrough(gen);
    } else {
        shadow_start_playthrough(gen, current_letter);
    }

#if USE_SHADOW_DEBUG
    /* Debug: print when bound is 0 for non-trivial cases */
    if (gen->highest_shadow_equity == 0) {
        int phys_row = (gen->dir == DIR_HORIZONTAL) ? gen->current_row : col;
        int phys_col = (gen->dir == DIR_HORIZONTAL) ? col : gen->current_row;
        CrossSet cs = gen->row_cross_sets[col];
        int has_blank = gen->rack.counts[BLANK_TILE] > 0;
        int has_left = (col > 0 && gen->row_letters[col-1] != EMPTY_SQUARE);
        printf("T%d SHADOW_BOUND_ZERO: phys(%d,%d,%c) letter=%d cross_set=0x%x "
               "rack_bits=0x%x has_blank=%d has_left=%d rack_total=%d\n",
               shadow_debug_turn, phys_row, phys_col, gen->dir ? 'V' : 'H',
               current_letter, cs, gen->rack_bits, has_blank, has_left, gen->rack.total);
    }
#endif
}

/*
 * Run shadow algorithm for entire board
 * Builds anchor heap sorted by highest_possible_equity
 */
static void gen_shadow(MoveGenState *gen) {
    anchor_heap_init(&gen->anchor_heap);

#if USE_SHADOW_DEBUG
    int initial_rack_total = gen->rack.total;
    int counts_sum = 0;
    for (int ml = 0; ml < ALPHABET_SIZE; ml++) {
        counts_sum += gen->rack.counts[ml];
    }
    printf("T%d GEN_SHADOW_START: rack_total=%d counts_sum=%d\n",
           shadow_debug_turn, initial_rack_total, counts_sum);
    if (counts_sum != initial_rack_total) {
        printf("T%d RACK_MISMATCH: total=%d but counts sum to %d\n",
               shadow_debug_turn, initial_rack_total, counts_sum);
    }
#endif

    /* Build descending tile scores once per turn */
    build_descending_tile_scores(gen);

    /* Check if board is empty (center square has no tile) */
    /* On empty board, only search horizontal (vertical is symmetric) */
    int center_idx = (BOARD_DIM / 2) * BOARD_DIM + (BOARD_DIM / 2);
    int board_is_empty = (gen->board->squares[center_idx].letter == EMPTY_SQUARE);
    int max_dir = board_is_empty ? 1 : 2;

    /* Process each row in both directions (or just horizontal if empty) */
    for (int dir = 0; dir < max_dir; dir++) {
        for (int row = 0; row < BOARD_DIM; row++) {
            cache_row(gen, row, dir);

            gen->last_anchor_col = BOARD_DIM;  /* Sentinel */

            for (int col = 0; col < BOARD_DIM; col++) {
                /* Only process anchors (empty squares adjacent to tiles).
                 * Shadow handles playthrough tiles by extending from adjacent anchors,
                 * same as actual move generation. */
                if (!is_anchor(gen, col)) continue;

                /* Save state that shadow_play_for_anchor may modify */
                Rack saved_rack;
                memcpy(&saved_rack, &gen->rack, sizeof(Rack));
                int8_t saved_scores[RACK_SIZE];
                memcpy(saved_scores, gen->descending_tile_scores, sizeof(saved_scores));

#if USE_SHADOW_DEBUG
                /* Check rack state before processing anchor */
                int pre_counts_sum = 0;
                for (int ml = 0; ml < ALPHABET_SIZE; ml++) {
                    pre_counts_sum += gen->rack.counts[ml];
                }
                uint32_t pre_rack_bits = build_rack_cross_set(&gen->rack);
                if (pre_counts_sum != initial_rack_total || gen->rack.total != initial_rack_total) {
                    int phys_row = (dir == DIR_HORIZONTAL) ? row : col;
                    int phys_col = (dir == DIR_HORIZONTAL) ? col : row;
                    printf("T%d RACK_CORRUPTION_BEFORE: phys(%d,%d,%c) total=%d counts=%d expected=%d rack_bits=0x%x\n",
                           shadow_debug_turn, phys_row, phys_col, dir ? 'V' : 'H',
                           gen->rack.total, pre_counts_sum, initial_rack_total, pre_rack_bits);
                }
#endif

                shadow_play_for_anchor(gen, col);

                /* Restore state */
                memcpy(&gen->rack, &saved_rack, sizeof(Rack));
                memcpy(gen->descending_tile_scores, saved_scores, sizeof(saved_scores));

#if USE_SHADOW_DEBUG
                if (gen->rack.total != initial_rack_total) {
                    printf("T%d RACK_CORRUPTION_AFTER: anchor(%d,%d,%c) rack_total=%d expected=%d\n",
                           shadow_debug_turn, row, col, dir ? 'V' : 'H',
                           gen->rack.total, initial_rack_total);
                }
#endif

                /* Add ALL anchors to heap (even with low equity).
                 * The shadow algorithm may underestimate equity for some positions,
                 * so we must not skip any anchors based on shadow equity.
                 * Store current last_anchor_col so move gen uses same left boundary. */
                Anchor anchor;
                anchor.row = (dir == DIR_HORIZONTAL) ? row : col;
                anchor.col = (dir == DIR_HORIZONTAL) ? col : row;
                anchor.dir = dir;
                anchor.last_anchor_col = gen->last_anchor_col;  /* Store for move gen */
                anchor.highest_possible_equity = gen->highest_shadow_equity;
                anchor.highest_possible_score = gen->highest_shadow_score;
                /* scan_order matches original non-shadow processing order:
                 * Horizontal: row * 15 + col (0-224)
                 * Vertical: 225 + col * 15 + row (225-449) */
                anchor.scan_order = (dir == DIR_HORIZONTAL)
                    ? (row * BOARD_DIM + col)
                    : (225 + col * BOARD_DIM + row);

                anchor_heap_insert(&gen->anchor_heap, &anchor);

                /* Update for next anchor: stop left extension before current anchor */
                gen->last_anchor_col = col;
            }
        }
    }

    /* Build the heap (heapify) */
    anchor_heap_build(&gen->anchor_heap);
}

/* ===== End Shadow Algorithm ===== */

/*
 * Compare new move vs current best using Magpie's tiebreaking rules:
 * 1. equity (higher is better)
 * 2. score (higher is better)
 * 3. row (lower is better)
 * 4. col (lower is better)
 * 5. dir (horizontal=0 wins over vertical=1)
 * 6. tiles_played (lower is better)
 * 7. tiles_length (lower is better)
 * 8. tiles (lexicographic - lower is better)
 * Returns 1 if new move is better, 0 otherwise
 */
static int is_better_move(Equity new_equity, int16_t new_score,
                          uint8_t new_row, uint8_t new_col, uint8_t new_dir,
                          uint8_t new_tiles_played, uint8_t new_tiles_length,
                          const MachineLetter *new_tiles,
                          const MoveGenState *gen) {
    Move *best = gen->best_move;

    /* First move always wins */
    if (gen->best_equity == EQUITY_INITIAL_VALUE) return 1;

    /* Compare equity */
    if (new_equity != gen->best_equity) {
        return new_equity > gen->best_equity;
    }

    /* Equal equity: compare score */
    if (new_score != best->score) {
        return new_score > best->score;
    }

    /* Equal score: compare row (lower is better) */
    if (new_row != best->row) {
        return new_row < best->row;
    }

    /* Equal row: compare col (lower is better) */
    if (new_col != best->col) {
        return new_col < best->col;
    }

    /* Equal position: prefer horizontal (dir=0) over vertical (dir=1) */
    if (new_dir != best->dir) {
        return best->dir;  /* returns 1 if best is vertical (new horizontal wins) */
    }

    /* Equal dir: compare tiles_played (lower is better) */
    if (new_tiles_played != best->tiles_played) {
        return new_tiles_played < best->tiles_played;
    }

    /* Equal tiles_played: compare tiles_length (lower is better) */
    if (new_tiles_length != best->tiles_length) {
        return new_tiles_length < best->tiles_length;
    }

    /* Equal length: lexicographic comparison of tiles (lower is better) */
    for (int i = 0; i < new_tiles_length; i++) {
        if (new_tiles[i] != best->tiles[i]) {
            return new_tiles[i] < best->tiles[i];
        }
    }

    /* Completely equal - don't replace */
    return 0;
}

/* Record a valid move - only keeps best by equity with tiebreaking */
static void record_move(MoveGenState *gen, int leftstrip, int rightstrip) {
    gen->move_count++;

    /* Calculate final score */
    int16_t score = gen->main_word_score * gen->word_multiplier + gen->cross_score;

    /* Add bingo bonus for using all 7 tiles */
    if (gen->tiles_played == RACK_SIZE) {
        score += 50;
    }

    /* Calculate equity = score*8 + leave_value (both in eighths of a point) */
    Equity equity = (Equity)(score * 8);

    /* Add leave value if KLV is available */
    if (gen->klv != NULL) {
        equity += leave_map_get_current(&gen->leave_map);
    }

    /* Compute row/col for comparison */
    uint8_t new_row, new_col;
    if (gen->dir == DIR_HORIZONTAL) {
        new_row = gen->current_row;
        new_col = leftstrip;
    } else {
        new_row = leftstrip;
        new_col = gen->current_row;
    }

    /* Build tiles array for tiebreaking comparison */
    uint8_t new_tiles_length = rightstrip - leftstrip + 1;
    MachineLetter new_tiles[BOARD_DIM];
    for (int i = leftstrip; i <= rightstrip; i++) {
        new_tiles[i - leftstrip] = gen->strip[i];
    }

    /* Only record if better than current best (with tiebreaking) */
    if (!is_better_move(equity, score, new_row, new_col, gen->dir,
                        gen->tiles_played, new_tiles_length, new_tiles, gen)) return;

    Move *move = gen->best_move;
    gen->best_equity = equity;

    move->row = new_row;
    move->col = new_col;
    move->dir = gen->dir;
    move->tiles_played = gen->tiles_played;
    move->tiles_length = rightstrip - leftstrip + 1;
    move->score = score;
    move->equity = equity;

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

                        /* Update leave map */
                        if (gen->klv != NULL) {
                            leave_map_take_letter(&gen->leave_map, tile,
                                                  gen->rack.counts[tile]);
                        }

                        go_on(gen, col, tile, next_index, accepts,
                              leftstrip, rightstrip);

                        /* Restore leave map */
                        if (gen->klv != NULL) {
                            leave_map_add_letter(&gen->leave_map, tile,
                                                 gen->rack.counts[tile]);
                        }

                        gen->tiles_played--;
                        gen->rack.total++;
                        gen->rack.counts[tile]++;
                    }

                    /* Try with blank */
                    if (has_blank) {
                        gen->rack.counts[BLANK_TILE]--;
                        gen->rack.total--;
                        gen->tiles_played++;

                        /* Update leave map */
                        if (gen->klv != NULL) {
                            leave_map_take_letter(&gen->leave_map, BLANK_TILE,
                                                  gen->rack.counts[BLANK_TILE]);
                        }

                        go_on(gen, col, BLANKED(tile), next_index, accepts,
                              leftstrip, rightstrip);

                        /* Restore leave map */
                        if (gen->klv != NULL) {
                            leave_map_add_letter(&gen->leave_map, BLANK_TILE,
                                                 gen->rack.counts[BLANK_TILE]);
                        }

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

#if !USE_SHADOW
/*
 * Generate moves for a single row/column (non-shadow mode)
 */
static void gen_for_row(MoveGenState *gen) {
    gen->last_anchor_col = BOARD_DIM;  /* Sentinel: no previous anchor */

    for (int col = 0; col < BOARD_DIM; col++) {
        if (!is_anchor(gen, col)) continue;

        gen->anchor_col = col;
        gen->tiles_played = 0;
        gen->main_word_score = 0;
        gen->cross_score = 0;
        gen->word_multiplier = 1;

        gen->left_ext_set = TRIVIAL_CROSS_SET;
        gen->right_ext_set = TRIVIAL_CROSS_SET;

        uint32_t root = kwg_get_gaddag_root(gen->kwg);
        recursive_gen(gen, col, root, col, col);

        gen->last_anchor_col = col;
    }
}
#endif

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

        /* Compute anchor status: empty square adjacent to a tile.
         * Note: Non-empty squares are NOT marked as anchors here because the
         * recursive algorithm handles playthroughs by extending from adjacent anchors.
         * The shadow algorithm has separate logic to handle playthrough anchors. */
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

/*
 * Generate all exchange moves and find best by leave value
 * Exchange moves trade tiles for new ones from the bag.
 * We generate all 2^n - 1 possible exchanges and pick the best leave.
 */
static void generate_exchange_moves(MoveGenState *gen, const Bag *bag,
                                     Move *best_exchange, Equity *best_exchange_equity) {
    if (bag == NULL || bag->count < RACK_SIZE) {
        /* Can only exchange when bag has >= 7 tiles */
        return;
    }

    if (gen->klv == NULL) {
        /* Need KLV for leave-based exchange selection */
        return;
    }

    /* Try all non-empty subsets of tiles to exchange */
    /* We iterate through possible exchanges using the leave_map indexing */
    Rack temp_rack;
    memcpy(&temp_rack, &gen->rack, sizeof(Rack));

    *best_exchange_equity = EQUITY_INITIAL_VALUE;  /* Start with very low equity */

    /* Use bitmask to enumerate all subsets of the rack */
    uint8_t rack_size = gen->rack.total;
    uint16_t max_mask = (1 << rack_size) - 1;

    for (uint16_t mask = 1; mask <= max_mask; mask++) {
        /* mask represents tiles to EXCHANGE (remove from rack) */
        /* So the leave is the complement */
        memcpy(&temp_rack, &gen->rack, sizeof(Rack));

        uint8_t exchange_count = 0;
        MachineLetter exchange_tiles[RACK_SIZE];

        /* Iterate through rack positions */
        uint8_t bit_pos = 0;
        for (MachineLetter ml = 0; ml < ALPHABET_SIZE && bit_pos < rack_size; ml++) {
            for (uint8_t c = 0; c < gen->rack.counts[ml]; c++) {
                if (mask & (1 << bit_pos)) {
                    /* This tile is exchanged */
                    temp_rack.counts[ml]--;
                    temp_rack.total--;
                    exchange_tiles[exchange_count++] = ml;
                }
                bit_pos++;
            }
        }

        /* Get leave value for remaining tiles */
        Equity leave = klv_get_leave_value(gen->klv, &temp_rack);

        /* Exchange has 0 points, so equity is just the leave value */
        if (leave > *best_exchange_equity) {
            *best_exchange_equity = leave;

            /* Record this exchange */
            best_exchange->row = 0;
            best_exchange->col = 0;
            best_exchange->dir = 0xFF;  /* Special marker for exchange */
            best_exchange->tiles_played = exchange_count;
            best_exchange->tiles_length = exchange_count;
            best_exchange->score = 0;
            for (int i = 0; i < exchange_count; i++) {
                best_exchange->tiles[i] = exchange_tiles[i];
            }
        }
    }
}

/*
 * Process a single anchor for move generation
 */
static void gen_for_anchor(MoveGenState *gen, int anchor_col) {
    gen->anchor_col = anchor_col;
    gen->tiles_played = 0;
    gen->main_word_score = 0;
    gen->cross_score = 0;
    gen->word_multiplier = 1;

    /* Set extension sets (simplified - allow all) */
    gen->left_ext_set = TRIVIAL_CROSS_SET;
    gen->right_ext_set = TRIVIAL_CROSS_SET;

#if USE_SHADOW_DEBUG
    Equity before_equity = gen->best_equity;
    CrossSet anchor_cross_set = gen->row_cross_sets[anchor_col];
    MachineLetter anchor_letter = gen->row_letters[anchor_col];
#endif

    /* Get GADDAG root */
    uint32_t root = kwg_get_gaddag_root(gen->kwg);

    /* Start recursive generation from anchor going left */
    recursive_gen(gen, anchor_col, root, anchor_col, anchor_col);

#if USE_SHADOW_DEBUG
    if (gen->best_equity > before_equity && before_equity > EQUITY_INITIAL_VALUE) {
        /* Print when move gen improves best_equity */
        int phys_row = (gen->dir == DIR_HORIZONTAL) ? gen->current_row : anchor_col;
        int phys_col = (gen->dir == DIR_HORIZONTAL) ? anchor_col : gen->current_row;
        printf("T%d GEN_BETTER: phys(%d,%d,%c) cross_set=0x%x before=%d after=%d\n",
               shadow_debug_turn, phys_row, phys_col, gen->dir ? 'V' : 'H',
               anchor_cross_set, before_equity, gen->best_equity);
    }
#endif
}

void generate_moves(const Board *board, const Rack *rack, const uint32_t *kwg,
                    const KLV *klv, const Bag *bag, MoveList *moves) {
    MoveGenState gen;
    memset(&gen, 0, sizeof(gen));

#if USE_SHADOW_DEBUG
    shadow_debug_turn++;
#endif

    gen.board = board;
    gen.kwg = kwg;
    gen.klv = klv;
    gen.tiles_in_bag = bag ? bag->count : 0;

    /* Use first slot in moves array as best_move storage */
    gen.best_move = &moves->moves[0];
    gen.best_equity = EQUITY_INITIAL_VALUE;  /* Start with minimum equity */
    gen.move_count = 0;

    /* Reset recursion counter */
    recursion_counter = 0;

    /* Copy rack (we modify it during generation) */
    memcpy(&gen.rack, rack, sizeof(Rack));

    /* Initialize leave map if KLV is available */
    if (klv != NULL) {
        leave_map_init(&gen.leave_map, klv, rack);
        /* Copy best_leaves from LeaveMap to MoveGenState for shadow algorithm.
         * LeaveMap uses index N for N-tile leave, MoveGenState uses N-1. */
        for (int i = 1; i <= RACK_SIZE; i++) {
            gen.best_leaves[i - 1] = gen.leave_map.best_leaves[i];
        }
    } else {
        /* No KLV - initialize best_leaves to 0 */
        for (int i = 0; i < RACK_SIZE; i++) {
            gen.best_leaves[i] = 0;
        }
    }

#if USE_SHADOW
    /* Run shadow algorithm to build anchor heap with upper bounds */
    gen_shadow(&gen);

    /* Restore rack after shadow (gen_shadow modifies it) */
    memcpy(&gen.rack, rack, sizeof(Rack));

    /*
     * Process anchors from heap in best-first order
     * This allows early cutoff when remaining anchors can't beat current best
     */
    int8_t cached_row = -1;
    int8_t cached_dir = -1;

    while (!anchor_heap_is_empty(&gen.anchor_heap)) {
        Anchor anchor;
        anchor_heap_extract_max(&gen.anchor_heap, &anchor);

        /* Early cutoff: if best anchor's upper bound can't beat current best, stop */
#if 0  /* DISABLED - use debug mode below */
        if (gen.best_equity > EQUITY_INITIAL_VALUE &&
            anchor.highest_possible_equity < gen.best_equity) {
            break;
        }
#endif
#if USE_SHADOW_DEBUG
        Equity equity_before = gen.best_equity;
#endif

        /* Cache row if different from current */
        int row = (anchor.dir == DIR_HORIZONTAL) ? anchor.row : anchor.col;
        if (row != cached_row || anchor.dir != cached_dir) {
            cache_row(&gen, row, anchor.dir);
            cached_row = row;
            cached_dir = anchor.dir;
        }
        /* Restore last_anchor_col from anchor to use same left boundary as non-shadow.
         * This prevents duplicate moves and ensures consistent results. */
        gen.last_anchor_col = anchor.last_anchor_col;

        /* Show progress */
        show_progress(row, anchor.dir);

        /* Reinitialize leave map for this anchor */
        if (klv != NULL) {
            leave_map_init(&gen.leave_map, klv, rack);
        }

        /* Restore rack for this anchor */
        memcpy(&gen.rack, rack, sizeof(Rack));

        /* Generate moves for this anchor */
        int anchor_col = (anchor.dir == DIR_HORIZONTAL) ? anchor.col : anchor.row;
        gen_for_anchor(&gen, anchor_col);

#if USE_SHADOW_DEBUG
        /* If cutoff would have fired but this anchor found better move */
        if (equity_before > EQUITY_INITIAL_VALUE &&
            anchor.highest_possible_equity < equity_before &&
            gen.best_equity > equity_before) {
            /* BAD CUTOFF: would have skipped anchor that found better move */
            shadow_debug_bad_cutoff_count++;
            shadow_debug_last_bad_cutoff = (BadCutoffInfo){
                .row = anchor.row,
                .col = anchor.col,
                .dir = anchor.dir,
                .bound = anchor.highest_possible_equity,
                .best_before = equity_before,
                .best_after = gen.best_equity
            };
            /* Print detailed info including best_leaves and actual leave */
            Equity actual_leave = leave_map_get_current(&gen.leave_map);
            printf("T%d BAD_CUTOFF: anchor(%d,%d,%c) bound=%d before=%d after=%d "
                   "actual_leave=%d best_leaves=[%d,%d,%d,%d,%d,%d,%d] bag=%d\n",
                   shadow_debug_turn,
                   anchor.row, anchor.col, anchor.dir ? 'V' : 'H',
                   anchor.highest_possible_equity, equity_before, gen.best_equity,
                   actual_leave,
                   gen.best_leaves[0], gen.best_leaves[1], gen.best_leaves[2],
                   gen.best_leaves[3], gen.best_leaves[4], gen.best_leaves[5],
                   gen.best_leaves[6], gen.tiles_in_bag);
        }
#endif
    }
#else
    /* Non-shadow mode: process rows in order (for validation) */
    /* Check if board is empty - skip vertical if so (symmetric) */
    int center_idx = (BOARD_DIM / 2) * BOARD_DIM + (BOARD_DIM / 2);
    int board_is_empty = (board->squares[center_idx].letter == EMPTY_SQUARE);

    /* Generate horizontal moves */
    for (int row = 0; row < BOARD_DIM; row++) {
        show_progress(row, 0);
        cache_row(&gen, row, DIR_HORIZONTAL);
        gen_for_row(&gen);
    }

    /* Generate vertical moves (skip if board empty) */
    if (!board_is_empty) {
        for (int col = 0; col < BOARD_DIM; col++) {
            show_progress(col, 1);
            cache_row(&gen, col, DIR_VERTICAL);
            gen_for_row(&gen);
        }
    }
#endif

    /* Generate exchange moves if no good play found or bag allows */
    Move best_exchange;
    Equity best_exchange_equity = EQUITY_INITIAL_VALUE;
    generate_exchange_moves(&gen, bag, &best_exchange, &best_exchange_equity);

    /* Compare best play vs best exchange */
    if (best_exchange_equity > gen.best_equity && gen.move_count > 0) {
        /* Exchange is better than best play - unusual but possible */
        memcpy(&moves->moves[0], &best_exchange, sizeof(Move));
        gen.best_equity = best_exchange_equity;
    } else if (gen.move_count == 0 && best_exchange_equity > EQUITY_INITIAL_VALUE) {
        /* No plays found, use exchange */
        memcpy(&moves->moves[0], &best_exchange, sizeof(Move));
        gen.best_equity = best_exchange_equity;
    }

    /* Set count: 0 if no moves found, 1 if best move found */
    moves->count = (gen.best_equity > EQUITY_INITIAL_VALUE) ? 1 : 0;
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
