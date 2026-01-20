"""
Bazel rules for generating disassembly explorer sites.

Usage in BUILD.bazel:

    load("//tools:disassembly.bzl", "disassembly_site")

    disassembly_site(
        name = "movegen_disasm",
        binary = "build/nwl23-shadow/scrabble.elf",
        srcs = glob(["src/*.c", "src/*.s", "inc/*.h"]),
    )

Then run:
    bazel build //:movegen_disasm

The output will be a directory containing:
    - index.html (main navigation page)
    - <filename>.html for each source file

IMPORTANT: The binary must be compiled with debug symbols (-g) for mixed
           C/assembly listings to work. Without debug info, objdump cannot
           interleave source code with assembly.
"""

def _disassembly_site_impl(ctx):
    """Implementation of disassembly_site rule."""

    # Declare output directory (TreeArtifact)
    output_dir = ctx.actions.declare_directory(ctx.label.name)

    # Get the binary file
    binary = ctx.file.binary

    # Get the objdump tool path
    objdump = ctx.attr.objdump

    # Get the splitter script
    splitter = ctx.file._splitter

    # Collect all source files
    srcs = ctx.files.srcs

    # Create inputs list: binary + all source files + splitter script
    inputs = [binary, splitter] + srcs

    # Build the command
    # 1. Run objdump to generate the raw listing
    # 2. Run the Python splitter to generate HTML files
    #
    # We use a shell command to chain these together and handle the
    # intermediate .lst file.

    # Build source paths for DWARF lookup
    # objdump needs the source files at the paths recorded in the DWARF info.
    # We create symlinks in a temp directory to match the expected paths.
    src_setup_cmds = []
    for src in srcs:
        # Get the path as it appears in the workspace
        src_path = src.path
        # Create parent directories and symlink
        src_setup_cmds.append(
            "mkdir -p $(dirname {path}) && ln -sf $(pwd)/{src} {path}".format(
                path = src_path,
                src = src.path,
            )
        )

    # Join setup commands
    src_setup = " && ".join(src_setup_cmds) if src_setup_cmds else "true"

    # Main command
    cmd = """
set -e

# Create a working directory for the raw dump
WORK_DIR=$(mktemp -d)
LST_FILE="$WORK_DIR/dump.lst"

# Run objdump with mixed source/assembly output
# -d: disassemble
# -S: intermix source code (requires debug symbols)
# -l: display line numbers
{objdump} -d -S -l {binary} > "$LST_FILE" 2>/dev/null || {{
    echo "Warning: objdump failed or binary lacks debug symbols" >&2
    {objdump} -d {binary} > "$LST_FILE"
}}

# Run the splitter script to generate HTML
python3 {splitter} "$LST_FILE" {output_dir}

# Cleanup
rm -rf "$WORK_DIR"
""".format(
        objdump = objdump,
        binary = binary.path,
        splitter = splitter.path,
        output_dir = output_dir.path,
    )

    ctx.actions.run_shell(
        inputs = inputs,
        outputs = [output_dir],
        command = cmd,
        mnemonic = "DisassemblySite",
        progress_message = "Generating disassembly site for %s" % binary.short_path,
        use_default_shell_env = True,  # Needed for python3
    )

    return [DefaultInfo(files = depset([output_dir]))]


disassembly_site = rule(
    implementation = _disassembly_site_impl,
    attrs = {
        "binary": attr.label(
            allow_single_file = [".elf"],
            mandatory = True,
            doc = "The ELF binary to disassemble. Must be compiled with -g for mixed listings.",
        ),
        "srcs": attr.label_list(
            allow_files = True,
            doc = "Source files to include for mixed C/assembly output.",
        ),
        "objdump": attr.string(
            default = "m68k-elf-objdump",
            doc = "Path to the objdump binary (default: m68k-elf-objdump).",
        ),
        "_splitter": attr.label(
            default = "//tools:split_asm.py",
            allow_single_file = True,
            doc = "The Python script that splits objdump output.",
        ),
    },
    doc = """
Generates a browsable disassembly site from an ELF binary.

The output is a directory containing HTML files that can be served
as a static website or browsed locally.

Example:
    disassembly_site(
        name = "my_disasm",
        binary = ":my_binary.elf",
        srcs = glob(["src/*.c", "inc/*.h"]),
    )
""",
)


def disassembly_sites(name, variants, srcs, objdump = "m68k-elf-objdump"):
    """
    Convenience macro to generate disassembly sites for multiple build variants.

    Args:
        name: Base name for the targets
        variants: Dict mapping variant name to ELF path
        srcs: Source files to include
        objdump: Path to objdump (default: m68k-elf-objdump)

    Example:
        disassembly_sites(
            name = "disasm",
            variants = {
                "nwl23-shadow": "build/nwl23-shadow/scrabble.elf",
                "csw24-noshadow": "build/csw24-noshadow/scrabble.elf",
            },
            srcs = glob(["src/*.c", "inc/*.h"]),
        )

    This creates:
        - //:disasm_nwl23-shadow
        - //:disasm_csw24-noshadow
    """
    for variant, binary_path in variants.items():
        disassembly_site(
            name = "{}_{}".format(name, variant),
            binary = binary_path,
            srcs = srcs,
            objdump = objdump,
        )
