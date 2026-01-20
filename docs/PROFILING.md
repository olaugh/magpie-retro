# Scrabble Genesis CPU Cycle Profiling

Profiling data collected using gxtest's 68k cycle profiler, which hooks into
Genesis Plus GX's native CPU execution to track cycles per function.

## Test Configuration
- **Game**: Seed 0, NWL23 lexicon
- **Final Score**: 431-467 (identical for both builds)
- **Profiling Mode**: Simple (exclusive cycles only)

## Results Summary

| Metric | Shadow | NoShadow | Difference |
|--------|--------|----------|------------|
| Total Frames | 14,582 | 14,371 | -211 (-1.4%) |
| Total Cycles | 13,115,426,656 | 12,926,425,431 | -189M (-1.4%) |

The shadow optimization completes the game in slightly more frames but with
comparable total cycles, indicating the optimization trades off some overhead
for reduced redundant computation in move generation.

## Shadow Build Profile

```
Function                            Cycles     Calls       %  Cyc/Call
----------------------------------------------------------------------
recursive_gen                   6669299308    950641  50.85%      7015
go_on                           4337766580    976068  33.07%      4444
memcpy                           321462134     80749   2.45%      3981
generate_moves                   251814010     66465   1.92%      3788
shadow_play_right.isra.0         222309710    153283   1.70%      1450
klv_get_word_index               196352044      5560   1.50%     35315
shadow_record                    192005982     27069   1.46%      7093
record_move                      179017104     29526   1.36%      6063
update_display                   139384994       352   1.06%    395980
cache_row                        128654806      1997   0.98%     64424
insert_unrestricted_multipliers   115156370     44134   0.88%      2609
maybe_recalc_effective_multipliers 83198080     53427   0.63%      1557
try_restrict_tile                 58394854     24526   0.45%      2380
memset                            31631502      2397   0.24%     13196
draw_history                      30648590        87   0.23%    352282
klv_init                          27120590        32   0.21%    847518
compute_cross_set                 26378198       224   0.20%    117759
draw_board                        24804080        77   0.19%    322130
populate_leave_values             21494942      2503   0.16%      8587
init_tiles                         7199598        41   0.05%    175599
----------------------------------------------------------------------
Total                          13115426656
```

## NoShadow Build Profile

```
Function                            Cycles     Calls       %  Cyc/Call
----------------------------------------------------------------------
recursive_gen                   7373050510   1044196  57.04%      7060
go_on                           4665258164   1073161  36.09%      4347
record_move                      210912870     34818   1.63%      6057
klv_get_word_index               196353332      5556   1.52%     35340
update_display                   139385736       352   1.08%    395982
generate_moves                    58630404      9071   0.45%      6463
cache_row                         50920366       774   0.39%     65788
draw_history                      30647512        86   0.24%    356366
klv_init                          27120590        32   0.21%    847518
compute_cross_set                 26378954       228   0.20%    115697
draw_board                        24804304        75   0.19%    330724
memset                            24354736        55   0.19%    442813
populate_leave_values             21494564      2497   0.17%      8608
memcpy                            18760630      3013   0.15%      6226
init_tiles                         7199598        41   0.06%    175599
compute_extension_sets             6455568       205   0.05%     31490
klv_get_leave_value                5323878      5825   0.04%       913
wait_vblank                        2989784         5   0.02%    597956
update_square_vertical             1493016       432   0.01%      3456
clear_screen                       1450466         4   0.01%    362616
----------------------------------------------------------------------
Total                          12926425431
```

## Analysis

### Move Generation Core (recursive_gen + go_on)

| Build | Cycles | % of Total | Calls |
|-------|--------|------------|-------|
| Shadow | 11.0B | 83.9% | 1.93M |
| NoShadow | 12.0B | 93.1% | 2.12M |

The shadow optimization reduces core move generation calls by ~9%, saving
approximately 1 billion cycles.

### Shadow-Specific Overhead

The shadow build introduces new functions not present in NoShadow:
- `shadow_play_right.isra.0`: 222M cycles (1.70%)
- `shadow_record`: 192M cycles (1.46%)
- `insert_unrestricted_multipliers`: 115M cycles (0.88%)
- `maybe_recalc_effective_multipliers`: 83M cycles (0.63%)
- `try_restrict_tile`: 58M cycles (0.45%)

Total shadow overhead: ~670M cycles (5.1%)

### Memory Operations

| Operation | Shadow | NoShadow |
|-----------|--------|----------|
| memcpy calls | 80,749 | 3,013 |
| memcpy cycles | 321M | 19M |
| memset cycles | 32M | 24M |

The shadow build has 27x more memcpy calls due to caching shadow state between
moves. Despite this overhead, the net effect is still beneficial.

### generate_moves Function

| Build | Cycles | Calls | Cycles/Call |
|-------|--------|-------|-------------|
| Shadow | 252M | 66,465 | 3,788 |
| NoShadow | 59M | 9,071 | 6,463 |

The shadow build calls generate_moves 7.3x more often (due to incremental updates)
but each call is 41% faster on average.

## Methodology

Profiling is performed using gxtest's CPU cycle profiler which:
1. Hooks into Genesis Plus GX's `HOOK_CPU` callback
2. Tracks the PC (program counter) before each 68k instruction
3. Attributes each instruction's cycle cost to the function containing that PC
4. Uses symbol information from the ELF file for function boundaries

This provides accurate cycle counts without any ROM modification - the profiling
is entirely within the emulator.

## Running the Profiler

```bash
# Build and run the profiling test
cd /path/to/magpie-genesis
bazel test //tests/gxtest:scrabble_profile_test --test_output=all

# Run specific comparison
bazel test //tests/gxtest:scrabble_profile_test \
    --test_filter="ScrabbleProfile.ShadowVsNoShadow" \
    --test_output=all
```
