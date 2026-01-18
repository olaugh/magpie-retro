/*
 * KLV (DAWG + Leave Values) implementation for Sega Genesis
 *
 * Loads .klv16 format and provides leave value lookup.
 */

#include "klv.h"
#include "scrabble.h"

/* From libc.c */
extern void *memset(void *s, int c, unsigned long n);
extern void *memcpy(void *dest, const void *src, unsigned long n);

/* KWG node accessors (same as kwg.h) */
#define KLV_KWG_TILE(node)      ((node) >> 24)
#define KLV_KWG_ACCEPTS(node)   (((node) & 0x00800000) != 0)
#define KLV_KWG_IS_END(node)    (((node) & 0x00400000) != 0)
#define KLV_KWG_ARC_INDEX(node) ((node) & 0x003FFFFF)

/*
 * Build word_counts array for entire DAWG
 * Fully iterative to avoid stack overflow on 68000
 *
 * In a DAWG, some nodes may have children at higher indices due to
 * node sharing. We use multiple passes until values stabilize.
 * Maximum depth is RACK_SIZE-1 = 6, so at most 6 passes needed.
 */
static void compute_word_counts(KLV *klv) {
    uint32_t kwg_size = klv->kwg_size;
    int changed;

    /* Zero initialize */
    memset(klv->word_counts, 0, kwg_size * sizeof(uint32_t));

    /* Iterate until no changes (max RACK_SIZE-1 passes) */
    do {
        changed = 0;

        /* Process nodes in reverse order */
        for (uint32_t i = kwg_size; i > 0; ) {
            i--;
            uint32_t node = klv->kwg[i];
            uint32_t count = 0;

            /* This node accepts? */
            if (KLV_KWG_ACCEPTS(node)) {
                count = 1;
            }

            /* Add children count */
            uint32_t child_index = KLV_KWG_ARC_INDEX(node);
            if (child_index != 0 && child_index < kwg_size) {
                count += klv->word_counts[child_index];
            }

            /* Add siblings count */
            if (!KLV_KWG_IS_END(node) && i + 1 < kwg_size) {
                count += klv->word_counts[i + 1];
            }

            if (klv->word_counts[i] != count) {
                klv->word_counts[i] = count;
                changed = 1;
            }
        }
    } while (changed);
}

/* Read little-endian uint32 from byte pointer */
static inline uint32_t read_le32(const uint8_t *p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Read little-endian int16 from byte pointer */
static inline int16_t read_le16(const uint8_t *p) {
    return (int16_t)(p[0] | (p[1] << 8));
}

/* Static buffer for converted KWG nodes */
static uint32_t klv_kwg_buf[2500];

/*
 * Initialize KLV from ROM data
 * Converts little-endian ROM data to native big-endian for 68000
 */
void klv_init(KLV *klv, const uint8_t *klv_data, uint32_t *word_counts_buf) {
    const uint8_t *p = klv_data;

    /* Read KWG size (little-endian) */
    klv->kwg_size = read_le32(p);
    p += 4;

    /* Convert KWG nodes from little-endian to native */
    for (uint32_t i = 0; i < klv->kwg_size; i++) {
        klv_kwg_buf[i] = read_le32(p);
        p += 4;
    }
    klv->kwg = klv_kwg_buf;

    /* Read number of leaves (little-endian) */
    klv->num_leaves = read_le32(p);
    p += 4;

    /* Leave values stay as ROM pointer - we'll read them on demand */
    /* Store the byte pointer and read with byte access */
    klv->leaves = (const int16_t *)p;

    /* Set up word counts buffer and compute */
    klv->word_counts = word_counts_buf;
    compute_word_counts(klv);
}

/*
 * Get DAWG root index
 */
static inline uint32_t klv_get_dawg_root(const KLV *klv) {
    return KLV_KWG_ARC_INDEX(klv->kwg[0]);
}

/*
 * Increment to machine letter in sibling list
 * Returns node_index if found, 0 if not found
 * Updates word_index appropriately
 */
static uint32_t increment_to_letter(const KLV *klv, uint32_t node_index,
                                     uint32_t word_index, uint32_t *next_word_index,
                                     MachineLetter ml) {
    if (node_index == 0) {
        *next_word_index = KLV_UNFOUND_INDEX;
        return 0;
    }

    uint32_t idx = word_index;

    for (;;) {
        uint32_t node = klv->kwg[node_index];

        if (KLV_KWG_TILE(node) == ml) {
            *next_word_index = idx;
            return node_index;
        }

        if (KLV_KWG_IS_END(node)) {
            *next_word_index = KLV_UNFOUND_INDEX;
            return 0;
        }

        /* Skip this sibling's subtree count */
        idx += klv->word_counts[node_index] - klv->word_counts[node_index + 1];
        node_index++;
    }
}

/*
 * Follow arc to children
 */
static uint32_t follow_arc(const KLV *klv, uint32_t node_index,
                            uint32_t word_index, uint32_t *next_word_index) {
    if (node_index == 0) {
        *next_word_index = KLV_UNFOUND_INDEX;
        return 0;
    }

    *next_word_index = word_index + 1;
    uint32_t node = klv->kwg[node_index];
    return KLV_KWG_ARC_INDEX(node);
}

/*
 * Get word index for a rack by traversing DAWG
 * Letters are traversed in sorted order (by machine letter value)
 */
uint32_t klv_get_word_index(const KLV *klv, const Rack *rack) {
    if (rack->total == 0) {
        return KLV_UNFOUND_INDEX;
    }

    uint32_t node_index = klv_get_dawg_root(klv);
    uint32_t idx = 0;

    /* Find first letter with count > 0 */
    MachineLetter ml = 0;
    int8_t ml_count = 0;
    for (ml = 0; ml < ALPHABET_SIZE; ml++) {
        if (rack->counts[ml] > 0) {
            ml_count = rack->counts[ml];
            break;
        }
    }

    uint8_t remaining = rack->total;

    while (node_index != 0) {
        uint32_t next_word_index;
        node_index = increment_to_letter(klv, node_index, idx, &next_word_index, ml);

        if (node_index == 0) {
            return KLV_UNFOUND_INDEX;
        }
        idx = next_word_index;

        ml_count--;
        remaining--;

        /* Advance to next letter if done with this one */
        while (ml_count == 0) {
            ml++;
            if (ml >= ALPHABET_SIZE) {
                break;
            }
            ml_count = rack->counts[ml];
        }

        if (remaining == 0) {
            return idx;
        }

        node_index = follow_arc(klv, node_index, idx, &next_word_index);
        idx = next_word_index;
    }

    return KLV_UNFOUND_INDEX;
}

/*
 * Get leave value for a rack
 */
Equity klv_get_leave_value(const KLV *klv, const Rack *rack) {
    if (rack->total == 0) {
        return 0;
    }

    uint32_t index = klv_get_word_index(klv, rack);
    return klv_get_indexed_leave(klv, index);
}

/*
 * Recursive helper to populate leave map
 */
static void populate_leave_values(LeaveMap *lm, const KLV *klv,
                                   Rack *temp_rack, MachineLetter start_ml) {
    /* Store leave value for current subset */
    Equity value = klv_get_leave_value(klv, temp_rack);
    lm->leave_values[lm->current_index] = value;

    /* Track best leave for this size */
    uint8_t leave_size = temp_rack->total;
    if (value > lm->best_leaves[leave_size]) {
        lm->best_leaves[leave_size] = value;
    }

    /* Try adding each remaining letter */
    for (MachineLetter ml = start_ml; ml < ALPHABET_SIZE; ml++) {
        if (temp_rack->counts[ml] > 0) {
            /* Take letter using complement indexing */
            uint8_t count_before = temp_rack->counts[ml];
            temp_rack->counts[ml]--;
            temp_rack->total--;

            uint8_t base = lm->letter_base_index[ml];
            uint8_t offset = temp_rack->counts[ml];
            uint8_t bit_index = base + offset;
            uint8_t reversed_bit = lm->reversed_bit_map[bit_index];
            lm->current_index |= reversed_bit;

            /* Recurse */
            populate_leave_values(lm, klv, temp_rack, ml);

            /* Restore */
            lm->current_index &= ~reversed_bit;
            temp_rack->counts[ml] = count_before;
            temp_rack->total++;
        }
    }
}

/*
 * Initialize LeaveMap for a rack
 */
void leave_map_init(LeaveMap *lm, const KLV *klv, const Rack *rack) {
    /* Build letter_base_index and reversed_bit_map */
    uint8_t current_base = 0;

    for (MachineLetter ml = 0; ml < ALPHABET_SIZE; ml++) {
        uint8_t count = rack->counts[ml];
        if (count > 0) {
            lm->letter_base_index[ml] = current_base;
            for (uint8_t j = 0; j < count; j++) {
                uint8_t bit_index = current_base + count - j - 1;
                lm->reversed_bit_map[current_base + j] = 1 << bit_index;
            }
            current_base += count;
        } else {
            lm->letter_base_index[ml] = 0;
        }
    }

    lm->rack_size = rack->total;
    lm->current_index = (1 << rack->total) - 1;  /* All bits set */

    /* Initialize best_leaves to minimum value */
    for (int i = 0; i <= RACK_SIZE; i++) {
        lm->best_leaves[i] = -32767;
    }

    /* Populate leave values for all subsets */
    Rack temp_rack;
    memcpy(&temp_rack, rack, sizeof(Rack));

    /* Start with index 0 (full rack - no tiles played yet) */
    lm->current_index = 0;
    populate_leave_values(lm, klv, &temp_rack, 0);

    /* Keep at 0 - no tiles played yet at start of move generation */
    /* As tiles are played, bits get SET in current_index */
}
