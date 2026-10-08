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

Z80NOPCODE ope_40(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in b,(c)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->b = Z80nPin(cpu, rf->bc);
   rf->f = log_f[rf->b] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_41(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),b
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, rf->b);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_42(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc hl,bc
    Z80N_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    int32_t hl = rf->hl;
    int32_t bc = rf->bc;

    int32_t halfHL = hl & 0x0FFF;  // Lower 12 bits for half-carry
    int32_t halfBC = bc & 0x0FFF;

    uint8_t flags = NF;
    flags |= ((halfHL - halfBC - (rf->af & CF)) >> 8) & 0x10; // HF

    uint32_t result = hl - bc - (rf->af & CF);
    if (result & 0x10000)
        flags |= CF;
    if (!(result & 0xFFFF))
        flags |= ZF;
    // P/V = overflow: operands different sign, result sign differs from minuend
    if (((hl ^ bc) & 0x8000) && ((hl ^ result) & 0x8000))
        flags |= PV;

    rf->hl = result & 0xFFFF;
    rf->f = flags | (rf->h & (F3 | F5 | SF));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_43(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (nnnn),bc
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z80nRd(cpu, pc++, true);
    addr += Z80nRd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    Z80nWd(cpu, addr, rf->c);
    Z80nWd(cpu, addr + 1, rf->b);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_44(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // neg
   rf->f = sbc_f[rf->a];
   rf->a = -rf->a;
   Z80N_RETURN_Q(0x28, tact0);
}

// RETN/RETI copy IFF2 to IFF1 too late for the INT sampling at the next
// boundary: when that copy sets IFF1 (only possible inside an NMI handler,
// IFF1 = 0 and IFF2 = 1), INT is refused there, like after EI (Weissflog
// 2021, Sainz de Baranda 2022; redcode Z80 and Visual Z80 agree).
Z80NINLINE void Z80nRetxIff(Z80nCPU *cpu, Z80nRegs *rf)
{
   if (!rf->iff1 && rf->iff2)
      cpu->opword = Z80N_OPWORD_INT_SHADOW;
   rf->iff1 = rf->iff2;
}

Z80NOPCODE ope_45(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // retn
   Z80N_T_BEGIN(cpu);
   Z80nRetxIff(cpu, rf);

   uint16_t sp = rf->sp;
   uint16_t addr;

   if (cpu->stacklessRetn && cpu->nmiLoad)
   {
       // Stackless NMI: the two pop cycles carry no memory access (3 T each, no MREQ); the address is the host's
       cputact(6);
       addr = cpu->nmiLoad(cpu, cpu->nmiData);
       sp = static_cast<uint16_t>(sp + 2);
       cpu->stacklessRetn = 0;
   }
   else
   {
       addr = Z80nRd(cpu, sp++);
       addr += 0x100 * Z80nRd(cpu, sp++);
   }


   rf->pc = addr;
   rf->memptr = addr;

   rf->sp = sp;

   // TODO: Is callback still needed?
   cpu->retn();

   // RETN notification (Z80nCpuSetRetnFn), same shape as the RETI hook in
   // ope_4D: publish T first so the host observes the exact current T.
   // Aliased RETN opcodes (ED 55/65/75) share this body.
   Z80N_T_PUBLISH(cpu);
   if (cpu->retnCallback)
       cpu->retnCallback(cpu, cpu->retnData);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_46(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // im 0
   rf->im = 0;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE ope_47(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld i,a
   Z80N_T_BEGIN(cpu);
   cpuidle(Z80N_IR(rf), 1);  // the old IR is on the bus in the extra T (FUSE)

   rf->i = rf->a;
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_48(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in c,(c)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->c = Z80nPin(cpu, rf->bc);
   rf->f = log_f[rf->c] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_49(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),c
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, rf->c);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_4A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc hl,bc
    Z80N_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    int32_t hl = rf->hl & 0xFFFF;
    int32_t bc = rf->bc & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfBC = bc & 0x0FFF;

    // Calculate half-carry flag (HF)
    uint8_t flags = ((halfHL + halfBC + (rf->f & CF)) >> 8) & 0x10; // HF
    uint32_t result = hl + bc + (rf->f & CF);

    if (result & 0x10000)
        flags |= CF;
    if (!(result & 0xFFFF))
        flags |= ZF;

    // P/V = overflow: operands same sign, result different sign
    if (((hl ^ bc) & 0x8000) == 0 && ((hl ^ result) & 0x8000))
        flags |= PV;

    // Store result back to registers
    rf->hl = result & 0xFFFF;
    rf->f = flags | (rf->h & (F3|F5|SF));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_4B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld bc,(nnnn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z80nRd(cpu, pc++, true);
    addr += Z80nRd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->c = Z80nRd(cpu, addr);
    rf->b = Z80nRd(cpu, addr + 1);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

#define ope_4C ope_44   // neg

Z80NOPCODE ope_4D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // reti
    Z80N_T_BEGIN(cpu);
    Z80nRetxIff(cpu, rf);

    uint16_t sp = rf->sp;

    uint16_t addr = Z80nRd(cpu, sp++);
    addr += 0x100 * Z80nRd(cpu, sp++);


    rf->pc = addr;
    rf->memptr = addr;

    rf->sp = sp;

    // RETI notification (z80ex-style device hook), fired from the handler
    // itself as z80ex does - the step wrapper carries no per-step opcode
    // test for it. Aliased RETI opcodes (ED 55/5D/65/6D/7D) share this body.
    // Publish first: the hook is a host callback and must observe the exact
    // current T (unreal-z80's flat bus does not publish on its own).
    Z80N_T_PUBLISH(cpu);
    if (cpu->retiCallback)
        cpu->retiCallback(cpu, cpu->retiData);
    Z80N_RETURN_Q(0, tact_);
}

#define ope_4E ope_46  // im0 undocumented

Z80NOPCODE ope_4F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld r,a
   Z80N_T_BEGIN(cpu);
   cpuidle(Z80N_IR(rf), 1);  // the old IR is on the bus in the extra T (FUSE)

   rf->r_low = rf->a;
   rf->r_hi = rf->a & 0x80;
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_50(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in d,(c)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->d = Z80nPin(cpu, rf->bc);
   rf->f = log_f[rf->d] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_51(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),d
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, rf->d);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_52(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc hl,de
    Z80N_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    int32_t hl = rf->hl & 0xFFFF;
    int32_t de = rf->de & 0xFFFF;
    int32_t halfHL = hl & 0x0FFF;
    int32_t halfDE = de & 0x0FFF;
    int32_t carryAF = rf->af & CF;

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

    rf->hl = result & 0xFFFF;
    rf->f = flags | (rf->h & (F3 | F5 | SF));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_53(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (nnnn),de
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    // Read 2 bytes of address from memory
    uint16_t adr = Z80nRd(cpu, pc++, true);
    adr += Z80nRd(cpu, pc++, true) * 0x100;

    rf->memptr = adr + 1;

    // Write 2 bytes from DE to memory using fetched address
    Z80nWd(cpu, adr, rf->e);
    Z80nWd(cpu, adr + 1, rf->d);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

#define ope_54 ope_44 // neg
#define ope_55 ope_45 // retn

Z80NOPCODE ope_56(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // im 1
    rf->im = 1;
    Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE ope_57(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,i
   Z80N_T_BEGIN(cpu);
   rf->a = rf->i;
   rf->f = (log_f[rf->a] & ~(PV | HF | NF)) | (rf->iff2 ? PV : 0) | (rf->f & CF);
   cpuidle(Z80N_IR(rf), 1);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_58(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in e,(c)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->e = Z80nPin(cpu, rf->bc);
   rf->f = log_f[rf->e] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_59(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),e
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, rf->e);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_5A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc hl,de
    Z80N_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    int32_t hl = rf->hl & 0xFFFF;
    int32_t de = rf->de & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfDE = de & 0x0FFF;

    uint8_t flags = ((halfHL + halfDE + (rf->af & CF)) >> 8) & 0x10; /* HF */
    uint32_t result = hl + de + (rf->af & CF);

    if (result & 0x10000)
       flags |= CF;
    if (!(result & 0xFFFF))
       flags |= ZF;

    // P/V = overflow: operands same sign, result different sign
    if (((hl ^ de) & 0x8000) == 0 && ((hl ^ result) & 0x8000))
       flags |= PV;

    // Store result back to registers
    rf->hl = result & 0xFFFF;
    rf->f = flags | (rf->h & (F3|F5|SF));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_5B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld de,(nnnn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z80nRd(cpu, pc++, true);
    addr += Z80nRd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->e = Z80nRd(cpu, addr);
    rf->d = Z80nRd(cpu, addr + 1);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

#define ope_5C ope_44   // neg
#define ope_5D ope_45   // retn (the FPGA core: all seven ED x5 / x D aliases but 4D are RETN)

Z80NOPCODE ope_5E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // im 2
   rf->im = 2;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE ope_5F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,r
   Z80N_T_BEGIN(cpu);
   rf->a = (rf->r_low & 0x7F) | rf->r_hi;
   rf->f = (log_f[rf->a] & ~(PV | HF | NF)) | (rf->iff2 ? PV : 0) | (rf->f & CF);
   cpuidle(Z80N_IR(rf), 1);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_60(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in h,(c)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->h = Z80nPin(cpu, rf->bc);
   rf->f = log_f[rf->h] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_61(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),h
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, rf->h);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_62(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc hl,hl
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->hl + 1;

   uint8_t flags = NF;
    flags |= (rf->f & CF) << 4; // HF - copy from CF

   uint32_t result = 0 - (rf->af & CF);
   if (result & 0x10000)
       flags |= CF;
   if (!(result & 0xFFFF))
       flags |= ZF;

   // never set PV
   rf->hl = result & 0xFFFF;
   rf->f = flags | (rf->h & (F3 | F5 | SF));

   cpuidle(Z80N_IR(rf), 7);
   Z80N_RETURN_Q(0x28, tact_);
}

#define ope_63 op_22 // ld (nnnn),hl
#define ope_64 ope_44 // neg
#define ope_65 ope_45 // retn
#define ope_66 ope_46 // im 0

Z80NOPCODE ope_67(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rrd
  Z80N_T_BEGIN(cpu);
  uint8_t value = Z80nRd(cpu, rf->hl);

  rf->memptr = rf->hl + 1;

  cpuidle(rf->hl, 4);

  Z80nWd(cpu, rf->hl, (rf->a << 4) | (value >> 4));

  rf->a = (rf->a & 0xF0) | (value & 0x0F);
  rf->f = log_f[rf->a] | (rf->f & CF);
  Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_68(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in l,(c)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->l = Z80nPin(cpu, rf->bc);
   rf->f = log_f[rf->l] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_69(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),l
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, rf->l);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}
Z80NOPCODE ope_6A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc hl,hl
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->hl + 1;

   uint8_t flags = ((rf->h << 1) & 0x10); // HF
   unsigned result = (rf->hl & 0xFFFF) * 2 + (rf->af & CF);

   if (result & 0x10000)
       flags |= CF;
   if (!(result & 0xFFFF))
       flags |= ZF;

   // P/V = overflow: signed result outside 16-bit signed range
   int32_t signedResult = 2 * (int16_t)(rf->hl & 0xFFFF) + (rf->af & CF);
   if (signedResult < -0x8000 || signedResult >= 0x8000)
       flags |= PV;

   rf->hl = result & 0xFFFF;
   rf->f = flags | (rf->h & (F3 | F5 | SF));

   cpuidle(Z80N_IR(rf), 7);
   Z80N_RETURN_Q(0x28, tact_);
}

#define ope_6B op_2A // ld hl,(nnnn)
#define ope_6C ope_44   // neg
#define ope_6D ope_45   // retn
#define ope_6E ope_46   // im 0 (undocumented "IM 0/1" acts as IM 0; z80.info / FUSE)

Z80NOPCODE ope_6F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rld
  Z80N_T_BEGIN(cpu);
  uint8_t value = Z80nRd(cpu, rf->hl);

  rf->memptr = rf->hl + 1;

  cpuidle(rf->hl, 4);

  Z80nWd(cpu, rf->hl, (rf->a & 0x0F) | (value << 4));

  rf->a = (rf->a & 0xF0) | (value >> 4);
  rf->f = log_f[rf->a] | (rf->f & CF);
  Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_70(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in (c) - Undocumented. Reads from the port and affects flags, but does not store the value to a register
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->f = log_f[Z80nPin(cpu, rf->bc)] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_71(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),0 - Undocumented. Writes zero to the port
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, cpu->outc0);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_72(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc hl,sp
    Z80N_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    int32_t hl = rf->hl & 0xFFFF;
    int32_t sp = rf->sp & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfSP = sp & 0x0FFF;

    uint8_t flags = NF;
    flags |= ((halfHL - halfSP - (rf->af & CF)) >> 8) & 0x10; // HF

    uint32_t result = hl - sp - (rf->af & CF);

    if (result & 0x10000)
        flags |= CF;
    if (!(result & 0xFFFF))
        flags |= ZF;

    // P/V = overflow: operands different sign, result sign differs from minuend
    if (((hl ^ sp) & 0x8000) && ((hl ^ result) & 0x8000))
        flags |= PV;

    // Store result back to regusters
    rf->hl = result & 0xFFFF;
    rf->f = flags | (rf->h & (F3 | F5 | SF));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_73(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (nnnn),sp
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z80nRd(cpu, pc++, true);
    addr += Z80nRd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    Z80nWd(cpu, addr, rf->spl);
    Z80nWd(cpu, addr + 1, rf->sph);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

#define ope_74 ope_44 // neg
#define ope_75 ope_45 // retn

Z80NOPCODE ope_76(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // im 1
   rf->im = 1;
   Z80N_RETURN_Q(0, tact0);
}

#define ope_77 op_00  // nop

Z80NOPCODE ope_78(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in a,(c)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   rf->a = Z80nPin(cpu, rf->bc);
   rf->f = log_f[rf->a] | (rf->f & CF);

   cputact(3);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_79(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (c),a
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;

   cputact(1);
   Z80nPout(cpu, rf->bc, rf->a);

   cputact(3);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_7A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc hl,sp
    Z80N_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    int32_t hl = rf->hl & 0xFFFF;
    int32_t sp = rf->sp & 0xFFFF;

    int32_t halfHL = hl & 0x0FFF;
    int32_t halfSP = sp & 0x0FFF;

    uint8_t flags = ((halfHL + halfSP + (rf->af & CF)) >> 8) & 0x10; // HF

    uint32_t result = hl + sp + (rf->af & CF);

    if (result & 0x10000)
       flags |= CF;
    if (!(uint16_t)result)
       flags |= ZF;

    // P/V = overflow: operands same sign, result different sign
    if (((hl ^ sp) & 0x8000) == 0 && ((hl ^ result) & 0x8000))
       flags |= PV;

    // Store result back to registers
    rf->hl = result & 0xFFFF;
    rf->f = flags | (rf->h & (F3 | F5 | SF));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_7B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld sp,(nnnn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z80nRd(cpu, pc++, true);
    addr += Z80nRd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->spl = Z80nRd(cpu, addr);
    rf->sph = Z80nRd(cpu, addr + 1);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

#define ope_7C ope_44   // neg
#define ope_7D ope_45   // retn
#define ope_7E ope_5E   // im 2
#define ope_7F op_00    // nop

Z80NOPCODE ope_A0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ldi
    Z80N_T_BEGIN(cpu);
    uint8_t value = Z80nRd(cpu, rf->hl++);

    Z80nWd(cpu, rf->de++, value);

    value += rf->a;
    value = (value & F3) + ((value << 4) & F5);

    rf->f = (rf->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--rf->bc)
        rf->f |= PV;

    cpuidle(rf->de - 1, 2);  // on the write address
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_A1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cpi
   Z80N_T_BEGIN(cpu);
   uint8_t cf = rf->f & CF;

   uint8_t value = Z80nRd(cpu, rf->hl++);

   rf->f = cpf8b[rf->a * 0x100 + value] + cf;

   if (--rf->bc)
	   rf->f |= PV;

   rf->memptr++;

   cpuidle(rf->hl - 1, 5);  // on the read address
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_A2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ini
    Z80N_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order (Xpeccy "5 4in 3wr" / ZXMAK2): M1 stall T, IO read, memory write.
    // 1T stall + 1T to reach IORQ at T2 of the IO cycle (see op_D3), then the
    // remaining 3T of the IO cycle before the 3T memory write.
    rf->memptr = rf->bc + 1;

    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T
    cputact(1);
    uint16_t hl = rf->hl;
    uint8_t value = Z80nPin(cpu, rf->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80nWd(cpu, hl++, value);

    // Decrement B
    rf->b--;
    uint8_t b_out = rf->b;

    // T = M + ((C + 1) & 0xFF) for INI
    uint16_t t = value + ((rf->c + 1) & 0xFF);

    // Flags based on Xpeccy
    rf->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) rf->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) rf->f |= NF; // NF = M.7
    if (t > 255) rf->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) rf->f |= PV;

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_A3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // outi
    Z80N_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order (Xpeccy "5 3rd 4wr" / ZXMAK2): M1 stall T, memory read, IO write.
    // IORQ asserts at T2 of the IO cycle (see op_D3).
    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T

    uint16_t hl = rf->hl;
    uint8_t value = Z80nRd(cpu, hl);  // M = memory value

    // Decrement B first
    rf->b--;
    uint8_t b_out = rf->b;

    rf->memptr = rf->bc + 1;

    cputact(1);
    Z80nPout(cpu, rf->bc, value);
    cputact(3);
    hl++;

    // T = M + L (output value of L)
    uint16_t t = value + (hl & 0xFF);

    // Flags based on Xpeccy
    rf->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) rf->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) rf->f |= NF; // NF = M.7
    if (t > 255) rf->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) rf->f |= PV;

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_A8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ldd
    Z80N_T_BEGIN(cpu);
    uint16_t hl = rf->hl;
    uint16_t de = rf->de;

    uint8_t value = Z80nRd(cpu, hl--);

    Z80nWd(cpu, de--, value);

    value += rf->a;
    value = (value & F3) + ((value << 4) & F5);

    rf->f = (rf->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--rf->bc)
        rf->f |= PV;

    rf->hl = hl;
    rf->de = de;

    cpuidle(rf->de + 1, 2);  // on the write address
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_A9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cpd
    Z80N_T_BEGIN(cpu);
    uint8_t cf = rf->f & CF;

    uint16_t hl = rf->hl;
    uint8_t value = Z80nRd(cpu, hl--);

    rf->f = cpf8b[rf->a * 0x100 + value] + cf;

    if (--rf->bc)
        rf->f |= PV;

    rf->hl = hl;

    rf->memptr--;

    cpuidle(rf->hl + 1, 5);  // on the read address
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_AA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ind
    Z80N_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order and IO phase: see ope_A2 (ini)
    rf->memptr = rf->bc - 1;

    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T
    cputact(1);
    uint16_t hl = rf->hl;
    uint8_t value = Z80nPin(cpu, rf->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80nWd(cpu, hl--, value);

    // Decrement B
    rf->b--;
    uint8_t b_out = rf->b;

    // T = M + ((C - 1) & 0xFF) for IND
    uint16_t t = value + ((rf->c - 1) & 0xFF);

    // Flags based on Xpeccy
    rf->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) rf->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) rf->f |= NF; // NF = M.7
    if (t > 255) rf->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) rf->f |= PV;

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_AB(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // outd
    Z80N_T_BEGIN(cpu);
    // Based on Xpeccy emulator implementation
    // Cycle order and IO phase: see ope_A3 (outi)
    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T

    uint16_t hl = rf->hl;
    uint8_t value = Z80nRd(cpu, hl);  // M = memory value

    // Decrement B first
    rf->b--;
    uint8_t b_out = rf->b;

    rf->memptr = rf->bc - 1;

    cputact(1);
    Z80nPout(cpu, rf->bc, value);
    cputact(3);
    hl--;

    // T = M + L (output value of L)
    uint16_t t = value + (hl & 0xFF);

    // Flags based on Xpeccy
    rf->f = (b_out & (SF|F3|F5));  // SF, XF, YF from B
    if (!b_out) rf->f |= ZF;       // ZF = (B == 0)
    if (value & 0x80) rf->f |= NF; // NF = M.7
    if (t > 255) rf->f |= (CF|HF); // CF = HF = (T > 255)

    // PF = parity((T & 7) ^ B)
    uint8_t pf = (t & 7) ^ b_out;
    pf ^= pf >> 4;
    pf ^= pf >> 2;
    pf ^= pf >> 1;
    if (!(pf & 1)) rf->f |= PV;

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_B0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ldir
    Z80N_T_BEGIN(cpu);
    uint16_t hl = rf->hl;
    uint16_t de = rf->de;

	uint8_t value = Z80nRd(cpu, hl++);

	Z80nWd(cpu, de++, value);

    value += rf->a;
    value = (value & F3) + ((value << 4) & F5);

	rf->f = (rf->f & ~(NF|HF|PV|F3|F5)) + value;

	if (--rf->bc)
	{
		rf->f |= PV;
		rf->pc = (rf->pc - 2) & 0xFFFF;

		// Interrupted block instruction: YF=PC.13, XF=PC.11
		rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));

		cpuidle(de - 1, 7);  // 2 + 5 for the repeat, on the write address

		rf->memptr = rf->pc + 1;
	}
	else
	{
		cpuidle(de - 1, 2);
	}

	rf->hl = hl;
	rf->de = de;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_B1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cpir
   Z80N_T_BEGIN(cpu);
   rf->memptr++;

   uint8_t cf = rf->f & CF;

   uint16_t hl = rf->hl;
   uint8_t value = Z80nRd(cpu, hl++);

   rf->f = cpf8b[rf->a * 0x100 + value] + cf;

   // Set PV flag and XF/YF from PC BEFORE any cputact calls
   // so interrupts occurring during cycles see correct flags
   if (--rf->bc)
   {
      rf->f |= PV;

      if (!(rf->f & ZF))
      {
          rf->pc = (rf->pc - 2) & 0xFFFF;

          // Interrupted block instruction: YF=PC.13, XF=PC.11
          rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));

          rf->memptr = rf->pc + 1;

          cpuidle(hl - 1, 10);  // 5 + 5 for the repeat, on the read address
      }
      else
      {
          cpuidle(hl - 1, 5);
      }
   }
   else
   {
      cpuidle(hl - 1, 5);
   }

   rf->hl = hl;
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_B2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inir
    Z80N_T_BEGIN(cpu);
    // Based on David Banks' research
    // For INIR: T = M + ((C + 1) & 0xFF)
    rf->memptr = rf->bc + 1;

    // Cycle order and IO phase: see ope_A2 (ini)
    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T
    cputact(1);
    uint16_t hl = rf->hl;
    uint8_t value = Z80nPin(cpu, rf->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80nWd(cpu, hl++, value);

    // Decrement B
    uint8_t b_out = rf->b - 1;  // Bo = output value of B
    rf->b = b_out;

    // T = M + ((C + 1) & 0xFF) for INIR
    uint16_t t = value + ((rf->c + 1) & 0xFF);

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

        rf->f = sf | pf | hf | nf | cf;
        rf->pc = (rf->pc - 2) & 0xFFFF;
        // Repeat path: like LDIR/CPIR, WZ holds the jump-back address + 1
        // during the 5 extra T (z80test z80memptr INIR/INDR->NOP', which the
        // BC-derived value fails; verified empirically against its CRCs).
        rf->memptr = rf->pc + 1;
        rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));

        cpuidle(hl - 1, 5);  // repeat, on the write address
    }
    else
    {
        uint8_t hf = cf ? HF : 0;
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        rf->f = ZF | pf | hf | nf | cf;
    }

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_B3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // otir
    Z80N_T_BEGIN(cpu);
    // Based on David Banks' research:
    // https://github.com/hoglet67/Z80nDecoder/wiki/Undocumented-Flags
    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T

    // Decrement B first (before output)
    uint8_t b_out = rf->b - 1;  // Bo = output value of B
    rf->b = b_out;

    uint16_t hl = rf->hl;
    uint8_t value = Z80nRd(cpu, hl++);  // M = memory value
    uint8_t l_out = hl & 0xFF;      // Lo = output value of L

    cputact(1);
    Z80nPout(cpu, rf->bc, value);
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

        rf->f = sf | pf | hf | nf | cf;

        // Set PC back to instruction for repeat
        rf->pc = (rf->pc - 2) & 0xFFFF;

        // Interrupted block instruction: YF=PC.13, XF=PC.11
        rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));

        // Repeat path: WZ = jump-back address + 1 (shared block-repeat
        // mechanism; verified for LDIR/CPIR/INIR/INDR, inferred for OTIR/
        // OTDR - z80test has no vector for it; memptr_eng's "BC after
        // decrement + 1" describes the final, non-repeating iteration)
        rf->memptr = rf->pc + 1;

        cpuidle(rf->bc, 5);  // repeat, on the port address
    }
    else  // B == 0, single iteration flags
    {
        rf->memptr = rf->bc + 1;  // final iteration: BC after decrementing B + 1
        // SF = 0, ZF = 1, HF = CF
        uint8_t hf = cf ? HF : 0;

        // PF = ((T & 7) ^ Bo).parity (Bo is 0 here)
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        rf->f = ZF | pf | hf | nf | cf;
    }

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_B8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // lddr
    Z80N_T_BEGIN(cpu);
    uint16_t hl = rf->hl;
    uint16_t de = rf->de;

    uint8_t value = Z80nRd(cpu, hl--);

    Z80nWd(cpu, de--, value);

    value += rf->a; value = (value & F3) + ((value << 4) & F5);

    rf->f = (rf->f & ~(NF|HF|PV|F3|F5)) + value;

    if (--rf->bc)
    {
       rf->f |= PV;
       rf->pc = (rf->pc - 2) & 0xFFFF;

       // Interrupted block instruction: YF=PC.13, XF=PC.11
       rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));

       // Repeating block op: MEMPTR = PC+1 (FUSE-verified, same as LDIR)
       rf->memptr = rf->pc + 1;

       cpuidle(de + 1, 7);  // 2 + 5 for the repeat, on the write address
    }
    else
    {
       cpuidle(de + 1, 2);
    }

    rf->hl = hl;
    rf->de = de;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_B9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cpdr
   Z80N_T_BEGIN(cpu);
   rf->memptr--;

   uint8_t cf = rf->f & CF;

   uint16_t hl = rf->hl;
   uint8_t value = Z80nRd(cpu, hl--);

   rf->f = cpf8b[rf->a * 0x100 + value] + cf;

   // Set PV flag and XF/YF from PC BEFORE any cputact calls
   // so interrupts occurring during cycles see correct flags
   if (--rf->bc)
   {
      rf->f |= PV;

      if (!(rf->f & ZF))
      {
          rf->pc = (rf->pc - 2) & 0xFFFF;

          // Interrupted block instruction: YF=PC.13, XF=PC.11
          rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));

          rf->memptr = rf->pc + 1;

          cpuidle(hl + 1, 10);  // 5 + 5 for the repeat, on the read address
      }
      else
      {
          cpuidle(hl + 1, 5);
      }
   }
   else
   {
      cpuidle(hl + 1, 5);
   }

   rf->hl = hl;
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_BA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // indr
    Z80N_T_BEGIN(cpu);
    // Based on David Banks' research
    // For INDR: T = M + ((C - 1) & 0xFF)
    rf->memptr = rf->bc - 1;

    // Cycle order and IO phase: see ope_A2 (ini)
    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T
    cputact(1);
    uint16_t hl = rf->hl;
    uint8_t value = Z80nPin(cpu, rf->bc);  // M = port value (IORQ at T2)
    cputact(3);
    Z80nWd(cpu, hl--, value);

    // Decrement B
    uint8_t b_out = rf->b - 1;  // Bo = output value of B
    rf->b = b_out;

    // T = M + ((C - 1) & 0xFF) for INDR
    uint16_t t = value + ((rf->c - 1) & 0xFF);

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

        rf->f = sf | pf | hf | nf | cf;
        rf->pc = (rf->pc - 2) & 0xFFFF;
        // Repeat path: like LDIR/CPIR, WZ holds the jump-back address + 1
        // during the 5 extra T (z80test z80memptr INIR/INDR->NOP', which the
        // BC-derived value fails; verified empirically against its CRCs).
        rf->memptr = rf->pc + 1;
        rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));

        cpuidle(hl + 1, 5);  // repeat, on the write address
    }
    else
    {
        uint8_t hf = cf ? HF : 0;
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        rf->f = ZF | pf | hf | nf | cf;

    }

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_BB(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // otdr
    Z80N_T_BEGIN(cpu);
    // Based on David Banks' research
    cpuidle(Z80N_IR(rf), 1);  // the M1 stall T

    // Decrement B first (before output)
    uint8_t b_out = rf->b - 1;  // Bo = output value of B
    rf->b = b_out;

    uint16_t hl = rf->hl;
    uint8_t value = Z80nRd(cpu, hl--);  // M = memory value
    uint8_t l_out = hl & 0xFF;      // Lo = output value of L

    cputact(1);
    Z80nPout(cpu, rf->bc, value);
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

        rf->f = sf | pf | hf | nf | cf;
        rf->pc = (rf->pc - 2) & 0xFFFF;
        rf->f = (rf->f & ~(F3|F5)) | ((rf->pc >> 8) & (F3|F5));
        rf->memptr = rf->pc + 1;  // repeat path, see ope_B3

        cpuidle(rf->bc, 5);  // repeat, on the port address
    }
    else
    {
        uint8_t hf = cf ? HF : 0;
        rf->memptr = rf->bc - 1;  // final iteration: BC after decrementing B - 1
        uint8_t pf = (t & 7);
        pf ^= pf >> 2;
        pf ^= pf >> 1;
        pf = (pf & 1) ? 0 : PV;

        rf->f = ZF | pf | hf | nf | cf;
    }

    rf->hl = hl;
    Z80N_RETURN_Q(0x28, tact_);
}


STEPFUNC const ext_opcode[0x100] =
{
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   op_00, op_00, op_00, ope_23, ope_24, op_00, op_00, ope_27,
   ope_28, ope_29, ope_2A, ope_2B, ope_2C, op_00, op_00, op_00,
   ope_30, ope_31, ope_32, ope_33, ope_34, ope_35, ope_36, op_00,
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
   op_00, op_00, ope_8A, op_00, op_00, op_00, op_00, op_00,
   ope_90, ope_91, ope_92, ope_93, ope_94, ope_95, op_00, op_00,
   ope_98, op_00, op_00, op_00, op_00, op_00, op_00, op_00,
   ope_A0, ope_A1, ope_A2, ope_A3, ope_A4, ope_A5, op_00, op_00,
   ope_A8, ope_A9, ope_AA, ope_AB, ope_AC, op_00, op_00, op_00,
   ope_B0, ope_B1, ope_B2, ope_B3, ope_B4, op_00, ope_B6, ope_B7,
   ope_B8, ope_B9, ope_BA, ope_BB, ope_BC, op_00, op_00, op_00,

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
Z80NOPCODE op_ED(Z80nCPU *cpu, int tact0, Z80nRegs *rf)
{
	int tact_ = tact0;
	uint8_t opcode = Z80nM1(cpu);
    cpu->prefix = 0xED;  // after the M1 (the opword store cleared it)

	// Tail-dispatch the ED-prefixed opcode (cpu->opcode already holds it
	// from the M1 fetch; the callee runs the shared epilogue).
	return (ext_opcode[opcode])(cpu, tact_, rf);
}

