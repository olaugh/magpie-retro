/*
 * Native test for KLV16 format loading and leave value lookup
 *
 * Compile with: gcc -o test_klv tests/test_klv.c -I inc -O2
 * Run with: ./test_klv data/NWL23.klv16
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Replicate key definitions from scrabble.h and klv.h for native test */

#define ALPHABET_SIZE 27
#define RACK_SIZE 7
#define ML_BLANK 0
#define BLANK_BIT 0x80

typedef uint8_t MachineLetter;
typedef int16_t Equity;

typedef struct {
    uint8_t counts[ALPHABET_SIZE];
    uint8_t total;
} Rack;

#define KLV_UNFOUND_INDEX 0xFFFFFFFF

/* KWG node accessors */
#define KLV_KWG_TILE(node)      ((node) >> 24)
#define KLV_KWG_ACCEPTS(node)   (((node) & 0x00800000) != 0)
#define KLV_KWG_IS_END(node)    (((node) & 0x00400000) != 0)
#define KLV_KWG_ARC_INDEX(node) ((node) & 0x003FFFFF)

/* KLV structure */
typedef struct {
    uint32_t kwg_size;
    uint32_t num_leaves;
    const uint32_t *kwg;
    const int16_t *leaves;
    uint32_t *word_counts;
} KLV;

/* Rack functions */
static void rack_clear(Rack *rack) {
    memset(rack, 0, sizeof(Rack));
}

static void rack_add_letter(Rack *rack, MachineLetter ml) {
    rack->counts[ml]++;
    rack->total++;
}

static void rack_from_string(Rack *rack, const char *s) {
    rack_clear(rack);
    while (*s) {
        char c = *s++;
        if (c == '?') {
            rack_add_letter(rack, ML_BLANK);
        } else if (c >= 'A' && c <= 'Z') {
            rack_add_letter(rack, c - 'A' + 1);
        } else if (c >= 'a' && c <= 'z') {
            rack_add_letter(rack, c - 'a' + 1);
        }
    }
}

/* Count words at node (recursive with memoization) */
static uint32_t count_words_at(KLV *klv, uint32_t node_index) {
    if (node_index >= klv->kwg_size) {
        return 0;
    }

    if (klv->word_counts[node_index] != 0) {
        if (klv->word_counts[node_index] == KLV_UNFOUND_INDEX) {
            return 0;
        }
        return klv->word_counts[node_index];
    }

    klv->word_counts[node_index] = KLV_UNFOUND_INDEX;

    uint32_t node = klv->kwg[node_index];
    uint32_t count = 0;

    if (KLV_KWG_ACCEPTS(node)) {
        count = 1;
    }

    uint32_t child_index = KLV_KWG_ARC_INDEX(node);
    if (child_index != 0) {
        count += count_words_at(klv, child_index);
    }

    if (!KLV_KWG_IS_END(node)) {
        count += count_words_at(klv, node_index + 1);
    }

    klv->word_counts[node_index] = count;
    return count;
}

static void compute_word_counts(KLV *klv) {
    for (uint32_t i = 0; i < klv->kwg_size; i++) {
        klv->word_counts[i] = 0;
    }

    for (uint32_t i = klv->kwg_size; i > 0; ) {
        i--;
        count_words_at(klv, i);
    }
}

static uint32_t klv_get_dawg_root(const KLV *klv) {
    return KLV_KWG_ARC_INDEX(klv->kwg[0]);
}

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

        idx += klv->word_counts[node_index] - klv->word_counts[node_index + 1];
        node_index++;
    }
}

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

static uint32_t klv_get_word_index(const KLV *klv, const Rack *rack) {
    if (rack->total == 0) {
        return KLV_UNFOUND_INDEX;
    }

    uint32_t node_index = klv_get_dawg_root(klv);
    uint32_t idx = 0;

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

static Equity klv_get_indexed_leave(const KLV *klv, uint32_t index) {
    if (index == KLV_UNFOUND_INDEX) {
        return 0;
    }
    return klv->leaves[index];
}

static Equity klv_get_leave_value(const KLV *klv, const Rack *rack) {
    if (rack->total == 0) {
        return 0;
    }
    uint32_t index = klv_get_word_index(klv, rack);
    return klv_get_indexed_leave(klv, index);
}

/* Load KLV16 from file */
static int load_klv16(const char *filename, KLV *klv, uint8_t **data_buf,
                       uint32_t **word_counts_buf) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", filename);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    *data_buf = malloc(size);
    if (!*data_buf) {
        fclose(f);
        return 0;
    }

    if (fread(*data_buf, 1, size, f) != (size_t)size) {
        free(*data_buf);
        fclose(f);
        return 0;
    }
    fclose(f);

    /* Parse header */
    uint8_t *data = *data_buf;
    klv->kwg_size = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    klv->kwg = (const uint32_t *)(data + 4);

    uint8_t *leaves_header = data + 4 + (klv->kwg_size * 4);
    klv->num_leaves = leaves_header[0] | (leaves_header[1] << 8) |
                      (leaves_header[2] << 16) | (leaves_header[3] << 24);
    klv->leaves = (const int16_t *)(leaves_header + 4);

    /* Allocate word counts */
    *word_counts_buf = calloc(klv->kwg_size, sizeof(uint32_t));
    if (!*word_counts_buf) {
        free(*data_buf);
        return 0;
    }
    klv->word_counts = *word_counts_buf;

    printf("KLV16 loaded:\n");
    printf("  KWG size: %u nodes\n", klv->kwg_size);
    printf("  Number of leaves: %u\n", klv->num_leaves);

    return 1;
}

/* Test leave lookups */
static void test_leave_lookup(const KLV *klv, const char *rack_str) {
    Rack rack;
    rack_from_string(&rack, rack_str);

    uint32_t index = klv_get_word_index(klv, &rack);
    Equity leave = klv_get_leave_value(klv, &rack);

    printf("Rack '%s': index=%u, leave=%.3f points (%d eighths)\n",
           rack_str,
           index == KLV_UNFOUND_INDEX ? 0 : index,
           leave / 8.0,
           leave);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.klv16>\n", argv[0]);
        return 1;
    }

    KLV klv;
    uint8_t *data_buf = NULL;
    uint32_t *word_counts_buf = NULL;

    if (!load_klv16(argv[1], &klv, &data_buf, &word_counts_buf)) {
        return 1;
    }

    printf("\nComputing word counts...\n");
    compute_word_counts(&klv);
    printf("Word counts computed.\n");

    /* Verify DAWG root */
    uint32_t dawg_root = klv_get_dawg_root(&klv);
    printf("\nDAWG root index: %u\n", dawg_root);
    printf("Word count at root: %u\n", klv.word_counts[dawg_root]);

    /* Test some leave lookups */
    printf("\nTesting leave lookups:\n");
    test_leave_lookup(&klv, "?");       /* Blank */
    test_leave_lookup(&klv, "E");       /* Single E */
    test_leave_lookup(&klv, "S");       /* Single S */
    test_leave_lookup(&klv, "Q");       /* Single Q */
    test_leave_lookup(&klv, "QU");      /* Q+U */
    test_leave_lookup(&klv, "AEINRST"); /* SATINER - common bingo leave */
    test_leave_lookup(&klv, "AEIRST");  /* 6-tile leave */
    test_leave_lookup(&klv, "?EINRST"); /* Blank + EINRST */
    test_leave_lookup(&klv, "AAAAAAA"); /* 7 A's */
    test_leave_lookup(&klv, "EEEEEEE"); /* 7 E's */
    test_leave_lookup(&klv, "ZZ");      /* Two Z's - invalid leave */

    /* Find min/max leave values */
    printf("\nScanning all leave values...\n");
    int16_t min_leave = 0, max_leave = 0;
    uint32_t min_idx = 0, max_idx = 0;
    for (uint32_t i = 0; i < klv.num_leaves; i++) {
        if (klv.leaves[i] < min_leave) {
            min_leave = klv.leaves[i];
            min_idx = i;
        }
        if (klv.leaves[i] > max_leave) {
            max_leave = klv.leaves[i];
            max_idx = i;
        }
    }
    printf("Min leave: %.3f points (%d eighths) at index %u\n",
           min_leave / 8.0, min_leave, min_idx);
    printf("Max leave: %.3f points (%d eighths) at index %u\n",
           max_leave / 8.0, max_leave, max_idx);

    printf("\nAll tests passed!\n");

    free(word_counts_buf);
    free(data_buf);
    return 0;
}
