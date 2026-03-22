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

    /* In the deinterleaved GFX data, tiles are organized by ROM group.
     * The data has two halves per group: bp01 data, then bp23 data.
     * Each ROM group produces (rom_size * 2) bytes of bp01 and bp23.
     *
     * For tile lookup, we treat the data as a flat array of 8x8 tiles.
     * Each tile = 64 bytes in the interleaved format:
     *   32 bytes from bp01 section + 32 bytes from bp23 section.
     *
     * Actually, with our deinterleaving scheme, bp01 and bp23 are
     * stored sequentially per group. Let's use a simpler approach:
     * treat each 8x8 tile as having its data distributed across the
     * two halves of each ROM group.
     *
     * For now, use a direct 4bpp packed format approach.
     * Each pixel = 4 bits, each row = 4 bytes (8 pixels * 4 bits = 32 bits).
     * Each 8x8 tile = 32 bytes.
     */

    /* Group size: each group has 4 ROMs of 512KB each = 2MB per group,
     * producing 2MB of bp01 + 2MB of bp23 = 4MB per group.
     * With 3 groups, total = 12MB.
     * But our deinterleaved data is: [group0_bp01, group0_bp23, group1_bp01, ...] */
    uint32_t rom_pair_size = 0x100000;  /* 2 * 512KB = 1MB per interleaved pair */
    uint32_t group_total = rom_pair_size * 2;  /* bp01 + bp23 = 2MB per group */
    uint32_t total_groups = s_gfx_size / group_total;
    if (total_groups == 0) total_groups = 1;

    /* Each 8x8 tile in the interleaved data:
     * Tiles are numbered sequentially within each ROM pair.
     * Each tile has 16 bytes in each pair (8 rows * 2 bytes per row).
     * bp01 pair: 16 bytes per tile (2 bytes per row * 8 rows)
     * bp23 pair: 16 bytes per tile */
    uint32_t tiles_per_pair = rom_pair_size / 16;

    /* Which group and local tile index? */
    uint32_t group = (tile_num / tiles_per_pair);
    uint32_t local_tile = tile_num % tiles_per_pair;

    if (group >= total_groups) return;

    uint32_t bp01_base = group * group_total;
    uint32_t bp23_base = bp01_base + rom_pair_size;
    uint32_t tile_off = local_tile * 16;

    if (bp23_base + tile_off + 16 > s_gfx_size) return;

    const uint8_t *bp01 = s_gfx_data + bp01_base + tile_off;
    const uint8_t *bp23 = s_gfx_data + bp23_base + tile_off;

    int pal_base = palette_idx * CPS1_COLORS_PER_PAL;

    for (int row = 0; row < 8; row++) {
        int src_row = flip_y ? (7 - row) : row;
        int py = y + row;
        if (py < 0 || py >= CPS1_SCREEN_HEIGHT) continue;

        /* Each row: 2 bytes from bp01 (interleaved from two ROMs) */
        uint8_t b0 = bp01[src_row * 2 + 0];  /* ROM even: bp0 bits for 8 pixels */
        uint8_t b1 = bp01[src_row * 2 + 1];  /* ROM odd: bp1 bits for 8 pixels */
        uint8_t b2 = bp23[src_row * 2 + 0];  /* ROM even: bp2 bits for 8 pixels */
        uint8_t b3 = bp23[src_row * 2 + 1];  /* ROM odd: bp3 bits for 8 pixels */

        for (int col = 0; col < 8; col++) {
            int src_col = flip_x ? (7 - col) : col;
            int px = x + col;
            if (px < 0 || px >= CPS1_SCREEN_WIDTH) continue;

            int bit = 7 - src_col;  /* MSB first */

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
 * Draw a 16x16 tile (composed of four 8x8 tiles).
 * CPS1 16x16 tiles are made of 4 consecutive 8x8 tiles:
 *   tile+0: top-left      tile+1: top-right
 *   tile+2: bottom-left   tile+3: bottom-right
 */
static void draw_16x16_tile(
    uint32_t *fb,
    uint32_t tile_num,
    int palette_idx,
    bool flip_x, bool flip_y,
    int x, int y,
    const uint32_t *argb)
{
    uint32_t base = tile_num * 4;

    if (!flip_x && !flip_y) {
        draw_8x8_tile(fb, base + 0, palette_idx, false, false, x,     y,     argb);
        draw_8x8_tile(fb, base + 1, palette_idx, false, false, x + 8, y,     argb);
        draw_8x8_tile(fb, base + 2, palette_idx, false, false, x,     y + 8, argb);
        draw_8x8_tile(fb, base + 3, palette_idx, false, false, x + 8, y + 8, argb);
    } else if (flip_x && !flip_y) {
        draw_8x8_tile(fb, base + 1, palette_idx, true, false, x,     y,     argb);
        draw_8x8_tile(fb, base + 0, palette_idx, true, false, x + 8, y,     argb);
        draw_8x8_tile(fb, base + 3, palette_idx, true, false, x,     y + 8, argb);
        draw_8x8_tile(fb, base + 2, palette_idx, true, false, x + 8, y + 8, argb);
    } else if (!flip_x && flip_y) {
        draw_8x8_tile(fb, base + 2, palette_idx, false, true, x,     y,     argb);
        draw_8x8_tile(fb, base + 3, palette_idx, false, true, x + 8, y,     argb);
        draw_8x8_tile(fb, base + 0, palette_idx, false, true, x,     y + 8, argb);
        draw_8x8_tile(fb, base + 1, palette_idx, false, true, x + 8, y + 8, argb);
    } else { /* flip_x && flip_y */
        draw_8x8_tile(fb, base + 3, palette_idx, true, true, x,     y,     argb);
        draw_8x8_tile(fb, base + 2, palette_idx, true, true, x + 8, y,     argb);
        draw_8x8_tile(fb, base + 1, palette_idx, true, true, x,     y + 8, argb);
        draw_8x8_tile(fb, base + 0, palette_idx, true, true, x + 8, y + 8, argb);
    }
}

/* ----- Scroll Layer Rendering ----- */

/*
 * Render Scroll 1 (8x8 tile layer, typically HUD/text).
 *
 * Scroll 1 tilemap in GFX RAM:
 *   Each entry = 16 bits: [tile_number:12][palette:4] or similar
 *   The layout is 64x64 tiles wrapping.
 *   Scroll offsets from CPS-A registers control the viewport.
 */
/*
 * Helper: extract GFX RAM base address from a CPS-A base register value.
 * The register value is shifted left 8 and masked to the GFX RAM size.
 * Example: $90C0 -> ($90C0 << 8) & $2FFFF = $0C000
 */
static inline uint32_t gfxram_base_from_reg(uint16_t reg_val) {
    return ((uint32_t)reg_val << 8) & (CPS1_GFXRAM_SIZE - 1);
}

static void render_scroll1(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_SCROLL1_BASE / 2];
    uint32_t tilemap_base = gfxram_base_from_reg(base_reg);

    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL1_X / 2];
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

            /* Tilemap address: base + (col * 64 + row) * 2 (CPS1 uses column-major) */
            uint32_t map_offset = tilemap_base + ((uint32_t)(map_col * 64 + map_row) * 2);
            uint16_t entry = gfxram_read16(map_offset);

            /* Entry format: tile[15:6] | palette[5:1] | flip_x[0]
             * Actually CPS1 scroll1 format varies. Common format:
             *   bits 15-0: tile number (can be up to 16 bits for scroll1 8x8) */
            uint16_t tile_num = entry & 0x1FFF;  /* 13-bit tile number */
            int palette_idx = (entry >> 13) & 0x7;  /* 3-bit palette (from CPS-B?) */

            if (tile_num == 0) continue;

            int px = col * 8 - off_x;
            int py = row * 8 - off_y;

            draw_8x8_tile(fb, tile_num, palette_idx, false, false, px, py, argb);
        }
    }
}

/*
 * Render Scroll 2 (16x16 tile layer, main playfield).
 */
static void render_scroll2(uint32_t *fb, const uint32_t *argb) {
    uint16_t base_reg = s_cps_a[CPS_A_SCROLL2_BASE / 2];
    uint32_t tilemap_base = gfxram_base_from_reg(base_reg);

    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL2_X / 2];
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
             * Word 1: [palette:5][flip_y:1][flip_x:1][???:9] (varies by CPS-B) */
            uint16_t tile_num = word0;
            int palette_idx = (word1 >> 5) & 0x1F;  /* 5-bit palette */
            bool flip_x = (word1 & 0x10) != 0;
            bool flip_y = (word1 & 0x20) != 0;

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

    int scroll_x = (int16_t)s_cps_a[CPS_A_SCROLL3_X / 2];
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
            int palette_idx = (word1 >> 5) & 0x1F;
            bool flip_x = (word1 & 0x10) != 0;
            bool flip_y = (word1 & 0x20) != 0;

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

        int palette_idx = (w2 >> 5) & 0x1F;
        bool flip_x = (w2 & 0x10) != 0;
        bool flip_y = (w2 & 0x20) != 0;

        draw_16x16_tile(fb, tile_num, palette_idx, flip_x, flip_y, x, y, argb);
    }
}

/* ----- Frame Rendering ----- */

void video_render_frame(uint32_t *framebuffer) {
    const uint32_t *argb = palette_get_argb_table();

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
