# How to Run Gamepairs Tests

The gamepairs test harness compares two move generation strategies by running paired games with swapped player positions, canceling out first-player advantage.

## Getting the Code

The gamepairs test code was added in commit `304996d`. If it has been removed from the current branch, you can retrieve it with:

```bash
git show 304996d:test_gamepairs.c > test_gamepairs.c
```

## Building

```bash
gcc -O3 -Iinc -o test_gamepairs test_gamepairs.c \
    src/board.c src/game.c src/movegen.c src/klv.c src/kwg.c src/libc.c \
    build/nwl23-shadow/kwg_data.c build/nwl23-shadow/klv_data.c
```

Note: Requires `nwl23-shadow` data files. Run `make nwl23-shadow` first if needed.

## Usage

```bash
./test_gamepairs <start_seed> <end_seed>
```

### Output Format

Per-seed output to stdout:
```
seed:p0a:p1a:p0b:p1b:new_spread
```

Where:
- Game A: P0 uses new strategy, P1 uses old strategy
- Game B: P0 uses old strategy, P1 uses new strategy
- `new_spread = (P0_A - P1_A) + (P1_B - P0_B)` = net advantage of new strategy

Summary statistics are printed to stderr.

### Example

```bash
# Run 10,000 gamepairs
./test_gamepairs 0 9999 > results.txt

# View summary
tail -10 results.txt
```

## Modifying Strategies

Edit `test_gamepairs.c` to change what's being compared:

```c
/* Flags for old (no adjustments) and new (with adjustments) strategies */
MoveGenFlags old_flags = MOVEGEN_FLAG_NO_STATIC_ADJUSTMENTS;
MoveGenFlags new_flags = MOVEGEN_FLAG_NONE;
```
