# Static Evaluation Improvements: Gamepairs Test Results

## Summary

Added static evaluation adjustments matching original magpie:
1. **Opening move penalties**: Penalize placing vowels on hotspot squares (adjacent to DLS) on the opening move
2. **Endgame adjustments**:
   - Non-outplay: `-2 * player_rack_value - 10` constant penalty
   - Outplay: `+2 * opponent_rack_value` bonus

## Test Methodology

Ran 10,000 gamepairs (20,000 total games) comparing:
- **New strategy**: With static evaluation adjustments enabled
- **Old strategy**: With `MOVEGEN_FLAG_NO_STATIC_ADJUSTMENTS` (raw leave values only)

Each gamepair uses the same random seed twice:
- Game A: Player 0 = new strategy, Player 1 = old strategy
- Game B: Player 0 = old strategy, Player 1 = new strategy

This cancels out first-player advantage and measures pure strategy strength.

## Results

| Metric | Value |
|--------|-------|
| **New strategy wins** | 6,147 (61.5%) |
| **Old strategy wins** | 2,077 (20.8%) |
| **Ties** | 1,776 (17.8%) |
| **Average spread per pair** | **+12.16 points** |
| **Total spread** | +121,632 points |

## Interpretation

The new static evaluation adjustments provide a substantial improvement:
- **3:1 win ratio** over unadjusted strategy
- **+12.16 points per gamepair** average advantage
- Statistically significant across 10,000 gamepairs

The improvements come from:
1. Better opening move selection (avoiding vowels on DLS-adjacent squares)
2. More accurate endgame play (accounting for rack leave values and outplay potential)
