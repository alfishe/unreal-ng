#include "stdafx.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/cpulogic.h"
#include "emulator/cpu/z80.h"
#include "emulator/cpu/cputables.h"
#include "op_noprefix.h"
#include "op_ed.h"

/// region <Information>

// There are ED prefixed undocumented commands (synonyms to existing documented ones)
// See: http://www.z80.info/zip/z80-documented.pdf
// See: http://www.z80.info/z80undoc3.txt
//    1.4) ED Prefix [1]
//
//    There are a number of undocumented EDxx instructions, of which most are
//            duplicates of documented instructions. Any instruction not listed has
//    no effect (just like 2 NOP instructions).
//
//    The complete list except for the block instructions: (* = undocumented)
//
//    ED40   IN B,(C)                 ED60   IN H,(C)
//    ED41   OUT (C),B                ED61   OUT (C),H
//    ED42   SBC HL,BC                ED62   SBC HL,HL
//    ED43   LD (nn),BC               ED63   LD (nn),HL
//    ED44   NEG                      ED64 * NEG
//    ED45   RETN                     ED65 * RETN
//    ED46   IM 0                     ED66 * IM 0
//    ED47   LD I,A                   ED67   RRD
//    ED48   IN C,(C)                 ED68   IN L,(C)
//    ED49   OUT (C),C                ED69   OUT (C),L
//    ED4A   ADC HL,BC                ED6A   ADC HL,HL
//    ED4B   LD BC,(nn)               ED6B   LD HL,(nn)
//    ED4C * NEG                      ED6C * NEG
//    ED4D   RETI                     ED6D * RETN
//    ED4E * IM 0                     ED6E * IM 0
//    ED4F   LD R,A                   ED6F   RLD
//
//    ED50   IN D,(C)                 ED70 * IN (C) / IN F,(C)
//    ED51   OUT (C),D                ED71 * OUT (C),0
//    ED52   SBC HL,DE                ED72   SBC HL,SP
//    ED53   LD (nn),DE               ED73   LD (nn),SP
//    ED54 * NEG                      ED74 * NEG
//    ED55 * RETN                     ED75 * RETN
//    ED56   IM 1                     ED76 * IM 1
//    ED57   LD A,I                   ED77 * NOP
//    ED58   IN E,(C)                 ED78   IN A,(C)
//    ED59   OUT (C),E                ED79   OUT (C),A
//    ED5A   ADC HL,DE                ED7A   ADC HL,SP
//    ED5B   LD DE,(nn)               ED7B   LD SP,(nn)
//    ED5C * NEG                      ED7C * NEG
//    ED5D * RETN                     ED7D * RETN
//    ED5E   IM 2                     ED7E * IM 2
//    ED5F   LD A,R                   ED7F * NOP
//
//    The ED70 instruction reads from I/O port C, but does not store the result.
//    It just affects the flags like the other IN x,(C) instruction. ED71 simply
//    outs the value 0 to I/O port C.
//
//    The ED63 is a duplicate of the 22 instruction (LD (nn),HL) just like the
//    ED6B is a duplicate of the 2A instruction. Of course the timings are
//    different. These instructions are listed in the official documentation.
//
//    According to Gerton Lunter (gerton@math.rug.nl):
//    The instructions ED 4E and ED 6E are IM 0 equivalents: when FF was put
//    on the bus (physically) at interrupt time, the Spectrum continued to
//    execute normally, whereas when an EF (RST #28) was put on the bus it
//    crashed, just as it does in that case when the Z80 is in the official
//    interrupt mode 0.  In IM 1 the Z80 just executes a RST #38 (opcode FF)
//    no matter what is on the bus.
//
//    [5] All the RETI/RETN instructions are the same, all like the RETN
//    instruction. So they all, including RETI, copy IFF2 to IFF1. More information
//    on RETI and RETN and IM x is in the part about Interrupts and I register (3).

/// endregion </Information>

// ED opcodes

Z80OPCODE ope_40(Z80 *cpu) { // in b,(c)
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->b = cpu->in(cpu->bc);
   cpu->f = log_f[cpu->b] | (cpu->f & CF);
}

Z80OPCODE ope_41(Z80 *cpu) { // out (c),b
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->b);
}

Z80OPCODE ope_42(Z80 *cpu) { // sbc hl,bc
    cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl;
    int32_t bc = cpu->bc;

    int32_t halfHL = hl;
    int32_t halfBC = bc;

    uint8_t flags = NF;
    flags |= ((halfHL - halfBC - (cpu->af & CF)) >> 8) & 0x10; // HF

    uint32_t result = hl - bc - (cpu->af & CF);
    if (result & 0x10000)
        flags |= CF;
    if (!(result & 0xFFFF))
        flags |= ZF;

    int32_t ri = hl - bc - (int)(cpu->af & CF);
    if (ri < -0x8000 || ri >= 0x8000)
        flags |= PV;

    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
}

Z80OPCODE ope_43(Z80 *cpu) { // ld (nnnn),bc
    uint16_t pc = cpu->pc;

    uint16_t addr = cpu->rd(pc++, true);
    addr += cpu->rd(pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->wd(addr, cpu->c);
    cpu->wd(addr + 1, cpu->b);

    cpu->pc = pc;
}

Z80OPCODE ope_44(Z80 *cpu) { // neg
   cpu->f = sbc_f[cpu->a];
   cpu->a = -cpu->a;
}

Z80OPCODE ope_45(Z80 *cpu) { // retn
   cpu->iff1 = cpu->iff2;

   uint16_t sp = cpu->sp;

   uint16_t addr = cpu->rd(sp++);
   addr += 0x100 * cpu->rd(sp++);

   cpu->last_branch = cpu->pc - 2;
   cpu->pc = addr;
   cpu->memptr = addr;

   cpu->sp = sp;

   // TODO: Is callback still needed?
   cpu->retn();
}

Z80OPCODE ope_46(Z80 *cpu) { // im 0
   cpu->im = 0;
}

Z80OPCODE ope_47(Z80 *cpu) { // ld i,a
   cpu->i = cpu->a;

   cputact(1);
}

Z80OPCODE ope_48(Z80 *cpu) { // in c,(c)
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->c = cpu->in(cpu->bc);
   cpu->f = log_f[cpu->c] | (cpu->f & CF);
}

Z80OPCODE ope_49(Z80 *cpu) { // out (c),c
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->c);
}

Z80OPCODE ope_4A(Z80 *cpu) { // adc hl,bc
    cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl & 0xFFFF;
    int32_t bc = cpu->bc & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfBC = bc & 0x0FFF;

    // Calculate half-carry flag (HF)
    uint8_t flags = ((halfHL + halfBC + (cpu->f & CF)) >> 8) & 0x10; // HF
    uint32_t result = hl + bc + (cpu->f & CF);

    if (result & 0x10000)
        flags |= CF;
    if (!(result & 0xFFFF))
        flags |= ZF;

    int ri = hl + bc + (int)(cpu->af & CF);

    if (ri < -0x8000 || ri >= 0x8000)
        flags |= PV;

    // Store result back to registers
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3|F5|SF));

    cputact(7);
}

Z80OPCODE ope_4B(Z80 *cpu) { // ld bc,(nnnn)
    uint16_t pc = cpu->pc;

    uint16_t addr = cpu->rd(pc++, true);
    addr += cpu->rd(pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->c = cpu->rd(addr);
    cpu->b = cpu->rd(addr + 1);

    cpu->pc = pc;
}

#define ope_4C ope_44   // neg

Z80OPCODE ope_4D(Z80 *cpu) { // reti
    cpu->iff1 = cpu->iff2;

    uint16_t sp = cpu->sp;

    uint16_t addr = cpu->rd(sp++);
    addr += 0x100 * cpu->rd(sp++);

    cpu->last_branch = cpu->pc - 2;
    cpu->pc = addr;
    cpu->memptr = addr;

    cpu->sp = sp;
}

#define ope_4E ope_46  // im0 undocumented

Z80OPCODE ope_4F(Z80 *cpu) { // ld r,a
   cpu->r_low = cpu->a;
   cpu->r_hi = cpu->a & 0x80;

   cputact(1);
}

Z80OPCODE ope_50(Z80 *cpu) { // in d,(c)
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->d = cpu->in(cpu->bc);
   cpu->f = log_f[cpu->d] | (cpu->f & CF);
}

Z80OPCODE ope_51(Z80 *cpu) { // out (c),d
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->d);
}

Z80OPCODE ope_52(Z80 *cpu) { // sbc hl,de
    cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl & 0xFFFF;
    int32_t de = cpu->de & 0xFFFF;
    int32_t halfHL = hl & 0x0FFF;
    int32_t halfDE = de & 0x0FFF;
    int32_t carryAF = cpu->af & CF;

    // Calculate half-carry flag
    uint8_t flags = NF;
    flags |= ((halfHL - halfDE - carryAF) >> 8) & 0x10; // HF

    uint32_t result = hl - de - carryAF;
    if (result & 0x10000)
        flags |= CF;

    if (!(result & 0xFFFF))
        flags |= ZF;

    int ri = hl - de - carryAF;
    if (ri < -0x8000 || ri >= 0x8000)
        flags |= PV;

    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
}

Z80OPCODE ope_53(Z80 *cpu) { // ld (nnnn),de
    uint16_t pc = cpu->pc;

    // Read 2 bytes of address from memory
    uint16_t adr = cpu->rd(pc++, true);
    adr += cpu->rd(pc++, true) * 0x100;

    cpu->memptr = adr + 1;

    // Write 2 bytes from DE to memory using fetched address
    cpu->wd(adr, cpu->e);
    cpu->wd(adr + 1, cpu->d);

    cpu->pc = pc;
}

#define ope_54 ope_44 // neg
#define ope_55 ope_45 // retn

Z80OPCODE ope_56(Z80 *cpu) { // im 1
    cpu->im = 1;
}

Z80OPCODE ope_57(Z80 *cpu) { // ld a,i
   cpu->a = cpu->i;
   cpu->f = (log_f[cpu->r_low & 0x7F] | (cpu->f & CF)) & ~PV;	// TODO: dirty fix with unknown reason and test sequence

   if (cpu->iff2 && (((uint32_t)(cpu->t + 10) < cpu->tpi) || cpu->eipos + 8 == cpu->t))
      cpu->f |= PV;

   cputact(1);
}

Z80OPCODE ope_58(Z80 *cpu) { // in e,(c)
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->e = cpu->in(cpu->bc);
   cpu->f = log_f[cpu->e] | (cpu->f & CF);
}

Z80OPCODE ope_59(Z80 *cpu) { // out (c),e
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->e);
}

Z80OPCODE ope_5A(Z80 *cpu) { // adc hl,de
    cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl & 0xFFFF;
    int32_t de = cpu->de & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfDE = de & 0x0FFF;

    uint8_t flags = ((halfHL + halfDE + (cpu->af & CF)) >> 8) & 0x10; /* HF */
    uint32_t result = hl + de + (cpu->af & CF);

    if (result & 0x10000)
       flags |= CF;
    if (!(result & 0xFFFF))
       flags |= ZF;

    int ri = (int)(short)cpu->hl + (int)(short)cpu->de + (int)(cpu->af & CF);
    if (ri < -0x8000 || ri >= 0x8000)
       flags |= PV;

    // Store result back to registers
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3|F5|SF));

    cputact(7);
}

Z80OPCODE ope_5B(Z80 *cpu) { // ld de,(nnnn)
    uint16_t pc = cpu->pc;

    uint16_t addr = cpu->rd(pc++, true);
    addr += cpu->rd(pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->e = cpu->rd(addr);
    cpu->d = cpu->rd(addr + 1);

    cpu->pc = pc;
}

#define ope_5C ope_44   // neg
#define ope_5D ope_4D   // reti

Z80OPCODE ope_5E(Z80 *cpu) { // im 2
   cpu->im = 2;
}

Z80OPCODE ope_5F(Z80 *cpu) { // ld a,r
   cpu->a = (cpu->r_low & 0x7F) | cpu->r_hi;
   cpu->f = (log_f[cpu->a] | (cpu->f & CF)) & ~PV;

   if (cpu->iff2 && (((uint32_t)(cpu->t + 10) < cpu->tpi) || cpu->eipos + 8==cpu->t))
      cpu->f |= PV;

   cputact(1);
}

Z80OPCODE ope_60(Z80 *cpu) { // in h,(c)
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->h = cpu->in(cpu->bc);
   cpu->f = log_f[cpu->h] | (cpu->f & CF);
}

Z80OPCODE ope_61(Z80 *cpu) { // out (c),h
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->h);
}

Z80OPCODE ope_62(Z80 *cpu) { // sbc hl,hl
   cpu->memptr = cpu->hl + 1;

   uint8_t flags = NF;
    flags |= (cpu->f & CF) << 4; // HF - copy from CF

   uint32_t result = 0 - (cpu->af & CF);
   if (result & 0x10000)
       flags |= CF;
   if (!(result & 0xFFFF))
       flags |= ZF;

   // never set PV
   cpu->hl = result & 0xFFFF;
   cpu->f = flags | (cpu->h & (F3 | F5 | SF));

   cputact(7);
}

#define ope_63 op_22 // ld (nnnn),hl
#define ope_64 ope_44 // neg
#define ope_65 ope_45 // retn
#define ope_66 ope_46 // im 0

Z80OPCODE ope_67(Z80 *cpu) { // rrd
  uint8_t value = cpu->rd(cpu->hl);

  cpu->memptr = cpu->hl + 1;

  cputact(4);

  cpu->wd(cpu->hl, (cpu->a << 4) | (value >> 4));

  cpu->a = (cpu->a & 0xF0) | (value & 0x0F);
  cpu->f = log_f[cpu->a] | (cpu->f & CF);
}

Z80OPCODE ope_68(Z80 *cpu) { // in l,(c)
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->l = cpu->in(cpu->bc);
   cpu->f = log_f[cpu->l] | (cpu->f & CF);
}

Z80OPCODE ope_69(Z80 *cpu) { // out (c),l
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->l);
}
Z80OPCODE ope_6A(Z80 *cpu) { // adc hl,hl
   cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl & 0xFFFF;

   uint8_t flags = ((cpu->h << 1) & 0x10); // HF
   unsigned result = hl + hl + (cpu->af & CF);

   if (result & 0x10000)
       flags |= CF;
   if (!(result & 0xFFFF))
       flags |= ZF;

   int ri = hl + hl + (int)(cpu->af & CF);
   if (ri < -0x8000 || ri >= 0x8000)
       flags |= PV;

   cpu->hl = result & 0xFFFF;
   cpu->f = flags | (cpu->h & (F3 | F5 | SF));

   cputact(7);
}

#define ope_6B op_2A // ld hl,(nnnn)
#define ope_6C ope_44   // neg
#define ope_6D ope_4D   // reti
#define ope_6E ope_56   // im 0/1 -> im 1

Z80OPCODE ope_6F(Z80 *cpu) { // rld
  uint8_t value = cpu->rd(cpu->hl);

  cpu->memptr = cpu->hl + 1;

  cputact(4);

  cpu->wd(cpu->hl, (cpu->a & 0x0F) | (value << 4));

  cpu->a = (cpu->a & 0xF0) | (value >> 4);
  cpu->f = log_f[cpu->a] | (cpu->f & CF);
}

Z80OPCODE ope_70(Z80 *cpu) { // in (c) - Undocumented. Reads from the port and affects flags, but does not store the value to a register
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->f = log_f[cpu->in(cpu->bc)] | (cpu->f & CF);
}

Z80OPCODE ope_71(Z80 *cpu) { // out (c),0 - Undocumented. Writes zero to the port
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->outc0);
}

Z80OPCODE ope_72(Z80 *cpu) { // sbc hl,sp
    cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl & 0xFFFF;
    int32_t sp = cpu->sp & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfSP = sp & 0x0FFF;

    uint8_t flags = NF;
    flags |= ((halfHL - halfSP - (cpu->af & CF)) >> 8) & 0x10; // HF

    uint32_t result = hl - sp - (cpu->af & CF);

    if (result & 0x10000)
        flags |= CF;
    if (!(result & 0xFFFF))
        flags |= ZF;

    int ri = hl - sp - (int)(cpu->af & CF);
    if (ri < -0x8000 || ri >= 0x8000)
        flags |= PV;

    // Store result back to regusters
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
}

Z80OPCODE ope_73(Z80 *cpu) { // ld (nnnn),sp
    uint16_t pc = cpu->pc;

    uint16_t addr = cpu->rd(pc++, true);
    addr += cpu->rd(pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->wd(addr, cpu->spl);
    cpu->wd(addr + 1, cpu->sph);

    cpu->pc = pc;
}

#define ope_74 ope_44 // neg
#define ope_75 ope_45 // retn

Z80OPCODE ope_76(Z80 *cpu) { // im 1
   cpu->im = 1;
}

#define ope_77 op_00  // nop

Z80OPCODE ope_78(Z80 *cpu) { // in a,(c)
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->a = cpu->in(cpu->bc);
   cpu->f = log_f[cpu->a] | (cpu->f & CF);
}

Z80OPCODE ope_79(Z80 *cpu) { // out (c),a
   cputact(4);

   cpu->memptr = cpu->bc + 1;

   cpu->out(cpu->bc, cpu->a);
}

Z80OPCODE ope_7A(Z80 *cpu) { // adc hl,sp
    cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl & 0xFFFF;
    int32_t sp = cpu->sp & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfSP = sp & 0x0FFF;

    uint8_t flags = ((halfHL + halfSP + (cpu->af & CF)) >> 8) & 0x10; // HF

    uint32_t result = hl + sp + (cpu->af & CF);

    if (result & 0x10000)
       flags |= CF;
    if (!(uint16_t)result)
       flags |= ZF;

    int ri = (int)(short)cpu->hl + (int)(short)cpu->sp + (int)(cpu->af & CF);
    if (ri < -0x8000 || ri >= 0x8000)
       flags |= PV;

    // Store result back to registers
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
}

Z80OPCODE ope_7B(Z80 *cpu) { // ld sp,(nnnn)
    uint16_t pc = cpu->pc;

    uint16_t addr = cpu->rd(pc++, true);
    addr += cpu->rd(pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->spl = cpu->rd(addr);
    cpu->sph = cpu->rd(addr + 1);

    cpu->pc = pc;
}

#define ope_7C ope_44   // neg
#define ope_7D ope_4D   // reti
#define ope_7E ope_5E   // im 2
#define ope_7F op_00    // nop

Z80OPCODE ope_A0(Z80 *cpu) { // ldi
    uint8_t value = cpu->rd(cpu->hl++);

    cpu->wd(cpu->de++, value);

    value += cpu->a;
    value = (value & F3) + ((value << 4) & F5);

    cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--cpu->bc)
        cpu->f |= PV;

    cputact(2);
}

Z80OPCODE ope_A1(Z80 *cpu) { // cpi
   uint8_t cf = cpu->f & CF;

   uint8_t value = cpu->rd(cpu->hl++);

   cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

   if (--cpu->bc)
	   cpu->f |= PV;

   cpu->memptr++;

   cputact(5);
}

Z80OPCODE ope_A2(Z80 *cpu) { // ini
   cpu->memptr = cpu->bc + 1;

   cputact(4);

   uint16_t hl = cpu->hl;
   cpu->wd(hl++, cpu->in(cpu->bc));

   dec8(cpu, cpu->b);

   cpu->hl = hl;

   cputact(1);
}

Z80OPCODE ope_A3(Z80 *cpu) { // outi
    cputact(1);

    dec8(cpu, cpu->b);

    uint16_t hl = cpu->hl;
    uint8_t value = cpu->rd(hl++);

    cputact(4);

    cpu->out(cpu->bc, value);
    cpu->f &= ~CF;
    if (!cpu->l)
        cpu->f |= CF;

    cpu->hl = hl;

    cpu->memptr = cpu->bc + 1;
}

Z80OPCODE ope_A8(Z80 *cpu) { // ldd
    uint16_t hl = cpu->hl;
    uint16_t de = cpu->de;

    uint8_t value = cpu->rd(hl--);

    cpu->wd(de--, value);

    value += cpu->a;
    value = (value & F3) + ((value << 4) & F5);

    cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--cpu->bc)
        cpu->f |= PV;

    cpu->hl = hl;
    cpu->de = de;

    cputact(2);
}

Z80OPCODE ope_A9(Z80 *cpu) { // cpd
    uint8_t cf = cpu->f & CF;

    uint16_t hl = cpu->hl;
    uint8_t value = cpu->rd(hl--);

    cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

    if (--cpu->bc)
        cpu->f |= PV;

    cpu->hl = hl;

    cpu->memptr--;

    cputact(5);
}

Z80OPCODE ope_AA(Z80 *cpu) { // ind
    cpu->memptr = cpu->bc - 1;

    cputact(4);

    uint16_t hl = cpu->hl;
    cpu->wd(hl--, cpu->in(cpu->bc));

    dec8(cpu, cpu->b);

    cpu->hl = hl;

    cputact(1);
}

Z80OPCODE ope_AB(Z80 *cpu) { // outd
    cputact(1);

    dec8(cpu, cpu->b);

    uint16_t hl = cpu->hl;
    uint8_t value = cpu->rd(hl--);

    cputact(4);

    cpu->out(cpu->bc, value);

    cpu->f &= ~CF;

    if (cpu->l == 0xFF)
        cpu->f |= CF;

    cpu->hl = hl;

    cpu->memptr = cpu->bc - 1;
}

Z80OPCODE ope_B0(Z80 *cpu) { // ldir
    uint16_t hl = cpu->hl;
    uint16_t de = cpu->de;

	uint8_t value = cpu->rd(hl++);

	cpu->wd(de++, value);

    value += cpu->a;
    value = (value & F3) + ((value << 4) & F5);

	cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

	if (--cpu->bc)
	{
		cpu->f |= PV;
		cpu->pc = (cpu->pc - 2) & 0xFFFF;

		cputact(7);

		cpu->memptr = cpu->pc + 1;
	}
	else
	{
		cputact(2);
	}

	cpu->hl = hl;
	cpu->de = de;
}

Z80OPCODE ope_B1(Z80 *cpu) { // cpir
   cpu->memptr++;

   uint8_t cf = cpu->f & CF;

   uint16_t hl = cpu->hl;
   uint8_t value = cpu->rd(hl++);

   cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

   cputact(5);

   if (--cpu->bc)
   {
      cpu->f |= PV;

	  if (!(cpu->f & ZF))
	  {
		  cpu->pc = (cpu->pc - 2) & 0xFFFF;

		  cputact(5);

		  cpu->memptr = cpu->pc + 1;
	  }
   }

   cpu->hl = hl;
}

Z80OPCODE ope_B2(Z80 *cpu) { // inir
   cpu->memptr = cpu->bc + 1;

   cputact(4);

   uint16_t hl = cpu->hl;
   cpu->wd(hl++, cpu->in(cpu->bc));

   dec8(cpu, cpu->b);

   if (cpu->b)
   {
	   cpu->f |= PV;
	   cpu->pc = (cpu->pc - 2) & 0xFFFF;

	   cputact(6);
   }
   else
   {
	   cpu->f &= ~PV;

	   cputact(1);
   }

   cpu->hl = hl;
}

Z80OPCODE ope_B3(Z80 *cpu) { // otir
   cputact(1);

   dec8(cpu, cpu->b);

   uint16_t hl = cpu->hl;
   uint8_t value = cpu->rd(hl++);

   cputact(4);

   cpu->out(cpu->bc, value);

   if (cpu->b)
   {
	   cpu->f |= PV;
	   cpu->pc = (cpu->pc - 2) & 0xFFFF;

	   cputact(5);
   }
   else
   {
	   cpu->f &= ~PV;
   }

   cpu->f &= ~CF; if (!cpu->l) cpu->f |= CF;

   cpu->hl = hl;

   cpu->memptr = cpu->bc + 1;
}

Z80OPCODE ope_B8(Z80 *cpu) { // lddr
    uint16_t hl = cpu->hl;
    uint16_t de = cpu->de;

    uint8_t value = cpu->rd(hl--);

    cpu->wd(de--, value);

    value += cpu->a; value = (value & F3) + ((value << 4) & F5);

    cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--cpu->bc)
    {
       cpu->f |= PV;
       cpu->pc = (cpu->pc - 2) & 0xFFFF;

       cputact(7);
    }
    else
    {
       cputact(2);
    }

    cpu->hl = hl;
    cpu->de = de;
}

Z80OPCODE ope_B9(Z80 *cpu) { // cpdr
   cpu->memptr--;

   uint8_t cf = cpu->f & CF;

   uint16_t hl = cpu->hl;
   uint8_t value = cpu->rd(hl--);

   cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

   cputact(5);

   if (--cpu->bc)
   {
      cpu->f |= PV;

	  if (!(cpu->f & ZF))
	  {
		  cpu->pc = cpu->pc - 2;

		  cputact(5);

		  cpu->memptr = cpu->pc + 1;
	  }
   }

   cpu->hl = hl;
}

Z80OPCODE ope_BA(Z80 *cpu) { // indr
   cpu->memptr = cpu->bc - 1;

   cputact(4);

   uint16_t hl = cpu->hl;
   cpu->wd(hl--, cpu->in(cpu->bc));

   dec8(cpu, cpu->b);

   if (cpu->b)
   {
	   cpu->f |= PV;
	   cpu->pc = cpu->pc - 2;

	   cputact(6);
   }
   else
   {
	   cpu->f &= ~PV;

	   cputact(1);
   }

   cpu->hl = hl;
}

Z80OPCODE ope_BB(Z80 *cpu) { // otdr
   cputact(1);

   dec8(cpu, cpu->b);

   uint16_t hl = cpu->hl;
   uint8_t value = cpu->rd(hl--);

   cputact(4);

   cpu->out(cpu->bc, value);

   if (cpu->b)
   {
	   cpu->f |= PV;
       cpu->pc = cpu->pc - 2;

	   cputact(5);
   }
   else
	   cpu->f &= ~PV;

   cpu->f &= ~CF;

   if (cpu->l == 0xFF)
	   cpu->f |= CF;

   cpu->hl = hl;

   cpu->memptr = cpu->bc - 1;
}


STEPFUNC const ext_opcode[0x100] =
{
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,

   ope_40, ope_41, ope_42, ope_43, ope_44, ope_45, ope_46, ope_47,
   ope_48, ope_49, ope_4A, ope_4B, ope_4C, ope_4D, ope_4E, ope_4F,
   ope_50, ope_51, ope_52, ope_53, ope_54, ope_55, ope_56, ope_57,
   ope_58, ope_59, ope_5A, ope_5B, ope_5C, ope_5D, ope_5E, ope_5F,
   ope_60, ope_61, ope_62, ope_63, ope_64, ope_65, ope_66, ope_67,
   ope_68, ope_69, ope_6A, ope_6B, ope_6C, ope_6D, ope_6E, ope_6F,
   ope_70, ope_71, ope_72, ope_73, ope_74, ope_75, ope_76, ope_77,
   ope_78, ope_79, ope_7A, ope_7B, ope_7C, ope_7D, ope_7E, ope_7F,

   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   ope_A0, ope_A1, ope_A2, ope_A3, op_00, op_00, op_00, op_00,
   ope_A8, ope_A9, ope_AA, ope_AB, op_00, op_00, op_00, op_00,
   ope_B0, ope_B1, ope_B2, ope_B3, op_00, op_00, op_00, op_00,
   ope_B8, ope_B9, ope_BA, ope_BB, op_00, op_00, op_00, op_00,

   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
};


/// ED-prefix handler. Executes one more M1 Core cycle to fetch extended opcode
/// \param cpu
Z80OPCODE op_ED(Z80 *cpu)
{
    // Record used prefix
    cpu->prefix = 0xED;

	uint8_t opcode = cpu->m1_cycle();
	(ext_opcode[opcode])(cpu);

	// Finalize opcode
	cpu->opcode = opcode;
}

