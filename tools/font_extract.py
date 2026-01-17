#!/usr/bin/env python3
"""
Extract bitmap font data from OTB/BDF files and generate C code for Sega Genesis.
Also generates JSON for the font browser.
"""

import sys
import json
import argparse
import re
from pathlib import Path

# Use venv if available
venv_path = Path(__file__).parent.parent / '.venv' / 'lib'
if venv_path.exists():
    import site
    for p in venv_path.glob('python*/site-packages'):
        site.addsitedir(str(p))


def extract_bdf_bitmaps(font_path):
    """Extract glyph bitmaps from a BDF font file."""
    with open(font_path, 'r') as f:
        content = f.read()

    # Get font bounding box for default dimensions
    bbox_match = re.search(r'FONTBOUNDINGBOX\s+(\d+)\s+(\d+)', content)
    if bbox_match:
        font_width = int(bbox_match.group(1))
        font_height = int(bbox_match.group(2))
    else:
        font_width = 8
        font_height = 8

    glyphs = {}

    # Parse each character
    char_pattern = re.compile(
        r'STARTCHAR\s+\S+\n'
        r'ENCODING\s+(\d+)\n'
        r'(?:.*?\n)*?'
        r'BBX\s+(\d+)\s+(\d+)\s+(-?\d+)\s+(-?\d+)\n'
        r'(?:.*?\n)*?'
        r'BITMAP\n([\dA-Fa-f\n]+?)ENDCHAR',
        re.MULTILINE
    )

    for match in char_pattern.finditer(content):
        encoding = int(match.group(1))
        bbx_width = int(match.group(2))
        bbx_height = int(match.group(3))
        bbx_offset_x = int(match.group(4))
        bbx_offset_y = int(match.group(5))
        bitmap_hex = match.group(6).strip().split('\n')

        if encoding < 32 or encoding > 126:
            continue

        # Parse bitmap rows (hex values)
        bitmap = []
        for hex_row in bitmap_hex:
            hex_row = hex_row.strip()
            if hex_row:
                row_val = int(hex_row, 16)
                # Shift to align with MSB based on width
                # BDF stores with MSB at leftmost pixel
                bitmap.append(row_val)

        glyphs[encoding] = {
            'width': bbx_width,
            'height': bbx_height,
            'offset_x': bbx_offset_x,
            'offset_y': bbx_offset_y,
            'bitmap': bitmap
        }

    # Find the max dimensions used
    max_width = max((g['width'] for g in glyphs.values()), default=font_width)
    max_height = max((g['height'] for g in glyphs.values()), default=font_height)

    return {
        'width': max_width,
        'height': max_height,
        'font_width': font_width,
        'font_height': font_height,
        'glyphs': glyphs
    }


def extract_otb_bitmaps(font_path):
    """Extract glyph bitmaps from an OTB font file."""
    from fontTools.ttLib import TTFont

    try:
        font = TTFont(font_path)
    except Exception as e:
        return None

    cmap = font.getBestCmap()
    if not cmap:
        font.close()
        return None

    if 'EBDT' not in font or 'EBLC' not in font:
        font.close()
        return None

    ebdt = font['EBDT']
    eblc = font['EBLC']

    strike = eblc.strikes[0]
    metrics = None
    for subtable in strike.indexSubTables:
        if hasattr(subtable, 'metrics'):
            metrics = subtable.metrics
            break

    if not metrics:
        bst = strike.bitmapSizeTable
        width = bst.hori.widthMax if hasattr(bst, 'hori') else 8
        height = bst.ppemY
    else:
        width = metrics.width
        height = metrics.height

    glyphs = {}

    for char_code in range(32, 127):
        if char_code not in cmap:
            continue

        glyph_name = cmap[char_code]
        glyph_data = ebdt.strikeData[0].get(glyph_name)

        if not glyph_data or not hasattr(glyph_data, 'data'):
            continue

        data = glyph_data.data

        bits = []
        for byte in data:
            for i in range(7, -1, -1):
                bits.append((byte >> i) & 1)

        bitmap = []
        for row in range(height):
            start = row * width
            row_bits = bits[start:start + width] if start + width <= len(bits) else [0] * width
            byte_val = 0
            for col, bit in enumerate(row_bits[:8]):
                if bit:
                    byte_val |= (0x80 >> col)
            bitmap.append(byte_val)

        glyphs[char_code] = {
            'width': width,
            'height': height,
            'bitmap': bitmap
        }

    font.close()

    return {
        'width': width,
        'height': height,
        'glyphs': glyphs
    }


def extract_font_bitmaps(font_path):
    """Extract bitmaps from either BDF or OTB font."""
    font_path = Path(font_path)
    if font_path.suffix.lower() == '.bdf':
        return extract_bdf_bitmaps(font_path)
    else:
        return extract_otb_bitmaps(font_path)


def generate_c_code(font_data, font_name, output_path, target_height=8):
    """Generate C code for Sega Genesis from extracted font data."""
    width = font_data['width']
    height = font_data.get('font_height', font_data['height'])
    glyphs = font_data['glyphs']

    # Use target height for output (pad or crop as needed)
    out_height = target_height

    lines = [
        f"/* Font: {font_name} ({width}x{height}, padded to {width}x{out_height}) */",
        f"/* Auto-generated from bitmap font */",
        "",
        f"#define FONT_{font_name.upper()}_WIDTH {width}",
        f"#define FONT_{font_name.upper()}_HEIGHT {out_height}",
        "",
        f"/* Each glyph is {out_height} bytes, one byte per row, MSB is leftmost pixel */",
        f"static const unsigned char font_{font_name.lower()}_data[96][{out_height}] = {{",
    ]

    for char_code in range(32, 128):
        char_repr = repr(chr(char_code)) if 32 <= char_code < 127 else f"({char_code})"
        if char_code in glyphs:
            glyph = glyphs[char_code]
            bitmap = glyph['bitmap']
            glyph_height = glyph.get('height', len(bitmap))

            row_bytes = []
            # Center vertically in target height
            top_pad = (out_height - glyph_height) // 2
            for row in range(out_height):
                src_row = row - top_pad
                if 0 <= src_row < len(bitmap):
                    byte_val = bitmap[src_row]
                    # BDF stores with MSB as leftmost pixel, already correctly aligned
                    # For wider glyphs (>8 pixels), take the leftmost 8 bits
                    if glyph['width'] > 8:
                        byte_val = (byte_val >> (glyph['width'] - 8)) & 0xFF
                    else:
                        byte_val = byte_val & 0xFF
                    row_bytes.append(f"0x{byte_val:02X}")
                else:
                    row_bytes.append("0x00")
            lines.append(f"    /* {char_repr} */ {{{', '.join(row_bytes)}}},")
        else:
            empty = ', '.join(['0x00'] * out_height)
            lines.append(f"    /* {char_repr} */ {{{empty}}},")

    lines.append("};")

    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))

    print(f"Generated {output_path}")


def generate_json(font_data, font_name, output_path):
    """Generate JSON for the font browser."""
    output = {
        'name': font_name,
        'width': font_data['width'],
        'height': font_data.get('font_height', font_data['height']),
        'glyphs': {}
    }

    for char_code, glyph in font_data['glyphs'].items():
        bitmap = glyph['bitmap']
        # BDF stores with MSB as leftmost pixel, already correctly aligned
        hex_rows = []
        for row_val in bitmap:
            if glyph['width'] > 8:
                row_val = (row_val >> (glyph['width'] - 8)) & 0xFF
            else:
                row_val = row_val & 0xFF
            hex_rows.append(row_val)
        output['glyphs'][str(char_code)] = hex_rows

    with open(output_path, 'w') as f:
        json.dump(output, f)

    print(f"Generated {output_path}")


def main():
    parser = argparse.ArgumentParser(description='Extract bitmap font data')
    parser.add_argument('font_path', help='Path to OTB or BDF font file')
    parser.add_argument('-c', '--c-output', help='Output C file path')
    parser.add_argument('-j', '--json-output', help='Output JSON file path')
    parser.add_argument('-n', '--name', help='Font name for generated code')
    parser.add_argument('-t', '--target-height', type=int, default=8,
                        help='Target height for C output (default: 8)')
    args = parser.parse_args()

    font_path = Path(args.font_path)
    font_name = args.name or font_path.stem.replace('-', '_').replace('.', '_')

    print(f"Extracting bitmaps from {font_path}...")
    font_data = extract_font_bitmaps(font_path)

    if not font_data or not font_data['glyphs']:
        print("Error: No bitmap glyphs found in font")
        sys.exit(1)

    print(f"Found {len(font_data['glyphs'])} glyphs, size {font_data['width']}x{font_data.get('font_height', font_data['height'])}")

    if args.c_output:
        generate_c_code(font_data, font_name, args.c_output, args.target_height)

    if args.json_output:
        generate_json(font_data, font_name, args.json_output)

    if not args.c_output and not args.json_output:
        print("\nSample glyphs:")
        for char_code in [65, 66, 67, 83]:  # A, B, C, S
            if char_code in font_data['glyphs']:
                glyph = font_data['glyphs'][char_code]
                print(f"\n{chr(char_code)} ({glyph['width']}x{glyph['height']}):")
                for row_val in glyph['bitmap']:
                    width = glyph['width']
                    bits = format(row_val, f'0{max(8, width)}b')[-max(8, width):]
                    print(''.join(['#' if b == '1' else '.' for b in bits[:width]]))


if __name__ == '__main__':
    main()
