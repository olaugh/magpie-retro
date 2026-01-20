# Scrabble Genesis CPU Cycle Profiling

Profiling data collected using gxtest's 68k cycle profiler, which hooks into
Genesis Plus GX's native CPU execution to track cycles per function.

## Test Configuration
- **Games**: 4 parallel games per build (seeds 0-3)
- **Lexicon**: NWL23
- **Profiling Mode**: Simple (exclusive cycles only)

## Results Summary

| Metric | Shadow | NoShadow | Difference |
|--------|--------|----------|------------|
| Total Frames | 39,815 | 39,035 | +780 (+2.0%) |
| Total Cycles | 35.88B | 35.18B | +700M (+2.0%) |
| Avg Cycles/Game | 8.97B | 8.79B | |
| recursive_gen+go_on | 76.9% | 90.9% | -14% |

The Shadow build uses slightly more total cycles but significantly reduces the
percentage spent in core move generation (recursive_gen + go_on).

## Shadow Build Profile (4 Games)

```
Function                               Cycles       Calls       %    Cyc/Call
-----------------------------------------------------------------------------
recursive_gen                     16888403000     2406600  47.07%        7017
go_on                             10712928380     2480976  29.86%        4318
memcpy                             1229527096      309989   3.43%        3966
generate_moves                      950617864      243364   2.65%        3906
shadow_play_right.isra.0            944562010      666650   2.63%        1416
shadow_record                       870581698      122842   2.43%        7087
klv_get_word_index                  707268856       20008   1.97%       35349
update_display                      557542188        1409   1.55%      395700
record_move                         525617512       87198   1.47%        6027
insert_unrestricted_multipliers     518547344      200197   1.45%        2590
cache_row                           499226532        7787   1.39%       64110
maybe_recalc_effective_multipliers  372444982      241669   1.04%        1541
try_restrict_tile                   245803684      109650   0.69%        2241
memset                              128121154       10104   0.36%       12680
draw_history                        122104206         343   0.34%      355988
klv_init                            108482360         128   0.30%      847518
draw_board                           99275820         307   0.28%      323374
compute_cross_set                    97986294         964   0.27%      101645
populate_leave_values                79077586        9205   0.22%        8590
compute_extension_sets               29980650         886   0.08%       33838
-----------------------------------------------------------------------------
Total                             35875614017
```

## NoShadow Build Profile (4 Games)

```
Function                               Cycles       Calls       %    Cyc/Call
-----------------------------------------------------------------------------
recursive_gen                     19782948304     2805614  56.24%        7051
go_on                             12175984898     2893911  34.61%        4207
klv_get_word_index                  707263914       19997   2.01%       35368
record_move                         649482078      107691   1.85%        6030
update_display                      557540760        1407   1.58%      396262
generate_moves                      213186862       33951   0.61%        6279
cache_row                           202771828        3069   0.58%       66070
draw_history                        122103758         339   0.35%      360188
klv_init                            108482360         128   0.31%      847518
draw_board                           99276590         304   0.28%      326567
compute_cross_set                    97988324         970   0.28%      101018
memset                               97418972         220   0.28%      442813
populate_leave_values                79076354        9208   0.22%        8587
memcpy                               66217214       10623   0.19%        6233
compute_extension_sets               29980104         887   0.09%       33799
init_tiles                           28798392         164   0.08%      175599
klv_get_leave_value                  18746728       20508   0.05%         914
wait_vblank                          11929862          20   0.03%      596493
update_square_vertical                6137180        1733   0.02%        3541
clear_screen                          5804090          16   0.02%      362755
-----------------------------------------------------------------------------
Total                             35176874332
```

## Analysis

### Move Generation Core (recursive_gen + go_on)

| Build | Cycles | % of Total | Calls |
|-------|--------|------------|-------|
| Shadow | 27.6B | 76.9% | 4.89M |
| NoShadow | 32.0B | 90.9% | 5.70M |

The shadow optimization reduces core move generation by ~14% of total cycles
and ~14% fewer calls.

### Shadow-Specific Overhead

The shadow build introduces new functions not present in NoShadow:
- `shadow_play_right.isra.0`: 945M cycles (2.63%)
- `shadow_record`: 871M cycles (2.43%)
- `insert_unrestricted_multipliers`: 519M cycles (1.45%)
- `maybe_recalc_effective_multipliers`: 372M cycles (1.04%)
- `try_restrict_tile`: 246M cycles (0.69%)

Total shadow overhead: ~2.95B cycles (8.2%)

### Memory Operations

| Operation | Shadow | NoShadow |
|-----------|--------|----------|
| memcpy calls | 309,989 | 10,623 |
| memcpy cycles | 1.23B | 66M |
| memset cycles | 128M | 97M |

The shadow build has 29x more memcpy calls due to caching shadow state between
moves.

### generate_moves Function

| Build | Cycles | Calls | Cycles/Call |
|-------|--------|-------|-------------|
| Shadow | 951M | 243,364 | 3,906 |
| NoShadow | 213M | 33,951 | 6,279 |

The shadow build calls generate_moves 7.2x more often but each call is 38%
faster on average.

## Sampled Profiling

For faster profiling with reduced accuracy, sampled modes are available:

| Mode | Sample Rate | Time (4 games) | Accuracy |
|------|-------------|----------------|----------|
| Full | 1/1 | ~100s | Exact |
| Sampled10 | 1/10 | ~16s | Good |
| Sampled100 | 1/100 | ~6s | Approximate |

Run sampled profiling:
```bash
bazel test //tests/gxtest:scrabble_profile_test \
    --test_filter="ScrabbleProfile.ShadowVsNoShadowSampled10" \
    --test_output=all
```

## Running the Profiler

```bash
# Full profiling (every instruction)
bazel test //tests/gxtest:scrabble_profile_test --test_output=all

# Specific test
bazel test //tests/gxtest:scrabble_profile_test \
    --test_filter="ScrabbleProfile.ShadowVsNoShadowParallel" \
    --test_output=all

# Sampled profiling (faster)
bazel test //tests/gxtest:scrabble_profile_test \
    --test_filter="ScrabbleProfile.ShadowVsNoShadowSampled10" \
    --test_output=all
```

Note: The profile test has `tags = ["manual"]` so it won't run with
`bazel test //...`. You must specify the target explicitly.
