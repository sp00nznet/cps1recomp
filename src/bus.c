/*
 * bus.c — CPS1 memory bus implementation.
 *
 * Routes 68000 memory accesses to the appropriate hardware subsystem.
 * All values are stored big-endian in RAM arrays and converted on access.
 */

#include <cps1recomp/bus.h>
#include <cps1recomp/video.h>
#include <cps1recomp/palette.h>
#include <cps1recomp/io.h>
#include <cps1recomp/z80.h>
#include <cps1recomp/timer.h>
#include <cps1recomp/debug.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Memory regions */
static uint8_t *s_rom = NULL;       /* Program ROM (up to 4 MB) */
static uint32_t s_rom_size = 0;
static uint8_t s_wram[0x10000];     /* 64 KB work RAM ($FF0000-$FFFFFF) */

/*
 * VBlank sync hook: called when the game's main loop reads the VBlank
 * flag at $FF020E (= -$7DF2 offset from A5=$FF8000).
 * This allows us to yield to the frame loop, render, and present.
 */
static void (*s_vblank_hook)(void) = NULL;
static bool s_vblank_hook_armed = true;
void bus_set_vblank_hook(void (*hook)(void)) { s_vblank_hook = hook; }
void bus_vblank_hook_arm(void) { s_vblank_hook_armed = true; }
void bus_vblank_hook_disarm(void) { s_vblank_hook_armed = false; }

#define VBLANK_FLAG_ADDR 0x020E  /* Offset in Work RAM */

int bus_init(void) {
    memset(s_wram, 0, sizeof(s_wram));
    return 0;
}

void bus_shutdown(void) {
    free(s_rom);
    s_rom = NULL;
    s_rom_size = 0;
}

/* Called by ROM loader to install the program ROM. */
void bus_set_rom(uint8_t *rom_data, uint32_t size) {
    s_rom = rom_data;
    s_rom_size = size;
}

/* --- Big-endian helpers --- */

static inline uint16_t be_read16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static inline void be_write16(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)(val >> 8);
    p[1] = (uint8_t)val;
}

static inline uint32_t be_read32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | p[3];
}

static inline void be_write32(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)(val >> 24);
    p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);
    p[3] = (uint8_t)val;
}

/* --- Address-decoded bus access --- */

static int s_read8_count = 0;

uint8_t bus_read8(uint32_t addr) {
    addr &= 0xFFFFFF;  /* 24-bit address bus */
    s_read8_count++;
    if (s_read8_count <= 3 && addr >= 0xFF0000) {
        fprintf(stderr, "[bus] read8 #%d: $%06X\n", s_read8_count, addr);
        fflush(stderr);
    }

    /* Program ROM (unmapped ROM space reads as 0, matching MAME) */
    if (addr < s_rom_size) {
        return s_rom[addr];
    }
    if (addr < 0x400000) {
        return 0;  /* Unmapped ROM space */
    }

    /* Input / DIP ports ($800000-$80001F) — these are NOT CPS-A registers.
     * SF2 reads several of them as bytes (e.g. loc_001C4E reads DIP A/B and the
     * system byte at $80001A/$80001C/$80001E); routing them to video_read_cps_a
     * returns garbage, which mis-selects attract scene paths. Mirror bus_read16. */
    if (addr >= 0x800000 && addr < 0x800020) {
        uint16_t val;
        switch (addr & ~1u) {
            case 0x800000: case 0x800006: val = io_read_player1(); break;
            case 0x800008: case 0x800018: val = io_read_player2(); break;
            case 0x80001A: val = io_read_dsw();    break;
            case 0x80001C: val = io_read_system(); break;
            case 0x80001E: val = io_read_dsw();    break;
            default:       val = io_read_system(); break;
        }
        return (uint8_t)val;
    }

    /* CPS-A registers */
    if (addr >= 0x800000 && addr < 0x800140) {
        uint32_t offset = addr - 0x800000;
        /* Byte access to word registers */
        uint16_t val = video_read_cps_a(offset & ~1);
        return (addr & 1) ? (uint8_t)val : (uint8_t)(val >> 8);
    }

    /* CPS-B registers */
    if (addr >= 0x800140 && addr < 0x800200) {
        uint32_t offset = addr - 0x800140;
        uint16_t val = video_read_cps_b(offset & ~1);
        return (addr & 1) ? (uint8_t)val : (uint8_t)(val >> 8);
    }

    /* GFX RAM */
    if (addr >= 0x900000 && addr < 0x930000) {
        uint32_t offset = addr - 0x900000;
        uint16_t val = video_gfxram_read(offset & ~1);
        return (addr & 1) ? (uint8_t)val : (uint8_t)(val >> 8);
    }

    /* Work RAM */
    if (addr >= 0xFF0000) {
        uint32_t offset = addr & 0xFFFF;
        /* VBlank flag intercept: only fire when armed (auto-disarms) */
        if (offset == VBLANK_FLAG_ADDR && s_vblank_hook && s_vblank_hook_armed) {
            s_vblank_hook_armed = false;
            s_vblank_hook();
        }
        return s_wram[offset];
    }

    debug_log("[bus] Unmapped read8: $%06X\n", addr);
    return 0xFF;
}

uint16_t bus_read16(uint32_t addr) {
    addr &= 0xFFFFFF;

    if (addr < s_rom_size) {
        return be_read16(s_rom + addr);
    }
    if (addr < 0x400000) {
        return 0;  /* Unmapped ROM space */
    }

    /* CPS1 I/O and register space ($800000-$8001FF) */
    if (addr >= 0x800000 && addr < 0x800200) {
        uint32_t offset = addr - 0x800000;

        /* Input ports (active during reads) — SF2 layout */
        switch (addr) {
            case 0x800000: return io_read_player1();     /* P1 direction + punches */
            case 0x800006: return io_read_player1();     /* P1 alt read */
            case 0x800008: return io_read_player2();     /* P2 direction + punches */
            case 0x800018: return io_read_player2();     /* P2 alt */
            case 0x80001A: return io_read_dsw();         /* DIP switches A */
            case 0x80001C: return io_read_system();      /* Coins + starts */
            case 0x80001E: return io_read_dsw();         /* DIP switches B */
            case 0x800176: return io_read_extra();       /* SF2 kick buttons (CPS-B mapped) */
        }

        /* Sound latch read ($800180-$800187) */
        if (offset >= 0x180 && offset < 0x188) {
            return z80_read_reply();
        }

        /* CPS-B registers ($800140-$8001FF) */
        if (offset >= 0x140) {
            return video_read_cps_b(offset - 0x140);
        }

        /* CPS-A registers ($800000-$80013F) */
        return video_read_cps_a(offset);
    }

    /* GFX RAM (includes palette area) */
    if (addr >= 0x900000 && addr < 0x930000) {
        return video_gfxram_read(addr - 0x900000);
    }

    /* Work RAM */
    if (addr >= 0xFF0000) {
        return be_read16(s_wram + (addr & 0xFFFF));
    }

    debug_log("[bus] Unmapped read16: $%06X\n", addr);
    return 0xFFFF;
}

uint32_t bus_read32(uint32_t addr) {
    return ((uint32_t)bus_read16(addr) << 16) | bus_read16(addr + 2);
}

void bus_write8(uint32_t addr, uint8_t val) {
    addr &= 0xFFFFFF;

    /* CPS register space — byte writes to $800000-$8001FF */
    if (addr >= 0x800000 && addr < 0x800200) {
        /* Byte writes to word registers: write to appropriate byte of the word.
         * CPS1 hardware treats odd-address writes as the low byte. */
        uint32_t offset = addr - 0x800000;

        /* Sound latch */
        if (offset >= 0x180 && offset < 0x188) {
            z80_send_command(val);
            return;
        }

        /* CPS-A byte write: route through the 16-bit write interface */
        if (offset < 0x140) {
            uint16_t cur = video_read_cps_a(offset & ~1u);
            if (offset & 1) {
                cur = (cur & 0xFF00) | val;
            } else {
                cur = ((uint16_t)val << 8) | (cur & 0x00FF);
            }
            video_write_cps_a(offset & ~1u, cur);
            return;
        }
        return;
    }

    /* GFX RAM byte write */
    if (addr >= 0x900000 && addr < 0x930000) {
        uint32_t offset = addr - 0x900000;
        /* Construct a 16-bit write from the byte */
        uint16_t cur = video_gfxram_read(offset & ~1u);
        if (addr & 1) {
            cur = (cur & 0xFF00) | val;        /* Low byte */
        } else {
            cur = ((uint16_t)val << 8) | (cur & 0x00FF);  /* High byte */
        }
        video_gfxram_write(offset & ~1u, cur);
        /* Update palette if in palette region (same as write16 path) */
        if (addr >= 0x920000 && addr < 0x920C00) {
            palette_write((addr & ~1u) - 0x920000, cur);
        }
        return;
    }

    /* Work RAM */
    if (addr >= 0xFF0000) {
        s_wram[addr & 0xFFFF] = val;
        return;
    }

    debug_log("[bus] Unmapped write8: $%06X = $%02X\n", addr, val);
}

void bus_write16(uint32_t addr, uint16_t val) {
    addr &= 0xFFFFFF;

    debug_trace_mem_write(addr, val, 2);

    /* CPS1 register space ($800000-$8001FF) */
    if (addr >= 0x800000 && addr < 0x800200) {
        uint32_t offset = addr - 0x800000;

        /* Sound latch write ($800180-$800187) — check before CPS-B */
        if (offset >= 0x180 && offset < 0x188) {
            z80_send_command((uint8_t)val);
            return;
        }

        /* CPS-B registers ($800140-$8001FF) */
        if (offset >= 0x140) {
            video_write_cps_b(offset - 0x140, val);
            return;
        }

        /* CPS-A registers ($800000-$80013F) — includes $800100+ layout regs */
        video_write_cps_a(offset, val);
        return;
    }

    /* GFX RAM */
    if (addr >= 0x900000 && addr < 0x930000) {
        uint32_t offset = addr - 0x900000;

        /* Palette area is typically at a CPS-A configured offset within GFX RAM */
        video_gfxram_write(offset, val);

        /* Also update palette if in the palette region */
        /* The palette base is configured via CPS-A registers; for SF2 it's $920000 */
        if (addr >= 0x920000 && addr < 0x920C00) {
            palette_write(addr - 0x920000, val);
        }
        return;
    }

    /* Work RAM */
    if (addr >= 0xFF0000) {
        be_write16(s_wram + (addr & 0xFFFF), val);
        return;
    }

    debug_log("[bus] Unmapped write16: $%06X = $%04X\n", addr, val);
}

void bus_write32(uint32_t addr, uint32_t val) {
    bus_write16(addr, (uint16_t)(val >> 16));
    bus_write16(addr + 2, (uint16_t)val);
}

/* --- Fast Work RAM accessors --- */

uint8_t bus_wram_read8(uint32_t offset) {
    offset &= 0xFFFF;

    /* VBlank flag intercept: when the main loop reads this byte,
     * we yield to the frame loop (render, present, sync). */
    if (offset == VBLANK_FLAG_ADDR && s_vblank_hook) {
        s_vblank_hook();
    }

    return s_wram[offset];
}

uint16_t bus_wram_read16(uint32_t offset) {
    return be_read16(s_wram + (offset & 0xFFFF));
}

uint32_t bus_wram_read32(uint32_t offset) {
    return be_read32(s_wram + (offset & 0xFFFF));
}

void bus_wram_write8(uint32_t offset, uint8_t val) {
    s_wram[offset & 0xFFFF] = val;
}

void bus_wram_write16(uint32_t offset, uint16_t val) {
    be_write16(s_wram + (offset & 0xFFFF), val);
}

void bus_wram_write32(uint32_t offset, uint32_t val) {
    be_write32(s_wram + (offset & 0xFFFF), val);
}

/* --- Direct pointers --- */

const uint8_t *bus_get_rom_ptr(void) { return s_rom; }
uint32_t bus_get_rom_size(void) { return s_rom_size; }
uint8_t *bus_get_wram_ptr(void) { return s_wram; }
