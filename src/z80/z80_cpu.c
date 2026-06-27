/* cpu.c - Z80 state + ALU/flag helpers. See cpu.h.
 *
 * Flag conventions implemented here follow "The Undocumented Z80 Documented":
 *  - H is the carry/borrow out of bit 3 (bit 11 for 16-bit ops).
 *  - P/V holds signed overflow for arithmetic and even-parity for logic/rotate.
 *  - N records whether the last op was a subtract (used by DAA).
 *  - X (bit 3) and Y (bit 5) are undocumented copies of result bits 3 and 5,
 *    except CP and BIT take them from the operand, and 16-bit ops take them
 *    from the high byte of the result. */
#include "z80_cpu.h"

cz80_cpu_t cz80_cpu;

void cz80_cpu_reset(void) {
    /* Z80 reset: PC and the special regs clear; the rest is nominally
     * undefined but we zero it for determinism. SP is conventionally left as
     * the firmware sets it; the TI boot code loads it, so 0 is a safe start. */
    cz80_cpu.af.w = cz80_cpu.bc.w = cz80_cpu.de.w = cz80_cpu.hl.w = 0;
    cz80_cpu.ix.w = cz80_cpu.iy.w = cz80_cpu.sp.w = cz80_cpu.pc.w = 0;
    cz80_cpu.af_.w = cz80_cpu.bc_.w = cz80_cpu.de_.w = cz80_cpu.hl_.w = 0;
    cz80_cpu.i = cz80_cpu.r = 0;
    cz80_cpu.iff1 = cz80_cpu.iff2 = 0;
    cz80_cpu.im = 0;
    cz80_cpu.halted = 0;
}

/* Even parity: 1 when the number of set bits in v is even. */
static int cz80_parity(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (~v) & 1;
}

/* --- 8-bit add/subtract --- */

uint8_t cz80_add8(uint8_t a, uint8_t v) {
    unsigned r = (unsigned)a + v;
    uint8_t res = (uint8_t)r;
    uint8_t f = 0;
    f |= res & CZ80_FLAG_S;
    if (res == 0)                                    f |= CZ80_FLAG_Z;
    if (((a & 0x0F) + (v & 0x0F)) & 0x10)            f |= CZ80_FLAG_H;
    if ((~(a ^ v) & (a ^ res)) & 0x80)               f |= CZ80_FLAG_PV;
    if (r & 0x100)                                   f |= CZ80_FLAG_C;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);              /* N = 0 */
    cz80_f = f;
    return res;
}

uint8_t cz80_adc8(uint8_t a, uint8_t v) {
    unsigned c = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    unsigned r = (unsigned)a + v + c;
    uint8_t res = (uint8_t)r;
    uint8_t f = 0;
    f |= res & CZ80_FLAG_S;
    if (res == 0)                                    f |= CZ80_FLAG_Z;
    if (((a & 0x0F) + (v & 0x0F) + c) & 0x10)        f |= CZ80_FLAG_H;
    if ((~(a ^ v) & (a ^ res)) & 0x80)               f |= CZ80_FLAG_PV;
    if (r & 0x100)                                   f |= CZ80_FLAG_C;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);              /* N = 0 */
    cz80_f = f;
    return res;
}

uint8_t cz80_sub8(uint8_t a, uint8_t v) {
    unsigned r = (unsigned)a - v;
    uint8_t res = (uint8_t)r;
    uint8_t f = CZ80_FLAG_N;
    f |= res & CZ80_FLAG_S;
    if (res == 0)                                    f |= CZ80_FLAG_Z;
    if (((a & 0x0F) - (v & 0x0F)) & 0x10)            f |= CZ80_FLAG_H;
    if (((a ^ v) & (a ^ res)) & 0x80)                f |= CZ80_FLAG_PV;
    if (r & 0x100)                                   f |= CZ80_FLAG_C;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

uint8_t cz80_sbc8(uint8_t a, uint8_t v) {
    unsigned c = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    unsigned r = (unsigned)a - v - c;
    uint8_t res = (uint8_t)r;
    uint8_t f = CZ80_FLAG_N;
    f |= res & CZ80_FLAG_S;
    if (res == 0)                                    f |= CZ80_FLAG_Z;
    if (((a & 0x0F) - (v & 0x0F) - c) & 0x10)        f |= CZ80_FLAG_H;
    if (((a ^ v) & (a ^ res)) & 0x80)                f |= CZ80_FLAG_PV;
    if (r & 0x100)                                   f |= CZ80_FLAG_C;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

void cz80_cp8(uint8_t a, uint8_t v) {
    /* CP is SUB without storing the result; X/Y come from the operand v. */
    unsigned r = (unsigned)a - v;
    uint8_t res = (uint8_t)r;
    uint8_t f = CZ80_FLAG_N;
    f |= res & CZ80_FLAG_S;
    if (res == 0)                                    f |= CZ80_FLAG_Z;
    if (((a & 0x0F) - (v & 0x0F)) & 0x10)            f |= CZ80_FLAG_H;
    if (((a ^ v) & (a ^ res)) & 0x80)                f |= CZ80_FLAG_PV;
    if (r & 0x100)                                   f |= CZ80_FLAG_C;
    f |= v & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
}

/* --- 8-bit logic --- */

uint8_t cz80_and8(uint8_t a, uint8_t v) {
    uint8_t res = a & v;
    uint8_t f = CZ80_FLAG_H;                           /* AND sets H, clears C/N */
    f |= res & CZ80_FLAG_S;
    if (res == 0)            f |= CZ80_FLAG_Z;
    if (cz80_parity(res))      f |= CZ80_FLAG_PV;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

uint8_t cz80_or8(uint8_t a, uint8_t v) {
    uint8_t res = a | v;
    uint8_t f = 0;                                   /* OR clears H/C/N */
    f |= res & CZ80_FLAG_S;
    if (res == 0)            f |= CZ80_FLAG_Z;
    if (cz80_parity(res))      f |= CZ80_FLAG_PV;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

uint8_t cz80_xor8(uint8_t a, uint8_t v) {
    uint8_t res = a ^ v;
    uint8_t f = 0;                                   /* XOR clears H/C/N */
    f |= res & CZ80_FLAG_S;
    if (res == 0)            f |= CZ80_FLAG_Z;
    if (cz80_parity(res))      f |= CZ80_FLAG_PV;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

/* --- INC/DEC: carry is preserved --- */

uint8_t cz80_inc8(uint8_t v) {
    uint8_t res = (uint8_t)(v + 1);
    uint8_t f = cz80_f & CZ80_FLAG_C;                    /* C untouched, N = 0 */
    f |= res & CZ80_FLAG_S;
    if (res == 0)            f |= CZ80_FLAG_Z;
    if ((v & 0x0F) == 0x0F)  f |= CZ80_FLAG_H;
    if (v == 0x7F)           f |= CZ80_FLAG_PV;        /* overflow 0x7F->0x80 */
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

uint8_t cz80_dec8(uint8_t v) {
    uint8_t res = (uint8_t)(v - 1);
    uint8_t f = (cz80_f & CZ80_FLAG_C) | CZ80_FLAG_N;      /* C untouched, N = 1 */
    f |= res & CZ80_FLAG_S;
    if (res == 0)            f |= CZ80_FLAG_Z;
    if ((v & 0x0F) == 0x00)  f |= CZ80_FLAG_H;
    if (v == 0x80)           f |= CZ80_FLAG_PV;        /* overflow 0x80->0x7F */
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

/* --- 16-bit add/subtract --- */

uint16_t cz80_add16(uint16_t a, uint16_t v) {
    unsigned r = (unsigned)a + v;
    uint16_t res = (uint16_t)r;
    /* ADD HL,rr preserves S, Z, P/V. */
    uint8_t f = cz80_f & (CZ80_FLAG_S | CZ80_FLAG_Z | CZ80_FLAG_PV);
    if (((a & 0x0FFF) + (v & 0x0FFF)) & 0x1000)      f |= CZ80_FLAG_H;
    if (r & 0x10000)                                 f |= CZ80_FLAG_C;
    f |= (res >> 8) & (CZ80_FLAG_X | CZ80_FLAG_Y);       /* from high byte, N = 0 */
    cz80_f = f;
    return res;
}

uint16_t cz80_adc16(uint16_t a, uint16_t v) {
    unsigned c = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    unsigned r = (unsigned)a + v + c;
    uint16_t res = (uint16_t)r;
    uint8_t f = 0;
    f |= (res >> 8) & CZ80_FLAG_S;
    if (res == 0)                                       f |= CZ80_FLAG_Z;
    if (((a & 0x0FFF) + (v & 0x0FFF) + c) & 0x1000)     f |= CZ80_FLAG_H;
    if ((~(a ^ v) & (a ^ res)) & 0x8000)                f |= CZ80_FLAG_PV;
    if (r & 0x10000)                                    f |= CZ80_FLAG_C;
    f |= (res >> 8) & (CZ80_FLAG_X | CZ80_FLAG_Y);          /* N = 0 */
    cz80_f = f;
    return res;
}

uint16_t cz80_sbc16(uint16_t a, uint16_t v) {
    unsigned c = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    unsigned r = (unsigned)a - v - c;
    uint16_t res = (uint16_t)r;
    uint8_t f = CZ80_FLAG_N;
    f |= (res >> 8) & CZ80_FLAG_S;
    if (res == 0)                                       f |= CZ80_FLAG_Z;
    if (((a & 0x0FFF) - (v & 0x0FFF) - c) & 0x1000)     f |= CZ80_FLAG_H;
    if (((a ^ v) & (a ^ res)) & 0x8000)                 f |= CZ80_FLAG_PV;
    if (r & 0x10000)                                    f |= CZ80_FLAG_C;
    f |= (res >> 8) & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

/* --- CB-prefixed rotates/shifts: full S/Z/P flags, H=N=0, X/Y from result --- */

static void rot_flags(uint8_t res, unsigned carry) {
    uint8_t f = 0;
    f |= res & CZ80_FLAG_S;
    if (res == 0)        f |= CZ80_FLAG_Z;
    if (cz80_parity(res))  f |= CZ80_FLAG_PV;
    if (carry)           f |= CZ80_FLAG_C;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
}

uint8_t cz80_rlc(uint8_t v) {
    unsigned c = (v >> 7) & 1;
    uint8_t res = (uint8_t)((v << 1) | c);
    rot_flags(res, c);
    return res;
}
uint8_t cz80_rrc(uint8_t v) {
    unsigned c = v & 1;
    uint8_t res = (uint8_t)((v >> 1) | (c << 7));
    rot_flags(res, c);
    return res;
}
uint8_t cz80_rl(uint8_t v) {
    unsigned c = (v >> 7) & 1;
    unsigned old = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    uint8_t res = (uint8_t)((v << 1) | old);
    rot_flags(res, c);
    return res;
}
uint8_t cz80_rr(uint8_t v) {
    unsigned c = v & 1;
    unsigned old = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    uint8_t res = (uint8_t)((v >> 1) | (old << 7));
    rot_flags(res, c);
    return res;
}
uint8_t cz80_sla(uint8_t v) {
    unsigned c = (v >> 7) & 1;
    uint8_t res = (uint8_t)(v << 1);
    rot_flags(res, c);
    return res;
}
uint8_t cz80_sra(uint8_t v) {
    unsigned c = v & 1;
    uint8_t res = (uint8_t)((v >> 1) | (v & 0x80));   /* keep sign bit */
    rot_flags(res, c);
    return res;
}
uint8_t cz80_sll(uint8_t v) {
    /* Undocumented SLL/SLS: shift left, force bit 0 to 1. */
    unsigned c = (v >> 7) & 1;
    uint8_t res = (uint8_t)((v << 1) | 1);
    rot_flags(res, c);
    return res;
}
uint8_t cz80_srl(uint8_t v) {
    unsigned c = v & 1;
    uint8_t res = (uint8_t)(v >> 1);
    rot_flags(res, c);
    return res;
}

/* --- accumulator rotates (RLCA/RRCA/RLA/RRA): preserve S/Z/P, clear H/N,
 * set C and X/Y from the result --- */

static void acc_rot_flags(uint8_t res, unsigned carry) {
    uint8_t f = cz80_f & (CZ80_FLAG_S | CZ80_FLAG_Z | CZ80_FLAG_PV);
    if (carry) f |= CZ80_FLAG_C;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);              /* H = 0, N = 0 */
    cz80_f = f;
}

uint8_t cz80_rlca(uint8_t a) {
    unsigned c = (a >> 7) & 1;
    uint8_t res = (uint8_t)((a << 1) | c);
    acc_rot_flags(res, c);
    return res;
}
uint8_t cz80_rrca(uint8_t a) {
    unsigned c = a & 1;
    uint8_t res = (uint8_t)((a >> 1) | (c << 7));
    acc_rot_flags(res, c);
    return res;
}
uint8_t cz80_rla(uint8_t a) {
    unsigned c = (a >> 7) & 1;
    unsigned old = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    uint8_t res = (uint8_t)((a << 1) | old);
    acc_rot_flags(res, c);
    return res;
}
uint8_t cz80_rra(uint8_t a) {
    unsigned c = a & 1;
    unsigned old = (cz80_f & CZ80_FLAG_C) ? 1u : 0u;
    uint8_t res = (uint8_t)((a >> 1) | (old << 7));
    acc_rot_flags(res, c);
    return res;
}

/* --- DAA: correct the accumulator to packed BCD after an add/sub --- */

uint8_t cz80_daa(uint8_t a) {
    int n = (cz80_f & CZ80_FLAG_N) != 0;
    int h = (cz80_f & CZ80_FLAG_H) != 0;
    int c = (cz80_f & CZ80_FLAG_C) != 0;
    uint8_t corr = 0;
    int newc = 0;
    if (h || (a & 0x0F) > 9) corr |= 0x06;
    if (c || a > 0x99)       { corr |= 0x60; newc = 1; }

    uint8_t res;
    int newh;
    if (n) {
        res  = (uint8_t)(a - corr);
        newh = h && ((a & 0x0F) < 6);               /* half-borrow */
    } else {
        res  = (uint8_t)(a + corr);
        newh = ((a & 0x0F) + (corr & 0x0F)) > 0x0F; /* half-carry */
    }

    uint8_t f = 0;
    f |= res & CZ80_FLAG_S;
    if (res == 0)        f |= CZ80_FLAG_Z;
    if (newh)            f |= CZ80_FLAG_H;
    if (cz80_parity(res))  f |= CZ80_FLAG_PV;
    if (n)               f |= CZ80_FLAG_N;             /* N preserved */
    if (newc)            f |= CZ80_FLAG_C;
    f |= res & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
    return res;
}

/* --- BIT b,r: test a bit; Z reflects the bit, X/Y come from the operand --- */

void cz80_bit(uint8_t bit, uint8_t v) {
    uint8_t isset = v & (uint8_t)(1u << bit);
    uint8_t f = (cz80_f & CZ80_FLAG_C) | CZ80_FLAG_H;      /* C preserved, H set, N=0 */
    if (!isset) f |= CZ80_FLAG_Z | CZ80_FLAG_PV;         /* Z and parity together */
    if (bit == 7 && isset) f |= CZ80_FLAG_S;           /* S only meaningful for b7 */
    f |= v & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
}
