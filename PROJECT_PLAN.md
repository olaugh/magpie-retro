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

### 2. Board Memory Layout: Structure of Arrays + Transposition

**Current Genesis problem**: Uses Array of Structures (AoS) with `Square` struct.

```c
typedef struct {
    MachineLetter letter;    // offset 0
    uint8_t bonus;           // offset 1
    // 2 bytes padding
    CrossSet cross_set_h;    // offset 4  (4 bytes)
    CrossSet cross_set_v;    // offset 8  (4 bytes)
    int8_t cross_score_h;    // offset 12
    int8_t cross_score_v;    // offset 13
    // 2 bytes padding
    CrossSet leftx_h;        // offset 16 (4 bytes)
    CrossSet rightx_h;       // offset 20 (4 bytes)
    CrossSet leftx_v;        // offset 24 (4 bytes)
    CrossSet rightx_v;       // offset 28 (4 bytes)
} Square;  // sizeof = 32 bytes!
```

**The stride problem**: When scanning a row to check which squares are empty, we jump **32 bytes** between squares. This defeats the 68000's most powerful optimization.

#### The `(A0)+` Superpower

The 68000's most efficient memory access is auto-increment:

```asm
; Stride = 1: Use (A0)+ for FREE increment
MOVE.B  (A0)+, D0   ; Read byte AND increment - 0 extra cycles
DBRA    D1, Loop

; Stride = 32: Must manually add (SLOW)
MOVE.B  (A0), D0    ; Read byte
ADDA.W  #32, A0     ; Add 32 to pointer - 8 cycles!
DBRA    D1, Loop
```

**Result**: The AoS loop is ~2x slower than it could be.

#### Solution: Structure of Arrays (SoA) + Transposition

Split the `Square` struct into separate contiguous arrays. Keep horizontal and vertical (transposed) views for both directions.

```c
typedef struct {
    // ---------------------------------------------------------
    // HORIZONTAL VIEW (row-major: squares[row][col])
    // ---------------------------------------------------------
    uint8_t  h_letters[BOARD_SIZE];      // 225 bytes, stride 1
    uint8_t  h_bonuses[BOARD_SIZE];      // 225 bytes, stride 1
    CrossSet h_cross_sets[BOARD_SIZE];   // 900 bytes, stride 4
    int8_t   h_cross_scores[BOARD_SIZE]; // 225 bytes, stride 1
    CrossSet h_leftx[BOARD_SIZE];        // 900 bytes - left extension sets
    CrossSet h_rightx[BOARD_SIZE];       // 900 bytes - right extension sets

    // ---------------------------------------------------------
    // VERTICAL VIEW (transposed: squares[col][row])
    // Enables (A0)+ for vertical scans
    // ---------------------------------------------------------
    uint8_t  v_letters[BOARD_SIZE];      // 225 bytes
    CrossSet v_cross_sets[BOARD_SIZE];   // 900 bytes
    int8_t   v_cross_scores[BOARD_SIZE]; // 225 bytes
    CrossSet v_leftx[BOARD_SIZE];        // 900 bytes
    CrossSet v_rightx[BOARD_SIZE];       // 900 bytes

    // ---------------------------------------------------------
    // GAME STATE
    // ---------------------------------------------------------
    uint8_t tiles_played;
} Board;
```

**Memory cost**: ~6.4 KB vs current 7.2 KB (actually smaller due to no padding!)

#### Benefits

1. **Fast row scans**: `for (int c = 0; c < 15; c++) letter = h_letters[row*15 + c]` compiles to `MOVE.B (A0)+, D0`

2. **Fast column scans**: `for (int r = 0; r < 15; r++) letter = v_letters[col*15 + r]` also uses `(A0)+`

3. **One code path**: Same move generator works for both directions - just pass h_* or v_* arrays

4. **Cache-friendly on modern CPUs too**: Native test builds benefit from better cache utilization

#### Keeping Views in Sync

When placing tile at (row, col):
```c
h_letters[row * 15 + col] = tile;
v_letters[col * 15 + row] = tile;  // Transposed index
```

Cost: 2 writes per tile placed (~4-7 tiles per move = 8-14 extra writes). Negligible compared to the scanning speedup.

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

1. **Structure of Arrays (SoA) board layout** - Reduces stride from 32 bytes to 1 byte, enables (A0)+ auto-increment. This is the single biggest optimization for 68000.
2. **Transposed board views** - Enables (A0)+ for vertical scans too, allows single code path for both directions
3. **Incremental cross-set updates** - Avoid full-board recomputation after each move
4. **Assembly hot paths** - Hand-optimize critical loops if profiling shows benefit
