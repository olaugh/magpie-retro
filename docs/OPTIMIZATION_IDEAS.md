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

| Priority | Task | Effort | Gain |
|----------|------|--------|------|
| 1 | Inline small memcpy calls | Medium | 3-5% |
| 2 | Reduce go_on call overhead | Medium | 5-10% |
| 3 | Hand-optimize GADDAG traversal | High | 5-10% |
| 4 | Pre-compute unrestricted multiplier products | Medium | 1-2% |
| 5 | Optimize remaining MULS in score_move | Low | 1% |

---

## Testing Methodology

All optimizations should be validated with:
1. `make debug` - build with MULT_SMALL_DEBUG assertions
2. `bazel test //tests/gxtest:scrabble_validation_test` - 100-game validation
3. `bazel test //tests/gxtest:scrabble_test` - frame count regression test
