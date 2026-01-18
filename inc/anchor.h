/*
 * Anchor and AnchorHeap for shadow algorithm
 * Genesis-optimized version
 *
 * The shadow algorithm computes the highest possible equity for each anchor,
 * then processes anchors in best-first order using a max-heap.
 * This enables early cutoff when remaining anchors can't beat current best.
 */

#ifndef ANCHOR_H
#define ANCHOR_H

#include <stdint.h>
#include "klv.h"  /* For Equity type */

/*
 * Anchor: represents a position where a play can start
 * 10-byte struct for Genesis (vs 24+ bytes on 64-bit)
 *
 * scan_order preserves original discovery order for tiebreaking:
 * - Horizontal: row * 15 + col (0-224)
 * - Vertical: 225 + col * 15 + row (225-449)
 */
typedef struct {
    int8_t row;                  /* Row (0-14) or col for vertical */
    int8_t col;                  /* Column within row */
    uint8_t dir;                 /* DIR_HORIZONTAL or DIR_VERTICAL */
    uint8_t pad;                 /* Alignment padding */
    Equity highest_possible_equity;  /* Upper bound on equity from this anchor */
    int16_t highest_possible_score;  /* Upper bound on score (for tiebreaking) */
    uint16_t scan_order;         /* Original scan order for tiebreaking */
} Anchor;

/*
 * AnchorHeap: max-heap of anchors sorted by highest_possible_equity
 * Used to process anchors in best-first order
 *
 * Maximum anchors = 15 rows * 15 anchors/row * 2 directions = 450
 * But in practice, most boards have far fewer anchors.
 * We limit to 200 which handles all but extreme cases.
 */
#define MAX_ANCHORS 200

typedef struct {
    Anchor anchors[MAX_ANCHORS];
    uint16_t count;
} AnchorHeap;

/* Initialize empty heap */
static inline void anchor_heap_init(AnchorHeap *heap) {
    heap->count = 0;
}

/* Get parent/child indices for heap operations */
#define HEAP_PARENT(i) (((i) - 1) >> 1)
#define HEAP_LEFT(i)   (((i) << 1) + 1)
#define HEAP_RIGHT(i)  (((i) << 1) + 2)

/*
 * Compare two anchors for heap ordering
 * Returns 1 if a should come before b (a is "greater")
 * Primary key: highest_possible_equity (higher is better)
 * Secondary key: scan_order (lower is better - matches original processing order)
 */
static inline int anchor_compare(const Anchor *a, const Anchor *b) {
    if (a->highest_possible_equity != b->highest_possible_equity) {
        return a->highest_possible_equity > b->highest_possible_equity;
    }
    /* Equal equity: prefer lower scan_order (earlier in original scan) */
    return a->scan_order < b->scan_order;
}

/*
 * Heapify down from index i
 * Maintains max-heap property after removal
 */
static inline void anchor_heap_heapify_down(AnchorHeap *heap, uint16_t i) {
    uint16_t count = heap->count;

    while (1) {
        uint16_t largest = i;
        uint16_t left = HEAP_LEFT(i);
        uint16_t right = HEAP_RIGHT(i);

        if (left < count &&
            anchor_compare(&heap->anchors[left], &heap->anchors[largest])) {
            largest = left;
        }

        if (right < count &&
            anchor_compare(&heap->anchors[right], &heap->anchors[largest])) {
            largest = right;
        }

        if (largest == i) break;

        /* Swap */
        Anchor tmp = heap->anchors[i];
        heap->anchors[i] = heap->anchors[largest];
        heap->anchors[largest] = tmp;

        i = largest;
    }
}

/*
 * Heapify up from index i
 * Used after insertion
 */
static inline void anchor_heap_heapify_up(AnchorHeap *heap, uint16_t i) {
    while (i > 0) {
        uint16_t parent = HEAP_PARENT(i);

        /* If parent is >= current (using our comparison), stop */
        if (!anchor_compare(&heap->anchors[i], &heap->anchors[parent])) {
            break;
        }

        /* Swap */
        Anchor tmp = heap->anchors[i];
        heap->anchors[i] = heap->anchors[parent];
        heap->anchors[parent] = tmp;

        i = parent;
    }
}

/*
 * Insert anchor into heap
 * Returns 1 on success, 0 if heap is full
 */
static inline int anchor_heap_insert(AnchorHeap *heap, const Anchor *anchor) {
    if (heap->count >= MAX_ANCHORS) return 0;

    uint16_t i = heap->count++;
    heap->anchors[i] = *anchor;
    anchor_heap_heapify_up(heap, i);
    return 1;
}

/*
 * Extract maximum (best) anchor from heap
 * Returns 1 on success with anchor copied to out, 0 if heap is empty
 */
static inline int anchor_heap_extract_max(AnchorHeap *heap, Anchor *out) {
    if (heap->count == 0) return 0;

    *out = heap->anchors[0];
    heap->count--;

    if (heap->count > 0) {
        heap->anchors[0] = heap->anchors[heap->count];
        anchor_heap_heapify_down(heap, 0);
    }

    return 1;
}

/*
 * Peek at maximum anchor without removing
 * Returns NULL if heap is empty
 */
static inline const Anchor *anchor_heap_peek(const AnchorHeap *heap) {
    if (heap->count == 0) return (const Anchor *)0;
    return &heap->anchors[0];
}

/*
 * Check if heap is empty
 */
static inline int anchor_heap_is_empty(const AnchorHeap *heap) {
    return heap->count == 0;
}

/*
 * Build heap from unsorted array (heapify)
 * More efficient than repeated insertions when adding many anchors at once
 */
static inline void anchor_heap_build(AnchorHeap *heap) {
    if (heap->count <= 1) return;

    /* Start from last non-leaf and heapify down */
    for (int i = HEAP_PARENT(heap->count - 1); i >= 0; i--) {
        anchor_heap_heapify_down(heap, (uint16_t)i);
    }
}

#endif /* ANCHOR_H */
