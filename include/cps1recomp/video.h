/*
 * video.h — CPS1 graphics system (CPS-A / CPS-B ASICs).
 *
 * The CPS1 has a layered 2D graphics system with:
 *   - Scroll 1: 8x8 tile layer (typically used for HUD/text)
 *   - Scroll 2: 16x16 tile layer (main playfield / stage)
 *   - Scroll 3: 16x16 tile layer (background / parallax)
 *   - Object layer: Up to 256 sprites (16x16 base, 1x1 to 4x4 tiles)
 *   - Star field: Not used in SF2
 *
 * Layer ordering is controlled by CPS-B priority registers.
 * Resolution: 384x224 pixels.
 *
 * GFX RAM ($900000-$92FFFF) contains:
 *   - Scroll layer tilemaps
 *   - Sprite table (object list)
 *   - Palette data (192 palettes x 16 colors)
 *
 * Tile graphics come from decoded GFX ROMs, stored in 4bpp planar format.
 */

#ifndef CPS1RECOMP_VIDEO_H
#define CPS1RECOMP_VIDEO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Constants ----- */

#define CPS1_SCREEN_WIDTH   384
#define CPS1_SCREEN_HEIGHT  224
#define CPS1_MAX_SPRITES    256
#define CPS1_GFXRAM_SIZE    0x30000  /* 192 KB */

/* ----- Initialization ----- */

int video_init(void);
void video_shutdown(void);

/* ----- GFX ROM Loading ----- */

/*
 * Set the decoded GFX tile data.
 * data: pointer to decoded 4bpp tile data (from ROM loader)
 * size: total size in bytes
 */
void video_set_gfx_data(const uint8_t *data, uint32_t size);

/* ----- GFX RAM Access (called by bus on $900000-$92FFFF) ----- */

uint16_t video_gfxram_read(uint32_t offset);
void video_gfxram_write(uint32_t offset, uint16_t val);

/* ----- CPS-A Register Access ($800000-$800139) ----- */

void video_write_cps_a(uint32_t offset, uint16_t val);
uint16_t video_read_cps_a(uint32_t offset);

/* ----- CPS-B Register Access ($800140-$8001FF) ----- */

void video_write_cps_b(uint32_t offset, uint16_t val);
uint16_t video_read_cps_b(uint32_t offset);

/* ----- Rendering ----- */

/*
 * Render the current frame to the framebuffer.
 *
 * Composites scroll1, scroll2, scroll3, and sprites according to
 * the CPS-B layer priority settings. Call once per VBlank.
 *
 * framebuffer: 384x224 array of ARGB8888 pixels.
 */
void video_render_frame(uint32_t *framebuffer);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_VIDEO_H */
