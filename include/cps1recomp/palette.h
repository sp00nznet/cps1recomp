/*
 * palette.h — CPS1 palette system.
 *
 * CPS1 uses 12-bit RGB color (4 bits per channel, 4096 possible colors).
 * 192 palettes of 16 colors each, stored in GFX RAM.
 *
 * Color format (16 bits):
 *   Bits 15-12: Bright/unused (varies by CPS-B version)
 *   Bits 11-8:  Red   (R3-R0)
 *   Bits 7-4:   Green (G3-G0)
 *   Bits 3-0:   Blue  (B3-B0)
 *
 * Color 0 of each palette is transparent.
 */

#ifndef CPS1RECOMP_PALETTE_H
#define CPS1RECOMP_PALETTE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CPS1_NUM_PALETTES     192
#define CPS1_COLORS_PER_PAL   16
#define CPS1_TOTAL_COLORS     (CPS1_NUM_PALETTES * CPS1_COLORS_PER_PAL)

int palette_init(void);
void palette_shutdown(void);

/* Update a palette entry. offset is byte offset into palette area of GFX RAM. */
void palette_write(uint32_t offset, uint16_t val);
uint16_t palette_read(uint32_t offset);

/* Convert CPS1 16-bit color to ARGB8888. */
uint32_t palette_cps1_to_argb(uint16_t color);

/* Get pre-converted ARGB lookup table (CPS1_TOTAL_COLORS entries). */
const uint32_t *palette_get_argb_table(void);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_PALETTE_H */
