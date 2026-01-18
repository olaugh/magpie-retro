/*
 * KWG (GADDAG/DAWG) lexicon functions
 */

#include "scrabble.h"
#include "kwg.h"

/*
 * Check if a sequence of letters forms a valid word using DAWG
 */
int kwg_is_valid_word(const uint32_t *kwg, const MachineLetter *letters, int count) {
    if (count < 2) return 0;  /* Minimum word length is 2 */

    uint32_t node_index = kwg_get_dawg_root(kwg);

    for (int i = 0; i < count; i++) {
        MachineLetter letter = UNBLANKED(letters[i]);

        /* Search for matching arc */
        int found = 0;
        for (uint32_t j = node_index; ; j++) {
            uint32_t node = kwg_get_node(kwg, j);
            MachineLetter tile = KWG_TILE(node);

            if (tile == letter) {
                if (i == count - 1) {
                    /* Last letter - check accepts */
                    return KWG_ACCEPTS(node);
                }
                node_index = KWG_ARC_INDEX(node);
                if (node_index == 0) return 0;
                found = 1;
                break;
            }

            if (KWG_IS_END(node)) break;
        }

        if (!found) return 0;
    }

    return 0;
}

/*
 * Compute cross-set for a position given prefix and suffix letters
 *
 * The cross-set is the set of all letters L such that:
 *   prefix + L + suffix forms a valid word
 *
 * Also computes the cross-score (sum of tile values for prefix + suffix)
 */
/*
 * Compute extension sets for a position given left and right tiles
 * in the MAIN word direction.
 *
 * leftx = "front hooks" - letters that can go BEFORE right_tiles to form valid word starts
 * rightx = "back hooks" - letters that can go AFTER left_tiles to continue/complete words
 *
 * These enable pruning: if leftx & rack == 0, no tiles can extend leftward through right_tiles.
 */
void compute_extension_sets(const uint32_t *kwg,
                            const MachineLetter *left_tiles, int left_len,
                            const MachineLetter *right_tiles, int right_len,
                            CrossSet *leftx, CrossSet *rightx) {
    *leftx = TRIVIAL_CROSS_SET;
    *rightx = TRIVIAL_CROSS_SET;

    /* Compute rightx (back hooks) - letters that can follow left_tiles.
     * Traverse DAWG through left_tiles, then get letter set at that node.
     * These are letters L such that left_tiles + L is a valid word or prefix. */
    if (left_len > 0) {
        uint32_t node_index = kwg_get_dawg_root(kwg);
        int valid = 1;
        for (int i = 0; i < left_len && valid; i++) {
            node_index = kwg_follow_arc(kwg, node_index, UNBLANKED(left_tiles[i]));
            if (node_index == 0) valid = 0;
        }
        if (valid && node_index != 0) {
            /* Get all letters that have arcs from this node (back extensions) */
            uint32_t ext_set;
            kwg_get_letter_sets(kwg, node_index, &ext_set);
            *rightx = ext_set;
        } else {
            *rightx = 0;  /* No valid back extensions */
        }
    }

    /* Compute leftx (front hooks) - letters that can precede right_tiles.
     * Use GADDAG: traverse reversed right_tiles, then follow separator,
     * then get letter set. These are letters L such that L + right_tiles
     * starts a valid word.
     *
     * GADDAG path: for tiles [A,B,C], we look up CBA^ then get letters after ^.
     */
    if (right_len > 0) {
        uint32_t node_index = kwg_get_gaddag_root(kwg);
        int valid = 1;

        /* Traverse reversed right_tiles through GADDAG */
        for (int i = right_len - 1; i >= 0 && valid; i--) {
            node_index = kwg_follow_arc(kwg, node_index, UNBLANKED(right_tiles[i]));
            if (node_index == 0) valid = 0;
        }

        /* Follow separator to get front hooks */
        if (valid && node_index != 0) {
            uint32_t sep_index = kwg_follow_arc(kwg, node_index, ML_SEPARATOR);
            if (sep_index != 0) {
                uint32_t ext_set;
                kwg_get_letter_sets(kwg, sep_index, &ext_set);
                *leftx = ext_set;
            } else {
                *leftx = 0;  /* No front extensions through separator */
            }
        } else {
            *leftx = 0;
        }
    }
}

CrossSet compute_cross_set(const uint32_t *kwg,
                           const MachineLetter *prefix, int prefix_len,
                           const MachineLetter *suffix, int suffix_len,
                           int8_t *cross_score) {
    CrossSet result = 0;
    *cross_score = 0;

    /* If no prefix and no suffix, all letters are valid */
    if (prefix_len == 0 && suffix_len == 0) {
        *cross_score = -1;  /* No cross word */
        return TRIVIAL_CROSS_SET;
    }

    /* Calculate cross-score from existing tiles.
     * Blanks (marked with BLANK_BIT) score 0, regular tiles use TILE_SCORES. */
    for (int i = 0; i < prefix_len; i++) {
        if (!IS_BLANKED(prefix[i])) {
            *cross_score += TILE_SCORES[UNBLANKED(prefix[i])];
        }
        /* else: blank tiles score 0, nothing to add */
    }
    for (int i = 0; i < suffix_len; i++) {
        if (!IS_BLANKED(suffix[i])) {
            *cross_score += TILE_SCORES[UNBLANKED(suffix[i])];
        }
    }

    /* Start from DAWG root */
    uint32_t node_index = kwg_get_dawg_root(kwg);

    /* Follow prefix (use UNBLANKED for dictionary lookup) */
    for (int i = 0; i < prefix_len; i++) {
        node_index = kwg_follow_arc(kwg, node_index, UNBLANKED(prefix[i]));
        if (node_index == 0) {
            /* Prefix not in dictionary - no valid letters */
            return 0;
        }
    }

    /* At this point, node_index points to children after prefix.
       For each letter L that has an arc here, check if following
       the suffix from that arc leads to a valid word. */

    for (uint32_t i = node_index; ; i++) {
        uint32_t node = kwg_get_node(kwg, i);
        MachineLetter letter = KWG_TILE(node);

        if (letter != 0) {  /* Skip separator */
            /* Check if prefix + letter + suffix is valid */
            uint32_t next = KWG_ARC_INDEX(node);

            if (suffix_len == 0) {
                /* No suffix - just check if this letter accepts */
                if (KWG_ACCEPTS(node)) {
                    result |= (1U << letter);
                }
            } else if (next != 0) {
                /* Follow suffix from here */
                uint32_t suf_node = next;
                int valid = 1;

                for (int j = 0; j < suffix_len && valid; j++) {
                    int found = 0;
                    MachineLetter suf_letter = UNBLANKED(suffix[j]);  /* Use unblanked for lookup */
                    for (uint32_t k = suf_node; ; k++) {
                        uint32_t n = kwg_get_node(kwg, k);
                        if (KWG_TILE(n) == suf_letter) {
                            if (j == suffix_len - 1) {
                                /* Last suffix letter - check accepts */
                                if (KWG_ACCEPTS(n)) {
                                    result |= (1U << letter);
                                }
                            } else {
                                suf_node = KWG_ARC_INDEX(n);
                                if (suf_node == 0) valid = 0;
                            }
                            found = 1;
                            break;
                        }
                        if (KWG_IS_END(n)) break;
                    }
                    if (!found) valid = 0;
                }
            }
        }

        if (KWG_IS_END(node)) break;
    }

    return result;
}
