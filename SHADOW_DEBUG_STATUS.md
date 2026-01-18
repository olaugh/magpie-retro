# Shadow Algorithm Debug Status

## Current State

**All bad cutoffs resolved: 35 → 0**

The shadow algorithm now correctly computes upper bounds on equity for each anchor position, enabling early cutoff when remaining anchors can't beat the current best move.

## Fixes Applied

### 1. Single-tile word multiplier bug
In `shadow_start_nonplaythrough`, the word multiplier was set to 0 before `shadow_record` was called, causing single-tile plays to score 0. Fixed by setting `shadow_word_multiplier = word_mult` before the record call.

### 2. Adjacent playthroughs in shadow_start_nonplaythrough (RIGHT only - partial fix)
When placing a single tile at an anchor, adjacent tiles to the right (playthroughs) were not being included in the main word score. Added a scan-right loop before `shadow_record` to accumulate playthrough tile scores.

### 3. Leftward playthrough detection in extend-left functions
Both `nonplaythrough_shadow_play_left` and `playthrough_shadow_play_left` had a bug where they treated occupied squares as empty squares needing tile placement. When the cross_set at an occupied position was 0 (correctly indicating no tile can be placed), the functions would exit early instead of recognizing the position as a playthrough.

Fixed by adding a check at the start of each left-extension iteration:
```c
MachineLetter left_ml = gen->row_letters[gen->shadow_left_col - 1];
if (left_ml != EMPTY_SQUARE) {
    /* Playthrough tile - add its score and continue */
    gen->shadow_left_col--;
    gen->shadow_mainword_restricted_score += get_tile_score(left_ml);
    continue;
}
```

### 4. Adjacent playthroughs in shadow_start_nonplaythrough (BOTH directions - complete fix)
The partial fix (#2) only scanned right, but playthroughs can also be on the left of the anchor. For example, with board layout `O_A` (O at left, empty anchor in middle, A at right), placing a tile forms word `O-tile-A`. The main word score must include both O and A.

Fixed by scanning both left AND right before recording the single-tile play:
```c
/* Scan left for playthroughs */
int scan_col = gen->shadow_left_col - 1;
while (scan_col >= 0) {
    MachineLetter ml = gen->row_letters[scan_col];
    if (ml == EMPTY_SQUARE) break;
    gen->shadow_mainword_restricted_score += get_tile_score(ml);
    scan_col--;
}

/* Scan right for playthroughs */
scan_col = gen->shadow_left_col + 1;
while (scan_col < BOARD_DIM) {
    MachineLetter ml = gen->row_letters[scan_col];
    if (ml == EMPTY_SQUARE) break;
    gen->shadow_mainword_restricted_score += get_tile_score(ml);
    scan_col++;
}
```

## Testing

Run the shadow debug build to verify no bad cutoffs:
```bash
make batch-shadow-debug
./build/batch-nwl23-shadow-debug/test_batch 1 60 2>&1 | grep BAD_CUTOFF | wc -l
# Should output: 0
```

## Summary

The shadow algorithm issues were all related to not properly accounting for playthrough tiles (existing tiles on the board that become part of a word when new tiles are placed adjacent to them):

1. Single-tile plays weren't scanning for adjacent playthroughs at all initially
2. The scan was added for right-side only, missing left-side playthroughs
3. The extend-left functions weren't recognizing existing tiles as playthroughs

All issues are now fixed and the shadow algorithm correctly computes upper bounds.

## Extension Set Integration (Complete)

Extension sets (leftx/rightx) enable tighter filtering during move generation by restricting which letters can extend a word in each direction. This matches the algorithm used in the original magpie.

### Implementation Details

1. **Extension set fields in Square structure** (`inc/scrabble.h`):
   - `leftx_h`, `rightx_h` - horizontal direction extension sets
   - `leftx_v`, `rightx_v` - vertical direction extension sets

2. **Extension set computation** (`src/kwg.c`):
   - `compute_extension_sets()` computes leftx and rightx using GADDAG traversal
   - **leftx (front hooks)**: Letters that can START a word - traverse reversed suffix in GADDAG, get letters DIRECTLY at node (NOT after separator)
   - **rightx (back hooks)**: Letters that can CONTINUE a word - traverse reversed prefix in GADDAG, follow separator, get letters AFTER separator
   - **Key insight from magpie**: leftx and rightx use DIFFERENT algorithms - leftx gets letters at node, rightx follows separator first

3. **Board cross-set computation** (`src/board.c`):
   - `board_update_cross_sets()` computes extension sets alongside cross sets

4. **Row cache** (`src/movegen.c`):
   - `row_leftx[]` and `row_rightx[]` arrays cache extension sets for current row
   - `cache_row()` copies extension sets from board squares

5. **Shadow algorithm integration** (`src/movegen.c`):
   - `shadow_play_for_anchor()` initializes `left_ext_set` and `right_ext_set` from cached row data
   - `shadow_play_right()` applies `right_ext_set` filter at first extension position
   - `nonplaythrough_shadow_play_left()` and `playthrough_shadow_play_left()` apply `left_ext_set` filter

### Performance Notes

With shadow cutoff and extension set filtering enabled:
- **Cutoff rate**: ~42.3% of anchors cut off
- **Correctness**: Verified across 1000 random games with zero bad cutoffs
- **Speed**: Shadow is now ~15-20% **faster** than noshadow

Timing comparison (300 games, NWL23):
- noshadow: ~2.47s
- shadow: ~2.09s (15% faster)

### Key Optimization: Remove Per-Anchor leave_map_init

The initial implementation called `leave_map_init()` for each anchor in the shadow path, which was a massive overhead (33% of total time). The leave map is based on the original rack and doesn't change between anchors, so this was unnecessary.

Fix: Only call `leave_map_init()` once at the start of `generate_moves()`, and reset `leave_map.current_index = 0` when restoring the rack for each anchor.

Before fix: shadow was ~25% slower than noshadow
After fix: shadow is ~15% faster than noshadow
