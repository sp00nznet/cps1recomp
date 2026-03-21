/*
 * z80.c — Z80 audio CPU for CPS1.
 *
 * Wraps the Genesis-Plus-GX Z80 interpreter core with CPS1-specific
 * memory mapping and I/O port routing.
 *
 * CPS1 Z80 Memory Map:
 *   $0000-$7FFF  Sound program ROM (32 KB visible, up to 128 KB banked)
 *   $8000-$BFFF  RAM (2 KB at $D000, mirrored — actually CPS1 uses $8000)
 *   $D000-$D001  YM2151 (address + data)
 *   $D002-$D003  Not used
 *   $E000-$E001  OKI MSM6295
 *   $F000        Sound latch (command from 68K)
 *   $F002        Bank switch / latch acknowledge
 *   $F800-$FFFF  RAM (2 KB)
 *
 * Communication:
 *   68K -> Z80: Write to sound latch ($800180), triggers NMI
 *   Z80 -> 68K: Z80 writes reply byte
 *
 * Clock: 3.579545 MHz → ~60,192 cycles per frame at 59.63 Hz
 */

#include <cps1recomp/z80.h>
#include <cps1recomp/ym2151.h>
#include <cps1recomp/oki6295.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ----- Genesis-Plus-GX Z80 core types ----- */
/* We provide our own minimal type defs to avoid needing osd_cpu.h */
#ifndef UINT8
#define UINT8  unsigned char
#define UINT16 unsigned short
#define UINT32 unsigned int
#define INT8   signed char
#define INT16  signed short
#define INT32  signed int
typedef union { struct { UINT8 l, h, h2, h3; } b; UINT16 w; UINT32 d; } PAIR;
#endif

/* Declare Z80 core functions (from Genesis-Plus-GX z80.c) */
/* We'll link against the z80.c compiled as part of our build */
typedef struct {
    PAIR pc, sp, af, bc, de, hl, ix, iy, wz;
    PAIR af2, bc2, de2, hl2;
    UINT8 r, r2, iff1, iff2, halt, im, i;
    UINT8 nmi_state, nmi_pending, irq_state, after_ei;
    UINT32 cycles;
    const void *daisy;
    int (*irq_callback)(int irqline);
} Z80_Regs;

extern Z80_Regs Z80;
extern unsigned char *z80_readmap[64];
extern unsigned char *z80_writemap[64];
extern void (*z80_writemem)(unsigned int address, unsigned char data);
extern unsigned char (*z80_readmem)(unsigned int address);
extern void (*z80_writeport)(unsigned int port, unsigned char data);
extern unsigned char (*z80_readport)(unsigned int port);

extern void z80_init(const void *config, int (*irqcallback)(int));
extern void z80_reset(void);
extern void z80_run(unsigned int cycles);
extern void z80_set_nmi_line(unsigned int state);
extern void z80_set_irq_line(unsigned int state);

#define CLEAR_LINE  0
#define ASSERT_LINE 1
#define PULSE_LINE  3

/* ----- CPS1 Z80 State ----- */

static uint8_t *s_rom = NULL;
static uint32_t s_rom_size = 0;
static uint8_t  s_ram[0x800];        /* 2 KB Z80 RAM */
static uint8_t  s_sound_latch = 0;
static uint8_t  s_reply_byte = 0;
static uint8_t  s_bank = 0;         /* ROM bank select */
static bool     s_initialized = false;

/* Dummy memory for unmapped regions */
static uint8_t s_dummy_r[0x400];
static uint8_t s_dummy_w[0x400];

/* ----- Z80 Memory Callbacks ----- */

static unsigned char cps1_z80_read(unsigned int address) {
    address &= 0xFFFF;

    /* $0000-$7FFF: ROM (banked) */
    if (address < 0x8000) {
        uint32_t rom_addr = address;
        /* Apply bank offset for addresses $4000-$7FFF (CPS1 banking varies) */
        if (address >= 0x4000 && s_bank > 0) {
            rom_addr = (uint32_t)s_bank * 0x4000 + (address - 0x4000);
        }
        if (rom_addr < s_rom_size) return s_rom[rom_addr];
        return 0xFF;
    }

    /* $D000-$D001: YM2151 */
    if (address >= 0xD000 && address <= 0xD001) {
        return ym2151_read();
    }

    /* $E000-$E001: OKI MSM6295 */
    if (address >= 0xE000 && address <= 0xE001) {
        return oki6295_read();
    }

    /* $F000: Sound latch from 68K */
    if (address >= 0xF000 && address < 0xF002) {
        return s_sound_latch;
    }

    /* $F800-$FFFF: RAM */
    if (address >= 0xF800) {
        return s_ram[address & 0x7FF];
    }

    /* $8000-$BFFF: RAM (mirrored) */
    if (address >= 0x8000 && address < 0xC000) {
        return s_ram[address & 0x7FF];
    }

    return 0xFF;
}

static void cps1_z80_write(unsigned int address, unsigned char data) {
    address &= 0xFFFF;

    /* $D000: YM2151 address register */
    if (address == 0xD000) {
        ym2151_write(0, data);
        return;
    }

    /* $D001: YM2151 data register */
    if (address == 0xD001) {
        ym2151_write(1, data);
        return;
    }

    /* $E000-$E001: OKI MSM6295 */
    if (address >= 0xE000 && address <= 0xE001) {
        oki6295_write(data);
        return;
    }

    /* $F002: Bank switch / latch acknowledge */
    if (address >= 0xF002 && address < 0xF004) {
        s_bank = data & 0x0F;
        return;
    }

    /* $F800-$FFFF: RAM */
    if (address >= 0xF800) {
        s_ram[address & 0x7FF] = data;
        return;
    }

    /* $8000-$BFFF: RAM (mirrored) */
    if (address >= 0x8000 && address < 0xC000) {
        s_ram[address & 0x7FF] = data;
        return;
    }
}

static unsigned char cps1_z80_read_port(unsigned int port) {
    (void)port;
    return 0xFF;
}

static void cps1_z80_write_port(unsigned int port, unsigned char data) {
    (void)port;
    (void)data;
}

static int cps1_z80_irq_callback(int irqline) {
    (void)irqline;
    return 0xFF;  /* Return RST 38h vector */
}

/* ----- Public Interface ----- */

int z80_cpu_init(void) {
    memset(s_ram, 0, sizeof(s_ram));
    memset(s_dummy_r, 0xFF, sizeof(s_dummy_r));
    memset(s_dummy_w, 0, sizeof(s_dummy_w));
    s_sound_latch = 0;
    s_reply_byte = 0;
    s_bank = 0;

    /* Initialize the Z80 core */
    z80_init(NULL, cps1_z80_irq_callback);

    /* Set up memory map: 64 pages of 1KB each */
    /* Default all to dummy pages */
    for (int i = 0; i < 64; i++) {
        z80_readmap[i] = s_dummy_r;
        z80_writemap[i] = s_dummy_w;
    }

    /* Map RAM at $F800-$FFFF (pages 62-63) */
    z80_readmap[62] = s_ram;
    z80_readmap[63] = s_ram;
    z80_writemap[62] = s_ram;
    z80_writemap[63] = s_ram;

    /* ROM will be mapped when loaded */

    /* Set up memory callbacks for I/O-mapped regions */
    z80_readmem = cps1_z80_read;
    z80_writemem = cps1_z80_write;
    z80_readport = cps1_z80_read_port;
    z80_writeport = cps1_z80_write_port;

    z80_reset();

    s_initialized = true;
    printf("[z80] Initialized (CPS1 mode, 3.579545 MHz)\n");
    return 0;
}

void z80_cpu_shutdown(void) {
    free(s_rom);
    s_rom = NULL;
    s_rom_size = 0;
    s_initialized = false;
}

int z80_load_rom(const uint8_t *data, uint32_t size) {
    free(s_rom);
    s_rom = (uint8_t *)malloc(size);
    if (!s_rom) return -1;
    memcpy(s_rom, data, size);
    s_rom_size = size;

    /* Map ROM pages ($0000-$7FFF = pages 0-31) */
    for (int i = 0; i < 32 && (unsigned)(i * 0x400) < size; i++) {
        z80_readmap[i] = s_rom + i * 0x400;
    }

    printf("[z80] Loaded ROM: %u bytes (%u KB)\n", size, size / 1024);
    return 0;
}

void z80_execute(int cycles) {
    if (!s_initialized || !s_rom) return;
    z80_run((unsigned int)cycles);
}

void z80_send_command(uint8_t cmd) {
    s_sound_latch = cmd;
    /* Fire NMI on the Z80 */
    if (s_initialized) {
        z80_set_nmi_line(PULSE_LINE);
    }
}

uint8_t z80_read_reply(void) {
    return s_reply_byte;
}

void z80_cpu_reset(void) {
    if (s_initialized) {
        z80_reset();
    }
    memset(s_ram, 0, sizeof(s_ram));
    s_sound_latch = 0;
    s_bank = 0;
}
