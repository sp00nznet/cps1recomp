/*
 * ym2151.c — YM2151 (OPM) FM synthesis stub.
 *
 * This is a placeholder. The real implementation will wrap ymfm's
 * ym2151 via a C++ glue file (ym2151_ymfm.cpp).
 */

#include <cps1recomp/ym2151.h>
#include <string.h>

static uint8_t s_addr_latch = 0;
static uint8_t s_regs[256];
static int s_sample_rate = 44100;

int ym2151_init(int sample_rate) {
    s_sample_rate = sample_rate;
    s_addr_latch = 0;
    memset(s_regs, 0, sizeof(s_regs));
    /* TODO: Phase 7 - Initialize ymfm::ym2151 */
    return 0;
}

void ym2151_shutdown(void) {
    /* TODO: Phase 7 - Destroy ymfm instance */
}

void ym2151_write(uint8_t addr, uint8_t data) {
    if (addr == 0) {
        s_addr_latch = data;
    } else {
        s_regs[s_addr_latch] = data;
        /* TODO: Phase 7 - Forward to ymfm */
    }
}

uint8_t ym2151_read(void) {
    /* Status register: bit 7 = busy, bits 1-0 = timer flags */
    /* Return not busy for now */
    return 0x00;
}

void ym2151_generate(int16_t *buffer, int num_samples) {
    /* Silence until ymfm is integrated */
    memset(buffer, 0, num_samples * 2 * sizeof(int16_t));
}

void ym2151_tick(uint32_t clocks) {
    (void)clocks;  /* no timers in the stub */
}

bool ym2151_irq_asserted(void) {
    return false;  /* stub never raises IRQ */
}
