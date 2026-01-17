# Scrabble for Sega Genesis

A Scrabble implementation for the Sega Genesis/Mega Drive featuring GADDAG-based
move generation using the NWL23 lexicon (or other KWG format lexicons).

## Features

- Full GADDAG-based legal move generation
- Two-player hotseat gameplay
- Standard Scrabble scoring with bonus squares
- Move list showing top scoring plays
- Works with NWL23, CSW24, or other KWG lexicons

## Hardware Constraints

The Sega Genesis has:
- Motorola 68000 CPU @ 7.67 MHz
- 64 KB Work RAM
- 64 KB Video RAM
- 16 MB address space for ROM

The NWL23.kwg lexicon is ~4.7 MB, fitting comfortably within the 16 MB ROM limit.

## Building

### Prerequisites

1. **SGDK (Sega Genesis Development Kit)**
   ```bash
   # Install SGDK
   git clone https://github.com/Stephane-D/SGDK.git /opt/sgdk
   cd /opt/sgdk
   make
   ```

   Or use a pre-built Docker image:
   ```bash
   docker pull ghcr.io/doragasu/sgdk-docker
   ```

2. **KWG Lexicon File**
   Place `NWL23.kwg` in the project root directory.

### Building the ROM

```bash
# Set SGDK path
export SGDK=/opt/sgdk

# Build
make

# Output will be in out/scrabble.bin
```

### Using Docker

```bash
docker run --rm -v $(pwd):/src ghcr.io/doragasu/sgdk-docker make
```

## Project Structure

```
magpie-genesis/
├── inc/                 # Header files
│   ├── scrabble.h       # Core definitions
│   └── kwg.h            # KWG/GADDAG format
├── src/                 # Source files
│   ├── main.c           # Game loop and input
│   ├── board.c          # Board representation
│   ├── movegen.c        # GADDAG move generation
│   ├── kwg.c            # Lexicon functions
│   ├── game.c           # Game logic
│   ├── graphics.c       # VDP graphics
│   └── boot.s           # Startup code
├── res/                 # Resources (tiles, sounds)
├── Makefile
├── linker.ld            # Linker script
└── README.md
```

## How It Works

### GADDAG Move Generation

The move generation algorithm uses a GADDAG (Generalized Directed Acyclic Graph)
data structure. Unlike a traditional DAWG which only stores words left-to-right,
a GADDAG stores each word in multiple orientations using a separator character.

For example, the word "CAT" is stored as:
- C^AT (start from C, separator, then AT)
- AC^T (A reversed to C, separator, then T)
- TAC^ (TA reversed to C, separator, end)

This allows efficient generation of all words that can be formed through any
anchor square on the board.

### KWG Format

Each node in the KWG is a 32-bit value:
- Bits 31-24: Letter (0-26, where 0 is separator/blank)
- Bit 23: Accepts flag (this path completes a valid word)
- Bit 22: Is-End flag (last sibling in arc list)
- Bits 21-0: Arc index (pointer to children)

Node 0's arc index points to DAWG root (for cross-set computation).
Node 1's arc index points to GADDAG root (for move generation).

### Cross-Set Computation

A cross-set for a square is the set of letters that can legally be placed there
considering perpendicular words. This is computed using the DAWG by checking
what letters, when combined with existing prefix/suffix letters in the
perpendicular direction, form valid words.

## Controls

- **D-Pad**: Move cursor on board
- **A Button**: Open move list
- **B Button**: Pass turn / Cancel
- **Start**: Start game / Pause

In move list:
- **Up/Down**: Navigate moves
- **A Button**: Play selected move
- **B Button**: Back to board

## Testing

Use a Genesis emulator like:
- [BlastEm](https://www.retrodev.com/blastem/) (recommended, most accurate)
- [Gens](http://www.gens.me/)
- [Exodus](https://www.exodusemulator.com/)

Or test on real hardware with a flash cart like Everdrive MD.

## License

This project is provided for educational purposes.
The NWL23 lexicon is copyright NASPA (North American Scrabble Players Association).

## Credits

- GADDAG algorithm based on [Magpie](https://github.com/jvc56/Magpie)
- KWG format from [Wolges](https://github.com/andy-k/wolges)
