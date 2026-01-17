#!/usr/bin/env python3
"""
Verify KLV16 conversion by comparing against original KLV2 file.
"""

import struct
import sys

def load_klv2(path):
    """Load original KLV2 format (float values in points)"""
    with open(path, 'rb') as f:
        data = f.read()

    offset = 0
    kwg_size = struct.unpack_from('<I', data, offset)[0]
    offset += 4 + kwg_size * 4

    num_leaves = struct.unpack_from('<I', data, offset)[0]
    offset += 4

    leaves = struct.unpack_from(f'<{num_leaves}f', data, offset)
    return kwg_size, list(leaves)

def load_klv16(path):
    """Load KLV16 format (int16 values in eighths)"""
    with open(path, 'rb') as f:
        data = f.read()

    offset = 0
    kwg_size = struct.unpack_from('<I', data, offset)[0]
    offset += 4 + kwg_size * 4

    num_leaves = struct.unpack_from('<I', data, offset)[0]
    offset += 4

    leaves = struct.unpack_from(f'<{num_leaves}h', data, offset)
    return kwg_size, list(leaves)

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <original.klv2> <converted.klv16>")
        sys.exit(1)

    klv2_path = sys.argv[1]
    klv16_path = sys.argv[2]

    print(f"Loading {klv2_path}...")
    kwg_size2, leaves2 = load_klv2(klv2_path)

    print(f"Loading {klv16_path}...")
    kwg_size16, leaves16 = load_klv16(klv16_path)

    # Verify KWG size matches
    if kwg_size2 != kwg_size16:
        print(f"ERROR: KWG size mismatch: {kwg_size2} vs {kwg_size16}")
        sys.exit(1)
    print(f"KWG size: {kwg_size2} nodes (match)")

    # Verify leave count matches
    if len(leaves2) != len(leaves16):
        print(f"ERROR: Leave count mismatch: {len(leaves2)} vs {len(leaves16)}")
        sys.exit(1)
    print(f"Leave count: {len(leaves2)} (match)")

    # Check conversion accuracy
    max_error = 0
    error_count = 0
    total_error = 0

    for i, (orig, conv) in enumerate(zip(leaves2, leaves16)):
        expected = round(orig * 8)  # Convert to eighths

        # Clip to int16 range
        if expected > 32767:
            expected = 32767
        elif expected < -32768:
            expected = -32768

        error = abs(conv - expected)
        if error != 0:
            error_count += 1
            total_error += error
            if error > max_error:
                max_error = error
                print(f"  Index {i}: orig={orig:.6f}, expected={expected}, got={conv}, error={error}")

    print(f"\nConversion verification:")
    print(f"  Total leaves: {len(leaves2)}")
    print(f"  Errors: {error_count}")
    print(f"  Max error: {max_error} eighths ({max_error/8:.3f} points)")
    if error_count > 0:
        print(f"  Avg error: {total_error/error_count:.3f} eighths")

    # Sample comparisons
    print("\nSample leave values (original vs converted):")
    samples = [0, 1, 100, 1000, 10000, 100000, len(leaves2)-1]
    for i in samples:
        if i < len(leaves2):
            orig_pts = leaves2[i]
            conv_eighths = leaves16[i]
            conv_pts = conv_eighths / 8.0
            print(f"  [{i}] orig={orig_pts:+.3f} pts, conv={conv_pts:+.3f} pts (diff={abs(orig_pts-conv_pts):.3f})")

    if error_count == 0:
        print("\nAll values converted correctly!")
    else:
        print(f"\nWARNING: {error_count} conversion errors found")

if __name__ == '__main__':
    main()
