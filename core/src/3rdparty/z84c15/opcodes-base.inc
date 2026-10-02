// opcodes-base.cpp - no-prefix opcodes, ported from core op_noprefix.cpp.


//  Non-prefixed opcodes
// Important note: M1 cycle lasts 4 clocks. This is the bare minimum for any opcode. So only additional deltas added here with cputact(n) macro

Z84OPCODE op_00(Z84CPU *cpu, int tact0, Z84Regs *rf) { // nop [4]
	// No increment for CPU cycles counter (t) is required
	// 4 cycles already spent in m1_cycle();
	Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_01(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld bc,nnnn [10]
    Z84_T_BEGIN(cpu);
    rf->c = Z84Rd(cpu, rf->pc++, true);
    rf->b = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_02(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (bc),a [7]
   Z84_T_BEGIN(cpu);
   // MEMPTR: low byte = (BC+1) & 0xFF, high byte = A (FUSE-verified)
   rf->memptr = ((rf->bc + 1) & 0xFF) | (rf->a << 8);
   Z84Wd(cpu, rf->bc, rf->a);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_03(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc bc
   Z84_T_BEGIN(cpu);
   rf->bc = (rf->bc + 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_04(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc b
   inc8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_05(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec b
   dec8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_06(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,nn
    Z84_T_BEGIN(cpu);
    rf->b = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_07(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rlca
   rf->f = rlca_f[rf->a] | (rf->f & (SF | ZF | PV));
   rf->a = rol[rf->a];
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_08(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ex af,af'
   uint16_t mainAF = rf->af;
   rf->af = rf->alt.af;
   rf->alt.af = mainAF;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_09(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add hl,bc
    Z84_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    uint8_t flags = rf->f;

    int hl = rf->hl & 0xFFFF;
    int bc = rf->bc & 0xFFFF;

    //int halfHL = hl & 0x0FFF;
    //int halfBC = bc & 0x0FFF;

    // Calculate half-carry flag (HF)
    flags = (flags & ~(NF | CF | F5 | F3 | HF));
    flags |= (((rf->hl & 0x0FFF) + (rf->bc & 0x0FFF)) >> 8) & 0x10; // HF

    // Do Add
    hl = hl + bc;

    // Check for carry flag (CF)
    if (hl & 0x10000)
        flags |= CF;

    flags |= ((hl >> 8) & (F5 | F3));  // X/Y from result high byte

    // Store result back to registers
    rf->hl = hl & 0xFFFF;
    rf->f = flags;

    cpuidle(Z84_IR(rf), 7);
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_0A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,(bc)
   Z84_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;
   rf->a = Z84Rd(cpu, rf->bc);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_0B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec bc
   Z84_T_BEGIN(cpu);
   rf->bc = (rf->bc - 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_0C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc c
   inc8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_0D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec c
   dec8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_0E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,nn
    Z84_T_BEGIN(cpu);
    rf->c = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_0F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rrca
   rf->f = rrca_f[rf->a] | (rf->f & (SF | ZF | PV));
   rf->a = ror[rf->a];
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_10(Z84CPU *cpu, int tact0, Z84Regs *rf) { // djnz rr
	Z84_T_BEGIN(cpu);
	cpuidle(Z84_IR(rf), 1);

	int8_t offset = Z84Rd(cpu, rf->pc++, true);  // instruction-stream byte (m1State, Operand kind)

	// Branch taken
	if (--rf->b)
	{

        rf->pc += offset;
		rf->memptr = rf->pc;

		cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
	}
	Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_11(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld de,nnnn
    Z84_T_BEGIN(cpu);
    rf->e = Z84Rd(cpu, rf->pc++, true);
    rf->d = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_12(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (de),a
   Z84_T_BEGIN(cpu);
   // MEMPTR: low byte = (DE+1) & 0xFF, high byte = A (FUSE-verified)
   rf->memptr = ((rf->de + 1) & 0xFF) | (rf->a << 8);
   Z84Wd(cpu, rf->de, rf->a);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_13(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc de
   Z84_T_BEGIN(cpu);
   rf->de = (rf->de + 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_14(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc d
   inc8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_15(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec d
   dec8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_16(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,nn
    Z84_T_BEGIN(cpu);
    rf->d = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_17(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rla
   uint8_t new_a = (rf->a << 1) + (rf->f & 1);

   rf->f = rlca_f[rf->a] | (rf->f & (SF | ZF | PV)); // use same table with rlca
   rf->a = new_a;
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_18(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jr rr
    Z84_T_BEGIN(cpu);
    int8_t offset = Z84Rd(cpu, rf->pc++, true);


    rf->pc += offset;
    rf->memptr = rf->pc;

    cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_19(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add hl,de
    Z84_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    uint8_t flags = rf->f;

    int hl = rf->hl & 0xFFFF;
    int de = rf->de & 0xFFFF;

    int halfHL = hl & 0x0FFF;
    int halfDE = de & 0x0FFF;

    // Check for half-carry (HF)
    flags = (flags & ~(NF | CF | F5 | F3 | HF));
    flags |= ((halfHL + halfDE) >> 8) & 0x10; // HF

    // Do add
    hl = hl + de;

    // Check for carry flag (CF)
    if (hl & 0x10000)
        flags |= CF;
    flags |= ((hl >> 8) & (F5 | F3));  // X/Y from result high byte

    // Store operation result back to registers
    rf->hl = hl & 0xFFFF;
    rf->f = flags;

    cpuidle(Z84_IR(rf), 7);
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_1A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,(de)
   Z84_T_BEGIN(cpu);
   rf->memptr = rf->de + 1;

   rf->a = Z84Rd(cpu, rf->de);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_1B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec de
   Z84_T_BEGIN(cpu);
   rf->de = (rf->de - 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_1C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc e
   inc8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_1D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec e
   dec8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_1E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,nn
    Z84_T_BEGIN(cpu);
    rf->e = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_1F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rra
   uint8_t new_a = (rf->a >> 1) + (rf->f << 7);

   rf->f = rrca_f[rf->a] | (rf->f & (SF | ZF | PV)); // use same table with rrca
   rf->a = new_a;
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_20(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jr nz, rr
    Z84_T_BEGIN(cpu);
    int8_t offset = Z84Rd(cpu, rf->pc++, true);

    if (!(rf->f & ZF))
    {
        rf->pc += offset;

        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_21(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld hl,nnnn
    Z84_T_BEGIN(cpu);
    rf->l = Z84Rd(cpu, rf->pc++, true);
    rf->h = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_22(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (nnnn),hl
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += Z84Rd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = addr + 1;

    Z84Wd(cpu, addr, rf->l);
    Z84Wd(cpu, addr + 1, rf->h);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_23(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc hl
   Z84_T_BEGIN(cpu);
   rf->hl = (rf->hl + 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_24(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc h
   inc8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_25(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec h
   dec8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_26(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,nn
    Z84_T_BEGIN(cpu);
    rf->h = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_27(Z84CPU *cpu, int tact0, Z84Regs *rf) { // daa
   rf->af = *(uint16_t *)(daatab + (rf->a + 0x100 * ((rf->f & 3) + ((rf->f >> 2) & 4))) * 2);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_28(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jr z,rr
    Z84_T_BEGIN(cpu);
    int8_t offset = Z84Rd(cpu, rf->pc++, true);

    // Branch taken
    if ((rf->f & ZF))
    {
        rf->pc += offset;
        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_29(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add hl,hl
    Z84_T_BEGIN(cpu);
    rf->memptr = rf->hl + 1;

    uint8_t flags = rf->f;
    int hl = rf->hl & 0xFFFF;

    // Clear flags affecting result
    flags = (rf->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry (HF) flag
    flags |= ((rf->hl >> 7) & 0x10); // HF

    // Do add
    hl = hl + hl;

    // Handle carry-over
    if (hl & 0x10000)
        flags |= CF;

    // Update undocumented flags
    flags |= ((hl >> 8) & (F5 | F3));  // X/Y from result high byte

    // Store result back to registers
    rf->hl = hl & 0xFFFF;
    rf->f = flags;

    cpuidle(Z84_IR(rf), 7);
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_2A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld hl,(nnnn)
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += Z84Rd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->l = Z84Rd(cpu, addr);
    rf->h = Z84Rd(cpu, addr + 1);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_2B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec hl
   Z84_T_BEGIN(cpu);
   rf->hl = (rf->hl - 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_2C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc l
   inc8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_2D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec l
   dec8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_2E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,nn
    Z84_T_BEGIN(cpu);
    rf->l = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_2F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cpl
   rf->a ^= 0xFF;
   rf->f = (rf->f & ~(F3|F5)) | NF | HF | (rf->a & (F3|F5));
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_30(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jr nc, rr
    Z84_T_BEGIN(cpu);
    int8_t offset = Z84Rd(cpu, rf->pc++, true);

    if (!(rf->f & CF))
    {
        rf->pc += offset;
        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_31(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld sp,nnnn
    Z84_T_BEGIN(cpu);
    rf->spl = Z84Rd(cpu, rf->pc++, true);
    rf->sph = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_32(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (nnnn),a
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += Z84Rd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = ((addr + 1) & 0xFF) + (rf->a << 8);
    rf->memh = rf->a;

    Z84Wd(cpu, addr, rf->a);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_33(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc sp
	Z84_T_BEGIN(cpu);
	rf->sp = (rf->sp + 1) & 0xFFFF;

	cpuidle(Z84_IR(rf), 2);
	Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_34(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc (hl)
	Z84_T_BEGIN(cpu);
	uint8_t value = Z84Rd(cpu, rf->hl);
	inc8(rf, value);

	cpuidle(rf->hl, 1);

	Z84Wd(cpu, rf->hl, value);
	Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_35(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec (hl)
	Z84_T_BEGIN(cpu);
	uint8_t hl = Z84Rd(cpu, rf->hl);

	dec8(rf, hl);

	cpuidle(rf->hl, 1);

	Z84Wd(cpu, rf->hl, hl);
	Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_36(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),nn
    Z84_T_BEGIN(cpu);
    uint8_t value = Z84Rd(cpu, rf->pc++, true);
    Z84Wd(cpu, rf->hl, value);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_37(Z84CPU *cpu, int tact0, Z84Regs *rf) { // scf - Zilog Z80 behavior
    // Undocumented YF/XF flags: (A | (F & ~Q)) & 0x28
    // Q captures YF/XF from previous flag-modifying instruction (set in Z80::Z80Step)
    // When Q=F, only A contributes; when Q≠F, F also contributes
    uint8_t undoc = (rf->a | (rf->f & ~rf->q)) & (F3 | F5);
    rf->f = (rf->f & ~(HF | NF | F3 | F5)) | undoc | CF;

    // Update Q immediately - needed for back-to-back SCF/CCF sequences
    // where F value may not change but is still a flag-modifying instruction
    rf->q = rf->f & (F3 | F5);
    Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_38(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jr c,rr
    Z84_T_BEGIN(cpu);
    int8_t offset = Z84Rd(cpu, rf->pc, true);

    if ((rf->f & CF))
    {
        rf->pc += offset + 1;
        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    else
        rf->pc++;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_39(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add hl,sp
   Z84_T_BEGIN(cpu);
   rf->memptr = rf->hl + 1;

   int hl = rf->hl & 0xFFFF;
   int sp = rf->sp & 0xFFFF;

   int halfHL = hl & 0x0FFF;
   int halfSP = sp & 0x0FFF;

   // Clear result flags
   rf->f = (rf->f & ~(NF | CF | F5 | F3 | HF));

   // Set half-carry flag (HF)
   rf->f |= ((halfHL + halfSP) >> 8) & 0x10; // HF

   // Do add
   hl = hl + sp;

   // Handle carry flag (CF)
   if (hl & 0x10000)
	   rf->f |= CF;

   // Store result back to registers
   rf->f |= ((hl >> 8) & (F5 | F3));  // X/Y from result high byte
   rf->hl = hl & 0xFFFF;

   cpuidle(Z84_IR(rf), 7);
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_3A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,(nnnn)
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += Z84Rd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->a = Z84Rd(cpu, addr);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_3B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec sp
   Z84_T_BEGIN(cpu);
   rf->sp = (rf->sp - 1) & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_3C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // inc a
   inc8(rf, rf->a);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_3D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // dec a
   dec8(rf, rf->a);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_3E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,nn
    Z84_T_BEGIN(cpu);
    rf->a = Z84Rd(cpu, rf->pc++, true);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_3F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ccf - Zilog Z80 behavior
    // Undocumented YF/XF flags: (A | (F & ~Q)) & 0x28
    // When Q=F, only A contributes; when Q≠F, F also contributes
    uint8_t undoc = (rf->a | (rf->f & ~rf->q)) & (F3 | F5);
    rf->f = ((rf->f & ~(NF | HF | F3 | F5)) | ((rf->f << 4) & HF) | undoc) ^ CF;
    // Update Q immediately - needed for back-to-back SCF/CCF sequences
    // where F value may not change but is still a flag-modifying instruction
    rf->q = rf->f & (F3 | F5);
    Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_41(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,c
   rf->b = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_42(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,d
   rf->b = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_43(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,e
   rf->b = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_44(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,h
   rf->b = rf->h;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_45(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,l
   rf->b = rf->l;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_46(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,(hl)
   Z84_T_BEGIN(cpu);
   rf->b = Z84Rd(cpu, rf->hl);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_47(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld b,a
   rf->b = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_48(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,b
   rf->c = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_4A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,d
   rf->c = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_4B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,e
   rf->c = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_4C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,h
   rf->c = rf->h;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_4D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,l
   rf->c = rf->l;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_4E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,(hl)
   Z84_T_BEGIN(cpu);
   rf->c = Z84Rd(cpu, rf->hl);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_4F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld c,a
   rf->c = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_50(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,b
   rf->d = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_51(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,c
   rf->d = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_53(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,e
   rf->d = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_54(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,h
   rf->d = rf->h;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_55(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,l
   rf->d = rf->l;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_56(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,(hl)
   Z84_T_BEGIN(cpu);
   rf->d = Z84Rd(cpu, rf->hl);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_57(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld d,a
   rf->d = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_58(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,b
   rf->e = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_59(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,c
   rf->e = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_5A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,d
   rf->e = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_5C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,h
   rf->e = rf->h;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_5D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,l
   rf->e = rf->l;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_5E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,(hl)
   Z84_T_BEGIN(cpu);
   rf->e = Z84Rd(cpu, rf->hl);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_5F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld e,a
   rf->e = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_60(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,b
   rf->h = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_61(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,c
   rf->h = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_62(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,d
   rf->h = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_63(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,e
   rf->h = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_65(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,l
   rf->h = rf->l;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_66(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,(hl)
   Z84_T_BEGIN(cpu);
   rf->h = Z84Rd(cpu, rf->hl);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_67(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld h,a
   rf->h = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_68(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,b
   rf->l = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_69(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,c
   rf->l = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_6A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,d
   rf->l = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_6B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,e
   rf->l = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_6C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,h
   rf->l = rf->h;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_6E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,(hl)
   Z84_T_BEGIN(cpu);
   rf->l = Z84Rd(cpu, rf->hl);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_6F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld l,a
   rf->l = rf->a;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_70(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),b
   Z84_T_BEGIN(cpu);
   Z84Wd(cpu, rf->hl, rf->b); //Alone Coder
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_71(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),c
   Z84_T_BEGIN(cpu);
   Z84Wd(cpu, rf->hl, rf->c); //Alone Coder
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_72(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),d
   Z84_T_BEGIN(cpu);
   Z84Wd(cpu, rf->hl, rf->d); //Alone Coder
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_73(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),e
   Z84_T_BEGIN(cpu);
   Z84Wd(cpu, rf->hl, rf->e); //Alone Coder
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_74(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),h
   Z84_T_BEGIN(cpu);
   Z84Wd(cpu, rf->hl, rf->h); //Alone Coder
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_75(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),l
   Z84_T_BEGIN(cpu);
   Z84Wd(cpu, rf->hl, rf->l); //Alone Coder
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_76(Z84CPU *cpu, int tact0, Z84Regs *rf) { // halt
   Z84_T_BEGIN(cpu);
   // (the HALT-entry T is host bookkeeping: the engine keeps no haltpos)
   rf->pc--;  // Repeating execution until RESET, INT or NMI
   rf->halted = 1;
   cpu->halt_cycle = 0;
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_77(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld (hl),a
   Z84_T_BEGIN(cpu);
   Z84Wd(cpu, rf->hl, rf->a);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_78(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,b
   rf->a = rf->b;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_79(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,c
   rf->a = rf->c;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_7A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,d
   rf->a = rf->d;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_7B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,e
   rf->a = rf->e;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_7C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,h
   rf->a = rf->h;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_7D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,l
   rf->a = rf->l;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_7E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld a,(hl)
   Z84_T_BEGIN(cpu);
   rf->a = Z84Rd(cpu, rf->hl);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_80(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,b
   add8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_81(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,c
   add8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_82(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,d
   add8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}
Z84OPCODE op_83(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,e
   add8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_84(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,h
   add8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_85(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,l
   add8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_86(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,(hl)
   Z84_T_BEGIN(cpu);
   add8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_87(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,a
   add8(rf, rf->a);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_88(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,b
   adc8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_89(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,c
   adc8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_8A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,d
   adc8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_8B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,e
   adc8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_8C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,h
   adc8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_8D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,l
   adc8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}
Z84OPCODE op_8E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,(hl)
   Z84_T_BEGIN(cpu);
   adc8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_8F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,a
   adc8(rf, rf->a);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_90(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub b
   sub8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_91(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub c
   sub8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_92(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub d
   sub8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_93(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub e
   sub8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_94(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub h
   sub8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_95(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub l
   sub8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_96(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub (hl)
   Z84_T_BEGIN(cpu);
   sub8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_97(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub a
   rf->af = ZF | NF;
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_98(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,b
   sbc8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_99(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,c
   sbc8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_9A(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,d
   sbc8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_9B(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,e
   sbc8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_9C(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,h
   sbc8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_9D(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,l
   sbc8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_9E(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,(hl)
   Z84_T_BEGIN(cpu);
   sbc8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_9F(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,a
   sbc8(rf, rf->a);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A0(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and b
   and8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A1(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and c
   and8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A2(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and d
   and8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A3(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and e
   and8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and h
   and8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and l
   and8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and (hl)
   Z84_T_BEGIN(cpu);
   and8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_A7(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and a
   and8(rf, rf->a);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A8(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor b
   xor8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_A9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor c
   xor8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_AA(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor d
   xor8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_AB(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor e
   xor8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_AC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor h
   xor8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_AD(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor l
   xor8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_AE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor (hl)
   Z84_T_BEGIN(cpu);
   xor8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_AF(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor a
   rf->af = ZF | PV;
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B0(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or b
   or8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B1(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or c
   or8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B2(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or d
   or8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B3(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or e
   or8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or h
   or8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or l
   or8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or (hl)
   Z84_T_BEGIN(cpu);
   or8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_B7(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or a
   or8(rf, rf->a);  // already optimized by compiler
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B8(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp b
   cp8(rf, rf->b);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_B9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp c
   cp8(rf, rf->c);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_BA(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp d
   cp8(rf, rf->d);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_BB(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp e
   cp8(rf, rf->e);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_BC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp h
   cp8(rf, rf->h);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_BD(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp l
   cp8(rf, rf->l);
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_BE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp (hl)
   Z84_T_BEGIN(cpu);
   cp8(rf, Z84Rd(cpu, rf->hl));
   Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_BF(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp a
   cp8(rf, rf->a); // can't optimize: F3,F5 depends on A
   Z84_RETURN_Q(0x28, tact0);
}

Z84OPCODE op_C0(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret nz
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    if (!(rf->f & ZF))
    {
        uint16_t addr = Z84Rd(cpu, rf->sp++);
        addr += 0x100 * Z84Rd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C1(Z84CPU *cpu, int tact0, Z84Regs *rf) { // pop bc
    Z84_T_BEGIN(cpu);
    rf->c = Z84Rd(cpu, rf->sp++);
    rf->b = Z84Rd(cpu, rf->sp++);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C2(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp nz,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & ZF))
    {
        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C3(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp nnnn
   Z84_T_BEGIN(cpu);

   uint8_t lo = Z84Rd(cpu, rf->pc, true);
   uint16_t hi = Z84Rd(cpu, rf->pc + 1, true) << 8;

   rf->pc = hi | lo;
   rf->memptr = rf->pc;
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call nz,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr = 0x100 * Z84Rd(cpu, rf->pc++, true) + addr;

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & ZF))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);

        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // push bc
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->b);
    Z84Wd(cpu, --rf->sp, rf->c);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // add a,nn
    Z84_T_BEGIN(cpu);
    add8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_C7(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 00
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x0000;
    rf->memptr = 0x0000;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C8(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret z
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    if (rf->f & ZF)
    {
        uint8_t lo = Z84Rd(cpu, rf->sp++);
        uint16_t hi = Z84Rd(cpu, rf->sp++) << 8;
        uint16_t addr = hi | lo;


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_C9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret
    Z84_T_BEGIN(cpu);
    uint8_t loAddr = Z84Rd(cpu, rf->sp++);
    uint8_t hiAddr = Z84Rd(cpu, rf->sp++);
    uint16_t addr = (hiAddr << 8) | loAddr;


    rf->pc = addr;
    rf->memptr = addr;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_CA(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp z,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & ZF)
    {
        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_CC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call z,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & ZF)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte
        Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);

        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_CD(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call nnnn
    Z84_T_BEGIN(cpu);

    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    cpuidle(rf->pc - 1, 1);  // on the address's high byte

    // Put return address to stack
    Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
    Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);

    rf->pc = addr;
    rf->memptr = addr;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_CE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // adc a,nn
    Z84_T_BEGIN(cpu);
    adc8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_CF(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 08
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x08;
    rf->memptr = 0x08;
    rf->memh = 0;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D0(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret nc
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    if (!(rf->f & CF))
    {
        uint16_t addr = Z84Rd(cpu, rf->sp++);
        addr += 0x100 * Z84Rd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D1(Z84CPU *cpu, int tact0, Z84Regs *rf) { // pop de
    Z84_T_BEGIN(cpu);
    rf->e = Z84Rd(cpu, rf->sp++);
    rf->d = Z84Rd(cpu, rf->sp++);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D2(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp nc,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & CF))
    {
        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

// The full 16-bit address bus is used, not just the 8-bit n value.
// The high byte (A15-A8) of the address bus is set to the contents of the accumulator (A).
// The low byte (A7-A0) of the address bus is set to the 8-bit port number n (from the instruction).
// So, the actual 16-bit address placed on the address bus is:
// A15-A8 = A (accumulator value)
// A7-A0 = n (port number from instruction)
// → (A << 8) | n
Z84OPCODE op_D3(Z84CPU *cpu, int tact0, Z84Regs *rf) { // out (n),a
    Z84_T_BEGIN(cpu);
    uint16_t port = Z84Rd(cpu, rf->pc++, true);

    rf->memptr = ((port + 1) & 0xFF) + (rf->a << 8);

    // IO write: Z80 IO machine cycles assert IORQ/WR one clock late - at T2 of
    // the 4T IO cycle (T1 carries no IORQ; Z80 UM "the CPU automatically inserts
    // one wait state" before IO). The port write must land on that T-state, not
    // at the IO cycle entry: charging 1T first places the SetBorderColor() flush
    // exactly at the IORQ T-state. At the cycle entry border changes render 1T
    // (2 px) early - Pentagon updates border every 1T (MiSTer ula.sv line 185),
    // which makes the offset clearly visible in border-synced effects.
    cputact(1);
    Z84Pout(cpu, port + (rf->a << 8), rf->a);
    cputact(3);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call nc,nnnn
    Z84_T_BEGIN(cpu);
    uint8_t loAddr = Z84Rd(cpu, rf->pc++, true);
    uint8_t hiAddr = Z84Rd(cpu, rf->pc++, true);
    uint16_t addr = (hiAddr << 8) | loAddr;

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & CF))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // push de
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->d);
    Z84Wd(cpu, --rf->sp, rf->e);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sub nn
    Z84_T_BEGIN(cpu);
    sub8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_D7(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 10
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x10;
    rf->memptr = 0x10;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D8(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret c
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    // Branch taken
    if (rf->f & CF)
    {
        uint16_t addr = Z84Rd(cpu, rf->sp++);
        addr += 0x100 * Z84Rd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_D9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // exx
    // Exchange data between main and shadow register sets
    uint16_t tmp = rf->bc; rf->bc = rf->alt.bc; rf->alt.bc = tmp;
    tmp = rf->de; rf->de = rf->alt.de; rf->alt.de = tmp;
    tmp = rf->hl; rf->hl = rf->alt.hl; rf->alt.hl = tmp;
    Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_DA(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp c,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (rf->f & CF)
    {

        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_DB(Z84CPU *cpu, int tact0, Z84Regs *rf) { // in a,(nn)
    Z84_T_BEGIN(cpu);
    uint16_t port = Z84Rd(cpu, rf->pc++, true) + (rf->a << 8);

    // MEMPTR = full port address + 1 ('port' already includes A on the high
    // byte - adding A<<8 again double-counted it; FUSE-verified)
    rf->memptr = port + 1;

    // IO read: IORQ/RD assert at T2 of the IO cycle - sample the port there,
    // not at the cycle entry (see op_D3).
    cputact(1);
    rf->a = Z84Pin(cpu, port);
    cputact(3);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_DC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call c,nnnn
    Z84_T_BEGIN(cpu);
    uint8_t loAddr = Z84Rd(cpu, rf->pc++, true);
    uint8_t hiAddr = Z84Rd(cpu, rf->pc++, true);
    uint16_t addr = (hiAddr << 8) | loAddr;

    rf->memptr = addr;

    // Branch taken
    if (rf->f & CF)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);

        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_DE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // sbc a,nn
    Z84_T_BEGIN(cpu);
    sbc8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_DF(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 18
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x18;
    rf->memptr = 0x18;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E0(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret po
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    // Branch taken
    if (!(rf->f & PV))
    {
        uint16_t addr = Z84Rd(cpu, rf->sp++);
        addr += 0x100 * Z84Rd(cpu, rf->sp++);

        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E1(Z84CPU *cpu, int tact0, Z84Regs *rf) { // pop hl
    Z84_T_BEGIN(cpu);
    rf->l = Z84Rd(cpu, rf->sp++);
    rf->h = Z84Rd(cpu, rf->sp++);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E2(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp po,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & PV))
    {
        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E3(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ex (sp),hl
    Z84_T_BEGIN(cpu);
    uint16_t value = Z84Rd(cpu, rf->sp) + 0x100 * Z84Rd(cpu, rf->sp + 1);

    cpuidle(rf->sp + 1, 1);

    // Real Z80 write order: high byte (SP+1) first, then low (SP) - FUSE-verified
    Z84Wd(cpu, rf->sp + 1, rf->h);
    Z84Wd(cpu, rf->sp, rf->l);

    rf->memptr = value;

    rf->hl = value;

    cpuidle(rf->sp, 2);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call po,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & PV))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

          Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // push hl
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->h);
    Z84Wd(cpu, --rf->sp, rf->l);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // and nn
    Z84_T_BEGIN(cpu);
    and8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_E7(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 20
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x20;
    rf->memptr = 0x20;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E8(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret pe
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    if (rf->f & PV)
    {
        uint16_t addr = Z84Rd(cpu, rf->sp++);
        addr += 0x100 * Z84Rd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_E9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp (hl)

   rf->pc = rf->hl;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_EA(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp pe,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;
    if (rf->f & PV)
    {

        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_EB(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ex de,hl
   uint16_t de = rf->de;
   rf->de = rf->hl;
   rf->hl = de;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_EC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call pe,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & PV)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_EE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // xor nn
    Z84_T_BEGIN(cpu);
    xor8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_EF(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 28
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x28;
    rf->memptr = 0x28;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F0(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret p
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    if (!(rf->f & SF))
    {
        uint16_t addr = Z84Rd(cpu, rf->sp++);
        addr += 0x100 * Z84Rd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F1(Z84CPU *cpu, int tact0, Z84Regs *rf) { // pop af
    Z84_T_BEGIN(cpu);
    rf->f = Z84Rd(cpu, rf->sp++);
    rf->a = Z84Rd(cpu, rf->sp++);

    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F2(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp p,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & SF))
    {

        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F3(Z84CPU *cpu, int tact0, Z84Regs *rf) { // di
   rf->iff1 = 0;
   rf->iff2 = 0;
   Z84_RETURN_Q(0, tact0);
}

Z84OPCODE op_F4(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call p,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & SF))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F5(Z84CPU *cpu, int tact0, Z84Regs *rf) { // push af
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->a);
    Z84Wd(cpu, --rf->sp, rf->f);
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F6(Z84CPU *cpu, int tact0, Z84Regs *rf) { // or nn
    Z84_T_BEGIN(cpu);
    or8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_F7(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 30
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x30;
    rf->memptr = 0x30;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F8(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ret m
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    if (rf->f & SF)
    {
        uint16_t addr = Z84Rd(cpu, rf->sp++);
        addr += 0x100 * Z84Rd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_F9(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ld sp,hl
   Z84_T_BEGIN(cpu);
   rf->sp = rf->hl & 0xFFFF;

   cpuidle(Z84_IR(rf), 2);
   Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_FA(Z84CPU *cpu, int tact0, Z84Regs *rf) { // jp m,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (rf->f & SF)
    {

        rf->pc = addr;
    }
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_FB(Z84CPU *cpu, int tact0, Z84Regs *rf) { // ei
    Z84_T_BEGIN(cpu);
    rf->iff1 = 1;
    rf->iff2 = 1;

    // The EI shadow is derived from the last opcode word (Z84CpuIntPossible);
    // the engine keeps no eipos (host bookkeeping in an attached block).
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_FC(Z84CPU *cpu, int tact0, Z84Regs *rf) { // call m,nnnn
    Z84_T_BEGIN(cpu);
    uint16_t addr = Z84Rd(cpu, rf->pc++, true);
    addr += 0x100 * Z84Rd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & SF)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z84Wd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z84Wd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_FE(Z84CPU *cpu, int tact0, Z84Regs *rf) { // cp nn
    Z84_T_BEGIN(cpu);
    cp8(rf, Z84Rd(cpu, rf->pc++, true));
    Z84_RETURN_Q(0x28, tact_);
}

Z84OPCODE op_FF(Z84CPU *cpu, int tact0, Z84Regs *rf) { // rst 38
    Z84_T_BEGIN(cpu);
    cpuidle(Z84_IR(rf), 1);

    Z84Wd(cpu, --rf->sp, rf->pch);
    Z84Wd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x38;
    rf->memptr = 0x38;
    Z84_RETURN_Q(0, tact_);
}

Z84OPCODE op_CB(Z84CPU *cpu, int);
Z84OPCODE op_ED(Z84CPU *cpu, int);
Z84OPCODE op_DD(Z84CPU *cpu, int);
Z84OPCODE op_FD(Z84CPU *cpu, int);

STEPFUNC const normal_opcode[0x100] =
{
   op_00, op_01, op_02, op_03, op_04, op_05, op_06, op_07,
   op_08, op_09, op_0A, op_0B, op_0C, op_0D, op_0E, op_0F,
   op_10, op_11, op_12, op_13, op_14, op_15, op_16, op_17,
   op_18, op_19, op_1A, op_1B, op_1C, op_1D, op_1E, op_1F,
   op_20, op_21, op_22, op_23, op_24, op_25, op_26, op_27,
   op_28, op_29, op_2A, op_2B, op_2C, op_2D, op_2E, op_2F,
   op_30, op_31, op_32, op_33, op_34, op_35, op_36, op_37,
   op_38, op_39, op_3A, op_3B, op_3C, op_3D, op_3E, op_3F,

   op_40, op_41, op_42, op_43, op_44, op_45, op_46, op_47,
   op_48, op_49, op_4A, op_4B, op_4C, op_4D, op_4E, op_4F,
   op_50, op_51, op_52, op_53, op_54, op_55, op_56, op_57,
   op_58, op_59, op_5A, op_5B, op_5C, op_5D, op_5E, op_5F,
   op_60, op_61, op_62, op_63, op_64, op_65, op_66, op_67,
   op_68, op_69, op_6A, op_6B, op_6C, op_6D, op_6E, op_6F,
   op_70, op_71, op_72, op_73, op_74, op_75, op_76, op_77,
   op_78, op_79, op_7A, op_7B, op_7C, op_7D, op_7E, op_7F,

   op_80, op_81, op_82, op_83, op_84, op_85, op_86, op_87,
   op_88, op_89, op_8A, op_8B, op_8C, op_8D, op_8E, op_8F,
   op_90, op_91, op_92, op_93, op_94, op_95, op_96, op_97,
   op_98, op_99, op_9A, op_9B, op_9C, op_9D, op_9E, op_9F,
   op_A0, op_A1, op_A2, op_A3, op_A4, op_A5, op_A6, op_A7,
   op_A8, op_A9, op_AA, op_AB, op_AC, op_AD, op_AE, op_AF,
   op_B0, op_B1, op_B2, op_B3, op_B4, op_B5, op_B6, op_B7,
   op_B8, op_B9, op_BA, op_BB, op_BC, op_BD, op_BE, op_BF,

   op_C0, op_C1, op_C2, op_C3, op_C4, op_C5, op_C6, op_C7,
   op_C8, op_C9, op_CA, op_CB, op_CC, op_CD, op_CE, op_CF,
   op_D0, op_D1, op_D2, op_D3, op_D4, op_D5, op_D6, op_D7,
   op_D8, op_D9, op_DA, op_DB, op_DC, op_DD, op_DE, op_DF,
   op_E0, op_E1, op_E2, op_E3, op_E4, op_E5, op_E6, op_E7,
   op_E8, op_E9, op_EA, op_EB, op_EC, op_ED, op_EE, op_EF,
   op_F0, op_F1, op_F2, op_F3, op_F4, op_F5, op_F6, op_F7,
   op_F8, op_F9, op_FA, op_FB, op_FC, op_FD, op_FE, op_FF,
};

