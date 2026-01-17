#!/usr/bin/env python3
"""
Convert KWG lexicon file to C array for embedding in ROM.

The KWG format stores nodes as little-endian 32-bit integers.
We convert to big-endian for the 68000 CPU.
"""

import sys
import struct
import os

def convert_kwg(input_file, output_file):
    """Convert KWG binary to C source file."""

    with open(input_file, 'rb') as f:
        data = f.read()

    num_nodes = len(data) // 4
    print(f"Converting {input_file}: {num_nodes} nodes ({len(data)} bytes)")

    with open(output_file, 'w') as f:
        f.write("/* Auto-generated KWG lexicon data */\n")
        f.write("/* Source: %s */\n" % os.path.basename(input_file))
        f.write("/* Nodes: %d, Size: %d bytes */\n\n" % (num_nodes, len(data)))
        f.write("#include <stdint.h>\n\n")
        f.write("const uint32_t kwg_data[] = {\n")

        # Process 4 bytes at a time
        for i in range(0, len(data), 4):
            if i + 4 > len(data):
                break

            # Read as little-endian, we'll let the code handle endianness
            # The 68000 is big-endian but we store as-is and convert at runtime
            value = struct.unpack('<I', data[i:i+4])[0]

            if i % 32 == 0:
                f.write("    ")

            f.write("0x%08X," % value)

            if (i + 4) % 32 == 0:
                f.write("\n")
            else:
                f.write(" ")

        f.write("\n};\n\n")
        f.write("const unsigned int kwg_data_size = %d;\n" % num_nodes)

    print(f"Output written to {output_file}")

def main():
    if len(sys.argv) < 2:
        print("Usage: kwg2c.py <input.kwg> [output.c]")
        print("Converts KWG lexicon file to C source for embedding in ROM")
        sys.exit(1)

    input_file = sys.argv[1]
    if len(sys.argv) >= 3:
        output_file = sys.argv[2]
    else:
        output_file = os.path.splitext(input_file)[0] + "_data.c"

    convert_kwg(input_file, output_file)

if __name__ == "__main__":
    main()
