// opcodes-cb.cpp - CB-prefix opcodes, ported from core op_cb.cpp.


// FDCB opcodes (FD CB <xx> <opN>)
// M:<X> - number of memory acces cycles
// T:<Y> - numper of CPU clocks per instruction
// (a, b, c, d) - CPU clocks per microcode operation

Z80OPCODE opl_00(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc b
   rf->f = rlc_f[rf->b];
   rf->b = rol[rf->b];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_01(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc c
   rf->f = rlc_f[rf->c];
   rf->c = rol[rf->c];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_02(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc d
   rf->f = rlc_f[rf->d];
   rf->d = rol[rf->d];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_03(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc e
   rf->f = rlc_f[rf->e];
   rf->e = rol[rf->e];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_04(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc h
   rf->f = rlc_f[rf->h];
   rf->h = rol[rf->h];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_05(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc l
   rf->f = rlc_f[rf->l];
   rf->l = rol[rf->l];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_06(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc (hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);
   rf->f = rlc_f[t];

   cputact(1);

   Z80Wd(cpu, rf->hl, rol[t]);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_07(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rlc a
   rf->f = rlc_f[rf->a];
   rf->a = rol[rf->a];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_08(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc b
   rf->f = rrc_f[rf->b];
   rf->b = ror[rf->b];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_09(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc c
   rf->f = rrc_f[rf->c];
   rf->c = ror[rf->c];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_0A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc d
   rf->f = rrc_f[rf->d];
   rf->d = ror[rf->d];
   Z80_RETURN_Q(0x28, tact0);
}
Z80OPCODE opl_0B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc e
   rf->f = rrc_f[rf->e];
   rf->e = ror[rf->e];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_0C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc h
   rf->f = rrc_f[rf->h];
   rf->h = ror[rf->h];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_0D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc l
   rf->f = rrc_f[rf->l];
   rf->l = ror[rf->l];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_0E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc (hl)  | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);
   rf->f = rrc_f[t];

   cputact(1);

   Z80Wd(cpu, rf->hl, ror[t]);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_0F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rrc a
   rf->f = rrc_f[rf->a];
   rf->a = ror[rf->a];
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_10(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl b
   if (rf->f & CF)
      rf->f = rl1[rf->b], rf->b = (rf->b << 1) + 1;
   else
      rf->f = rl0[rf->b], rf->b = (rf->b << 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_11(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl c
   if (rf->f & CF)
      rf->f = rl1[rf->c], rf->c = (rf->c << 1) + 1;
   else
      rf->f = rl0[rf->c], rf->c = (rf->c << 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_12(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl d
   if (rf->f & CF)
      rf->f = rl1[rf->d], rf->d = (rf->d << 1) + 1;
   else
      rf->f = rl0[rf->d], rf->d = (rf->d << 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_13(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl e
   if (rf->f & CF)
      rf->f = rl1[rf->e], rf->e = (rf->e << 1) + 1;
   else
      rf->f = rl0[rf->e], rf->e = (rf->e << 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_14(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl h
   if (rf->f & CF)
      rf->f = rl1[rf->h], rf->h = (rf->h << 1) + 1;
   else
      rf->f = rl0[rf->h], rf->h = (rf->h << 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_15(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl l
   if (rf->f & CF)
      rf->f = rl1[rf->l], rf->l = (rf->l << 1) + 1;
   else
      rf->f = rl0[rf->l], rf->l = (rf->l << 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_16(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl (hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   if (rf->f & CF)
   {
	   rf->f = rl1[t];
	   t = (t << 1) + 1;
   }
   else
   {
	   rf->f = rl0[t];
	   t = (t << 1);
   }

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_17(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rl a
   if (rf->f & CF)
      rf->f = rl1[rf->a], rf->a = (rf->a << 1) + 1;
   else
      rf->f = rl0[rf->a], rf->a = (rf->a << 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_18(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr b
   if (rf->f & CF)
      rf->f = rr1[rf->b], rf->b = (rf->b >> 1) + 0x80;
   else
      rf->f = rr0[rf->b], rf->b = (rf->b >> 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_19(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr c
   if (rf->f & CF)
      rf->f = rr1[rf->c], rf->c = (rf->c >> 1) + 0x80;
   else
      rf->f = rr0[rf->c], rf->c = (rf->c >> 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_1A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr d
   if (rf->f & CF)
      rf->f = rr1[rf->d], rf->d = (rf->d >> 1) + 0x80;
   else
      rf->f = rr0[rf->d], rf->d = (rf->d >> 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_1B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr e
   if (rf->f & CF)
      rf->f = rr1[rf->e], rf->e = (rf->e >> 1) + 0x80;
   else
      rf->f = rr0[rf->e], rf->e = (rf->e >> 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_1C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr h
   if (rf->f & CF)
      rf->f = rr1[rf->h], rf->h = (rf->h >> 1) + 0x80;
   else
      rf->f = rr0[rf->h], rf->h = (rf->h >> 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_1D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr l
   if (rf->f & CF)
      rf->f = rr1[rf->l], rf->l = (rf->l >> 1) + 0x80;
   else
      rf->f = rr0[rf->l], rf->l = (rf->l >> 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_1E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr (hl) | M:4 T:15 (4, 4, 4, 3)
	Z80_T_BEGIN(cpu);
	uint8_t t = Z80Rd(cpu, rf->hl);

	if (rf->f & CF)
	{
		rf->f = rr1[t];
		t = (t >> 1) | 0x80;
	}
	else
	{
		rf->f = rr0[t];
		t = (t >> 1);
	}

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
	Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_1F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // rr a
	if (rf->f & CF)
	{
		rf->f = rr1[rf->a];
		rf->a = (rf->a >> 1) + 0x80;
	}
	else
	{
		rf->f = rr0[rf->a];
		rf->a = (rf->a >> 1);
	}
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_20(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla b
	rf->f = rl0[rf->b];
	rf->b = (rf->b << 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_21(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla c
	rf->f = rl0[rf->c];
	rf->c = (rf->c << 1);
	Z80_RETURN_Q(0x28, tact0);
}
Z80OPCODE opl_22(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla d
	rf->f = rl0[rf->d];
	rf->d = (rf->d << 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_23(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla e
	rf->f = rl0[rf->e];
	rf->e = (rf->e << 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_24(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla h
	rf->f = rl0[rf->h];
	rf->h = (rf->h << 1);
	Z80_RETURN_Q(0x28, tact0);
}
Z80OPCODE opl_25(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla l
	rf->f = rl0[rf->l];
	rf->l = (rf->l << 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_26(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla (hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);
   rf->f = rl0[t];
   t = (t << 1);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_27(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sla a
	rf->f = rl0[rf->a];
	rf->a = (rf->a << 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_28(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra b
	rf->f = sra_f[rf->b];
	rf->b = (rf->b >> 1) + (rf->b & 0x80);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_29(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra c
	rf->f = sra_f[rf->c];
	rf->c = (rf->c >> 1) + (rf->c & 0x80);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_2A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra d
	rf->f = sra_f[rf->d];
	rf->d = (rf->d >> 1) + (rf->d & 0x80);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_2B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra e
	rf->f = sra_f[rf->e];
	rf->e = (rf->e >> 1) + (rf->e & 0x80);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_2C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra h
	rf->f = sra_f[rf->h];
	rf->h = (rf->h >> 1) + (rf->h & 0x80);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_2D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra l
	rf->f = sra_f[rf->l];
	rf->l = (rf->l >> 1) + (rf->l & 0x80);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_2E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra (hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   rf->f = sra_f[t];
   t = (t >> 1) + (t & 0x80);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_2F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sra a
	rf->f = sra_f[rf->a];
	rf->a = (rf->a >> 1) + (rf->a & 0x80);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_30(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli b
	rf->f = rl1[rf->b];
	rf->b = (rf->b << 1) + 1;
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_31(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli c
	rf->f = rl1[rf->c];
	rf->c = (rf->c << 1) + 1;
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_32(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli d
	rf->f = rl1[rf->d];
	rf->d = (rf->d << 1) + 1;
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_33(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli e
	rf->f = rl1[rf->e];
	rf->e = (rf->e << 1) + 1;
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_34(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli h
	rf->f = rl1[rf->h];
	rf->h = (rf->h << 1) + 1;
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_35(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli l
	rf->f = rl1[rf->l];
	rf->l = (rf->l << 1) + 1;
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_36(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli (hl)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);
   rf->f = rl1[t];
   t = (t << 1) + 1;

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_37(Z80CPU *cpu, int tact0, Z80Regs *rf) { // sli a
	rf->f = rl1[rf->a];
	rf->a = (rf->a << 1) + 1;
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_38(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl b
	rf->f = rr0[rf->b];
	rf->b = (rf->b >> 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_39(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl c
	rf->f = rr0[rf->c];
	rf->c = (rf->c >> 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_3A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl d
	rf->f = rr0[rf->d];
	rf->d = (rf->d >> 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_3B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl e
	rf->f = rr0[rf->e];
	rf->e = (rf->e >> 1);
	Z80_RETURN_Q(0x28, tact0);
}
Z80OPCODE opl_3C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl h
	rf->f = rr0[rf->h];
	rf->h = (rf->h >> 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_3D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl l
	rf->f = rr0[rf->l];
	rf->l = (rf->l >> 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_3E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl (hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   rf->f = rr0[value];
    value = (value >> 1);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_3F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // srl a
	rf->f = rr0[rf->a];
	rf->a = (rf->a >> 1);
	Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_40(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,b
   bit(rf, rf->b, 0);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_41(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,c
   bit(rf, rf->c, 0);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_42(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,d
   bit(rf, rf->d, 0);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_43(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,e
   bit(rf, rf->e, 0);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_44(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,h
   bit(rf, rf->h, 0);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_45(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,l
   bit(rf, rf->l, 0);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_46(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 0);
   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_47(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 0,a
   bit(rf, rf->a, 0);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_48(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,b
   bit(rf, rf->b, 1);
   Z80_RETURN_Q(0x28, tact0);
}
Z80OPCODE opl_49(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,c
   bit(rf, rf->c, 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_4A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,d
   bit(rf, rf->d, 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_4B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,e
   bit(rf, rf->e, 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_4C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,h
   bit(rf, rf->h, 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_4D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,l
   bit(rf, rf->l, 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_4E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 1);
   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_4F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 1,a
   bit(rf, rf->a, 1);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_50(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,b
   bit(rf, rf->b, 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_51(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,c
   bit(rf, rf->c, 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_52(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,d
   bit(rf, rf->d, 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_53(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,e
   bit(rf, rf->e, 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_54(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,h
   bit(rf, rf->h, 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_55(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,l
   bit(rf, rf->l, 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_56(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 2);

   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_57(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 2,a
   bit(rf, rf->a, 2);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_58(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,b
   bit(rf, rf->b, 3);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_59(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,c
   bit(rf, rf->c, 3);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_5A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,d
   bit(rf, rf->d, 3);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_5B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,e
   bit(rf, rf->e, 3);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_5C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,h
   bit(rf, rf->h, 3);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_5D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,l
   bit(rf, rf->l, 3);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_5E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 3);
   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_5F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 3,a
   bit(rf, rf->a, 3);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_60(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,b
   bit(rf, rf->b, 4);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_61(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,c
   bit(rf, rf->c, 4);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_62(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,d
   bit(rf, rf->d, 4);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_63(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,e
   bit(rf, rf->e, 4);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_64(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,h
   bit(rf, rf->h, 4);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_65(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,l
   bit(rf, rf->l, 4);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_66(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 4);

   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_67(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 4,a
   bit(rf, rf->a, 4);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_68(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,b
   bit(rf, rf->b, 5);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_69(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,c
   bit(rf, rf->c, 5);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_6A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,d
   bit(rf, rf->d, 5);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_6B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,e
   bit(rf, rf->e, 5);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_6C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,h
   bit(rf, rf->h, 5);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_6D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,l
   bit(rf, rf->l, 5);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_6E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 5);

   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_6F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 5,a
   bit(rf, rf->a, 5);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_70(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,b
   bit(rf, rf->b, 6);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_71(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,c
   bit(rf, rf->c, 6);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_72(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,d
   bit(rf, rf->d, 6);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_73(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,e
   bit(rf, rf->e, 6);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_74(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,h
   bit(rf, rf->h, 6);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_75(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,l
   bit(rf, rf->l, 6);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_76(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 6);

   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_77(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 6,a
   bit(rf, rf->a, 6);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_78(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,b
   bit(rf, rf->b, 7);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_79(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,c
   bit(rf, rf->c, 7);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_7A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,d
   bit(rf, rf->d, 7);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_7B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,e
   bit(rf, rf->e, 7);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_7C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,h
   bit(rf, rf->h, 7);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_7D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,l
   bit(rf, rf->l, 7);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_7E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,(hl)
   Z80_T_BEGIN(cpu);
   bitmem(rf, Z80Rd(cpu, rf->hl), 7);

   cputact(1);
   Z80_RETURN_Q(0x28, tact_);
}

Z80OPCODE opl_7F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // bit 7,a
   bit(rf, rf->a, 7);
   Z80_RETURN_Q(0x28, tact0);
}

Z80OPCODE opl_80(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,b
   res(rf->b, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_81(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,c
   res(rf->c, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_82(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,d
   res(rf->d, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_83(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,e
   res(rf->e, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_84(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,h
   res(rf->h, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_85(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,l
   res(rf->l, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_86(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   res(t, 0);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_87(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 0,a
   res(rf->a, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_88(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,b
   res(rf->b, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_89(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,c
   res(rf->c, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_8A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,d
   res(rf->d, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_8B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,e
   res(rf->e, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_8C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,h
   res(rf->h, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_8D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,l
   res(rf->l, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_8E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   res(value, 1);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_8F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 1,a
   res(rf->a, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_90(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,b
   res(rf->b, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_91(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,c
   res(rf->c, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_92(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,d
   res(rf->d, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_93(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,e
   res(rf->e, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_94(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,h
   res(rf->h, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_95(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,l
   res(rf->l, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_96(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   res(t, 2);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_97(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 2,a
   res(rf->a, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_98(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,b
   res(rf->b, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_99(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,c
   res(rf->c, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_9A(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,d
   res(rf->d, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_9B(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,e
   res(rf->e, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_9C(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,h
   res(rf->h, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_9D(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,l
   res(rf->l, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_9E(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   res(t, 3);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_9F(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 3,a
   res(rf->a, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A0(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,b
   res(rf->b, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A1(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,c
   res(rf->c, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A2(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,d
   res(rf->d, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A3(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,e
   res(rf->e, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A4(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,h
   res(rf->h, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A5(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,l
   res(rf->l, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A6(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   res(value, 4);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_A7(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 4,a
   res(rf->a, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A8(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,b
   res(rf->b, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_A9(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,c
   res(rf->c, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_AA(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,d
   res(rf->d, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_AB(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,e
   res(rf->e, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_AC(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,h
   res(rf->h, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_AD(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,l
   res(rf->l, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_AE(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   res(value, 5);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_AF(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 5,a
   res(rf->a, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B0(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,b
   res(rf->b, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B1(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,c
   res(rf->c, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B2(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,d
   res(rf->d, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B3(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,e
   res(rf->e, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B4(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,h
   res(rf->h, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B5(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,l
   res(rf->l, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B6(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   res(t, 6);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_B7(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 6,a
   res(rf->a, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B8(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,b
   res(rf->b, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_B9(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,c
   res(rf->c, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_BA(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,d
   res(rf->d, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_BB(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,e
   res(rf->e, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_BC(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,h
   res(rf->h, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_BD(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,l
   res(rf->l, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_BE(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   res(value, 7);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_BF(Z80CPU *cpu, int tact0, Z80Regs *rf) { // res 7,a
   res(rf->a, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C0(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,b
   op_set(rf->b, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C1(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,c
   op_set(rf->c, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C2(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,d
   op_set(rf->d, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C3(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,e
   op_set(rf->e, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C4(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,h
   op_set(rf->h, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C5(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,l
   op_set(rf->l, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C6(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   op_set(value, 0);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_C7(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 0,a
   op_set(rf->a, 0);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C8(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,b
   op_set(rf->b, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_C9(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,c
   op_set(rf->c, 1);
   Z80_RETURN_Q(0, tact0);
}
Z80OPCODE opl_CA(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,d
   op_set(rf->d, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_CB(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,e
   op_set(rf->e, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_CC(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,h
   op_set(rf->h, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_CD(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,l
   op_set(rf->l, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_CE(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   op_set(t, 1);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_CF(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 1,a
   op_set(rf->a, 1);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D0(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,b
   op_set(rf->b, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D1(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,c
   op_set(rf->c, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D2(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,d
   op_set(rf->d, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D3(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,e
   op_set(rf->e, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D4(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,h
   op_set(rf->h, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D5(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,l
   op_set(rf->l, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D6(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   op_set(value, 2);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_D7(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 2,a
   op_set(rf->a, 2);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D8(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,b
   op_set(rf->b, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_D9(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,c
   op_set(rf->c, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_DA(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,d
   op_set(rf->d, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_DB(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,e
   op_set(rf->e, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_DC(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,h
   op_set(rf->h, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_DD(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,l
   op_set(rf->l, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_DE(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   op_set(value, 3);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_DF(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 3,a
   op_set(rf->a, 3);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E0(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,b
   op_set(rf->b, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E1(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,c
   op_set(rf->c, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E2(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,d
   op_set(rf->d, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E3(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,e
   op_set(rf->e, 4);
   Z80_RETURN_Q(0, tact0);
}
Z80OPCODE opl_E4(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,h
   op_set(rf->h, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E5(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,l
   op_set(rf->l, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E6(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   op_set(t, 4);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_E7(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 4,a
   op_set(rf->a, 4);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E8(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,b
   op_set(rf->b, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_E9(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,c
   op_set(rf->c, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_EA(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,d
   op_set(rf->d, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_EB(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,e
   op_set(rf->e, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_EC(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,h
   op_set(rf->h, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_ED(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,l
   op_set(rf->l, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_EE(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t t = Z80Rd(cpu, rf->hl);

   op_set(t, 5);

   cputact(1);

   Z80Wd(cpu, rf->hl, t);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_EF(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 5,a
   op_set(rf->a, 5);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F0(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,b
   op_set(rf->b, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F1(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,c
   op_set(rf->c, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F2(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,d
   op_set(rf->d, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F3(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,e
   op_set(rf->e, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F4(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,h
   op_set(rf->h, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F5(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,l
   op_set(rf->l, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F6(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   op_set(value, 6);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_F7(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 6,a
   op_set(rf->a, 6);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F8(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,b
   op_set(rf->b, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_F9(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,c
   op_set(rf->c, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_FA(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,d
   op_set(rf->d, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_FB(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,e
   op_set(rf->e, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_FC(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,h
   op_set(rf->h, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_FD(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,l
   op_set(rf->l, 7);
   Z80_RETURN_Q(0, tact0);
}

Z80OPCODE opl_FE(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,(hl) | M:4 T:15 (4, 4, 4, 3)
   Z80_T_BEGIN(cpu);
   uint8_t value = Z80Rd(cpu, rf->hl);

   op_set(value, 7);

   cputact(1);

   Z80Wd(cpu, rf->hl, value);
   Z80_RETURN_Q(0, tact_);
}

Z80OPCODE opl_FF(Z80CPU *cpu, int tact0, Z80Regs *rf) { // set 7,a
   op_set(rf->a, 7);
   Z80_RETURN_Q(0, tact0);
}

STEPFUNC const logic_opcode[0x100] =
{
   opl_00, opl_01, opl_02, opl_03, opl_04, opl_05, opl_06, opl_07,
   opl_08, opl_09, opl_0A, opl_0B, opl_0C, opl_0D, opl_0E, opl_0F,
   opl_10, opl_11, opl_12, opl_13, opl_14, opl_15, opl_16, opl_17,
   opl_18, opl_19, opl_1A, opl_1B, opl_1C, opl_1D, opl_1E, opl_1F,
   opl_20, opl_21, opl_22, opl_23, opl_24, opl_25, opl_26, opl_27,
   opl_28, opl_29, opl_2A, opl_2B, opl_2C, opl_2D, opl_2E, opl_2F,
   opl_30, opl_31, opl_32, opl_33, opl_34, opl_35, opl_36, opl_37,
   opl_38, opl_39, opl_3A, opl_3B, opl_3C, opl_3D, opl_3E, opl_3F,

   opl_40, opl_41, opl_42, opl_43, opl_44, opl_45, opl_46, opl_47,
   opl_48, opl_49, opl_4A, opl_4B, opl_4C, opl_4D, opl_4E, opl_4F,
   opl_50, opl_51, opl_52, opl_53, opl_54, opl_55, opl_56, opl_57,
   opl_58, opl_59, opl_5A, opl_5B, opl_5C, opl_5D, opl_5E, opl_5F,
   opl_60, opl_61, opl_62, opl_63, opl_64, opl_65, opl_66, opl_67,
   opl_68, opl_69, opl_6A, opl_6B, opl_6C, opl_6D, opl_6E, opl_6F,
   opl_70, opl_71, opl_72, opl_73, opl_74, opl_75, opl_76, opl_77,
   opl_78, opl_79, opl_7A, opl_7B, opl_7C, opl_7D, opl_7E, opl_7F,

   opl_80, opl_81, opl_82, opl_83, opl_84, opl_85, opl_86, opl_87,
   opl_88, opl_89, opl_8A, opl_8B, opl_8C, opl_8D, opl_8E, opl_8F,
   opl_90, opl_91, opl_92, opl_93, opl_94, opl_95, opl_96, opl_97,
   opl_98, opl_99, opl_9A, opl_9B, opl_9C, opl_9D, opl_9E, opl_9F,
   opl_A0, opl_A1, opl_A2, opl_A3, opl_A4, opl_A5, opl_A6, opl_A7,
   opl_A8, opl_A9, opl_AA, opl_AB, opl_AC, opl_AD, opl_AE, opl_AF,
   opl_B0, opl_B1, opl_B2, opl_B3, opl_B4, opl_B5, opl_B6, opl_B7,
   opl_B8, opl_B9, opl_BA, opl_BB, opl_BC, opl_BD, opl_BE, opl_BF,

   opl_C0, opl_C1, opl_C2, opl_C3, opl_C4, opl_C5, opl_C6, opl_C7,
   opl_C8, opl_C9, opl_CA, opl_CB, opl_CC, opl_CD, opl_CE, opl_CF,
   opl_D0, opl_D1, opl_D2, opl_D3, opl_D4, opl_D5, opl_D6, opl_D7,
   opl_D8, opl_D9, opl_DA, opl_DB, opl_DC, opl_DD, opl_DE, opl_DF,
   opl_E0, opl_E1, opl_E2, opl_E3, opl_E4, opl_E5, opl_E6, opl_E7,
   opl_E8, opl_E9, opl_EA, opl_EB, opl_EC, opl_ED, opl_EE, opl_EF,
   opl_F0, opl_F1, opl_F2, opl_F3, opl_F4, opl_F5, opl_F6, opl_F7,
   opl_F8, opl_F9, opl_FA, opl_FB, opl_FC, opl_FD, opl_FE, opl_FF,

};

Z80OPCODE op_CB(Z80CPU *cpu, int tact0, Z80Regs *rf)
{
    int tact_ = tact0;
    uint8_t opcode = Z80M1(cpu);

    cpu->prefix = 0xCB;  // after the M1 (the opword store cleared it)

    // Tail-dispatch the CB-prefixed opcode (cpu->opcode already holds it
    // from the M1 fetch; the callee runs the shared epilogue).
    return (logic_opcode[opcode])(cpu, tact_, rf);
}

