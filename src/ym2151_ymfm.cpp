/*
 * ym2151_ymfm.cpp — YM2151 (OPM) FM synthesis via ymfm library.
 *
 * This C++ file wraps ymfm's ym2151 implementation behind the C
 * interface declared in ym2151.h. It's the only C++ file in the
 * project — everything else is pure C17.
 *
 * ymfm by Aaron Giles, BSD-3 licensed.
 * https://github.com/aaronsgiles/ymfm
 */

#include "ymfm_opm.h"

#include <cstdint>
#include <cstring>
#include <cstdio>

/* ---- ymfm interface implementation ---- */

class cps1_ymfm_interface : public ymfm::ymfm_interface {
public:
    cps1_ymfm_interface() = default;
    ~cps1_ymfm_interface() override = default;

    /* Timer callbacks — CPS1 doesn't need precise timers for basic playback */
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override {
        /* For now, ignore timers. The Z80 sound driver polls the status
         * register for timer flags, but many CPS1 games work without
         * accurate timer emulation during initial bring-up. */
        (void)tnum;
        (void)duration_in_clocks;
    }

    void ymfm_set_busy_end(uint32_t clocks) override {
        m_busy_clocks = clocks;
        m_busy_counter = clocks;
    }

    bool ymfm_is_busy() override {
        /* Simple busy flag: decrements each generate call */
        return m_busy_counter > 0;
    }

    void ymfm_update_irq(bool asserted) override {
        m_irq = asserted;
    }

    bool get_irq() const { return m_irq; }

    void tick_busy(uint32_t samples) {
        if (m_busy_counter > samples)
            m_busy_counter -= samples;
        else
            m_busy_counter = 0;
    }

private:
    bool m_irq = false;
    uint32_t m_busy_clocks = 0;
    uint32_t m_busy_counter = 0;
};

/* ---- Global state ---- */

static cps1_ymfm_interface *s_intf = nullptr;
static ymfm::ym2151 *s_chip = nullptr;
static int s_sample_rate = 44100;
static uint32_t s_chip_clock = 3579545;  /* CPS1 YM2151 clock */

/* Resampling state: ymfm generates at chip_clock/64 (~55.9 kHz),
 * we need to resample to the output sample rate (44100 Hz). */
static double s_resample_pos = 0.0;
static double s_resample_step = 0.0;
static int32_t s_last_left = 0;
static int32_t s_last_right = 0;

/* ---- C interface (extern "C") ---- */

extern "C" {

int ym2151_init(int sample_rate) {
    s_sample_rate = sample_rate;

    s_intf = new cps1_ymfm_interface();
    s_chip = new ymfm::ym2151(*s_intf);
    s_chip->reset();

    /* Calculate resampling ratio */
    uint32_t chip_rate = s_chip->sample_rate(s_chip_clock);
    s_resample_step = (double)chip_rate / (double)sample_rate;
    s_resample_pos = 0.0;
    s_last_left = 0;
    s_last_right = 0;

    printf("[ym2151] Initialized via ymfm (clock: %u Hz, chip rate: %u Hz, output: %d Hz)\n",
           s_chip_clock, chip_rate, sample_rate);
    return 0;
}

void ym2151_shutdown(void) {
    delete s_chip;
    s_chip = nullptr;
    delete s_intf;
    s_intf = nullptr;
}

void ym2151_write(uint8_t addr, uint8_t data) {
    if (!s_chip) return;

    if (addr == 0) {
        s_chip->write_address(data);
    } else {
        s_chip->write_data(data);
    }
}

uint8_t ym2151_read(void) {
    if (!s_chip) return 0;
    return s_chip->read_status();
}

void ym2151_generate(int16_t *buffer, int num_samples) {
    if (!s_chip || !buffer) {
        if (buffer) memset(buffer, 0, num_samples * 2 * sizeof(int16_t));
        return;
    }

    ymfm::ym2151::output_data output;

    for (int i = 0; i < num_samples; i++) {
        /* Generate chip samples until we reach the next output sample */
        while (s_resample_pos >= 1.0) {
            s_chip->generate(&output, 1);
            s_last_left  = output.data[0];
            s_last_right = output.data[1];
            s_resample_pos -= 1.0;
            s_intf->tick_busy(1);
        }

        /* Clamp to int16 range */
        int32_t l = s_last_left;
        int32_t r = s_last_right;
        if (l > 32767) l = 32767;
        if (l < -32768) l = -32768;
        if (r > 32767) r = 32767;
        if (r < -32768) r = -32768;

        buffer[i * 2 + 0] = (int16_t)l;
        buffer[i * 2 + 1] = (int16_t)r;

        s_resample_pos += s_resample_step;
    }
}

} /* extern "C" */
