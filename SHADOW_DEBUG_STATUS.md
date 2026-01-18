# Shadow Algorithm Debug Status

## Current State

The shadow algorithm computes upper bounds on equity for each anchor position, enabling early cutoff when remaining anchors can't beat the current best move. This document tracks the debugging progress.

**Bad cutoffs reduced: 35 → 2**

## Fixes Applied

### 1. Single-tile word multiplier bug
In `shadow_start_nonplaythrough`, the word multiplier was set to 0 before `shadow_record` was called, causing single-tile plays to score 0. Fixed by setting `shadow_word_multiplier = word_mult` before the record call.

### 2. Adjacent rightward playthroughs in shadow_start_nonplaythrough
When placing a single tile at an anchor, adjacent tiles to the right (playthroughs) were not being included in the main word score. Added a scan-right loop before `shadow_record` to accumulate playthrough tile scores.

### 3. Leftward playthrough detection
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

## Remaining Bad Cutoffs (2)

### T618: anchor(13,9,V)
- bound=178, before=182, after=183
- actual_leave=103
- best_leaves=[68,90,103,103,-32767,-32767,-32767]
- bag=0 (endgame)
- Difference: 5 equity points (183-178)

### T976: anchor(14,1,V)
- bound=224, before=232, after=240
- actual_leave=195
- best_leaves=[68,90,135,176,195,-32767,-32767]
- bag=0 (endgame)
- Difference: 16 equity points (240-224)

Both are **vertical anchors** at edge positions (row 13/14).

## Hypotheses for Remaining Issues

### 1. Vertical-specific playthrough handling
The fixes applied affect horizontal processing. Vertical anchors use the same functions but with transposed coordinates. There may be an issue with how row_letters is set up for vertical processing, or the playthrough detection may have edge cases specific to vertical moves.

### 2. Edge position handling
Both anchors are near board edges (row 13 and 14). There could be boundary condition issues when extending playthroughs near the bottom of the board.

### 3. Leave value calculation
The differences are small (5 and 16 equity points). This could indicate an issue with leave value lookup for specific tile counts, or the best_leaves array not being properly updated for certain move configurations.

## Suggested Next Steps

1. **Add debug output for T618 and T976** - Change `shadow_debug_turn` to 618 or 976 and trace through the vertical anchor processing to see where the bound falls short.

2. **Compare row_letters setup** - Verify that when processing vertical anchors, the transposed row_letters contains the correct tile positions.

3. **Check playthrough detection for vertical** - Ensure the leftward/rightward playthrough fixes work correctly when dir=DIR_VERTICAL (coordinates are transposed).

4. **Verify leave calculation** - The small differences might indicate leave value issues rather than score calculation issues. Check if `get_best_leave_for_tiles_remaining` returns correct values for these cases.

## Testing

Run the shadow debug build:
```bash
make batch-shadow-debug
./build/batch-nwl23-shadow-debug/test_batch 1 60 2>&1 | grep BAD_CUTOFF
```

To debug a specific turn, modify `shadow_debug_turn` in movegen.c and look for the anchor in question.
