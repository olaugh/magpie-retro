# Scrabble for Sega Genesis

A Scrabble AI for the Sega Genesis/Mega Drive featuring GADDAG-based move
generation with shadow algorithm pruning. Port of
[Magpie](https://github.com/jvc56/Magpie) to the 68000.

## Features

- Full GADDAG-based legal move generation
- Shadow algorithm for move pruning (~40% anchor cutoff rate)
- Leave-based equity evaluation
- AI vs AI gameplay with move history display
- NWL23 lexicon included

## Hardware Constraints

The Sega Genesis has:
- Motorola 68000 CPU @ 7.67 MHz
- 64 KB Work RAM
- 64 KB Video RAM
- 16 MB address space for ROM

The NWL23 lexicon is ~4.7 MB, fitting within the 16 MB ROM limit.

## Building

### Prerequisites

- m68k-elf cross-compiler toolchain (gcc, binutils)
- Native C compiler for test builds

### Building the ROM

```bash
make nwl23-shadow    # Build ROM with shadow algorithm
make nwl23-noshadow  # Build ROM without shadow (for comparison)
```

Output: `build/nwl23-shadow/scrabble.bin` or `build/nwl23-noshadow/scrabble.bin`

### Native Test Builds

```bash
make build/batch-nwl23-shadow/test_batch
make build/batch-nwl23-noshadow/test_batch

# Run comparison test
./build/batch-nwl23-shadow/test_batch 0 100 > /tmp/shadow.txt
./build/batch-nwl23-noshadow/test_batch 0 100 > /tmp/noshadow.txt
diff <(grep -v SHADOW_CUTOFF /tmp/shadow.txt) <(grep -v SHADOW_CUTOFF /tmp/noshadow.txt)
```

## Project Structure

```
magpie-genesis/
├── inc/                 # Header files
│   ├── scrabble.h       # Core types (Move, Board, Rack, game_event_t)
│   ├── kwg.h            # KWG/GADDAG lexicon access
│   ├── klv.h            # KLV leave values
│   └── anchor.h         # Anchor heap for shadow algorithm
├── src/
│   ├── main.c           # Game loop and display
│   ├── board.c          # Board representation and cross-sets
│   ├── movegen.c        # GADDAG move generation with shadow pruning
│   ├── game.c           # Game logic and scoring
│   ├── klv.c            # Leave value lookup
│   ├── kwg.c            # Lexicon functions
│   ├── graphics.c       # VDP graphics
│   ├── boot.s           # 68000 startup code
│   └── libc.c           # Minimal libc (memset, memcpy)
├── data/lexica/         # Lexicon data (checked in)
│   └── NWL23.kwg        # NWL23 GADDAG (~4.7 MB)
├── test_batch.c         # Batch game comparison test
├── Makefile
└── linker.ld            # 68000 linker script
```

## How It Works

### Shadow Algorithm

The shadow algorithm computes an upper bound on the best possible equity from
each anchor position before doing full move generation. Anchors are processed
in best-first order using a max-heap, allowing early cutoff when remaining
anchors cannot beat the current best move.

This provides ~40% reduction in anchors processed with identical move selection.

### GADDAG Move Generation

Uses a GADDAG data structure where each word is stored in multiple orientations.
For example, "CAT" is stored as C^AT, AC^T, and TAC^ (^ = separator). This
enables efficient generation of all words through any anchor square.

### Leave Evaluation

Move equity = score × 8 + leave_value, where leave_value comes from a precomputed
KLV (K Leave Values) table. Values are in eighths of a point for integer math.

## Testing

Use a Genesis emulator:
- [BlastEm](https://www.retrodev.com/blastem/) (recommended)
- [Genesis Plus GX](https://github.com/ekeeke/Genesis-Plus-GX)

Or test on real hardware with a flash cart.

## Credits

- GADDAG algorithm and shadow pruning from [Magpie](https://github.com/jvc56/Magpie)
- KWG/KLV formats from [Wolges](https://github.com/andy-k/wolges)
