# Plan: Score in Eighths Optimization

## Status: COMPLETE

## Goal
Store all scores in eighths (×8) in movegen.c to eliminate the `score << 3` multiplication when computing equity. This removes a per-move multiplication from the hot path.

## Changes Made

### 1. `src/movegen.c`
- [x] Added `TO_EIGHTHS(x)` macro: `((x) * 8)`
- [x] Update `tile_scores[]` array to eighths using TO_EIGHTHS macro
- [x] Change bingo bonus from 50 to 400 in `shadow_record()`
- [x] Change bingo bonus from 50 to 400 in `record_move()`
- [x] Remove `<< 3` in `shadow_record()` equity calculation
- [x] Remove `<< 3` in `record_move()` equity calculation
- [x] Change `row_cross_scores` type from int8_t to int16_t
- [x] `move->score` now stores eighths

### 2. `src/board.c`
- [x] Added `TO_EIGHTHS(x)` macro
- [x] Update global `TILE_SCORES[]` to eighths (cross_scores computed in eighths)

### 3. `inc/scrabble.h`
- [x] Changed `h_cross_scores` and `v_cross_scores` from int8_t to int16_t

### 4. `inc/kwg.h` and `src/kwg.c`
- [x] Updated `compute_cross_set()` signature: cross_score param is now int16_t*

### 5. `src/graphics.c`
- [x] Display conversion: `score >> 3` when rendering

### 6. `tests/gxtest/scrabble_test.cpp`
- [x] Score reading: `ReadWord(...) >> 3` to convert from eighths
- [x] Updated expected frame counts for new baseline

## Profiling Results

### Baseline (before changes) - CSW24 50 games:
```
Function                          Cycles          %
recursive_gen                203033697055     47.09%
go_on                        123970575526     28.76%
shadow_record                 12731924779      2.95%
```

### After score-in-eighths - CSW24 50 games:
```
Function                          Cycles          %
recursive_gen                202530633389     46.64%
go_on                        127077137404     29.26%
shadow_record                 12512610264      2.88%
```

### Analysis
- `shadow_record`: 2.95% → 2.88% (reduced as expected by eliminating `<< 3`)
- Small regression in `go_on` due to int16_t cross_scores (wider memory ops)
- Net effect: slight regression in frame counts (~0.75%) due to int16_t overhead
- The int16_t type is required: cross_scores in eighths can exceed 127

## Test Results
All tests pass. NWL23 shadow speedup: 3.51%, CSW24 shadow speedup: 15.22%

---

## Additional Optimizations (Post-Score-in-Eighths)

### Shadow Loop Optimization
- Only loop for `min(rack.total, num_unrestricted_multipliers)` in shadow_record
- Avoids multiplying zero tile scores by zero multipliers
- NWL23 shadow speedup: 3.51% → 3.67%

### Inline Assembly for Word Multiplier (MULT_BY_WORD_MULT macro)
- Uses m68k inline assembly to avoid 70-cycle MULS when word_mult is 1, 2, or 3
- word_mult == 1: no-op (saves 70 cycles)
- word_mult == 2: `addw` (4 cycles vs 70)
- word_mult == 3: two adds (12 cycles vs 70)
- Default (4, 6, 9): falls back to MULS

**Results after inline asm optimization:**
- NWL23 Shadow total: 103,396 → 101,511 frames (1.82% faster)
- NWL23 NoShadow total: 107,336 → 105,077 frames (2.10% faster)
- NWL23 shadow speedup: 3.67% → 3.39%
- CSW24 shadow speedup: 15.12%
