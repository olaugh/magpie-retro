#!/usr/bin/env python3
"""
Extract glyphs from the five-pixel-font compressed data and generate C code.
The font uses 5x5 glyphs in 6x6 cells (64x64 texture atlas).
"""

# Compressed font data from five_pixel_font.h
fpf_compressed_font = bytes([
    0, 1, 133, 20, 123, 34, 8, 16, 128, 0, 1, 133, 62, 163, 69, 16, 32, 64,
    0, 1, 128, 20, 112, 130, 0, 1, 32, 64, 0, 2, 62, 41, 101, 0, 1, 32, 64,
    0, 1, 128, 20, 242, 98, 128, 16, 128, 0, 8, 168, 0, 3, 39, 8, 49, 128,
    112, 128, 0, 2, 73, 152, 72, 64, 249, 192, 28, 0, 1, 138, 136, 16, 128,
    112, 130, 0, 1, 1, 12, 136, 32, 64, 168, 4, 0, 1, 34, 7, 28, 121, 128,
    0, 8, 49, 194, 30, 97, 128, 0, 1, 16, 0, 1, 81, 4, 2, 146, 66, 8, 33,
    192, 121, 142, 4, 97, 192, 0, 1, 64, 0, 1, 16, 73, 8, 144, 66, 8, 33,
    192, 17, 134, 8, 96, 64, 16, 16, 0, 9, 65, 135, 28, 241, 239, 62, 249,
    224, 32, 64, 162, 138, 8, 160, 130, 0, 1, 16, 134, 190, 242, 8, 188,
    242, 96, 32, 10, 162, 138, 8, 160, 130, 32, 64, 135, 34, 241, 239, 62,
    129, 224, 0, 8, 137, 195, 164, 130, 40, 156, 241, 192, 136, 129, 40,
    131, 108, 162, 138, 32, 248, 129, 48, 130, 170, 162, 242, 32, 136, 137,
    40, 130, 41, 162, 130, 64, 137, 198, 36, 242, 40, 156, 129, 160, 0, 8,
    241, 239, 162, 138, 40, 162, 113, 192, 138, 2, 34, 138, 37, 20, 17, 0,
    1, 241, 194, 34, 138, 162, 8, 33, 0, 1, 160, 34, 34, 82, 165, 8, 65, 0,
    1, 147, 194, 28, 33, 72, 136, 113, 192, 0, 8, 129, 194, 0, 1, 32, 8, 0,
    1, 16, 0, 1, 64, 69, 0, 1, 17, 206, 12, 16, 128, 32, 64, 0, 1, 2, 73,
    16, 113, 64, 16, 64, 0, 1, 2, 73, 16, 145, 128, 9, 192, 62, 1, 174, 12,
    112, 192, 0, 8, 16, 196, 8, 17, 2, 0, 3, 33, 68, 0, 1, 1, 2, 52, 96,
    128, 112, 199, 8, 17, 66, 42, 81, 64, 32, 68, 136, 81, 130, 34, 81, 64,
    33, 132, 136, 33, 66, 34, 80, 128, 0, 16, 96, 197, 12, 113, 37, 34, 81,
    64, 81, 70, 8, 33, 37, 42, 33, 64, 96, 196, 4, 33, 37, 42, 32, 128, 64,
    68, 12, 16, 194, 20, 81, 0, 10, 66, 16, 3, 224, 63, 128, 0, 1, 48, 130,
    8, 82, 32, 63, 128, 0, 1, 17, 128, 12, 162, 32, 63, 128, 0, 1, 32, 130,
    8, 2, 32, 63, 128, 0, 1, 48, 66, 16, 3, 239, 255, 128, 0, 5, 15, 255,
    128
])

TEXTURE_WIDTH = 64
TEXTURE_HEIGHT = 64
GLYPH_WIDTH = 6
GLYPH_HEIGHT = 6

def decompress_font():
    """Decompress the font data into a 64x64 bitmap."""
    texture = bytearray(TEXTURE_WIDTH * TEXTURE_HEIGHT)

    byte_index = 0
    pixel_index = 0

    while byte_index < len(fpf_compressed_font) and pixel_index < len(texture):
        byte = fpf_compressed_font[byte_index]
        byte_index += 1

        if byte == 0:
            # Run of zeros - next byte * 8 pixels
            if byte_index < len(fpf_compressed_font):
                run_length = fpf_compressed_font[byte_index] * 8
                byte_index += 1
                for _ in range(run_length):
                    if pixel_index < len(texture):
                        texture[pixel_index] = 0
                        pixel_index += 1
        else:
            # 8 pixels encoded in this byte
            for i in range(7, -1, -1):
                if pixel_index < len(texture):
                    texture[pixel_index] = 0xFF if (byte & (1 << i)) else 0x00
                    pixel_index += 1

    return texture

def extract_glyph(texture, char_code):
    """Extract a 6x6 glyph from the texture atlas."""
    # Characters start at space (32)
    glyph_index = char_code - 32
    if glyph_index < 0 or glyph_index >= 96:
        return None

    glyphs_per_row = TEXTURE_WIDTH // GLYPH_WIDTH  # 10 glyphs per row
    row = glyph_index // glyphs_per_row
    col = glyph_index % glyphs_per_row

    start_x = col * GLYPH_WIDTH
    start_y = row * GLYPH_HEIGHT

    # Extract 6 rows of 6 pixels each, convert to bytes (MSB = leftmost)
    bitmap = []
    for y in range(GLYPH_HEIGHT):
        byte_val = 0
        for x in range(GLYPH_WIDTH):
            px = start_x + x
            py = start_y + y
            if py < TEXTURE_HEIGHT and px < TEXTURE_WIDTH:
                if texture[py * TEXTURE_WIDTH + px]:
                    byte_val |= (0x80 >> x)
        bitmap.append(byte_val)

    return bitmap

def generate_c_code(output_path, pad_to_8=False):
    """Generate C code for the font."""
    texture = decompress_font()

    height = 8 if pad_to_8 else 6

    lines = [
        '/* Font: five_pixel (5x5 glyphs in 6x6 cells) */',
        '/* Extracted from five-pixel-font by Chris Gassib (Public Domain) */',
        '',
        '#define FONT_FIVE_PIXEL_WIDTH 6',
        f'#define FONT_FIVE_PIXEL_HEIGHT {height}',
        '',
        f'/* Each glyph is {height} bytes, one byte per row, MSB is leftmost pixel */',
        f'static const unsigned char font_five_pixel_data[96][{height}] = {{',
    ]

    for char_code in range(32, 128):
        bitmap = extract_glyph(texture, char_code)
        if bitmap:
            # Pad to 8 rows if requested (add 1 row top, 1 row bottom)
            if pad_to_8:
                bitmap = [0x00] + bitmap + [0x00]
            char_repr = repr(chr(char_code)) if 32 <= char_code < 127 else f'({char_code})'
            row_bytes = ', '.join(f'0x{b:02X}' for b in bitmap)
            lines.append(f'    /* {char_repr} */ {{{row_bytes}}},')
        else:
            empty = ', '.join(['0x00'] * height)
            lines.append(f'    /* ({char_code}) */ {{{empty}}},')

    lines.append('};')

    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))

    print(f'Generated {output_path}')

    # Print sample glyphs for verification
    print('\nSample glyphs:')
    for char_code in [65, 87, 77]:  # A, W, M
        bitmap = extract_glyph(texture, char_code)
        if bitmap:
            print(f"\n{chr(char_code)}:")
            for row in bitmap:
                print(''.join(['#' if row & (0x80 >> i) else '.' for i in range(6)]))

if __name__ == '__main__':
    generate_c_code('src/font_five_pixel.c', pad_to_8=True)
