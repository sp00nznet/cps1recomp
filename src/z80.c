/*
 * z80.c — Z80 audio CPU stub.
 *
 * The Z80 will be implemented with an interpreted core in Phase 6.
 * For now this provides the interface for 68K <-> Z80 communication.
 */

#include <cps1recomp/z80.h>
#include <string.h>
#include <stdio.h>

static uint8_t *s_rom = NULL;
static uint32_t s_rom_size = 0;
static uint8_t s_ram[0x800];     /* 2 KB Z80 RAM */
static uint8_t s_sound_latch = 0;
static uint8_t s_reply_byte = 0;

int z80_init(void) {
    memset(s_ram, 0, sizeof(s_ram));
    s_sound_latch = 0;
    s_reply_byte = 0;
    return 0;
}

void z80_shutdown(void) {
    free(s_rom);
    s_rom = NULL;
    s_rom_size = 0;
}

int z80_load_rom(const uint8_t *data, uint32_t size) {
    s_rom = (uint8_t *)malloc(size);
    if (!s_rom) return -1;
    memcpy(s_rom, data, size);
    s_rom_size = size;
    return 0;
}

void z80_execute(int cycles) {
    /* TODO: Phase 6 - Execute Z80 interpreter for given cycles
     * ~60,192 cycles per frame at 3.579545 MHz / 59.63 Hz
     *
     * Z80 Memory Map:
     *   $0000-$7FFF: ROM (banked)
     *   $8000-$BFFF: RAM (2 KB mirrored)
     *   $D000-$D001: YM2151
     *   $E000-$E001: OKI MSM6295
     *   $F000: Sound latch from 68K
     */
    (void)cycles;
}

void z80_send_command(uint8_t cmd) {
    s_sound_latch = cmd;
    /* TODO: Phase 6 - Fire NMI on Z80 */
}

uint8_t z80_read_reply(void) {
    return s_reply_byte;
}

void z80_reset(void) {
    memset(s_ram, 0, sizeof(s_ram));
    s_sound_latch = 0;
    /* TODO: Phase 6 - Reset Z80 CPU state */
}
