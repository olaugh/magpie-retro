# Scrabble for Sega Genesis
# Standalone build (no SGDK dependency)
# Builds ROMs for multiple lexicons (NWL23, CSW24)

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

# Magpie data path
MAGPIE_DATA = /Users/olaugh/sources/jan14-magpie/MAGPIE/data/lexica

# Source files
C_SOURCES = $(wildcard $(SRC_DIR)/*.c)
S_SOURCES = $(wildcard $(SRC_DIR)/*.s)

# Compiler flags
CFLAGS = -m68000 -Wall -O2 -fno-builtin -fshort-enums
CFLAGS += -I$(INC_DIR)
CFLAGS += -nostdlib -ffreestanding -fomit-frame-pointer

# Assembler flags
ASFLAGS = -m68000

# Linker flags
LDFLAGS = -T linker.ld -nostdlib
LIBGCC = $(shell $(CC) -print-libgcc-file-name)

# Lexicons to build
LEXICONS = nwl23 csw24

.PHONY: all clean dirs nwl23 csw24 data

# Build all lexicons
all: $(LEXICONS)
	@echo ""
	@echo "=== Build complete ==="
	@ls -la $(OUT_DIR)/*.bin

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(OUT_DIR)

#
# NWL23 build
#
nwl23: dirs $(OUT_DIR)/scrabble-nwl23.bin
	@echo "NWL23 ROM created: $(OUT_DIR)/scrabble-nwl23.bin"

$(BUILD_DIR)/nwl23:
	@mkdir -p $@

$(BUILD_DIR)/nwl23/kwg_data.c: | $(BUILD_DIR)/nwl23
	python3 tools/kwg2c.py $(MAGPIE_DATA)/NWL23.kwg $@

$(BUILD_DIR)/nwl23/klv_data.c: data/NWL23.klv16 | $(BUILD_DIR)/nwl23
	python3 tools/klv2c.py $< $@

$(BUILD_DIR)/nwl23/kwg_data.o: $(BUILD_DIR)/nwl23/kwg_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/nwl23/klv_data.o: $(BUILD_DIR)/nwl23/klv_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Object files for NWL23 (in lexicon-specific subdir)
NWL23_C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/nwl23/%.o,$(C_SOURCES))
NWL23_S_OBJECTS = $(patsubst $(SRC_DIR)/%.s,$(BUILD_DIR)/nwl23/%.o,$(S_SOURCES))
NWL23_CFLAGS = $(CFLAGS) -DLEXICON_NAME='"NWL23"'

$(BUILD_DIR)/nwl23/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/nwl23
	$(CC) $(NWL23_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/nwl23/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)/nwl23
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/nwl23/scrabble.elf: $(BUILD_DIR)/nwl23/boot.o $(NWL23_C_OBJECTS) $(BUILD_DIR)/nwl23/kwg_data.o $(BUILD_DIR)/nwl23/klv_data.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

$(OUT_DIR)/scrabble-nwl23.bin: $(BUILD_DIR)/nwl23/scrabble.elf | dirs
	$(OBJCOPY) -O binary $< $@
	@# Pad to power of 2 size for compatibility
	@SIZE=$$(stat -f%z $@ 2>/dev/null || stat -c%s $@); \
	if [ $$SIZE -lt 131072 ]; then \
		dd if=/dev/zero bs=1 count=$$((131072 - $$SIZE)) >> $@ 2>/dev/null; \
	fi
	@cp $@ $(OUT_DIR)/scrabble-nwl23.md

#
# CSW24 build
#
csw24: dirs $(OUT_DIR)/scrabble-csw24.bin
	@echo "CSW24 ROM created: $(OUT_DIR)/scrabble-csw24.bin"

$(BUILD_DIR)/csw24:
	@mkdir -p $@

$(BUILD_DIR)/csw24/kwg_data.c: | $(BUILD_DIR)/csw24
	python3 tools/kwg2c.py $(MAGPIE_DATA)/CSW24.kwg $@

$(BUILD_DIR)/csw24/klv_data.c: data/CSW24.klv16 | $(BUILD_DIR)/csw24
	python3 tools/klv2c.py $< $@

$(BUILD_DIR)/csw24/kwg_data.o: $(BUILD_DIR)/csw24/kwg_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/csw24/klv_data.o: $(BUILD_DIR)/csw24/klv_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Object files for CSW24 (in lexicon-specific subdir)
CSW24_C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/csw24/%.o,$(C_SOURCES))
CSW24_S_OBJECTS = $(patsubst $(SRC_DIR)/%.s,$(BUILD_DIR)/csw24/%.o,$(S_SOURCES))
CSW24_CFLAGS = $(CFLAGS) -DLEXICON_NAME='"CSW24"'

$(BUILD_DIR)/csw24/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/csw24
	$(CC) $(CSW24_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/csw24/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)/csw24
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/csw24/scrabble.elf: $(BUILD_DIR)/csw24/boot.o $(CSW24_C_OBJECTS) $(BUILD_DIR)/csw24/kwg_data.o $(BUILD_DIR)/csw24/klv_data.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

$(OUT_DIR)/scrabble-csw24.bin: $(BUILD_DIR)/csw24/scrabble.elf | dirs
	$(OBJCOPY) -O binary $< $@
	@# Pad to power of 2 size for compatibility
	@SIZE=$$(stat -f%z $@ 2>/dev/null || stat -c%s $@); \
	if [ $$SIZE -lt 131072 ]; then \
		dd if=/dev/zero bs=1 count=$$((131072 - $$SIZE)) >> $@ 2>/dev/null; \
	fi
	@cp $@ $(OUT_DIR)/scrabble-csw24.md

#
# Utility targets
#
clean:
	rm -rf $(BUILD_DIR) $(OUT_DIR)

# Print symbol table
symbols-nwl23: $(BUILD_DIR)/nwl23/scrabble.elf
	$(NM) -n $<

symbols-csw24: $(BUILD_DIR)/csw24/scrabble.elf
	$(NM) -n $<

# Generate KLV16 files from Magpie KLV2 format (run once)
data/NWL23.klv16:
	python3 tools/klv2_to_klv16.py $(MAGPIE_DATA)/NWL23.klv2 $@

data/CSW24.klv16:
	python3 tools/klv2_to_klv16.py $(MAGPIE_DATA)/CSW24.klv2 $@

# Regenerate all KLV16 files
klv16: data/NWL23.klv16 data/CSW24.klv16
