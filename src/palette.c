/*
 * palette.c — CPS1 12-bit RGB palette system.
 */

#include <cps1recomp/palette.h>
#include <string.h>

static uint16_t s_palram[CPS1_TOTAL_COLORS];
static uint32_t s_argb_table[CPS1_TOTAL_COLORS];

int palette_init(void) {
    memset(s_palram, 0, sizeof(s_palram));
    memset(s_argb_table, 0, sizeof(s_argb_table));
    return 0;
}

void palette_shutdown(void) {
    /* Nothing to free */
}

uint32_t palette_cps1_to_argb(uint16_t color) {
    /*
     * CPS1 color format (16 bits):
     *   Bits 11-8: Red   (4 bits)
     *   Bits 7-4:  Green (4 bits)
     *   Bits 3-0:  Blue  (4 bits)
     *   Bits 15-12: brightness/unused (game-dependent)
     *
     * Expand 4-bit channels to 8-bit by replicating: 0xR -> 0xRR
     */
    uint8_t r4 = (color >> 8) & 0xF;
    uint8_t g4 = (color >> 4) & 0xF;
    uint8_t b4 = (color >> 0) & 0xF;

    uint8_t r = (r4 << 4) | r4;
    uint8_t g = (g4 << 4) | g4;
    uint8_t b = (b4 << 4) | b4;

    /* Check bright bit (bit 15 on some CPS1 variants) */
    if (color & 0x8000) {
        /* Slight brightness boost — shift up by ~12% */
        r = (r < 224) ? r + 32 : 255;
        g = (g < 224) ? g + 32 : 255;
        b = (b < 224) ? b + 32 : 255;
    }

    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void palette_write(uint32_t offset, uint16_t val) {
    uint32_t index = offset / 2;
    if (index < CPS1_TOTAL_COLORS) {
        s_palram[index] = val;
        s_argb_table[index] = palette_cps1_to_argb(val);
    }
}

uint16_t palette_read(uint32_t offset) {
    uint32_t index = offset / 2;
    if (index < CPS1_TOTAL_COLORS) {
        return s_palram[index];
    }
    return 0;
}

const uint32_t *palette_get_argb_table(void) {
    return s_argb_table;
}
