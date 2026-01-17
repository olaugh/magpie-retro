#!/bin/bash
# Download font collections for the font browser and extraction tools

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FONTS_DIR="$PROJECT_DIR/fonts"

mkdir -p "$FONTS_DIR/downloaded"

echo "Downloading font collections..."

# Old School PC Font Pack - comprehensive collection of retro bitmap fonts
OLDSCHOOL_URL="https://int10h.org/oldschool-pc-fonts/download/oldschool_pc_font_pack_v2.2_FULL.zip"
OLDSCHOOL_ZIP="$FONTS_DIR/oldschool_pc_font_pack.zip"
if [ ! -f "$OLDSCHOOL_ZIP" ]; then
    echo "Downloading Old School PC Font Pack..."
    curl -L -o "$OLDSCHOOL_ZIP" "$OLDSCHOOL_URL"
    echo "Extracting..."
    unzip -q -o "$OLDSCHOOL_ZIP" -d "$FONTS_DIR"
    rm "$OLDSCHOOL_ZIP"
else
    echo "Old School PC Font Pack already downloaded"
fi

# Five Pixel Font - 5x5 glyphs, good for small tiles
FIVE_PIXEL_DIR="$FONTS_DIR/downloaded/five-pixel-font"
if [ ! -d "$FIVE_PIXEL_DIR" ]; then
    echo "Cloning five-pixel-font..."
    git clone --depth 1 https://github.com/ChrisG0x20/five-pixel-font.git "$FIVE_PIXEL_DIR"
else
    echo "five-pixel-font already downloaded"
fi

# Burnfont - variable width pixel fonts
BURNFONT_DIR="$FONTS_DIR/downloaded/burnfont"
if [ ! -d "$BURNFONT_DIR" ]; then
    echo "Cloning burnfont..."
    git clone --depth 1 https://github.com/xyproto/burnfont.git "$BURNFONT_DIR"
else
    echo "burnfont already downloaded"
fi

# Spleen - monospaced bitmap fonts
SPLEEN_URL="https://github.com/fcambus/spleen/releases/download/2.1.0/spleen-2.1.0.tar.gz"
SPLEEN_DIR="$FONTS_DIR/downloaded/spleen-2.1.0"
if [ ! -d "$SPLEEN_DIR" ]; then
    echo "Downloading Spleen fonts..."
    curl -L -o "$FONTS_DIR/spleen.tar.gz" "$SPLEEN_URL"
    tar -xzf "$FONTS_DIR/spleen.tar.gz" -C "$FONTS_DIR/downloaded"
    rm "$FONTS_DIR/spleen.tar.gz"
else
    echo "Spleen fonts already downloaded"
fi

echo ""
echo "Done! Fonts downloaded to: $FONTS_DIR"
echo ""
echo "To browse fonts, run:"
echo "  python3 tools/font_browser.py fonts font_browser.html"
echo "  open font_browser.html"
