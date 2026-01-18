# Scrabble for Sega Genesis
# Standalone build (no SGDK dependency)
# Builds ROMs for multiple lexicons (NWL23, CSW24) with shadow/no-shadow variants

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

# All build variants
VARIANTS = nwl23-shadow nwl23-noshadow csw24-shadow csw24-noshadow

.PHONY: all clean dirs $(VARIANTS) klv16

# Build all variants
all: $(VARIANTS)
	@echo ""
	@echo "=== Build complete ==="
	@ls -la $(OUT_DIR)/*.bin

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(OUT_DIR)

#
# NWL23 with shadow (default)
#
nwl23-shadow: dirs $(OUT_DIR)/scrabble-nwl23-shadow.bin
	@echo "NWL23 shadow ROM: $(OUT_DIR)/scrabble-nwl23-shadow.bin"

$(BUILD_DIR)/nwl23-shadow:
	@mkdir -p $@

$(BUILD_DIR)/nwl23-shadow/kwg_data.c: | $(BUILD_DIR)/nwl23-shadow
	python3 tools/kwg2c.py $(MAGPIE_DATA)/NWL23.kwg $@

$(BUILD_DIR)/nwl23-shadow/klv_data.c: data/NWL23.klv16 | $(BUILD_DIR)/nwl23-shadow
	python3 tools/klv2c.py $< $@

$(BUILD_DIR)/nwl23-shadow/kwg_data.o: $(BUILD_DIR)/nwl23-shadow/kwg_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/nwl23-shadow/klv_data.o: $(BUILD_DIR)/nwl23-shadow/klv_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

NWL23_SHADOW_C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/nwl23-shadow/%.o,$(C_SOURCES))
NWL23_SHADOW_CFLAGS = $(CFLAGS) -DLEXICON_NAME='"NWL23"' -DUSE_SHADOW=1

$(BUILD_DIR)/nwl23-shadow/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/nwl23-shadow
	$(CC) $(NWL23_SHADOW_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/nwl23-shadow/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)/nwl23-shadow
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/nwl23-shadow/scrabble.elf: $(BUILD_DIR)/nwl23-shadow/boot.o $(NWL23_SHADOW_C_OBJECTS) $(BUILD_DIR)/nwl23-shadow/kwg_data.o $(BUILD_DIR)/nwl23-shadow/klv_data.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

$(OUT_DIR)/scrabble-nwl23-shadow.bin: $(BUILD_DIR)/nwl23-shadow/scrabble.elf | dirs
	$(OBJCOPY) -O binary $< $@
	@SIZE=$$(stat -f%z $@ 2>/dev/null || stat -c%s $@); \
	if [ $$SIZE -lt 131072 ]; then \
		dd if=/dev/zero bs=1 count=$$((131072 - $$SIZE)) >> $@ 2>/dev/null; \
	fi

#
# NWL23 without shadow
#
nwl23-noshadow: dirs $(OUT_DIR)/scrabble-nwl23-noshadow.bin
	@echo "NWL23 no-shadow ROM: $(OUT_DIR)/scrabble-nwl23-noshadow.bin"

$(BUILD_DIR)/nwl23-noshadow:
	@mkdir -p $@

$(BUILD_DIR)/nwl23-noshadow/kwg_data.c: | $(BUILD_DIR)/nwl23-noshadow
	python3 tools/kwg2c.py $(MAGPIE_DATA)/NWL23.kwg $@

$(BUILD_DIR)/nwl23-noshadow/klv_data.c: data/NWL23.klv16 | $(BUILD_DIR)/nwl23-noshadow
	python3 tools/klv2c.py $< $@

$(BUILD_DIR)/nwl23-noshadow/kwg_data.o: $(BUILD_DIR)/nwl23-noshadow/kwg_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/nwl23-noshadow/klv_data.o: $(BUILD_DIR)/nwl23-noshadow/klv_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

NWL23_NOSHADOW_C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/nwl23-noshadow/%.o,$(C_SOURCES))
NWL23_NOSHADOW_CFLAGS = $(CFLAGS) -DLEXICON_NAME='"NWL23"' -DUSE_SHADOW=0

$(BUILD_DIR)/nwl23-noshadow/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/nwl23-noshadow
	$(CC) $(NWL23_NOSHADOW_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/nwl23-noshadow/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)/nwl23-noshadow
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/nwl23-noshadow/scrabble.elf: $(BUILD_DIR)/nwl23-noshadow/boot.o $(NWL23_NOSHADOW_C_OBJECTS) $(BUILD_DIR)/nwl23-noshadow/kwg_data.o $(BUILD_DIR)/nwl23-noshadow/klv_data.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

$(OUT_DIR)/scrabble-nwl23-noshadow.bin: $(BUILD_DIR)/nwl23-noshadow/scrabble.elf | dirs
	$(OBJCOPY) -O binary $< $@
	@SIZE=$$(stat -f%z $@ 2>/dev/null || stat -c%s $@); \
	if [ $$SIZE -lt 131072 ]; then \
		dd if=/dev/zero bs=1 count=$$((131072 - $$SIZE)) >> $@ 2>/dev/null; \
	fi

#
# CSW24 with shadow
#
csw24-shadow: dirs $(OUT_DIR)/scrabble-csw24-shadow.bin
	@echo "CSW24 shadow ROM: $(OUT_DIR)/scrabble-csw24-shadow.bin"

$(BUILD_DIR)/csw24-shadow:
	@mkdir -p $@

$(BUILD_DIR)/csw24-shadow/kwg_data.c: | $(BUILD_DIR)/csw24-shadow
	python3 tools/kwg2c.py $(MAGPIE_DATA)/CSW24.kwg $@

$(BUILD_DIR)/csw24-shadow/klv_data.c: data/CSW24.klv16 | $(BUILD_DIR)/csw24-shadow
	python3 tools/klv2c.py $< $@

$(BUILD_DIR)/csw24-shadow/kwg_data.o: $(BUILD_DIR)/csw24-shadow/kwg_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/csw24-shadow/klv_data.o: $(BUILD_DIR)/csw24-shadow/klv_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

CSW24_SHADOW_C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/csw24-shadow/%.o,$(C_SOURCES))
CSW24_SHADOW_CFLAGS = $(CFLAGS) -DLEXICON_NAME='"CSW24"' -DUSE_SHADOW=1

$(BUILD_DIR)/csw24-shadow/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/csw24-shadow
	$(CC) $(CSW24_SHADOW_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/csw24-shadow/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)/csw24-shadow
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/csw24-shadow/scrabble.elf: $(BUILD_DIR)/csw24-shadow/boot.o $(CSW24_SHADOW_C_OBJECTS) $(BUILD_DIR)/csw24-shadow/kwg_data.o $(BUILD_DIR)/csw24-shadow/klv_data.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

$(OUT_DIR)/scrabble-csw24-shadow.bin: $(BUILD_DIR)/csw24-shadow/scrabble.elf | dirs
	$(OBJCOPY) -O binary $< $@
	@SIZE=$$(stat -f%z $@ 2>/dev/null || stat -c%s $@); \
	if [ $$SIZE -lt 131072 ]; then \
		dd if=/dev/zero bs=1 count=$$((131072 - $$SIZE)) >> $@ 2>/dev/null; \
	fi

#
# CSW24 without shadow
#
csw24-noshadow: dirs $(OUT_DIR)/scrabble-csw24-noshadow.bin
	@echo "CSW24 no-shadow ROM: $(OUT_DIR)/scrabble-csw24-noshadow.bin"

$(BUILD_DIR)/csw24-noshadow:
	@mkdir -p $@

$(BUILD_DIR)/csw24-noshadow/kwg_data.c: | $(BUILD_DIR)/csw24-noshadow
	python3 tools/kwg2c.py $(MAGPIE_DATA)/CSW24.kwg $@

$(BUILD_DIR)/csw24-noshadow/klv_data.c: data/CSW24.klv16 | $(BUILD_DIR)/csw24-noshadow
	python3 tools/klv2c.py $< $@

$(BUILD_DIR)/csw24-noshadow/kwg_data.o: $(BUILD_DIR)/csw24-noshadow/kwg_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/csw24-noshadow/klv_data.o: $(BUILD_DIR)/csw24-noshadow/klv_data.c
	$(CC) $(CFLAGS) -c -o $@ $<

CSW24_NOSHADOW_C_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/csw24-noshadow/%.o,$(C_SOURCES))
CSW24_NOSHADOW_CFLAGS = $(CFLAGS) -DLEXICON_NAME='"CSW24"' -DUSE_SHADOW=0

$(BUILD_DIR)/csw24-noshadow/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)/csw24-noshadow
	$(CC) $(CSW24_NOSHADOW_CFLAGS) -c -o $@ $<

$(BUILD_DIR)/csw24-noshadow/%.o: $(SRC_DIR)/%.s | $(BUILD_DIR)/csw24-noshadow
	$(AS) $(ASFLAGS) -o $@ $<

$(BUILD_DIR)/csw24-noshadow/scrabble.elf: $(BUILD_DIR)/csw24-noshadow/boot.o $(CSW24_NOSHADOW_C_OBJECTS) $(BUILD_DIR)/csw24-noshadow/kwg_data.o $(BUILD_DIR)/csw24-noshadow/klv_data.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

$(OUT_DIR)/scrabble-csw24-noshadow.bin: $(BUILD_DIR)/csw24-noshadow/scrabble.elf | dirs
	$(OBJCOPY) -O binary $< $@
	@SIZE=$$(stat -f%z $@ 2>/dev/null || stat -c%s $@); \
	if [ $$SIZE -lt 131072 ]; then \
		dd if=/dev/zero bs=1 count=$$((131072 - $$SIZE)) >> $@ 2>/dev/null; \
	fi

#
# Utility targets
#
clean:
	rm -rf $(BUILD_DIR) $(OUT_DIR)

# Print symbol tables
symbols-nwl23-shadow: $(BUILD_DIR)/nwl23-shadow/scrabble.elf
	$(NM) -n $<

symbols-nwl23-noshadow: $(BUILD_DIR)/nwl23-noshadow/scrabble.elf
	$(NM) -n $<

symbols-csw24-shadow: $(BUILD_DIR)/csw24-shadow/scrabble.elf
	$(NM) -n $<

symbols-csw24-noshadow: $(BUILD_DIR)/csw24-noshadow/scrabble.elf
	$(NM) -n $<

# Generate KLV16 files from Magpie KLV2 format (run once)
data/NWL23.klv16:
	python3 tools/klv2_to_klv16.py $(MAGPIE_DATA)/NWL23.klv2 $@

data/CSW24.klv16:
	python3 tools/klv2_to_klv16.py $(MAGPIE_DATA)/CSW24.klv2 $@

# Regenerate all KLV16 files
klv16: data/NWL23.klv16 data/CSW24.klv16
