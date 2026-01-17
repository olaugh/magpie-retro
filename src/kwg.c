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

    /* Calculate cross-score from existing tiles */
    for (int i = 0; i < prefix_len; i++) {
        *cross_score += TILE_SCORES[prefix[i]];
    }
    for (int i = 0; i < suffix_len; i++) {
        *cross_score += TILE_SCORES[suffix[i]];
    }

    /* Start from DAWG root */
    uint32_t node_index = kwg_get_dawg_root(kwg);

    /* Follow prefix */
    for (int i = 0; i < prefix_len; i++) {
        node_index = kwg_follow_arc(kwg, node_index, prefix[i]);
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
                    for (uint32_t k = suf_node; ; k++) {
                        uint32_t n = kwg_get_node(kwg, k);
                        if (KWG_TILE(n) == suffix[j]) {
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
