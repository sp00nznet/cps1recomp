/*
 * z80.h — Z80 audio CPU subsystem for CPS1.
 *
 * The CPS1's Z80 runs at 3.579545 MHz, dedicated to audio control.
 * It communicates with the 68k via a sound latch:
 *
 *   68k -> Z80: Write to $800180 (sound latch), triggers NMI on Z80
 *   Z80 -> 68k: Z80 writes reply, 68k reads from $800180
 *
 * Z80 Memory Map (CPS1):
 *   $0000-$7FFF  Sound program ROM (32 KB visible, banked)
 *   $8000-$BFFF  Z80 RAM (2 KB, mirrored)
 *   $D000-$D001  YM2151 (address + data)
 *   $D002-$D003  Not used
 *   $E000-$E001  OKI MSM6295
 *   $F000        Sound latch (command from 68k)
 *   $F002        Sound latch clear / bank switch
 *
 * Note: Functions are prefixed with z80_cpu_ to avoid symbol collision
 * with the underlying Genesis-Plus-GX Z80 interpreter core.
 */

#ifndef CPS1RECOMP_Z80_H
#define CPS1RECOMP_Z80_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int z80_cpu_init(void);
void z80_cpu_shutdown(void);

/* Load Z80 sound program ROM. */
int z80_load_rom(const uint8_t *data, uint32_t size);

/* Execute Z80 for given number of cycles (~60,192 per frame at 3.579545 MHz). */
void z80_execute(int cycles);

/* 68k -> Z80: send sound command (triggers NMI). */
void z80_send_command(uint8_t cmd);

/* 68k <- Z80: read reply byte. */
uint8_t z80_read_reply(void);

/* Reset the Z80. */
void z80_cpu_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_Z80_H */
