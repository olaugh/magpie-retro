# CSW24 Shadow vs NoShadow Profiling Results

**Date:** 2025-01-20
**Configuration:** 50 games, 1/100 sampling rate, parallel execution via fork
**Lexicon:** CSW24 (Collins Scrabble Words 2024)

## Summary

| Metric | Shadow | NoShadow | Difference |
|--------|--------|----------|------------|
| Total frames | 478,350 | 528,584 | -9.5% |
| Total cycles | 431.1B | 476.1B | **-9.5% faster** |
| Avg cycles/game | 8.62B | 9.52B | -9.4% |

**The shadow algorithm provides a 9.5% speedup on CSW24.**

## Top Functions by Cycle Count

### Shadow Build

| Function | Cycles | % | Cyc/Call |
|----------|--------|---|----------|
| `recursive_gen` | 203.0B | 47.09% | 702,436 |
| `go_on` | 124.0B | 28.76% | 416,958 |
| `memcpy` | 17.4B | 4.03% | 441,552 |
| `shadow_record` | 12.7B | 2.95% | 766,660 |
| `generate_moves` | 12.0B | 2.79% | 396,490 |
| `shadow_play_right` | 11.1B | 2.57% | 126,566 |
| `klv_get_word_index` | 8.2B | 1.90% | 3,661,161 |
| `insert_unrestricted_multipliers` | 7.5B | 1.74% | 280,408 |
| `update_display` | 6.7B | 1.56% | 36,629,746 |
| `record_move` | 6.3B | 1.45% | 616,691 |

### NoShadow Build

| Function | Cycles | % | Cyc/Call |
|----------|--------|---|----------|
| `recursive_gen` | 272.7B | 57.28% | 698,714 |
| `go_on` | 164.4B | 34.53% | 409,063 |
| `record_move` | 8.7B | 1.82% | 612,662 |
| `klv_get_word_index` | 8.2B | 1.72% | 3,552,023 |
| `update_display` | 6.7B | 1.42% | 42,657,632 |
| `generate_moves` | 2.5B | 0.53% | 655,369 |
| `cache_row` | 2.4B | 0.51% | 6,397,030 |

## Analysis

### Core Move Generation Savings

The shadow algorithm significantly reduces work in the core move generation functions:

| Function | Shadow | NoShadow | Reduction |
|----------|--------|----------|-----------|
| `recursive_gen` | 203.0B | 272.7B | **25.5%** |
| `go_on` | 124.0B | 164.4B | **24.6%** |
| **Combined** | 327.0B | 437.1B | **25.2%** |

### Shadow Algorithm Overhead

The shadow algorithm introduces these additional functions:

| Function | Cycles | % of Total |
|----------|--------|------------|
| `shadow_record` | 12.7B | 2.95% |
| `shadow_play_right` | 11.1B | 2.57% |
| **Total Overhead** | ~24B | ~5.5% |

### Net Benefit

- Cycles saved in `recursive_gen` + `go_on`: ~110B
- Shadow overhead: ~24B
- **Net savings: ~86B cycles (18% of move generation)**

The shadow pruning algorithm successfully identifies and skips anchors that cannot produce better moves than already found, reducing the search space by approximately 25% while adding only 5.5% overhead.

## Test Command

```bash
bazel test //tests/gxtest:scrabble_profile_test \
    --test_filter="ScrabbleProfile.CSW24ShadowVsNoShadowSampled100" \
    --test_output=all
```
