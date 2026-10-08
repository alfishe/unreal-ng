// opcodes-dd.cpp - DD (IX) prefix opcodes, ported from core op_dd.cpp.


// DD prefix opcodes

Z80NOPCODE opx_09(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add ix,bc
   Z80N_T_BEGIN(cpu);
   rf->memptr = (rf->ix + 1) & 0xFFFF;

   int ix = rf->ix & 0xFFFF;
   int bc = rf->bc & 0xFFFF;

   int halfIX = ix & 0x0FFF;
   int halfBC = bc & 0x0FFF;

   // Clear flags
   rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

   // Set half-carry flag (HF)
   rf->f |= ((halfIX + halfBC) >> 8) & 0x10; // HF

   ix = ix + bc;

   if (ix & 0x10000)
       rf->f |= CF;

   // Store result to registers
   rf->ix = ix & 0xFFFF;
   rf->f |= (rf->xh & (F5 | F3));

   cpuidle(Z80N_IR(rf), 7);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_19(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add ix,de
    Z80N_T_BEGIN(cpu);
    rf->memptr = (rf->ix + 1) & 0xFFFF;

    int ix = rf->ix & 0xFFFF;
    int de = rf->de & 0xFFFF;

    int halfIX = ix & 0x0FFF;
    int halfDE = de & 0x0FFF;

    // Clear flags
    rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
    rf->f |= ((halfIX + halfDE) >> 8) & 0x10; // HF

    // Do add
    ix = ix + de;

    if (ix & 0x10000)
        rf->f |= CF;

    // Store result to registers
    rf->ix = ix & 0xFFFF;
    rf->f |= (rf->xh & (F5 | F3));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_21(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld ix,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    rf->xl = Z80nRd(cpu, pc++, true);
    rf->xh = Z80nRd(cpu, pc++, true);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_22(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (nnnn),ix
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

   uint16_t addr = Z80nRd(cpu, pc++, true);
   addr += Z80nRd(cpu, pc++, true) * 0x100;

   rf->memptr = addr + 1;

   Z80nWd(cpu, addr, rf->xl);
   Z80nWd(cpu, addr + 1, rf->xh);

   rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_23(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc ix
   Z80N_T_BEGIN(cpu);
   rf->ix = (rf->ix + 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_24(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc xh
   inc8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_25(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec xh
   dec8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_26(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xh,nn
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

   rf->xh = Z80nRd(cpu, pc++, true);

   rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_29(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add ix,ix
    Z80N_T_BEGIN(cpu);
    rf->memptr = rf->ix + 1;

    int ix = rf->ix & 0xFFFF;

    // Clear flags
    rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
    rf->f |= ((ix >> 7) & 0x10); // HF

    // Do add
    ix = ix + ix;

    if (ix & 0x10000)
        rf->f |= CF;

    // Store result to registers
    rf->ix = ix & 0xFFFF;
    rf->f |= (rf->xh & (F5 | F3));

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_2A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld ix,(nnnn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z80nRd(cpu, pc++, true);
    addr += Z80nRd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->xl = Z80nRd(cpu, addr);
    rf->xh = Z80nRd(cpu, addr + 1);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_2B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec ix
   Z80N_T_BEGIN(cpu);
   rf->ix = (rf->ix - 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_2C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc xl
   inc8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_2D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec xl
   dec8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_2E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xl,nn
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    rf->xl = Z80nRd(cpu, pc++, true);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_34(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc (ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    uint8_t t = Z80nRd(cpu, rf->ix + displacement);
    inc8(rf, t);

    cpuidle(rf->ix + displacement, 1);

    Z80nWd(cpu, rf->ix + displacement, t);

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_35(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec (ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    uint8_t t = Z80nRd(cpu, rf->ix + displacement);
    dec8(rf, t);

    cpuidle(rf->ix + displacement, 1);

    Z80nWd(cpu, rf->ix + displacement, t);

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_36(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),nn
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;
    uint8_t value = Z80nRd(cpu, pc++, true);

    cpuidle(pc - 1, 2);  // on the address of n

    Z80nWd(cpu, rf->ix + displacement, value);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_39(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add ix,sp
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->ix + 1;

   int ix = rf->ix & 0xFFFF;
   int sp = rf->sp & 0xFFFF;

   int halfIX = ix & 0x0FFF;
   int halfSP = sp & 0x0FFF;

   // Clear flags
   rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
   rf->f |= ((halfIX + halfSP) >> 8) & 0x10; // HF

   // Do add
   ix = ix +  sp;

   if (ix & 0x10000)
	   rf->f |= CF;

   // Store result to registers
   rf->ix = ix & 0xFFFF;
   rf->f |= (rf->xh & (F5 | F3));

   cpuidle(Z80N_IR(rf), 7);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_44(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,xh
   rf->b = rf->xh;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_45(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,xl
   rf->b = rf->xl;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_46(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->b = Z80nRd(cpu, rf->ix + displacement);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_4C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,xh
   rf->c = rf->xh;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_4D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,xl
   rf->c = rf->xl;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_4E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->c = Z80nRd(cpu, rf->ix + displacement);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_54(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,xh
   rf->d = rf->xh;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_55(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,xl
   rf->d = rf->xl;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_56(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->d = Z80nRd(cpu, rf->ix + displacement);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_5C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,xh
   rf->e = rf->xh;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_5D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,xl
   rf->e = rf->xl;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_5E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->e = Z80nRd(cpu, rf->ix + displacement);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_60(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xh,b
   rf->xh = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_61(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xh,c
   rf->xh = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_62(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xh,d
   rf->xh = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_63(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xh,e
   rf->xh = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_65(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xh,xl
   rf->xh = rf->xl;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_66(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->h = Z80nRd(cpu, rf->ix + displacement);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_67(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xh,a
   rf->xh = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_68(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xl,b
   rf->xl = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_69(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xl,c
   rf->xl = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_6A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xl,d
   rf->xl = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_6B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xl,e
   rf->xl = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_6C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xl,xh
   rf->xl = rf->xh;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_6E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->l = Z80nRd(cpu, rf->ix + displacement);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_6F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld xl,a
   rf->xl = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_70(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),b
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z80nWd(cpu, rf->ix + displacement, rf->b);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_71(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),c
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z80nWd(cpu, rf->ix + displacement, rf->c);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_72(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),d
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z80nWd(cpu, rf->ix + displacement, rf->d);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_73(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),e
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z80nWd(cpu, rf->ix + displacement, rf->e);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_74(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),h
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z80nWd(cpu, rf->ix + displacement, rf->h);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_75(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),l
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z80nWd(cpu, rf->ix + displacement, rf->l);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_77(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (ix+nn),a
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z80nWd(cpu, rf->ix + displacement, rf->a);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_7C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,xh
   rf->a = rf->xh;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_7D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,xl
   rf->a = rf->xl;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_7E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->a = Z80nRd(cpu, rf->ix + displacement);

    rf->pc = pc;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_84(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,xh
   add8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_85(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,xl
   add8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_86(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    add8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_8C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,xh
   adc8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_8D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,xl
   adc8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_8E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    adc8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_94(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub xh
   sub8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_95(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub xl
   sub8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_96(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub (ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    sub8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_9C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,xh
   sbc8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_9D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,xl
   sbc8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_9E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,(ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    sbc8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_A4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and xh
   and8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_A5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and xl
   and8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_A6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and (ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    and8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_AC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor xh
   xor8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_AD(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor xl
   xor8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_AE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor (ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    xor8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_B4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or xh
   or8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_B5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or xl
   or8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_B6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or (ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    or8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_BC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp xh
   cp8(rf, rf->xh);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_BD(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp xl
   cp8(rf, rf->xl);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE opx_BE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp (ix+nn)
    Z80N_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z80nRd(cpu, pc++, true);

    // MEMPTR = IX+d for all (ix+d) addressing (FUSE-verified)
    rf->memptr = (rf->ix + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    cp8(rf, Z80nRd(cpu, rf->ix + displacement));

    rf->pc = pc;
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE opx_E1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // pop ix
    Z80N_T_BEGIN(cpu);
    uint16_t sp = rf->sp;

   rf->xl = Z80nRd(cpu, sp++);
   rf->xh = Z80nRd(cpu, sp++);

   rf->sp = sp;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_E3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ex (sp),ix
   Z80N_T_BEGIN(cpu);
   uint16_t value = Z80nRd(cpu, rf->sp) + 0x100 * Z80nRd(cpu, rf->sp + 1);

   cpuidle(rf->sp + 1, 1);

   // Real Z80 write order: high byte (SP+1) first, then low (SP) - FUSE-verified
   Z80nWd(cpu, rf->sp + 1, rf->xh);
   Z80nWd(cpu, rf->sp, rf->xl);

   rf->memptr = value;
   rf->ix = value;

   cpuidle(rf->sp, 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE opx_E5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // push ix
   Z80N_T_BEGIN(cpu);
   cpuidle(Z80N_IR(rf), 1);

   Z80nWd(cpu, --rf->sp, rf->xh);
   Z80nWd(cpu, --rf->sp, rf->xl);
   Z80N_RETURN_Q(0, tact_);
}
Z80NOPCODE opx_E9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp (ix)

   rf->pc = rf->ix & 0xFFFF;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE opx_F9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld sp,ix
   Z80N_T_BEGIN(cpu);
   rf->sp = rf->ix & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

// SCF/CCF behind DD/FD: the prefix M1 is the "previous instruction" whose
// Q they read, and it wrote no flags, so Q = 0 (Tony Brewer's hardware
// finding; redcode Z80, FUSE and MAME agree). Only SCF/CCF read Q, so the
// reset lives in these two entries instead of on every prefixed instruction.
Z80NOPCODE opx_37(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { rf->q = 0; return op_37(cpu, tact0, rf); }
Z80NOPCODE opx_3F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { rf->q = 0; return op_3F(cpu, tact0, rf); }

STEPFUNC const ix_opcode[0x100] =
{
    op_00,  op_01,  op_02,  op_03,  op_04,  op_05,  op_06,  op_07,
    op_08, opx_09,  op_0A,  op_0B,  op_0C,  op_0D,  op_0E,  op_0F,
    op_10,  op_11,  op_12,  op_13,  op_14,  op_15,  op_16,  op_17,
    op_18, opx_19,  op_1A,  op_1B,  op_1C,  op_1D,  op_1E,  op_1F,
    op_20, opx_21, opx_22, opx_23, opx_24, opx_25, opx_26,  op_27,
    op_28, opx_29, opx_2A, opx_2B, opx_2C, opx_2D, opx_2E,  op_2F,
    op_30,  op_31,  op_32,  op_33, opx_34, opx_35, opx_36, opx_37,
    op_38, opx_39,  op_3A,  op_3B,  op_3C,  op_3D,  op_3E, opx_3F,

    op_40,  op_41,  op_42,  op_43, opx_44, opx_45, opx_46,  op_47,
    op_48,  op_49,  op_4A,  op_4B, opx_4C, opx_4D, opx_4E,  op_4F,
    op_50,  op_51,  op_52,  op_53, opx_54, opx_55, opx_56,  op_57,
    op_58,  op_59,  op_5A,  op_5B, opx_5C, opx_5D, opx_5E,  op_5F,
   opx_60, opx_61, opx_62, opx_63,  op_64, opx_65, opx_66, opx_67,
   opx_68, opx_69, opx_6A, opx_6B, opx_6C,  op_6D, opx_6E, opx_6F,
   opx_70, opx_71, opx_72, opx_73, opx_74, opx_75,  op_76, opx_77,
    op_78,  op_79,  op_7A,  op_7B, opx_7C, opx_7D, opx_7E,  op_7F,

    op_80,  op_81,  op_82,  op_83, opx_84, opx_85, opx_86,  op_87,
    op_88,  op_89,  op_8A,  op_8B, opx_8C, opx_8D, opx_8E,  op_8F,
    op_90,  op_91,  op_92,  op_93, opx_94, opx_95, opx_96,  op_97,
    op_98,  op_99,  op_9A,  op_9B, opx_9C, opx_9D, opx_9E,  op_9F,
    op_A0,  op_A1,  op_A2,  op_A3, opx_A4, opx_A5, opx_A6,  op_A7,
    op_A8,  op_A9,  op_AA,  op_AB, opx_AC, opx_AD, opx_AE,  op_AF,
    op_B0,  op_B1,  op_B2,  op_B3, opx_B4, opx_B5, opx_B6,  op_B7,
    op_B8,  op_B9,  op_BA,  op_BB, opx_BC, opx_BD, opx_BE,  op_BF,

    op_C0,  op_C1,  op_C2,  op_C3,  op_C4,  op_C5,  op_C6,  op_C7,
    op_C8,  op_C9,  op_CA,  op_CB,  op_CC,  op_CD,  op_CE,  op_CF,
    op_D0,  op_D1,  op_D2,  op_D3,  op_D4,  op_D5,  op_D6,  op_D7,
    op_D8,  op_D9,  op_DA,  op_DB,  op_DC,  op_DD,  op_DE,  op_DF,
    op_E0, opx_E1,  op_E2, opx_E3,  op_E4, opx_E5,  op_E6,  op_E7,
    op_E8, opx_E9,  op_EA,  op_EB,  op_EC,  op_ED,  op_EE,  op_EF,
    op_F0,  op_F1,  op_F2,  op_F3,  op_F4,  op_F5,  op_F6,  op_F7,
    op_F8, opx_F9,  op_FA,  op_FB,  op_FC,  op_FD,  op_FE,  op_FF,
};

