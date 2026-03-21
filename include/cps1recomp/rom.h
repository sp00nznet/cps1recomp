/*
 * rom.h — CPS1 multi-ROM loader.
 *
 * CPS1 games ship as multiple ROM chips that must be assembled:
 *   - 68K program ROMs: multiple 128KB chips at specific addresses
 *   - GFX ROMs: interleaved bitplane data, decoded into linear tiles
 *   - Z80 ROM: sound program (one chip, typically 64-128 KB)
 *   - OKI ROMs: ADPCM sample data (multiple chips)
 *
 * This module handles extraction from a MAME-format zip, assembly
 * of the 68K program, GFX ROM deinterleaving, and loading all data
 * into the appropriate subsystems.
 */

#ifndef CPS1RECOMP_ROM_H
#define CPS1RECOMP_ROM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ROM set descriptor — defines how to assemble a CPS1 game's ROMs. */
typedef struct {
    const char *name;           /* Game name (e.g. "sf2") */

    /* 68K program ROM files and their load addresses */
    struct {
        const char *filename;
        uint32_t load_addr;
        uint32_t size;
    } prog_roms[8];
    int num_prog_roms;

    /* GFX ROM files (in order, will be deinterleaved) */
    struct {
        const char *filename;
        uint32_t size;
    } gfx_roms[32];
    int num_gfx_roms;

    /* Z80 program ROM */
    const char *z80_rom;
    uint32_t z80_rom_size;

    /* OKI sample ROMs */
    struct {
        const char *filename;
        uint32_t size;
    } oki_roms[8];
    int num_oki_roms;

    /* Total expected sizes */
    uint32_t total_prog_size;
    uint32_t total_gfx_size;
    uint32_t total_oki_size;
} cps1_romset_t;

/* Pre-defined ROM set for SF2 (USA rev A). */
extern const cps1_romset_t SF2_ROMSET;

/*
 * Load a CPS1 ROM set from a zip file or directory.
 *
 * Assembles 68K program, decodes GFX ROMs, loads Z80 and OKI data.
 * All data is loaded into the appropriate subsystems (bus, video, z80, oki).
 *
 * Returns 0 on success.
 */
int rom_load(const char *path, const cps1_romset_t *romset);

/* Free all ROM data. */
void rom_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_ROM_H */
