/* interp.c - single-step Z80 interpreter. See interp.h.
 *
 * Every instruction is decoded with z80_decode (over live cz80_mem) and then
 * executed by mutating cz80_cpu / memory / ports. All arithmetic and flag effects
 * go through the cz80_* ALU helpers in cpu.c so the interpreter and the recompiled
 * emitter compute identical flags. Control flow advances cz80_cpu.pc.w directly:
 * NEXT = addr + len, TGT = insn.target, with conditional forms branching on the
 * decoded cond_code.
 *
 * Plane coverage: full main table, full CB plane (plain + DDCB/FDCB), the common
 * ED set (IN/OUT (C), 16-bit ADC/SBC, LD (nn),rr / LD rr,(nn), NEG, IM, LD I/R,
 * RRD/RLD, RETI/RETN, and every block op LDI/LDD/CPI/CPD/INI/IND/OUTI/OUTD and
 * their repeating LDIR/LDDR/CPIR/CPDR/INIR/INDR/OTIR/OTDR), and the DD/FD index
 * planes (HL->IX/IY, (HL)->(IX+d)/(IY+d), H/L->IXH/IXL).
 */
#include "z80_interp.h"
#include "z80_decode.h"
#include "z80_cpu.h"

/* ------------------------------------------------------------------------- */
/* Index-plane context: which 16-bit reg stands in for HL, whether a DD/FD
 * prefix is active, whether this op references the indexed memory operand, and
 * the resolved (HL)/(IX+d) address. */
typedef struct {
    cz80_reg16_t *hl;      /* HL, IX, or IY (pair + IXH/IXL substitution)     */
    int         sub;     /* 1 if a DD/FD prefix is active                   */
    int         is_idx;  /* 1 if the memory operand is (IX+d)/(IY+d)/(HL)   */
    int8_t      disp;    /* index displacement                              */
    uint16_t    maddr;   /* effective (HL)/(IX+d) address                   */
} ctx_t;

/* --- small helpers ------------------------------------------------------- */

static int parity8(uint8_t v) {
    v ^= (uint8_t)(v >> 4);
    v ^= (uint8_t)(v >> 2);
    v ^= (uint8_t)(v >> 1);
    return (~v) & 1;
}

/* Plain (no-substitution) 8-bit register file: B,C,D,E,H,L,(HL),A. */
static uint8_t rd8(int r) {
    switch (r) {
        case 0: return cz80_cpu.bc.b.hi;
        case 1: return cz80_cpu.bc.b.lo;
        case 2: return cz80_cpu.de.b.hi;
        case 3: return cz80_cpu.de.b.lo;
        case 4: return cz80_cpu.hl.b.hi;
        case 5: return cz80_cpu.hl.b.lo;
        case 6: return cz80_mem_read(cz80_cpu.hl.w);
        default: return cz80_a;
    }
}
static void wr8(int r, uint8_t v) {
    switch (r) {
        case 0: cz80_cpu.bc.b.hi = v; break;
        case 1: cz80_cpu.bc.b.lo = v; break;
        case 2: cz80_cpu.de.b.hi = v; break;
        case 3: cz80_cpu.de.b.lo = v; break;
        case 4: cz80_cpu.hl.b.hi = v; break;
        case 5: cz80_cpu.hl.b.lo = v; break;
        case 6: cz80_mem_write(cz80_cpu.hl.w, v); break;
        default: cz80_a = v; break;
    }
}

/* Index-aware 8-bit register file. H/L map to IXH/IXL only for pure register
 * ops under DD/FD (sub && !is_idx); when the op references (IX+d) the register
 * operand H/L stays the real H/L. (HL)/(IX+d) reads/writes ctx->maddr. */
static uint8_t rd_r(const ctx_t *c, int r) {
    switch (r) {
        case 0: return cz80_cpu.bc.b.hi;
        case 1: return cz80_cpu.bc.b.lo;
        case 2: return cz80_cpu.de.b.hi;
        case 3: return cz80_cpu.de.b.lo;
        case 4: return (c->sub && !c->is_idx) ? c->hl->b.hi : cz80_cpu.hl.b.hi;
        case 5: return (c->sub && !c->is_idx) ? c->hl->b.lo : cz80_cpu.hl.b.lo;
        case 6: return cz80_mem_read(c->maddr);
        default: return cz80_a;
    }
}
static void wr_r(const ctx_t *c, int r, uint8_t v) {
    switch (r) {
        case 0: cz80_cpu.bc.b.hi = v; break;
        case 1: cz80_cpu.bc.b.lo = v; break;
        case 2: cz80_cpu.de.b.hi = v; break;
        case 3: cz80_cpu.de.b.lo = v; break;
        case 4: if (c->sub && !c->is_idx) c->hl->b.hi = v; else cz80_cpu.hl.b.hi = v; break;
        case 5: if (c->sub && !c->is_idx) c->hl->b.lo = v; else cz80_cpu.hl.b.lo = v; break;
        case 6: cz80_mem_write(c->maddr, v); break;
        default: cz80_a = v; break;
    }
}

/* 16-bit pair selector (2-bit field): BC,DE,HL/idx,SP. */
static cz80_reg16_t *rp_ptr(const ctx_t *c, int p) {
    switch (p) {
        case 0: return &cz80_cpu.bc;
        case 1: return &cz80_cpu.de;
        case 2: return c->hl;
        default: return &cz80_cpu.sp;
    }
}
/* PUSH/POP pair selector: index 3 is AF rather than SP. */
static cz80_reg16_t *rp2_ptr(const ctx_t *c, int p) {
    switch (p) {
        case 0: return &cz80_cpu.bc;
        case 1: return &cz80_cpu.de;
        case 2: return c->hl;
        default: return &cz80_cpu.af;
    }
}

/* Plain 16-bit pair access for the ED plane (HL is always real HL there). */
static uint16_t rpw(int p) {
    switch (p) {
        case 0: return cz80_cpu.bc.w;
        case 1: return cz80_cpu.de.w;
        case 2: return cz80_cpu.hl.w;
        default: return cz80_cpu.sp.w;
    }
}
static void rpw_set(int p, uint16_t v) {
    switch (p) {
        case 0: cz80_cpu.bc.w = v; break;
        case 1: cz80_cpu.de.w = v; break;
        case 2: cz80_cpu.hl.w = v; break;
        default: cz80_cpu.sp.w = v; break;
    }
}

/* The eight ALU ops routed through cpu.c helpers (A operand implicit). */
static void alu(int kind, uint8_t v) {
    switch (kind) {
        case 0: cz80_a = cz80_add8(cz80_a, v); break;
        case 1: cz80_a = cz80_adc8(cz80_a, v); break;
        case 2: cz80_a = cz80_sub8(cz80_a, v); break;
        case 3: cz80_a = cz80_sbc8(cz80_a, v); break;
        case 4: cz80_a = cz80_and8(cz80_a, v); break;
        case 5: cz80_a = cz80_xor8(cz80_a, v); break;
        case 6: cz80_a = cz80_or8(cz80_a, v);  break;
        default: cz80_cp8(cz80_a, v); break;
    }
}

/* The eight CB rotate/shift ops. */
static uint8_t cb_rot(int y, uint8_t v) {
    switch (y) {
        case 0: return cz80_rlc(v);
        case 1: return cz80_rrc(v);
        case 2: return cz80_rl(v);
        case 3: return cz80_rr(v);
        case 4: return cz80_sla(v);
        case 5: return cz80_sra(v);
        case 6: return cz80_sll(v);
        default: return cz80_srl(v);
    }
}

/* IN r,(C) / RRD / RLD style flags: S,Z,P from result, H=N=0, C preserved. */
static void in_flags(uint8_t v) {
    uint8_t f = cz80_f & CZ80_FLAG_C;
    f |= v & CZ80_FLAG_S;
    if (v == 0)        f |= CZ80_FLAG_Z;
    if (parity8(v))    f |= CZ80_FLAG_PV;
    f |= v & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
}

/* LD A,I / LD A,R: S,Z,X,Y from value, P/V := IFF2, H=N=0, C preserved. */
static void ld_a_ir(uint8_t v) {
    uint8_t f = cz80_f & CZ80_FLAG_C;
    cz80_a = v;
    f |= v & CZ80_FLAG_S;
    if (v == 0)        f |= CZ80_FLAG_Z;
    if (cz80_cpu.iff2)   f |= CZ80_FLAG_PV;
    f |= v & (CZ80_FLAG_X | CZ80_FLAG_Y);
    cz80_f = f;
}

/* --- block-op single steps (the repeating forms loop these in one step) --- */

static void ldi_step(int dir) {
    uint8_t v = cz80_mem_read(cz80_cpu.hl.w);
    cz80_mem_write(cz80_cpu.de.w, v);
    cz80_cpu.hl.w = (uint16_t)(cz80_cpu.hl.w + dir);
    cz80_cpu.de.w = (uint16_t)(cz80_cpu.de.w + dir);
    cz80_cpu.bc.w--;
    {
        uint8_t n = (uint8_t)(cz80_a + v);
        uint8_t f = cz80_f & (CZ80_FLAG_S | CZ80_FLAG_Z | CZ80_FLAG_C);  /* H=N=0 */
        if (cz80_cpu.bc.w) f |= CZ80_FLAG_PV;
        if (n & 0x08)    f |= CZ80_FLAG_X;
        if (n & 0x02)    f |= CZ80_FLAG_Y;
        cz80_f = f;
    }
}

static void cpi_step(int dir) {
    uint8_t v   = cz80_mem_read(cz80_cpu.hl.w);
    uint8_t res = (uint8_t)(cz80_a - v);
    int hc = (((cz80_a & 0x0F) - (v & 0x0F)) & 0x10) ? 1 : 0;
    cz80_cpu.hl.w = (uint16_t)(cz80_cpu.hl.w + dir);
    cz80_cpu.bc.w--;
    {
        uint8_t n = (uint8_t)(res - (hc ? 1 : 0));
        uint8_t f = (cz80_f & CZ80_FLAG_C) | CZ80_FLAG_N;
        f |= res & CZ80_FLAG_S;
        if (res == 0)    f |= CZ80_FLAG_Z;
        if (hc)          f |= CZ80_FLAG_H;
        if (cz80_cpu.bc.w) f |= CZ80_FLAG_PV;
        if (n & 0x08)    f |= CZ80_FLAG_X;
        if (n & 0x02)    f |= CZ80_FLAG_Y;
        cz80_f = f;
    }
}

static void in_step(int dir) {
    uint8_t v = cz80_port_in(cz80_cpu.bc.b.lo);
    cz80_mem_write(cz80_cpu.hl.w, v);
    cz80_cpu.bc.b.hi--;                                   /* B-- */
    cz80_cpu.hl.w = (uint16_t)(cz80_cpu.hl.w + dir);
    {
        uint8_t  B = cz80_cpu.bc.b.hi;
        unsigned k = (unsigned)v + ((cz80_cpu.bc.b.lo + dir) & 0xFF);
        uint8_t  f = 0;
        f |= B & CZ80_FLAG_S;
        if (B == 0)               f |= CZ80_FLAG_Z;
        if (v & 0x80)             f |= CZ80_FLAG_N;
        if (k > 0xFF)             f |= CZ80_FLAG_H | CZ80_FLAG_C;
        if (parity8((uint8_t)((k & 7) ^ B))) f |= CZ80_FLAG_PV;
        f |= B & (CZ80_FLAG_X | CZ80_FLAG_Y);
        cz80_f = f;
    }
}

static void out_step(int dir) {
    uint8_t v = cz80_mem_read(cz80_cpu.hl.w);
    cz80_port_out(cz80_cpu.bc.b.lo, v);
    cz80_cpu.bc.b.hi--;                                   /* B-- */
    cz80_cpu.hl.w = (uint16_t)(cz80_cpu.hl.w + dir);
    {
        uint8_t  B = cz80_cpu.bc.b.hi;
        unsigned k = (unsigned)v + cz80_cpu.hl.b.lo;       /* L after update */
        uint8_t  f = 0;
        f |= B & CZ80_FLAG_S;
        if (B == 0)               f |= CZ80_FLAG_Z;
        if (v & 0x80)             f |= CZ80_FLAG_N;
        if (k > 0xFF)             f |= CZ80_FLAG_H | CZ80_FLAG_C;
        if (parity8((uint8_t)((k & 7) ^ B))) f |= CZ80_FLAG_PV;
        f |= B & (CZ80_FLAG_X | CZ80_FLAG_Y);
        cz80_f = f;
    }
}

static void do_rrd(void) {
    uint8_t m = cz80_mem_read(cz80_cpu.hl.w);
    uint8_t newm = (uint8_t)((m >> 4) | (cz80_a << 4));
    cz80_a = (uint8_t)((cz80_a & 0xF0) | (m & 0x0F));
    cz80_mem_write(cz80_cpu.hl.w, newm);
    in_flags(cz80_a);
}
static void do_rld(void) {
    uint8_t m = cz80_mem_read(cz80_cpu.hl.w);
    uint8_t newm = (uint8_t)((m << 4) | (cz80_a & 0x0F));
    cz80_a = (uint8_t)((cz80_a & 0xF0) | (m >> 4));
    cz80_mem_write(cz80_cpu.hl.w, newm);
    in_flags(cz80_a);
}

/* ------------------------------------------------------------------------- */
/* CB plane: plain CB plus DDCB/FDCB (which always target (IX+d)/(IY+d)).     */
static void exec_cb(const z80_insn_t *in, const ctx_t *c) {
    unsigned op = in->opcode, x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    int      ddcb  = (in->prefix == ZP_DDCB || in->prefix == ZP_FDCB);
    int      usemem = ddcb || (z == 6);
    uint16_t maddr = ddcb ? c->maddr : cz80_cpu.hl.w;
    uint8_t  v = usemem ? cz80_mem_read(maddr) : rd8(z);
    uint8_t  r;

    if (x == 1) { cz80_bit(y, v); return; }               /* BIT: no writeback */

    if (x == 0)      r = cb_rot(y, v);
    else if (x == 2) r = (uint8_t)(v & ~(1u << y));      /* RES */
    else             r = (uint8_t)(v | (1u << y));       /* SET */

    if (usemem) cz80_mem_write(maddr, r);
    else        wr8(z, r);
}

/* ------------------------------------------------------------------------- */
/* ED plane (data effects only; RETI/RETN pc handled by the control block).  */
static void exec_ed(const z80_insn_t *in) {
    unsigned op = in->opcode;

    if ((op & 0xC7) == 0x40) {                          /* IN r,(C) */
        int r = (op >> 3) & 7;
        uint8_t v = cz80_port_in(cz80_cpu.bc.b.lo);
        in_flags(v);
        if (r != 6) wr8(r, v);                          /* 0x70 = IN (C): flags only */
        return;
    }
    if ((op & 0xC7) == 0x41) {                          /* OUT (C),r */
        int r = (op >> 3) & 7;
        uint8_t v = (r == 6) ? 0 : rd8(r);              /* 0x71 = OUT (C),0 */
        cz80_port_out(cz80_cpu.bc.b.lo, v);
        return;
    }
    if ((op & 0xCF) == 0x42) {                          /* SBC HL,rr */
        cz80_cpu.hl.w = cz80_sbc16(cz80_cpu.hl.w, rpw((op >> 4) & 3));
        return;
    }
    if ((op & 0xCF) == 0x4A) {                          /* ADC HL,rr */
        cz80_cpu.hl.w = cz80_adc16(cz80_cpu.hl.w, rpw((op >> 4) & 3));
        return;
    }
    if ((op & 0xCF) == 0x43) {                          /* LD (nn),rr */
        cz80_mem_write16(in->imm, rpw((op >> 4) & 3));
        return;
    }
    if ((op & 0xCF) == 0x4B) {                          /* LD rr,(nn) */
        rpw_set((op >> 4) & 3, cz80_mem_read16(in->imm));
        return;
    }

    switch (op) {
        case 0x44: case 0x4C: case 0x54: case 0x5C:
        case 0x64: case 0x6C: case 0x74: case 0x7C:
            cz80_a = cz80_sub8(0, cz80_a); break;             /* NEG */

        case 0x46: case 0x4E: case 0x66: case 0x6E: cz80_cpu.im = 0; break;
        case 0x56: case 0x76:                       cz80_cpu.im = 1; break;
        case 0x5E: case 0x7E:                       cz80_cpu.im = 2; break;

        case 0x47: cz80_cpu.i = cz80_a; break;              /* LD I,A */
        case 0x4F: cz80_cpu.r = cz80_a; break;              /* LD R,A */
        case 0x57: ld_a_ir(cz80_cpu.i); break;            /* LD A,I */
        case 0x5F: ld_a_ir(cz80_cpu.r); break;            /* LD A,R */

        case 0x67: do_rrd(); break;
        case 0x6F: do_rld(); break;

        case 0xA0: ldi_step(+1); break;                 /* LDI */
        case 0xA8: ldi_step(-1); break;                 /* LDD */
        case 0xB0: do { ldi_step(+1); } while (cz80_cpu.bc.w); break;  /* LDIR */
        case 0xB8: do { ldi_step(-1); } while (cz80_cpu.bc.w); break;  /* LDDR */

        case 0xA1: cpi_step(+1); break;                 /* CPI */
        case 0xA9: cpi_step(-1); break;                 /* CPD */
        case 0xB1: do { cpi_step(+1); } while (cz80_cpu.bc.w && !(cz80_f & CZ80_FLAG_Z)); break; /* CPIR */
        case 0xB9: do { cpi_step(-1); } while (cz80_cpu.bc.w && !(cz80_f & CZ80_FLAG_Z)); break; /* CPDR */

        case 0xA2: in_step(+1); break;                  /* INI */
        case 0xAA: in_step(-1); break;                  /* IND */
        case 0xB2: do { in_step(+1); } while (cz80_cpu.bc.b.hi); break;  /* INIR */
        case 0xBA: do { in_step(-1); } while (cz80_cpu.bc.b.hi); break;  /* INDR */

        case 0xA3: out_step(+1); break;                 /* OUTI */
        case 0xAB: out_step(-1); break;                 /* OUTD */
        case 0xB3: do { out_step(+1); } while (cz80_cpu.bc.b.hi); break; /* OTIR */
        case 0xBB: do { out_step(-1); } while (cz80_cpu.bc.b.hi); break; /* OTDR */

        /* RETI/RETN (0x45/4D/55/5D/65/6D/75/7D): pc + IFF handled by CF block.
         * Any other / undefined ED opcode acts as a 2-byte NOP. */
        default: break;
    }
}

/* ------------------------------------------------------------------------- */
/* Main table (shared by ZP_NONE / ZP_DD / ZP_FD). Control-flow opcodes have no
 * data effect here - the control block resolves their pc. */
static void exec_main(const z80_insn_t *in, ctx_t *c) {
    unsigned op = in->opcode;

    if (op >= 0x40 && op <= 0x7F) {                     /* LD r,r' (0x76 = HALT) */
        if (op == 0x76) return;
        wr_r(c, (int)((op >> 3) & 7), rd_r(c, (int)(op & 7)));
        return;
    }
    if (op >= 0x80 && op <= 0xBF) {                     /* ALU A,r */
        alu((int)((op >> 3) & 7), rd_r(c, (int)(op & 7)));
        return;
    }

    switch (op) {
        case 0x00: break;                               /* NOP */

        case 0x01: case 0x11: case 0x21: case 0x31:     /* LD rr,nn */
            rp_ptr(c, (int)((op >> 4) & 3))->w = in->imm; break;

        case 0x02: cz80_mem_write(cz80_cpu.bc.w, cz80_a); break;       /* LD (BC),A */
        case 0x12: cz80_mem_write(cz80_cpu.de.w, cz80_a); break;       /* LD (DE),A */
        case 0x22: cz80_mem_write16(in->imm, c->hl->w); break;     /* LD (nn),HL */
        case 0x32: cz80_mem_write(in->imm, cz80_a); break;           /* LD (nn),A */
        case 0x0A: cz80_a = cz80_mem_read(cz80_cpu.bc.w); break;       /* LD A,(BC) */
        case 0x1A: cz80_a = cz80_mem_read(cz80_cpu.de.w); break;       /* LD A,(DE) */
        case 0x2A: c->hl->w = cz80_mem_read16(in->imm); break;     /* LD HL,(nn) */
        case 0x3A: cz80_a = cz80_mem_read(in->imm); break;           /* LD A,(nn) */

        case 0x03: case 0x13: case 0x23: case 0x33:     /* INC rr */
            rp_ptr(c, (int)((op >> 4) & 3))->w++; break;
        case 0x0B: case 0x1B: case 0x2B: case 0x3B:     /* DEC rr */
            rp_ptr(c, (int)((op >> 4) & 3))->w--; break;

        case 0x04: case 0x0C: case 0x14: case 0x1C:     /* INC r */
        case 0x24: case 0x2C: case 0x34: case 0x3C: {
            int r = (int)((op >> 3) & 7);
            wr_r(c, r, cz80_inc8(rd_r(c, r))); break;
        }
        case 0x05: case 0x0D: case 0x15: case 0x1D:     /* DEC r */
        case 0x25: case 0x2D: case 0x35: case 0x3D: {
            int r = (int)((op >> 3) & 7);
            wr_r(c, r, cz80_dec8(rd_r(c, r))); break;
        }
        case 0x06: case 0x0E: case 0x16: case 0x1E:     /* LD r,n */
        case 0x26: case 0x2E: case 0x36: case 0x3E:
            wr_r(c, (int)((op >> 3) & 7), (uint8_t)in->imm); break;

        case 0x07: cz80_a = cz80_rlca(cz80_a); break;
        case 0x0F: cz80_a = cz80_rrca(cz80_a); break;
        case 0x17: cz80_a = cz80_rla(cz80_a); break;
        case 0x1F: cz80_a = cz80_rra(cz80_a); break;

        case 0x08: {                                    /* EX AF,AF' */
            cz80_reg16_t t = cz80_cpu.af; cz80_cpu.af = cz80_cpu.af_; cz80_cpu.af_ = t; break;
        }
        case 0x09: case 0x19: case 0x29: case 0x39:     /* ADD HL,rr */
            c->hl->w = cz80_add16(c->hl->w, rp_ptr(c, (int)((op >> 4) & 3))->w); break;

        case 0x27: cz80_a = cz80_daa(cz80_a); break;          /* DAA */
        case 0x2F: cz80_a = (uint8_t)~cz80_a; cz80_f |= CZ80_FLAG_H | CZ80_FLAG_N; break;  /* CPL */

        case 0x37:                                      /* SCF */
            cz80_f = (uint8_t)((cz80_f & (CZ80_FLAG_S | CZ80_FLAG_Z | CZ80_FLAG_PV))
                             | CZ80_FLAG_C | (cz80_a & (CZ80_FLAG_X | CZ80_FLAG_Y)));
            break;
        case 0x3F: {                                    /* CCF */
            int oc = cz80_f & CZ80_FLAG_C;
            uint8_t f = cz80_f & (CZ80_FLAG_S | CZ80_FLAG_Z | CZ80_FLAG_PV);
            f |= oc ? CZ80_FLAG_H : CZ80_FLAG_C;
            f |= cz80_a & (CZ80_FLAG_X | CZ80_FLAG_Y);
            cz80_f = f; break;
        }

        case 0xC1: case 0xD1: case 0xE1: case 0xF1:     /* POP rr */
            rp2_ptr(c, (int)((op >> 4) & 3))->w = cz80_pop16(); break;
        case 0xC5: case 0xD5: case 0xE5: case 0xF5:     /* PUSH rr */
            cz80_push16(rp2_ptr(c, (int)((op >> 4) & 3))->w); break;

        case 0xC6: alu(0, (uint8_t)in->imm); break;     /* ADD A,n */
        case 0xCE: alu(1, (uint8_t)in->imm); break;     /* ADC A,n */
        case 0xD6: alu(2, (uint8_t)in->imm); break;     /* SUB n   */
        case 0xDE: alu(3, (uint8_t)in->imm); break;     /* SBC A,n */
        case 0xE6: alu(4, (uint8_t)in->imm); break;     /* AND n   */
        case 0xEE: alu(5, (uint8_t)in->imm); break;     /* XOR n   */
        case 0xF6: alu(6, (uint8_t)in->imm); break;     /* OR n    */
        case 0xFE: alu(7, (uint8_t)in->imm); break;     /* CP n    */

        case 0xD3: cz80_port_out((uint8_t)in->imm, cz80_a); break;   /* OUT (n),A */
        case 0xDB: cz80_a = cz80_port_in((uint8_t)in->imm); break;   /* IN A,(n)  */

        case 0xE3: {                                    /* EX (SP),HL/IX/IY */
            uint8_t lo = cz80_mem_read(cz80_cpu.sp.w);
            uint8_t hi = cz80_mem_read((uint16_t)(cz80_cpu.sp.w + 1));
            cz80_mem_write(cz80_cpu.sp.w, (uint8_t)c->hl->w);
            cz80_mem_write((uint16_t)(cz80_cpu.sp.w + 1), (uint8_t)(c->hl->w >> 8));
            c->hl->w = (uint16_t)(lo | (hi << 8)); break;
        }
        case 0xEB: {                                    /* EX DE,HL (DD/FD: still HL) */
            cz80_reg16_t t = cz80_cpu.de; cz80_cpu.de = cz80_cpu.hl; cz80_cpu.hl = t; break;
        }
        case 0xF9: cz80_cpu.sp.w = c->hl->w; break;       /* LD SP,HL */
        case 0xF3: cz80_cpu.iff1 = cz80_cpu.iff2 = 0; break;/* DI */
        case 0xFB: cz80_cpu.iff1 = cz80_cpu.iff2 = 1; break;/* EI */

        case 0xD9: {                                    /* EXX */
            cz80_reg16_t t;
            t = cz80_cpu.bc; cz80_cpu.bc = cz80_cpu.bc_; cz80_cpu.bc_ = t;
            t = cz80_cpu.de; cz80_cpu.de = cz80_cpu.de_; cz80_cpu.de_ = t;
            t = cz80_cpu.hl; cz80_cpu.hl = cz80_cpu.hl_; cz80_cpu.hl_ = t; break;
        }

        /* All control-flow opcodes (JR/JP/CALL/RET/RST/DJNZ/JP (HL)/HALT) and
         * the prefix-NOP fall here: pc is resolved by the control block. */
        default: break;
    }
}

/* ------------------------------------------------------------------------- */

static int cond_met(uint8_t cc) {
    switch (cc) {
        case 0: return !(cz80_f & CZ80_FLAG_Z);     /* NZ */
        case 1: return  (cz80_f & CZ80_FLAG_Z);     /* Z  */
        case 2: return !(cz80_f & CZ80_FLAG_C);     /* NC */
        case 3: return  (cz80_f & CZ80_FLAG_C);     /* C  */
        case 4: return !(cz80_f & CZ80_FLAG_PV);    /* PO */
        case 5: return  (cz80_f & CZ80_FLAG_PV);    /* PE */
        case 6: return !(cz80_f & CZ80_FLAG_S);     /* P  */
        default:return  (cz80_f & CZ80_FLAG_S);     /* M  */
    }
}

int z80_interp_step(uint16_t addr) {
    uint8_t buf[4];
    z80_insn_t in;
    ctx_t c;
    int len;
    uint16_t next, tgt;

    buf[0] = cz80_mem_read(addr);
    buf[1] = cz80_mem_read((uint16_t)(addr + 1));
    buf[2] = cz80_mem_read((uint16_t)(addr + 2));
    buf[3] = cz80_mem_read((uint16_t)(addr + 3));

    len = z80_decode(buf, 4, addr, &in);
    if (len <= 0) { cz80_cpu.pc.w = (uint16_t)(addr + 1); return 1; }

    /* Build the index-plane context. */
    c.sub = (in.prefix == ZP_DD || in.prefix == ZP_FD ||
             in.prefix == ZP_DDCB || in.prefix == ZP_FDCB);
    c.hl = (in.prefix == ZP_DD || in.prefix == ZP_DDCB) ? &cz80_cpu.ix
         : (in.prefix == ZP_FD || in.prefix == ZP_FDCB) ? &cz80_cpu.iy
         : &cz80_cpu.hl;
    c.is_idx = (in.operand_kind == Z_IDX);
    c.disp   = in.disp;
    c.maddr  = (c.sub && c.is_idx) ? (uint16_t)(c.hl->w + in.disp) : cz80_cpu.hl.w;

    /* Data effect. */
    switch (in.prefix) {
        case ZP_CB: case ZP_DDCB: case ZP_FDCB: exec_cb(&in, &c); break;
        case ZP_ED:                              exec_ed(&in);     break;
        default:                                 exec_main(&in, &c); break;
    }

    /* Control flow / pc advance. */
    next = (uint16_t)(addr + in.len);
    tgt  = in.target;
    switch (in.cflow) {
        case CF_NORMAL: cz80_cpu.pc.w = next; break;
        case CF_JMP:    cz80_cpu.pc.w = tgt;  break;
        case CF_CALL:   cz80_push16(next); cz80_cpu.pc.w = tgt; break;
        case CF_RST:    cz80_push16(next); cz80_cpu.pc.w = tgt; break;
        case CF_STOP:   cz80_cpu.halted = 1; cz80_cpu.pc.w = next; break;
        case CF_JPHL:   cz80_cpu.pc.w = c.hl->w; break;
        case CF_RET:
            if (in.prefix == ZP_ED) cz80_cpu.iff1 = cz80_cpu.iff2;   /* RETN/RETI */
            cz80_cpu.pc.w = cz80_pop16();
            break;
        case CF_BRANCH: {
            unsigned op = in.opcode;
            if (op == 0x10) {                               /* DJNZ */
                cz80_cpu.bc.b.hi--;
                cz80_cpu.pc.w = cz80_cpu.bc.b.hi ? tgt : next;
            } else {
                int taken = cond_met(in.cond_code);
                unsigned low = op & 7;
                if (op >= 0xC0 && low == 4) {               /* CALL cc */
                    if (taken) { cz80_push16(next); cz80_cpu.pc.w = tgt; }
                    else        cz80_cpu.pc.w = next;
                } else if (op >= 0xC0 && low == 0) {        /* RET cc */
                    cz80_cpu.pc.w = taken ? cz80_pop16() : next;
                } else {                                     /* JR cc / JP cc */
                    cz80_cpu.pc.w = taken ? tgt : next;
                }
            }
            break;
        }
        default: cz80_cpu.pc.w = next; break;
    }

    return len;
}
