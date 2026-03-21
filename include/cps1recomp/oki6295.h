/*
 * oki6295.h — OKI MSM6295 ADPCM sample playback interface.
 *
 * The CPS1 uses an OKI MSM6295 for digitized sound effects.
 * 4 independent channels of 4-bit ADPCM playback from a sample ROM.
 *
 * The Z80 accesses the OKI at $E000-$E001:
 *   $E000 write: Command register (start/stop channels, select phrase)
 *   $E000 read:  Status register (channel busy flags)
 */

#ifndef CPS1RECOMP_OKI6295_H
#define CPS1RECOMP_OKI6295_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int oki6295_init(int sample_rate);
void oki6295_shutdown(void);

/* Load OKI sample ROM data. */
void oki6295_set_rom(const uint8_t *data, uint32_t size);

/* Register access (from Z80 bus). */
void oki6295_write(uint8_t data);
uint8_t oki6295_read(void);

/* Set ROM bank (for games with banked OKI ROMs). */
void oki6295_set_bank(uint32_t bank_offset);

/* Generate audio samples. Fills buffer with mono int16. */
void oki6295_generate(int16_t *buffer, int num_samples);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_OKI6295_H */
