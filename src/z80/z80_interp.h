/* z80_interp.h - single-step Z80 interpreter for the CPS1 sound CPU.
 *
 * Ported from the sp00nznet z80recomp runtime (pacrecomp/zxrecomp). It decodes
 * the instruction at cz80_cpu.pc via z80_decode over live guest memory (read
 * through cz80_mem_read, so self-modified code is seen exactly as the CPU would)
 * and executes it, routing every ALU/flag effect through the cz80_* helpers in
 * z80_cpu.c, memory through cz80_mem_*, and the stack through cz80_push16/pop16.
 *
 * Plane coverage: full main table, full CB plane (plain + DDCB/FDCB), the common
 * ED set (IN/OUT (C), 16-bit ADC/SBC, LD (nn),rr / LD rr,(nn), NEG, IM, LD I/R,
 * RRD/RLD, RETI/RETN, all block ops + their repeating forms), and the DD/FD
 * index planes.
 */
#ifndef Z80_INTERP_H
#define Z80_INTERP_H

#include <stdint.h>

/* Execute exactly one Z80 instruction located at cz80_cpu.pc.w (== addr),
 * reading opcode bytes via cz80_mem_read, performing its effect on
 * cz80_cpu / memory, and advancing cz80_cpu.pc.w (to the next instruction, or a
 * branch/call/ret target). Returns the instruction length in bytes. */
int z80_interp_step(uint16_t addr);

#endif /* Z80_INTERP_H */
