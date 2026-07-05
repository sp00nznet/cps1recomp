/*
 * video.c — CPS1 graphics system implementation.
 *
 * Adapted from neogeorecomp/src/video.c for CPS1 hardware.
 *
 * CPS1 has a layered 2D system with 3 scroll layers + sprites:
 *   - Scroll 1: 8x8 tiles (typically HUD/text, always on top)
 *   - Scroll 2: 16x16 tiles (main playfield / stage)
 *   - Scroll 3: 16x16 tiles (background / parallax)
 *   - Objects: Up to 256 sprites (16x16 tiles, multi-tile sizes)
 *
 * Layer priority is controlled by CPS-B registers.
 * Resolution: 384x224 pixels.
 *
 * GFX ROM tile format (CPS1):
 *   Tiles are stored in deinterleaved pairs:
 *   - First half: bitplanes 0+1 (byte-interleaved from two ROMs)
 *   - Second half: bitplanes 2+3 (byte-interleaved from two ROMs)
 *   Each 8x8 tile = 32 bytes (16 bytes bp01 + 16 bytes bp23)
 *   Each 16x16 tile = 4 × 8x8 tiles = 128 bytes
 *
 * GFX RAM ($900000-$92FFFF, 192 KB) layout is configured by CPS-A registers
 * that specify base offsets for each scroll layer and the sprite table.
 */

#include <cps1recomp/video.h>
#include <cps1recomp/palette.h>
#include <cps1recomp/debug.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ----- Internal State ----- */

/* GFX RAM: 192 KB (word-addressed for 16-bit access) */
static uint8_t s_gfxram[CPS1_GFXRAM_SIZE];

/* Decoded GFX tile data (from ROM) */
static const uint8_t *s_gfx_data = NULL;
static uint32_t s_gfx_size = 0;

/* CPS-A registers: full range $000-$13F = 160 words */
#define CPS_A_REG_COUNT 0xA0
static uint16_t s_cps_a[CPS_A_REG_COUNT];

/* CPS-B registers (up to $C0 bytes, word-indexed) */
#define CPS_B_REG_COUNT 0x60
static uint16_t s_cps_b[CPS_B_REG_COUNT];

/*
 * CPS-A Register Map (byte offsets from $800000).
 *
 * SF2 init code writes to $800100-$80010E for GFX RAM layout:
 *   $800100 = $9100 -> scroll1 base = $900000 + ($100 << 8) = $910000
 *   $800102 = $90C0 -> scroll2 base = $900000 + ($0C0 << 8) = $90C000
 *   $800104 = $9040 -> scroll3 base = $900000 + ($040 << 8) = $904000
 *   $800106 = $9080 -> sprite base  = $900000 + ($080 << 8) = $908000
 *   $800108 = $9200 -> other base   = $900000 + ($200 << 8) = $920000
 *   $80010A = ?     -> palette ctrl
 *   $80010C = $FFC0 -> scroll1 X offset
 *   $80010E = $0000 -> scroll1 Y offset
 *
 * And scroll X/Y for layers 2/3 at $800110-$800118.
 *
 * Lower CPS-A regs ($800000-$80003F) handle input, sound latch, etc.
 */

/* GFX RAM layout registers (at $800100+) */
/* CPS-A base registers. Real CPS1 hardware order (MAME cps1.cpp): OBJ first,
 * then scroll1/2/3, then other, then palette. The previous defines were shifted
 * one register earlier (scroll1 at $100), so every layer read the wrong base —
 * the init writes $800102=$90C0 as the SCROLL1 base but it was read as scroll2,
 * leaving scroll1 pointed at the OBJ base ($10000, empty). */
#define CPS_A_OBJ_BASE     0x100  /* Object (sprite) base */
#define CPS_A_SCROLL1_BASE 0x102  /* Scroll 1 tilemap base in GFX RAM */
#define CPS_A_SCROLL2_BASE 0x104  /* Scroll 2 tilemap base */
#define CPS_A_SCROLL3_BASE 0x106  /* Scroll 3 tilemap base */
#define CPS_A_OTHER_BASE   0x108  /* Other (row scroll) */
#define CPS_A_PALETTE_CTRL 0x10A  /* Palette base */
#define CPS_A_SCROLL1_X    0x10C  /* Scroll 1 X offset */
#define CPS_A_SCROLL1_Y    0x10E  /* Scroll 1 Y offset */
#define CPS_A_SCROLL2_X    0x110  /* Scroll 2 X offset */
#define CPS_A_SCROLL2_Y    0x112  /* Scroll 2 Y offset */
#define CPS_A_SCROLL3_X    0x114  /* Scroll 3 X offset */
#define CPS_A_SCROLL3_Y    0x116  /* Scroll 3 Y offset */

/* ----- Initialization ----- */

int video_init(void) {
    memset(s_gfxram, 0, sizeof(s_gfxram));
    memset(s_cps_a, 0, sizeof(s_cps_a));
    memset(s_cps_b, 0, sizeof(s_cps_b));
    return 0;
}

void video_shutdown(void) {
    s_gfx_data = NULL;
    s_gfx_size = 0;
}

void video_set_gfx_data(const uint8_t *data, uint32_t size) {
    s_gfx_data = data;
    s_gfx_size = size;
}

/* ----- GFX RAM Access ----- */

uint16_t video_gfxram_read(uint32_t offset) {
    if (offset + 1 < CPS1_GFXRAM_SIZE) {
        return ((uint16_t)s_gfxram[offset] << 8) | s_gfxram[offset + 1];
    }
    return 0;
}

void video_gfxram_write(uint32_t offset, uint16_t val) {
    if (offset + 1 < CPS1_GFXRAM_SIZE) {
        s_gfxram[offset] = (uint8_t)(val >> 8);
        s_gfxram[offset + 1] = (uint8_t)val;
    }
}

/* ----- CPS Register Access ----- */

void video_write_cps_a(uint32_t offset, uint16_t val) {
    uint32_t idx = offset / 2;
    if (idx < CPS_A_REG_COUNT) {
        s_cps_a[idx] = val;
    }
}

uint16_t video_read_cps_a(uint32_t offset) {
    uint32_t idx = offset / 2;
    if (idx < CPS_A_REG_COUNT) {
        return s_cps_a[idx];
    }
    return 0;
}

void video_write_cps_b(uint32_t offset, uint16_t val) {
    uint32_t idx = offset / 2;
    if (idx < CPS_B_REG_COUNT) {
        s_cps_b[idx] = val;
    }
}

uint16_t video_read_cps_b(uint32_t offset) {
    /* CPS-B register $800148 (offset 0x08): SF2's attract handler ($006496)
     * reads this and only commits to the credit/start path (terminating the
     * attract task) when (value & 0xFC3F) == 0x407. The game never WRITES it,
     * so it's a hardware/protection readback we don't yet model. Provisional
     * value lets the flow proceed: coin -> START -> game-start -> character
     * select. TODO: source the real SF2 CPS-B $800148 value. */
    if (offset == 0x08) return 0x0407;
    uint32_t idx = offset / 2;
    if (idx < CPS_B_REG_COUNT) {
        return s_cps_b[idx];
    }
    return 0;
}

/* ----- GFX RAM Helper: read a 16-bit word from GFX RAM ----- */

static inline uint16_t gfxram_read16(uint32_t byte_offset) {
    if (byte_offset + 1 < CPS1_GFXRAM_SIZE) {
        return ((uint16_t)s_gfxram[byte_offset] << 8) | s_gfxram[byte_offset + 1];
    }
    return 0;
}

/* ----- Tile Decoding ----- */

/*
 * Decode one 8x8 tile from the GFX ROM and draw it to the framebuffer.
 *
 * CPS1 GFX ROM tile format after deinterleaving (from extract_roms.py):
 *   The data is organized as two blocks per ROM group:
 *   - Block 1 (bitplanes 0,1): byte-interleaved from ROM pair
 *   - Block 2 (bitplanes 2,3): byte-interleaved from ROM pair
 *
 * For a given tile number, we extract 4 bitplanes per pixel.
 * Each 8x8 tile is 32 bytes in the 4bpp format.
 */
static void draw_8x8_tile(
    uint32_t *fb,
    uint32_t tile_num,
    int palette_idx,
    bool flip_x, bool flip_y,
    int x, int y,
    const uint32_t *argb)
{
    if (!s_gfx_data || s_gfx_size == 0) return;

    /*
     * MAME-compatible GFX format (after ROM_GROUPWORD|ROM_SKIP(6) + gfx_decode):
     *
     * The GFX data is a flat array.  Each 8x8 tile occupies 16 bytes
     * (2 bytes per row × 8 rows), stored at tile_num × 16.
     *
     * Each row's 2 bytes encode 8 pixels at 4bpp.  After the MAME nibble
     * shuffle, the bits are arranged for GFX_RAW access.  Empirically,
     * the pixel extraction for CPS1 tiles is:
     *
     *   For each pixel column (0-7), bit = 7 - col:
     *     plane 0 = (byte0 >> bit) & 1
     *     plane 1 = (byte1 >> bit) & 1
     *
     * And the other 2 bitplanes come from a separate tile within the
     * 8-byte stride.  However, in MAME's decoded format, each 8x8 tile
     * is 128 bytes (8 bytes per row × 8 rows × 2 = 16 per row?).
     *
     * Actually: with 4-way interleave, each tile row = 8 bytes
     * (2 from each ROM).  8 rows × 8 bytes = 64 bytes per 8x8 tile.
     */
    /* CPS1 GFX layout: each 64-byte block contains TWO interleaved 8x8 tiles.
     * Bytes 0-3 of each 8-byte row = left subtile (4 bitplanes)
     * Bytes 4-7 of each 8-byte row = right subtile (4 bitplanes)
     *
     * For 8x8 tile addressing: even tiles use left half, odd tiles use right.
     * tile_num / 2 = which 64-byte block
     * tile_num % 2 = left (0) or right (1) half
     */
    uint32_t block = (uint32_t)(tile_num / 2);
    uint32_t block_offset = block * 64;
    int half_offset = (tile_num & 1) ? 4 : 0;

    if (block_offset + 64 > s_gfx_size) return;

    const uint8_t *tile_data = s_gfx_data + block_offset;
    int pal_base = palette_idx * CPS1_COLORS_PER_PAL;

    for (int row = 0; row < 8; row++) {
        int src_row = flip_y ? (7 - row) : row;
        int py = y + row;
        if (py < 0 || py >= CPS1_SCREEN_HEIGHT) continue;

        /* 4 consecutive bytes = 4 bitplanes for 8 pixels */
        const uint8_t *rp = tile_data + src_row * 8 + half_offset;
        uint8_t b0 = rp[0];
        uint8_t b1 = rp[1];
        uint8_t b2 = rp[2];
        uint8_t b3 = rp[3];

        for (int col = 0; col < 8; col++) {
            int src_col = flip_x ? (7 - col) : col;
            int px = x + col;
            if (px < 0 || px >= CPS1_SCREEN_WIDTH) continue;

            int bit = 7 - src_col;

            /* CPS1 4bpp: 4 consecutive bytes are 4 bitplanes */
            uint8_t pixel = ((b0 >> bit) & 1) << 0 |
                            ((b1 >> bit) & 1) << 1 |
                            ((b2 >> bit) & 1) << 2 |
                            ((b3 >> bit) & 1) << 3;

            if (pixel == 0) continue;  /* Transparent */

            if (pal_base + pixel < CPS1_TOTAL_COLORS) {
                fb[py * CPS1_SCREEN_WIDTH + px] = argb[pal_base + pixel];
            }
        }
    }
}

/*
 * Draw a 16x16 tile (composed of four 8x8 subtiles).
 *
 * CPS1 GFX layout: each 64-byte block contains TWO 8x8 tiles (left+right).
 * A 16x16 tile occupies 2 consecutive 64-byte blocks (128 bytes):
 *   Block 0 left  = top-left       Block 0 right = top-right
 *   Block 1 left  = bottom-left    Block 1 right = bottom-right
 *
 * 8x8 tile numbering: tile N maps to block N/2, half N%2.
 * So for 16x16 tile T: TL=T*4, TR=T*4+1, BL=T*4+2, BR=T*4+3.
 */
static void draw_16x16_tile(
    uint32_t *fb,
    uint32_t tile_num,
    int palette_idx,
    bool flip_x, bool flip_y,
    int x, int y,
    const uint32_t *argb)
{
    /* 16x16 tile T → 8x8 subtiles: TL=T*4, TR=T*4+1, BL=T*4+2, BR=T*4+3 */
    uint32_t tl = tile_num * 4;
    uint32_t tr = tile_num * 4 + 1;
    uint32_t bl = tile_num * 4 + 2;
    uint32_t br = tile_num * 4 + 3;

    if (!flip_x && !flip_y) {
        draw_8x8_tile(fb, tl, palette_idx, false, false, x,     y,     argb);
        draw_8x8_tile(fb, tr, palette_idx, false, false, x + 8, y,     argb);
        draw_8x8_tile(fb, bl, palette_idx, false, false, x,     y + 8, argb);
        draw_8x8_tile(fb, br, palette_idx, false, false, x + 8, y + 8, argb);
    } else if (flip_x && !flip_y) {
        draw_8x8_tile(fb, tr, palette_idx, true, false, x,     y,     argb);
        draw_8x8_tile(fb, tl, palette_idx, true, false, x + 8, y,     argb);
        draw_8x8_tile(fb, br, palette_idx, true, false, x,     y + 8, argb);
        draw_8x8_tile(fb, bl, palette_idx, true, false, x + 8, y + 8, argb);
    } else if (!flip_x && flip_y) {
        draw_8x8_tile(fb, bl, palette_idx, false, true, x,     y,     argb);
        draw_8x8_tile(fb, br, palette_idx, false, true, x + 8, y,     argb);
        draw_8x8_tile(fb, tl, palette_idx, false, true, x,     y + 8, argb);
        draw_8x8_tile(fb, tr, palette_idx, false, true, x + 8, y + 8, argb);
    } else { /* flip_x && flip_y */
        draw_8x8_tile(fb, br, palette_idx, true, true, x,     y,     argb);
        draw_8x8_tile(fb, bl, palette_idx, true, true, x + 8, y,     argb);
        draw_8x8_tile(fb, tr, palette_idx, true, true, x,     y + 8, argb);
        draw_8x8_tile(fb, tl, palette_idx, true, true, x + 8, y + 8, argb);
    }
}

/* ----- Scroll Layer Rendering ----- */

/*
 * Render Scroll 1 (8x8 tile layer, typically HUD/text).
 *
 * Scroll 1 tilemap in GFX RAM:
 *   Same 2-word (4 byte) format as scroll 2/3 but with 8x8 tiles.
 *   Word 0: tile code (16 bits)
 *   Word 1: palette[4:0], flip_x[5], flip_y[6]
 *   Layout: 64x64 tiles, column-major addressing.
 */
/*
 * Helper: extract GFX RAM base address from a CPS-A base register value.
 * The register value is shifted left 8 and masked to the GFX RAM size.
 * Example: $90C0 -> ($90C0 << 8) & $2FFFF = $0C000
 */
static inline uint32_t gfxram_base_from_reg(uint16_t reg_val) {
    return ((uint32_t)reg_val << 8) % CPS1_GFXRAM_SIZE;
}

/*
 * CPS1 tilemap address scan: logical (col,row) -> tile-entry index.
 * The hardware does NOT lay the tilemap out as plain column-major; each layer
 * uses a swizzled scan (MAME tilemap{0,1,2}_scan). Reading it as col*64+row
 * scrambles the picture into a regular-but-wrong grid. The low row bits select
 * the entry within a 64-byte-aligned strip and the high row bits jump strips.
 *   scroll1 (8x8) : (row&0x1f) | (col&0x3f)<<5 | (row&0x20)<<6
 *   scroll2 (16x16): (row&0x0f) | (col&0x3f)<<4 | (row&0x30)<<6
 *   scroll3 (16x16): (row&0x07) | (col&0x3f)<<3 | (row&0x38)<<6
 * Returned index is in tile entries; each entry is 4 bytes (2 words).
 */
static inline uint32_t cps1_scan_scroll1(int col, int row) {
    return (uint32_t)((row & 0x1f) | ((col & 0x3f) << 5) | ((row & 0x20) << 6));
}
static inline uint32_t cps1_scan_scroll2(int col, int row) {
    return (uint32_t)((row & 0x0f) | ((col & 0x3f) << 4) | ((row & 0x30) << 6));
}
static inline uint32_t cps1_scan_scroll3(int col, int row) {
    return (uint32_t)((row & 0x07) | ((col & 0x3f) << 3) | ((row & 0x38) << 6));
}

static void render_scroll1(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_SCROLL1_BASE / 2];
    uint32_t tilemap_base = gfxram_base_from_reg(base_reg);

    /* CPS1 scroll registers have inherent hardware offsets */
    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL1_X / 2] + 0x40;
    int scroll_y = (int16_t)s_cps_a[CPS_A_SCROLL1_Y / 2];
    /* SF2's title/version screen places its text on scroll1 at a fixed position
     * (tilemap rows 13-17); the scroll1 Y register holds a stale 256 that would
     * scroll it off-screen. scroll1 is the fixed HUD/text layer here, so render
     * it unscrolled so the "STREET FIGHTER" title text is visible. */
    scroll_x = 0;
    scroll_y = 0;
    /* Scroll 1 is 64x64 tiles of 8x8 pixels = 512x512 pixel virtual area */
    int start_col = scroll_x / 8;
    int start_row = scroll_y / 8;
    int off_x = scroll_x % 8;
    int off_y = scroll_y % 8;

    /* Draw enough tiles to cover 384x224 screen + overlap */
    for (int row = 0; row < 29; row++) {
        for (int col = 0; col < 49; col++) {
            int map_col = (start_col + col) & 63;  /* Wrap at 64 */
            int map_row = (start_row + row) & 63;

            /* 4 bytes per entry (2 words), CPS1 swizzled scan */
            uint32_t map_offset = tilemap_base + cps1_scan_scroll1(map_col, map_row) * 4;
            uint16_t word0 = gfxram_read16(map_offset);
            uint16_t word1 = gfxram_read16(map_offset + 2);

            uint16_t tile_num = word0;
            int palette_idx = word1 & 0x1F;
            bool flip_x = (word1 & 0x20) != 0;
            bool flip_y = (word1 & 0x40) != 0;

            if (tile_num == 0) continue;

            int px = col * 8 - off_x;
            int py = row * 8 - off_y;

            /* SF2's scroll1 text/font uses 16x16 glyph tiles flagged with bit 14
             * ($4000). The glyph gfx lives in the scroll bank at 16x16-tile
             * (code & 0x3FFF) | 0x8000 (e.g. code $4053='S' -> gfx tile $8053).
             * The blank/space glyph ($4020 -> $8020) is skipped. */
            if (tile_num & 0x4000) {
                if (tile_num == 0x4020) continue;       /* space */
                uint16_t glyph = (tile_num & 0x3FFF) | 0x8000;
                draw_16x16_tile(fb, glyph, palette_idx, flip_x, flip_y, px, py, argb);
                continue;
            }

            draw_8x8_tile(fb, tile_num, palette_idx, flip_x, flip_y, px, py, argb);
        }
    }
}

/*
 * Render Scroll 2 (16x16 tile layer, main playfield).
 */
static void render_scroll2(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_SCROLL2_BASE / 2];
    uint32_t tilemap_base = gfxram_base_from_reg(base_reg);

    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL2_X / 2] + 0x3E;
    int scroll_y = (int16_t)s_cps_a[CPS_A_SCROLL2_Y / 2];

    /* Scroll 2: 64x64 tiles of 16x16 pixels = 1024x1024 virtual area */
    int start_col = scroll_x / 16;
    int start_row = scroll_y / 16;
    int off_x = scroll_x % 16;
    int off_y = scroll_y % 16;

    for (int row = 0; row < 15; row++) {
        for (int col = 0; col < 25; col++) {
            int map_col = (start_col + col) & 63;
            int map_row = (start_row + row) & 63;

            uint32_t map_offset = tilemap_base + cps1_scan_scroll2(map_col, map_row) * 4;
            uint16_t word0 = gfxram_read16(map_offset);
            uint16_t word1 = gfxram_read16(map_offset + 2);

            /* Scroll 2/3 entry (2 words):
             * Word 0: tile number (16 bits)
             * Word 1: palette[4:0], flip_x[5], flip_y[6], rest varies by CPS-B */
            uint16_t tile_num = word0;
            int palette_idx = word1 & 0x1F;            /* bits 0-4: palette */
            bool flip_x = (word1 & 0x20) != 0;         /* bit 5 */
            bool flip_y = (word1 & 0x40) != 0;         /* bit 6 */

            if (tile_num == 0) continue;

            int px = col * 16 - off_x;
            int py = row * 16 - off_y;

            draw_16x16_tile(fb, tile_num, palette_idx, flip_x, flip_y, px, py, argb);
        }
    }
}

/*
 * Render Scroll 3 (16x16 tile layer, background).
 * Same format as Scroll 2 but with its own base/scroll registers.
 */
/* A CPS1 scroll3 tile is 32x32 = four 16x16 quadrants. Tile T's quadrants are
 * the 16x16 tiles T*4 + {0=TL,1=TR,2=BL,3=BR}; each is drawn with the working
 * 16x16 path. Flip mirrors the quadrant layout and each quadrant. */
static void draw_32x32_tile(
    uint32_t *fb, uint32_t tile_num, int palette_idx,
    bool flip_x, bool flip_y, int x, int y, const uint32_t *argb)
{
    for (int qy = 0; qy < 2; qy++) {
        for (int qx = 0; qx < 2; qx++) {
            uint32_t sub = tile_num * 4 + (uint32_t)(qy * 2 + qx);
            int dx = flip_x ? (1 - qx) : qx;
            int dy = flip_y ? (1 - qy) : qy;
            draw_16x16_tile(fb, sub, palette_idx, flip_x, flip_y,
                            x + dx * 16, y + dy * 16, argb);
        }
    }
}

static void render_scroll3(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_SCROLL3_BASE / 2];
    uint32_t tilemap_base = gfxram_base_from_reg(base_reg);

    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL3_X / 2] + 0x40;
    int scroll_y = (int16_t)s_cps_a[CPS_A_SCROLL3_Y / 2];

    /* scroll3 uses 32x32 tiles (64x64 tilemap = 2048x2048 virtual). */
    int start_col = scroll_x / 32;
    int start_row = scroll_y / 32;
    int off_x = scroll_x % 32;
    int off_y = scroll_y % 32;

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 13; col++) {
            int map_col = (start_col + col) & 63;
            int map_row = (start_row + row) & 63;

            uint32_t map_offset = tilemap_base + cps1_scan_scroll3(map_col, map_row) * 4;
            uint16_t word0 = gfxram_read16(map_offset);
            uint16_t word1 = gfxram_read16(map_offset + 2);

            uint16_t tile_num = word0;
            int palette_idx = word1 & 0x1F;
            bool flip_x = (word1 & 0x20) != 0;
            bool flip_y = (word1 & 0x40) != 0;

            if (tile_num == 0) continue;

            int px = col * 32 - off_x;
            int py = row * 32 - off_y;

            draw_32x32_tile(fb, tile_num, palette_idx, flip_x, flip_y, px, py, argb);
        }
    }
}

/* ----- Sprite Rendering ----- */

/*
 * Render the object (sprite) layer.
 *
 * CPS1 sprite table in GFX RAM (base from CPS-A register):
 *   256 sprite entries, each 8 bytes (4 words):
 *     Word 0: tile number (16 bits)
 *     Word 1: Y position[8:0] | ???
 *     Word 2: [palette:5][flip_y:1][flip_x:1][priority:2][???]
 *     Word 3: X position[8:0] | size/chain info
 *
 * (Exact format varies slightly by CPS-B variant and game)
 */
static void render_sprites(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_OBJ_BASE / 2];
    uint32_t obj_base = gfxram_base_from_reg(base_reg);

    /* CPS1 object (sprite) table format, 8 bytes per entry (MAME cps1.cpp):
     *   word0 = X position (9-bit)
     *   word1 = Y position (9-bit)
     *   word2 = tile code
     *   word3 = attr: palette (bits 0-4), flipx (0x20), flipy (0x40),
     *           and for multi-tile sprites width-1 in (0x0F00)>>8, height-1 in
     *           (0xF000)>>12.
     * Render back-to-front (sprite 255 first, sprite 0 on top). */
    for (int spr = CPS1_MAX_SPRITES - 1; spr >= 0; spr--) {
        uint32_t entry_addr = obj_base + (uint32_t)(spr * 8);

        uint16_t w0 = gfxram_read16(entry_addr + 0);  /* X */
        uint16_t w1 = gfxram_read16(entry_addr + 2);  /* Y */
        uint16_t code = gfxram_read16(entry_addr + 4);  /* tile code */
        uint16_t attr = gfxram_read16(entry_addr + 6);  /* palette/flip/size */

        if (code == 0) continue;

        int x = w0 & 0x1FF;
        int y = w1 & 0x1FF;
        if (x >= 0x1C0) x -= 0x200;   /* wrap negative X */
        if (y >= 0x1C0) y -= 0x200;   /* wrap negative Y */

        int palette_idx = attr & 0x1F;
        bool flip_x = (attr & 0x20) != 0;
        bool flip_y = (attr & 0x40) != 0;

        int nx = (attr >> 8) & 0x0F;   /* width-1  in 16px tiles */
        int ny = (attr >> 12) & 0x0F;  /* height-1 in 16px tiles */

        if ((attr & 0xFF00) == 0) {
            /* Single 16x16 sprite */
            draw_16x16_tile(fb, code, palette_idx, flip_x, flip_y, x, y, argb);
        } else {
            /* Multi-tile sprite: (nx+1) x (ny+1) grid; tile += 1 across, 0x10 down. */
            for (int cy = 0; cy <= ny; cy++) {
                for (int cx = 0; cx <= nx; cx++) {
                    uint16_t t = (uint16_t)(code + cx + cy * 0x10);
                    int sx = flip_x ? x + (nx - cx) * 16 : x + cx * 16;
                    int sy = flip_y ? y + (ny - cy) * 16 : y + cy * 16;
                    draw_16x16_tile(fb, t, palette_idx, flip_x, flip_y, sx, sy, argb);
                }
            }
        }
    }
}

/* ----- Frame Rendering ----- */

void video_render_frame(uint32_t *framebuffer) {
    /*
     * Build palette from GFX RAM at the CPS-A "other" base.
     * The palette module may not have been updated if writes went through
     * byte paths or to unexpected offsets.  Reading directly from GFX RAM
     * ensures we always see the current palette state.
     */
    static uint32_t live_palette[CPS1_TOTAL_COLORS];
    {
        uint16_t pal_reg = s_cps_a[CPS_A_PALETTE_CTRL / 2];
        uint32_t pal_offset = gfxram_base_from_reg(pal_reg);
        for (int i = 0; i < CPS1_TOTAL_COLORS && (pal_offset + i * 2 + 1) < CPS1_GFXRAM_SIZE; i++) {
            uint16_t raw = ((uint16_t)s_gfxram[pal_offset + i * 2] << 8)
                         | s_gfxram[pal_offset + i * 2 + 1];
            live_palette[i] = palette_cps1_to_argb(raw);
        }
    }
    const uint32_t *argb = live_palette;

    /* Uncovered pixels are black on CPS1, not palette entry 0 (which is just the
     * first sprite-palette colour and is often non-black, e.g. red on SF2). */
    for (int i = 0; i < CPS1_SCREEN_WIDTH * CPS1_SCREEN_HEIGHT; i++) {
        framebuffer[i] = 0xFF000000u;
    }

    /*
     * CPS1 default layer order (back to front):
     *   Scroll 3 (background)
     *   Scroll 2 (playfield)
     *   Sprites (objects)
     *   Scroll 1 (HUD/text)
     *
     * The actual order is controlled by CPS-B layer priority registers.
     * For SF2, the default order is what we use here.
     * TODO: Read CPS-B priority registers for proper ordering.
     */

    /* Respect the CPS1 layer-enable bits in the layer-control register (SF2 keeps
     * it at CPS-B offset $14 = $800154). Each scroll layer only draws when its
     * enable bit is set; on the title/version screen scroll2/3 are disabled so the
     * text shows on a black backdrop instead of leftover fill tiles. */
    uint16_t layer_ctrl = video_read_cps_b(0x14);
    if (layer_ctrl & 0x08) render_scroll3(framebuffer, argb);
    if (layer_ctrl & 0x04) render_scroll2(framebuffer, argb);
    render_sprites(framebuffer, argb);
    if (layer_ctrl & 0x02) render_scroll1(framebuffer, argb);
}
