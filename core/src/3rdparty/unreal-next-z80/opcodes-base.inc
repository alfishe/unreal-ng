// opcodes-base.cpp - no-prefix opcodes, ported from core op_noprefix.cpp.


//  Non-prefixed opcodes
// Important note: M1 cycle lasts 4 clocks. This is the bare minimum for any opcode. So only additional deltas added here with cputact(n) macro

Z80NOPCODE op_00(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // nop [4]
	// No increment for CPU cycles counter (t) is required
	// 4 cycles already spent in m1_cycle();
	Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_01(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld bc,nnnn [10]
    Z80N_T_BEGIN(cpu);
    rf->c = Z80nRd(cpu, rf->pc++, true);
    rf->b = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_02(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (bc),a [7]
   Z80N_T_BEGIN(cpu);
   // MEMPTR: low byte = (BC+1) & 0xFF, high byte = A (FUSE-verified)
   rf->memptr = ((rf->bc + 1) & 0xFF) | (rf->a << 8);
   Z80nWd(cpu, rf->bc, rf->a);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_03(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc bc
   Z80N_T_BEGIN(cpu);
   rf->bc = (rf->bc + 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_04(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc b
   inc8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_05(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec b
   dec8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_06(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,nn
    Z80N_T_BEGIN(cpu);
    rf->b = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_07(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rlca
   rf->f = rlca_f[rf->a] | (rf->f & (SF | ZF | PV));
   rf->a = rol[rf->a];
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_08(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ex af,af'
   uint16_t mainAF = rf->af;
   rf->af = rf->alt.af;
   rf->alt.af = mainAF;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_09(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add hl,bc
    Z80N_T_BEGIN(cpu);
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

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_0A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,(bc)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->bc + 1;
   rf->a = Z80nRd(cpu, rf->bc);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_0B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec bc
   Z80N_T_BEGIN(cpu);
   rf->bc = (rf->bc - 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_0C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc c
   inc8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_0D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec c
   dec8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_0E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,nn
    Z80N_T_BEGIN(cpu);
    rf->c = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_0F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rrca
   rf->f = rrca_f[rf->a] | (rf->f & (SF | ZF | PV));
   rf->a = ror[rf->a];
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_10(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // djnz rr
	Z80N_T_BEGIN(cpu);
	cpuidle(Z80N_IR(rf), 1);

	int8_t offset = Z80nRd(cpu, rf->pc++, true);  // instruction-stream byte (m1State, Operand kind)

	// Branch taken
	if (--rf->b)
	{

        rf->pc += offset;
		rf->memptr = rf->pc;

		cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
	}
	Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_11(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld de,nnnn
    Z80N_T_BEGIN(cpu);
    rf->e = Z80nRd(cpu, rf->pc++, true);
    rf->d = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_12(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (de),a
   Z80N_T_BEGIN(cpu);
   // MEMPTR: low byte = (DE+1) & 0xFF, high byte = A (FUSE-verified)
   rf->memptr = ((rf->de + 1) & 0xFF) | (rf->a << 8);
   Z80nWd(cpu, rf->de, rf->a);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_13(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc de
   Z80N_T_BEGIN(cpu);
   rf->de = (rf->de + 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_14(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc d
   inc8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_15(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec d
   dec8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_16(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,nn
    Z80N_T_BEGIN(cpu);
    rf->d = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_17(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rla
   uint8_t new_a = (rf->a << 1) + (rf->f & 1);

   rf->f = rlca_f[rf->a] | (rf->f & (SF | ZF | PV)); // use same table with rlca
   rf->a = new_a;
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_18(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jr rr
    Z80N_T_BEGIN(cpu);
    int8_t offset = Z80nRd(cpu, rf->pc++, true);


    rf->pc += offset;
    rf->memptr = rf->pc;

    cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_19(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add hl,de
    Z80N_T_BEGIN(cpu);
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

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_1A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,(de)
   Z80N_T_BEGIN(cpu);
   rf->memptr = rf->de + 1;

   rf->a = Z80nRd(cpu, rf->de);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_1B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec de
   Z80N_T_BEGIN(cpu);
   rf->de = (rf->de - 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_1C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc e
   inc8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_1D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec e
   dec8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_1E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,nn
    Z80N_T_BEGIN(cpu);
    rf->e = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_1F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rra
   uint8_t new_a = (rf->a >> 1) + (rf->f << 7);

   rf->f = rrca_f[rf->a] | (rf->f & (SF | ZF | PV)); // use same table with rrca
   rf->a = new_a;
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_20(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jr nz, rr
    Z80N_T_BEGIN(cpu);
    int8_t offset = Z80nRd(cpu, rf->pc++, true);

    if (!(rf->f & ZF))
    {
        rf->pc += offset;

        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_21(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld hl,nnnn
    Z80N_T_BEGIN(cpu);
    rf->l = Z80nRd(cpu, rf->pc++, true);
    rf->h = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_22(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (nnnn),hl
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += Z80nRd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = addr + 1;

    Z80nWd(cpu, addr, rf->l);
    Z80nWd(cpu, addr + 1, rf->h);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_23(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc hl
   Z80N_T_BEGIN(cpu);
   rf->hl = (rf->hl + 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_24(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc h
   inc8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_25(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec h
   dec8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_26(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,nn
    Z80N_T_BEGIN(cpu);
    rf->h = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_27(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // daa
   rf->af = *(uint16_t *)(daatab + (rf->a + 0x100 * ((rf->f & 3) + ((rf->f >> 2) & 4))) * 2);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_28(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jr z,rr
    Z80N_T_BEGIN(cpu);
    int8_t offset = Z80nRd(cpu, rf->pc++, true);

    // Branch taken
    if ((rf->f & ZF))
    {
        rf->pc += offset;
        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_29(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add hl,hl
    Z80N_T_BEGIN(cpu);
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

    cpuidle(Z80N_IR(rf), 7);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_2A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld hl,(nnnn)
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += Z80nRd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->l = Z80nRd(cpu, addr);
    rf->h = Z80nRd(cpu, addr + 1);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_2B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec hl
   Z80N_T_BEGIN(cpu);
   rf->hl = (rf->hl - 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_2C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc l
   inc8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_2D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec l
   dec8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_2E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,nn
    Z80N_T_BEGIN(cpu);
    rf->l = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_2F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cpl
   rf->a ^= 0xFF;
   rf->f = (rf->f & ~(F3|F5)) | NF | HF | (rf->a & (F3|F5));
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_30(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jr nc, rr
    Z80N_T_BEGIN(cpu);
    int8_t offset = Z80nRd(cpu, rf->pc++, true);

    if (!(rf->f & CF))
    {
        rf->pc += offset;
        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_31(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld sp,nnnn
    Z80N_T_BEGIN(cpu);
    rf->spl = Z80nRd(cpu, rf->pc++, true);
    rf->sph = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_32(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (nnnn),a
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += Z80nRd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = ((addr + 1) & 0xFF) + (rf->a << 8);
    rf->memh = rf->a;

    Z80nWd(cpu, addr, rf->a);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_33(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc sp
	Z80N_T_BEGIN(cpu);
	rf->sp = (rf->sp + 1) & 0xFFFF;

	cpuidle(Z80N_IR(rf), 2);
	Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_34(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc (hl)
	Z80N_T_BEGIN(cpu);
	uint8_t value = Z80nRd(cpu, rf->hl);
	inc8(rf, value);

	cpuidle(rf->hl, 1);

	Z80nWd(cpu, rf->hl, value);
	Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_35(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec (hl)
	Z80N_T_BEGIN(cpu);
	uint8_t hl = Z80nRd(cpu, rf->hl);

	dec8(rf, hl);

	cpuidle(rf->hl, 1);

	Z80nWd(cpu, rf->hl, hl);
	Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_36(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),nn
    Z80N_T_BEGIN(cpu);
    uint8_t value = Z80nRd(cpu, rf->pc++, true);
    Z80nWd(cpu, rf->hl, value);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_37(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // scf - Zilog Z80 behavior
    // Undocumented YF/XF flags: (A | (F & ~Q)) & 0x28
    // Q captures YF/XF from previous flag-modifying instruction (set in Z80::Z80Step)
    // When Q=F, only A contributes; when Q≠F, F also contributes
    uint8_t undoc = (rf->a | (rf->f & ~rf->q)) & (F3 | F5);
    rf->f = (rf->f & ~(HF | NF | F3 | F5)) | undoc | CF;

    // Update Q immediately - needed for back-to-back SCF/CCF sequences
    // where F value may not change but is still a flag-modifying instruction
    rf->q = rf->f & (F3 | F5);
    Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_38(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jr c,rr
    Z80N_T_BEGIN(cpu);
    int8_t offset = Z80nRd(cpu, rf->pc, true);

    if ((rf->f & CF))
    {
        rf->pc += offset + 1;
        rf->memptr = rf->pc;

        cpuidle(rf->pc - offset - 1, 5);  // on the displacement's address
    }
    else
        rf->pc++;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_39(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add hl,sp
   Z80N_T_BEGIN(cpu);
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

   cpuidle(Z80N_IR(rf), 7);
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_3A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,(nnnn)
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += Z80nRd(cpu, rf->pc++, true) * 0x100;

    rf->memptr = addr + 1;

    rf->a = Z80nRd(cpu, addr);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_3B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec sp
   Z80N_T_BEGIN(cpu);
   rf->sp = (rf->sp - 1) & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_3C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // inc a
   inc8(rf, rf->a);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_3D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // dec a
   dec8(rf, rf->a);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_3E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,nn
    Z80N_T_BEGIN(cpu);
    rf->a = Z80nRd(cpu, rf->pc++, true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_3F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ccf - Zilog Z80 behavior
    // Undocumented YF/XF flags: (A | (F & ~Q)) & 0x28
    // When Q=F, only A contributes; when Q≠F, F also contributes
    uint8_t undoc = (rf->a | (rf->f & ~rf->q)) & (F3 | F5);
    rf->f = ((rf->f & ~(NF | HF | F3 | F5)) | ((rf->f << 4) & HF) | undoc) ^ CF;
    // Update Q immediately - needed for back-to-back SCF/CCF sequences
    // where F value may not change but is still a flag-modifying instruction
    rf->q = rf->f & (F3 | F5);
    Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_41(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,c
   rf->b = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_42(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,d
   rf->b = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_43(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,e
   rf->b = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_44(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,h
   rf->b = rf->h;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_45(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,l
   rf->b = rf->l;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_46(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,(hl)
   Z80N_T_BEGIN(cpu);
   rf->b = Z80nRd(cpu, rf->hl);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_47(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld b,a
   rf->b = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_48(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,b
   rf->c = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_4A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,d
   rf->c = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_4B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,e
   rf->c = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_4C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,h
   rf->c = rf->h;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_4D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,l
   rf->c = rf->l;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_4E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,(hl)
   Z80N_T_BEGIN(cpu);
   rf->c = Z80nRd(cpu, rf->hl);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_4F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld c,a
   rf->c = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_50(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,b
   rf->d = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_51(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,c
   rf->d = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_53(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,e
   rf->d = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_54(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,h
   rf->d = rf->h;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_55(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,l
   rf->d = rf->l;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_56(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,(hl)
   Z80N_T_BEGIN(cpu);
   rf->d = Z80nRd(cpu, rf->hl);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_57(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld d,a
   rf->d = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_58(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,b
   rf->e = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_59(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,c
   rf->e = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_5A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,d
   rf->e = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_5C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,h
   rf->e = rf->h;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_5D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,l
   rf->e = rf->l;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_5E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,(hl)
   Z80N_T_BEGIN(cpu);
   rf->e = Z80nRd(cpu, rf->hl);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_5F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld e,a
   rf->e = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_60(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,b
   rf->h = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_61(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,c
   rf->h = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_62(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,d
   rf->h = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_63(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,e
   rf->h = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_65(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,l
   rf->h = rf->l;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_66(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,(hl)
   Z80N_T_BEGIN(cpu);
   rf->h = Z80nRd(cpu, rf->hl);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_67(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld h,a
   rf->h = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_68(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,b
   rf->l = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_69(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,c
   rf->l = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_6A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,d
   rf->l = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_6B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,e
   rf->l = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_6C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,h
   rf->l = rf->h;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_6E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,(hl)
   Z80N_T_BEGIN(cpu);
   rf->l = Z80nRd(cpu, rf->hl);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_6F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld l,a
   rf->l = rf->a;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_70(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),b
   Z80N_T_BEGIN(cpu);
   Z80nWd(cpu, rf->hl, rf->b); //Alone Coder
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_71(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),c
   Z80N_T_BEGIN(cpu);
   Z80nWd(cpu, rf->hl, rf->c); //Alone Coder
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_72(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),d
   Z80N_T_BEGIN(cpu);
   Z80nWd(cpu, rf->hl, rf->d); //Alone Coder
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_73(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),e
   Z80N_T_BEGIN(cpu);
   Z80nWd(cpu, rf->hl, rf->e); //Alone Coder
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_74(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),h
   Z80N_T_BEGIN(cpu);
   Z80nWd(cpu, rf->hl, rf->h); //Alone Coder
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_75(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),l
   Z80N_T_BEGIN(cpu);
   Z80nWd(cpu, rf->hl, rf->l); //Alone Coder
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_76(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // halt
   Z80N_T_BEGIN(cpu);
   // (the HALT-entry T is host bookkeeping: the engine keeps no haltpos)
   rf->pc--;  // Repeating execution until RESET, INT or NMI
   rf->halted = 1;
   cpu->halt_cycle = 0;
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_77(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld (hl),a
   Z80N_T_BEGIN(cpu);
   Z80nWd(cpu, rf->hl, rf->a);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_78(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,b
   rf->a = rf->b;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_79(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,c
   rf->a = rf->c;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_7A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,d
   rf->a = rf->d;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_7B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,e
   rf->a = rf->e;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_7C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,h
   rf->a = rf->h;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_7D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,l
   rf->a = rf->l;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_7E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld a,(hl)
   Z80N_T_BEGIN(cpu);
   rf->a = Z80nRd(cpu, rf->hl);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_80(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,b
   add8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_81(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,c
   add8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_82(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,d
   add8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}
Z80NOPCODE op_83(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,e
   add8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_84(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,h
   add8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_85(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,l
   add8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_86(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,(hl)
   Z80N_T_BEGIN(cpu);
   add8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_87(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,a
   add8(rf, rf->a);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_88(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,b
   adc8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_89(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,c
   adc8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_8A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,d
   adc8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_8B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,e
   adc8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_8C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,h
   adc8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_8D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,l
   adc8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}
Z80NOPCODE op_8E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,(hl)
   Z80N_T_BEGIN(cpu);
   adc8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_8F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,a
   adc8(rf, rf->a);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_90(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub b
   sub8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_91(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub c
   sub8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_92(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub d
   sub8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_93(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub e
   sub8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_94(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub h
   sub8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_95(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub l
   sub8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_96(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub (hl)
   Z80N_T_BEGIN(cpu);
   sub8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_97(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub a
   rf->af = ZF | NF;
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_98(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,b
   sbc8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_99(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,c
   sbc8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_9A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,d
   sbc8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_9B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,e
   sbc8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_9C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,h
   sbc8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_9D(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,l
   sbc8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_9E(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,(hl)
   Z80N_T_BEGIN(cpu);
   sbc8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_9F(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,a
   sbc8(rf, rf->a);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and b
   and8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and c
   and8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and d
   and8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and e
   and8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and h
   and8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and l
   and8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and (hl)
   Z80N_T_BEGIN(cpu);
   and8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_A7(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and a
   and8(rf, rf->a);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor b
   xor8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_A9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor c
   xor8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_AA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor d
   xor8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_AB(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor e
   xor8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_AC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor h
   xor8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_AD(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor l
   xor8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_AE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor (hl)
   Z80N_T_BEGIN(cpu);
   xor8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_AF(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor a
   rf->af = ZF | PV;
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or b
   or8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or c
   or8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or d
   or8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or e
   or8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or h
   or8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or l
   or8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or (hl)
   Z80N_T_BEGIN(cpu);
   or8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_B7(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or a
   or8(rf, rf->a);  // already optimized by compiler
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp b
   cp8(rf, rf->b);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_B9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp c
   cp8(rf, rf->c);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_BA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp d
   cp8(rf, rf->d);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_BB(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp e
   cp8(rf, rf->e);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_BC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp h
   cp8(rf, rf->h);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_BD(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp l
   cp8(rf, rf->l);
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_BE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp (hl)
   Z80N_T_BEGIN(cpu);
   cp8(rf, Z80nRd(cpu, rf->hl));
   Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_BF(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp a
   cp8(rf, rf->a); // can't optimize: F3,F5 depends on A
   Z80N_RETURN_Q(0x28, tact0);
}

Z80NOPCODE op_C0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret nz
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    if (!(rf->f & ZF))
    {
        uint16_t addr = Z80nRd(cpu, rf->sp++);
        addr += 0x100 * Z80nRd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // pop bc
    Z80N_T_BEGIN(cpu);
    rf->c = Z80nRd(cpu, rf->sp++);
    rf->b = Z80nRd(cpu, rf->sp++);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp nz,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & ZF))
    {
        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp nnnn
   Z80N_T_BEGIN(cpu);

   uint8_t lo = Z80nRd(cpu, rf->pc, true);
   uint16_t hi = Z80nRd(cpu, rf->pc + 1, true) << 8;

   rf->pc = hi | lo;
   rf->memptr = rf->pc;
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call nz,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr = 0x100 * Z80nRd(cpu, rf->pc++, true) + addr;

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & ZF))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);

        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // push bc
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->b);
    Z80nWd(cpu, --rf->sp, rf->c);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add a,nn
    Z80N_T_BEGIN(cpu);
    add8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_C7(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 00
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x0000;
    rf->memptr = 0x0000;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret z
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    if (rf->f & ZF)
    {
        uint8_t lo = Z80nRd(cpu, rf->sp++);
        uint16_t hi = Z80nRd(cpu, rf->sp++) << 8;
        uint16_t addr = hi | lo;


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_C9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret
    Z80N_T_BEGIN(cpu);
    uint8_t loAddr = Z80nRd(cpu, rf->sp++);
    uint8_t hiAddr = Z80nRd(cpu, rf->sp++);
    uint16_t addr = (hiAddr << 8) | loAddr;


    rf->pc = addr;
    rf->memptr = addr;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_CA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp z,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & ZF)
    {
        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_CC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call z,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & ZF)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte
        Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);

        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_CD(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call nnnn
    Z80N_T_BEGIN(cpu);

    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    cpuidle(rf->pc - 1, 1);  // on the address's high byte

    // Put return address to stack
    Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
    Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);

    rf->pc = addr;
    rf->memptr = addr;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_CE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // adc a,nn
    Z80N_T_BEGIN(cpu);
    adc8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_CF(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 08
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x08;
    rf->memptr = 0x08;
    rf->memh = 0;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret nc
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    if (!(rf->f & CF))
    {
        uint16_t addr = Z80nRd(cpu, rf->sp++);
        addr += 0x100 * Z80nRd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // pop de
    Z80N_T_BEGIN(cpu);
    rf->e = Z80nRd(cpu, rf->sp++);
    rf->d = Z80nRd(cpu, rf->sp++);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp nc,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & CF))
    {
        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

// The full 16-bit address bus is used, not just the 8-bit n value.
// The high byte (A15-A8) of the address bus is set to the contents of the accumulator (A).
// The low byte (A7-A0) of the address bus is set to the 8-bit port number n (from the instruction).
// So, the actual 16-bit address placed on the address bus is:
// A15-A8 = A (accumulator value)
// A7-A0 = n (port number from instruction)
// → (A << 8) | n
Z80NOPCODE op_D3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // out (n),a
    Z80N_T_BEGIN(cpu);
    uint16_t port = Z80nRd(cpu, rf->pc++, true);

    rf->memptr = ((port + 1) & 0xFF) + (rf->a << 8);

    // IO write: Z80 IO machine cycles assert IORQ/WR one clock late - at T2 of
    // the 4T IO cycle (T1 carries no IORQ; Z80 UM "the CPU automatically inserts
    // one wait state" before IO). The port write must land on that T-state, not
    // at the IO cycle entry: charging 1T first places the SetBorderColor() flush
    // exactly at the IORQ T-state. At the cycle entry border changes render 1T
    // (2 px) early - Pentagon updates border every 1T (MiSTer ula.sv line 185),
    // which makes the offset clearly visible in border-synced effects.
    cputact(1);
    Z80nPout(cpu, port + (rf->a << 8), rf->a);
    cputact(3);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call nc,nnnn
    Z80N_T_BEGIN(cpu);
    uint8_t loAddr = Z80nRd(cpu, rf->pc++, true);
    uint8_t hiAddr = Z80nRd(cpu, rf->pc++, true);
    uint16_t addr = (hiAddr << 8) | loAddr;

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & CF))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // push de
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->d);
    Z80nWd(cpu, --rf->sp, rf->e);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sub nn
    Z80N_T_BEGIN(cpu);
    sub8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_D7(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 10
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x10;
    rf->memptr = 0x10;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret c
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    // Branch taken
    if (rf->f & CF)
    {
        uint16_t addr = Z80nRd(cpu, rf->sp++);
        addr += 0x100 * Z80nRd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_D9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // exx
    // Exchange data between main and shadow register sets
    uint16_t tmp = rf->bc; rf->bc = rf->alt.bc; rf->alt.bc = tmp;
    tmp = rf->de; rf->de = rf->alt.de; rf->alt.de = tmp;
    tmp = rf->hl; rf->hl = rf->alt.hl; rf->alt.hl = tmp;
    Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_DA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp c,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (rf->f & CF)
    {

        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_DB(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // in a,(nn)
    Z80N_T_BEGIN(cpu);
    uint16_t port = Z80nRd(cpu, rf->pc++, true) + (rf->a << 8);

    // MEMPTR = full port address + 1 ('port' already includes A on the high
    // byte - adding A<<8 again double-counted it; FUSE-verified)
    rf->memptr = port + 1;

    // IO read: IORQ/RD assert at T2 of the IO cycle - sample the port there,
    // not at the cycle entry (see op_D3).
    cputact(1);
    rf->a = Z80nPin(cpu, port);
    cputact(3);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_DC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call c,nnnn
    Z80N_T_BEGIN(cpu);
    uint8_t loAddr = Z80nRd(cpu, rf->pc++, true);
    uint8_t hiAddr = Z80nRd(cpu, rf->pc++, true);
    uint16_t addr = (hiAddr << 8) | loAddr;

    rf->memptr = addr;

    // Branch taken
    if (rf->f & CF)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);

        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_DE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // sbc a,nn
    Z80N_T_BEGIN(cpu);
    sbc8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_DF(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 18
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x18;
    rf->memptr = 0x18;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret po
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    // Branch taken
    if (!(rf->f & PV))
    {
        uint16_t addr = Z80nRd(cpu, rf->sp++);
        addr += 0x100 * Z80nRd(cpu, rf->sp++);

        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // pop hl
    Z80N_T_BEGIN(cpu);
    rf->l = Z80nRd(cpu, rf->sp++);
    rf->h = Z80nRd(cpu, rf->sp++);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp po,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & PV))
    {
        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ex (sp),hl
    Z80N_T_BEGIN(cpu);
    uint16_t value = Z80nRd(cpu, rf->sp) + 0x100 * Z80nRd(cpu, rf->sp + 1);

    cpuidle(rf->sp + 1, 1);

    // Real Z80 write order: high byte (SP+1) first, then low (SP) - FUSE-verified
    Z80nWd(cpu, rf->sp + 1, rf->h);
    Z80nWd(cpu, rf->sp, rf->l);

    rf->memptr = value;

    rf->hl = value;

    cpuidle(rf->sp, 2);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call po,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & PV))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

          Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // push hl
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->h);
    Z80nWd(cpu, --rf->sp, rf->l);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // and nn
    Z80N_T_BEGIN(cpu);
    and8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_E7(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 20
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x20;
    rf->memptr = 0x20;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret pe
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    if (rf->f & PV)
    {
        uint16_t addr = Z80nRd(cpu, rf->sp++);
        addr += 0x100 * Z80nRd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_E9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp (hl)

   rf->pc = rf->hl;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_EA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp pe,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;
    if (rf->f & PV)
    {

        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_EB(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ex de,hl
   uint16_t de = rf->de;
   rf->de = rf->hl;
   rf->hl = de;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_EC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call pe,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & PV)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_EE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // xor nn
    Z80N_T_BEGIN(cpu);
    xor8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_EF(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 28
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x28;
    rf->memptr = 0x28;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F0(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret p
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    if (!(rf->f & SF))
    {
        uint16_t addr = Z80nRd(cpu, rf->sp++);
        addr += 0x100 * Z80nRd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F1(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // pop af
    Z80N_T_BEGIN(cpu);
    rf->f = Z80nRd(cpu, rf->sp++);
    rf->a = Z80nRd(cpu, rf->sp++);

    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F2(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp p,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & SF))
    {

        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F3(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // di
   rf->iff1 = 0;
   rf->iff2 = 0;
   Z80N_RETURN_Q(0, tact0);
}

Z80NOPCODE op_F4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call p,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (!(rf->f & SF))
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // push af
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->a);
    Z80nWd(cpu, --rf->sp, rf->f);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F6(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // or nn
    Z80N_T_BEGIN(cpu);
    or8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_F7(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 30
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x30;
    rf->memptr = 0x30;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F8(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ret m
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    if (rf->f & SF)
    {
        uint16_t addr = Z80nRd(cpu, rf->sp++);
        addr += 0x100 * Z80nRd(cpu, rf->sp++);


        rf->pc = addr;
        rf->memptr = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_F9(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ld sp,hl
   Z80N_T_BEGIN(cpu);
   rf->sp = rf->hl & 0xFFFF;

   cpuidle(Z80N_IR(rf), 2);
   Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_FA(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp m,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    // Branch taken
    if (rf->f & SF)
    {

        rf->pc = addr;
    }
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_FB(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ei
    Z80N_T_BEGIN(cpu);
    rf->iff1 = 1;
    rf->iff2 = 1;

    // The EI shadow is derived from the last opcode word (Z80nCpuIntPossible);
    // the engine keeps no eipos (host bookkeeping in an attached block).
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_FC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // call m,nnnn
    Z80N_T_BEGIN(cpu);
    uint16_t addr = Z80nRd(cpu, rf->pc++, true);
    addr += 0x100 * Z80nRd(cpu, rf->pc++, true);

    rf->memptr = addr;

    if (rf->f & SF)
    {
        cpuidle(rf->pc - 1, 1);  // on the address's high byte

        Z80nWd(cpu, --rf->sp, (rf->pc >> 8) & 0xFF);
        Z80nWd(cpu, --rf->sp, rf->pc & 0xFF);


        rf->pc = addr;
    };
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_FE(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // cp nn
    Z80N_T_BEGIN(cpu);
    cp8(rf, Z80nRd(cpu, rf->pc++, true));
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE op_FF(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // rst 38
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);

    Z80nWd(cpu, --rf->sp, rf->pch);
    Z80nWd(cpu, --rf->sp, rf->pcl);


    rf->pc = 0x38;
    rf->memptr = 0x38;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE op_CB(Z80nCPU *cpu, int);
Z80NOPCODE op_ED(Z80nCPU *cpu, int);
Z80NOPCODE op_DD(Z80nCPU *cpu, int);
Z80NOPCODE op_FD(Z80nCPU *cpu, int);

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

