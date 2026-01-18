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
