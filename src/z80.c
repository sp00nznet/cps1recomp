/*
 * z80.c — Z80 audio CPU for CPS1.
 *
 * Drives the CPS1 sound hardware (YM2151 + OKI MSM6295) by interpreting the
 * original sound-program Z80 ROM. The Z80 core (decoder + ALU + interpreter) is
 * ported from the sp00nznet z80recomp runtime; this file provides the CPS1
 * sound-CPU memory map and the per-frame execution driver.
 *
 * Z80 memory map (CPS1):
 *   $0000-$7FFF  sound program ROM   (fixed, first 32 KB)
 *   $8000-$BFFF  banked program ROM  (16 KB window; bank select via $F004)
 *   $D000-$D7FF  work RAM            (2 KB, mirrored to $DFFF)
 *   $F000        YM2151 address / status
 *   $F001        YM2151 data
 *   $F002        OKI MSM6295 (write: command, read: status)
 *   $F004        ROM bank select     (write)
 *   $F006        OKI pin 7 / sample-rate select (write — ignored here)
 *   $F008        sound latch  (read: command from 68k)
 *   $F00A        sound latch 2 (read)
 *
 * The 68k posts a command by writing $800180 (-> z80_send_command). The Z80
 * polls $F008 and is paced by the YM2151 timer interrupt (IM 1 -> $0038).
 */

#include <cps1recomp/z80.h>
#include <cps1recomp/ym2151.h>
#include <cps1recomp/oki6295.h>

#include "z80/z80_cpu.h"
#include "z80/z80_interp.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---- state ---- */
static uint8_t *s_rom = NULL;
static uint32_t s_rom_size = 0;
static uint8_t  s_ram[0x800];        /* 2 KB work RAM ($D000-$D7FF)          */
static uint32_t s_bank = 0;          /* current $8000-$BFFF bank             */
static uint32_t s_num_banks = 1;     /* number of 16 KB banks above $8000    */
static uint8_t  s_sound_latch = 0;   /* command from 68k ($F008)             */
static uint8_t  s_sound_latch2 = 0;  /* secondary latch ($F00A)              */
static uint8_t  s_reply_byte = 0;    /* Z80 -> 68k reply ($800180 readback)  */
static bool     s_initialized = false;

/* Bring-up diagnostics (cheap counters; printed periodically). */
static uint64_t s_dbg_ym_writes  = 0;
static uint64_t s_dbg_oki_writes = 0;
static uint64_t s_dbg_cmds       = 0;
static uint64_t s_dbg_irqs       = 0;
static uint64_t s_dbg_insns      = 0;

/* ---- bus glue (used by the interpreter) ---- */

uint8_t cz80_mem_read(uint16_t addr) {
    if (addr < 0x8000) {                                 /* fixed ROM */
        return (addr < s_rom_size) ? s_rom[addr] : 0xFF;
    }
    if (addr < 0xC000) {                                 /* banked ROM */
        uint32_t off = 0x8000u + s_bank * 0x4000u + (uint32_t)(addr - 0x8000);
        return (off < s_rom_size) ? s_rom[off] : 0xFF;
    }
    if (addr >= 0xD000 && addr < 0xE000) {               /* 2 KB RAM (mirrored) */
        return s_ram[addr & 0x7FF];
    }
    switch (addr) {
        case 0xF000:
        case 0xF001: return ym2151_read();               /* YM2151 status */
        case 0xF002: return oki6295_read();              /* OKI status    */
        case 0xF008: return s_sound_latch;               /* command latch */
        case 0xF00A: return s_sound_latch2;
        default:     return 0xFF;                         /* open bus */
    }
}

void cz80_mem_write(uint16_t addr, uint8_t val) {
    if (addr >= 0xD000 && addr < 0xE000) {               /* 2 KB RAM (mirrored) */
        s_ram[addr & 0x7FF] = val;
        return;
    }
    switch (addr) {
        case 0xF000: ym2151_write(0, val); s_dbg_ym_writes++;  return; /* YM2151 address */
        case 0xF001: ym2151_write(1, val); s_dbg_ym_writes++;  return; /* YM2151 data    */
        case 0xF002: oki6295_write(val);   s_dbg_oki_writes++; return; /* OKI command    */
        case 0xF004:                                      /* bank select    */
            s_bank = (s_num_banks > 1) ? (val % s_num_banks) : 0;
            return;
        case 0xF006: /* OKI pin 7 (sample-rate select) — ignored */ return;
        default:
            /* Writes to ROM space / open bus are dropped. */
            return;
    }
}

uint16_t cz80_mem_read16(uint16_t addr) {
    uint8_t lo = cz80_mem_read(addr);
    uint8_t hi = cz80_mem_read((uint16_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}

void cz80_mem_write16(uint16_t addr, uint16_t val) {
    cz80_mem_write(addr, (uint8_t)val);
    cz80_mem_write((uint16_t)(addr + 1), (uint8_t)(val >> 8));
}

/* CPS1 sound CPU is fully memory-mapped; Z80 IN/OUT ports are unused. */
uint8_t cz80_port_in(uint16_t port)            { (void)port; return 0xFF; }
void    cz80_port_out(uint16_t port, uint8_t v){ (void)port; (void)v; }

void cz80_push16(uint16_t val) {
    cz80_cpu.sp.w = (uint16_t)(cz80_cpu.sp.w - 2);
    cz80_mem_write16(cz80_cpu.sp.w, val);
}

uint16_t cz80_pop16(void) {
    uint16_t v = cz80_mem_read16(cz80_cpu.sp.w);
    cz80_cpu.sp.w = (uint16_t)(cz80_cpu.sp.w + 2);
    return v;
}

/* ---- public API ---- */

int z80_cpu_init(void) {
    cz80_cpu_reset();
    memset(s_ram, 0, sizeof(s_ram));
    s_bank = 0;
    s_sound_latch = 0;
    s_sound_latch2 = 0;
    s_reply_byte = 0;
    s_initialized = true;
    printf("[z80] Initialized (interpreter core)\n");
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
    /* Banks tile the ROM above $8000 in 16 KB pages. */
    s_num_banks = (size > 0x8000) ? ((size - 0x8000) / 0x4000) : 1;
    if (s_num_banks == 0) s_num_banks = 1;
    printf("[z80] Loaded ROM: %u bytes (%u KB), %u bank(s)\n",
           size, size / 1024, s_num_banks);
    /* Cold reset so PC = $0000 boots into the freshly loaded program. */
    cz80_cpu_reset();
    return 0;
}

void z80_cpu_reset(void) {
    cz80_cpu_reset();
    memset(s_ram, 0, sizeof(s_ram));
    s_bank = 0;
    s_sound_latch = 0;
    s_sound_latch2 = 0;
}

/* Accept an IM 1 maskable interrupt: push PC, vector to $0038, disable IFFs. */
static void z80_accept_irq(void) {
    cz80_cpu.iff1 = cz80_cpu.iff2 = 0;
    cz80_cpu.halted = 0;
    cz80_push16(cz80_cpu.pc.w);
    cz80_cpu.pc.w = 0x0038;
}

void z80_execute(int cycles) {
    if (!s_initialized || !s_rom) return;

    /* Bring-up test hook: CZ80_INJECT=0xNN posts sound command NN once at
     * frame ~150 to prove the sound path produces audio independent of the
     * (incomplete) attract-mode state machine. */
    {
        static int s_injframe = 0;
        static int s_injected = 0;
        const char *e = getenv("CZ80_INJECT");
        if (e && !s_injected && ++s_injframe == 150) {
            unsigned v = (unsigned)strtol(e, NULL, 0);
            s_sound_latch = (uint8_t)v;
            s_injected = 1;
            printf("[z80] INJECT test command $%02X\n", (uint8_t)v);
            fflush(stdout);
        }
    }

    int remaining = cycles;
    while (remaining > 0) {
        /* Maskable interrupt from the YM2151 timer (IM 1). */
        if (cz80_cpu.iff1 && ym2151_irq_asserted()) {
            z80_accept_irq();
            s_dbg_irqs++;
            ym2151_tick(13);
            remaining -= 13;        /* interrupt acknowledge ~13 cycles */
            continue;
        }

        if (cz80_cpu.halted) {
            /* Idle until an interrupt arrives; keep the YM timer advancing. */
            ym2151_tick(4);
            remaining -= 4;
            continue;
        }

        int len = z80_interp_step(cz80_cpu.pc.w);
        s_dbg_insns++;
        int cyc = len * 4;          /* coarse cycle estimate; timer self-paces */
        if (cyc < 4) cyc = 4;
        ym2151_tick((uint32_t)cyc);
        remaining -= cyc;
    }

    /* Bring-up diagnostic: report Z80 liveness once per ~10 seconds. */
    static int s_frame = 0;
    if (++s_frame % 600 == 0) {
        printf("[z80] frame %d: pc=$%04X sp=$%04X iff1=%d bank=%u | "
               "insns=%llu irqs=%llu ymW=%llu okiW=%llu cmds=%llu\n",
               s_frame, cz80_cpu.pc.w, cz80_cpu.sp.w, cz80_cpu.iff1, s_bank,
               (unsigned long long)s_dbg_insns, (unsigned long long)s_dbg_irqs,
               (unsigned long long)s_dbg_ym_writes, (unsigned long long)s_dbg_oki_writes,
               (unsigned long long)s_dbg_cmds);
        fflush(stdout);
    }
}

void z80_send_command(uint8_t cmd) {
    s_sound_latch = cmd;
    s_dbg_cmds++;
    if (s_dbg_cmds <= 24) {
        printf("[z80] 68k sound command #%llu = $%02X\n",
               (unsigned long long)s_dbg_cmds, cmd);
        fflush(stdout);
    }
}

uint8_t z80_read_reply(void) {
    return s_reply_byte;
}
