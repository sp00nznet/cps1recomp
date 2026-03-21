/*
 * oki6295.c — OKI MSM6295 ADPCM playback stub.
 *
 * 4 channels of 4-bit ADPCM sample playback from ROM.
 */

#include <cps1recomp/oki6295.h>
#include <string.h>
#include <stdio.h>

/* ADPCM channel state */
typedef struct {
    bool playing;
    uint32_t addr;          /* Current ROM address */
    uint32_t end_addr;      /* End address */
    int16_t signal;         /* Current decoded sample */
    int32_t step;           /* ADPCM step index */
    uint8_t nibble;         /* Cached nibble for odd samples */
    bool nibble_valid;
    uint8_t volume;         /* Attenuation (0 = max volume) */
} oki_channel_t;

static oki_channel_t s_channels[4];
static const uint8_t *s_rom = NULL;
static uint32_t s_rom_size = 0;
static uint32_t s_bank_offset = 0;
static int s_sample_rate = 44100;

int oki6295_init(int sample_rate) {
    s_sample_rate = sample_rate;
    memset(s_channels, 0, sizeof(s_channels));
    return 0;
}

void oki6295_shutdown(void) {
    s_rom = NULL;
    s_rom_size = 0;
}

void oki6295_set_rom(const uint8_t *data, uint32_t size) {
    s_rom = data;
    s_rom_size = size;
}

void oki6295_set_bank(uint32_t bank_offset) {
    s_bank_offset = bank_offset;
}

void oki6295_write(uint8_t data) {
    /* TODO: Phase 8 - Parse OKI command register
     * If bit 7 set: channel selection + start
     *   Bits 6-4: phrase number high bits
     *   Bits 3-0: channel enable (one-hot)
     * If bit 7 clear: channel stop
     *   Bits 3-0: channel stop (one-hot)
     */
    if (!(data & 0x80)) {
        /* Stop channels */
        for (int i = 0; i < 4; i++) {
            if (data & (1 << i)) {
                s_channels[i].playing = false;
            }
        }
    }
}

uint8_t oki6295_read(void) {
    /* Status: bits 3-0 = channel busy (1 = playing) */
    uint8_t status = 0;
    for (int i = 0; i < 4; i++) {
        if (s_channels[i].playing) status |= (1 << i);
    }
    return status;
}

void oki6295_generate(int16_t *buffer, int num_samples) {
    /* Silence until Phase 8 */
    memset(buffer, 0, num_samples * sizeof(int16_t));
}
