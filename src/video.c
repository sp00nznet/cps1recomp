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
#define CPS_A_SCROLL1_BASE 0x100  /* Scroll 1 tilemap base in GFX RAM */
#define CPS_A_SCROLL2_BASE 0x102  /* Scroll 2 tilemap base */
#define CPS_A_SCROLL3_BASE 0x104  /* Scroll 3 tilemap base */
#define CPS_A_OBJ_BASE     0x106  /* Object (sprite) base */
#define CPS_A_OTHER_BASE   0x108  /* Other (palette, row scroll) */
#define CPS_A_PALETTE_CTRL 0x10A  /* Palette control */
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

static void render_scroll1(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_SCROLL1_BASE / 2];
    uint32_t tilemap_base = gfxram_base_from_reg(base_reg);

    /* CPS1 scroll registers have inherent hardware offsets */
    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL1_X / 2] + 0x40;
    int scroll_y = (int16_t)s_cps_a[CPS_A_SCROLL1_Y / 2];

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

            /* 4 bytes per entry (2 words), column-major */
            uint32_t map_offset = tilemap_base + ((uint32_t)(map_col * 64 + map_row) * 4);
            uint16_t word0 = gfxram_read16(map_offset);
            uint16_t word1 = gfxram_read16(map_offset + 2);

            uint16_t tile_num = word0;
            int palette_idx = word1 & 0x1F;
            bool flip_x = (word1 & 0x20) != 0;
            bool flip_y = (word1 & 0x40) != 0;

            if (tile_num == 0) continue;

            int px = col * 8 - off_x;
            int py = row * 8 - off_y;

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

            uint32_t map_offset = tilemap_base + ((uint32_t)(map_col * 64 + map_row) * 4);
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
static void render_scroll3(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_SCROLL3_BASE / 2];
    uint32_t tilemap_base = gfxram_base_from_reg(base_reg);

    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL3_X / 2] + 0x40;
    int scroll_y = (int16_t)s_cps_a[CPS_A_SCROLL3_Y / 2];

    int start_col = scroll_x / 16;
    int start_row = scroll_y / 16;
    int off_x = scroll_x % 16;
    int off_y = scroll_y % 16;

    for (int row = 0; row < 15; row++) {
        for (int col = 0; col < 25; col++) {
            int map_col = (start_col + col) & 63;
            int map_row = (start_row + row) & 63;

            uint32_t map_offset = tilemap_base + ((uint32_t)(map_col * 64 + map_row) * 4);
            uint16_t word0 = gfxram_read16(map_offset);
            uint16_t word1 = gfxram_read16(map_offset + 2);

            uint16_t tile_num = word0;
            int palette_idx = word1 & 0x1F;
            bool flip_x = (word1 & 0x20) != 0;
            bool flip_y = (word1 & 0x40) != 0;

            if (tile_num == 0) continue;

            int px = col * 16 - off_x;
            int py = row * 16 - off_y;

            draw_16x16_tile(fb, tile_num, palette_idx, flip_x, flip_y, px, py, argb);
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

    /* Render sprites back-to-front (sprite 255 first, 0 on top) */
    for (int spr = CPS1_MAX_SPRITES - 1; spr >= 0; spr--) {
        uint32_t entry_addr = obj_base + (uint32_t)(spr * 8);

        uint16_t w0 = gfxram_read16(entry_addr + 0);  /* Tile number */
        uint16_t w1 = gfxram_read16(entry_addr + 2);  /* Y + attributes */
        uint16_t w2 = gfxram_read16(entry_addr + 4);  /* Palette + flip */
        uint16_t w3 = gfxram_read16(entry_addr + 6);  /* X position */

        uint16_t tile_num = w0;
        if (tile_num == 0) continue;

        /* X and Y positions (9-bit, signed) */
        int x = w3 & 0x1FF;
        int y = w1 & 0x1FF;

        /* Adjust for screen (CPS1 sprites can wrap) */
        if (x >= 384) x -= 512;
        if (y >= 224) y -= 512;

        int palette_idx = w2 & 0x1F;             /* bits 0-4: palette */
        bool flip_x = (w2 & 0x20) != 0;         /* bit 5 */
        bool flip_y = (w2 & 0x40) != 0;         /* bit 6 */

        draw_16x16_tile(fb, tile_num, palette_idx, flip_x, flip_y, x, y, argb);
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

    /* Fill with black (palette entry 0 could be the backdrop) */
    uint32_t backdrop = argb[0];
    for (int i = 0; i < CPS1_SCREEN_WIDTH * CPS1_SCREEN_HEIGHT; i++) {
        framebuffer[i] = backdrop;
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

    render_scroll3(framebuffer, argb);
    render_scroll2(framebuffer, argb);
    render_sprites(framebuffer, argb);
    render_scroll1(framebuffer, argb);
}
