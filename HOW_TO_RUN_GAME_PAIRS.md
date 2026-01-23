# How to Run Gamepairs Tests

The gamepairs test harness compares two move generation strategies by running paired games with swapped player positions, canceling out first-player advantage.

## Historical Code

The gamepairs test code and `MoveGenFlags` runtime strategy switching were added in commit `304996d` for validation, then removed. To run gamepairs tests, you need to check out that commit or cherry-pick the relevant code.

```bash
# Check out the commit with gamepairs support
git checkout 304996d

# Or retrieve just the test file (requires MoveGenFlags from that commit)
git show 304996d:test_gamepairs.c > test_gamepairs.c
git show 304996d:inc/scrabble.h > inc/scrabble.h  # for MoveGenFlags
git show 304996d:src/movegen.c > src/movegen.c    # for generate_moves_ex
```

## Building (at commit 304996d)

```bash
gcc -O3 -Iinc -o test_gamepairs test_gamepairs.c \
    src/board.c src/game.c src/movegen.c src/klv.c src/kwg.c src/libc.c \
    build/nwl23-shadow/kwg_data.c build/nwl23-shadow/klv_data.c
```

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
