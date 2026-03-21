/*
 * oki6295.c — OKI MSM6295 ADPCM sample playback.
 *
 * The MSM6295 provides 4 independent channels of ADPCM playback
 * from a sample ROM. Each channel can play one "phrase" at a time.
 *
 * The sample ROM contains a phrase table at offset 0:
 *   8 entries, each 8 bytes:
 *     Bytes 0-2: Start address (24-bit, /8 for actual byte address)
 *     Bytes 3-5: End address (24-bit, /8)
 *     Bytes 6-7: Not used
 *
 * Command protocol:
 *   Write with bit 7 set: Select phrase and prepare for playback
 *     Bits 6-0: Phrase number (0-127)
 *   Next write: Start channels
 *     Bits 7-4: Channel mask (one-hot: bit 7=ch0, bit 6=ch1, etc.)
 *     Bits 3-0: Attenuation (volume)
 *   Write with bit 7 clear: Stop channels
 *     Bits 7-4: Channel mask to stop
 *
 * Output: ~8 kHz ADPCM (pin 7 selects 6.061 kHz or 8.000 kHz)
 *         CPS1 uses the 8 kHz divider.
 *
 * ADPCM decode: 4-bit input -> 12-bit signed PCM output
 */

#include <cps1recomp/oki6295.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* ADPCM step size table (index 0-48) */
static const int16_t s_step_table[49] = {
     16,  17,  19,  21,  23,  25,  28,  31,  34,  37,
     41,  45,  50,  55,  60,  66,  73,  80,  88,  97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253,
    279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963,1060,1166,1282,1411,1552
};

/* Step index adjustment table (per nibble value 0-7) */
static const int8_t s_index_adjust[8] = {
    -1, -1, -1, -1, 2, 4, 6, 8
};

/* Channel state */
typedef struct {
    bool playing;
    uint32_t addr;          /* Current ROM byte address */
    uint32_t end_addr;      /* End address */
    int16_t  signal;        /* Current decoded sample value */
    int32_t  step_idx;      /* ADPCM step index (0-48) */
    bool     nibble_toggle; /* Which nibble of the current byte (false=high, true=low) */
    uint8_t  cur_byte;      /* Current ROM byte being decoded */
    uint8_t  volume;        /* Attenuation (0 = loudest, 15 = silent) */
} oki_channel_t;

static oki_channel_t s_channels[4];
static const uint8_t *s_rom = NULL;
static uint32_t s_rom_size = 0;
static uint32_t s_bank_offset = 0;
static int s_sample_rate = 44100;
static uint8_t s_pending_phrase = 0;
static bool s_phrase_pending = false;

/* OKI chip output rate: ~8 kHz (CPS1 uses the /132 divider) */
#define OKI_CLOCK      1000000     /* 1 MHz input clock */
#define OKI_DIVIDER    132         /* /132 for ~7.576 kHz */
static double s_resample_pos = 0.0;
static double s_resample_step = 0.0;
static int16_t s_last_sample = 0;

/* Volume attenuation table (linear approximation) */
static const int16_t s_volume_table[16] = {
    256, 228, 203, 181, 161, 143, 128, 114,
    101,  90,  80,  72,  64,  57,  50,   0
};

/* Decode one ADPCM nibble */
static int16_t decode_nibble(oki_channel_t *ch, uint8_t nibble) {
    int16_t step = s_step_table[ch->step_idx];
    int32_t diff = step >> 3;

    if (nibble & 4) diff += step;
    if (nibble & 2) diff += step >> 1;
    if (nibble & 1) diff += step >> 2;
    if (nibble & 8) diff = -diff;

    int32_t signal = ch->signal + diff;

    /* Clamp to 12-bit signed range */
    if (signal > 2047) signal = 2047;
    if (signal < -2048) signal = -2048;
    ch->signal = (int16_t)signal;

    /* Update step index */
    ch->step_idx += s_index_adjust[nibble & 7];
    if (ch->step_idx < 0) ch->step_idx = 0;
    if (ch->step_idx > 48) ch->step_idx = 48;

    return ch->signal;
}

/* Get next ADPCM nibble from ROM for a channel */
static uint8_t get_nibble(oki_channel_t *ch) {
    if (!s_rom || ch->addr >= s_rom_size) {
        ch->playing = false;
        return 0;
    }

    if (!ch->nibble_toggle) {
        /* Read new byte, return high nibble */
        uint32_t rom_addr = (ch->addr + s_bank_offset) % s_rom_size;
        ch->cur_byte = s_rom[rom_addr];
        ch->nibble_toggle = true;
        return (ch->cur_byte >> 4) & 0x0F;
    } else {
        /* Return low nibble, advance address */
        ch->nibble_toggle = false;
        ch->addr++;
        return ch->cur_byte & 0x0F;
    }
}

int oki6295_init(int sample_rate) {
    s_sample_rate = sample_rate;
    memset(s_channels, 0, sizeof(s_channels));
    s_bank_offset = 0;
    s_phrase_pending = false;

    /* Calculate resample ratio: OKI rate -> output rate */
    double oki_rate = (double)OKI_CLOCK / (double)OKI_DIVIDER;
    s_resample_step = oki_rate / (double)sample_rate;
    s_resample_pos = 0.0;
    s_last_sample = 0;

    printf("[oki6295] Initialized (clock: %d Hz, output: %.1f Hz, target: %d Hz)\n",
           OKI_CLOCK, oki_rate, sample_rate);
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
    if (s_phrase_pending) {
        /* Second write: start channels with the pending phrase */
        uint8_t channel_mask = (data >> 4) & 0x0F;
        uint8_t volume = data & 0x0F;

        if (!s_rom || s_rom_size < 0x400) {
            s_phrase_pending = false;
            return;
        }

        /* Read phrase table from ROM (8 bytes per phrase) */
        uint32_t phrase_offset = (uint32_t)s_pending_phrase * 8;
        if (phrase_offset + 7 >= s_rom_size) {
            s_phrase_pending = false;
            return;
        }

        const uint8_t *entry = s_rom + s_bank_offset + phrase_offset;
        uint32_t start = ((uint32_t)entry[0] << 16) | ((uint32_t)entry[1] << 8) | entry[2];
        uint32_t end   = ((uint32_t)entry[3] << 16) | ((uint32_t)entry[4] << 8) | entry[5];

        /* Addresses in the phrase table are byte addresses */
        /* (Some games use /8 addressing — CPS1 uses direct byte addresses) */

        /* Start on the first available channel from the mask */
        for (int i = 0; i < 4; i++) {
            if (channel_mask & (0x08 >> i)) {
                s_channels[i].playing = true;
                s_channels[i].addr = start;
                s_channels[i].end_addr = end;
                s_channels[i].signal = 0;
                s_channels[i].step_idx = 0;
                s_channels[i].nibble_toggle = false;
                s_channels[i].volume = volume;
                break;  /* Only start one channel per command */
            }
        }

        s_phrase_pending = false;
    } else if (data & 0x80) {
        /* First write: select phrase */
        s_pending_phrase = data & 0x7F;
        s_phrase_pending = true;
    } else {
        /* Stop channels */
        for (int i = 0; i < 4; i++) {
            if (data & (0x08 >> i)) {
                s_channels[i].playing = false;
            }
        }
    }
}

uint8_t oki6295_read(void) {
    /* Status register: bits 3-0 = channel busy (1 = playing) */
    uint8_t status = 0;
    for (int i = 0; i < 4; i++) {
        if (s_channels[i].playing) status |= (0x08 >> i);
    }
    return status;
}

/* Generate one OKI sample (mix of all 4 channels) */
static int16_t generate_one_sample(void) {
    int32_t mix = 0;

    for (int i = 0; i < 4; i++) {
        oki_channel_t *ch = &s_channels[i];
        if (!ch->playing) continue;

        /* Check end address */
        if (ch->addr >= ch->end_addr) {
            ch->playing = false;
            continue;
        }

        uint8_t nibble = get_nibble(ch);
        int16_t sample = decode_nibble(ch, nibble);

        /* Apply volume attenuation */
        int32_t attenuated = ((int32_t)sample * s_volume_table[ch->volume]) >> 8;
        mix += attenuated;
    }

    /* Clamp */
    if (mix > 32767) mix = 32767;
    if (mix < -32768) mix = -32768;

    return (int16_t)mix;
}

void oki6295_generate(int16_t *buffer, int num_samples) {
    if (!buffer) return;

    for (int i = 0; i < num_samples; i++) {
        /* Generate OKI samples until we reach the next output sample */
        while (s_resample_pos >= 1.0) {
            s_last_sample = generate_one_sample();
            s_resample_pos -= 1.0;
        }

        buffer[i] = s_last_sample;
        s_resample_pos += s_resample_step;
    }
}
