/*
 * io.h — CPS1 input and system I/O.
 *
 * CPS1 input layout for Street Fighter II (active low):
 *
 * Player input (read via CPS-A registers):
 *   Bit 0: Coin 1         Bit 0: Up
 *   Bit 1: Coin 2         Bit 1: Down
 *   Bit 2: Start P1       Bit 2: Left
 *   Bit 3: Start P2       Bit 3: Right
 *   Bit 4: Service        Bit 4: LP (Light Punch)
 *   Bit 5: (unused)       Bit 5: MP (Medium Punch)
 *   Bit 6: (unused)       Bit 6: HP (Heavy Punch)
 *   Bit 7: (unused)       Bit 7: (unused for 3-button; see extra port for kicks)
 *
 * SF2 uses a 6-button layout, with kicks on a separate port:
 *   Bit 0: LK (Light Kick)
 *   Bit 1: MK (Medium Kick)
 *   Bit 2: HK (Heavy Kick)
 */

#ifndef CPS1RECOMP_IO_H
#define CPS1RECOMP_IO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int io_init(void);
void io_shutdown(void);

/* Update controller state from platform layer. Call once per frame. */
void io_update(void);

/* Set button state. */
void io_set_button(int player, uint8_t button, bool pressed);

/* Button constants */
#define IO_BTN_UP      0x01
#define IO_BTN_DOWN    0x02
#define IO_BTN_LEFT    0x04
#define IO_BTN_RIGHT   0x08
#define IO_BTN_LP      0x10   /* Light Punch */
#define IO_BTN_MP      0x20   /* Medium Punch */
#define IO_BTN_HP      0x40   /* Heavy Punch */

/* Kick buttons (separate port) */
#define IO_BTN_LK      0x01   /* Light Kick */
#define IO_BTN_MK      0x02   /* Medium Kick */
#define IO_BTN_HK      0x04   /* Heavy Kick */

/* System buttons (CPS1 standard IN0 layout at $800018).
 * SF2 reads ~$800018 into A5+$76 and edge-detects bits 4,5 (0x30 =
 * START1|START2) to start a game, and tests bit 6 for the service coin. */
#define IO_BTN_COIN1   0x01
#define IO_BTN_COIN2   0x02
#define IO_BTN_START1  0x10
#define IO_BTN_START2  0x20
#define IO_BTN_SERVICE 0x40

/* Register reads (called by bus layer) */
uint16_t io_read_player1(void);    /* P1 direction + punches */
uint16_t io_read_player2(void);    /* P2 direction + punches */
uint16_t io_read_system(void);     /* Coins, starts, service */
uint16_t io_read_dsw(void);        /* DIP switches */
uint16_t io_read_extra(void);      /* P1/P2 kick buttons */

/* DIP switch configuration */
void io_set_dsw(uint16_t dsw_a, uint16_t dsw_b);

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_IO_H */
