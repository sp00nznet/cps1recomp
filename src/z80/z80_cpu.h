/* cpu.h - Zilog Z80 CPU state + ALU/flag helpers for recompiled TI-83/84+ code.
 *
 * The TI-83/84 Plus calculators run a Z80 at ~6/15 MHz. Recompiled routines
 * manipulate this state directly (register pairs, the shadow set, the flag
 * byte) and call cz80_mem_read/cz80_mem_write for memory and cz80_port_in/out for
 * I/O. This is the standard Z80 programmer's model.
 *
 * The flag math in the cz80_* ALU helpers is the load-bearing part of this
 * runtime: the Z80's half-carry, parity/overflow, add/subtract (N) flag, and
 * the undocumented X/Y bits (copies of result bits 3/5) must all be correct or
 * recompiled programs that branch on flags misbehave. See cpu.c. */
#ifndef CZ80_CPU_H
#define CZ80_CPU_H

#include <stdint.h>

/* A 16-bit register pair, addressable as the pair or as its two halves.
 * The Z80 is little-endian; in a pair like HL the "low" half is L and the
 * "high" half is H. We overlap a uint16_t with a {lo,hi} struct so the C
 * struct layout matches: byte 0 = low half, byte 1 = high half. This relies on
 * the common little-endian host layout for the overlap to read back as a
 * native uint16_t; the halves are always individually correct regardless. */
typedef union {
    uint16_t w;                     /* whole pair (e.g. HL)            */
    struct { uint8_t lo, hi; } b;   /* halves: lo=L/E/C/F, hi=H/D/B/A  */
} cz80_reg16_t;

typedef struct {
    /* Main register set. The .b.hi / .b.lo halves give the named 8-bit regs:
     *   af: hi=A,  lo=F      bc: hi=B, lo=C
     *   de: hi=D,  lo=E      hl: hi=H, lo=L                         */
    cz80_reg16_t af, bc, de, hl;
    cz80_reg16_t ix, iy;              /* index registers                 */
    cz80_reg16_t sp, pc;              /* stack pointer, program counter  */

    /* Shadow register set (EX AF,AF' and EXX swap these in).          */
    cz80_reg16_t af_, bc_, de_, hl_;

    uint8_t i;                      /* interrupt vector base register  */
    uint8_t r;                      /* memory refresh counter          */
    uint8_t iff1, iff2;             /* interrupt enable flip-flops     */
    uint8_t im;                     /* interrupt mode (0,1,2)          */
    uint8_t halted;                 /* set by HALT, cleared by INT     */
} cz80_cpu_t;

extern cz80_cpu_t cz80_cpu;

/* Convenience accessors for the live flag byte (F = low half of AF). */
#define cz80_a (cz80_cpu.af.b.hi)
#define cz80_f (cz80_cpu.af.b.lo)

/* Z80 flag bits in F. S/Z/H/P-V/N/C are documented; X and Y are the
 * undocumented bits 3 and 5, set to copies of result bits 3 and 5. */
#define CZ80_FLAG_C  0x01   /* carry                                  */
#define CZ80_FLAG_N  0x02   /* add/subtract (1 after a subtract)      */
#define CZ80_FLAG_PV 0x04   /* parity (logic) / overflow (arithmetic) */
#define CZ80_FLAG_X  0x08   /* undocumented: copy of result bit 3     */
#define CZ80_FLAG_H  0x10   /* half carry (carry out of bit 3)        */
#define CZ80_FLAG_Y  0x20   /* undocumented: copy of result bit 5     */
#define CZ80_FLAG_Z  0x40   /* zero                                   */
#define CZ80_FLAG_S  0x80   /* sign (copy of result bit 7)            */

void cz80_cpu_reset(void);

/* --- 8-bit ALU (operate on cz80_cpu.f; A is passed/returned by the caller) ---
 * The *8 add/sub helpers take the current accumulator value `a` and operand
 * `v`, return the 8-bit result, and set every flag the real opcode would. */
uint8_t cz80_add8(uint8_t a, uint8_t v);
uint8_t cz80_adc8(uint8_t a, uint8_t v);
uint8_t cz80_sub8(uint8_t a, uint8_t v);
uint8_t cz80_sbc8(uint8_t a, uint8_t v);
uint8_t cz80_and8(uint8_t a, uint8_t v);
uint8_t cz80_or8 (uint8_t a, uint8_t v);
uint8_t cz80_xor8(uint8_t a, uint8_t v);
void    cz80_cp8 (uint8_t a, uint8_t v);   /* compare: flags only, no result   */
uint8_t cz80_inc8(uint8_t v);              /* INC r: leaves C untouched        */
uint8_t cz80_dec8(uint8_t v);              /* DEC r: leaves C untouched        */

/* --- 16-bit ALU --- */
uint16_t cz80_add16(uint16_t a, uint16_t v);  /* ADD HL,rr  (H,N,C,X,Y)        */
uint16_t cz80_adc16(uint16_t a, uint16_t v);  /* ADC HL,rr  (full flags)       */
uint16_t cz80_sbc16(uint16_t a, uint16_t v);  /* SBC HL,rr  (full flags)       */

/* --- rotates / shifts (CB-prefixed forms: full S,Z,P,H=0,N=0,C,X,Y) --- */
uint8_t cz80_rlc(uint8_t v);
uint8_t cz80_rrc(uint8_t v);
uint8_t cz80_rl (uint8_t v);
uint8_t cz80_rr (uint8_t v);
uint8_t cz80_sla(uint8_t v);
uint8_t cz80_sra(uint8_t v);
uint8_t cz80_sll(uint8_t v);   /* undocumented: shift left, bit0 := 1          */
uint8_t cz80_srl(uint8_t v);

/* --- accumulator rotate variants (RLCA/RRCA/RLA/RRA): these clear H and N,
 * leave S/Z/P-V untouched, and set X/Y from the result. Distinct from the CB
 * forms above, which set S/Z/P. --- */
uint8_t cz80_rlca(uint8_t a);
uint8_t cz80_rrca(uint8_t a);
uint8_t cz80_rla (uint8_t a);
uint8_t cz80_rra (uint8_t a);

/* --- misc --- */
uint8_t cz80_daa(uint8_t a);               /* decimal adjust accumulator       */
void    cz80_bit(uint8_t bit, uint8_t v);  /* BIT b,r: tests, sets Z/H/N/S/P    */

/* --- bus glue (implemented in z80.c against the CPS1 sound memory map) ------
 * The interpreter reads/writes guest memory and I/O exclusively through these.
 * On CPS1 the sound Z80 is fully memory-mapped, so the port hooks are stubs. */
uint8_t  cz80_mem_read  (uint16_t addr);
void     cz80_mem_write (uint16_t addr, uint8_t val);
uint16_t cz80_mem_read16(uint16_t addr);
void     cz80_mem_write16(uint16_t addr, uint16_t val);
uint8_t  cz80_port_in (uint16_t port);
void     cz80_port_out(uint16_t port, uint8_t val);

/* Z80 stack helpers (SP lives in guest RAM, little-endian, pre-decrement). */
void     cz80_push16(uint16_t val);
uint16_t cz80_pop16(void);

#endif /* CZ80_CPU_H */
