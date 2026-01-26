/*
 * KWG (GADDAG) lexicon format for Sega Genesis
 *
 * The KWG is stored in ROM. Each node is a 32-bit value:
 *   Bits 31-24: Tile (8 bits, letter value 0-26)
 *   Bit 23: Accepts flag (this path forms a valid word)
 *   Bit 22: Is-End flag (last sibling in arc list)
 *   Bits 21-0: Arc index (pointer to children, 22 bits)
 *
 * Node 0's arc_index points to DAWG root (for cross-set computation)
 * Node 1's arc_index points to GADDAG root (for move generation)
 *
 * GADDAG encodes words bidirectionally with separator tile (0).
 * For word "CAT": stores C^AT, AC^T, TAC^ where ^ is separator
 * This allows efficient generation starting from any letter position.
 */

#ifndef KWG_H
#define KWG_H

#include <stdint.h>
#include "scrabble.h"
#include "bit_tables.h"

/* KWG node bit field definitions */
#define KWG_TILE_SHIFT     24
#define KWG_ACCEPTS_FLAG   0x00800000
#define KWG_IS_END_FLAG    0x00400000
#define KWG_ARC_INDEX_MASK 0x003FFFFF

/* Node accessor macros */
#define KWG_TILE(node)      ((node) >> KWG_TILE_SHIFT)
#define KWG_ACCEPTS(node)   (((node) & KWG_ACCEPTS_FLAG) != 0)
#define KWG_IS_END(node)    (((node) & KWG_IS_END_FLAG) != 0)
#define KWG_ARC_INDEX(node) ((node) & KWG_ARC_INDEX_MASK)

/*
 * For large lexicons (NWL23 is ~4.7MB), we use ROM bank switching.
 * The SSF2 mapper provides 8 x 512KB banks mapped to addresses:
 * 0x000000-0x07FFFF: Bank 0 (fixed, contains code)
 * 0x080000-0x0FFFFF: Bank 1
 * 0x100000-0x17FFFF: Bank 2
 * ...
 * 0x380000-0x3FFFFF: Bank 7
 *
 * KWG data starts at bank 1 (0x080000) to keep code in bank 0.
 */

/* Bank switching registers (SSF2 mapper) */
#define SSF2_BANK_REG_BASE 0xA130F3

/* KWG ROM base address (after code, in bank area) */
#define KWG_ROM_BASE ((const uint32_t *)0x080000)

/* Size of each bank */
#define BANK_SIZE 0x80000  /* 512 KB */

/* Current bank tracking (for bank switching if needed) */
extern uint8_t current_kwg_bank;

/* Get a KWG node, handling bank switching if necessary */
static inline uint32_t kwg_get_node(const uint32_t *kwg, uint32_t index) {
    /* For indices within the first ~1M nodes (4MB), no bank switch needed
       with a linear mapping. For Genesis, we assume the ROM is mapped. */
    return kwg[index];
}

/* Get DAWG root index (for cross-set computation) */
static inline uint32_t kwg_get_dawg_root(const uint32_t *kwg) {
    return KWG_ARC_INDEX(kwg[0]);
}

/* Get GADDAG root index (for move generation) */
static inline uint32_t kwg_get_gaddag_root(const uint32_t *kwg) {
    return KWG_ARC_INDEX(kwg[1]);
}

/*
 * Find child node with given letter.
 * Returns arc_index of matching node, or 0 if not found.
 */
static inline uint32_t kwg_follow_arc(const uint32_t *kwg, uint32_t node_index,
                                       MachineLetter letter) {
    uint32_t i = node_index;
    for (;;) {
        uint32_t node = kwg_get_node(kwg, i);
        if (KWG_TILE(node) == letter) {
            return KWG_ARC_INDEX(node);
        }
        if (KWG_IS_END(node)) {
            return 0;
        }
        i++;
    }
}

/*
 * Check if arc with given letter accepts (forms a word).
 */
static inline int kwg_letter_accepts(const uint32_t *kwg, uint32_t node_index,
                                      MachineLetter letter) {
    uint32_t i = node_index;
    for (;;) {
        uint32_t node = kwg_get_node(kwg, i);
        if (KWG_TILE(node) == letter) {
            return KWG_ACCEPTS(node);
        }
        if (KWG_IS_END(node)) {
            return 0;
        }
        i++;
    }
}

/*
 * Get letter set at node (bitmap of valid letters).
 * Also returns extension set in *ext_set (letters with children).
 */
static inline uint32_t kwg_get_letter_sets(const uint32_t *kwg, uint32_t node_index,
                                            uint32_t *ext_set) {
    uint32_t letter_set = 0;
    uint32_t extension_set = 0;

    for (uint32_t i = node_index; ; i++) {
        uint32_t node = kwg_get_node(kwg, i);
        MachineLetter tile = KWG_TILE(node);

        if (tile != 0) {  /* Skip separator */
            uint32_t bit = BIT_MASK[tile];
            extension_set |= bit;
            if (KWG_ACCEPTS(node)) {
                letter_set |= bit;
            }
        }

        if (KWG_IS_END(node)) {
            break;
        }
    }

    *ext_set = extension_set;
    return letter_set;
}

/*
 * Traverse DAWG to check if a sequence of letters forms a valid word.
 * letters[] should be in order, count is the length.
 * Returns 1 if valid word, 0 otherwise.
 */
int kwg_is_valid_word(const uint32_t *kwg, const MachineLetter *letters, int count);

/*
 * Compute cross-set for a position given prefix/suffix letters.
 * This uses the DAWG to find all letters that form valid cross-words.
 */
CrossSet compute_cross_set(const uint32_t *kwg,
                           const MachineLetter *prefix, int prefix_len,
                           const MachineLetter *suffix, int suffix_len,
                           int16_t *cross_score);

/*
 * Compute extension sets for a position given left and right tiles
 * in the MAIN word direction (not the cross direction).
 *
 * leftx = "front hooks" - letters that can go BEFORE right_tiles
 * rightx = "back hooks" - letters that can go AFTER left_tiles
 */
void compute_extension_sets(const uint32_t *kwg,
                            const MachineLetter *left_tiles, int left_len,
                            const MachineLetter *right_tiles, int right_len,
                            CrossSet *leftx, CrossSet *rightx);

#endif /* KWG_H */
