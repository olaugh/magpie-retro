"""Bazel rules for building Sega Genesis ROMs with m68k-elf toolchain."""

def _kwg_data(name, kwg_file):
    """Generate kwg_data.c from a KWG lexicon file."""
    native.genrule(
        name = name,
        srcs = [kwg_file],
        outs = [name + ".c"],
        cmd = "$(location //tools:kwg2c) $(SRCS) $@",
        tools = ["//tools:kwg2c"],
    )

def _klv_data(name, klv_file):
    """Generate klv_data.c from a KLV16 leave values file."""
    native.genrule(
        name = name,
        srcs = [klv_file],
        outs = [name + ".c"],
        cmd = "$(location //tools:klv2c) $(SRCS) $@",
        tools = ["//tools:klv2c"],
    )

def _compile_c(name, src, hdrs, copts):
    """Compile a C source file to object file."""
    native.genrule(
        name = name,
        srcs = [src] + hdrs,
        outs = [name + ".o"],
        cmd = """
            m68k-elf-gcc -m68000 -Wall -O2 -fno-builtin -fshort-enums \
                -nostdlib -ffreestanding -fomit-frame-pointer \
                -I$$(dirname $(location //inc:scrabble.h)) \
                {copts} \
                -c $(location {src}) -o $@
        """.format(src = src, copts = " ".join(copts)),
    )

def _compile_genrule_c(name, src_rule, hdrs, copts):
    """Compile a generated C source file to object file."""
    native.genrule(
        name = name,
        srcs = [src_rule] + hdrs,
        outs = [name + ".o"],
        cmd = """
            m68k-elf-gcc -m68000 -Wall -O2 -fno-builtin -fshort-enums \
                -nostdlib -ffreestanding -fomit-frame-pointer \
                -I$$(dirname $(location //inc:scrabble.h)) \
                {copts} \
                -c $(location {src_rule}) -o $@
        """.format(src_rule = src_rule, copts = " ".join(copts)),
    )

def _assemble(name, src, output_name = None):
    """Assemble a .s source file to object file."""
    out_file = (output_name if output_name else name) + ".o"
    native.genrule(
        name = name,
        srcs = [src],
        outs = [out_file],
        cmd = "m68k-elf-as -m68000 $(SRCS) -o $@",
    )

def _link_elf(name, objs, linker_script):
    """Link object files into an ELF executable."""
    # We need to find libgcc at build time
    native.genrule(
        name = name,
        srcs = objs + [linker_script],
        outs = [name + ".elf"],
        cmd = """
            LIBGCC=$$(m68k-elf-gcc -print-libgcc-file-name) && \
            m68k-elf-ld -T $(location {linker_script}) -nostdlib \
                -o $@ {obj_locs} $$LIBGCC
        """.format(
            linker_script = linker_script,
            obj_locs = " ".join(["$(location " + obj + ")" for obj in objs]),
        ),
    )

def _elf_to_bin(name, elf, min_size = 131072):
    """Convert ELF to binary ROM, padded to minimum size."""
    native.genrule(
        name = name,
        srcs = [elf],
        outs = [name + ".bin"],
        cmd = """
            m68k-elf-objcopy -O binary $(SRCS) $@ && \
            SIZE=$$(wc -c < $@ | tr -d ' ') && \
            if [ $$SIZE -lt {min_size} ]; then \
                dd if=/dev/zero bs=1 count=$$(({min_size} - $$SIZE)) >> $@ 2>/dev/null; \
            fi
        """.format(min_size = min_size),
    )

def genesis_rom(
        name,
        lexicon,
        kwg_file,
        klv_file,
        use_shadow = True,
        use_hybrid = False,
        collect_move_stats = False,
        debug = False):
    """
    Build a Sega Genesis Scrabble ROM.

    Args:
        name: Base name for the ROM (e.g., "nwl23-shadow")
        lexicon: Lexicon name string (e.g., "NWL23")
        kwg_file: Path to the KWG lexicon file
        klv_file: Path to the KLV16 leave values file
        use_shadow: Enable shadow algorithm (default True)
        use_hybrid: Enable hybrid mode (default False)
        collect_move_stats: Enable timing instrumentation (default False)
        debug: Build with debug symbols (default False)
    """

    # Variant-specific compiler flags
    copts = [
        "-DLEXICON_NAME='\"{}\"'".format(lexicon),
        "-DUSE_SHADOW={}".format(1 if use_shadow else 0),
    ]
    if use_hybrid:
        copts.append("-DUSE_HYBRID=1")
    if collect_move_stats:
        copts.append("-DCOLLECT_MOVE_STATS=1")
    if debug:
        copts.append("-g")

    # Header files
    hdrs = [
        "//inc:scrabble.h",
        "//inc:kwg.h",
        "//inc:klv.h",
        "//inc:anchor.h",
        "//inc:equity.h",
        "//inc:bit_tables.h",
    ]

    # Generate data files
    kwg_data_name = name + "_kwg_data"
    klv_data_name = name + "_klv_data"
    _kwg_data(kwg_data_name, kwg_file)
    _klv_data(klv_data_name, klv_file)

    # C source files (alphabetical order to match Make build)
    c_sources = [
        "//src:bit_tables.c",
        "//src:board.c",
        "//src:font_4x6.c",
        "//src:font_5x7.c",
        "//src:font_6x10.c",
        "//src:font_6x9.c",
        "//src:font_everex_5x8.c",
        "//src:font_five_pixel.c",
        "//src:font_tom_thumb.c",
        "//src:game.c",
        "//src:graphics.c",
        "//src:klv.c",
        "//src:kwg.c",
        "//src:libc.c",
        "//src:main.c",
        "//src:movegen.c",
    ]

    # Compile assembly (boot.s)
    boot_obj = name + "_boot"
    _assemble(boot_obj, "//src:boot.s")

    # Compile C sources
    c_objs = []
    for src in c_sources:
        # Extract filename without path and extension
        src_name = src.split(":")[-1].replace(".c", "")
        obj_name = name + "_" + src_name
        _compile_c(obj_name, src, hdrs, copts)
        c_objs.append(":" + obj_name)

    # Compile generated data files
    kwg_obj = name + "_kwg_data_obj"
    klv_obj = name + "_klv_data_obj"
    _compile_genrule_c(kwg_obj, ":" + kwg_data_name, hdrs, copts)
    _compile_genrule_c(klv_obj, ":" + klv_data_name, hdrs, copts)

    # All object files (boot must be first for linker script)
    all_objs = [":" + boot_obj] + c_objs + [":" + kwg_obj, ":" + klv_obj]

    # Link ELF
    elf_name = name + "_elf"
    linker_script = "//:linker_debug.ld" if debug else "//:linker.ld"
    _link_elf(elf_name, all_objs, linker_script)

    # Convert to binary ROM
    _elf_to_bin(name, ":" + elf_name)

    # Create filegroup for the final ROM
    native.filegroup(
        name = name + "_rom",
        srcs = [":" + name],
        visibility = ["//visibility:public"],
    )

    # Also expose the ELF for debugging/disassembly
    native.filegroup(
        name = name + "_elf_out",
        srcs = [":" + elf_name],
        visibility = ["//visibility:public"],
    )
