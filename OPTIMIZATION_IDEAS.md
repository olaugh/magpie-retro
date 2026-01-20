# Optimization Ideas for Genesis Scrabble

Potential speedups identified during development. The 68000 runs at 7.6 MHz with expensive multiply (70 cycles for MULU) and no cache, so micro-optimizations matter.

## Cross-Set / Extension-Set Updates

### Done
- **Incremental cross-set update**: Only update affected squares after a move instead of all 225. ~2% speedup.
- **Avoid redundant updates in incremental**: Removed duplicate `update_square` calls for tile positions (was called once in main loop, once in perpendicular loop).
- **Single-direction update**: Split `update_square` into `update_square_vertical` and `update_square_horizontal`. Each direction-specific function only traverses neighbors in one direction and updates the relevant cross-sets/extension-sets. **~20% speedup** in cross-set updates (measured: 2.4 us/call → 1.9 us/call on native).

### Potential
- **Precompute row_start indices**: In scanning loops, `h_idx - col` computes row start. Could pass it in or cache it.

## Board Memory Layout

### Done
- **Structure of Arrays (SoA)**: Changed from `Square` struct array to separate arrays for letters, cross-sets, etc. Enables stride-1 access for the 68000's (A0)+ addressing.
- **Transposed v_* arrays**: Vertical view uses `col*15 + row` indexing so vertical scans also get stride-1 access.

### Potential
- **Eliminate v_idx multiply**: Currently compute `v_idx = col * BOARD_DIM + row` separately from h_idx. For the full board update, could track v_idx with stride addition (v_idx += BOARD_DIM when col increments).
- **Assembly for hot loops**: Hand-optimize board scanning in 68000 assembly to ensure (A0)+ is used.

## Move Generation

### Potential
- **Reduce anchor heap size**: MAX_ANCHORS was increased to 450 for correctness. Profile to see if this adds overhead.
- **Cache-friendly anchor ordering**: Process anchors in memory order rather than equity order to improve cache locality (though 68000 has no cache, this helps with prefetch patterns).
- **Inline small functions**: Ensure hot path functions are inlined by the compiler. Check disassembly.

## GADDAG Traversal

### Potential
- **Word-aligned node access**: Ensure KWG nodes are accessed with word-aligned addresses for faster memory access on 68000.
- **Reduce function call overhead**: `kwg_follow_arc`, `compute_cross_set`, etc. are called frequently. Consider inlining or using macros.
- **Early termination**: In cross-set computation, terminate early if result is already 0 (no valid letters).

## Scoring

### Potential
- **Precompute tile scores array offset**: Accessing `TILE_SCORES[letter]` requires base + offset. Could use pointer arithmetic.
- **Combine cross-score lookup**: Currently look up h_cross_scores or v_cross_scores based on direction. Could unify.

## General 68000 Optimizations

### Potential
- **Use 16-bit operations where possible**: 68000 is faster with word operations than long operations.
- **Register allocation**: Ensure hot variables stay in D/A registers. Use `register` keyword hints.
- **Avoid divisions**: Any `/ 15` or `% 15` is very expensive. Use lookup tables or bit tricks.
- **Loop unrolling**: For fixed-size loops (e.g., BOARD_DIM iterations), partial unrolling may help.
- **Branch prediction**: 68000 has no branch predictor, but arranging likely paths as fall-through helps.

## Measurements

To measure impact of changes, run full game in RetroArch unthrottled and note frame counter at end:
- Baseline (SoA + incremental): ~0x3904 frames
- Full cross-set update: ~0x3A33 frames (+303 frames, ~2% slower)
- Single-direction cross-set update: ~0x38F7 frames (-13 frames, ~0.1% faster)
