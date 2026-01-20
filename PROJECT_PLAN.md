# Project Plan: Genesis Scrabble Move Generator

## Current PR Scope

This PR (`fix-shadow-playthrough-bounds`) focuses on **shadow algorithm correctness** and proof of concept that the shadow pruning approach provides speedup. It is not intended to achieve feature parity with original magpie.

**Validated**: 10,000 games with 0 differences between shadow and non-shadow move generation.

## Gaps Between Genesis and Original Magpie

### 1. Incremental Cross-Set and Extension-Set Generation

**Original magpie**: Only updates cross-sets and extension-sets for squares that could be affected by the last move played.

**Genesis**: Recomputes all cross-sets and extension-sets for the entire board after every move.

**Impact**: Significant performance cost, especially in late game when most squares are unaffected by each move.

**Future work**: Track affected region (bounding box of played tiles + adjacent squares) and only update those.

### 2. Transposed Board Representation

**Original magpie**: Maintains two copies of the board - one normal, one transposed - so that vertical neighbors are adjacent in memory.

**Genesis**: Single board representation. Vertical scans use stride-15 addressing.

#### Why Transposed Boards Matter for the 68000

The reasoning for transposed boards **differs completely** between modern CPUs and the 68000:

**Modern CPU (cache locality)**: Adjacent memory accesses hit the same cache line. Stride-15 access causes cache misses.

**68000 (no cache)**: The Genesis has no data cache. Reading adjacent bytes vs. bytes 15 apart costs the same ~4 cycles per fetch. Cache locality is irrelevant.

**However**, the 68000 has a different reason to love transposed boards: **Auto-Increment Addressing `(A0)+`**.

##### The `(A0)+` Superpower

The 68000's most efficient memory access pattern is auto-increment:

```asm
; Transposed board: vertical neighbors are adjacent (stride = 1)
MOVE.B  (A0)+, D0   ; Read byte AND increment pointer - effectively FREE
```

Without transposition, vertical scans require manual pointer arithmetic:

```asm
; Normal board: vertical neighbors are stride-15 apart
MOVE.B  (A0), D0    ; Read byte
ADDA.W  #15, A0     ; Add 15 to pointer - costs 8 cycles!
```

**Cost per square scanned**: ~8 extra cycles without transposition.

##### Code Size Win

Without transposition, you need either:
- Two separate move generators (horizontal vs vertical), or
- A generic generator with a `step` parameter (burns a register, requires slow multiply/add logic)

With transposition:
- One optimized generator function
- Pass 1: `Generate(Board)` for horizontal moves
- Pass 2: `Generate(TransposedBoard)` for vertical moves
- Logic is identical, code size is halved, bugs are halved

##### Implementation Cost

- **Memory**: 225 bytes (negligible with 64KB available)
- **Update**: ~4-7 extra writes per move: `transposed[col][row] = tile`
- **Benefit**: Enables `(A0)+` for all scanning loops

##### Note for C Code

GCC is smart enough to use `(A0)+` when iterating a pointer sequentially (`*p++`). It is **not** smart enough to optimize a `+= 15` stride into anything efficient. The transposed board makes the compiler's job easy.

### 3. Cross-Score Sentinel Value

**Original magpie**: Stores cross_score as sum of perpendicular tile values (always >= 0). Checks board adjacency at scoring time to determine if there's a cross word.

**Genesis**: Uses -1 as sentinel for "no cross word" to skip adjacency check during scoring.

**Status**: Functionally equivalent. TODO comment in code.

### 4. Other Differences

- Genesis uses fixed-size arrays vs original magpie's more flexible data structures
- Genesis targets embedded (68000) vs original magpie's modern systems
- Leave values are pre-converted to 16-bit fixed point for Genesis

## Performance Measurements

### Shadow Algorithm Speedup

Native -O3 build, 100 games:
- **Shadow**: 0.92s (41% cutoff rate)
- **No shadow**: 1.20s
- **Speedup**: ~23%

### Genesis Timing

Frame counts displayed on screen (60 fps):
- Typical game: ~15,000 frames (~4 minutes)
- Per-move timing shown in move history

## Future Optimization Priorities

1. **Transposed board** - Enables (A0)+ addressing, halves code complexity
2. **Incremental cross-set updates** - Avoid full-board recomputation
3. **Assembly hot paths** - Hand-optimize critical loops if needed
