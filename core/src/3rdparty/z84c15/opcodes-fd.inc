// opcodes-fd.cpp - FD (IY) prefix opcodes, ported from core op_fd.cpp.


// FD prefix opcodes

Z84OPCODE opy_09(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add iy,bc
   Z84_T_BEGIN(cpu);
   rf->memptr = rf->iy + 1;

   int iy = rf->iy & 0xFFFF;
   int bc = rf->bc & 0xFFFF;

   int halfIY = iy & 0x0FFF;
   int halfBC = bc & 0x0FFF;

   // Clear flags
   rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
   rf->f |= ((halfIY + halfBC) >> 8) & 0x10; // HF

   // Do add
   iy = iy + bc;

   if (iy & 0x10000)
	   rf->f |= CF;

   // Store result to registers
   rf->iy = iy & 0xFFFF;
   rf->f |= (rf->yh & (F5 | F3));

   cpuidle(Z84_IR(rf), 7);
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_19(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add iy,de
    Z84_T_BEGIN(cpu);
    rf->memptr = rf->iy + 1;

    int iy = rf->iy & 0xFFFF;
    int de = rf->de & 0xFFFF;

    int halfIY = iy & 0x0FFF;
    int halfDE = de & 0x0FFF;

    // Clear flags
    rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
    rf->f |= ((halfIY + halfDE) >> 8) & 0x10; // HF

    // Do add
    iy = iy + de;

    if (iy & 0x10000)
        rf->f |= CF;

    // Store result to registers
    rf->iy = iy & 0xFFFFF;
    rf->f |= (rf->yh & (F5 | F3));

    cpuidle(Z84_IR(rf), 7);
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_21(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld iy,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

   rf->yl = Z84Rd(cpu, pc++, true);
   rf->yh = Z84Rd(cpu, pc++, true);

   rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_22(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (nnnn),iy
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z84Rd(cpu, pc++, true);
    addr += Z84Rd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    Z84Wd(cpu, addr, rf->yl);
    Z84Wd(cpu, addr + 1, rf->yh);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_23(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc iy
   Z84_T_BEGIN(cpu);
   rf->iy = (rf->iy + 1) & 0xFFFF;
   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_24(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc yh
   inc8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_25(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec yh
   dec8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_26(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yh,nn
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    rf->yh = Z84Rd(cpu, pc++, true);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_29(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add iy,iy
    Z84_T_BEGIN(cpu);
    rf->memptr = rf->iy + 1;

    int iy = rf->iy & 0xFFFF;

    // Clear flags
    rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
    rf->f |= ((iy >> 7) & 0x10); // HF

    // Do add
    iy = iy+ iy;

    if (iy & 0x10000)
        rf->f |= CF;

    // Store result to registers
    rf->iy = iy & 0xFFFF;
    rf->f |= (rf->yh & (F5 | F3));

    cpuidle(Z84_IR(rf), 7);
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_2A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld iy,(nnnn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    uint16_t addr = Z84Rd(cpu, pc++, true);
    addr += Z84Rd(cpu, pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->yl = Z84Rd(cpu, addr);
    rf->yh = Z84Rd(cpu, addr + 1);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_2B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec iy
   Z84_T_BEGIN(cpu);
   rf->iy = (rf->iy - 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_2C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc yl
   inc8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_2D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec yl
   dec8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_2E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yl,nn
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    rf->yl = Z84Rd(cpu, pc++, true);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_34(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc (iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    uint8_t value = Z84Rd(cpu, rf->iy + displacement);
    inc8(rf, value);

    cpuidle(rf->iy + displacement, 1);

    Z84Wd(cpu, rf->iy + displacement, value);

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_35(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec (iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    uint8_t value = Z84Rd(cpu, rf->iy + displacement);
    dec8(rf, value);

    cpuidle(rf->iy + displacement, 1);

    Z84Wd(cpu, rf->iy + displacement, value);

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_36(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),nn
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;
    uint8_t value = Z84Rd(cpu, pc++, true);

    cpuidle(pc - 1, 2);  // on the address of n

    Z84Wd(cpu, rf->iy + displacement, value);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_39(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add iy,sp
   Z84_T_BEGIN(cpu);
   rf->memptr = rf->iy + 1;

   int iy = rf->iy & 0xFFFF;
   int sp = rf->sp & 0xFFFF;

   int halfIY = iy & 0x0FFF;
   int halfSP = sp & 0x0FFF;

   // Clear flags
   rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry flag (HF)
   rf->f |= ((halfIY + halfSP) >> 8) & 0x10; // HF

   // Do add
   iy = iy + sp;

   if (iy & 0x10000)
	   rf->f |= CF;

   // Store result to registers
   rf->iy = iy & 0xFFFF;
   rf->f |= (rf->yh & (F5 | F3));

   cpuidle(Z84_IR(rf), 7);
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_44(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,yh
   rf->b = rf->yh;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_45(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,yl
   rf->b = rf->yl;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_46(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->b = Z84Rd(cpu, rf->iy + displacement);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_4C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,yh
   rf->c = rf->yh;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_4D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,yl
   rf->c = rf->yl;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_4E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;

   int8_t displacement = Z84Rd(cpu, pc++, true);

   // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
   rf->memptr = (rf->iy + displacement) & 0xFFFF;

   cpuidle(pc - 1, 5);  // on the displacement's address

   rf->c = Z84Rd(cpu, rf->iy + displacement);

   rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_54(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,yh
   rf->d = rf->yh;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_55(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,yl
   rf->d = rf->yl;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_56(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->d = Z84Rd(cpu, rf->iy + displacement);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_5C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,yh
   rf->e = rf->yh;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_5D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,yl
   rf->e = rf->yl;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_5E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->e = Z84Rd(cpu, rf->iy + displacement);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_60(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yh,b
   rf->yh = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_61(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yh,c
   rf->yh = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_62(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yh,d
   rf->yh = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_63(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yh,e
   rf->yh = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_65(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yh,yl
   rf->yh = rf->yl;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_66(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->h = Z84Rd(cpu, rf->iy + displacement);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_67(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yh,a
   rf->yh = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_68(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yl,b
   rf->yl = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_69(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yl,c
   rf->yl = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_6A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yl,d
   rf->yl = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_6B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yl,e
   rf->yl = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_6C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yl,yh
   rf->yl = rf->yh;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_6E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->l = Z84Rd(cpu, rf->iy + displacement);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_6F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld yl,a
   rf->yl = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_70(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),b
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z84Wd(cpu, rf->iy + displacement, rf->b);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_71(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),c
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z84Wd(cpu, rf->iy + displacement, rf->c);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_72(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),d
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z84Wd(cpu, rf->iy + displacement, rf->d);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_73(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),e
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z84Wd(cpu, rf->iy + displacement, rf->e);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_74(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),h
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z84Wd(cpu, rf->iy + displacement, rf->h);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_75(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),l
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z84Wd(cpu, rf->iy + displacement, rf->l);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_77(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (iy+nn),a
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    Z84Wd(cpu, rf->iy + displacement, rf->a);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_7C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,yh
   rf->a = rf->yh;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_7D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,yl
   rf->a = rf->yl;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_7E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    rf->a = Z84Rd(cpu, rf->iy + displacement);

    rf->pc = pc;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_84(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,yh
   add8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_85(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,yl
   add8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_86(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    add8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_8C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,yh
   adc8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_8D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,yl
   adc8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_8E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    adc8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_94(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub yh
   sub8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_95(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub yl
   sub8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_96(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub (iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    sub8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_9C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,yh
   sbc8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_9D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,yl
   sbc8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_9E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,(iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    sbc8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_A4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and yh
   and8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_A5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and yl
   and8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_A6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and (iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    and8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_AC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor yh
   xor8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_AD(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor yl
   xor8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_AE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor (iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    xor8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_B4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or yh
   or8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_B5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or yl
   or8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_B6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or (iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    or8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_BC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp yh
   cp8(rf, rf->yh);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_BD(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp yl
   cp8(rf, rf->yl);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE opy_BE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp (iy+nn)
    Z84_T_BEGIN(cpu);
    uint16_t pc = rf->pc;
    int8_t displacement = Z84Rd(cpu, pc++, true);

    // MEMPTR = IY+d for all (iy+d) addressing (FUSE-verified)
    rf->memptr = (rf->iy + displacement) & 0xFFFF;

    cpuidle(pc - 1, 5);  // on the displacement's address

    cp8(rf, Z84Rd(cpu, rf->iy + displacement));

    rf->pc = pc;
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE opy_E1(Z84CPU *cpu, int tact0, Z84Regs *rf) { // pop iy
   Z84_T_BEGIN(cpu);
   uint16_t sp = rf->sp & 0xFFFF;

   rf->yl = Z84Rd(cpu, sp++);
   rf->yh = Z84Rd(cpu, sp++);

   rf->sp = sp & 0xFFFF;
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_E3(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ex (sp),iy
    Z84_T_BEGIN(cpu);
    uint16_t sp = rf->sp & 0xFFFF;

    uint16_t value = Z84Rd(cpu, sp) + 0x100 * Z84Rd(cpu, sp + 1);
    cpuidle(rf->sp + 1, 1);

    // Real Z80 write order: high byte (SP+1) first, then low (SP) - FUSE-verified
    Z84Wd(cpu, sp + 1, rf->yh);
    Z84Wd(cpu, sp, rf->yl);

    rf->memptr = value;

    rf->iy = value;

    cpuidle(rf->sp, 2);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_E5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // push iy
   Z84_T_BEGIN(cpu);
   cpuidle(Z84_IR(rf), 1);

   uint16_t sp = rf->sp;

   Z84Wd(cpu, --sp, rf->yh);
   Z84Wd(cpu, --sp, rf->yl);

   rf->sp = sp;
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE opy_E9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp (iy)

   rf->pc = rf->iy & 0xFFFF;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE opy_F9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld sp,iy
   Z84_T_BEGIN(cpu);
   rf->sp = rf->iy & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

// SCF/CCF behind FD: Q = 0 (see opx_37 in opcodes-dd.inc)
Z84OPCODE opy_37(Z84CPU *cpu, int tact0, Z84Regs *rf) { rf->q = 0; return op_37(cpu, tact0, rf); }
Z84OPCODE opy_3F(Z84CPU *cpu, int tact0, Z84Regs *rf) { rf->q = 0; return op_3F(cpu, tact0, rf); }

STEPFUNC const iy_opcode[0x100] =
{
    op_00,  op_01,  op_02,  op_03,  op_04,  op_05,  op_06,  op_07,
    op_08, opy_09,  op_0A,  op_0B,  op_0C,  op_0D,  op_0E,  op_0F,
    op_10,  op_11,  op_12,  op_13,  op_14,  op_15,  op_16,  op_17,
    op_18, opy_19,  op_1A,  op_1B,  op_1C,  op_1D,  op_1E,  op_1F,
    op_20, opy_21, opy_22, opy_23, opy_24, opy_25, opy_26,  op_27,
    op_28, opy_29, opy_2A, opy_2B, opy_2C, opy_2D, opy_2E,  op_2F,
    op_30,  op_31,  op_32,  op_33, opy_34, opy_35, opy_36, opy_37,
    op_38, opy_39,  op_3A,  op_3B,  op_3C,  op_3D,  op_3E, opy_3F,

    op_40,  op_41,  op_42,  op_43, opy_44, opy_45, opy_46,  op_47,
    op_48,  op_49,  op_4A,  op_4B, opy_4C, opy_4D, opy_4E,  op_4F,
    op_50,  op_51,  op_52,  op_53, opy_54, opy_55, opy_56,  op_57,
    op_58,  op_59,  op_5A,  op_5B, opy_5C, opy_5D, opy_5E,  op_5F,
   opy_60, opy_61, opy_62, opy_63,  op_64, opy_65, opy_66, opy_67,
   opy_68, opy_69, opy_6A, opy_6B, opy_6C,  op_6D, opy_6E, opy_6F,
   opy_70, opy_71, opy_72, opy_73, opy_74, opy_75,  op_76, opy_77,
    op_78,  op_79,  op_7A,  op_7B, opy_7C, opy_7D, opy_7E,  op_7F,

    op_80,  op_81,  op_82,  op_83, opy_84, opy_85, opy_86,  op_87,
    op_88,  op_89,  op_8A,  op_8B, opy_8C, opy_8D, opy_8E,  op_8F,
    op_90,  op_91,  op_92,  op_93, opy_94, opy_95, opy_96,  op_97,
    op_98,  op_99,  op_9A,  op_9B, opy_9C, opy_9D, opy_9E,  op_9F,
    op_A0,  op_A1,  op_A2,  op_A3, opy_A4, opy_A5, opy_A6,  op_A7,
    op_A8,  op_A9,  op_AA,  op_AB, opy_AC, opy_AD, opy_AE,  op_AF,
    op_B0,  op_B1,  op_B2,  op_B3, opy_B4, opy_B5, opy_B6,  op_B7,
    op_B8,  op_B9,  op_BA,  op_BB, opy_BC, opy_BD, opy_BE,  op_BF,

    op_C0,  op_C1,  op_C2,  op_C3,  op_C4,  op_C5,  op_C6,  op_C7,
    op_C8,  op_C9,  op_CA,  op_CB,  op_CC,  op_CD,  op_CE,  op_CF,
    op_D0,  op_D1,  op_D2,  op_D3,  op_D4,  op_D5,  op_D6,  op_D7,
    op_D8,  op_D9,  op_DA,  op_DB,  op_DC,  op_DD,  op_DE,  op_DF,
    op_E0, opy_E1,  op_E2, opy_E3,  op_E4, opy_E5,  op_E6,  op_E7,
    op_E8, opy_E9,  op_EA,  op_EB,  op_EC,  op_ED,  op_EE,  op_EF,
    op_F0,  op_F1,  op_F2,  op_F3,  op_F4,  op_F5,  op_F6,  op_F7,
    op_F8, opy_F9,  op_FA,  op_FB,  op_FC,  op_FD,  op_FE,  op_FF,
};
