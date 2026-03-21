/*
 * io.c — CPS1 input system implementation.
 *
 * CPS1 SF2 uses three input ports:
 *   - Player 1: direction + 3 punch buttons (active low)
 *   - Player 2: direction + 3 punch buttons (active low)
 *   - System: coins, starts, service (active low)
 *   - Extra: kick buttons for both players (active low)
 *
 * Active low means 0 = pressed, 1 = not pressed.
 * All ports default to 0xFF (nothing pressed).
 */

#include <cps1recomp/io.h>
#include <string.h>

/* Raw button states — active high internally, inverted on read */
static uint8_t s_p1_dir_punch = 0;   /* UP/DOWN/LEFT/RIGHT/LP/MP/HP */
static uint8_t s_p2_dir_punch = 0;
static uint8_t s_p1_kicks = 0;       /* LK/MK/HK */
static uint8_t s_p2_kicks = 0;
static uint8_t s_system = 0;         /* COIN1/COIN2/START1/START2/SERVICE */
static uint16_t s_dsw_a = 0xFF;
static uint16_t s_dsw_b = 0xFF;

int io_init(void) {
    s_p1_dir_punch = 0;
    s_p2_dir_punch = 0;
    s_p1_kicks = 0;
    s_p2_kicks = 0;
    s_system = 0;
    /* Default DIP switches: freeplay, normal difficulty */
    s_dsw_a = 0xFF;
    s_dsw_b = 0xFF;
    return 0;
}

void io_shutdown(void) { }

void io_update(void) {
    /* State is updated via io_set_button from platform_poll_input */
}

void io_set_button(int player, uint8_t button, bool pressed) {
    /*
     * Player 0 = P1 direction + punches
     * Player 1 = P2 direction + punches
     * Player 2 = P1 kicks (LK/MK/HK)
     * Player 3 = P2 kicks
     * Player 4 = System (coins/starts/service)
     */
    uint8_t *target;
    switch (player) {
        case 0: target = &s_p1_dir_punch; break;
        case 1: target = &s_p2_dir_punch; break;
        case 2: target = &s_p1_kicks; break;
        case 3: target = &s_p2_kicks; break;
        case 4: target = &s_system; break;
        default: return;
    }

    if (pressed)
        *target |= button;
    else
        *target &= ~button;
}

/* All reads return active-low (inverted) */
uint16_t io_read_player1(void) {
    return (uint16_t)(~s_p1_dir_punch & 0xFF);
}

uint16_t io_read_player2(void) {
    return (uint16_t)(~s_p2_dir_punch & 0xFF);
}

uint16_t io_read_system(void) {
    return (uint16_t)(~s_system & 0xFF);
}

uint16_t io_read_dsw(void) {
    return s_dsw_a;
}

uint16_t io_read_extra(void) {
    /* Low byte: P1 kicks, high byte: P2 kicks (both active low) */
    return (uint16_t)((~s_p1_kicks & 0x07) | ((~s_p2_kicks & 0x07) << 8));
}

void io_set_dsw(uint16_t dsw_a, uint16_t dsw_b) {
    s_dsw_a = dsw_a;
    s_dsw_b = dsw_b;
}
