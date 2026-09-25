// opcodes-ed.cpp - ED prefix opcodes, ported from core op_ed.cpp.


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

Z80OPCODE ope_40(Z80CPU *cpu, int tact0) { // in b,(c)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->b = Z80Pin(cpu, cpu->bc);
   cpu->f = log_f[cpu->b] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_41(Z80CPU *cpu, int tact0) { // out (c),b
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->b);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_42(Z80CPU *cpu, int tact0) { // sbc hl,bc
    Z80_T_BEGIN(cpu);
    cpu->memptr = cpu->hl + 1;

    int32_t hl = cpu->hl;
    int32_t bc = cpu->bc;

    int32_t halfHL = hl & 0x0FFF;  // Lower 12 bits for half-carry
    int32_t halfBC = bc & 0x0FFF;

    uint8_t flags = NF;
    flags |= ((halfHL - halfBC - (cpu->af & CF)) >> 8) & 0x10; // HF

    uint32_t result = hl - bc - (cpu->af & CF);
    if (result & 0x10000)
        flags |= CF;
    if (!(result & 0xFFFF))
        flags |= ZF;
    // P/V = overflow: operands different sign, result sign differs from minuend
    if (((hl ^ bc) & 0x8000) && ((hl ^ result) & 0x8000))
        flags |= PV;

    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_43(Z80CPU *cpu, int tact0) { // ld (nnnn),bc
    Z80_T_BEGIN(cpu);
    uint16_t pc = cpu->pc;

    uint16_t addr = Z80Rd(cpu, pc++, true);
    addr += Z80Rd(cpu, pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    Z80Wd(cpu, addr, cpu->c);
    Z80Wd(cpu, addr + 1, cpu->b);

    cpu->pc = pc;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_44(Z80CPU *cpu, int tact0) { // neg
   cpu->f = sbc_f[cpu->a];
   cpu->a = -cpu->a;
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE ope_45(Z80CPU *cpu, int tact0) { // retn
   Z80_T_BEGIN(cpu);
   cpu->iff1 = cpu->iff2;

   uint16_t sp = cpu->sp;

   uint16_t addr = Z80Rd(cpu, sp++);
   addr += 0x100 * Z80Rd(cpu, sp++);


   cpu->pc = addr;
   cpu->memptr = addr;

   cpu->sp = sp;

   // TODO: Is callback still needed?
   cpu->retn();

   // RETN notification (Z80CpuSetRetnFn), same shape as the RETI hook in
   // ope_4D: publish T first so the host observes the exact current T.
   // Aliased RETN opcodes (ED 55/65/75) share this body.
   Z80_T_PUBLISH(cpu);
   if (cpu->retnCallback)
       cpu->retnCallback(cpu, cpu->retnData);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_46(Z80CPU *cpu, int tact0) { // im 0
   cpu->im = 0;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE ope_47(Z80CPU *cpu, int tact0) { // ld i,a
   Z80_T_BEGIN(cpu);
   cpu->i = cpu->a;

   cputact(1);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_48(Z80CPU *cpu, int tact0) { // in c,(c)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->c = Z80Pin(cpu, cpu->bc);
   cpu->f = log_f[cpu->c] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_49(Z80CPU *cpu, int tact0) { // out (c),c
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->c);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_4A(Z80CPU *cpu, int tact0) { // adc hl,bc
    Z80_T_BEGIN(cpu);
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

    // P/V = overflow: operands same sign, result different sign
    if (((hl ^ bc) & 0x8000) == 0 && ((hl ^ result) & 0x8000))
        flags |= PV;

    // Store result back to registers
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3|F5|SF));

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_4B(Z80CPU *cpu, int tact0) { // ld bc,(nnnn)
    Z80_T_BEGIN(cpu);
    uint16_t pc = cpu->pc;

    uint16_t addr = Z80Rd(cpu, pc++, true);
    addr += Z80Rd(cpu, pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->c = Z80Rd(cpu, addr);
    cpu->b = Z80Rd(cpu, addr + 1);

    cpu->pc = pc;
    Z80_RETURN_Q(0, tact_);
}

#define ope_4C ope_44   // neg

Z80OPCODE ope_4D(Z80CPU *cpu, int tact0) { // reti
    Z80_T_BEGIN(cpu);
    cpu->iff1 = cpu->iff2;

    uint16_t sp = cpu->sp;

    uint16_t addr = Z80Rd(cpu, sp++);
    addr += 0x100 * Z80Rd(cpu, sp++);


    cpu->pc = addr;
    cpu->memptr = addr;

    cpu->sp = sp;

    // RETI notification (z80ex-style device hook), fired from the handler
    // itself as z80ex does - the step wrapper carries no per-step opcode
    // test for it. Aliased RETI opcodes (ED 55/5D/65/6D/7D) share this body.
    // Publish first: the hook is a host callback and must observe the exact
    // current T (flat-mode bus accesses do not publish on their own).
    Z80_T_PUBLISH(cpu);
    if (cpu->retiCallback)
        cpu->retiCallback(cpu, cpu->retiData);
    Z80_RETURN_Q(0, tact_);
}

#define ope_4E ope_46  // im0 undocumented

Z80OPCODE ope_4F(Z80CPU *cpu, int tact0) { // ld r,a
   Z80_T_BEGIN(cpu);
   cpu->r_low = cpu->a;
   cpu->r_hi = cpu->a & 0x80;

   cputact(1);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_50(Z80CPU *cpu, int tact0) { // in d,(c)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->d = Z80Pin(cpu, cpu->bc);
   cpu->f = log_f[cpu->d] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_51(Z80CPU *cpu, int tact0) { // out (c),d
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->d);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_52(Z80CPU *cpu, int tact0) { // sbc hl,de
    Z80_T_BEGIN(cpu);
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

    // P/V = overflow: operands different sign, result sign differs from minuend
    if (((hl ^ de) & 0x8000) && ((hl ^ result) & 0x8000))
        flags |= PV;

    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_53(Z80CPU *cpu, int tact0) { // ld (nnnn),de
    Z80_T_BEGIN(cpu);
    uint16_t pc = cpu->pc;

    // Read 2 bytes of address from memory
    uint16_t adr = Z80Rd(cpu, pc++, true);
    adr += Z80Rd(cpu, pc++, true) * 0x100;

    cpu->memptr = adr + 1;

    // Write 2 bytes from DE to memory using fetched address
    Z80Wd(cpu, adr, cpu->e);
    Z80Wd(cpu, adr + 1, cpu->d);

    cpu->pc = pc;
    Z80_RETURN_Q(0, tact_);
}

#define ope_54 ope_44 // neg
#define ope_55 ope_45 // retn

Z80OPCODE ope_56(Z80CPU *cpu, int tact0) { // im 1
    cpu->im = 1;
    Z80_RETURN_Q(0, tact0);
}

Z80OPCODE ope_57(Z80CPU *cpu, int tact0) { // ld a,i
   Z80_T_BEGIN(cpu);
   cpu->a = cpu->i;
   cpu->f = (log_f[cpu->a] & ~(PV | HF | NF)) | (cpu->iff2 ? PV : 0) | (cpu->f & CF);
   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_58(Z80CPU *cpu, int tact0) { // in e,(c)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->e = Z80Pin(cpu, cpu->bc);
   cpu->f = log_f[cpu->e] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_59(Z80CPU *cpu, int tact0) { // out (c),e
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->e);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_5A(Z80CPU *cpu, int tact0) { // adc hl,de
    Z80_T_BEGIN(cpu);
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

    // P/V = overflow: operands same sign, result different sign
    if (((hl ^ de) & 0x8000) == 0 && ((hl ^ result) & 0x8000))
       flags |= PV;

    // Store result back to registers
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3|F5|SF));

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_5B(Z80CPU *cpu, int tact0) { // ld de,(nnnn)
    Z80_T_BEGIN(cpu);
    uint16_t pc = cpu->pc;

    uint16_t addr = Z80Rd(cpu, pc++, true);
    addr += Z80Rd(cpu, pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->e = Z80Rd(cpu, addr);
    cpu->d = Z80Rd(cpu, addr + 1);

    cpu->pc = pc;
    Z80_RETURN_Q(0, tact_);
}

#define ope_5C ope_44   // neg
#define ope_5D ope_4D   // reti

Z80OPCODE ope_5E(Z80CPU *cpu, int tact0) { // im 2
   cpu->im = 2;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE ope_5F(Z80CPU *cpu, int tact0) { // ld a,r
   Z80_T_BEGIN(cpu);
   cpu->a = (cpu->r_low & 0x7F) | cpu->r_hi;
   cpu->f = (log_f[cpu->a] & ~(PV | HF | NF)) | (cpu->iff2 ? PV : 0) | (cpu->f & CF);
   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_60(Z80CPU *cpu, int tact0) { // in h,(c)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->h = Z80Pin(cpu, cpu->bc);
   cpu->f = log_f[cpu->h] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_61(Z80CPU *cpu, int tact0) { // out (c),h
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->h);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_62(Z80CPU *cpu, int tact0) { // sbc hl,hl
   Z80_T_BEGIN(cpu);
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
   Z80_RETURN_Q(0x28, tact_);
}

#define ope_63 op_22 // ld (nnnn),hl
#define ope_64 ope_44 // neg
#define ope_65 ope_45 // retn
#define ope_66 ope_46 // im 0

Z80OPCODE ope_67(Z80CPU *cpu, int tact0) { // rrd
  Z80_T_BEGIN(cpu);
  uint8_t value = Z80Rd(cpu, cpu->hl);

  cpu->memptr = cpu->hl + 1;

  cputact(4);

  Z80Wd(cpu, cpu->hl, (cpu->a << 4) | (value >> 4));

  cpu->a = (cpu->a & 0xF0) | (value & 0x0F);
  cpu->f = log_f[cpu->a] | (cpu->f & CF);
  Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_68(Z80CPU *cpu, int tact0) { // in l,(c)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->l = Z80Pin(cpu, cpu->bc);
   cpu->f = log_f[cpu->l] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_69(Z80CPU *cpu, int tact0) { // out (c),l
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->l);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}
Z80OPCODE ope_6A(Z80CPU *cpu, int tact0) { // adc hl,hl
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->hl + 1;

   uint8_t flags = ((cpu->h << 1) & 0x10); // HF
   unsigned result = (cpu->hl & 0xFFFF) * 2 + (cpu->af & CF);

   if (result & 0x10000)
       flags |= CF;
   if (!(result & 0xFFFF))
       flags |= ZF;

   // P/V = overflow: signed result outside 16-bit signed range
   int32_t signedResult = 2 * (int16_t)(cpu->hl & 0xFFFF) + (cpu->af & CF);
   if (signedResult < -0x8000 || signedResult >= 0x8000)
       flags |= PV;

   cpu->hl = result & 0xFFFF;
   cpu->f = flags | (cpu->h & (F3 | F5 | SF));

   cputact(7);
   Z80_RETURN_Q(0x28, tact_);
}

#define ope_6B op_2A // ld hl,(nnnn)
#define ope_6C ope_44   // neg
#define ope_6D ope_4D   // reti
#define ope_6E ope_46   // im 0 (undocumented "IM 0/1" acts as IM 0; z80.info / FUSE)

Z80OPCODE ope_6F(Z80CPU *cpu, int tact0) { // rld
  Z80_T_BEGIN(cpu);
  uint8_t value = Z80Rd(cpu, cpu->hl);

  cpu->memptr = cpu->hl + 1;

  cputact(4);

  Z80Wd(cpu, cpu->hl, (cpu->a & 0x0F) | (value << 4));

  cpu->a = (cpu->a & 0xF0) | (value >> 4);
  cpu->f = log_f[cpu->a] | (cpu->f & CF);
  Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_70(Z80CPU *cpu, int tact0) { // in (c) - Undocumented. Reads from the port and affects flags, but does not store the value to a register
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->f = log_f[Z80Pin(cpu, cpu->bc)] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_71(Z80CPU *cpu, int tact0) { // out (c),0 - Undocumented. Writes zero to the port
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->outc0);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_72(Z80CPU *cpu, int tact0) { // sbc hl,sp
    Z80_T_BEGIN(cpu);
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

    // P/V = overflow: operands different sign, result sign differs from minuend
    if (((hl ^ sp) & 0x8000) && ((hl ^ result) & 0x8000))
        flags |= PV;

    // Store result back to regusters
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_73(Z80CPU *cpu, int tact0) { // ld (nnnn),sp
    Z80_T_BEGIN(cpu);
    uint16_t pc = cpu->pc;

    uint16_t addr = Z80Rd(cpu, pc++, true);
    addr += Z80Rd(cpu, pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    Z80Wd(cpu, addr, cpu->spl);
    Z80Wd(cpu, addr + 1, cpu->sph);

    cpu->pc = pc;
    Z80_RETURN_Q(0, tact_);
}

#define ope_74 ope_44 // neg
#define ope_75 ope_45 // retn

Z80OPCODE ope_76(Z80CPU *cpu, int tact0) { // im 1
   cpu->im = 1;
   Z80_RETURN_Q(0, tact0);
}

#define ope_77 op_00  // nop

Z80OPCODE ope_78(Z80CPU *cpu, int tact0) { // in a,(c)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   cpu->a = Z80Pin(cpu, cpu->bc);
   cpu->f = log_f[cpu->a] | (cpu->f & CF);

   cputact(3);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_79(Z80CPU *cpu, int tact0) { // out (c),a
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;

   cputact(1);
   Z80Pout(cpu, cpu->bc, cpu->a);

   cputact(3);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE ope_7A(Z80CPU *cpu, int tact0) { // adc hl,sp
    Z80_T_BEGIN(cpu);
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

    // P/V = overflow: operands same sign, result different sign
    if (((hl ^ sp) & 0x8000) == 0 && ((hl ^ result) & 0x8000))
       flags |= PV;

    // Store result back to registers
    cpu->hl = result & 0xFFFF;
    cpu->f = flags | (cpu->h & (F3 | F5 | SF));

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_7B(Z80CPU *cpu, int tact0) { // ld sp,(nnnn)
    Z80_T_BEGIN(cpu);
    uint16_t pc = cpu->pc;

    uint16_t addr = Z80Rd(cpu, pc++, true);
    addr += Z80Rd(cpu, pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->spl = Z80Rd(cpu, addr);
    cpu->sph = Z80Rd(cpu, addr + 1);

    cpu->pc = pc;
    Z80_RETURN_Q(0, tact_);
}

#define ope_7C ope_44   // neg
#define ope_7D ope_4D   // reti
#define ope_7E ope_5E   // im 2
#define ope_7F op_00    // nop

Z80OPCODE ope_A0(Z80CPU *cpu, int tact0) { // ldi
    Z80_T_BEGIN(cpu);
    uint8_t value = Z80Rd(cpu, cpu->hl++);

    Z80Wd(cpu, cpu->de++, value);

    value += cpu->a;
    value = (value & F3) + ((value << 4) & F5);

    cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--cpu->bc)
        cpu->f |= PV;

    cputact(2);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_A1(Z80CPU *cpu, int tact0) { // cpi
   Z80_T_BEGIN(cpu);
   uint8_t cf = cpu->f & CF;

   uint8_t value = Z80Rd(cpu, cpu->hl++);

   cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

   if (--cpu->bc)
	   cpu->f |= PV;

   cpu->memptr++;

   cputact(5);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_A2(Z80CPU *cpu, int tact0) { // ini
    Z80_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order (Xpeccy "5 4in 3wr" / ZXMAK2): M1 stall T, IO read, memory write.
    // 1T stall + 1T to reach IORQ at T2 of the IO cycle (see op_D3), then the
    // remaining 3T of the IO cycle before the 3T memory write.
    cpu->memptr = cpu->bc + 1;

    cputact(2);
    uint16_t hl = cpu->hl;
    uint8_t value = Z80Pin(cpu, cpu->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80Wd(cpu, hl++, value);

    // Decrement B
    cpu->b--;
    uint8_t b_out = cpu->b;

    // T = M + ((C + 1) & 0xFF) for INI
    uint16_t t = value + ((cpu->c + 1) & 0xFF);

    // Flags based on Xpeccy
    cpu->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) cpu->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) cpu->f |= NF; // NF = M.7
    if (t > 255) cpu->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) cpu->f |= PV;

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_A3(Z80CPU *cpu, int tact0) { // outi
    Z80_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order (Xpeccy "5 3rd 4wr" / ZXMAK2): M1 stall T, memory read, IO write.
    // IORQ asserts at T2 of the IO cycle (see op_D3).
    cputact(1);

    uint16_t hl = cpu->hl;
    uint8_t value = Z80Rd(cpu, hl);  // M = memory value

    // Decrement B first
    cpu->b--;
    uint8_t b_out = cpu->b;

    cpu->memptr = cpu->bc + 1;

    cputact(1);
    Z80Pout(cpu, cpu->bc, value);
    cputact(3);
    hl++;

    // T = M + L (output value of L)
    uint16_t t = value + (hl & 0xFF);

    // Flags based on Xpeccy
    cpu->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) cpu->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) cpu->f |= NF; // NF = M.7
    if (t > 255) cpu->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) cpu->f |= PV;

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_A8(Z80CPU *cpu, int tact0) { // ldd
    Z80_T_BEGIN(cpu);
    uint16_t hl = cpu->hl;
    uint16_t de = cpu->de;

    uint8_t value = Z80Rd(cpu, hl--);

    Z80Wd(cpu, de--, value);

    value += cpu->a;
    value = (value & F3) + ((value << 4) & F5);

    cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--cpu->bc)
        cpu->f |= PV;

    cpu->hl = hl;
    cpu->de = de;

    cputact(2);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_A9(Z80CPU *cpu, int tact0) { // cpd
    Z80_T_BEGIN(cpu);
    uint8_t cf = cpu->f & CF;

    uint16_t hl = cpu->hl;
    uint8_t value = Z80Rd(cpu, hl--);

    cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

    if (--cpu->bc)
        cpu->f |= PV;

    cpu->hl = hl;

    cpu->memptr--;

    cputact(5);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_AA(Z80CPU *cpu, int tact0) { // ind
    Z80_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order and IO phase: see ope_A2 (ini)
    cpu->memptr = cpu->bc - 1;

    cputact(2);
    uint16_t hl = cpu->hl;
    uint8_t value = Z80Pin(cpu, cpu->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80Wd(cpu, hl--, value);

    // Decrement B
    cpu->b--;
    uint8_t b_out = cpu->b;

    // T = M + ((C - 1) & 0xFF) for IND
    uint16_t t = value + ((cpu->c - 1) & 0xFF);

    // Flags based on Xpeccy
    cpu->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) cpu->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) cpu->f |= NF; // NF = M.7
    if (t > 255) cpu->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) cpu->f |= PV;

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_AB(Z80CPU *cpu, int tact0) { // outd
    Z80_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order and IO phase: see ope_A3 (outi)
    cputact(1);

    uint16_t hl = cpu->hl;
    uint8_t value = Z80Rd(cpu, hl);  // M = memory value

    // Decrement B first
    cpu->b--;
    uint8_t b_out = cpu->b;

    cpu->memptr = cpu->bc - 1;

    cputact(1);
    Z80Pout(cpu, cpu->bc, value);
    cputact(3);
    hl--;

    // T = M + L (output value of L)
    uint16_t t = value + (hl & 0xFF);

    // Flags based on Xpeccy
    cpu->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) cpu->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) cpu->f |= NF; // NF = M.7
    if (t > 255) cpu->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) cpu->f |= PV;

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_B0(Z80CPU *cpu, int tact0) { // ldir
    Z80_T_BEGIN(cpu);
    uint16_t hl = cpu->hl;
    uint16_t de = cpu->de;

	uint8_t value = Z80Rd(cpu, hl++);

	Z80Wd(cpu, de++, value);

    value += cpu->a;
    value = (value & F3) + ((value << 4) & F5);

	cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

	if (--cpu->bc)
	{
		cpu->f |= PV;
		cpu->pc = (cpu->pc - 2) & 0xFFFF;

		// Interrupted block instruction: YF=PC.13, XF=PC.11
		cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));

		cputact(7);

		cpu->memptr = cpu->pc + 1;
	}
	else
	{
		cputact(2);
	}

	cpu->hl = hl;
	cpu->de = de;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_B1(Z80CPU *cpu, int tact0) { // cpir
   Z80_T_BEGIN(cpu);
   cpu->memptr++;

   uint8_t cf = cpu->f & CF;

   uint16_t hl = cpu->hl;
   uint8_t value = Z80Rd(cpu, hl++);

   cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

   // Set PV flag and XF/YF from PC BEFORE any cputact calls
   // so interrupts occurring during cycles see correct flags
   if (--cpu->bc)
   {
      cpu->f |= PV;

      if (!(cpu->f & ZF))
      {
          cpu->pc = (cpu->pc - 2) & 0xFFFF;

          // Interrupted block instruction: YF=PC.13, XF=PC.11
          cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));

          cpu->memptr = cpu->pc + 1;

          cputact(10); // 5 + 5 cycles for repeat
      }
      else
      {
          cputact(5);
      }
   }
   else
   {
      cputact(5);
   }

   cpu->hl = hl;
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_B2(Z80CPU *cpu, int tact0) { // inir
    Z80_T_BEGIN(cpu);
    // Based on David Banks' research
    // For INIR: T = M + ((C + 1) & 0xFF)
    cpu->memptr = cpu->bc + 1;

    // Cycle order and IO phase: see ope_A2 (ini)
    cputact(2);
    uint16_t hl = cpu->hl;
    uint8_t value = Z80Pin(cpu, cpu->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80Wd(cpu, hl++, value);

    // Decrement B
    uint8_t b_out = cpu->b - 1;  // Bo = output value of B
    cpu->b = b_out;

    // T = M + ((C + 1) & 0xFF) for INIR
    uint16_t t = value + ((cpu->c + 1) & 0xFF);

    // NF = M.7, CF = T > 255
    uint8_t nf = (value & 0x80) ? NF : 0;
    uint8_t cf = (t > 255) ? CF : 0;

    if (b_out)  // B != 0
    {
        uint8_t sf = (b_out & 0x80) ? SF : 0;
        uint8_t hf = 0;
        uint8_t pf;

        if (cf)
        {
            if (value & 0x80)  // M.7 = 1
            {
                uint8_t balu = b_out - 1;
                hf = ((b_out & 0x0F) == 0) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
            else
            {
                uint8_t balu = b_out + 1;
                hf = ((b_out & 0x0F) == 0x0F) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
        }
        else
        {
            hf = 0;
            pf = ((t & 7) ^ b_out ^ (b_out & 7));
        }

        pf ^= pf >> 4;
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        cpu->f = sf | pf | hf | nf | cf;
        cpu->pc = (cpu->pc - 2) & 0xFFFF;
        // Repeat path: like LDIR/CPIR, WZ holds the jump-back address + 1
        // during the 5 extra T (z80test z80memptr INIR/INDR->NOP', which the
        // BC-derived value fails; verified empirically against its CRCs).
        cpu->memptr = cpu->pc + 1;
        cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));

        cputact(5);
    }
    else
    {
        uint8_t hf = cf ? HF : 0;
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        cpu->f = ZF | pf | hf | nf | cf;
    }

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_B3(Z80CPU *cpu, int tact0) { // otir
    Z80_T_BEGIN(cpu);
    // Based on David Banks' research:
    // https://github.com/hoglet67/Z80Decoder/wiki/Undocumented-Flags
    cputact(1);

    // Decrement B first (before output)
    uint8_t b_out = cpu->b - 1;  // Bo = output value of B
    cpu->b = b_out;

    uint16_t hl = cpu->hl;
    uint8_t value = Z80Rd(cpu, hl++);  // M = memory value
    uint8_t l_out = hl & 0xFF;      // Lo = output value of L

    cputact(1);
    Z80Pout(cpu, cpu->bc, value);
    cputact(3);

    // T = M + Lo (for OTIR/OTDR)
    uint16_t t = value + l_out;

    // Calculate flags according to David Banks
    // NF = M.7
    uint8_t nf = (value & 0x80) ? NF : 0;
    // CF = T > 255
    uint8_t cf = (t > 255) ? CF : 0;

    if (b_out)  // B != 0, instruction will repeat
    {
        // ZF = 0, SF = Bo.7
        uint8_t sf = (b_out & 0x80) ? SF : 0;

        // Calculate HF and PF based on CF and M.7
        uint8_t hf = 0;
        uint8_t pf;

        if (cf)
        {
            if (value & 0x80)  // M.7 = 1
            {
                // Balu = Bo - 1
                uint8_t balu = b_out - 1;
                hf = ((b_out & 0x0F) == 0) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
            else  // M.7 = 0
            {
                // Balu = Bo + 1
                uint8_t balu = b_out + 1;
                hf = ((b_out & 0x0F) == 0x0F) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
        }
        else
        {
            // Balu = Bo (no adjustment when CF=0)
            hf = 0;
            pf = ((t & 7) ^ b_out ^ (b_out & 7));
        }

        // Calculate parity of pf
        pf ^= pf >> 4;
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;  // Even parity = PV set

        cpu->f = sf | pf | hf | nf | cf;

        // Set PC back to instruction for repeat
        cpu->pc = (cpu->pc - 2) & 0xFFFF;

        // Interrupted block instruction: YF=PC.13, XF=PC.11
        cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));

        // Repeat path: WZ = jump-back address + 1 (shared block-repeat
        // mechanism; verified for LDIR/CPIR/INIR/INDR, inferred for OTIR/
        // OTDR - z80test has no vector for it; memptr_eng's "BC after
        // decrement + 1" describes the final, non-repeating iteration)
        cpu->memptr = cpu->pc + 1;

        cputact(5);
    }
    else  // B == 0, single iteration flags
    {
        cpu->memptr = cpu->bc + 1;  // final iteration: BC after decrementing B + 1
        // SF = 0, ZF = 1, HF = CF
        uint8_t hf = cf ? HF : 0;

        // PF = ((T & 7) ^ Bo).parity (Bo is 0 here)
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        cpu->f = ZF | pf | hf | nf | cf;
    }

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_B8(Z80CPU *cpu, int tact0) { // lddr
    Z80_T_BEGIN(cpu);
    uint16_t hl = cpu->hl;
    uint16_t de = cpu->de;

    uint8_t value = Z80Rd(cpu, hl--);

    Z80Wd(cpu, de--, value);

    value += cpu->a; value = (value & F3) + ((value << 4) & F5);

    cpu->f = (cpu->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--cpu->bc)
    {
       cpu->f |= PV;
       cpu->pc = (cpu->pc - 2) & 0xFFFF;

       // Interrupted block instruction: YF=PC.13, XF=PC.11
       cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));

       // Repeating block op: MEMPTR = PC+1 (FUSE-verified, same as LDIR)
       cpu->memptr = cpu->pc + 1;

       cputact(7);
    }
    else
    {
       cputact(2);
    }

    cpu->hl = hl;
    cpu->de = de;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_B9(Z80CPU *cpu, int tact0) { // cpdr
   Z80_T_BEGIN(cpu);
   cpu->memptr--;

   uint8_t cf = cpu->f & CF;

   uint16_t hl = cpu->hl;
   uint8_t value = Z80Rd(cpu, hl--);

   cpu->f = cpf8b[cpu->a * 0x100 + value] + cf;

   // Set PV flag and XF/YF from PC BEFORE any cputact calls
   // so interrupts occurring during cycles see correct flags
   if (--cpu->bc)
   {
      cpu->f |= PV;

      if (!(cpu->f & ZF))
      {
          cpu->pc = (cpu->pc - 2) & 0xFFFF;

          // Interrupted block instruction: YF=PC.13, XF=PC.11
          cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));

          cpu->memptr = cpu->pc + 1;

          cputact(10); // 5 + 5 cycles for repeat
      }
      else
      {
          cputact(5);
      }
   }
   else
   {
      cputact(5);
   }

   cpu->hl = hl;
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_BA(Z80CPU *cpu, int tact0) { // indr
    Z80_T_BEGIN(cpu);
    // Based on David Banks' research
    // For INDR: T = M + ((C - 1) & 0xFF)
    cpu->memptr = cpu->bc - 1;

    // Cycle order and IO phase: see ope_A2 (ini)
    cputact(2);
    uint16_t hl = cpu->hl;
    uint8_t value = Z80Pin(cpu, cpu->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80Wd(cpu, hl--, value);

    // Decrement B
    uint8_t b_out = cpu->b - 1;  // Bo = output value of B
    cpu->b = b_out;

    // T = M + ((C - 1) & 0xFF) for INDR
    uint16_t t = value + ((cpu->c - 1) & 0xFF);

    // NF = M.7, CF = T > 255
    uint8_t nf = (value & 0x80) ? NF : 0;
    uint8_t cf = (t > 255) ? CF : 0;

    if (b_out)  // B != 0
    {
        uint8_t sf = (b_out & 0x80) ? SF : 0;
        uint8_t hf = 0;
        uint8_t pf;

        if (cf)
        {
            if (value & 0x80)  // M.7 = 1
            {
                uint8_t balu = b_out - 1;
                hf = ((b_out & 0x0F) == 0) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
            else
            {
                uint8_t balu = b_out + 1;
                hf = ((b_out & 0x0F) == 0x0F) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
        }
        else
        {
            hf = 0;
            pf = ((t & 7) ^ b_out ^ (b_out & 7));
        }

        pf ^= pf >> 4;
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        cpu->f = sf | pf | hf | nf | cf;
        cpu->pc = (cpu->pc - 2) & 0xFFFF;
        // Repeat path: like LDIR/CPIR, WZ holds the jump-back address + 1
        // during the 5 extra T (z80test z80memptr INIR/INDR->NOP', which the
        // BC-derived value fails; verified empirically against its CRCs).
        cpu->memptr = cpu->pc + 1;
        cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));

        cputact(5);
    }
    else
    {
        uint8_t hf = cf ? HF : 0;
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        cpu->f = ZF | pf | hf | nf | cf;

    }

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE ope_BB(Z80CPU *cpu, int tact0) { // otdr
    Z80_T_BEGIN(cpu);
    // Based on David Banks' research
    cputact(1);

    // Decrement B first (before output)
    uint8_t b_out = cpu->b - 1;  // Bo = output value of B
    cpu->b = b_out;

    uint16_t hl = cpu->hl;
    uint8_t value = Z80Rd(cpu, hl--);  // M = memory value
    uint8_t l_out = hl & 0xFF;      // Lo = output value of L

    cputact(1);
    Z80Pout(cpu, cpu->bc, value);
    cputact(3);

    // T = M + Lo (for OTIR/OTDR)
    uint16_t t = value + l_out;

    // NF = M.7, CF = T > 255
    uint8_t nf = (value & 0x80) ? NF : 0;
    uint8_t cf = (t > 255) ? CF : 0;

    if (b_out)  // B != 0
    {
        uint8_t sf = (b_out & 0x80) ? SF : 0;
        uint8_t hf = 0;
        uint8_t pf;

        if (cf)
        {
            if (value & 0x80)  // M.7 = 1
            {
                uint8_t balu = b_out - 1;
                hf = ((b_out & 0x0F) == 0) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
            else
            {
                uint8_t balu = b_out + 1;
                hf = ((b_out & 0x0F) == 0x0F) ? HF : 0;
                pf = ((t & 7) ^ b_out ^ (balu & 7));
            }
        }
        else
        {
            hf = 0;
            pf = ((t & 7) ^ b_out ^ (b_out & 7));
        }

        pf ^= pf >> 4;
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        cpu->f = sf | pf | hf | nf | cf;
        cpu->pc = (cpu->pc - 2) & 0xFFFF;
        cpu->f = (cpu->f & ~(F3|F5)) | ((cpu->pc >> 8) & (F3|F5));
        cpu->memptr = cpu->pc + 1;  // repeat path, see ope_B3

        cputact(5);
    }
    else
    {
        uint8_t hf = cf ? HF : 0;
        cpu->memptr = cpu->bc - 1;  // final iteration: BC after decrementing B - 1
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        cpu->f = ZF | pf | hf | nf | cf;
    }

    cpu->hl = hl;
    Z80_RETURN_Q(0x28, tact_);
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
Z80OPCODE op_ED(Z80CPU *cpu, int tact0)
{
	int tact_ = tact0;
	uint8_t opcode = Z80M1(cpu);
    cpu->prefix = 0xED;  // after the M1 (the opword store cleared it)

	// Tail-dispatch the ED-prefixed opcode (cpu->opcode already holds it
	// from the M1 fetch; the callee runs the shared epilogue).
	return (ext_opcode[opcode])(cpu, tact_);
}

