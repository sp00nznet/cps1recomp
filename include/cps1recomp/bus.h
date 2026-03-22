/*
 * bus.h — CPS1 memory bus and address space routing.
 *
 * CPS1 Memory Map (68000 perspective):
 *   $000000-$3FFFFF  Program ROM (up to 4 MB, typically 640 KB for SF2)
 *   $800000-$800139  CPS-A registers (scroll offsets, palette control, etc.)
 *   $800140-$8001FF  CPS-B registers (layer enable, priority, ID)
 *   $900000-$92FFFF  GFX RAM (scroll tilemaps + sprite table + palette)
 *   $FF0000-$FFFFFF  Work RAM (64 KB)
 *
 * Sound communication:
 *   $800180-$800187  Sound latch (68K -> Z80 command)
 *
 * The bus handles big-endian byte ordering (68000 is big-endian,
 * x86 is little-endian). All read/write functions perform the
 * necessary byte swapping transparently.
 */

#ifndef CPS1RECOMP_BUS_H
#define CPS1RECOMP_BUS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----- Initialization ----- */

int bus_init(void);
void bus_shutdown(void);

/* ----- General Bus Access (big-endian, address-decoded) ----- */

uint8_t  bus_read8(uint32_t addr);
uint16_t bus_read16(uint32_t addr);
uint32_t bus_read32(uint32_t addr);

void bus_write8(uint32_t addr, uint8_t val);
void bus_write16(uint32_t addr, uint16_t val);
void bus_write32(uint32_t addr, uint32_t val);

/* ----- Fast Work RAM Access (offset relative to $FF0000) ----- */

uint8_t  bus_wram_read8(uint32_t offset);
uint16_t bus_wram_read16(uint32_t offset);
uint32_t bus_wram_read32(uint32_t offset);

void bus_wram_write8(uint32_t offset, uint8_t val);
void bus_wram_write16(uint32_t offset, uint16_t val);
void bus_wram_write32(uint32_t offset, uint32_t val);

/* ----- VBlank Sync Hook ----- */

/*
 * Set a callback that fires when the game's main loop reads the
 * VBlank flag. This lets the runtime render a frame and sync timing
 * when the game is waiting for VBlank in an infinite loop.
 */
void bus_set_vblank_hook(void (*hook)(void));

/* Arm/disarm the vblank hook.  When armed, the next read of the VBlank
   flag address fires the hook and auto-disarms.  This prevents multiple
   hook firings within a single frame (e.g., when task code polls the flag). */
void bus_vblank_hook_arm(void);
void bus_vblank_hook_disarm(void);

/* ----- Direct Pointers (for tools/analysis) ----- */

const uint8_t *bus_get_rom_ptr(void);
uint32_t bus_get_rom_size(void);
uint8_t *bus_get_wram_ptr(void);
uint8_t *bus_get_gfxram_ptr(void);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_BUS_H */
