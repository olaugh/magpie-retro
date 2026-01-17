/*
 * KLV (DAWG + Leave Values) for Sega Genesis
 *
 * KLV16 format (Genesis-specific):
 *   - KWG size (uint32_t LE)
 *   - KWG nodes (uint32_t[] LE) - DAWG for leave indexing
 *   - Number of leaves (uint32_t LE)
 *   - Leave values (int16_t[] LE) - in eighths of a point
 *
 * Equity is stored as int16_t representing eighths of a point.
 * This gives a range of -4095.875 to +4095.875 points with 0.125 precision.
 */

#ifndef KLV_H
#define KLV_H

#include <stdint.h>
#include "scrabble.h"

/* Equity type: 16-bit signed, in eighths of a point */
typedef int16_t Equity;

/* Convert points to equity (eighths) */
#define POINTS_TO_EQUITY(pts) ((Equity)((pts) * 8))
#define EQUITY_TO_POINTS(eq) ((eq) / 8)

/* Sentinel for unfound leave index */
#define KLV_UNFOUND_INDEX 0xFFFFFFFF

/* KLV structure - leave values stored separately from main KWG */
struct KLV {
    uint32_t kwg_size;        /* Number of DAWG nodes */
    uint32_t num_leaves;      /* Number of leave values */
    const uint32_t *kwg;      /* DAWG nodes (ROM pointer) */
    const int16_t *leaves;    /* Leave values in eighths (ROM pointer) */
    uint32_t *word_counts;    /* Computed at load time (RAM) */
};
typedef struct KLV KLV;

/*
 * LeaveMap: O(1) leave value lookup during move generation
 *
 * Uses bitmask indexing: each tile on rack gets a unique bit.
 * As tiles are played, bits are cleared. The current_index
 * directly indexes into pre-computed leave_values array.
 *
 * For a 7-tile rack, there are 128 possible subsets (2^7).
 */
#define LEAVE_MAP_SIZE 128  /* 2^RACK_SIZE */

typedef struct {
    Equity leave_values[LEAVE_MAP_SIZE];    /* Pre-computed leave values */
    uint8_t letter_base_index[ALPHABET_SIZE]; /* Base bit index for each letter */
    uint8_t reversed_bit_map[RACK_SIZE];    /* For complement indexing */
    uint8_t current_index;                   /* Current bitmask index */
    uint8_t rack_size;                       /* Actual number of tiles */
} LeaveMap;

/* KLV functions */

/*
 * Initialize KLV from ROM pointers
 * klv_data points to start of .klv16 data in ROM
 * word_counts_buf must be pre-allocated (kwg_size * 4 bytes)
 */
void klv_init(KLV *klv, const uint8_t *klv_data, uint32_t *word_counts_buf);

/*
 * Get leave value for a rack using DAWG traversal
 * Returns 0 if rack is empty or leave not found
 */
Equity klv_get_leave_value(const KLV *klv, const Rack *rack);

/*
 * Get word index for a rack (for leave lookup)
 */
uint32_t klv_get_word_index(const KLV *klv, const Rack *rack);

/*
 * Get indexed leave value
 * Reads little-endian int16 from ROM
 */
static inline Equity klv_get_indexed_leave(const KLV *klv, uint32_t index) {
    if (index == KLV_UNFOUND_INDEX) {
        return 0;
    }
    /* Read little-endian int16 from byte pointer */
    const uint8_t *p = (const uint8_t *)klv->leaves + index * 2;
    return (int16_t)(p[0] | (p[1] << 8));
}

/* LeaveMap functions */

/*
 * Initialize LeaveMap for a rack
 * Pre-computes leave values for all 128 rack subsets
 */
void leave_map_init(LeaveMap *lm, const KLV *klv, const Rack *rack);

/*
 * Take a letter from rack (during move generation)
 * Updates current_index for O(1) leave lookup
 */
static inline void leave_map_take_letter(LeaveMap *lm, MachineLetter letter,
                                          uint8_t count_after) {
    uint8_t base = lm->letter_base_index[letter];
    uint8_t bit_index = base + count_after;
    lm->current_index &= ~(1 << bit_index);
}

/*
 * Add letter back to rack (backtracking)
 */
static inline void leave_map_add_letter(LeaveMap *lm, MachineLetter letter,
                                         uint8_t count_before) {
    uint8_t base = lm->letter_base_index[letter];
    uint8_t bit_index = base + count_before;
    lm->current_index |= (1 << bit_index);
}

/*
 * Get current leave value in O(1)
 */
static inline Equity leave_map_get_current(const LeaveMap *lm) {
    return lm->leave_values[lm->current_index];
}

/*
 * Get current index (for debugging)
 */
static inline uint8_t leave_map_get_index(const LeaveMap *lm) {
    return lm->current_index;
}

#endif /* KLV_H */
