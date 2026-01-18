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
MAGPIE_DATA = data/lexica

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

#
# Native test build with sanitizers
#
NATIVE_CC = clang
NATIVE_CFLAGS = -g -O0 -Wall -Wextra -fsanitize=address,undefined -DUSE_SHADOW=1
NATIVE_LDFLAGS = -fsanitize=address,undefined

NATIVE_SOURCES = src/board.c src/game.c src/movegen.c src/klv.c src/kwg.c src/libc.c
NATIVE_OBJECTS = $(patsubst src/%.c,build/native/%.o,$(NATIVE_SOURCES))

build/native:
	@mkdir -p $@

build/native/%.o: src/%.c | build/native
	$(NATIVE_CC) $(NATIVE_CFLAGS) -Iinc -c -o $@ $<

build/native/kwg_data.o: build/nwl23-shadow/kwg_data.c | build/native
	$(NATIVE_CC) $(NATIVE_CFLAGS) -Iinc -c -o $@ $<

build/native/klv_data.o: build/nwl23-shadow/klv_data.c | build/native
	$(NATIVE_CC) $(NATIVE_CFLAGS) -Iinc -c -o $@ $<

build/native/test_native.o: test_native.c | build/native
	$(NATIVE_CC) $(NATIVE_CFLAGS) -Iinc -c -o $@ $<

test-native: nwl23-shadow build/native/test_native.o $(NATIVE_OBJECTS) build/native/kwg_data.o build/native/klv_data.o
	$(NATIVE_CC) $(NATIVE_LDFLAGS) -o build/native/test_native \
		build/native/test_native.o $(NATIVE_OBJECTS) \
		build/native/kwg_data.o build/native/klv_data.o
	@echo "Running native test with sanitizers..."
	./build/native/test_native 8

# Native test WITHOUT shadow
NATIVE_NOSHADOW_CFLAGS = -g -O0 -Wall -Wextra -fsanitize=address,undefined -DUSE_SHADOW=0
NATIVE_NOSHADOW_OBJECTS = $(patsubst src/%.c,build/native-noshadow/%.o,$(NATIVE_SOURCES))

build/native-noshadow:
	@mkdir -p $@

build/native-noshadow/%.o: src/%.c | build/native-noshadow
	$(NATIVE_CC) $(NATIVE_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/native-noshadow/kwg_data.o: build/nwl23-noshadow/kwg_data.c | build/native-noshadow
	$(NATIVE_CC) $(NATIVE_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/native-noshadow/klv_data.o: build/nwl23-noshadow/klv_data.c | build/native-noshadow
	$(NATIVE_CC) $(NATIVE_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/native-noshadow/test_native.o: test_native.c | build/native-noshadow
	$(NATIVE_CC) $(NATIVE_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

test-native-noshadow: nwl23-noshadow build/native-noshadow/test_native.o $(NATIVE_NOSHADOW_OBJECTS) build/native-noshadow/kwg_data.o build/native-noshadow/klv_data.o
	$(NATIVE_CC) $(NATIVE_LDFLAGS) -o build/native-noshadow/test_native \
		build/native-noshadow/test_native.o $(NATIVE_NOSHADOW_OBJECTS) \
		build/native-noshadow/kwg_data.o build/native-noshadow/klv_data.o
	@echo "Running native test (no shadow) with sanitizers..."
	./build/native-noshadow/test_native 8

# Optimized native test build (for timing)
NATIVE_OPT_CFLAGS = -O3 -DNDEBUG -DUSE_SHADOW=1

NATIVE_OPT_OBJECTS = $(patsubst src/%.c,build/native-opt/%.o,$(NATIVE_SOURCES))

build/native-opt:
	@mkdir -p $@

build/native-opt/%.o: src/%.c | build/native-opt
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/native-opt/kwg_data.o: build/nwl23-shadow/kwg_data.c | build/native-opt
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/native-opt/klv_data.o: build/nwl23-shadow/klv_data.c | build/native-opt
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/native-opt/test_native.o: test_native.c | build/native-opt
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

test-native-opt: nwl23-shadow build/native-opt/test_native.o $(NATIVE_OPT_OBJECTS) build/native-opt/kwg_data.o build/native-opt/klv_data.o
	$(NATIVE_CC) -o build/native-opt/test_native \
		build/native-opt/test_native.o $(NATIVE_OPT_OBJECTS) \
		build/native-opt/kwg_data.o build/native-opt/klv_data.o
	@echo "Running optimized native test..."
	./build/native-opt/test_native 100

# Optimized native test WITHOUT shadow (for timing comparison)
NATIVE_OPT_NOSHADOW_CFLAGS = -O3 -DNDEBUG -DUSE_SHADOW=0
NATIVE_OPT_NOSHADOW_OBJECTS = $(patsubst src/%.c,build/native-opt-noshadow/%.o,$(NATIVE_SOURCES))

build/native-opt-noshadow:
	@mkdir -p $@

build/native-opt-noshadow/%.o: src/%.c | build/native-opt-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/native-opt-noshadow/kwg_data.o: build/nwl23-noshadow/kwg_data.c | build/native-opt-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/native-opt-noshadow/klv_data.o: build/nwl23-noshadow/klv_data.c | build/native-opt-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/native-opt-noshadow/test_native.o: test_native.c | build/native-opt-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

test-native-opt-noshadow: nwl23-noshadow build/native-opt-noshadow/test_native.o $(NATIVE_OPT_NOSHADOW_OBJECTS) build/native-opt-noshadow/kwg_data.o build/native-opt-noshadow/klv_data.o
	$(NATIVE_CC) -o build/native-opt-noshadow/test_native \
		build/native-opt-noshadow/test_native.o $(NATIVE_OPT_NOSHADOW_OBJECTS) \
		build/native-opt-noshadow/kwg_data.o build/native-opt-noshadow/klv_data.o
	@echo "Running optimized native test (no shadow)..."
	./build/native-opt-noshadow/test_native 100

#
# Batch testing builds (for running many games)
#

# NWL23 shadow batch
build/batch-nwl23-shadow:
	@mkdir -p $@

build/batch-nwl23-shadow/%.o: src/%.c | build/batch-nwl23-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/batch-nwl23-shadow/test_batch.o: test_batch.c | build/batch-nwl23-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/batch-nwl23-shadow/kwg_data.o: build/nwl23-shadow/kwg_data.c | build/batch-nwl23-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/batch-nwl23-shadow/klv_data.o: build/nwl23-shadow/klv_data.c | build/batch-nwl23-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

BATCH_NWL23_SHADOW_OBJECTS = $(patsubst src/%.c,build/batch-nwl23-shadow/%.o,$(NATIVE_SOURCES))

build/batch-nwl23-shadow/test_batch: nwl23-shadow build/batch-nwl23-shadow/test_batch.o $(BATCH_NWL23_SHADOW_OBJECTS) build/batch-nwl23-shadow/kwg_data.o build/batch-nwl23-shadow/klv_data.o
	$(NATIVE_CC) -o $@ build/batch-nwl23-shadow/test_batch.o $(BATCH_NWL23_SHADOW_OBJECTS) \
		build/batch-nwl23-shadow/kwg_data.o build/batch-nwl23-shadow/klv_data.o

# NWL23 noshadow batch
build/batch-nwl23-noshadow:
	@mkdir -p $@

build/batch-nwl23-noshadow/%.o: src/%.c | build/batch-nwl23-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/batch-nwl23-noshadow/test_batch.o: test_batch.c | build/batch-nwl23-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/batch-nwl23-noshadow/kwg_data.o: build/nwl23-noshadow/kwg_data.c | build/batch-nwl23-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/batch-nwl23-noshadow/klv_data.o: build/nwl23-noshadow/klv_data.c | build/batch-nwl23-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

BATCH_NWL23_NOSHADOW_OBJECTS = $(patsubst src/%.c,build/batch-nwl23-noshadow/%.o,$(NATIVE_SOURCES))

build/batch-nwl23-noshadow/test_batch: nwl23-noshadow build/batch-nwl23-noshadow/test_batch.o $(BATCH_NWL23_NOSHADOW_OBJECTS) build/batch-nwl23-noshadow/kwg_data.o build/batch-nwl23-noshadow/klv_data.o
	$(NATIVE_CC) -o $@ build/batch-nwl23-noshadow/test_batch.o $(BATCH_NWL23_NOSHADOW_OBJECTS) \
		build/batch-nwl23-noshadow/kwg_data.o build/batch-nwl23-noshadow/klv_data.o

# CSW24 shadow batch
build/batch-csw24-shadow:
	@mkdir -p $@

build/batch-csw24-shadow/%.o: src/%.c | build/batch-csw24-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/batch-csw24-shadow/test_batch.o: test_batch.c | build/batch-csw24-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/batch-csw24-shadow/kwg_data.o: build/csw24-shadow/kwg_data.c | build/batch-csw24-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

build/batch-csw24-shadow/klv_data.o: build/csw24-shadow/klv_data.c | build/batch-csw24-shadow
	$(NATIVE_CC) $(NATIVE_OPT_CFLAGS) -Iinc -c -o $@ $<

BATCH_CSW24_SHADOW_OBJECTS = $(patsubst src/%.c,build/batch-csw24-shadow/%.o,$(NATIVE_SOURCES))

build/batch-csw24-shadow/test_batch: csw24-shadow build/batch-csw24-shadow/test_batch.o $(BATCH_CSW24_SHADOW_OBJECTS) build/batch-csw24-shadow/kwg_data.o build/batch-csw24-shadow/klv_data.o
	$(NATIVE_CC) -o $@ build/batch-csw24-shadow/test_batch.o $(BATCH_CSW24_SHADOW_OBJECTS) \
		build/batch-csw24-shadow/kwg_data.o build/batch-csw24-shadow/klv_data.o

# CSW24 noshadow batch
build/batch-csw24-noshadow:
	@mkdir -p $@

build/batch-csw24-noshadow/%.o: src/%.c | build/batch-csw24-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/batch-csw24-noshadow/test_batch.o: test_batch.c | build/batch-csw24-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/batch-csw24-noshadow/kwg_data.o: build/csw24-noshadow/kwg_data.c | build/batch-csw24-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

build/batch-csw24-noshadow/klv_data.o: build/csw24-noshadow/klv_data.c | build/batch-csw24-noshadow
	$(NATIVE_CC) $(NATIVE_OPT_NOSHADOW_CFLAGS) -Iinc -c -o $@ $<

BATCH_CSW24_NOSHADOW_OBJECTS = $(patsubst src/%.c,build/batch-csw24-noshadow/%.o,$(NATIVE_SOURCES))

build/batch-csw24-noshadow/test_batch: csw24-noshadow build/batch-csw24-noshadow/test_batch.o $(BATCH_CSW24_NOSHADOW_OBJECTS) build/batch-csw24-noshadow/kwg_data.o build/batch-csw24-noshadow/klv_data.o
	$(NATIVE_CC) -o $@ build/batch-csw24-noshadow/test_batch.o $(BATCH_CSW24_NOSHADOW_OBJECTS) \
		build/batch-csw24-noshadow/kwg_data.o build/batch-csw24-noshadow/klv_data.o

# Build all batch test binaries
batch-builds: build/batch-nwl23-shadow/test_batch build/batch-nwl23-noshadow/test_batch \
              build/batch-csw24-shadow/test_batch build/batch-csw24-noshadow/test_batch
