#!/usr/bin/env python3
"""
Convert KLV16 leave values file to C array for embedding in ROM.

The KLV16 format contains:
  - KWG size (uint32_t LE)
  - KWG nodes (uint32_t[] LE)
  - Number of leaves (uint32_t LE)
  - Leave values (int16_t[] LE) - in eighths of a point

We output the raw bytes as a uint8_t array for the klv_init function.
"""

import sys
import os

def convert_klv16(input_file, output_file):
    """Convert KLV16 binary to C source file."""

    with open(input_file, 'rb') as f:
        data = f.read()

    print(f"Converting {input_file}: {len(data)} bytes")

    with open(output_file, 'w') as f:
        f.write("/* Auto-generated KLV16 leave values data */\n")
        f.write("/* Source: %s */\n" % os.path.basename(input_file))
        f.write("/* Size: %d bytes */\n\n" % len(data))
        f.write("#include <stdint.h>\n\n")
        f.write("const uint8_t klv_data[] = {\n")

        # Output bytes, 16 per line
        for i in range(0, len(data), 16):
            f.write("    ")
            chunk = data[i:i+16]
            hex_bytes = ", ".join("0x%02X" % b for b in chunk)
            f.write(hex_bytes)
            if i + 16 < len(data):
                f.write(",")
            f.write("\n")

        f.write("};\n\n")
        f.write("const unsigned int klv_data_size = %d;\n" % len(data))

    print(f"Output written to {output_file}")

def main():
    if len(sys.argv) < 2:
        print("Usage: klv2c.py <input.klv16> [output.c]")
        print("Converts KLV16 leave values file to C source for embedding in ROM")
        sys.exit(1)

    input_file = sys.argv[1]
    if len(sys.argv) >= 3:
        output_file = sys.argv[2]
    else:
        output_file = os.path.splitext(input_file)[0] + "_data.c"

    convert_klv16(input_file, output_file)

if __name__ == "__main__":
    main()
