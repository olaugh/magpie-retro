# Shadow Algorithm Bug: Debug Plan

## Summary of Issues Found

### Bug #1: Anchor Heap Overflow (FIXED)
- **Cause**: `MAX_ANCHORS` was 200, but boards can have up to 450 anchors (15×15×2 directions)
- **Effect**: High-equity anchors silently dropped when heap filled up
- **Game affected**: Game 107, turn 20 (THRALL vs QAT)
- **Fix**: Increased `MAX_ANCHORS` from 200 to 450 in `inc/anchor.h`
- **Status**: ✅ FIXED

### Bug #2: Shadow Bounds Too Low (UNRESOLVED)
- **Cause**: Shadow algorithm computes equity upper bounds that are too low for some anchors
- **Effect**: Good anchors cut off prematurely because their computed bound < current best
- **Games affected**: 4, 5, 11, 16, 20, 46, 47, 52, 57, 61, 76, 82, 87 (and likely more)
- **Example**: Game 4, turn 5 - anchor (5,6,V) has bound=296 but actual best=326
- **Status**: ❌ NEEDS INVESTIGATION

## Detailed Analysis

### Game 107, Turn 20 (Bug #1 - FIXED)
- **Shadow picked**: `QAT@2,6H:31/115`
- **Should have picked**: `TH.ALL@9,14V:27/139`
- **Root cause**: Anchor at phys(8,14,V) with bound=1089 was inserted when heap had 200 entries, causing silent drop
- **Fix verification**: After increasing MAX_ANCHORS to 450, game 107 now matches noshadow

### Game 4, Turn 5 (Bug #2 - UNRESOLVED)
- **Shadow picked**: `AX@3,3V:37/298`
- **Should have picked**: `GAB.E@4,6V:36/326`
- **Root cause**: Anchor (5,6,V) has shadow bound=296, which is less than current best 298, so it gets cut off
- **Actual equity**: When processed anyway (in debug mode), finds equity 326
- **BAD_CUTOFF diagnostic**: `T5 BAD_CUTOFF: anchor(5,6,V) bound=296 before=298 after=326`

## Investigation Notes for Bug #2

### Board State at Game 4, Turn 5, Column 6
```
row_letters[0-14]: 255 255 255 255 255 255 255 12 255 255 255 255 255 255 255
```
Position 7 has letter 'M' (machine letter 12). All other positions empty.

### The GAB.E Move Structure
- G at row 4 (placed)
- A at row 5 (placed)
- B at row 6 (placed)
- M at row 7 (playthrough - existing)
- E at row 8 (placed)

### Shadow Bound Computation Issue
For anchor at (5,6,V):
- Shadow only extends LEFT (to rows 4,3,2,1)
- Shadow does NOT extend RIGHT through the playthrough at row 7
- Max recorded: `left=1 right=5` with equity 296
- Should extend: `left=4 right=8` to reach equity 326

### Possible Causes
1. Extension set constraints preventing rightward extension from anchor 5
2. nonblank_leftx check failing at position 6
3. rack_bits intersection failing for required letters

## Commands

```bash
# Test games 1-100 for differences
diff <(./build/batch-nwl23-shadow/test_batch 1 100 2>&1 | grep -v SHADOW_CUTOFF) \
     <(./build/batch-nwl23-noshadow/test_batch 1 100 2>&1)

# Build debug version with BAD_CUTOFF output
make build/batch-nwl23-shadow-debug/test_batch

# Run with BAD_CUTOFF diagnostics
./build/batch-nwl23-shadow-debug/test_batch 4 4 2>&1 | grep BAD_CUTOFF
```

## Files
- `inc/anchor.h` - MAX_ANCHORS definition
- `src/movegen.c` - Shadow algorithm implementation
  - `shadow_play_right()` - rightward extension
  - `nonplaythrough_shadow_play_left()` - leftward extension for empty anchors
  - `shadow_record()` - equity bound recording
