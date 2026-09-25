// opcodes-xxcb.cpp - DD/FD prefix dispatch and DDCB/FDCB bit opcodes,
// ported from core op_ddcb.cpp (single shared implementation for both
// index registers, unlike z80ex which ships duplicated ddcb/fdcb units).


// DDCB/FDCB opcodes
// Note: cpu.t and destination updated in step(), here process 'byte'


Z80LOGIC oplx_00(Z80CPU *cpu, uint8_t byte) { // rlc (ix+nn)
   cpu->f = rlc_f[byte];
   
   return rol[byte];
}

Z80LOGIC oplx_08(Z80CPU *cpu, uint8_t byte) { // rrc (ix+nn)
   cpu->f = rrc_f[byte];
   
   return ror[byte];
}

Z80LOGIC oplx_10(Z80CPU *cpu, uint8_t byte) { // rl (ix+nn)
   if (cpu->f & CF)
   {
      cpu->f = rl1[byte]; return (byte << 1) + 1;
   }
   else
   {
      cpu->f = rl0[byte]; return (byte << 1);
   }
}

Z80LOGIC oplx_18(Z80CPU *cpu, uint8_t byte) { // rr (ix+nn)
   if (cpu->f & CF)
   {
      cpu->f = rr1[byte]; return (byte >> 1) + 0x80;
   }
   else
   {
      cpu->f = rr0[byte]; return (byte >> 1);
   }
}

Z80LOGIC oplx_20(Z80CPU *cpu, uint8_t byte) { // sla (ix+nn)
   cpu->f = rl0[byte];
   
   return (byte << 1);
}

Z80LOGIC oplx_28(Z80CPU *cpu, uint8_t byte) { // sra (ix+nn)
   cpu->f = sra_f[byte];
   
   return (byte >> 1) + (byte & 0x80);
}

Z80LOGIC oplx_30(Z80CPU *cpu, uint8_t byte) { // sli (ix+nn)
   cpu->f = rl1[byte];
   
   return (byte << 1) + 1;
}

Z80LOGIC oplx_38(Z80CPU *cpu, uint8_t byte) { // srl (ix+nn)
   cpu->f = rr0[byte]; return (byte >> 1);
}

Z80LOGIC oplx_40(Z80CPU *cpu, uint8_t byte) { // bit 0,(ix+nn)
   bitmem(cpu, byte, 0); return byte;
}

Z80LOGIC oplx_48(Z80CPU *cpu, uint8_t byte) { // bit 1,(ix+nn)
   bitmem(cpu, byte, 1); return byte;
}

Z80LOGIC oplx_50(Z80CPU *cpu, uint8_t byte) { // bit 2,(ix+nn)
   bitmem(cpu, byte, 2); return byte;
}

Z80LOGIC oplx_58(Z80CPU *cpu, uint8_t byte) { // bit 3,(ix+nn)
   bitmem(cpu, byte, 3); return byte;
}

Z80LOGIC oplx_60(Z80CPU *cpu, uint8_t byte) { // bit 4,(ix+nn)
   bitmem(cpu, byte, 4); return byte;
}

Z80LOGIC oplx_68(Z80CPU *cpu, uint8_t byte) { // bit 5,(ix+nn)
   bitmem(cpu, byte, 5); return byte;
}

Z80LOGIC oplx_70(Z80CPU *cpu, uint8_t byte) { // bit 6,(ix+nn)
   bitmem(cpu, byte, 6); return byte;
}

Z80LOGIC oplx_78(Z80CPU *cpu, uint8_t byte) { // bit 7,(ix+nn)
   bitmem(cpu, byte, 7); return byte;
}

Z80LOGIC oplx_80([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 0,(ix+nn)
   return resbyte(byte, 0);
}

Z80LOGIC oplx_88([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 1,(ix+nn)
   return resbyte(byte, 1);
}

Z80LOGIC oplx_90([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 2,(ix+nn)
   return resbyte(byte, 2);
}

Z80LOGIC oplx_98([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 3,(ix+nn)
   return resbyte(byte, 3);
}

Z80LOGIC oplx_A0([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 4,(ix+nn)
   return resbyte(byte, 4);
}

Z80LOGIC oplx_A8([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 5,(ix+nn)
   return resbyte(byte, 5);
}

Z80LOGIC oplx_B0([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 6,(ix+nn)
   return resbyte(byte, 6);
}

Z80LOGIC oplx_B8([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // res 7,(ix+nn)
   return resbyte(byte, 7);
}

Z80LOGIC oplx_C0([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 0,(ix+nn)
   return setbyte(byte, 0);
}

Z80LOGIC oplx_C8([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 1,(ix+nn)
   return setbyte(byte, 1);
}

Z80LOGIC oplx_D0([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 2,(ix+nn)
   return setbyte(byte, 2);
}

Z80LOGIC oplx_D8([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 3,(ix+nn)
   return setbyte(byte, 3);
}

Z80LOGIC oplx_E0([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 4,(ix+nn)
   return setbyte(byte, 4);
}

Z80LOGIC oplx_E8([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 5,(ix+nn)
   return setbyte(byte, 5);
}

Z80LOGIC oplx_F0([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 6,(ix+nn)
   return setbyte(byte, 6);
}

Z80LOGIC oplx_F8([[maybe_unused]] Z80CPU *cpu, uint8_t byte) { // set 7,(ix+nn)
   return setbyte(byte, 7);
}

LOGICFUNC const logic_ix_opcode[0x100] =
{
   oplx_00, oplx_00, oplx_00, oplx_00, oplx_00, oplx_00, oplx_00, oplx_00,
   oplx_08, oplx_08, oplx_08, oplx_08, oplx_08, oplx_08, oplx_08, oplx_08,
   oplx_10, oplx_10, oplx_10, oplx_10, oplx_10, oplx_10, oplx_10, oplx_10,
   oplx_18, oplx_18, oplx_18, oplx_18, oplx_18, oplx_18, oplx_18, oplx_18,
   oplx_20, oplx_20, oplx_20, oplx_20, oplx_20, oplx_20, oplx_20, oplx_20,
   oplx_28, oplx_28, oplx_28, oplx_28, oplx_28, oplx_28, oplx_28, oplx_28,
   oplx_30, oplx_30, oplx_30, oplx_30, oplx_30, oplx_30, oplx_30, oplx_30,
   oplx_38, oplx_38, oplx_38, oplx_38, oplx_38, oplx_38, oplx_38, oplx_38,

   oplx_40, oplx_40, oplx_40, oplx_40, oplx_40, oplx_40, oplx_40, oplx_40,
   oplx_48, oplx_48, oplx_48, oplx_48, oplx_48, oplx_48, oplx_48, oplx_48,
   oplx_50, oplx_50, oplx_50, oplx_50, oplx_50, oplx_50, oplx_50, oplx_50,
   oplx_58, oplx_58, oplx_58, oplx_58, oplx_58, oplx_58, oplx_58, oplx_58,
   oplx_60, oplx_60, oplx_60, oplx_60, oplx_60, oplx_60, oplx_60, oplx_60,
   oplx_68, oplx_68, oplx_68, oplx_68, oplx_68, oplx_68, oplx_68, oplx_68,
   oplx_70, oplx_70, oplx_70, oplx_70, oplx_70, oplx_70, oplx_70, oplx_70,
   oplx_78, oplx_78, oplx_78, oplx_78, oplx_78, oplx_78, oplx_78, oplx_78,

   oplx_80, oplx_80, oplx_80, oplx_80, oplx_80, oplx_80, oplx_80, oplx_80,
   oplx_88, oplx_88, oplx_88, oplx_88, oplx_88, oplx_88, oplx_88, oplx_88,
   oplx_90, oplx_90, oplx_90, oplx_90, oplx_90, oplx_90, oplx_90, oplx_90,
   oplx_98, oplx_98, oplx_98, oplx_98, oplx_98, oplx_98, oplx_98, oplx_98,
   oplx_A0, oplx_A0, oplx_A0, oplx_A0, oplx_A0, oplx_A0, oplx_A0, oplx_A0,
   oplx_A8, oplx_A8, oplx_A8, oplx_A8, oplx_A8, oplx_A8, oplx_A8, oplx_A8,
   oplx_B0, oplx_B0, oplx_B0, oplx_B0, oplx_B0, oplx_B0, oplx_B0, oplx_B0,
   oplx_B8, oplx_B8, oplx_B8, oplx_B8, oplx_B8, oplx_B8, oplx_B8, oplx_B8,

   oplx_C0, oplx_C0, oplx_C0, oplx_C0, oplx_C0, oplx_C0, oplx_C0, oplx_C0,
   oplx_C8, oplx_C8, oplx_C8, oplx_C8, oplx_C8, oplx_C8, oplx_C8, oplx_C8,
   oplx_D0, oplx_D0, oplx_D0, oplx_D0, oplx_D0, oplx_D0, oplx_D0, oplx_D0,
   oplx_D8, oplx_D8, oplx_D8, oplx_D8, oplx_D8, oplx_D8, oplx_D8, oplx_D8,
   oplx_E0, oplx_E0, oplx_E0, oplx_E0, oplx_E0, oplx_E0, oplx_E0, oplx_E0,
   oplx_E8, oplx_E8, oplx_E8, oplx_E8, oplx_E8, oplx_E8, oplx_E8, oplx_E8,
   oplx_F0, oplx_F0, oplx_F0, oplx_F0, oplx_F0, oplx_F0, oplx_F0, oplx_F0,
   oplx_F8, oplx_F8, oplx_F8, oplx_F8, oplx_F8, oplx_F8, oplx_F8, oplx_F8,
};

// Destination registers for DDCB/FDCB results (b,c,d,e,h,l,<trash>,a)
// live in the Z80CPU::directRegisters member, wired by Z80CpuCreate()
// (the core used a process-global array bound to its single instance).

Z80INLINE int Z80FAST ddfd_prefixes(Z80CPU *cpu, uint8_t opcode, int tact0)
{
    int tact_ = tact0; // every exit returns the final T (Step publishes cpu->t)
    uint8_t op1; // last DD/FD prefix

    do
    {
        op1 = opcode;
        opcode = Z80M1(cpu);
    }
    while ((opcode | 0x20) == 0xFD); // opcode == DD/FD

    // xxCB prefix - bit operations
    // DDCB - IX base address
    // FDCB - IY base address
    if (opcode == 0xCB)
    {
        uint16_t ptr; // pointer to DDCB operand
        int8_t displacement = Z80Rd(cpu, cpu->pc++, true);
        ptr = ((op1 == 0xDD) ? cpu->ix:cpu->iy) + displacement;
        cpu->memptr = ptr;

        // DDCBnnXX,FDCBnnXX increment R by 2, not 3!
        opcode = Z80M1(cpu);
        cpu->r_low--;
        cpu->prefix = 0xCB;  // after the M1 (the opword store cleared it)

        cputact(1);

        uint8_t byte = (logic_ix_opcode[opcode])(cpu, Z80Rd(cpu, ptr, true));

        cputact(1);

        if ((opcode & 0xC0) == 0x40)
            return Z80FinishFlags(cpu, tact_); // bit n,rm: writes F

        // Select destination register for shift/res/set:
        // [0] - b
        // [1] - c
        // [2] - d
        // [3] - e
        // [4] - h
        // [5] - l
        // [6] - <unused>
        // [7] - a
        uint8_t destRegisterIndex = opcode & 0b0000'0111;

        // Store operation result into specified register
        // Note: If destRegisterIndex=6 i.e. no results should be stored - trashRegister will be used
        // Examples: set <N>, (iy + <M>) and similar mnemonics
        *(cpu->directRegisters[destRegisterIndex]) = byte;

        // Store result to IX/IY addressed memory
        Z80Wd(cpu, ptr, byte);

        // Finalize opcode
        cpu->opcode = opcode;

        // rotates/shifts (00-3F) write F, RES/SET (80-FF) do not
        return (opcode < 0x80) ? Z80FinishFlags(cpu, tact_) : Z80FinishNoFlags(cpu, tact_);
    }

    // ED prefix
    if (opcode == 0xED)
    {
        opcode = Z80M1(cpu);
        cpu->prefix = 0xED;  // DD/FD before ED is ignored by the CPU

        // the callee returns the final T
        return (ext_opcode[opcode])(cpu, tact_);
    }

    // DD/FD prefix: IX/IY opcode maps (prefix byte stays 0: DD FB is EI)
    if (op1 == 0xDD)
        return ix_opcode[opcode](cpu, tact_);
    if (op1 == 0xFD)
        return iy_opcode[opcode](cpu, tact_);

    // Unreachable: ddfd_prefixes is only entered with a DD/FD argument
    // (the do/while above guarantees op1 is 0xDD or 0xFD at this point).
    return Z80FinishNoFlags(cpu, tact_);
}

Z80OPCODE op_DD(Z80CPU *cpu, int tact0)
{
    return ddfd_prefixes(cpu, 0xDD, tact0);
}

Z80OPCODE op_FD(Z80CPU *cpu, int tact0)
{
    return ddfd_prefixes(cpu, 0xFD, tact0);
}

