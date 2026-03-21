/*
 * timer.h — CPS1 interrupt timing.
 *
 * CPS1 uses 68000 interrupt levels:
 *   IRQ 2 (autovector): VBlank interrupt (~59.63 Hz NTSC)
 *
 * The VBlank interrupt is the primary timing mechanism.
 * Some games also use raster interrupts but SF2 primarily uses VBlank.
 */

#ifndef CPS1RECOMP_TIMER_H
#define CPS1RECOMP_TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int timer_init(void);
void timer_shutdown(void);

/* Fire the VBlank interrupt. */
void timer_trigger_vblank(void);

/* Acknowledge the VBlank interrupt. */
void timer_ack_vblank(void);

/* Check if VBlank is pending. */
bool timer_vblank_pending(void);

/* Get current scanline (0-261). */
uint16_t timer_get_scanline(void);

/* Advance by one scanline. */
void timer_tick_scanline(void);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_TIMER_H */
