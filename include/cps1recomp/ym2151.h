/*
 * ym2151.h — Yamaha YM2151 (OPM) FM synthesis interface.
 *
 * The CPS1 uses a YM2151 clocked at 3.579545 MHz for FM audio.
 * This module wraps the ymfm library's OPM implementation behind
 * a C interface for use by the Z80 sound CPU.
 *
 * The Z80 accesses the YM2151 at $D000-$D001:
 *   $D000 write: Register address select
 *   $D001 write: Register data write
 *   $D000 read:  Status register (busy flag + timer flags)
 */

#ifndef CPS1RECOMP_YM2151_H
#define CPS1RECOMP_YM2151_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int ym2151_init(int sample_rate);
void ym2151_shutdown(void);

/* Register access (from Z80 bus). */
void ym2151_write(uint8_t addr, uint8_t data);
uint8_t ym2151_read(void);

/* Generate audio samples. Fills buffer with interleaved stereo int16. */
void ym2151_generate(int16_t *buffer, int num_samples);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_YM2151_H */
