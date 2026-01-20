/*
 * Graphics and UI for Sega Genesis - Simplified for debugging
 */

#include "scrabble.h"
#include <stdint.h>

/* VDP ports */
#define VDP_DATA    (*((volatile uint16_t *)0xC00000))
#define VDP_CTRL    (*((volatile uint16_t *)0xC00004))
#define VDP_CTRL32  (*((volatile uint32_t *)0xC00004))

/* Write to VDP control port (for registers) */
static void vdp_set_reg(uint8_t reg, uint8_t val) {
    VDP_CTRL = 0x8000 | (reg << 8) | val;
}

/* Set VRAM write address using 32-bit write */
static void vdp_set_vram_write(uint16_t addr) {
    /* Command format: 0x40000000 | ((addr & 0x3FFF) << 16) | ((addr >> 14) & 3) */
    uint32_t cmd = 0x40000000UL | ((uint32_t)(addr & 0x3FFF) << 16) | ((addr >> 14) & 3);
    VDP_CTRL32 = cmd;
}

/* Set CRAM write address using 32-bit write */
static void vdp_set_cram_write(uint16_t addr) {
    /* CRAM command: CD1 CD0 = 11, CD5 CD4 = 00 */
    /* Address goes in bits 16-29 (A13-A0), not shifted by 17 */
    uint32_t cmd = 0xC0000000UL | ((uint32_t)(addr & 0x7F) << 16);
    VDP_CTRL32 = cmd;
}

/* Wait for VBlank */
void wait_vblank(void) {
    /* Simple delay instead of waiting for VBlank flag */
    for (volatile int i = 0; i < 5000; i++);
}

/* Initialize VDP */
void vdp_init(void) {
    /* Set up VDP registers */
    vdp_set_reg(0, 0x04);   /* Mode reg 1: No H-int */
    vdp_set_reg(1, 0x64);   /* Mode reg 2: Display ON, V-int ON (bit 5) */
    vdp_set_reg(2, 0x30);   /* Plane A at 0xC000 */
    vdp_set_reg(3, 0x3C);   /* Window at 0xF000 */
    vdp_set_reg(4, 0x07);   /* Plane B at 0xE000 */
    vdp_set_reg(5, 0x6C);   /* Sprite table at 0xD800 */
    vdp_set_reg(6, 0x00);   /* Unused */
    vdp_set_reg(7, 0x00);   /* Background: palette 0, color 0 */
    vdp_set_reg(10, 0xFF);  /* H-int timing */
    vdp_set_reg(11, 0x00);  /* Mode reg 3 */
    vdp_set_reg(12, 0x81);  /* Mode reg 4: H40 mode */
    vdp_set_reg(13, 0x3F);  /* H-scroll at 0xFC00 */
    vdp_set_reg(15, 0x02);  /* Auto-increment 2 */
    vdp_set_reg(16, 0x01);  /* Scroll size 64x32 */
    vdp_set_reg(17, 0x00);  /* Window H pos */
    vdp_set_reg(18, 0x00);  /* Window V pos */
}

/* Set a palette color */
void set_palette(int pal, int index, uint16_t color) {
    vdp_set_cram_write((pal * 32) + (index * 2));
    VDP_DATA = color;
}

/* Colors (Genesis BGR format: 0x0BGR) */
#define BLACK     0x0000
#define WHITE     0x0EEE
#define DARKGRAY  0x0444  /* Board background */
#define GRIDLINE  0x0222  /* Grid lines between squares */
#define RED       0x024E  /* TWS - red */
#define PINK      0x046A  /* DWS - pink/salmon */
#define DKBLUE    0x0A42  /* TLS - dark blue */
#define LTBLUE    0x0C86  /* DLS - light blue */
#define CREAM     0x08CE  /* Letter tile background */
#define TANBORDER 0x046A  /* Letter tile border */
#define PURPLE    0x0A4A  /* Blank tile */
#define GREEN     0x0464  /* Board felt green (alternative) */

/* Tile indices for custom graphics */
#define TILE_EMPTY    128   /* Empty board square */
#define TILE_TWS      129   /* Triple word score */
#define TILE_DWS      130   /* Double word score */
#define TILE_TLS      131   /* Triple letter score */
#define TILE_DLS      132   /* Double letter score */
#define TILE_LETTER_A 133   /* Letters A-Z at 133-158 */
#define TILE_BLANK    159   /* Blank tile */
#define TILE_STAR     160   /* Center star */
#define TILE_GRID     161   /* Grid tile for Plane B background */


/* Initialize palettes - using full 16-color palette for board graphics */
void init_palettes(void) {
    /* Palette 0: Board and tile graphics */
    set_palette(0, 0, BLACK);      /* 0: Transparent/black */
    set_palette(0, 1, WHITE);      /* 1: White text */
    set_palette(0, 2, DARKGRAY);   /* 2: Empty square / board bg */
    set_palette(0, 3, RED);        /* 3: TWS */
    set_palette(0, 4, PINK);       /* 4: DWS */
    set_palette(0, 5, DKBLUE);     /* 5: TLS */
    set_palette(0, 6, LTBLUE);     /* 6: DLS */
    set_palette(0, 7, CREAM);      /* 7: Letter tile background */
    set_palette(0, 8, BLACK);      /* 8: Letter foreground */
    set_palette(0, 9, TANBORDER);  /* 9: Tile border */
    set_palette(0, 10, PURPLE);    /* 10: Blank tile bg */
    set_palette(0, 11, GRIDLINE);  /* 11: Grid lines */
    set_palette(0, 12, 0x0888);    /* 12: Medium gray */
    set_palette(0, 13, 0x0CCC);    /* 13: Light gray */
    set_palette(0, 14, 0x00AE);    /* 14: Orange/yellow highlight */
    set_palette(0, 15, GREEN);     /* 15: Green */

    /* Palette 1: Grey text for blank tiles in history */
    set_palette(1, 0, BLACK);
    set_palette(1, 1, 0x0888);     /* Grey text for blanks */
}

/* Standard 8x8 font for UI text (IBM VGA style - perfect for tiling) */
static const uint8_t font_ui_data[96][8] = {
    /* ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* '!' */ {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    /* '"' */ {0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* '#' */ {0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00},
    /* '$' */ {0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00},
    /* '%' */ {0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00},
    /* '&' */ {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00},
    /* "'" */ {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* '(' */ {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    /* ')' */ {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    /* '*' */ {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    /* '+' */ {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    /* ',' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    /* '-' */ {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    /* '.' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* '/' */ {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00},
    /* '0' */ {0x7C, 0xCE, 0xDE, 0xF6, 0xE6, 0xC6, 0x7C, 0x00},
    /* '1' */ {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    /* '2' */ {0x7C, 0xC6, 0x06, 0x7C, 0xC0, 0xC0, 0xFE, 0x00},
    /* '3' */ {0xFC, 0x06, 0x06, 0x3C, 0x06, 0x06, 0xFC, 0x00},
    /* '4' */ {0x0C, 0xCC, 0xCC, 0xCC, 0xFE, 0x0C, 0x0C, 0x00},
    /* '5' */ {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00},
    /* '6' */ {0x7C, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00},
    /* '7' */ {0xFE, 0x06, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x00},
    /* '8' */ {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00},
    /* '9' */ {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x06, 0x7C, 0x00},
    /* ':' */ {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    /* ';' */ {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30},
    /* '<' */ {0x0C, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0C, 0x00},
    /* '=' */ {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    /* '>' */ {0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00},
    /* '?' */ {0x3C, 0x66, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00},
    /* '@' */ {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x7C, 0x00},
    /* 'A' */ {0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0x00},
    /* 'B' */ {0xFC, 0xC6, 0xC6, 0xFC, 0xC6, 0xC6, 0xFC, 0x00},
    /* 'C' */ {0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00},
    /* 'D' */ {0xF8, 0xCC, 0xC6, 0xC6, 0xC6, 0xCC, 0xF8, 0x00},
    /* 'E' */ {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xFE, 0x00},
    /* 'F' */ {0xFE, 0xC0, 0xC0, 0xF8, 0xC0, 0xC0, 0xC0, 0x00},
    /* 'G' */ {0x7C, 0xC6, 0xC0, 0xCE, 0xC6, 0xC6, 0x7E, 0x00},
    /* 'H' */ {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00},
    /* 'I' */ {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    /* 'J' */ {0x06, 0x06, 0x06, 0x06, 0xC6, 0xC6, 0x7C, 0x00},
    /* 'K' */ {0xC6, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0xC6, 0x00},
    /* 'L' */ {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xFE, 0x00},
    /* 'M' */ {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
    /* 'N' */ {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00},
    /* 'O' */ {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    /* 'P' */ {0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0, 0xC0, 0x00},
    /* 'Q' */ {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x06},
    /* 'R' */ {0xFC, 0xC6, 0xC6, 0xFC, 0xD8, 0xCC, 0xC6, 0x00},
    /* 'S' */ {0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0xC6, 0x7C, 0x00},
    /* 'T' */ {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* 'U' */ {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    /* 'V' */ {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00},
    /* 'W' */ {0xC6, 0xC6, 0xD6, 0xFE, 0xFE, 0xEE, 0xC6, 0x00},
    /* 'X' */ {0xC6, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0xC6, 0x00},
    /* 'Y' */ {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00},
    /* 'Z' */ {0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00},
    /* '[' */ {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    /* '\\' */ {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00},
    /* ']' */ {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    /* '^' */ {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00},
    /* '_' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE},
    /* '`' */ {0x18, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 'a' */ {0x00, 0x00, 0x7C, 0x06, 0x7E, 0xC6, 0x7E, 0x00},
    /* 'b' */ {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xFC, 0x00},
    /* 'c' */ {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00},
    /* 'd' */ {0x06, 0x06, 0x7E, 0xC6, 0xC6, 0xC6, 0x7E, 0x00},
    /* 'e' */ {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00},
    /* 'f' */ {0x1C, 0x36, 0x30, 0x7C, 0x30, 0x30, 0x30, 0x00},
    /* 'g' */ {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x7C},
    /* 'h' */ {0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00},
    /* 'i' */ {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* 'j' */ {0x06, 0x00, 0x06, 0x06, 0x06, 0xC6, 0xC6, 0x7C},
    /* 'k' */ {0xC0, 0xC0, 0xCC, 0xD8, 0xF0, 0xD8, 0xCC, 0x00},
    /* 'l' */ {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00},
    /* 'm' */ {0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xC6, 0x00},
    /* 'n' */ {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0x00},
    /* 'o' */ {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00},
    /* 'p' */ {0x00, 0x00, 0xFC, 0xC6, 0xC6, 0xFC, 0xC0, 0xC0},
    /* 'q' */ {0x00, 0x00, 0x7E, 0xC6, 0xC6, 0x7E, 0x06, 0x06},
    /* 'r' */ {0x00, 0x00, 0xDC, 0xE6, 0xC0, 0xC0, 0xC0, 0x00},
    /* 's' */ {0x00, 0x00, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x00},
    /* 't' */ {0x30, 0x30, 0x7C, 0x30, 0x30, 0x36, 0x1C, 0x00},
    /* 'u' */ {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x00},
    /* 'v' */ {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00},
    /* 'w' */ {0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00},
    /* 'x' */ {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00},
    /* 'y' */ {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x7C},
    /* 'z' */ {0x00, 0x00, 0xFE, 0x0C, 0x38, 0x60, 0xFE, 0x00},
    /* '{' */ {0x0E, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0E, 0x00},
    /* '|' */ {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
    /* '}' */ {0x70, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x70, 0x00},
    /* '~' */ {0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* DEL */ {0x00, 0x10, 0x38, 0x6C, 0xC6, 0xC6, 0xFE, 0x00},
};

/* five-pixel font for letter tiles on the board (5x5 glyphs in 6x6 cells, padded to 8 rows) */
/* Extracted from five-pixel-font by Chris Gassib (Public Domain) */
/* Each glyph is 8 bytes, one byte per row, MSB is leftmost pixel */
static const uint8_t font_tile_data[96][8] = {
    /* ' ' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* '!' */ {0x00, 0x20, 0x20, 0x20, 0x00, 0x20, 0x00, 0x00},
    /* '"' */ {0x00, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* '#' */ {0x00, 0x50, 0xF8, 0x50, 0xF8, 0x50, 0x00, 0x00},
    /* '$' */ {0x00, 0x78, 0xA0, 0x70, 0x28, 0xF0, 0x00, 0x00},
    /* '%' */ {0x00, 0xC8, 0xD0, 0x20, 0x58, 0x98, 0x00, 0x00},
    /* '&' */ {0x00, 0x20, 0x50, 0x20, 0x50, 0x28, 0x00, 0x00},
    /* "'" */ {0x00, 0x20, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* '(' */ {0x00, 0x10, 0x20, 0x20, 0x20, 0x10, 0x00, 0x00},
    /* ')' */ {0x00, 0x20, 0x10, 0x10, 0x10, 0x20, 0x00, 0x00},
    /* '*' */ {0x00, 0xA8, 0x70, 0xF8, 0x70, 0xA8, 0x00, 0x00},
    /* '+' */ {0x00, 0x00, 0x20, 0x70, 0x20, 0x00, 0x00, 0x00},
    /* ',' */ {0x00, 0x00, 0x00, 0x00, 0x20, 0x40, 0x00, 0x00},
    /* '-' */ {0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00},
    /* '.' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00},
    /* '/' */ {0x00, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x00},
    /* '0' */ {0x00, 0x70, 0x98, 0xA8, 0xC8, 0x70, 0x00, 0x00},
    /* '1' */ {0x00, 0x20, 0x60, 0x20, 0x20, 0x70, 0x00, 0x00},
    /* '2' */ {0x00, 0x30, 0x48, 0x10, 0x20, 0x78, 0x00, 0x00},
    /* '3' */ {0x00, 0x60, 0x10, 0x20, 0x10, 0x60, 0x00, 0x00},
    /* '4' */ {0x00, 0x30, 0x50, 0x78, 0x10, 0x10, 0x00, 0x00},
    /* '5' */ {0x00, 0x70, 0x40, 0x60, 0x10, 0x60, 0x00, 0x00},
    /* '6' */ {0x00, 0x20, 0x40, 0xE0, 0x90, 0x60, 0x00, 0x00},
    /* '7' */ {0x00, 0x78, 0x08, 0x10, 0x20, 0x20, 0x00, 0x00},
    /* '8' */ {0x00, 0x60, 0x90, 0x60, 0x90, 0x60, 0x00, 0x00},
    /* '9' */ {0x00, 0x60, 0x90, 0x70, 0x10, 0x10, 0x00, 0x00},
    /* ':' */ {0x00, 0x00, 0x20, 0x00, 0x20, 0x00, 0x00, 0x00},
    /* ';' */ {0x00, 0x00, 0x20, 0x00, 0x20, 0x40, 0x00, 0x00},
    /* '<' */ {0x00, 0x10, 0x20, 0x40, 0x20, 0x10, 0x00, 0x00},
    /* '=' */ {0x00, 0x00, 0x70, 0x00, 0x70, 0x00, 0x00, 0x00},
    /* '>' */ {0x00, 0x40, 0x20, 0x10, 0x20, 0x40, 0x00, 0x00},
    /* '?' */ {0x00, 0x60, 0x10, 0x20, 0x00, 0x20, 0x00, 0x00},
    /* '@' */ {0x00, 0x70, 0x08, 0x68, 0xA8, 0x70, 0x00, 0x00},
    /* 'A' */ {0x00, 0x70, 0x88, 0xF8, 0x88, 0x88, 0x00, 0x00},
    /* 'B' */ {0x00, 0xF0, 0x88, 0xF0, 0x88, 0xF0, 0x00, 0x00},
    /* 'C' */ {0x00, 0x78, 0x80, 0x80, 0x80, 0x78, 0x00, 0x00},
    /* 'D' */ {0x00, 0xF0, 0x88, 0x88, 0x88, 0xF0, 0x00, 0x00},
    /* 'E' */ {0x00, 0xF8, 0x80, 0xF0, 0x80, 0xF8, 0x00, 0x00},
    /* 'F' */ {0x00, 0xF8, 0x80, 0xF0, 0x80, 0x80, 0x00, 0x00},
    /* 'G' */ {0x00, 0x78, 0x80, 0x98, 0x88, 0x78, 0x00, 0x00},
    /* 'H' */ {0x00, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x00, 0x00},
    /* 'I' */ {0x00, 0x70, 0x20, 0x20, 0x20, 0x70, 0x00, 0x00},
    /* 'J' */ {0x00, 0x38, 0x10, 0x10, 0x90, 0x60, 0x00, 0x00},
    /* 'K' */ {0x00, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x00, 0x00},
    /* 'L' */ {0x00, 0x80, 0x80, 0x80, 0x80, 0xF0, 0x00, 0x00},
    /* 'M' */ {0x00, 0x88, 0xD8, 0xA8, 0x88, 0x88, 0x00, 0x00},
    /* 'N' */ {0x00, 0x88, 0xC8, 0xA8, 0x98, 0x88, 0x00, 0x00},
    /* 'O' */ {0x00, 0x70, 0x88, 0x88, 0x88, 0x70, 0x00, 0x00},
    /* 'P' */ {0x00, 0xF0, 0x88, 0xF0, 0x80, 0x80, 0x00, 0x00},
    /* 'Q' */ {0x00, 0x70, 0x88, 0x88, 0x90, 0x68, 0x00, 0x00},
    /* 'R' */ {0x00, 0xF0, 0x88, 0xF0, 0xA0, 0x90, 0x00, 0x00},
    /* 'S' */ {0x00, 0x78, 0x80, 0x70, 0x08, 0xF0, 0x00, 0x00},
    /* 'T' */ {0x00, 0xF8, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00},
    /* 'U' */ {0x00, 0x88, 0x88, 0x88, 0x88, 0x70, 0x00, 0x00},
    /* 'V' */ {0x00, 0x88, 0x88, 0x88, 0x50, 0x20, 0x00, 0x00},
    /* 'W' */ {0x00, 0x88, 0x88, 0xA8, 0xA8, 0x50, 0x00, 0x00},
    /* 'X' */ {0x00, 0x88, 0x50, 0x20, 0x50, 0x88, 0x00, 0x00},
    /* 'Y' */ {0x00, 0x88, 0x50, 0x20, 0x20, 0x20, 0x00, 0x00},
    /* 'Z' */ {0x00, 0x70, 0x10, 0x20, 0x40, 0x70, 0x00, 0x00},
    /* '[' */ {0x00, 0x70, 0x40, 0x40, 0x40, 0x70, 0x00, 0x00},
    /* '\\' */ {0x00, 0x80, 0x40, 0x20, 0x10, 0x08, 0x00, 0x00},
    /* ']' */ {0x00, 0x70, 0x10, 0x10, 0x10, 0x70, 0x00, 0x00},
    /* '^' */ {0x00, 0x20, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* '_' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00},
    /* '`' */ {0x00, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* 'a' */ {0x00, 0x00, 0x70, 0x90, 0x90, 0x68, 0x00, 0x00},
    /* 'b' */ {0x00, 0x80, 0xE0, 0x90, 0x90, 0xE0, 0x00, 0x00},
    /* 'c' */ {0x00, 0x00, 0x30, 0x40, 0x40, 0x30, 0x00, 0x00},
    /* 'd' */ {0x00, 0x10, 0x10, 0x70, 0x90, 0x70, 0x00, 0x00},
    /* 'e' */ {0x00, 0x00, 0x20, 0x50, 0x60, 0x30, 0x00, 0x00},
    /* 'f' */ {0x00, 0x10, 0x20, 0x70, 0x20, 0x20, 0x00, 0x00},
    /* 'g' */ {0x00, 0x30, 0x50, 0x30, 0x10, 0x60, 0x00, 0x00},
    /* 'h' */ {0x00, 0x40, 0x40, 0x70, 0x48, 0x48, 0x00, 0x00},
    /* 'i' */ {0x00, 0x20, 0x00, 0x20, 0x20, 0x20, 0x00, 0x00},
    /* 'j' */ {0x00, 0x10, 0x00, 0x10, 0x50, 0x20, 0x00, 0x00},
    /* 'k' */ {0x00, 0x40, 0x40, 0x50, 0x60, 0x50, 0x00, 0x00},
    /* 'l' */ {0x00, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00},
    /* 'm' */ {0x00, 0x00, 0xD0, 0xA8, 0x88, 0x88, 0x00, 0x00},
    /* 'n' */ {0x00, 0x00, 0x60, 0x50, 0x50, 0x50, 0x00, 0x00},
    /* 'o' */ {0x00, 0x00, 0x20, 0x50, 0x50, 0x20, 0x00, 0x00},
    /* 'p' */ {0x00, 0x00, 0x60, 0x50, 0x60, 0x40, 0x00, 0x00},
    /* 'q' */ {0x00, 0x00, 0x30, 0x50, 0x30, 0x10, 0x00, 0x00},
    /* 'r' */ {0x00, 0x00, 0x50, 0x60, 0x40, 0x40, 0x00, 0x00},
    /* 's' */ {0x00, 0x00, 0x30, 0x20, 0x10, 0x30, 0x00, 0x00},
    /* 't' */ {0x00, 0x00, 0x70, 0x20, 0x20, 0x10, 0x00, 0x00},
    /* 'u' */ {0x00, 0x00, 0x48, 0x48, 0x48, 0x30, 0x00, 0x00},
    /* 'v' */ {0x00, 0x00, 0x50, 0x50, 0x50, 0x20, 0x00, 0x00},
    /* 'w' */ {0x00, 0x00, 0x88, 0xA8, 0xA8, 0x50, 0x00, 0x00},
    /* 'x' */ {0x00, 0x00, 0x50, 0x20, 0x20, 0x50, 0x00, 0x00},
    /* 'y' */ {0x00, 0x00, 0x50, 0x50, 0x20, 0x40, 0x00, 0x00},
    /* 'z' */ {0x00, 0x00, 0x30, 0x10, 0x20, 0x30, 0x00, 0x00},
    /* '{' */ {0x00, 0x10, 0x20, 0x60, 0x20, 0x10, 0x00, 0x00},
    /* '|' */ {0x00, 0x20, 0x20, 0x00, 0x20, 0x20, 0x00, 0x00},
    /* '}' */ {0x00, 0x40, 0x20, 0x30, 0x20, 0x40, 0x00, 0x00},
    /* '~' */ {0x00, 0x00, 0x50, 0xA0, 0x00, 0x00, 0x00, 0x00},
    /* DEL */ {0x00, 0xF8, 0x88, 0x88, 0x88, 0xF8, 0x00, 0x00},
};

/* Load a character tile into VRAM with foreground/background colors (uses 8x8 UI font) */
static void load_char_tile_colored(int tile_index, int char_code, uint8_t fg, uint8_t bg) {
    const uint8_t *glyph;

    if (char_code >= 32 && char_code < 128) {
        glyph = font_ui_data[char_code - 32];
    } else {
        glyph = font_ui_data[0]; /* Space for unknown */
    }

    /* Set VRAM address for tile (each tile is 32 bytes) */
    vdp_set_vram_write(tile_index * 32);

    /* Convert 1bpp font to 4bpp tile format with specified colors */
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        uint32_t pixels = 0;

        for (int col = 0; col < 8; col++) {
            uint8_t color = (bits & (0x80 >> col)) ? fg : bg;
            pixels |= ((uint32_t)color << ((7 - col) * 4));
        }

        /* Write 4 bytes (8 pixels at 4bpp) */
        VDP_DATA = (pixels >> 16) & 0xFFFF;
        VDP_DATA = pixels & 0xFFFF;
    }
}

/* Load a character tile into VRAM (white on black for text) */
static void load_char_tile(int tile_index, int char_code) {
    load_char_tile_colored(tile_index, char_code, 1, 0);  /* White on black */
}

/* Create a colored square tile with TRANSPARENT right+bottom edges (for Plane A) */
static void load_board_square_tile(int tile_index, uint8_t fill_color) {
    vdp_set_vram_write(tile_index * 32);

    for (int row = 0; row < 8; row++) {
        uint32_t pixels = 0;
        for (int col = 0; col < 8; col++) {
            uint8_t color;
            /* Transparent on right edge (col 7) and bottom edge (row 7) - shows Plane B grid */
            if (row == 7 || col == 7) {
                color = 0;  /* Transparent */
            } else {
                color = fill_color;
            }
            pixels |= ((uint32_t)color << ((7 - col) * 4));
        }
        VDP_DATA = (pixels >> 16) & 0xFFFF;
        VDP_DATA = pixels & 0xFFFF;
    }
}

/* Create a grid tile for Plane B - grid lines on right+bottom, dark fill elsewhere */
static void load_grid_tile(int tile_index) {
    vdp_set_vram_write(tile_index * 32);

    for (int row = 0; row < 8; row++) {
        uint32_t pixels = 0;
        for (int col = 0; col < 8; col++) {
            uint8_t color;
            /* Grid line on right edge (col 7) and bottom edge (row 7) */
            if (row == 7 || col == 7) {
                color = 11;  /* Grid line color */
            } else {
                color = 2;   /* Dark gray background (won't be visible when Plane A covers it) */
            }
            pixels |= ((uint32_t)color << ((7 - col) * 4));
        }
        VDP_DATA = (pixels >> 16) & 0xFFFF;
        VDP_DATA = pixels & 0xFFFF;
    }
}

/* Create a letter tile: 5x7 glyph centered with margins, transparent edges for grid */
static void load_letter_tile(int tile_index, char letter) {
    const uint8_t *glyph;
    int char_code = letter;

    if (char_code >= 32 && char_code < 128) {
        glyph = font_tile_data[char_code - 32];
    } else {
        glyph = font_tile_data[0];
    }

    vdp_set_vram_write(tile_index * 32);

    for (int row = 0; row < 8; row++) {
        uint32_t pixels = 0;

        for (int col = 0; col < 8; col++) {
            uint8_t color;
            /* Transparent on right (col 7) and bottom (row 7) - shows Plane B grid */
            if (row == 7 || col == 7) {
                color = 0;  /* Transparent */
            }
            /* Left margin (col 0) and right margin (col 6) */
            else if (col == 0 || col == 6) {
                color = 7;  /* Cream margin */
            }
            /* Glyph area: rows 0-6, cols 1-5 (5 pixels wide, 7 rows) */
            else {
                uint8_t bits = glyph[row];
                /* Check bit for column: col 1 -> bit 7, col 2 -> bit 6, etc. */
                if (bits & (0x80 >> (col - 1))) {
                    color = 8;  /* Black letter */
                } else {
                    color = 7;  /* Cream background */
                }
            }
            pixels |= ((uint32_t)color << ((7 - col) * 4));
        }
        VDP_DATA = (pixels >> 16) & 0xFFFF;
        VDP_DATA = pixels & 0xFFFF;
    }
}

/* Create a blank (wildcard) tile: 5x7 glyph on purple, transparent edges for grid */
static void load_blank_letter_tile(int tile_index, char letter) {
    const uint8_t *glyph;
    int char_code = letter;

    if (char_code >= 32 && char_code < 128) {
        glyph = font_tile_data[char_code - 32];
    } else {
        glyph = font_tile_data[0];
    }

    vdp_set_vram_write(tile_index * 32);

    for (int row = 0; row < 8; row++) {
        uint32_t pixels = 0;

        for (int col = 0; col < 8; col++) {
            uint8_t color;
            /* Transparent on right (col 7) and bottom (row 7) - shows Plane B grid */
            if (row == 7 || col == 7) {
                color = 0;  /* Transparent */
            }
            /* Left margin (col 0) and right margin (col 6) */
            else if (col == 0 || col == 6) {
                color = 10;  /* Purple margin */
            }
            /* Glyph area: rows 0-6, cols 1-5 (5 pixels wide, 7 rows) */
            else {
                uint8_t bits = glyph[row];
                /* Check bit for column: col 1 -> bit 7, col 2 -> bit 6, etc. */
                if (bits & (0x80 >> (col - 1))) {
                    color = 1;  /* White letter on purple */
                } else {
                    color = 10; /* Purple background */
                }
            }
            pixels |= ((uint32_t)color << ((7 - col) * 4));
        }
        VDP_DATA = (pixels >> 16) & 0xFFFF;
        VDP_DATA = pixels & 0xFFFF;
    }
}

/* Create center star tile, transparent edges for grid */
static void load_star_tile(int tile_index) {
    /* Simple star pattern on pink background */
    static const uint8_t star_pattern[8] = {
        0x00,  /*         */
        0x10,  /*    *    */
        0x38,  /*   ***   */
        0x7C,  /*  *****  */
        0x38,  /*   ***   */
        0x28,  /*   * *   */
        0x00,  /*         */
        0x00   /*         */
    };

    vdp_set_vram_write(tile_index * 32);

    for (int row = 0; row < 8; row++) {
        uint8_t bits = star_pattern[row];
        uint32_t pixels = 0;

        for (int col = 0; col < 8; col++) {
            uint8_t color;
            /* Transparent on right (col 7) and bottom (row 7) - shows Plane B grid */
            if (row == 7 || col == 7) {
                color = 0;   /* Transparent */
            } else if (bits & (0x80 >> col)) {
                color = 1;   /* White star */
            } else {
                color = 4;   /* Pink background (DWS) */
            }
            pixels |= ((uint32_t)color << ((7 - col) * 4));
        }
        VDP_DATA = (pixels >> 16) & 0xFFFF;
        VDP_DATA = pixels & 0xFFFF;
    }
}

/* Initialize all tiles */
void init_tiles(void) {
    /* Load ASCII characters 32-127 as tiles 32-127 (for text) */
    for (int i = 32; i < 128; i++) {
        load_char_tile(i, i);
    }

    /* Load board square tiles */
    load_board_square_tile(TILE_EMPTY, 2);  /* Dark gray */
    load_board_square_tile(TILE_TWS, 3);    /* Red */
    load_board_square_tile(TILE_DWS, 4);    /* Pink */
    load_board_square_tile(TILE_TLS, 5);    /* Dark blue */
    load_board_square_tile(TILE_DLS, 6);    /* Light blue */

    /* Load letter tiles A-Z */
    for (int i = 0; i < 26; i++) {
        load_letter_tile(TILE_LETTER_A + i, 'A' + i);
    }

    /* Load blank letter tiles (lowercase a-z represent blanks) */
    for (int i = 0; i < 26; i++) {
        load_blank_letter_tile(TILE_LETTER_A + 32 + i, 'A' + i);  /* Tiles 165-190 */
    }

    /* Load center star tile */
    load_star_tile(TILE_STAR);

    /* Load blank tile (? on purple background) */
    load_blank_letter_tile(TILE_BLANK, '?');

    /* Load grid tile for Plane B background */
    load_grid_tile(TILE_GRID);

    /* Sprites disabled - using 8x8 font with perfect tiling instead */
}

/* Put a tile at screen position on Plane A */
void put_tile(int x, int y, int tile, int pal) {
    /* Plane A is at 0xC000, each entry is 2 bytes, 64 tiles per row */
    uint16_t addr = 0xC000 + (y * 64 + x) * 2;
    uint16_t attr = tile | (pal << 13);

    vdp_set_vram_write(addr);
    VDP_DATA = attr;
}

/* Put a tile at screen position on Plane B (background) */
static void put_tile_b(int x, int y, int tile, int pal) {
    /* Plane B is at 0xE000, each entry is 2 bytes, 64 tiles per row */
    uint16_t addr = 0xE000 + (y * 64 + x) * 2;
    uint16_t attr = tile | (pal << 13);

    vdp_set_vram_write(addr);
    VDP_DATA = attr;
}

/* Draw a character at tile position */
void draw_char(int x, int y, char c, int pal) {
    if (c >= 32 && c < 128) {
        put_tile(x, y, c, pal);
    }
}

/* Draw a string */
void draw_string(int x, int y, const char *str, int pal) {
    while (*str) {
        draw_char(x++, y, *str++, pal);
    }
}

/* Draw a number - avoid division for 68000 compatibility */
void draw_number(int x, int y, int num, int pal) {
    char buf[12];
    int pos = 0;
    int neg = 0;

    if (num < 0) {
        neg = 1;
        num = -num;
    }

    if (num == 0) {
        buf[pos++] = '0';
    } else {
        /* Extract digits using repeated subtraction instead of division */
        static const int powers[] = {10000, 1000, 100, 10, 1};
        int started = 0;
        for (int i = 0; i < 5; i++) {
            int digit = 0;
            while (num >= powers[i]) {
                num -= powers[i];
                digit++;
            }
            if (digit > 0 || started) {
                buf[pos++] = '0' + digit;
                started = 1;
            }
        }
    }

    if (neg) {
        draw_char(x++, y, '-', pal);
    }

    /* Print digits */
    for (int i = 0; i < pos; i++) {
        draw_char(x++, y, buf[i], pal);
    }
}

/* Draw a number in hexadecimal */
void draw_hex(int x, int y, uint32_t num, int pal) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buf[8];

    /* Convert to hex digits (up to 8 nibbles for 32-bit) */
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex_chars[num & 0xF];
        num >>= 4;
    }

    /* Skip leading zeros but always show at least one digit */
    int start = 0;
    while (start < 7 && buf[start] == '0') {
        start++;
    }

    /* Print digits */
    for (int i = start; i < 8; i++) {
        draw_char(x++, y, buf[i], pal);
    }
}

/* Draw exactly n hex digits (with leading zeros) */
static void draw_hex_n(int x, int y, uint32_t num, int n, int pal) {
    static const char hex_chars[] = "0123456789ABCDEF";
    for (int i = n - 1; i >= 0; i--) {
        draw_char(x + i, y, hex_chars[num & 0xF], pal);
        num >>= 4;
    }
}

/* Draw 3-digit decimal number (  0-999, with leading spaces) */
/* Uses repeated subtraction instead of division for M68000 */
static void draw_number_3d(int x, int y, int num, int pal) {
    if (num < 0) num = 0;
    if (num > 999) num = 999;
    int hundreds = 0;
    while (num >= 100) {
        hundreds++;
        num -= 100;
    }
    int tens = 0;
    while (num >= 10) {
        tens++;
        num -= 10;
    }
    /* Leading spaces instead of zeros */
    if (hundreds > 0) {
        draw_char(x, y, '0' + hundreds, pal);
        draw_char(x + 1, y, '0' + tens, pal);
    } else if (tens > 0) {
        draw_char(x, y, ' ', pal);
        draw_char(x + 1, y, '0' + tens, pal);
    } else {
        draw_char(x, y, ' ', pal);
        draw_char(x + 1, y, ' ', pal);
    }
    draw_char(x + 2, y, '0' + num, pal);
}

/* Clear screen */
void clear_screen(void) {
    vdp_set_vram_write(0xC000);
    for (int i = 0; i < 64 * 32; i++) {
        VDP_DATA = 0;
    }
}

/* Board positioning in tile coordinates (8x8 hardware tiles) */
#define BOARD_LEFT 2   /* Column for board start (after row numbers) */
#define BOARD_TOP  4   /* Row for board start (after column headers) */

/* Draw the game board - uses two planes for grid effect */
void draw_board(const Board *board) {
    /* First, draw static grid pattern on Plane B for entire board area */
    for (int row = 0; row < BOARD_DIM; row++) {
        for (int col = 0; col < BOARD_DIM; col++) {
            put_tile_b(BOARD_LEFT + col, BOARD_TOP + row, TILE_GRID, 0);
        }
    }

    /* Column headers: A-O at row 3 */
    for (int c = 0; c < BOARD_DIM; c++) {
        draw_char(BOARD_LEFT + c, 3, 'A' + c, 0);
    }

    /* Draw row labels - all tiles, 8x8 font tiles perfectly */
    for (int row = 0; row < BOARD_DIM; row++) {
        int row_num = row + 1;  /* 1-15 */
        int y = BOARD_TOP + row;

        if (row_num < 10) {
            /* Single digit: clear tens position, draw ones at column 1 */
            put_tile(0, y, ' ', 0);
            draw_char(1, y, '0' + row_num, 0);
        } else {
            /* Double digit: draw both at columns 0 and 1 */
            draw_char(0, y, '1', 0);
            draw_char(1, y, '0' + (row_num - 10), 0);
        }
    }

    /* Draw board squares */
    for (int row = 0; row < BOARD_DIM; row++) {
        int y = BOARD_TOP + row;

        /* Draw squares */
        for (int col = 0; col < BOARD_DIM; col++) {
            int idx = row * BOARD_DIM + col;
            MachineLetter ml = board->h_letters[idx];
            uint8_t bonus = board->bonuses[idx];
            int x = BOARD_LEFT + col;
            int tile;

            if (ml != ALPHABET_EMPTY_SQUARE_MARKER) {
                /* Tile placed - draw letter (covers entire square with grid) */
                uint8_t letter_idx = UNBLANKED(ml);
                if (letter_idx >= 1 && letter_idx <= 26) {
                    if (IS_BLANKED(ml)) {
                        /* Blank tile - use purple background tiles (165-190) */
                        tile = TILE_LETTER_A + 32 + (letter_idx - 1);
                    } else {
                        /* Normal letter tile */
                        tile = TILE_LETTER_A + (letter_idx - 1);
                    }
                } else {
                    tile = TILE_BLANK;  /* Unknown - show ? */
                }
            } else {
                /* Empty square - show bonus or plain (with grid) */
                switch (bonus) {
                    case BONUS_TW: tile = TILE_TWS; break;
                    case BONUS_DW: tile = TILE_DWS; break;
                    case BONUS_CENTER: tile = TILE_STAR; break;
                    case BONUS_TL: tile = TILE_TLS; break;
                    case BONUS_DL: tile = TILE_DLS; break;
                    default: tile = TILE_EMPTY; break;
                }
            }
            put_tile(x, y, tile, 0);
        }
    }
}

/* Draw player rack using graphical tiles */
void draw_rack(const Rack *rack) {
    int y = BOARD_TOP + 16;  /* Below the board (row 20) */
    draw_string(0, y, "RACK:", 0);

    /* Convert rack to string, then draw as tiles */
    char rack_str[RACK_SIZE + 1];
    rack_to_string(rack, rack_str);

    /* Draw each rack tile graphically */
    int x = 6;
    for (int i = 0; rack_str[i] != '\0' && i < RACK_SIZE; i++) {
        char c = rack_str[i];
        int tile;
        if (c == '?') {
            /* Blank tile */
            tile = TILE_BLANK;
        } else if (c >= 'A' && c <= 'Z') {
            /* Normal letter */
            tile = TILE_LETTER_A + (c - 'A');
        } else {
            /* Unknown - use text character */
            tile = c;
        }
        put_tile(x, y, tile, 0);
        x++;
    }
    /* Clear remaining rack slots */
    while (x < 14) {
        put_tile(x++, y, ' ', 0);
    }
}

/* Draw scores above the board on the left side */
void draw_scores(const GameState *game) {
    /* Player 1 score on row 0 */
    draw_char(0, 0, (game->current_player == 0) ? '>' : ' ', 0);
    draw_string(1, 0, "P1:", 0);
    draw_number(4, 0, game->players[0].score, 0);
    draw_string(8, 0, "        ", 0);  /* Clear trailing */

    /* Player 2 score on row 1 */
    draw_char(0, 1, (game->current_player == 1) ? '>' : ' ', 0);
    draw_string(1, 1, "P2:", 0);
    draw_number(4, 1, game->players[1].score, 0);
    draw_string(8, 1, "        ", 0);  /* Clear trailing */
}

/* History entry structure (must match main.c) */
typedef struct {
    char word[16];
    uint16_t blanks;   /* Bitmask: bit i set if position i is a blank */
    int16_t score;
    int16_t equity;    /* Equity in eighths of a point */
    uint16_t frames;   /* Frames elapsed finding this move */
    uint8_t player;
} HistoryEntry;

/* Draw move history in sidebar - uses rows 0-27 on the right side */
#define HISTORY_START_ROW 0
#define HISTORY_ROWS 28    /* Rows 0-27 = 28 rows available */
#define HISTORY_COL 18     /* Start column for history */

void draw_history(const HistoryEntry *hist, int count) {
    int start = 0;

    /* Show last HISTORY_ROWS moves that fit */
    if (count > HISTORY_ROWS) start = count - HISTORY_ROWS;

    for (int i = 0; i < HISTORY_ROWS; i++) {
        int y = HISTORY_START_ROW + i;
        int idx = start + i;

        if (idx < count) {
            const HistoryEntry *h = &hist[idx];
            /* Player indicator: > for P1, < for P2 */
            draw_char(HISTORY_COL, y, (h->player == 0) ? '>' : '<', 0);
            /* Word (up to 9 chars) */
            int word_ended = 0;
            for (int j = 0; j < 9; j++) {
                char c = h->word[j];
                if (word_ended || c == '\0') {
                    draw_char(HISTORY_COL + 1 + j, y, ' ', 0);
                    word_ended = 1;
                } else {
                    /* Use grey (palette 1) for blanks, white (palette 0) for normal */
                    int pal = (h->blanks & (1 << j)) ? 1 : 0;
                    draw_char(HISTORY_COL + 1 + j, y, c, pal);
                }
            }
            /* Score (3 digits) */
            draw_number_3d(HISTORY_COL + 10, y, h->score, 0);
            /* Space */
            draw_char(HISTORY_COL + 13, y, ' ', 0);
            /* Equity (3 hex digits) */
            draw_hex_n(HISTORY_COL + 14, y, (uint32_t)(uint16_t)h->equity, 3, 0);
            /* Space */
            draw_char(HISTORY_COL + 17, y, ' ', 0);
            /* Frames (4 hex digits) */
            draw_hex_n(HISTORY_COL + 18, y, h->frames, 4, 0);
        } else {
            /* Clear empty rows */
            for (int j = HISTORY_COL; j < 40; j++) {
                draw_char(j, y, ' ', 0);
            }
        }
    }
}

/* Main display update - now with history instead of moves */
void update_display(const GameState *game, const void *history, int history_count, uint32_t move_frames) {
    (void)move_frames;  /* No longer displayed in header */
    wait_vblank();
    draw_board(&game->board);
    draw_scores(game);
    draw_rack(&game->players[game->current_player].rack);
    draw_history((const HistoryEntry *)history, history_count);
}

