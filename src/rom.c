/*
 * rom.c — CPS1 multi-ROM loader.
 *
 * Loads assembled ROM files produced by tools/extract_roms.py:
 *   sf2_68k.bin  — Assembled 68K program
 *   sf2_gfx.bin  — Deinterleaved GFX tile data
 *   sf2_z80.bin  — Z80 sound program
 *   sf2_oki.bin  — OKI ADPCM samples
 */

#include <cps1recomp/rom.h>
#include <cps1recomp/video.h>
#include <cps1recomp/z80.h>
#include <cps1recomp/oki6295.h>
#include <cps1recomp/debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Forward declaration for bus ROM installation */
extern void bus_set_rom(uint8_t *rom_data, uint32_t size);

/* SF2 USA Rev A ROM set definition */
const cps1_romset_t SF2_ROMSET = {
    .name = "sf2",
    .prog_roms = {
        { "sf2u.30a", 0x00000, 0x20000 },
        { "sf2u.37a", 0x20000, 0x20000 },
        { "sf2u.31a", 0x40000, 0x20000 },
        { "sf2u.35a", 0x60000, 0x20000 },
        { "sf2u.38a", 0x80000, 0x20000 },
    },
    .num_prog_roms = 5,
    .gfx_roms = {
        { "sf2_06.bin", 0x80000 }, { "sf2_08.bin", 0x80000 },
        { "sf2_05.bin", 0x80000 }, { "sf2_07.bin", 0x80000 },
        { "sf2_15.bin", 0x80000 }, { "sf2_17.bin", 0x80000 },
        { "sf2_14.bin", 0x80000 }, { "sf2_16.bin", 0x80000 },
        { "sf2_25.bin", 0x80000 }, { "sf2_27.bin", 0x80000 },
        { "sf2_24.bin", 0x80000 }, { "sf2_26.bin", 0x80000 },
    },
    .num_gfx_roms = 12,
    .z80_rom = "sf2_09.bin",
    .z80_rom_size = 0x10000,
    .oki_roms = {
        { "sf2_18.bin", 0x20000 },
        { "sf2_19.bin", 0x20000 },
    },
    .num_oki_roms = 2,
    .total_prog_size = 0xA0000,
    .total_gfx_size  = 0x600000,
    .total_oki_size  = 0x40000,
};

/* Static data for ROM files */
static uint8_t *s_gfx_data = NULL;
static uint8_t *s_oki_data = NULL;

static uint8_t *load_file(const char *path, uint32_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) { fclose(f); return NULL; }

    fread(data, 1, size, f);
    fclose(f);

    if (out_size) *out_size = (uint32_t)size;
    return data;
}

static char *path_join(const char *dir, const char *file) {
    size_t dlen = strlen(dir);
    size_t flen = strlen(file);
    /* +2 for separator and null terminator */
    char *path = (char *)malloc(dlen + flen + 2);
    if (!path) return NULL;
    memcpy(path, dir, dlen);
    /* Add separator if needed */
    if (dlen > 0 && dir[dlen-1] != '/' && dir[dlen-1] != '\\') {
        path[dlen] = '/';
        dlen++;
    }
    memcpy(path + dlen, file, flen + 1);
    return path;
}

int rom_load(const char *path, const cps1_romset_t *romset) {
    printf("[rom] Loading ROM set '%s' from: %s\n", romset->name, path);

    /*
     * The path can be either:
     * 1. A directory containing pre-extracted ROM files (from extract_roms.py)
     *    Expected files: sf2_68k.bin, sf2_gfx.bin, sf2_z80.bin, sf2_oki.bin
     * 2. A zip file (handled by the Python tool first)
     */

    /* Try loading pre-extracted files from the directory */
    char *prog_path = path_join(path, "sf2_68k.bin");
    char *gfx_path  = path_join(path, "sf2_gfx.bin");
    char *z80_path  = path_join(path, "sf2_z80.bin");
    char *oki_path  = path_join(path, "sf2_oki.bin");

    /* --- 68K Program ROM --- */
    uint32_t prog_size = 0;
    uint8_t *prog_data = load_file(prog_path, &prog_size);
    if (prog_data) {
        printf("[rom] 68K program: %u KB\n", prog_size / 1024);
        bus_set_rom(prog_data, prog_size);
    } else {
        fprintf(stderr, "[rom] Failed to load %s\n", prog_path);
        free(prog_path); free(gfx_path); free(z80_path); free(oki_path);
        return -1;
    }

    /* --- GFX tile data --- */
    uint32_t gfx_size = 0;
    s_gfx_data = load_file(gfx_path, &gfx_size);
    if (s_gfx_data) {
        printf("[rom] GFX tiles: %u KB\n", gfx_size / 1024);
        video_set_gfx_data(s_gfx_data, gfx_size);
    } else {
        fprintf(stderr, "[rom] WARNING: No GFX data (%s)\n", gfx_path);
    }

    /* --- Z80 sound program --- */
    uint32_t z80_size = 0;
    uint8_t *z80_data = load_file(z80_path, &z80_size);
    if (z80_data) {
        printf("[rom] Z80 program: %u KB\n", z80_size / 1024);
        z80_load_rom(z80_data, z80_size);
        free(z80_data);
    } else {
        fprintf(stderr, "[rom] WARNING: No Z80 ROM (%s)\n", z80_path);
    }

    /* --- OKI ADPCM samples --- */
    uint32_t oki_size = 0;
    s_oki_data = load_file(oki_path, &oki_size);
    if (s_oki_data) {
        printf("[rom] OKI samples: %u KB\n", oki_size / 1024);
        oki6295_set_rom(s_oki_data, oki_size);
    } else {
        fprintf(stderr, "[rom] WARNING: No OKI data (%s)\n", oki_path);
    }

    free(prog_path);
    free(gfx_path);
    free(z80_path);
    free(oki_path);

    printf("[rom] ROM loading complete\n");
    return 0;
}

void rom_shutdown(void) {
    free(s_gfx_data);
    s_gfx_data = NULL;
    free(s_oki_data);
    s_oki_data = NULL;
}
