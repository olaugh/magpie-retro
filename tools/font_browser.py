#!/usr/bin/env python3
"""
Generate an HTML font browser that renders bitmap fonts pixel-perfectly using canvas.
"""

import os
import re
import sys
import json
import subprocess
from pathlib import Path
from collections import defaultdict

# Use venv if available
venv_path = Path(__file__).parent.parent / '.venv' / 'lib'
if venv_path.exists():
    import site
    for p in venv_path.glob('python*/site-packages'):
        site.addsitedir(str(p))

import re
from fontTools.ttLib import TTFont


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
        bitmap_hex = match.group(6).strip().split('\n')

        if encoding < 32 or encoding > 126:
            continue

        # Parse bitmap rows - BDF stores MSB as leftmost pixel
        # For fonts wider than 8px, we need to extract the leftmost 8 bits
        bitmap = []
        for hex_row in bitmap_hex:
            hex_row = hex_row.strip()
            if hex_row:
                row_val = int(hex_row, 16)
                # Calculate total bits in the hex value
                total_bits = len(hex_row) * 4
                # Shift right to get the leftmost 8 bits
                if total_bits > 8:
                    row_val = (row_val >> (total_bits - 8)) & 0xFF
                else:
                    row_val = row_val & 0xFF
                bitmap.append(row_val)

        glyphs[encoding] = bitmap

    return {
        'width': font_width,
        'height': font_height,
        'glyphs': glyphs
    }


def extract_font_bitmaps(font_path):
    """Extract all ASCII glyph bitmaps from a font file."""
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

    # Find the bitmap subtable with metrics
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
            # Convert to byte (MSB = leftmost)
            byte_val = 0
            for col, bit in enumerate(row_bits[:8]):
                if bit:
                    byte_val |= (0x80 >> col)
            bitmap.append(byte_val)

        glyphs[char_code] = bitmap

    font.close()

    return {
        'width': width,
        'height': height,
        'glyphs': glyphs
    }


def generate_html(fonts_dir, output_file):
    """Generate HTML font browser with canvas-based pixel rendering."""
    fonts_dir = Path(fonts_dir)

    print("Scanning fonts and extracting bitmaps...")
    fonts = []

    # Collect both OTB and BDF files
    otb_files = list(fonts_dir.rglob("*.otb"))
    bdf_files = list(fonts_dir.rglob("*.bdf"))
    all_files = [(f, 'otb') for f in otb_files] + [(f, 'bdf') for f in bdf_files]
    total = len(all_files)

    if total == 0:
        print("No font files found!")
        return

    print(f"Found {len(otb_files)} OTB files and {len(bdf_files)} BDF files")

    for i, (font_file, font_type) in enumerate(all_files):
        if (i + 1) % 50 == 0:
            print(f"  Processing {i+1}/{total}...")

        if font_type == 'bdf':
            font_data = extract_bdf_bitmaps(font_file)
        else:
            font_data = extract_font_bitmaps(font_file)

        if not font_data or not font_data['glyphs']:
            continue

        name = font_file.stem
        size = f"{font_data['width']}x{font_data['height']}"

        fonts.append({
            'name': name,
            'size': size,
            'width': font_data['width'],
            'height': font_data['height'],
            'glyphs': {str(k): v for k, v in font_data['glyphs'].items()}
        })

    # Sort by height first, then width, then name
    fonts.sort(key=lambda f: (f['height'], f['width'], f['name'].lower()))

    # Get unique sizes
    def size_sort_key(s):
        parts = s.split('x')
        try:
            return (int(parts[1]), int(parts[0]))
        except:
            return (999, 999)

    sizes = sorted(set(f['size'] for f in fonts), key=size_sort_key)

    by_size = defaultdict(list)
    for f in fonts:
        by_size[f['size']].append(f)

    # Generate HTML with embedded font data
    html = '''<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Pixel Font Browser (Canvas)</title>
    <style>
        * { box-sizing: border-box; }
        body {
            font-family: system-ui, -apple-system, sans-serif;
            margin: 0;
            padding: 20px;
            background: #1a1a2e;
            color: #eee;
        }
        h1 { margin: 0 0 20px 0; color: #fff; }
        .controls {
            position: sticky;
            top: 0;
            background: #1a1a2e;
            padding: 15px 0;
            border-bottom: 1px solid #333;
            margin-bottom: 20px;
            z-index: 100;
        }
        .size-filters {
            display: flex;
            flex-wrap: wrap;
            gap: 8px;
            margin-bottom: 15px;
        }
        .size-btn {
            padding: 6px 12px;
            border: 1px solid #444;
            background: #2a2a4e;
            color: #aaa;
            border-radius: 4px;
            cursor: pointer;
            font-size: 13px;
        }
        .size-btn:hover { background: #3a3a6e; color: #fff; }
        .size-btn.active { background: #4a4a8e; color: #fff; border-color: #6a6aae; }
        .size-btn .count { opacity: 0.6; font-size: 11px; margin-left: 4px; }
        .search-box { display: flex; gap: 10px; align-items: center; flex-wrap: wrap; }
        #search, .sample-input {
            padding: 8px 12px;
            border: 1px solid #444;
            background: #2a2a4e;
            color: #fff;
            border-radius: 4px;
            font-size: 14px;
        }
        #search { width: 300px; }
        .sample-input { width: 200px; }
        #search:focus, .sample-input:focus { outline: none; border-color: #6a6aae; }
        .stats { color: #888; font-size: 13px; }
        .scale-controls { display: flex; gap: 8px; align-items: center; }
        .scale-btn {
            padding: 4px 10px;
            border: 1px solid #444;
            background: #2a2a4e;
            color: #aaa;
            border-radius: 4px;
            cursor: pointer;
        }
        .scale-btn.active { background: #4a4a8e; color: #fff; }
        .font-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(450px, 1fr));
            gap: 15px;
        }
        .font-card {
            background: #252545;
            border: 1px solid #333;
            border-radius: 8px;
            padding: 15px;
        }
        .font-card:hover { border-color: #555; }
        .font-card.hidden { display: none; }
        .font-name {
            font-size: 12px;
            color: #888;
            margin-bottom: 8px;
            word-break: break-all;
        }
        .font-size-tag {
            display: inline-block;
            background: #3a3a6e;
            color: #aaf;
            padding: 2px 6px;
            border-radius: 3px;
            font-size: 11px;
            margin-left: 8px;
        }
        .font-sample {
            background: #1a1a2e;
            padding: 12px;
            border-radius: 4px;
            overflow-x: auto;
        }
        .font-sample canvas {
            image-rendering: pixelated;
            image-rendering: crisp-edges;
        }
        .alphabet {
            margin-top: 8px;
            background: #1a1a2e;
            padding: 8px;
            border-radius: 4px;
            overflow-x: auto;
        }
    </style>
</head>
<body>
'''

    html += f'<h1>Pixel Font Browser <span class="stats">({len(fonts)} bitmap fonts)</span></h1>\n'

    # Controls
    html += '<div class="controls">\n'
    html += '    <div class="size-filters">\n'
    html += f'        <button class="size-btn active" data-size="all">All <span class="count">({len(fonts)})</span></button>\n'
    for size in sizes:
        count = len(by_size[size])
        html += f'        <button class="size-btn" data-size="{size}">{size} <span class="count">({count})</span></button>\n'
    html += '    </div>\n'
    html += '    <div class="search-box">\n'
    html += '        <input type="text" id="search" placeholder="Search font names...">\n'
    html += '        <input type="text" class="sample-input" id="sample-text" value="SCRABBLE 123" placeholder="Sample text">\n'
    html += '        <div class="scale-controls">\n'
    html += '            <span class="stats">Scale:</span>\n'
    html += '            <button class="scale-btn" data-scale="1">1x</button>\n'
    html += '            <button class="scale-btn" data-scale="2">2x</button>\n'
    html += '            <button class="scale-btn active" data-scale="3">3x</button>\n'
    html += '            <button class="scale-btn" data-scale="4">4x</button>\n'
    html += '        </div>\n'
    html += '        <span class="stats" id="visible-count"></span>\n'
    html += '    </div>\n'
    html += '</div>\n'

    # Font grid
    html += '<div class="font-grid" id="font-grid">\n'
    for i, font in enumerate(fonts):
        html += f'''    <div class="font-card" data-size="{font['size']}" data-name="{font['name'].lower()}" data-idx="{i}">
        <div class="font-name">{font['name']} <span class="font-size-tag">{font['size']}</span></div>
        <div class="font-sample"><canvas class="sample-canvas"></canvas></div>
        <div class="alphabet"><canvas class="alphabet-canvas"></canvas></div>
    </div>
'''
    html += '</div>\n'

    # Embed font data as JSON
    html += '<script>\n'
    html += 'const fontData = '
    html += json.dumps(fonts, separators=(',', ':'))
    html += ';\n'

    # JavaScript for rendering and filtering
    html += '''
let currentScale = 3;
let sampleText = 'SCRABBLE 123';
const alphabetText = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789 !@#$%';

function drawText(canvas, font, text, scale) {
    const ctx = canvas.getContext('2d');
    const charWidth = font.width;
    const charHeight = font.height;

    canvas.width = text.length * charWidth * scale;
    canvas.height = charHeight * scale;

    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    ctx.fillStyle = '#ffffff';

    for (let i = 0; i < text.length; i++) {
        const charCode = text.charCodeAt(i);
        const glyphData = font.glyphs[charCode];
        if (!glyphData) continue;

        const x = i * charWidth * scale;

        for (let row = 0; row < charHeight; row++) {
            const rowByte = glyphData[row] || 0;
            for (let col = 0; col < charWidth; col++) {
                if (rowByte & (0x80 >> col)) {
                    ctx.fillRect(x + col * scale, row * scale, scale, scale);
                }
            }
        }
    }
}

function renderAllFonts() {
    const cards = document.querySelectorAll('.font-card');
    cards.forEach(card => {
        const idx = parseInt(card.dataset.idx);
        const font = fontData[idx];
        const sampleCanvas = card.querySelector('.sample-canvas');
        const alphabetCanvas = card.querySelector('.alphabet-canvas');

        drawText(sampleCanvas, font, sampleText, currentScale);
        drawText(alphabetCanvas, font, alphabetText, Math.max(1, currentScale - 1));
    });
}

function updateVisibility() {
    const cards = document.querySelectorAll('.font-card');
    const activeSize = document.querySelector('.size-btn.active').dataset.size;
    const searchTerm = document.getElementById('search').value.toLowerCase();

    let count = 0;
    cards.forEach(card => {
        const matchesSize = activeSize === 'all' || card.dataset.size === activeSize;
        const matchesSearch = searchTerm === '' || card.dataset.name.includes(searchTerm);
        if (matchesSize && matchesSearch) {
            card.classList.remove('hidden');
            count++;
        } else {
            card.classList.add('hidden');
        }
    });
    document.getElementById('visible-count').textContent = `Showing ${count} fonts`;
}

// Event listeners
document.querySelectorAll('.size-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.size-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        updateVisibility();
    });
});

document.querySelectorAll('.scale-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.scale-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentScale = parseInt(btn.dataset.scale);
        renderAllFonts();
    });
});

document.getElementById('search').addEventListener('input', updateVisibility);

document.getElementById('sample-text').addEventListener('input', (e) => {
    sampleText = e.target.value || 'SCRABBLE';
    renderAllFonts();
});

// Initial render
renderAllFonts();
updateVisibility();
</script>
</body>
</html>
'''

    with open(output_file, 'w') as f:
        f.write(html)

    print(f"\nGenerated {output_file} with {len(fonts)} fonts")
    print(f"Sizes found: {', '.join(sizes)}")


if __name__ == '__main__':
    fonts_dir = sys.argv[1] if len(sys.argv) > 1 else 'fonts'
    output_file = sys.argv[2] if len(sys.argv) > 2 else 'font_browser.html'
    generate_html(fonts_dir, output_file)
