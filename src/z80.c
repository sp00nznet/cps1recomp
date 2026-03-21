/*
 * z80.c — Z80 audio CPU for CPS1.
 *
 * Stub implementation: accepts sound commands and loads ROMs,
 * but does not execute Z80 instructions. YM2151 and OKI6295
 * will remain silent until a standalone Z80 interpreter is integrated.
 *
 * TODO: Integrate a standalone Z80 core (z80ex, or extract the
 * Juergen Buchmueller Z80 from its GenPlusGX dependencies).
 */

#include <cps1recomp/z80.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static uint8_t *s_rom = NULL;
static uint32_t s_rom_size = 0;
static uint8_t  s_sound_latch = 0;
static uint8_t  s_reply_byte = 0;
static bool     s_initialized = false;

int z80_cpu_init(void) {
    s_sound_latch = 0;
    s_reply_byte = 0;
    s_initialized = true;
    printf("[z80] Initialized (stub — no execution)\n");
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
    printf("[z80] Loaded ROM: %u bytes (%u KB)\n", size, size / 1024);
    return 0;
}

void z80_execute(int cycles) {
    /* Stub: no Z80 execution */
    (void)cycles;
}

void z80_send_command(uint8_t cmd) {
    s_sound_latch = cmd;
    /* In a full implementation, this would fire NMI on the Z80 */
}

uint8_t z80_read_reply(void) {
    return s_reply_byte;
}

void z80_cpu_reset(void) {
    s_sound_latch = 0;
}
