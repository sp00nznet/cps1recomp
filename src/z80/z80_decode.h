/* decode.h - Zilog Z80 instruction decoder.
 *
 * The TI-83/84 Plus family runs a stock Zilog Z80 (a ~6-15 MHz, depending on
 * model). TI assembly programs are raw Z80 machine code: the documented main
 * set plus the four prefix planes CB / ED / DD / FD (and the two double
 * prefixes DDCB / FDCB). Each prefix re-interprets the byte(s) that follow:
 *   CB   - rotate/shift/BIT/RES/SET            (always 2 bytes)
 *   ED   - extended LD/arith/block/IM/RETI/N   (2 bytes, 4 for LD rr,(nn))
 *   DD   - HL->IX,  (HL)->(IX+d)               (main length, +1 for the disp)
 *   FD   - HL->IY,  (HL)->(IY+d)               (main length, +1 for the disp)
 *   DDCB - CB op on (IX+d)                      (always 4 bytes)
 *   FDCB - CB op on (IY+d)                      (always 4 bytes)
 *
 * Getting the LENGTH and CONTROL-FLOW class right for every encoding is the
 * whole point: analyze.c does a linear sweep over a program image, and one
 * wrong length desynchronizes every instruction after it. This decoder is the
 * foundation of the recompiler: decode -> analyze -> emit.
 */
#ifndef Z80_DECODE_H
#define Z80_DECODE_H

#include <stdint.h>

/* Operand / addressing kind. Total length of a *main* opcode is derived from
 * this by z80_operand_len(); prefixed planes adjust from there. */
typedef enum {
    Z_NONE,    /* no operand                              (1 byte)  */
    Z_IMM8,    /* 8-bit immediate n                       (2 bytes) */
    Z_IMM16,   /* 16-bit immediate nn  (LD rr,nn)         (3 bytes) */
    Z_REL8,    /* signed relative e    (JR/DJNZ)          (2 bytes) */
    Z_ABS16,   /* absolute nn target   (JP/CALL nn)       (3 bytes) */
    Z_MEM16,   /* 16-bit memory addr   (LD (nn),A etc.)   (3 bytes) */
    Z_RST,     /* restart vector encoded in the opcode    (1 byte)  */
    Z_PORT,    /* I/O port n           (IN/OUT (n))       (2 bytes) */
    Z_IDX      /* (IX+d)/(IY+d) displacement (DD/FD only)           */
} z80_operand_t;

/* Which decode plane an instruction belongs to. */
typedef enum {
    ZP_NONE, ZP_CB, ZP_ED, ZP_DD, ZP_FD, ZP_DDCB, ZP_FDCB
} z80_prefix_t;

/* Control-flow class - drives function discovery in analyze.c. */
typedef enum {
    CF_NORMAL,  /* falls through                                       */
    CF_BRANCH,  /* conditional JR/JP/CALL/RET/DJNZ - may fall through   */
    CF_JMP,     /* unconditional JP/JR                                  */
    CF_CALL,    /* unconditional CALL                                   */
    CF_RET,     /* RET / RETI / RETN                                    */
    CF_RST,     /* RST p                                                */
    CF_STOP,    /* HALT                                                 */
    CF_JPHL     /* JP (HL)/(IX)/(IY) - computed, target unknown         */
} z80_cflow_t;

/* One main-table entry. operand/cflow are stored narrow; idx flags a main
 * opcode that references (HL) and therefore gains a displacement byte under a
 * DD/FD prefix (becoming (IX+d)/(IY+d)). */
typedef struct {
    const char *mnemonic;  /* template; "%s" marks the operand hole, if any */
    uint8_t     operand;   /* z80_operand_t */
    uint8_t     cflow;     /* z80_cflow_t   */
    uint8_t     idx;       /* 1 = uses (HL) -> indexable under DD/FD         */
} z80_opinfo_t;

/* One decoded instruction. */
typedef struct {
    uint16_t      pc;            /* address of the first opcode byte         */
    uint8_t       bytes[4];      /* raw machine bytes (up to len)            */
    uint8_t       len;           /* total instruction length (1..4)          */
    z80_prefix_t  prefix;        /* decode plane                             */
    uint8_t       opcode;        /* effective opcode (post-prefix byte)      */
    z80_operand_t operand_kind;  /* operand/addressing kind                  */
    z80_cflow_t   cflow;         /* control-flow class                       */
    const char   *mnemonic;      /* base/template mnemonic                   */
    uint16_t      imm;           /* immediate or memory address operand      */
    int8_t        disp;          /* (IX+d)/(IY+d) or relative displacement   */
    uint16_t      target;        /* resolved JR/JP/CALL/RST target, else 0   */
    uint8_t       cond_code;     /* condition (0..7), 0xFF if not applicable */
} z80_insn_t;

/* 256-entry main opcode table, indexed by the opcode byte. */
extern const z80_opinfo_t z80_optab[256];

/* Bytes consumed by a *main* opcode of the given operand kind (1/2/3). */
int z80_operand_len(z80_operand_t kind);

/* Decode one instruction. `mem` points at the first byte; `avail` is the
 * number of valid bytes remaining (guards against overread at image end);
 * `pc` is the address of the first byte (used to resolve JR/DJNZ targets).
 * Returns the instruction length, or 0 if `avail` is too small. */
int z80_decode(const uint8_t *mem, int avail, uint16_t pc, z80_insn_t *out);

/* Format a decoded instruction as a disassembly string (no address column),
 * e.g. "LD HL,$9D95", "JR NZ,$4012", "BIT 7,(IX+$04)", "OUT ($10),A". */
void z80_format(const z80_insn_t *in, char *buf, int bufsz);

#endif /* Z80_DECODE_H */
