#!/usr/bin/env python3
"""
Convert Magpie .klv2 files to .klv16 format for Sega Genesis.

KLV2 format (Magpie):
  - KWG size (uint32_t LE)
  - KWG nodes (uint32_t[] LE)
  - Number of leaves (uint32_t LE)
  - Leave values (float[] LE) - in points

KLV16 format (Genesis):
  - KWG size (uint32_t LE)
  - KWG nodes (uint32_t[] LE)
  - Number of leaves (uint32_t LE)
  - Leave values (int16_t[] LE) - in eighths of a point
"""

import struct
import sys
import os

def convert_klv2_to_klv16(input_path, output_path):
    """Convert a .klv2 file to .klv16 format."""

    with open(input_path, 'rb') as f:
        data = f.read()

    offset = 0

    # Read KWG size
    kwg_size = struct.unpack_from('<I', data, offset)[0]
    offset += 4
    print(f"KWG size: {kwg_size} nodes")

    # Read KWG nodes
    kwg_nodes = struct.unpack_from(f'<{kwg_size}I', data, offset)
    offset += kwg_size * 4
    print(f"KWG data: {kwg_size * 4} bytes")

    # Read number of leaves
    num_leaves = struct.unpack_from('<I', data, offset)[0]
    offset += 4
    print(f"Number of leaves: {num_leaves}")

    # Read leave values as floats
    leave_values_float = struct.unpack_from(f'<{num_leaves}f', data, offset)
    offset += num_leaves * 4

    # Convert to eighths of a point (int16)
    leave_values_int16 = []
    clipped_count = 0
    min_val = float('inf')
    max_val = float('-inf')

    for val in leave_values_float:
        min_val = min(min_val, val)
        max_val = max(max_val, val)

        # Convert to eighths of a point
        val_eighths = round(val * 8)

        # Clip to int16 range
        if val_eighths > 32767:
            val_eighths = 32767
            clipped_count += 1
        elif val_eighths < -32768:
            val_eighths = -32768
            clipped_count += 1

        leave_values_int16.append(val_eighths)

    print(f"Leave value range: {min_val:.3f} to {max_val:.3f} points")
    print(f"In eighths: {round(min_val * 8)} to {round(max_val * 8)}")
    if clipped_count > 0:
        print(f"WARNING: {clipped_count} values clipped to int16 range")

    # Write KLV16 file
    with open(output_path, 'wb') as f:
        # KWG size
        f.write(struct.pack('<I', kwg_size))

        # KWG nodes
        f.write(struct.pack(f'<{kwg_size}I', *kwg_nodes))

        # Number of leaves
        f.write(struct.pack('<I', num_leaves))

        # Leave values as int16
        f.write(struct.pack(f'<{num_leaves}h', *leave_values_int16))

    input_size = os.path.getsize(input_path)
    output_size = os.path.getsize(output_path)
    savings = input_size - output_size

    print(f"\nInput size:  {input_size:,} bytes")
    print(f"Output size: {output_size:,} bytes")
    print(f"Savings:     {savings:,} bytes ({100*savings/input_size:.1f}%)")
    print(f"\nWrote {output_path}")

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.klv2> <output.klv16>")
        print(f"       {sys.argv[0]} /path/to/NWL23.klv2 /path/to/NWL23.klv16")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    if not os.path.exists(input_path):
        print(f"Error: Input file not found: {input_path}")
        sys.exit(1)

    convert_klv2_to_klv16(input_path, output_path)

if __name__ == '__main__':
    main()
