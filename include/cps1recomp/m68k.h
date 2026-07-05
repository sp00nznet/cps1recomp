/*
 * m68k.h — Motorola 68000 CPU context and instruction macros.
 *
 * This file defines the runtime representation of the 68000's register
 * file and provides C macros that faithfully reproduce every 68k instruction's
 * behavior, including condition code flag updates.
 *
 * The approach: each 68k instruction becomes a macro call that operates on
 * the global CPU context (g_m68k). For example:
 *
 *   Original 68k:    add.w  d0, d1
 *   Recompiled C:    M68K_ADD16(g_m68k.d[1], g_m68k.d[0])
 *
 * The macro updates d1, then sets/clears the C, V, Z, N, X flags exactly
 * as the real 68000 would. This is critical for correct recompilation —
 * subsequent branch instructions (beq, bne, bgt, etc.) test these flags.
 *
 * Architecture note: The 68000 is big-endian with 32-bit registers. When
 * an instruction operates on a byte (.b) or word (.w), only the low 8 or
 * 16 bits of the register are affected — the upper bits are preserved.
 * The macros handle this correctly.
 *
 * Adapted from genrecomp/neogeorecomp (sp00nznet) for CPS1 use.
 * The 68000 CPU is identical across Genesis, Neo Geo, CPS1/2, and System 16.
 */

#ifndef CPS1RECOMP_M68K_H
#define CPS1RECOMP_M68K_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ----- CPU Context ----- */

typedef struct {
    uint32_t d[8];      /* Data registers D0-D7 */
    uint32_t a[8];      /* Address registers A0-A7 (A7 = active stack pointer) */
    uint32_t usp;       /* User stack pointer (saved when in supervisor mode) */
    uint32_t ssp;       /* Supervisor stack pointer */
    uint32_t pc;        /* Program counter */

    /* Condition Code Register (CCR) flags — stored individually for speed */
    bool flag_c;        /* Carry: set on unsigned overflow/borrow */
    bool flag_v;        /* Overflow: set on signed overflow */
    bool flag_z;        /* Zero: set when result is zero */
    bool flag_n;        /* Negative: set when result MSB is 1 */
    bool flag_x;        /* Extend: "sticky carry" for multi-precision arithmetic */

    /* Status register upper byte */
    bool supervisor;    /* Supervisor mode flag (S bit) */
    uint8_t int_mask;   /* Interrupt priority mask (3 bits, levels 0-7) */
    bool trace;         /* Trace mode flag (T bit) */

    /* Internal state */
    bool stopped;       /* CPU is stopped (STOP instruction executed) */
    bool halted;        /* CPU is halted (double bus fault) */
} m68k_context_t;

extern m68k_context_t g_m68k;

/* ----- Initialization ----- */

void m68k_init(void);
void m68k_load_vectors(const uint8_t *rom);

/* ----- Status Register Helpers ----- */

uint8_t m68k_get_ccr(void);
void m68k_set_ccr(uint8_t ccr);
uint16_t m68k_get_sr(void);
void m68k_set_sr(uint16_t sr);

/* ----- Instruction Macros ----- */

/* --- ADD: dst = dst + src, update XNZVC --- */
#define M68K_ADD8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d + (uint16_t)_s; \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFF); \
    g_m68k.flag_v = ((_d ^ _res) & (_s ^ _res) & 0x80) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_ADD16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d + (uint32_t)_s; \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFF); \
    g_m68k.flag_v = ((_d ^ _res) & (_s ^ _res) & 0x8000) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_ADD32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d + (uint64_t)_s; \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFFFFFFu); \
    g_m68k.flag_v = ((_d ^ _res) & (_s ^ _res) & 0x80000000u) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- SUB: dst = dst - src, update XNZVC --- */
#define M68K_SUB8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d - (uint16_t)_s; \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_d < _s); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x80) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_SUB16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d - (uint32_t)_s; \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_d < _s); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x8000) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_SUB32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d - (uint64_t)_s; \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_d < _s); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x80000000u) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- CMP: compare (subtract without storing), update NZVC (not X) --- */
#define M68K_CMP8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d - (uint16_t)_s; \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_c = (_d < _s); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x80) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x80) != 0; \
} while(0)

#define M68K_CMP16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d - (uint32_t)_s; \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_c = (_d < _s); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x8000) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x8000) != 0; \
} while(0)

#define M68K_CMP32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d - (uint64_t)_s; \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_c = (_d < _s); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x80000000u) != 0; \
    g_m68k.flag_z = (_res == 0); \
    g_m68k.flag_n = (_res & 0x80000000u) != 0; \
} while(0)

/* --- AND: dst = dst & src, update NZ, clear CV --- */
#define M68K_AND8(dst, src) do { \
    uint8_t _res = (uint8_t)(dst) & (uint8_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_AND16(dst, src) do { \
    uint16_t _res = (uint16_t)(dst) & (uint16_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_AND32(dst, src) do { \
    uint32_t _res = (uint32_t)(dst) & (uint32_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- OR: dst = dst | src, update NZ, clear CV --- */
#define M68K_OR8(dst, src) do { \
    uint8_t _res = (uint8_t)(dst) | (uint8_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_OR16(dst, src) do { \
    uint16_t _res = (uint16_t)(dst) | (uint16_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_OR32(dst, src) do { \
    uint32_t _res = (uint32_t)(dst) | (uint32_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- EOR: dst = dst ^ src, update NZ, clear CV --- */
#define M68K_EOR8(dst, src) do { \
    uint8_t _res = (uint8_t)(dst) ^ (uint8_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_EOR16(dst, src) do { \
    uint16_t _res = (uint16_t)(dst) ^ (uint16_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_EOR32(dst, src) do { \
    uint32_t _res = (uint32_t)(dst) ^ (uint32_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- TST: test operand, update NZ, clear CV --- */
#define M68K_TST8(val) do { \
    uint8_t _v = (uint8_t)(val); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_v == 0); g_m68k.flag_n = (_v & 0x80) != 0; \
} while(0)

#define M68K_TST16(val) do { \
    uint16_t _v = (uint16_t)(val); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_v == 0); g_m68k.flag_n = (_v & 0x8000) != 0; \
} while(0)

#define M68K_TST32(val) do { \
    uint32_t _v = (uint32_t)(val); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_v == 0); g_m68k.flag_n = (_v & 0x80000000u) != 0; \
} while(0)

/* --- NEG: dst = 0 - dst, update XNZVC --- */
#define M68K_NEG8(dst) do { \
    uint8_t _d = (uint8_t)(dst); \
    uint8_t _res = (uint8_t)(-(int8_t)_d); \
    g_m68k.flag_c = g_m68k.flag_x = (_d != 0); \
    g_m68k.flag_v = (_d == 0x80); \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_NEG16(dst) do { \
    uint16_t _d = (uint16_t)(dst); \
    uint16_t _res = (uint16_t)(-(int16_t)_d); \
    g_m68k.flag_c = g_m68k.flag_x = (_d != 0); \
    g_m68k.flag_v = (_d == 0x8000); \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_NEG32(dst) do { \
    uint32_t _d = (uint32_t)(dst); \
    uint32_t _res = (uint32_t)(-(int32_t)_d); \
    g_m68k.flag_c = g_m68k.flag_x = (_d != 0); \
    g_m68k.flag_v = (_d == 0x80000000u); \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- NOT: dst = ~dst, update NZ, clear CV --- */
#define M68K_NOT8(dst) do { \
    uint8_t _res = ~(uint8_t)(dst); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_NOT16(dst) do { \
    uint16_t _res = ~(uint16_t)(dst); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_NOT32(dst) do { \
    uint32_t _res = ~(uint32_t)(dst); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- MULU: unsigned 16x16 -> 32, update NZ, clear CV --- */
#define M68K_MULU(dst, src) do { \
    uint32_t _res = (uint32_t)(uint16_t)(dst) * (uint32_t)(uint16_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- MULS: signed 16x16 -> 32, update NZ, clear CV --- */
#define M68K_MULS(dst, src) do { \
    int32_t _res = (int32_t)(int16_t)(dst) * (int32_t)(int16_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_res == 0); g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = (uint32_t)_res; \
} while(0)

/* --- DIVU: unsigned 32/16 -> 16q:16r --- */
#define M68K_DIVU(dst, src) do { \
    uint16_t _divisor = (uint16_t)(src); \
    if (_divisor != 0) { \
        uint32_t _dividend = (uint32_t)(dst); \
        uint32_t _quotient = _dividend / _divisor; \
        if (_quotient > 0xFFFF) { \
            g_m68k.flag_v = true; g_m68k.flag_c = false; \
        } else { \
            uint16_t _remainder = (uint16_t)(_dividend % _divisor); \
            (dst) = ((uint32_t)_remainder << 16) | (uint16_t)_quotient; \
            g_m68k.flag_v = false; g_m68k.flag_c = false; \
            g_m68k.flag_z = ((uint16_t)_quotient == 0); \
            g_m68k.flag_n = ((uint16_t)_quotient & 0x8000) != 0; \
        } \
    } \
} while(0)

/* --- DIVS: signed 32/16 -> 16q:16r --- */
#define M68K_DIVS(dst, src) do { \
    int16_t _divisor = (int16_t)(src); \
    if (_divisor != 0) { \
        int32_t _dividend = (int32_t)(dst); \
        int32_t _quotient = _dividend / _divisor; \
        if (_quotient < -32768 || _quotient > 32767) { \
            g_m68k.flag_v = true; g_m68k.flag_c = false; \
        } else { \
            int16_t _remainder = (int16_t)(_dividend % _divisor); \
            (dst) = ((uint32_t)(uint16_t)_remainder << 16) | (uint16_t)(int16_t)_quotient; \
            g_m68k.flag_v = false; g_m68k.flag_c = false; \
            g_m68k.flag_z = ((int16_t)_quotient == 0); \
            g_m68k.flag_n = ((int16_t)_quotient < 0); \
        } \
    } \
} while(0)

/* --- Shift/Rotate operations --- */

#define M68K_LSL8(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint8_t _d = (uint8_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 8) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (8 - _cnt)) & 1) != 0; \
            _d <<= _cnt; \
        } else { g_m68k.flag_c = g_m68k.flag_x = false; _d = 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _d; \
} while(0)

#define M68K_LSL16(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint16_t _d = (uint16_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 16) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (16 - _cnt)) & 1) != 0; \
            _d <<= _cnt; \
        } else { g_m68k.flag_c = g_m68k.flag_x = false; _d = 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _d; \
} while(0)

#define M68K_LSL32(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint32_t _d = (uint32_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 32) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (32 - _cnt)) & 1) != 0; \
            _d = (_cnt < 32) ? (_d << _cnt) : 0; \
        } else { g_m68k.flag_c = g_m68k.flag_x = false; _d = 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80000000u) != 0; \
    (dst) = _d; \
} while(0)

#define M68K_LSR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint8_t _d = (uint8_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 8) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (_cnt - 1)) & 1) != 0; \
            _d >>= _cnt; \
        } else { g_m68k.flag_c = g_m68k.flag_x = false; _d = 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = false; \
    (dst) = ((dst) & 0xFFFFFF00u) | _d; \
} while(0)

#define M68K_LSR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint16_t _d = (uint16_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 16) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (_cnt - 1)) & 1) != 0; \
            _d >>= _cnt; \
        } else { g_m68k.flag_c = g_m68k.flag_x = false; _d = 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = false; \
    (dst) = ((dst) & 0xFFFF0000u) | _d; \
} while(0)

#define M68K_LSR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint32_t _d = (uint32_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 32) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (_cnt - 1)) & 1) != 0; \
            _d = (_cnt < 32) ? (_d >> _cnt) : 0; \
        } else { g_m68k.flag_c = g_m68k.flag_x = false; _d = 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = false; \
    (dst) = _d; \
} while(0)

#define M68K_ASR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    int8_t _d = (int8_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 8) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (_cnt - 1)) & 1) != 0; \
            _d >>= _cnt; \
        } else { g_m68k.flag_c = g_m68k.flag_x = (_d < 0); _d = (_d < 0) ? -1 : 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = ((uint8_t)_d == 0); g_m68k.flag_n = (_d < 0); \
    (dst) = ((dst) & 0xFFFFFF00u) | (uint8_t)_d; \
} while(0)

#define M68K_ASR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    int16_t _d = (int16_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 16) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (_cnt - 1)) & 1) != 0; \
            _d >>= _cnt; \
        } else { g_m68k.flag_c = g_m68k.flag_x = (_d < 0); _d = (_d < 0) ? -1 : 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = ((uint16_t)_d == 0); g_m68k.flag_n = (_d < 0); \
    (dst) = ((dst) & 0xFFFF0000u) | (uint16_t)_d; \
} while(0)

#define M68K_ASR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    int32_t _d = (int32_t)(dst); \
    if (_cnt > 0) { \
        if (_cnt <= 32) { \
            g_m68k.flag_c = g_m68k.flag_x = ((_d >> (_cnt - 1)) & 1) != 0; \
            _d = (_cnt < 32) ? (_d >> _cnt) : ((_d < 0) ? -1 : 0); \
        } else { g_m68k.flag_c = g_m68k.flag_x = (_d < 0); _d = (_d < 0) ? -1 : 0; } \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = ((uint32_t)_d == 0); g_m68k.flag_n = (_d < 0); \
    (dst) = (uint32_t)_d; \
} while(0)

#define M68K_ROL16(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint16_t _d = (uint16_t)(dst); \
    if (_cnt > 0) { \
        _cnt %= 16; \
        _d = (_d << _cnt) | (_d >> (16 - _cnt)); \
        g_m68k.flag_c = (_d & 1) != 0; \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _d; \
} while(0)

#define M68K_ROR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint16_t _d = (uint16_t)(dst); \
    if (_cnt > 0) { \
        _cnt %= 16; \
        _d = (_d >> _cnt) | (_d << (16 - _cnt)); \
        g_m68k.flag_c = (_d & 0x8000) != 0; \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _d; \
} while(0)

/* --- ROXL/ROXR: rotate through the X (extend) bit ---
 * The X bit acts as a 9th/17th/33rd bit. Loop-based so any rotate count is
 * exact. With count 0, X is unaffected and C is loaded from X. Otherwise X and
 * C both receive the last bit rotated out. V is always cleared; N/Z from result. */
#define M68K_ROXL8(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; uint8_t _d = (uint8_t)(dst); \
    int _x = g_m68k.flag_x ? 1 : 0; \
    if (_cnt == 0) { g_m68k.flag_c = g_m68k.flag_x; } \
    else { for (uint8_t _i = 0; _i < _cnt; _i++) { int _nx = (_d >> 7) & 1; _d = (uint8_t)((_d << 1) | _x); _x = _nx; } \
           g_m68k.flag_c = g_m68k.flag_x = _x; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _d; \
} while(0)

#define M68K_ROXL16(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; uint16_t _d = (uint16_t)(dst); \
    int _x = g_m68k.flag_x ? 1 : 0; \
    if (_cnt == 0) { g_m68k.flag_c = g_m68k.flag_x; } \
    else { for (uint8_t _i = 0; _i < _cnt; _i++) { int _nx = (_d >> 15) & 1; _d = (uint16_t)((_d << 1) | _x); _x = _nx; } \
           g_m68k.flag_c = g_m68k.flag_x = _x; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _d; \
} while(0)

#define M68K_ROXL32(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; uint32_t _d = (uint32_t)(dst); \
    int _x = g_m68k.flag_x ? 1 : 0; \
    if (_cnt == 0) { g_m68k.flag_c = g_m68k.flag_x; } \
    else { for (uint8_t _i = 0; _i < _cnt; _i++) { int _nx = (_d >> 31) & 1; _d = (_d << 1) | (uint32_t)_x; _x = _nx; } \
           g_m68k.flag_c = g_m68k.flag_x = _x; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80000000u) != 0; \
    (dst) = _d; \
} while(0)

#define M68K_ROXR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; uint8_t _d = (uint8_t)(dst); \
    int _x = g_m68k.flag_x ? 1 : 0; \
    if (_cnt == 0) { g_m68k.flag_c = g_m68k.flag_x; } \
    else { for (uint8_t _i = 0; _i < _cnt; _i++) { int _nx = _d & 1; _d = (uint8_t)((_d >> 1) | (_x << 7)); _x = _nx; } \
           g_m68k.flag_c = g_m68k.flag_x = _x; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _d; \
} while(0)

#define M68K_ROXR16(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; uint16_t _d = (uint16_t)(dst); \
    int _x = g_m68k.flag_x ? 1 : 0; \
    if (_cnt == 0) { g_m68k.flag_c = g_m68k.flag_x; } \
    else { for (uint8_t _i = 0; _i < _cnt; _i++) { int _nx = _d & 1; _d = (uint16_t)((_d >> 1) | (_x << 15)); _x = _nx; } \
           g_m68k.flag_c = g_m68k.flag_x = _x; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _d; \
} while(0)

#define M68K_ROXR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; uint32_t _d = (uint32_t)(dst); \
    int _x = g_m68k.flag_x ? 1 : 0; \
    if (_cnt == 0) { g_m68k.flag_c = g_m68k.flag_x; } \
    else { for (uint8_t _i = 0; _i < _cnt; _i++) { int _nx = _d & 1; _d = (_d >> 1) | ((uint32_t)_x << 31); _x = _nx; } \
           g_m68k.flag_c = g_m68k.flag_x = _x; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80000000u) != 0; \
    (dst) = _d; \
} while(0)

/* --- BCD arithmetic (byte only): ABCD/SBCD/NBCD ---
 * Packed binary-coded-decimal add/subtract/negate, honouring the X bit.
 * Z is only cleared (never set) so it stays valid across multi-byte chains. */
#define M68K_ABCD8(dst, src) do { \
    uint32_t _s = (uint8_t)(src), _d = (uint8_t)(dst); \
    uint32_t _res = (_s & 0x0f) + (_d & 0x0f) + (g_m68k.flag_x ? 1u : 0u); \
    if (_res > 9) _res += 6; \
    _res += (_s & 0xf0) + (_d & 0xf0); \
    g_m68k.flag_c = g_m68k.flag_x = (_res > 0x99); \
    if (_res > 0x99) _res -= 0xA0; \
    _res &= 0xff; \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_SBCD8(dst, src) do { \
    uint32_t _s = (uint8_t)(src), _d = (uint8_t)(dst); \
    uint32_t _res = (_d & 0x0f) - (_s & 0x0f) - (g_m68k.flag_x ? 1u : 0u); \
    if (_res > 9) _res -= 6; \
    _res += (_d & 0xf0) - (_s & 0xf0); \
    g_m68k.flag_c = g_m68k.flag_x = (_res > 0x99); \
    if (_res > 0x99) _res += 0xA0; \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    _res &= 0xff; \
    if (_res != 0) g_m68k.flag_z = false; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_NBCD8(dst) do { \
    uint32_t _s = (uint8_t)(dst); \
    uint32_t _res = (0u - (_s & 0x0f)) - (g_m68k.flag_x ? 1u : 0u); \
    if (_res > 9) _res -= 6; \
    _res += 0u - (_s & 0xf0); \
    g_m68k.flag_c = g_m68k.flag_x = (_res > 0x99); \
    if (_res > 0x99) _res += 0xA0; \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    _res &= 0xff; \
    if (_res != 0) g_m68k.flag_z = false; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

/* --- SWAP: swap upper and lower words --- */
#define M68K_SWAP(dst) do { \
    uint32_t _d = (uint32_t)(dst); \
    _d = (_d >> 16) | (_d << 16); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80000000u) != 0; \
    (dst) = _d; \
} while(0)

/* --- EXT: sign-extend --- */
#define M68K_EXT16(dst) do { \
    int16_t _res = (int8_t)(dst); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = ((uint16_t)_res == 0); g_m68k.flag_n = (_res < 0); \
    (dst) = ((dst) & 0xFFFF0000u) | (uint16_t)_res; \
} while(0)

#define M68K_EXT32(dst) do { \
    int32_t _res = (int16_t)(dst); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = ((uint32_t)_res == 0); g_m68k.flag_n = (_res < 0); \
    (dst) = (uint32_t)_res; \
} while(0)

/* --- CLR: clear destination --- */
#define M68K_CLR8(dst) do { \
    (dst) = (dst) & 0xFFFFFF00u; \
    g_m68k.flag_c = false; g_m68k.flag_v = false; g_m68k.flag_z = true; g_m68k.flag_n = false; \
} while(0)

#define M68K_CLR16(dst) do { \
    (dst) = (dst) & 0xFFFF0000u; \
    g_m68k.flag_c = false; g_m68k.flag_v = false; g_m68k.flag_z = true; g_m68k.flag_n = false; \
} while(0)

#define M68K_CLR32(dst) do { \
    (dst) = 0; \
    g_m68k.flag_c = false; g_m68k.flag_v = false; g_m68k.flag_z = true; g_m68k.flag_n = false; \
} while(0)

/* --- Bit operations --- */
#define M68K_BTST(val, bit) do { \
    g_m68k.flag_z = (((uint32_t)(val) >> ((bit) & 31)) & 1) == 0; \
} while(0)

#define M68K_BSET(dst, bit) do { \
    uint32_t _bit = (bit) & 31; \
    g_m68k.flag_z = (((uint32_t)(dst) >> _bit) & 1) == 0; \
    (dst) |= (1u << _bit); \
} while(0)

#define M68K_BCLR(dst, bit) do { \
    uint32_t _bit = (bit) & 31; \
    g_m68k.flag_z = (((uint32_t)(dst) >> _bit) & 1) == 0; \
    (dst) &= ~(1u << _bit); \
} while(0)

#define M68K_BCHG(dst, bit) do { \
    uint32_t _bit = (bit) & 31; \
    g_m68k.flag_z = (((uint32_t)(dst) >> _bit) & 1) == 0; \
    (dst) ^= (1u << _bit); \
} while(0)

/* --- ADDX/SUBX: with extend --- */
#define M68K_ADDX8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d + (uint16_t)_s + (g_m68k.flag_x ? 1u : 0u); \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFF); \
    g_m68k.flag_v = ((_d ^ _res) & (_s ^ _res) & 0x80) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_ADDX16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d + (uint32_t)_s + (g_m68k.flag_x ? 1u : 0u); \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFF); \
    g_m68k.flag_v = ((_d ^ _res) & (_s ^ _res) & 0x8000) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_ADDX32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d + (uint64_t)_s + (g_m68k.flag_x ? 1u : 0u); \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFFFFFFu); \
    g_m68k.flag_v = ((_d ^ _res) & (_s ^ _res) & 0x80000000u) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

#define M68K_SUBX8(dst, src) do { \
    uint8_t _s = (uint8_t)(src); uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = (uint16_t)_d - (uint16_t)_s - (g_m68k.flag_x ? 1u : 0u); \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFF); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x80) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_SUBX16(dst, src) do { \
    uint16_t _s = (uint16_t)(src); uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = (uint32_t)_d - (uint32_t)_s - (g_m68k.flag_x ? 1u : 0u); \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFF); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x8000) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

#define M68K_SUBX32(dst, src) do { \
    uint32_t _s = (uint32_t)(src); uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = (uint64_t)_d - (uint64_t)_s - (g_m68k.flag_x ? 1u : 0u); \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFFFFFFu); \
    g_m68k.flag_v = ((_d ^ _s) & (_d ^ _res) & 0x80000000u) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- NEGX: negate with extend --- */
#define M68K_NEGX32(dst) do { \
    uint32_t _d = (uint32_t)(dst); \
    uint64_t _r = 0ULL - (uint64_t)_d - (g_m68k.flag_x ? 1u : 0u); \
    uint32_t _res = (uint32_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFFFFFFu); \
    g_m68k.flag_v = (_d != 0 && _res == _d && g_m68k.flag_x); \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x80000000u) != 0; \
    (dst) = _res; \
} while(0)

/* --- NEGX8/16: negate with extend (8/16-bit) --- */
#define M68K_NEGX8(dst) do { \
    uint8_t _d = (uint8_t)(dst); \
    uint16_t _r = 0u - (uint16_t)_d - (g_m68k.flag_x ? 1u : 0u); \
    uint8_t _res = (uint8_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFF); \
    g_m68k.flag_v = ((_d & _res) & 0x80) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _res; \
} while(0)

#define M68K_NEGX16(dst) do { \
    uint16_t _d = (uint16_t)(dst); \
    uint32_t _r = 0u - (uint32_t)_d - (g_m68k.flag_x ? 1u : 0u); \
    uint16_t _res = (uint16_t)_r; \
    g_m68k.flag_c = g_m68k.flag_x = (_r > 0xFFFF); \
    g_m68k.flag_v = ((_d & _res) & 0x8000) != 0; \
    if (_res != 0) g_m68k.flag_z = false; \
    g_m68k.flag_n = (_res & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _res; \
} while(0)

/* --- ROL/ROR 8/32: rotate left/right (8-bit and 32-bit variants) --- */
#define M68K_ROL8(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint8_t _d = (uint8_t)(dst); \
    if (_cnt > 0) { \
        _cnt %= 8; \
        if (_cnt) _d = (_d << _cnt) | (_d >> (8 - _cnt)); \
        g_m68k.flag_c = (_d & 1) != 0; \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _d; \
} while(0)

#define M68K_ROL32(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint32_t _d = (uint32_t)(dst); \
    if (_cnt > 0) { \
        _cnt %= 32; \
        if (_cnt) _d = (_d << _cnt) | (_d >> (32 - _cnt)); \
        g_m68k.flag_c = (_d & 1) != 0; \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80000000u) != 0; \
    (dst) = _d; \
} while(0)

#define M68K_ROR8(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint8_t _d = (uint8_t)(dst); \
    if (_cnt > 0) { \
        _cnt %= 8; \
        if (_cnt) _d = (_d >> _cnt) | (_d << (8 - _cnt)); \
        g_m68k.flag_c = (_d & 0x80) != 0; \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _d; \
} while(0)

#define M68K_ROR32(dst, count) do { \
    uint8_t _cnt = (uint8_t)(count) & 63; \
    uint32_t _d = (uint32_t)(dst); \
    if (_cnt > 0) { \
        _cnt %= 32; \
        if (_cnt) _d = (_d >> _cnt) | (_d << (32 - _cnt)); \
        g_m68k.flag_c = (_d & 0x80000000u) != 0; \
    } else { g_m68k.flag_c = false; } \
    g_m68k.flag_v = false; g_m68k.flag_z = (_d == 0); g_m68k.flag_n = (_d & 0x80000000u) != 0; \
    (dst) = _d; \
} while(0)

/* --- MOVE: dst = src, update NZ, clear CV --- */
#define M68K_MOVE8(dst, src) do { \
    uint8_t _v = (uint8_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_v == 0); g_m68k.flag_n = (_v & 0x80) != 0; \
    (dst) = ((dst) & 0xFFFFFF00u) | _v; \
} while(0)

#define M68K_MOVE16(dst, src) do { \
    uint16_t _v = (uint16_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_v == 0); g_m68k.flag_n = (_v & 0x8000) != 0; \
    (dst) = ((dst) & 0xFFFF0000u) | _v; \
} while(0)

#define M68K_MOVE32(dst, src) do { \
    uint32_t _v = (uint32_t)(src); \
    g_m68k.flag_c = false; g_m68k.flag_v = false; \
    g_m68k.flag_z = (_v == 0); g_m68k.flag_n = (_v & 0x80000000u) != 0; \
    (dst) = _v; \
} while(0)

/* MOVEA: move to address register — NO flag updates */
#define M68K_MOVEA16(dst, src) do { (dst) = (uint32_t)(int32_t)(int16_t)(src); } while(0)
#define M68K_MOVEA32(dst, src) do { (dst) = (uint32_t)(src); } while(0)

/* --- Condition Code Tests --- */
#define M68K_CC_T    (true)
#define M68K_CC_F    (false)
#define M68K_CC_HI   (!g_m68k.flag_c && !g_m68k.flag_z)
#define M68K_CC_LS   (g_m68k.flag_c || g_m68k.flag_z)
#define M68K_CC_CC   (!g_m68k.flag_c)
#define M68K_CC_CS   (g_m68k.flag_c)
#define M68K_CC_NE   (!g_m68k.flag_z)
#define M68K_CC_EQ   (g_m68k.flag_z)
#define M68K_CC_VC   (!g_m68k.flag_v)
#define M68K_CC_VS   (g_m68k.flag_v)
#define M68K_CC_PL   (!g_m68k.flag_n)
#define M68K_CC_MI   (g_m68k.flag_n)
#define M68K_CC_GE   (g_m68k.flag_n == g_m68k.flag_v)
#define M68K_CC_LT   (g_m68k.flag_n != g_m68k.flag_v)
#define M68K_CC_GT   (!g_m68k.flag_z && (g_m68k.flag_n == g_m68k.flag_v))
#define M68K_CC_LE   (g_m68k.flag_z || (g_m68k.flag_n != g_m68k.flag_v))

#ifdef __cplusplus
}
#endif

#endif /* CPS1RECOMP_M68K_H */
