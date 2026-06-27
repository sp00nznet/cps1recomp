/* decode.c - Zilog Z80 opcode tables + decoder. See decode.h.
 *
 * Three tables drive the decode: the 256-entry main set (z80_optab), the
 * sparse ED extended set (ed_optab), and the algorithmically-generated CB
 * plane. DD/FD reuse the main set with HL->IX/IY and (HL)->(IX+d)/(IY+d),
 * which only ever adds the single displacement byte. Lengths and control-flow
 * classes are the load-bearing fields here - mnemonics are cosmetic. */
#include "z80_decode.h"
#include <stdio.h>
#include <string.h>

/* --- control-flow shorthands -------------------------------------------- */
#define N  CF_NORMAL
#define BR CF_BRANCH
#define JM CF_JMP
#define CA CF_CALL
#define RT CF_RET
#define RS CF_RST
#define ST CF_STOP
#define JH CF_JPHL
/* --- operand-kind shorthands -------------------------------------------- */
#define _NO  Z_NONE
#define _I8  Z_IMM8
#define _I16 Z_IMM16
#define _R8  Z_REL8
#define _A16 Z_ABS16
#define _M16 Z_MEM16
#define _RS  Z_RST
#define _PT  Z_PORT

/* The complete 256-entry main matrix. "%s" in a mnemonic marks where the
 * operand text goes; the trailing flag marks (HL)-referencing ops that become
 * indexed under DD/FD. Prefix bytes (CB/DD/ED/FD) carry placeholder entries -
 * z80_decode intercepts them before they are ever read here. */
const z80_opinfo_t z80_optab[256] = {
/* 0x00 */ {"NOP",_NO,N,0},        {"LD BC,%s",_I16,N,0},  {"LD (BC),A",_NO,N,0},  {"INC BC",_NO,N,0},
/* 0x04 */ {"INC B",_NO,N,0},      {"DEC B",_NO,N,0},      {"LD B,%s",_I8,N,0},    {"RLCA",_NO,N,0},
/* 0x08 */ {"EX AF,AF'",_NO,N,0},  {"ADD HL,BC",_NO,N,0},  {"LD A,(BC)",_NO,N,0},  {"DEC BC",_NO,N,0},
/* 0x0C */ {"INC C",_NO,N,0},      {"DEC C",_NO,N,0},      {"LD C,%s",_I8,N,0},    {"RRCA",_NO,N,0},
/* 0x10 */ {"DJNZ %s",_R8,BR,0},   {"LD DE,%s",_I16,N,0},  {"LD (DE),A",_NO,N,0},  {"INC DE",_NO,N,0},
/* 0x14 */ {"INC D",_NO,N,0},      {"DEC D",_NO,N,0},      {"LD D,%s",_I8,N,0},    {"RLA",_NO,N,0},
/* 0x18 */ {"JR %s",_R8,JM,0},     {"ADD HL,DE",_NO,N,0},  {"LD A,(DE)",_NO,N,0},  {"DEC DE",_NO,N,0},
/* 0x1C */ {"INC E",_NO,N,0},      {"DEC E",_NO,N,0},      {"LD E,%s",_I8,N,0},    {"RRA",_NO,N,0},
/* 0x20 */ {"JR NZ,%s",_R8,BR,0},  {"LD HL,%s",_I16,N,0},  {"LD (%s),HL",_M16,N,0},{"INC HL",_NO,N,0},
/* 0x24 */ {"INC H",_NO,N,0},      {"DEC H",_NO,N,0},      {"LD H,%s",_I8,N,0},    {"DAA",_NO,N,0},
/* 0x28 */ {"JR Z,%s",_R8,BR,0},   {"ADD HL,HL",_NO,N,0},  {"LD HL,(%s)",_M16,N,0},{"DEC HL",_NO,N,0},
/* 0x2C */ {"INC L",_NO,N,0},      {"DEC L",_NO,N,0},      {"LD L,%s",_I8,N,0},    {"CPL",_NO,N,0},
/* 0x30 */ {"JR NC,%s",_R8,BR,0},  {"LD SP,%s",_I16,N,0},  {"LD (%s),A",_M16,N,0}, {"INC SP",_NO,N,0},
/* 0x34 */ {"INC (HL)",_NO,N,1},   {"DEC (HL)",_NO,N,1},   {"LD (HL),%s",_I8,N,1}, {"SCF",_NO,N,0},
/* 0x38 */ {"JR C,%s",_R8,BR,0},   {"ADD HL,SP",_NO,N,0},  {"LD A,(%s)",_M16,N,0}, {"DEC SP",_NO,N,0},
/* 0x3C */ {"INC A",_NO,N,0},      {"DEC A",_NO,N,0},      {"LD A,%s",_I8,N,0},    {"CCF",_NO,N,0},
/* 0x40 */ {"LD B,B",_NO,N,0},     {"LD B,C",_NO,N,0},     {"LD B,D",_NO,N,0},     {"LD B,E",_NO,N,0},
/* 0x44 */ {"LD B,H",_NO,N,0},     {"LD B,L",_NO,N,0},     {"LD B,(HL)",_NO,N,1},  {"LD B,A",_NO,N,0},
/* 0x48 */ {"LD C,B",_NO,N,0},     {"LD C,C",_NO,N,0},     {"LD C,D",_NO,N,0},     {"LD C,E",_NO,N,0},
/* 0x4C */ {"LD C,H",_NO,N,0},     {"LD C,L",_NO,N,0},     {"LD C,(HL)",_NO,N,1},  {"LD C,A",_NO,N,0},
/* 0x50 */ {"LD D,B",_NO,N,0},     {"LD D,C",_NO,N,0},     {"LD D,D",_NO,N,0},     {"LD D,E",_NO,N,0},
/* 0x54 */ {"LD D,H",_NO,N,0},     {"LD D,L",_NO,N,0},     {"LD D,(HL)",_NO,N,1},  {"LD D,A",_NO,N,0},
/* 0x58 */ {"LD E,B",_NO,N,0},     {"LD E,C",_NO,N,0},     {"LD E,D",_NO,N,0},     {"LD E,E",_NO,N,0},
/* 0x5C */ {"LD E,H",_NO,N,0},     {"LD E,L",_NO,N,0},     {"LD E,(HL)",_NO,N,1},  {"LD E,A",_NO,N,0},
/* 0x60 */ {"LD H,B",_NO,N,0},     {"LD H,C",_NO,N,0},     {"LD H,D",_NO,N,0},     {"LD H,E",_NO,N,0},
/* 0x64 */ {"LD H,H",_NO,N,0},     {"LD H,L",_NO,N,0},     {"LD H,(HL)",_NO,N,1},  {"LD H,A",_NO,N,0},
/* 0x68 */ {"LD L,B",_NO,N,0},     {"LD L,C",_NO,N,0},     {"LD L,D",_NO,N,0},     {"LD L,E",_NO,N,0},
/* 0x6C */ {"LD L,H",_NO,N,0},     {"LD L,L",_NO,N,0},     {"LD L,(HL)",_NO,N,1},  {"LD L,A",_NO,N,0},
/* 0x70 */ {"LD (HL),B",_NO,N,1},  {"LD (HL),C",_NO,N,1},  {"LD (HL),D",_NO,N,1},  {"LD (HL),E",_NO,N,1},
/* 0x74 */ {"LD (HL),H",_NO,N,1},  {"LD (HL),L",_NO,N,1},  {"HALT",_NO,ST,0},      {"LD (HL),A",_NO,N,1},
/* 0x78 */ {"LD A,B",_NO,N,0},     {"LD A,C",_NO,N,0},     {"LD A,D",_NO,N,0},     {"LD A,E",_NO,N,0},
/* 0x7C */ {"LD A,H",_NO,N,0},     {"LD A,L",_NO,N,0},     {"LD A,(HL)",_NO,N,1},  {"LD A,A",_NO,N,0},
/* 0x80 */ {"ADD A,B",_NO,N,0},    {"ADD A,C",_NO,N,0},    {"ADD A,D",_NO,N,0},    {"ADD A,E",_NO,N,0},
/* 0x84 */ {"ADD A,H",_NO,N,0},    {"ADD A,L",_NO,N,0},    {"ADD A,(HL)",_NO,N,1}, {"ADD A,A",_NO,N,0},
/* 0x88 */ {"ADC A,B",_NO,N,0},    {"ADC A,C",_NO,N,0},    {"ADC A,D",_NO,N,0},    {"ADC A,E",_NO,N,0},
/* 0x8C */ {"ADC A,H",_NO,N,0},    {"ADC A,L",_NO,N,0},    {"ADC A,(HL)",_NO,N,1}, {"ADC A,A",_NO,N,0},
/* 0x90 */ {"SUB B",_NO,N,0},      {"SUB C",_NO,N,0},      {"SUB D",_NO,N,0},      {"SUB E",_NO,N,0},
/* 0x94 */ {"SUB H",_NO,N,0},      {"SUB L",_NO,N,0},      {"SUB (HL)",_NO,N,1},   {"SUB A",_NO,N,0},
/* 0x98 */ {"SBC A,B",_NO,N,0},    {"SBC A,C",_NO,N,0},    {"SBC A,D",_NO,N,0},    {"SBC A,E",_NO,N,0},
/* 0x9C */ {"SBC A,H",_NO,N,0},    {"SBC A,L",_NO,N,0},    {"SBC A,(HL)",_NO,N,1}, {"SBC A,A",_NO,N,0},
/* 0xA0 */ {"AND B",_NO,N,0},      {"AND C",_NO,N,0},      {"AND D",_NO,N,0},      {"AND E",_NO,N,0},
/* 0xA4 */ {"AND H",_NO,N,0},      {"AND L",_NO,N,0},      {"AND (HL)",_NO,N,1},   {"AND A",_NO,N,0},
/* 0xA8 */ {"XOR B",_NO,N,0},      {"XOR C",_NO,N,0},      {"XOR D",_NO,N,0},      {"XOR E",_NO,N,0},
/* 0xAC */ {"XOR H",_NO,N,0},      {"XOR L",_NO,N,0},      {"XOR (HL)",_NO,N,1},   {"XOR A",_NO,N,0},
/* 0xB0 */ {"OR B",_NO,N,0},       {"OR C",_NO,N,0},       {"OR D",_NO,N,0},       {"OR E",_NO,N,0},
/* 0xB4 */ {"OR H",_NO,N,0},       {"OR L",_NO,N,0},       {"OR (HL)",_NO,N,1},    {"OR A",_NO,N,0},
/* 0xB8 */ {"CP B",_NO,N,0},       {"CP C",_NO,N,0},       {"CP D",_NO,N,0},       {"CP E",_NO,N,0},
/* 0xBC */ {"CP H",_NO,N,0},       {"CP L",_NO,N,0},       {"CP (HL)",_NO,N,1},    {"CP A",_NO,N,0},
/* 0xC0 */ {"RET NZ",_NO,BR,0},    {"POP BC",_NO,N,0},     {"JP NZ,%s",_A16,BR,0}, {"JP %s",_A16,JM,0},
/* 0xC4 */ {"CALL NZ,%s",_A16,BR,0},{"PUSH BC",_NO,N,0},   {"ADD A,%s",_I8,N,0},   {"RST %s",_RS,RS,0},
/* 0xC8 */ {"RET Z",_NO,BR,0},     {"RET",_NO,RT,0},       {"JP Z,%s",_A16,BR,0},  {"(CB prefix)",_NO,N,0},
/* 0xCC */ {"CALL Z,%s",_A16,BR,0},{"CALL %s",_A16,CA,0},  {"ADC A,%s",_I8,N,0},   {"RST %s",_RS,RS,0},
/* 0xD0 */ {"RET NC",_NO,BR,0},    {"POP DE",_NO,N,0},     {"JP NC,%s",_A16,BR,0}, {"OUT (%s),A",_PT,N,0},
/* 0xD4 */ {"CALL NC,%s",_A16,BR,0},{"PUSH DE",_NO,N,0},   {"SUB %s",_I8,N,0},     {"RST %s",_RS,RS,0},
/* 0xD8 */ {"RET C",_NO,BR,0},     {"EXX",_NO,N,0},        {"JP C,%s",_A16,BR,0},  {"IN A,(%s)",_PT,N,0},
/* 0xDC */ {"CALL C,%s",_A16,BR,0},{"(DD prefix)",_NO,N,0},{"SBC A,%s",_I8,N,0},   {"RST %s",_RS,RS,0},
/* 0xE0 */ {"RET PO",_NO,BR,0},    {"POP HL",_NO,N,0},     {"JP PO,%s",_A16,BR,0}, {"EX (SP),HL",_NO,N,0},
/* 0xE4 */ {"CALL PO,%s",_A16,BR,0},{"PUSH HL",_NO,N,0},   {"AND %s",_I8,N,0},     {"RST %s",_RS,RS,0},
/* 0xE8 */ {"RET PE",_NO,BR,0},    {"JP (HL)",_NO,JH,0},   {"JP PE,%s",_A16,BR,0}, {"EX DE,HL",_NO,N,0},
/* 0xEC */ {"CALL PE,%s",_A16,BR,0},{"(ED prefix)",_NO,N,0},{"XOR %s",_I8,N,0},    {"RST %s",_RS,RS,0},
/* 0xF0 */ {"RET P",_NO,BR,0},     {"POP AF",_NO,N,0},     {"JP P,%s",_A16,BR,0},  {"DI",_NO,N,0},
/* 0xF4 */ {"CALL P,%s",_A16,BR,0},{"PUSH AF",_NO,N,0},    {"OR %s",_I8,N,0},      {"RST %s",_RS,RS,0},
/* 0xF8 */ {"RET M",_NO,BR,0},     {"LD SP,HL",_NO,N,0},   {"JP M,%s",_A16,BR,0},  {"EI",_NO,N,0},
/* 0xFC */ {"CALL M,%s",_A16,BR,0},{"(FD prefix)",_NO,N,0},{"CP %s",_I8,N,0},      {"RST %s",_RS,RS,0},
};

/* ED extended set. Sparse: any opcode not listed is an undefined 2-byte
 * NOP-like (decoded with mnemonic "NOP"). The eight LD (nn),rr / LD rr,(nn)
 * forms carry a 16-bit address and are 4 bytes total; everything else is 2.
 * RETI/RETN -> CF_RET. The block ops (LDIR/CPIR/...) re-run internally and
 * fall through when done, so they are CF_NORMAL. */
static const z80_opinfo_t ed_optab[256] = {
    [0x40]={"IN B,(C)",_NO,N,0},   [0x41]={"OUT (C),B",_NO,N,0},
    [0x42]={"SBC HL,BC",_NO,N,0},  [0x43]={"LD (%s),BC",_M16,N,0},
    [0x44]={"NEG",_NO,N,0},        [0x45]={"RETN",_NO,RT,0},
    [0x46]={"IM 0",_NO,N,0},       [0x47]={"LD I,A",_NO,N,0},
    [0x48]={"IN C,(C)",_NO,N,0},   [0x49]={"OUT (C),C",_NO,N,0},
    [0x4A]={"ADC HL,BC",_NO,N,0},  [0x4B]={"LD BC,(%s)",_M16,N,0},
    [0x4C]={"NEG",_NO,N,0},        [0x4D]={"RETI",_NO,RT,0},
    [0x4E]={"IM 0",_NO,N,0},       [0x4F]={"LD R,A",_NO,N,0},
    [0x50]={"IN D,(C)",_NO,N,0},   [0x51]={"OUT (C),D",_NO,N,0},
    [0x52]={"SBC HL,DE",_NO,N,0},  [0x53]={"LD (%s),DE",_M16,N,0},
    [0x54]={"NEG",_NO,N,0},        [0x55]={"RETN",_NO,RT,0},
    [0x56]={"IM 1",_NO,N,0},       [0x57]={"LD A,I",_NO,N,0},
    [0x58]={"IN E,(C)",_NO,N,0},   [0x59]={"OUT (C),E",_NO,N,0},
    [0x5A]={"ADC HL,DE",_NO,N,0},  [0x5B]={"LD DE,(%s)",_M16,N,0},
    [0x5C]={"NEG",_NO,N,0},        [0x5D]={"RETN",_NO,RT,0},
    [0x5E]={"IM 2",_NO,N,0},       [0x5F]={"LD A,R",_NO,N,0},
    [0x60]={"IN H,(C)",_NO,N,0},   [0x61]={"OUT (C),H",_NO,N,0},
    [0x62]={"SBC HL,HL",_NO,N,0},  [0x63]={"LD (%s),HL",_M16,N,0},
    [0x64]={"NEG",_NO,N,0},        [0x65]={"RETN",_NO,RT,0},
    [0x66]={"IM 0",_NO,N,0},       [0x67]={"RRD",_NO,N,0},
    [0x68]={"IN L,(C)",_NO,N,0},   [0x69]={"OUT (C),L",_NO,N,0},
    [0x6A]={"ADC HL,HL",_NO,N,0},  [0x6B]={"LD HL,(%s)",_M16,N,0},
    [0x6C]={"NEG",_NO,N,0},        [0x6D]={"RETN",_NO,RT,0},
    [0x6E]={"IM 0",_NO,N,0},       [0x6F]={"RLD",_NO,N,0},
    [0x70]={"IN (C)",_NO,N,0},     [0x71]={"OUT (C),0",_NO,N,0},
    [0x72]={"SBC HL,SP",_NO,N,0},  [0x73]={"LD (%s),SP",_M16,N,0},
    [0x74]={"NEG",_NO,N,0},        [0x75]={"RETN",_NO,RT,0},
    [0x76]={"IM 1",_NO,N,0},
    [0x78]={"IN A,(C)",_NO,N,0},   [0x79]={"OUT (C),A",_NO,N,0},
    [0x7A]={"ADC HL,SP",_NO,N,0},  [0x7B]={"LD SP,(%s)",_M16,N,0},
    [0x7C]={"NEG",_NO,N,0},        [0x7D]={"RETN",_NO,RT,0},
    [0x7E]={"IM 2",_NO,N,0},
    [0xA0]={"LDI",_NO,N,0},        [0xA1]={"CPI",_NO,N,0},
    [0xA2]={"INI",_NO,N,0},        [0xA3]={"OUTI",_NO,N,0},
    [0xA8]={"LDD",_NO,N,0},        [0xA9]={"CPD",_NO,N,0},
    [0xAA]={"IND",_NO,N,0},        [0xAB]={"OUTD",_NO,N,0},
    [0xB0]={"LDIR",_NO,N,0},       [0xB1]={"CPIR",_NO,N,0},
    [0xB2]={"INIR",_NO,N,0},       [0xB3]={"OTIR",_NO,N,0},
    [0xB8]={"LDDR",_NO,N,0},       [0xB9]={"CPDR",_NO,N,0},
    [0xBA]={"INDR",_NO,N,0},       [0xBB]={"OTDR",_NO,N,0},
};

int z80_operand_len(z80_operand_t kind) {
    switch (kind) {
        case Z_NONE: case Z_RST:
            return 1;
        case Z_IMM8: case Z_REL8: case Z_PORT:
            return 2;
        case Z_IMM16: case Z_ABS16: case Z_MEM16:
            return 3;
        default:
            return 1;
    }
}

/* --- internal helpers --------------------------------------------------- */

/* Copy up to `len` (<=4) raw bytes into the decoded instruction. */
static void copy_bytes(z80_insn_t *out, const uint8_t *mem, int len) {
    int i;
    for (i = 0; i < len && i < 4; i++)
        out->bytes[i] = mem[i];
}

/* Substitute `to` for every occurrence of `from` in `src`, into `dst`. */
static void str_replace(char *dst, int dstsz, const char *src,
                        const char *from, const char *to) {
    int fl = (int)strlen(from), di = 0, i = 0, k;
    while (src[i] && di < dstsz - 1) {
        if (strncmp(src + i, from, (size_t)fl) == 0) {
            for (k = 0; to[k] && di < dstsz - 1; k++)
                dst[di++] = to[k];
            i += fl;
        } else {
            dst[di++] = src[i++];
        }
    }
    dst[di] = '\0';
}

/* Render `tmpl` into `buf`, replacing a single "%s" hole with `opnd`. */
static void subst(char *buf, int bufsz, const char *tmpl, const char *opnd) {
    const char *p = strstr(tmpl, "%s");
    if (!p) { snprintf(buf, bufsz, "%s", tmpl); return; }
    snprintf(buf, bufsz, "%.*s%s%s", (int)(p - tmpl), tmpl, opnd, p + 2);
}

/* Build the operand text for the generic (main/ED) format path. */
static void operand_str(const z80_insn_t *in, char *o, int sz) {
    switch (in->operand_kind) {
        case Z_IMM8: case Z_PORT:   snprintf(o, sz, "$%02X", in->imm); break;
        case Z_IMM16: case Z_MEM16: snprintf(o, sz, "$%04X", in->imm); break;
        case Z_ABS16: case Z_REL8:  snprintf(o, sz, "$%04X", in->target); break;
        case Z_RST:                 snprintf(o, sz, "$%02X", in->target); break;
        default:                    o[0] = '\0'; break;
    }
}

/* --- prefix sub-decoders ------------------------------------------------ */

/* CB plane (plain or DDCB/FDCB). For DDCB/FDCB the caller has already filled
 * prefix/opcode/disp; here we only set the shared fields. All CF_NORMAL. */
static void fill_cb(z80_insn_t *out, uint8_t op2) {
    static const char *rot[8] =
        { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };
    unsigned x = op2 >> 6, y = (op2 >> 3) & 7;
    out->opcode       = op2;
    out->operand_kind = Z_IDX;       /* cosmetic; format reconstructs        */
    out->cflow        = CF_NORMAL;
    out->cond_code    = 0xFF;
    out->mnemonic     = (x == 0) ? rot[y] : (x == 1) ? "BIT"
                      : (x == 2) ? "RES" : "SET";
}

static int decode_cb(const uint8_t *mem, int avail, z80_insn_t *out) {
    if (avail < 2) return 0;
    out->prefix = ZP_CB;
    out->len    = 2;
    fill_cb(out, mem[1]);
    copy_bytes(out, mem, 2);
    return 2;
}

static int decode_ddcb(const uint8_t *mem, int avail, z80_insn_t *out,
                       z80_prefix_t pfx) {
    /* bytes: DD/FD, CB, disp, opcode */
    if (avail < 4) return 0;
    out->prefix = pfx;
    out->len    = 4;
    out->disp   = (int8_t)mem[2];
    fill_cb(out, mem[3]);
    copy_bytes(out, mem, 4);
    return 4;
}

static int decode_ed(const uint8_t *mem, int avail, uint16_t pc,
                     z80_insn_t *out) {
    uint8_t op2;
    const z80_opinfo_t *oi;
    int len;
    (void)pc;
    if (avail < 2) return 0;
    op2 = mem[1];
    oi  = &ed_optab[op2];
    len = (oi->operand == Z_MEM16) ? 4 : 2;
    if (avail < len) return 0;
    out->prefix       = ZP_ED;
    out->opcode       = op2;
    out->operand_kind = (z80_operand_t)oi->operand;
    out->cflow        = (z80_cflow_t)oi->cflow;
    out->mnemonic     = oi->mnemonic ? oi->mnemonic : "NOP";
    out->cond_code    = 0xFF;
    out->len          = (uint8_t)len;
    if (oi->operand == Z_MEM16)
        out->imm = (uint16_t)(mem[2] | (mem[3] << 8));
    copy_bytes(out, mem, len);
    return len;
}

/* DD/FD plane: reuse the main table with HL->IX/IY, (HL)->(IX+d)/(IY+d). */
static int decode_idx(const uint8_t *mem, int avail, uint16_t pc,
                      z80_insn_t *out, z80_prefix_t pfx) {
    uint8_t op2;
    const z80_opinfo_t *oi;
    int base, len, usehl;

    if (avail < 2) return 0;
    op2 = mem[1];

    if (op2 == 0xCB)
        return decode_ddcb(mem, avail, out,
                           pfx == ZP_DD ? ZP_DDCB : ZP_FDCB);

    /* A DD/FD immediately followed by another prefix has no effect: the first
     * byte acts as a 1-byte NOP and the sweep resumes at the next byte. */
    if (op2 == 0xDD || op2 == 0xFD || op2 == 0xED) {
        out->prefix       = pfx;
        out->opcode       = 0x00;
        out->operand_kind = Z_NONE;
        out->cflow        = CF_NORMAL;
        out->mnemonic     = "NOP";
        out->cond_code    = 0xFF;
        out->len          = 1;
        copy_bytes(out, mem, 1);
        return 1;
    }

    oi    = &z80_optab[op2];
    base  = z80_operand_len((z80_operand_t)oi->operand);
    usehl = oi->idx;
    len   = 1 + base + (usehl ? 1 : 0);
    if (avail < len) return 0;

    out->prefix    = pfx;
    out->opcode    = op2;
    out->cflow     = (z80_cflow_t)oi->cflow;
    out->mnemonic  = oi->mnemonic;
    out->cond_code = 0xFF;
    out->len       = (uint8_t)len;

    if (usehl) {
        out->operand_kind = Z_IDX;
        out->disp = (int8_t)mem[2];
        if (oi->operand == Z_IMM8)         /* LD (IX+d),n : n after the disp */
            out->imm = mem[3];
    } else {
        out->operand_kind = (z80_operand_t)oi->operand;
        switch (oi->operand) {             /* operand starts at mem[2]       */
            case Z_IMM8: case Z_PORT:
                out->imm = mem[2];
                break;
            case Z_IMM16: case Z_MEM16:
                out->imm = (uint16_t)(mem[2] | (mem[3] << 8));
                break;
            case Z_ABS16:
                out->imm = (uint16_t)(mem[2] | (mem[3] << 8));
                out->target = out->imm;
                break;
            case Z_REL8: {
                int8_t e = (int8_t)mem[2];
                out->disp   = e;
                out->imm    = mem[2];
                out->target = (uint16_t)(pc + len + e);
                break;
            }
            case Z_RST:
                out->target = op2 & 0x38;
                out->imm    = op2 & 0x38;
                break;
            default:
                break;
        }
        if (out->cflow == CF_BRANCH)
            out->cond_code = (op2 >= 0xC0) ? (op2 >> 3) & 7 : (op2 >> 3) & 3;
    }
    copy_bytes(out, mem, len);
    return len;
}

/* --- public API --------------------------------------------------------- */

int z80_decode(const uint8_t *mem, int avail, uint16_t pc, z80_insn_t *out) {
    uint8_t op;
    const z80_opinfo_t *oi;
    int len;

    memset(out, 0, sizeof(*out));
    out->cond_code = 0xFF;
    if (avail < 1) return 0;
    out->pc = pc;
    op = mem[0];

    if (op == 0xCB) { int r = decode_cb(mem, avail, out); out->pc = pc; return r; }
    if (op == 0xED) return decode_ed(mem, avail, pc, out);
    if (op == 0xDD) return decode_idx(mem, avail, pc, out, ZP_DD);
    if (op == 0xFD) return decode_idx(mem, avail, pc, out, ZP_FD);

    oi  = &z80_optab[op];
    len = z80_operand_len((z80_operand_t)oi->operand);
    if (avail < len) return 0;

    out->prefix       = ZP_NONE;
    out->opcode       = op;
    out->operand_kind = (z80_operand_t)oi->operand;
    out->cflow        = (z80_cflow_t)oi->cflow;
    out->mnemonic     = oi->mnemonic;
    out->len          = (uint8_t)len;

    switch (oi->operand) {
        case Z_IMM8: case Z_PORT:
            out->imm = mem[1];
            break;
        case Z_IMM16: case Z_MEM16:
            out->imm = (uint16_t)(mem[1] | (mem[2] << 8));
            break;
        case Z_ABS16:
            out->imm    = (uint16_t)(mem[1] | (mem[2] << 8));
            out->target = out->imm;
            break;
        case Z_REL8: {
            int8_t e = (int8_t)mem[1];
            out->disp   = e;
            out->imm    = mem[1];
            out->target = (uint16_t)(pc + len + e);
            break;
        }
        case Z_RST:
            out->target = op & 0x38;
            out->imm    = op & 0x38;
            break;
        default:
            break;
    }
    if (out->cflow == CF_BRANCH)
        out->cond_code = (op == 0x10) ? 0xFF   /* DJNZ: no condition code */
                       : (op >= 0xC0) ? (op >> 3) & 7 : (op >> 3) & 3;

    copy_bytes(out, mem, len);
    return len;
}

/* CB / DDCB / FDCB disassembly, reconstructed from the opcode bits. */
static void format_cb(const z80_insn_t *in, char *buf, int bufsz) {
    static const char *rot[8] =
        { "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL" };
    static const char *reg[8] =
        { "B", "C", "D", "E", "H", "L", "(HL)", "A" };
    unsigned op = in->opcode, x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    char rb[16];

    if (in->prefix == ZP_DDCB || in->prefix == ZP_FDCB) {
        const char *ir = (in->prefix == ZP_DDCB) ? "IX" : "IY";
        int d = in->disp;
        if (d < 0) snprintf(rb, sizeof rb, "(%s-$%02X)", ir, (unsigned)(-d));
        else       snprintf(rb, sizeof rb, "(%s+$%02X)", ir, (unsigned)d);
    } else {
        snprintf(rb, sizeof rb, "%s", reg[z]);
    }

    switch (x) {
        case 0:  snprintf(buf, bufsz, "%s %s", rot[y], rb); break;
        case 1:  snprintf(buf, bufsz, "BIT %u,%s", y, rb);  break;
        case 2:  snprintf(buf, bufsz, "RES %u,%s", y, rb);  break;
        default: snprintf(buf, bufsz, "SET %u,%s", y, rb);  break;
    }
}

/* DD/FD disassembly: rewrite the main template for IX/IY. */
static void format_idx(const z80_insn_t *in, char *buf, int bufsz) {
    const char *ir = (in->prefix == ZP_DD) ? "IX" : "IY";
    char tmp[48], o[24];

    if (in->operand_kind == Z_IDX) {
        char idxb[16];
        int d = in->disp;
        if (d < 0) snprintf(idxb, sizeof idxb, "(%s-$%02X)", ir, (unsigned)(-d));
        else       snprintf(idxb, sizeof idxb, "(%s+$%02X)", ir, (unsigned)d);
        str_replace(tmp, sizeof tmp, in->mnemonic, "(HL)", idxb);
        if (strstr(tmp, "%s")) {           /* LD (IX+d),n */
            snprintf(o, sizeof o, "$%02X", in->imm);
            subst(buf, bufsz, tmp, o);
        } else {
            snprintf(buf, bufsz, "%s", tmp);
        }
    } else {
        str_replace(tmp, sizeof tmp, in->mnemonic, "HL", ir);
        operand_str(in, o, sizeof o);
        subst(buf, bufsz, tmp, o);
    }
}

void z80_format(const z80_insn_t *in, char *buf, int bufsz) {
    char o[24];
    if (bufsz <= 0) return;
    switch (in->prefix) {
        case ZP_CB:
        case ZP_DDCB:
        case ZP_FDCB:
            format_cb(in, buf, bufsz);
            return;
        case ZP_DD:
        case ZP_FD:
            format_idx(in, buf, bufsz);
            return;
        default:                            /* ZP_NONE and ZP_ED */
            operand_str(in, o, sizeof o);
            subst(buf, bufsz, in->mnemonic ? in->mnemonic : "NOP", o);
            return;
    }
}
