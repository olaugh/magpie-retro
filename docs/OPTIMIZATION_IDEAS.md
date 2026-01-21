# Optimization Ideas for Scrabble Genesis

Analysis of disassembly and profiling data to identify potential speedups.

## Tools Available

- **split_asm.py**: Generate browsable HTML with mixed C/assembly view
- **Profiling JSON**: `profiles/csw24-shadow.profile.json` has per-address cycle counts
- **objdump**: `m68k-elf-objdump -S build/nwl23-shadow/scrabble.elf` for mixed output

## Profiling Summary (CSW24 Shadow, 10 games)

Total cycles: ~10.5 billion

### Hottest Functions

| Function | Key Hot Spots | % of Total |
|----------|---------------|------------|
| `go_on` | GADDAG traversal, state save/restore | ~25% |
| `record_move` | Move recording, equity calculation | ~5% |
| `compute_cross_set` | Cross-set calculation, arc traversal | ~3% |
| `shadow_record` | Shadow equity calculation | ~2% |

---

## Optimization Opportunities

### 1. Reduce Function Call Overhead in go_on

**Problem**: The hottest addresses (0x4a66-0x4a80) are in function epilogue code - restoring registers and cleaning up the stack after recursive calls.

```asm
4a66:  moveal %sp@(22),%a0      ; restore state
4a6a:  moveb %d2,%a0@(112)      ; restore byte
4a6e:  movew %d1,%a0@(108)      ; restore word
4a72:  movew %sp@(34),%a0@(110) ; restore word
4a78:  moveml %sp@+,%d2-%d4/%a2-%a3  ; pop registers
4a7c:  lea %sp@(24),%sp         ; clean stack
4a80:  rts
```

**Ideas**:
- Consider inlining small helper functions
- Use tail-call optimization where possible
- Reduce number of callee-saved registers if possible

**Difficulty**: Medium
**Potential Gain**: 5-10%

---

### 2. Eliminate memcpy Calls in State Save/Restore

**Problem**: 32 `memcpy` calls in the codebase, many in hot paths like `shadow_play_right` and `generate_moves`.

```asm
; shadow_play_right saves state with 4 memcpy calls:
5290:  jsr 3704 <memcpy>   ; save unrestricted data
52ae:  jsr 3704 <memcpy>   ; save more state
52cc:  jsr 3704 <memcpy>   ; save cross word multipliers
52e8:  jsr 3704 <memcpy>   ; save effective multipliers
```

**Ideas**:
- Use inline assembly for small fixed-size copies (e.g., 14 or 28 bytes)
- Use `movem.l` for copying multiple longwords at once
- Consider storing state differently to avoid copies

**Difficulty**: Medium
**Potential Gain**: 3-5%

---

### 3. Optimize GADDAG Arc Traversal in compute_cross_set

**Problem**: Hot loop computing arc index with multiply-by-3:

```asm
3684:  addl %d1,%d1         ; d1 *= 2
3686:  moveal %d1,%a0       ; a0 = d1
3688:  addal %d1,%a0        ; a0 = d1 * 3
368a:  movel %a2@(0,%a0:l),%d1  ; load arc
```

**Ideas**:
- Pre-compute arc offsets if structure allows
- Consider different arc encoding to avoid multiply
- Use lookup table for small indices

**Difficulty**: High (changes data structure)
**Potential Gain**: 2-3%

---

### 4. Remaining MULS Instructions

**Problem**: Still have a few `muls` instructions (70 cycles each):

| Address | Function | Context |
|---------|----------|---------|
| 0x3fc8 | shadow_record | Loop over unrestricted_eff_letter_mult |
| 0x3ffa | shadow_record | word_multiplier default case (4 or 9) |
| 0x44b6 | record_move | Similar to shadow_record |
| 0x1542+ | score_move | Score calculation |

**Ideas**:
- The loop at 0x3fc8 multiplies by values from an array - could pre-compute products
- score_move multiplications may be unavoidable (variable multipliers)

**Difficulty**: Medium
**Potential Gain**: 1-2%

---

### 5. Inline tile_scores Lookup

**Problem**: Frequent lookups to `tile_scores` array with sign extension:

```asm
47b2:  lea 719e <tile_scores>,%a3   ; load base address
47b8:  movew %a3@(0,%d0:l),%d4      ; indexed load
```

**Ideas**:
- Keep tile_scores base in a register throughout hot functions
- Use byte table + shift instead of word table (if values fit)

**Difficulty**: Low
**Potential Gain**: 1%

---

### 6. Shadow Loop Already Optimized

The shadow loop in `shadow_record` was optimized to only iterate `min(rack.total, num_unrestricted_multipliers)` times. This is already implemented.

---

### 7. Branch Prediction / Code Layout

**Problem**: 68000 has no branch prediction, but branch direction affects timing slightly.

**Ideas**:
- Profile branch taken/not-taken ratios
- Arrange code so common paths fall through
- Use `bra.s` instead of `bra.w` where possible (2 cycles faster)

**Difficulty**: Low
**Potential Gain**: 1%

---

### 8. Consider Assembly for Critical Inner Loops

**Problem**: Compiler may not generate optimal code for tight loops.

**Candidates for hand-written assembly**:
- GADDAG arc traversal loop in `compute_cross_set`
- Unrestricted multiplier loop in `shadow_record`
- Rack tile counting/iteration

**Difficulty**: High
**Potential Gain**: 5-10% for targeted loops

---

## Priority Ranking

| Priority | Task | Effort | Gain | Status |
|----------|------|--------|------|--------|
| 1 | Inline small memcpy calls | Medium | 3-5% | **DONE** |
| 2 | Reduce go_on call overhead | Medium | 5-10% | Open |
| 3 | Hand-optimize GADDAG traversal | High | 5-10% | Open |
| 4 | Pre-compute unrestricted multiplier products | Medium | 1-2% | Open |
| 5 | Optimize remaining MULS in score_move | Low | 1% | Open |

---

## Completed Optimizations

### D. Inline memcpy in shadow_play_right

**Problem**: `shadow_play_right` called memcpy 4 times for save and 4 times for restore, creating significant function call overhead on 68000.

**Implementation**:
- Replaced stack-allocated copy arrays with struct-level copies already in MoveGenState
- Used struct assignment for Rack (28 bytes) instead of memcpy
- Used explicit long-word (32-bit) copies for 14-byte arrays instead of memcpy

**Result**: **~3% FASTER**
- NWL23 100 games: 902,527 → 875,416 frames (3.0% improvement)
- CSW24 100 games: 920,782 → 894,435 frames (2.9% improvement)
- NWL23 10-seed regression: 99,921 → 97,333 frames (2.6% improvement)
- CSW24 10-seed regression: 88,664 → 86,138 frames (2.8% improvement)

**Analysis**: The optimization benefits from:
- Avoiding memcpy function call overhead (JSR/RTS + stack frame setup)
- Struct assignment compiles to efficient move instructions
- Explicit long-word copies (3-4 MOVE.L) vs memcpy byte loop (28 or 14 iterations)
- Using struct-level copies avoids stack allocation

**Conclusion**: Confirmed that inlining small memcpy calls provides measurable speedup on 68000.

---

## Explored But Rejected Optimizations

### A. Transposed Bonus Arrays (USE_BONUS_TRANSPOSE)

**Hypothesis**: Pre-computing transposed bonus arrays (h_bonuses/v_bonuses like h_letters/v_letters) would enable pointer-based access in cache_row, avoiding per-column index computation.

**Implementation**: Added h_bonuses[225] and v_bonuses[225] to Board struct, initialized in board_init(), and used pointer-based access in cache_row when USE_BONUS_TRANSPOSE=1.

**Result**: **0.4% SLOWER** (100,313 vs 99,921 NWL23 Shadow frames)

**Analysis**: The pointer-based approach adds overhead on 68000:
- Extra instructions to load pointer to register and dereference
- Larger Board struct (450 extra bytes)
- Extra initialization cost

**Conclusion**: Array-based index computation is more efficient than pointer indirection on 68000. Left as optional compile flag (default=0) for testing.

### B. Separated Letter/Word Multiplier Arrays

**Hypothesis**: Separating letter_mult and word_mult into distinct arrays would eliminate bit extraction overhead (shift and mask operations).

**Analysis**: Given that transposed bonuses hurt performance, separated arrays would likely have similar results. The 68000 is efficient at nibble extraction:
- `LSR.B #4` for upper nibble (letter_mult)
- `AND.B #0x0F` for lower nibble (word_mult)

**Conclusion**: Not implemented - expected to hurt performance based on pattern A.

### C. Row Caching (USE_ROW_CACHE)

**Hypothesis**: Copying board data to local arrays in cache_row is overhead on 68000 since there's no CPU cache to benefit.

**Result**: **0.2% SLOWER** when disabled (99,921 vs 100,122 NWL23 Shadow frames)

**Analysis**: Even without CPU cache, the data is accessed many times per row during traversal. The pointer indirection overhead exceeds the copy cost.

**Conclusion**: Keep USE_ROW_CACHE=1 (default).

---

## Key Insight: Pointer Indirection on 68000

On 68000, pointer-based access often hurts rather than helps:
- Loading a pointer requires extra instructions
- Address registers are scarce (A0-A7)
- Computed array indexing can be folded into addressing modes

Prefer array access with computed indices over pointer-based patterns.

---

## Testing Methodology

All optimizations should be validated with:
1. `make debug` - build with MULT_SMALL_DEBUG assertions
2. `bazel test //tests/gxtest:scrabble_validation_test` - 100-game validation
3. `bazel test //tests/gxtest:scrabble_test` - frame count regression test
