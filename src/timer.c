/*
 * timer.c — CPS1 interrupt timing.
 */

#include <cps1recomp/timer.h>
#include <cps1recomp/video.h>

#define CPS1_TOTAL_SCANLINES 262  /* NTSC */

static uint16_t s_scanline = 0;
static bool s_vblank_pending = false;

int timer_init(void) {
    s_scanline = 0;
    s_vblank_pending = false;
    return 0;
}

void timer_shutdown(void) { }

void timer_trigger_vblank(void) {
    s_vblank_pending = true;
}

void timer_ack_vblank(void) {
    s_vblank_pending = false;
}

bool timer_vblank_pending(void) {
    return s_vblank_pending;
}

uint16_t timer_get_scanline(void) {
    return s_scanline;
}

void timer_tick_scanline(void) {
    s_scanline++;
    if (s_scanline >= CPS1_TOTAL_SCANLINES) {
        s_scanline = 0;
    }
    /* VBlank fires at scanline 224 (start of vertical blank) */
    if (s_scanline == CPS1_SCREEN_HEIGHT) {
        timer_trigger_vblank();
    }
}
