// opcodes-base.cpp - no-prefix opcodes, ported from core op_noprefix.cpp.


//  Non-prefixed opcodes
// Important note: M1 cycle lasts 4 clocks. This is the bare minimum for any opcode. So only additional deltas added here with cputact(n) macro

Z80OPCODE op_00(Z80CPU *cpu, int tact0) { // nop [4]
	// No increment for CPU cycles counter (t) is required
	// 4 cycles already spent in m1_cycle();
	Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_01(Z80CPU *cpu, int tact0) { // ld bc,nnnn [10]
    Z80_T_BEGIN(cpu);
    cpu->c = Z80Rd(cpu, cpu->pc++, true);
    cpu->b = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_02(Z80CPU *cpu, int tact0) { // ld (bc),a [7]
   Z80_T_BEGIN(cpu);
   // MEMPTR: low byte = (BC+1) & 0xFF, high byte = A (FUSE-verified)
   cpu->memptr = ((cpu->bc + 1) & 0xFF) | (cpu->a << 8);
   Z80Wd(cpu, cpu->bc, cpu->a);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_03(Z80CPU *cpu, int tact0) { // inc bc
   Z80_T_BEGIN(cpu);
   cpu->bc = (cpu->bc + 1) & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_04(Z80CPU *cpu, int tact0) { // inc b
   inc8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_05(Z80CPU *cpu, int tact0) { // dec b
   dec8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_06(Z80CPU *cpu, int tact0) { // ld b,nn
    Z80_T_BEGIN(cpu);
    cpu->b = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_07(Z80CPU *cpu, int tact0) { // rlca
   cpu->f = rlca_f[cpu->a] | (cpu->f & (SF | ZF | PV));
   cpu->a = rol[cpu->a];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_08(Z80CPU *cpu, int tact0) { // ex af,af'
   uint16_t mainAF = cpu->af;
   cpu->af = cpu->alt.af;
   cpu->alt.af = mainAF;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_09(Z80CPU *cpu, int tact0) { // add hl,bc
    Z80_T_BEGIN(cpu);
    cpu->memptr = cpu->hl + 1;

    uint8_t flags = cpu->f;

    int hl = cpu->hl & 0xFFFF;
    int bc = cpu->bc & 0xFFFF;

    //int halfHL = hl & 0x0FFF;
    //int halfBC = bc & 0x0FFF;

    // Calculate half-carry flag (HF)
    flags = (flags & ~(NF | CF | F5 | F3 | HF));
    flags |= (((cpu->hl & 0x0FFF) + (cpu->bc & 0x0FFF)) >> 8) & 0x10; // HF

    // Do Add
    hl = hl + bc;

    // Check for carry flag (CF)
    if (hl & 0x10000)
        flags |= CF;

    flags |= ((hl >> 8) & (F5 | F3));  // X/Y from result high byte

    // Store result back to registers
    cpu->hl = hl & 0xFFFF;
    cpu->f = flags;

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_0A(Z80CPU *cpu, int tact0) { // ld a,(bc)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->bc + 1;
   cpu->a = Z80Rd(cpu, cpu->bc);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_0B(Z80CPU *cpu, int tact0) { // dec bc
   Z80_T_BEGIN(cpu);
   cpu->bc = (cpu->bc - 1) & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_0C(Z80CPU *cpu, int tact0) { // inc c
   inc8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_0D(Z80CPU *cpu, int tact0) { // dec c
   dec8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_0E(Z80CPU *cpu, int tact0) { // ld c,nn
    Z80_T_BEGIN(cpu);
    cpu->c = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_0F(Z80CPU *cpu, int tact0) { // rrca
   cpu->f = rrca_f[cpu->a] | (cpu->f & (SF | ZF | PV));
   cpu->a = ror[cpu->a];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_10(Z80CPU *cpu, int tact0) { // djnz rr
	Z80_T_BEGIN(cpu);
	cputact(1);

	int8_t offset = Z80Rd(cpu, cpu->pc++, true);  // instruction-stream byte (m1State, Operand kind)

	// Branch taken
	if (--cpu->b)
	{

        cpu->pc += offset;
		cpu->memptr = cpu->pc;

		cputact(5);
	}
	Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_11(Z80CPU *cpu, int tact0) { // ld de,nnnn
    Z80_T_BEGIN(cpu);
    cpu->e = Z80Rd(cpu, cpu->pc++, true);
    cpu->d = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_12(Z80CPU *cpu, int tact0) { // ld (de),a
   Z80_T_BEGIN(cpu);
   // MEMPTR: low byte = (DE+1) & 0xFF, high byte = A (FUSE-verified)
   cpu->memptr = ((cpu->de + 1) & 0xFF) | (cpu->a << 8);
   Z80Wd(cpu, cpu->de, cpu->a);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_13(Z80CPU *cpu, int tact0) { // inc de
   Z80_T_BEGIN(cpu);
   cpu->de = (cpu->de + 1) & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_14(Z80CPU *cpu, int tact0) { // inc d
   inc8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_15(Z80CPU *cpu, int tact0) { // dec d
   dec8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_16(Z80CPU *cpu, int tact0) { // ld d,nn
    Z80_T_BEGIN(cpu);
    cpu->d = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_17(Z80CPU *cpu, int tact0) { // rla
   uint8_t new_a = (cpu->a << 1) + (cpu->f & 1);

   cpu->f = rlca_f[cpu->a] | (cpu->f & (SF | ZF | PV)); // use same table with rlca
   cpu->a = new_a;
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_18(Z80CPU *cpu, int tact0) { // jr rr
    Z80_T_BEGIN(cpu);
    int8_t offset = Z80Rd(cpu, cpu->pc++, true);


    cpu->pc += offset;
    cpu->memptr = cpu->pc;

    cputact(5);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_19(Z80CPU *cpu, int tact0) { // add hl,de
    Z80_T_BEGIN(cpu);
    cpu->memptr = cpu->hl + 1;

    uint8_t flags = cpu->f;

    int hl = cpu->hl & 0xFFFF;
    int de = cpu->de & 0xFFFF;

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
    cpu->hl = hl & 0xFFFF;
    cpu->f = flags;

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_1A(Z80CPU *cpu, int tact0) { // ld a,(de)
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->de + 1;

   cpu->a = Z80Rd(cpu, cpu->de);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_1B(Z80CPU *cpu, int tact0) { // dec de
   Z80_T_BEGIN(cpu);
   cpu->de = (cpu->de - 1) & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_1C(Z80CPU *cpu, int tact0) { // inc e
   inc8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_1D(Z80CPU *cpu, int tact0) { // dec e
   dec8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_1E(Z80CPU *cpu, int tact0) { // ld e,nn
    Z80_T_BEGIN(cpu);
    cpu->e = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_1F(Z80CPU *cpu, int tact0) { // rra
   uint8_t new_a = (cpu->a >> 1) + (cpu->f << 7);

   cpu->f = rrca_f[cpu->a] | (cpu->f & (SF | ZF | PV)); // use same table with rrca
   cpu->a = new_a;
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_20(Z80CPU *cpu, int tact0) { // jr nz, rr
    Z80_T_BEGIN(cpu);
    int8_t offset = Z80Rd(cpu, cpu->pc++, true);

    if (!(cpu->f & ZF))
    {
        cpu->pc += offset;

        cpu->memptr = cpu->pc;

        cputact(5);
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_21(Z80CPU *cpu, int tact0) { // ld hl,nnnn
    Z80_T_BEGIN(cpu);
    cpu->l = Z80Rd(cpu, cpu->pc++, true);
    cpu->h = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_22(Z80CPU *cpu, int tact0) { // ld (nnnn),hl
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += Z80Rd(cpu, cpu->pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    Z80Wd(cpu, addr, cpu->l);
    Z80Wd(cpu, addr + 1, cpu->h);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_23(Z80CPU *cpu, int tact0) { // inc hl
   Z80_T_BEGIN(cpu);
   cpu->hl = (cpu->hl + 1) & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_24(Z80CPU *cpu, int tact0) { // inc h
   inc8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_25(Z80CPU *cpu, int tact0) { // dec h
   dec8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_26(Z80CPU *cpu, int tact0) { // ld h,nn
    Z80_T_BEGIN(cpu);
    cpu->h = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_27(Z80CPU *cpu, int tact0) { // daa
   cpu->af = *(uint16_t *)(daatab + (cpu->a + 0x100 * ((cpu->f & 3) + ((cpu->f >> 2) & 4))) * 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_28(Z80CPU *cpu, int tact0) { // jr z,rr
    Z80_T_BEGIN(cpu);
    int8_t offset = Z80Rd(cpu, cpu->pc++, true);

    // Branch taken
    if ((cpu->f & ZF))
    {
        cpu->pc += offset;
        cpu->memptr = cpu->pc;

        cputact(5);
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_29(Z80CPU *cpu, int tact0) { // add hl,hl
    Z80_T_BEGIN(cpu);
    cpu->memptr = cpu->hl + 1;

    uint8_t flags = cpu->f;
    int hl = cpu->hl & 0xFFFF;

    // Clear flags affecting result
    flags = (cpu->f & ~(NF | CF | F5 | F3 | HF));

    // Set half-carry (HF) flag
    flags |= ((cpu->hl >> 7) & 0x10); // HF

    // Do add
    hl = hl + hl;

    // Handle carry-over
    if (hl & 0x10000)
        flags |= CF;

    // Update undocumented flags
    flags |= ((hl >> 8) & (F5 | F3));  // X/Y from result high byte

    // Store result back to registers
    cpu->hl = hl & 0xFFFF;
    cpu->f = flags;

    cputact(7);
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_2A(Z80CPU *cpu, int tact0) { // ld hl,(nnnn)
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += Z80Rd(cpu, cpu->pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->l = Z80Rd(cpu, addr);
    cpu->h = Z80Rd(cpu, addr + 1);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_2B(Z80CPU *cpu, int tact0) { // dec hl
   Z80_T_BEGIN(cpu);
   cpu->hl = (cpu->hl - 1) & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_2C(Z80CPU *cpu, int tact0) { // inc l
   inc8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_2D(Z80CPU *cpu, int tact0) { // dec l
   dec8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_2E(Z80CPU *cpu, int tact0) { // ld l,nn
    Z80_T_BEGIN(cpu);
    cpu->l = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_2F(Z80CPU *cpu, int tact0) { // cpl
   cpu->a ^= 0xFF;
   cpu->f = (cpu->f & ~(F3|F5)) | NF | HF | (cpu->a & (F3|F5));
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_30(Z80CPU *cpu, int tact0) { // jr nc, rr
    Z80_T_BEGIN(cpu);
    int8_t offset = Z80Rd(cpu, cpu->pc++, true);

    if (!(cpu->f & CF))
    {
        cpu->pc += offset;
        cpu->memptr = cpu->pc;

        cputact(5);
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_31(Z80CPU *cpu, int tact0) { // ld sp,nnnn
    Z80_T_BEGIN(cpu);
    cpu->spl = Z80Rd(cpu, cpu->pc++, true);
    cpu->sph = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_32(Z80CPU *cpu, int tact0) { // ld (nnnn),a
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += Z80Rd(cpu, cpu->pc++, true) * 0x100;

    cpu->memptr = ((addr + 1) & 0xFF) + (cpu->a << 8);
    cpu->memh = cpu->a;

    Z80Wd(cpu, addr, cpu->a);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_33(Z80CPU *cpu, int tact0) { // inc sp
	Z80_T_BEGIN(cpu);
	cpu->sp = (cpu->sp + 1) & 0xFFFF;

	cputact(2);
	Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_34(Z80CPU *cpu, int tact0) { // inc (hl)
	Z80_T_BEGIN(cpu);
	uint8_t value = Z80Rd(cpu, cpu->hl);
	inc8(cpu, value);

	cputact(1);

	Z80Wd(cpu, cpu->hl, value);
	Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_35(Z80CPU *cpu, int tact0) { // dec (hl)
	Z80_T_BEGIN(cpu);
	uint8_t hl = Z80Rd(cpu, cpu->hl);

	dec8(cpu, hl);

	cputact(1);

	Z80Wd(cpu, cpu->hl, hl);
	Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_36(Z80CPU *cpu, int tact0) { // ld (hl),nn
    Z80_T_BEGIN(cpu);
    uint8_t value = Z80Rd(cpu, cpu->pc++, true);
    Z80Wd(cpu, cpu->hl, value);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_37(Z80CPU *cpu, int tact0) { // scf - Zilog Z80 behavior
    // Undocumented YF/XF flags: (A | (F & ~Q)) & 0x28
    // Q captures YF/XF from previous flag-modifying instruction (set in Z80::Z80Step)
    // When Q=F, only A contributes; when Q≠F, F also contributes
    uint8_t undoc = (cpu->a | (cpu->f & ~cpu->q)) & (F3 | F5);
    cpu->f = (cpu->f & ~(HF | NF | F3 | F5)) | undoc | CF;

    // Update Q immediately - needed for back-to-back SCF/CCF sequences
    // where F value may not change but is still a flag-modifying instruction
    cpu->q = cpu->f & (F3 | F5);
    Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_38(Z80CPU *cpu, int tact0) { // jr c,rr
    Z80_T_BEGIN(cpu);
    int8_t offset = Z80Rd(cpu, cpu->pc, true);

    if ((cpu->f & CF))
    {
        cpu->pc += offset + 1;
        cpu->memptr = cpu->pc;

        cputact(5);
    }
    else
        cpu->pc++;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_39(Z80CPU *cpu, int tact0) { // add hl,sp
   Z80_T_BEGIN(cpu);
   cpu->memptr = cpu->hl + 1;

   int hl = cpu->hl & 0xFFFF;
   int sp = cpu->sp & 0xFFFF;

   int halfHL = hl & 0x0FFF;
   int halfSP = sp & 0x0FFF;

   // Clear result flags
   cpu->f = (cpu->f & ~(NF | CF | F5 | F3 | HF));

   // Set half-carry flag (HF)
   cpu->f |= ((halfHL + halfSP) >> 8) & 0x10; // HF

   // Do add
   hl = hl + sp;

   // Handle carry flag (CF)
   if (hl & 0x10000)
	   cpu->f |= CF;

   // Store result back to registers
   cpu->f |= ((hl >> 8) & (F5 | F3));  // X/Y from result high byte
   cpu->hl = hl & 0xFFFF;

   cputact(7);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_3A(Z80CPU *cpu, int tact0) { // ld a,(nnnn)
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += Z80Rd(cpu, cpu->pc++, true) * 0x100;

    cpu->memptr = addr + 1;

    cpu->a = Z80Rd(cpu, addr);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_3B(Z80CPU *cpu, int tact0) { // dec sp
   Z80_T_BEGIN(cpu);
   cpu->sp = (cpu->sp - 1) & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_3C(Z80CPU *cpu, int tact0) { // inc a
   inc8(cpu, cpu->a);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_3D(Z80CPU *cpu, int tact0) { // dec a
   dec8(cpu, cpu->a);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_3E(Z80CPU *cpu, int tact0) { // ld a,nn
    Z80_T_BEGIN(cpu);
    cpu->a = Z80Rd(cpu, cpu->pc++, true);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_3F(Z80CPU *cpu, int tact0) { // ccf - Zilog Z80 behavior
    // Undocumented YF/XF flags: (A | (F & ~Q)) & 0x28
    // When Q=F, only A contributes; when Q≠F, F also contributes
    uint8_t undoc = (cpu->a | (cpu->f & ~cpu->q)) & (F3 | F5);
    cpu->f = ((cpu->f & ~(NF | HF | F3 | F5)) | ((cpu->f << 4) & HF) | undoc) ^ CF;
    // Update Q immediately - needed for back-to-back SCF/CCF sequences
    // where F value may not change but is still a flag-modifying instruction
    cpu->q = cpu->f & (F3 | F5);
    Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_41(Z80CPU *cpu, int tact0) { // ld b,c
   cpu->b = cpu->c;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_42(Z80CPU *cpu, int tact0) { // ld b,d
   cpu->b = cpu->d;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_43(Z80CPU *cpu, int tact0) { // ld b,e
   cpu->b = cpu->e;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_44(Z80CPU *cpu, int tact0) { // ld b,h
   cpu->b = cpu->h;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_45(Z80CPU *cpu, int tact0) { // ld b,l
   cpu->b = cpu->l;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_46(Z80CPU *cpu, int tact0) { // ld b,(hl)
   Z80_T_BEGIN(cpu);
   cpu->b = Z80Rd(cpu, cpu->hl);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_47(Z80CPU *cpu, int tact0) { // ld b,a
   cpu->b = cpu->a;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_48(Z80CPU *cpu, int tact0) { // ld c,b
   cpu->c = cpu->b;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_4A(Z80CPU *cpu, int tact0) { // ld c,d
   cpu->c = cpu->d;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_4B(Z80CPU *cpu, int tact0) { // ld c,e
   cpu->c = cpu->e;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_4C(Z80CPU *cpu, int tact0) { // ld c,h
   cpu->c = cpu->h;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_4D(Z80CPU *cpu, int tact0) { // ld c,l
   cpu->c = cpu->l;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_4E(Z80CPU *cpu, int tact0) { // ld c,(hl)
   Z80_T_BEGIN(cpu);
   cpu->c = Z80Rd(cpu, cpu->hl);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_4F(Z80CPU *cpu, int tact0) { // ld c,a
   cpu->c = cpu->a;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_50(Z80CPU *cpu, int tact0) { // ld d,b
   cpu->d = cpu->b;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_51(Z80CPU *cpu, int tact0) { // ld d,c
   cpu->d = cpu->c;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_53(Z80CPU *cpu, int tact0) { // ld d,e
   cpu->d = cpu->e;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_54(Z80CPU *cpu, int tact0) { // ld d,h
   cpu->d = cpu->h;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_55(Z80CPU *cpu, int tact0) { // ld d,l
   cpu->d = cpu->l;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_56(Z80CPU *cpu, int tact0) { // ld d,(hl)
   Z80_T_BEGIN(cpu);
   cpu->d = Z80Rd(cpu, cpu->hl);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_57(Z80CPU *cpu, int tact0) { // ld d,a
   cpu->d = cpu->a;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_58(Z80CPU *cpu, int tact0) { // ld e,b
   cpu->e = cpu->b;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_59(Z80CPU *cpu, int tact0) { // ld e,c
   cpu->e = cpu->c;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_5A(Z80CPU *cpu, int tact0) { // ld e,d
   cpu->e = cpu->d;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_5C(Z80CPU *cpu, int tact0) { // ld e,h
   cpu->e = cpu->h;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_5D(Z80CPU *cpu, int tact0) { // ld e,l
   cpu->e = cpu->l;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_5E(Z80CPU *cpu, int tact0) { // ld e,(hl)
   Z80_T_BEGIN(cpu);
   cpu->e = Z80Rd(cpu, cpu->hl);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_5F(Z80CPU *cpu, int tact0) { // ld e,a
   cpu->e = cpu->a;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_60(Z80CPU *cpu, int tact0) { // ld h,b
   cpu->h = cpu->b;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_61(Z80CPU *cpu, int tact0) { // ld h,c
   cpu->h = cpu->c;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_62(Z80CPU *cpu, int tact0) { // ld h,d
   cpu->h = cpu->d;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_63(Z80CPU *cpu, int tact0) { // ld h,e
   cpu->h = cpu->e;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_65(Z80CPU *cpu, int tact0) { // ld h,l
   cpu->h = cpu->l;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_66(Z80CPU *cpu, int tact0) { // ld h,(hl)
   Z80_T_BEGIN(cpu);
   cpu->h = Z80Rd(cpu, cpu->hl);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_67(Z80CPU *cpu, int tact0) { // ld h,a
   cpu->h = cpu->a;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_68(Z80CPU *cpu, int tact0) { // ld l,b
   cpu->l = cpu->b;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_69(Z80CPU *cpu, int tact0) { // ld l,c
   cpu->l = cpu->c;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_6A(Z80CPU *cpu, int tact0) { // ld l,d
   cpu->l = cpu->d;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_6B(Z80CPU *cpu, int tact0) { // ld l,e
   cpu->l = cpu->e;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_6C(Z80CPU *cpu, int tact0) { // ld l,h
   cpu->l = cpu->h;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_6E(Z80CPU *cpu, int tact0) { // ld l,(hl)
   Z80_T_BEGIN(cpu);
   cpu->l = Z80Rd(cpu, cpu->hl);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_6F(Z80CPU *cpu, int tact0) { // ld l,a
   cpu->l = cpu->a;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_70(Z80CPU *cpu, int tact0) { // ld (hl),b
   Z80_T_BEGIN(cpu);
   Z80Wd(cpu, cpu->hl, cpu->b); //Alone Coder
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_71(Z80CPU *cpu, int tact0) { // ld (hl),c
   Z80_T_BEGIN(cpu);
   Z80Wd(cpu, cpu->hl, cpu->c); //Alone Coder
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_72(Z80CPU *cpu, int tact0) { // ld (hl),d
   Z80_T_BEGIN(cpu);
   Z80Wd(cpu, cpu->hl, cpu->d); //Alone Coder
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_73(Z80CPU *cpu, int tact0) { // ld (hl),e
   Z80_T_BEGIN(cpu);
   Z80Wd(cpu, cpu->hl, cpu->e); //Alone Coder
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_74(Z80CPU *cpu, int tact0) { // ld (hl),h
   Z80_T_BEGIN(cpu);
   Z80Wd(cpu, cpu->hl, cpu->h); //Alone Coder
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_75(Z80CPU *cpu, int tact0) { // ld (hl),l
   Z80_T_BEGIN(cpu);
   Z80Wd(cpu, cpu->hl, cpu->l); //Alone Coder
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_76(Z80CPU *cpu, int tact0) { // halt
   Z80_T_BEGIN(cpu);
   if (!cpu->halted)
       cpu->haltpos = static_cast<uint16_t>(tact_);

   cpu->pc--;  // Repeating execution until RESET, INT or NMI
   cpu->halted = 1;
   cpu->halt_cycle = 0;
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_77(Z80CPU *cpu, int tact0) { // ld (hl),a
   Z80_T_BEGIN(cpu);
   Z80Wd(cpu, cpu->hl, cpu->a);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_78(Z80CPU *cpu, int tact0) { // ld a,b
   cpu->a = cpu->b;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_79(Z80CPU *cpu, int tact0) { // ld a,c
   cpu->a = cpu->c;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_7A(Z80CPU *cpu, int tact0) { // ld a,d
   cpu->a = cpu->d;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_7B(Z80CPU *cpu, int tact0) { // ld a,e
   cpu->a = cpu->e;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_7C(Z80CPU *cpu, int tact0) { // ld a,h
   cpu->a = cpu->h;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_7D(Z80CPU *cpu, int tact0) { // ld a,l
   cpu->a = cpu->l;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_7E(Z80CPU *cpu, int tact0) { // ld a,(hl)
   Z80_T_BEGIN(cpu);
   cpu->a = Z80Rd(cpu, cpu->hl);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_80(Z80CPU *cpu, int tact0) { // add a,b
   add8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_81(Z80CPU *cpu, int tact0) { // add a,c
   add8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_82(Z80CPU *cpu, int tact0) { // add a,d
   add8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}
Z80OPCODE op_83(Z80CPU *cpu, int tact0) { // add a,e
   add8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_84(Z80CPU *cpu, int tact0) { // add a,h
   add8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_85(Z80CPU *cpu, int tact0) { // add a,l
   add8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_86(Z80CPU *cpu, int tact0) { // add a,(hl)
   Z80_T_BEGIN(cpu);
   add8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_87(Z80CPU *cpu, int tact0) { // add a,a
   add8(cpu, cpu->a);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_88(Z80CPU *cpu, int tact0) { // adc a,b
   adc8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_89(Z80CPU *cpu, int tact0) { // adc a,c
   adc8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_8A(Z80CPU *cpu, int tact0) { // adc a,d
   adc8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_8B(Z80CPU *cpu, int tact0) { // adc a,e
   adc8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_8C(Z80CPU *cpu, int tact0) { // adc a,h
   adc8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_8D(Z80CPU *cpu, int tact0) { // adc a,l
   adc8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}
Z80OPCODE op_8E(Z80CPU *cpu, int tact0) { // adc a,(hl)
   Z80_T_BEGIN(cpu);
   adc8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_8F(Z80CPU *cpu, int tact0) { // adc a,a
   adc8(cpu, cpu->a);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_90(Z80CPU *cpu, int tact0) { // sub b
   sub8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_91(Z80CPU *cpu, int tact0) { // sub c
   sub8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_92(Z80CPU *cpu, int tact0) { // sub d
   sub8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_93(Z80CPU *cpu, int tact0) { // sub e
   sub8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_94(Z80CPU *cpu, int tact0) { // sub h
   sub8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_95(Z80CPU *cpu, int tact0) { // sub l
   sub8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_96(Z80CPU *cpu, int tact0) { // sub (hl)
   Z80_T_BEGIN(cpu);
   sub8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_97(Z80CPU *cpu, int tact0) { // sub a
   cpu->af = ZF | NF;
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_98(Z80CPU *cpu, int tact0) { // sbc a,b
   sbc8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_99(Z80CPU *cpu, int tact0) { // sbc a,c
   sbc8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_9A(Z80CPU *cpu, int tact0) { // sbc a,d
   sbc8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_9B(Z80CPU *cpu, int tact0) { // sbc a,e
   sbc8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_9C(Z80CPU *cpu, int tact0) { // sbc a,h
   sbc8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_9D(Z80CPU *cpu, int tact0) { // sbc a,l
   sbc8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_9E(Z80CPU *cpu, int tact0) { // sbc a,(hl)
   Z80_T_BEGIN(cpu);
   sbc8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_9F(Z80CPU *cpu, int tact0) { // sbc a,a
   sbc8(cpu, cpu->a);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A0(Z80CPU *cpu, int tact0) { // and b
   and8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A1(Z80CPU *cpu, int tact0) { // and c
   and8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A2(Z80CPU *cpu, int tact0) { // and d
   and8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A3(Z80CPU *cpu, int tact0) { // and e
   and8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A4(Z80CPU *cpu, int tact0) { // and h
   and8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A5(Z80CPU *cpu, int tact0) { // and l
   and8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A6(Z80CPU *cpu, int tact0) { // and (hl)
   Z80_T_BEGIN(cpu);
   and8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_A7(Z80CPU *cpu, int tact0) { // and a
   and8(cpu, cpu->a);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A8(Z80CPU *cpu, int tact0) { // xor b
   xor8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_A9(Z80CPU *cpu, int tact0) { // xor c
   xor8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_AA(Z80CPU *cpu, int tact0) { // xor d
   xor8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_AB(Z80CPU *cpu, int tact0) { // xor e
   xor8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_AC(Z80CPU *cpu, int tact0) { // xor h
   xor8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_AD(Z80CPU *cpu, int tact0) { // xor l
   xor8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_AE(Z80CPU *cpu, int tact0) { // xor (hl)
   Z80_T_BEGIN(cpu);
   xor8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_AF(Z80CPU *cpu, int tact0) { // xor a
   cpu->af = ZF | PV;
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B0(Z80CPU *cpu, int tact0) { // or b
   or8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B1(Z80CPU *cpu, int tact0) { // or c
   or8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B2(Z80CPU *cpu, int tact0) { // or d
   or8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B3(Z80CPU *cpu, int tact0) { // or e
   or8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B4(Z80CPU *cpu, int tact0) { // or h
   or8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B5(Z80CPU *cpu, int tact0) { // or l
   or8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B6(Z80CPU *cpu, int tact0) { // or (hl)
   Z80_T_BEGIN(cpu);
   or8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_B7(Z80CPU *cpu, int tact0) { // or a
   or8(cpu, cpu->a);  // already optimized by compiler
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B8(Z80CPU *cpu, int tact0) { // cp b
   cp8(cpu, cpu->b);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_B9(Z80CPU *cpu, int tact0) { // cp c
   cp8(cpu, cpu->c);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_BA(Z80CPU *cpu, int tact0) { // cp d
   cp8(cpu, cpu->d);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_BB(Z80CPU *cpu, int tact0) { // cp e
   cp8(cpu, cpu->e);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_BC(Z80CPU *cpu, int tact0) { // cp h
   cp8(cpu, cpu->h);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_BD(Z80CPU *cpu, int tact0) { // cp l
   cp8(cpu, cpu->l);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_BE(Z80CPU *cpu, int tact0) { // cp (hl)
   Z80_T_BEGIN(cpu);
   cp8(cpu, Z80Rd(cpu, cpu->hl));
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_BF(Z80CPU *cpu, int tact0) { // cp a
   cp8(cpu, cpu->a); // can't optimize: F3,F5 depends on A
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE op_C0(Z80CPU *cpu, int tact0) { // ret nz
    Z80_T_BEGIN(cpu);
    cputact(1);

    if (!(cpu->f & ZF))
    {
        uint16_t addr = Z80Rd(cpu, cpu->sp++);
        addr += 0x100 * Z80Rd(cpu, cpu->sp++);


        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C1(Z80CPU *cpu, int tact0) { // pop bc
    Z80_T_BEGIN(cpu);
    cpu->c = Z80Rd(cpu, cpu->sp++);
    cpu->b = Z80Rd(cpu, cpu->sp++);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C2(Z80CPU *cpu, int tact0) { // jp nz,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & ZF))
    {
        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C3(Z80CPU *cpu, int tact0) { // jp nnnn
   Z80_T_BEGIN(cpu);

   uint8_t lo = Z80Rd(cpu, cpu->pc, true);
   uint16_t hi = Z80Rd(cpu, cpu->pc + 1, true) << 8;

   cpu->pc = hi | lo;
   cpu->memptr = cpu->pc;
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C4(Z80CPU *cpu, int tact0) { // call nz,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr = 0x100 * Z80Rd(cpu, cpu->pc++, true) + addr;

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & ZF))
    {
        cputact(1);

        Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);

        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C5(Z80CPU *cpu, int tact0) { // push bc
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->b);
    Z80Wd(cpu, --cpu->sp, cpu->c);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C6(Z80CPU *cpu, int tact0) { // add a,nn
    Z80_T_BEGIN(cpu);
    add8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_C7(Z80CPU *cpu, int tact0) { // rst 00
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x0000;
    cpu->memptr = 0x0000;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C8(Z80CPU *cpu, int tact0) { // ret z
    Z80_T_BEGIN(cpu);
    cputact(1);

    if (cpu->f & ZF)
    {
        uint8_t lo = Z80Rd(cpu, cpu->sp++);
        uint16_t hi = Z80Rd(cpu, cpu->sp++) << 8;
        uint16_t addr = hi | lo;


        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_C9(Z80CPU *cpu, int tact0) { // ret
    Z80_T_BEGIN(cpu);
    uint8_t loAddr = Z80Rd(cpu, cpu->sp++);
    uint8_t hiAddr = Z80Rd(cpu, cpu->sp++);
    uint16_t addr = (hiAddr << 8) | loAddr;


    cpu->pc = addr;
    cpu->memptr = addr;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_CA(Z80CPU *cpu, int tact0) { // jp z,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    if (cpu->f & ZF)
    {
        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_CC(Z80CPU *cpu, int tact0) { // call z,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    if (cpu->f & ZF)
    {
        cputact(1);
        Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);

        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_CD(Z80CPU *cpu, int tact0) { // call nnnn
    Z80_T_BEGIN(cpu);

    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cputact(1);

    // Put return address to stack
    Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
    Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);

    cpu->pc = addr;
    cpu->memptr = addr;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_CE(Z80CPU *cpu, int tact0) { // adc a,nn
    Z80_T_BEGIN(cpu);
    adc8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_CF(Z80CPU *cpu, int tact0) { // rst 08
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x08;
    cpu->memptr = 0x08;
    cpu->memh = 0;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D0(Z80CPU *cpu, int tact0) { // ret nc
    Z80_T_BEGIN(cpu);
    cputact(1);

    if (!(cpu->f & CF))
    {
        uint16_t addr = Z80Rd(cpu, cpu->sp++);
        addr += 0x100 * Z80Rd(cpu, cpu->sp++);


        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D1(Z80CPU *cpu, int tact0) { // pop de
    Z80_T_BEGIN(cpu);
    cpu->e = Z80Rd(cpu, cpu->sp++);
    cpu->d = Z80Rd(cpu, cpu->sp++);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D2(Z80CPU *cpu, int tact0) { // jp nc,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & CF))
    {
        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

// The full 16-bit address bus is used, not just the 8-bit n value.
// The high byte (A15-A8) of the address bus is set to the contents of the accumulator (A).
// The low byte (A7-A0) of the address bus is set to the 8-bit port number n (from the instruction).
// So, the actual 16-bit address placed on the address bus is:
// A15-A8 = A (accumulator value)
// A7-A0 = n (port number from instruction)
// → (A << 8) | n
Z80OPCODE op_D3(Z80CPU *cpu, int tact0) { // out (n),a
    Z80_T_BEGIN(cpu);
    uint16_t port = Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = ((port + 1) & 0xFF) + (cpu->a << 8);

    // IO write: Z80 IO machine cycles assert IORQ/WR one clock late - at T2 of
    // the 4T IO cycle (T1 carries no IORQ; Z80 UM "the CPU automatically inserts
    // one wait state" before IO). The port write must land on that T-state, not
    // at the IO cycle entry: charging 1T first places the SetBorderColor() flush
    // exactly at the IORQ T-state. At the cycle entry border changes render 1T
    // (2 px) early - Pentagon updates border every 1T (MiSTer ula.sv line 185),
    // which makes the offset clearly visible in border-synced effects.
    cputact(1);
    Z80Pout(cpu, port + (cpu->a << 8), cpu->a);
    cputact(3);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D4(Z80CPU *cpu, int tact0) { // call nc,nnnn
    Z80_T_BEGIN(cpu);
    uint8_t loAddr = Z80Rd(cpu, cpu->pc++, true);
    uint8_t hiAddr = Z80Rd(cpu, cpu->pc++, true);
    uint16_t addr = (hiAddr << 8) | loAddr;

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & CF))
    {
        cputact(1);

        Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);


        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D5(Z80CPU *cpu, int tact0) { // push de
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->d);
    Z80Wd(cpu, --cpu->sp, cpu->e);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D6(Z80CPU *cpu, int tact0) { // sub nn
    Z80_T_BEGIN(cpu);
    sub8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_D7(Z80CPU *cpu, int tact0) { // rst 10
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x10;
    cpu->memptr = 0x10;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D8(Z80CPU *cpu, int tact0) { // ret c
    Z80_T_BEGIN(cpu);
    cputact(1);

    // Branch taken
    if (cpu->f & CF)
    {
        uint16_t addr = Z80Rd(cpu, cpu->sp++);
        addr += 0x100 * Z80Rd(cpu, cpu->sp++);


        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_D9(Z80CPU *cpu, int tact0) { // exx
    // Exchange data between main and shadow register sets
    uint16_t tmp = cpu->bc; cpu->bc = cpu->alt.bc; cpu->alt.bc = tmp;
    tmp = cpu->de; cpu->de = cpu->alt.de; cpu->alt.de = tmp;
    tmp = cpu->hl; cpu->hl = cpu->alt.hl; cpu->alt.hl = tmp;
    Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_DA(Z80CPU *cpu, int tact0) { // jp c,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (cpu->f & CF)
    {

        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_DB(Z80CPU *cpu, int tact0) { // in a,(nn)
    Z80_T_BEGIN(cpu);
    uint16_t port = Z80Rd(cpu, cpu->pc++, true) + (cpu->a << 8);

    // MEMPTR = full port address + 1 ('port' already includes A on the high
    // byte - adding A<<8 again double-counted it; FUSE-verified)
    cpu->memptr = port + 1;

    // IO read: IORQ/RD assert at T2 of the IO cycle - sample the port there,
    // not at the cycle entry (see op_D3).
    cputact(1);
    cpu->a = Z80Pin(cpu, port);
    cputact(3);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_DC(Z80CPU *cpu, int tact0) { // call c,nnnn
    Z80_T_BEGIN(cpu);
    uint8_t loAddr = Z80Rd(cpu, cpu->pc++, true);
    uint8_t hiAddr = Z80Rd(cpu, cpu->pc++, true);
    uint16_t addr = (hiAddr << 8) | loAddr;

    cpu->memptr = addr;

    // Branch taken
    if (cpu->f & CF)
    {
        cputact(1);

        Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);

        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_DE(Z80CPU *cpu, int tact0) { // sbc a,nn
    Z80_T_BEGIN(cpu);
    sbc8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_DF(Z80CPU *cpu, int tact0) { // rst 18
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x18;
    cpu->memptr = 0x18;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E0(Z80CPU *cpu, int tact0) { // ret po
    Z80_T_BEGIN(cpu);
    cputact(1);

    // Branch taken
    if (!(cpu->f & PV))
    {
        uint16_t addr = Z80Rd(cpu, cpu->sp++);
        addr += 0x100 * Z80Rd(cpu, cpu->sp++);

        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E1(Z80CPU *cpu, int tact0) { // pop hl
    Z80_T_BEGIN(cpu);
    cpu->l = Z80Rd(cpu, cpu->sp++);
    cpu->h = Z80Rd(cpu, cpu->sp++);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E2(Z80CPU *cpu, int tact0) { // jp po,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & PV))
    {
        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E3(Z80CPU *cpu, int tact0) { // ex (sp),hl
    Z80_T_BEGIN(cpu);
    uint16_t value = Z80Rd(cpu, cpu->sp) + 0x100 * Z80Rd(cpu, cpu->sp + 1);

    cputact(1);

    // Real Z80 write order: high byte (SP+1) first, then low (SP) - FUSE-verified
    Z80Wd(cpu, cpu->sp + 1, cpu->h);
    Z80Wd(cpu, cpu->sp, cpu->l);

    cpu->memptr = value;

    cpu->hl = value;

    cputact(2);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E4(Z80CPU *cpu, int tact0) { // call po,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & PV))
    {
        cputact(1);

          Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);


        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E5(Z80CPU *cpu, int tact0) { // push hl
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->h);
    Z80Wd(cpu, --cpu->sp, cpu->l);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E6(Z80CPU *cpu, int tact0) { // and nn
    Z80_T_BEGIN(cpu);
    and8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_E7(Z80CPU *cpu, int tact0) { // rst 20
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x20;
    cpu->memptr = 0x20;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E8(Z80CPU *cpu, int tact0) { // ret pe
    Z80_T_BEGIN(cpu);
    cputact(1);

    if (cpu->f & PV)
    {
        uint16_t addr = Z80Rd(cpu, cpu->sp++);
        addr += 0x100 * Z80Rd(cpu, cpu->sp++);


        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_E9(Z80CPU *cpu, int tact0) { // jp (hl)

   cpu->pc = cpu->hl;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_EA(Z80CPU *cpu, int tact0) { // jp pe,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;
    if (cpu->f & PV)
    {

        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_EB(Z80CPU *cpu, int tact0) { // ex de,hl
   uint16_t de = cpu->de;
   cpu->de = cpu->hl;
   cpu->hl = de;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_EC(Z80CPU *cpu, int tact0) { // call pe,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    if (cpu->f & PV)
    {
        cputact(1);

        Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);


        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_EE(Z80CPU *cpu, int tact0) { // xor nn
    Z80_T_BEGIN(cpu);
    xor8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_EF(Z80CPU *cpu, int tact0) { // rst 28
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x28;
    cpu->memptr = 0x28;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F0(Z80CPU *cpu, int tact0) { // ret p
    Z80_T_BEGIN(cpu);
    cputact(1);

    if (!(cpu->f & SF))
    {
        uint16_t addr = Z80Rd(cpu, cpu->sp++);
        addr += 0x100 * Z80Rd(cpu, cpu->sp++);


        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F1(Z80CPU *cpu, int tact0) { // pop af
    Z80_T_BEGIN(cpu);
    cpu->f = Z80Rd(cpu, cpu->sp++);
    cpu->a = Z80Rd(cpu, cpu->sp++);

    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F2(Z80CPU *cpu, int tact0) { // jp p,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & SF))
    {

        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F3(Z80CPU *cpu, int tact0) { // di
   cpu->iff1 = 0;
   cpu->iff2 = 0;
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE op_F4(Z80CPU *cpu, int tact0) { // call p,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (!(cpu->f & SF))
    {
        cputact(1);

        Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);


        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F5(Z80CPU *cpu, int tact0) { // push af
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->a);
    Z80Wd(cpu, --cpu->sp, cpu->f);
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F6(Z80CPU *cpu, int tact0) { // or nn
    Z80_T_BEGIN(cpu);
    or8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_F7(Z80CPU *cpu, int tact0) { // rst 30
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x30;
    cpu->memptr = 0x30;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F8(Z80CPU *cpu, int tact0) { // ret m
    Z80_T_BEGIN(cpu);
    cputact(1);

    if (cpu->f & SF)
    {
        uint16_t addr = Z80Rd(cpu, cpu->sp++);
        addr += 0x100 * Z80Rd(cpu, cpu->sp++);


        cpu->pc = addr;
        cpu->memptr = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_F9(Z80CPU *cpu, int tact0) { // ld sp,hl
   Z80_T_BEGIN(cpu);
   cpu->sp = cpu->hl & 0xFFFF;

   cputact(2);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_FA(Z80CPU *cpu, int tact0) { // jp m,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    // Branch taken
    if (cpu->f & SF)
    {

        cpu->pc = addr;
    }
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_FB(Z80CPU *cpu, int tact0) { // ei
    Z80_T_BEGIN(cpu);
    cpu->iff1 = 1;
    cpu->iff2 = 1;

    // Remember EI command timing mark since in fact interrupts are enabled only
    // after the CPU command after EI
    cpu->eipos = tact_;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_FC(Z80CPU *cpu, int tact0) { // call m,nnnn
    Z80_T_BEGIN(cpu);
    uint16_t addr = Z80Rd(cpu, cpu->pc++, true);
    addr += 0x100 * Z80Rd(cpu, cpu->pc++, true);

    cpu->memptr = addr;

    if (cpu->f & SF)
    {
        cputact(1);

        Z80Wd(cpu, --cpu->sp, (cpu->pc >> 8) & 0xFF);
        Z80Wd(cpu, --cpu->sp, cpu->pc & 0xFF);


        cpu->pc = addr;
    };
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_FE(Z80CPU *cpu, int tact0) { // cp nn
    Z80_T_BEGIN(cpu);
    cp8(cpu, Z80Rd(cpu, cpu->pc++, true));
    Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE op_FF(Z80CPU *cpu, int tact0) { // rst 38
    Z80_T_BEGIN(cpu);
    cputact(1);

    Z80Wd(cpu, --cpu->sp, cpu->pch);
    Z80Wd(cpu, --cpu->sp, cpu->pcl);


    cpu->pc = 0x38;
    cpu->memptr = 0x38;
    Z80_RETURN_Q(0, tact_);
}

Z80OPCODE op_CB(Z80CPU *cpu, int);
Z80OPCODE op_ED(Z80CPU *cpu, int);
Z80OPCODE op_DD(Z80CPU *cpu, int);
Z80OPCODE op_FD(Z80CPU *cpu, int);

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

