# Scrabble for Sega Genesis
# Standalone build (no SGDK dependency)

# Toolchain (Homebrew m68k-elf)
CC = m68k-elf-gcc
AS = m68k-elf-as
LD = m68k-elf-ld
OBJCOPY = m68k-elf-objcopy
NM = m68k-elf-nm

# Directories
SRC_DIR = src
INC_DIR = inc
RES_DIR = res
BUILD_DIR = build
OUT_DIR = out

# Source files
C_SOURCES = $(wildcard $(SRC_DIR)/*.c)
S_SOURCES = $(wildcard $(SRC_DIR)/*.s)

# Object files
C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
S_OBJECTS = $(patsubst $(SRC_DIR)/%.s,$(BUILD_DIR)/%.o,$(S_SOURCES))
OBJECTS = $(C_OBJECTS) $(S_OBJECTS)

# Compiler flags
CFLAGS = -m68000 -Wall -O2 -fno-builtin -fshort-enums
CFLAGS += -I$(INC_DIR)
CFLAGS += -nostdlib -ffreestanding -fomit-frame-pointer

# Assembler flags
ASFLAGS = -m68000

# Linker flags
LDFLAGS = -T linker.ld -nostdlib
LIBGCC = $(shell $(CC) -print-libgcc-file-name)

# Output
ROM_NAME = scrabble
ROM = $(OUT_DIR)/$(ROM_NAME).bin

# KWG lexicon data
KWG_SRC = $(BUILD_DIR)/kwg_data.c
KWG_OBJ = $(BUILD_DIR)/kwg_data.o

.PHONY: all clean dirs

all: dirs $(ROM)
	@echo "ROM created: $(ROM)"
	@ls -la $(ROM)

dirs:
	@mkdir -p $(BUILD_DIR) $(OUT_DIR)

# Compile C sources
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Assemble sources
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.s
	$(AS) $(ASFLAGS) -o $@ $<

# Link
$(BUILD_DIR)/$(ROM_NAME).elf: $(BUILD_DIR)/boot.o $(C_OBJECTS) $(KWG_OBJ)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

# Create ROM binary
$(ROM): $(BUILD_DIR)/$(ROM_NAME).elf
	$(OBJCOPY) -O binary $< $@
	@# Pad to power of 2 size for compatibility
	@SIZE=$$(stat -f%z $@ 2>/dev/null || stat -c%s $@); \
	if [ $$SIZE -lt 131072 ]; then \
		dd if=/dev/zero bs=1 count=$$((131072 - $$SIZE)) >> $@ 2>/dev/null; \
	fi
	@# Also create .md copy for emulators that prefer that extension
	@cp $@ $(OUT_DIR)/$(ROM_NAME).md

clean:
	rm -rf $(BUILD_DIR) $(OUT_DIR)

# Print symbol table
symbols: $(BUILD_DIR)/$(ROM_NAME).elf
	$(NM) -n $<

# Generate KWG data (run separately before building)
kwg:
	python3 tools/kwg2c.py /Users/olaugh/sources/jan14-magpie/MAGPIE/data/lexica/NWL23.kwg $(KWG_SRC)
