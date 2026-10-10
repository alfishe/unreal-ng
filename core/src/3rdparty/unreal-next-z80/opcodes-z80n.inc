// opcodes-z80n.inc - the Z80N extended instructions (all ED-prefixed), included by opcodes-callback.cpp before
// opcodes-ed.inc, which puts them into the ED table.
//
// Sources: the instruction table of https://table.specnext.dev/ (sizes and T-states; the "cycles" column is the
// total at 3.5 MHz) and the FPGA core's T80 microcode (cores/zxnext/src/cpu/t80n_mcode.vhd, t80n.vhd: the shape of
// the machine cycles and the flags, which differ from the table in the block-copy family: the core sets the flags
// like LDI). The cycle shapes matter for the host's contention hook:
//   - the two M1 cycles (ED, then the opcode) are 8 T and are already spent when a handler starts;
//   - "8 T" instructions have no further cycle;
//   - operand reads are normal 3 T cycles (kind Operand); ADD rr,nn gives each operand cycle a fourth idle T.
// Every handler is written like the other ED handlers: Z80N_T_BEGIN, bus primitives, Z80N_RETURN_Q.

// ---------------------------------------------------------------------------------------------------------------------
// Register-only instructions, 8 T

Z80NOPCODE ope_23(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // swapnib
    Z80N_T_BEGIN(cpu);
    rf->a = static_cast<uint8_t>((rf->a << 4) | (rf->a >> 4));
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_24(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // mirror a
    Z80N_T_BEGIN(cpu);
    uint8_t v = rf->a;
    v = static_cast<uint8_t>(((v & 0xF0) >> 4) | ((v & 0x0F) << 4));
    v = static_cast<uint8_t>(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
    v = static_cast<uint8_t>(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
    rf->a = v;
    Z80N_RETURN_Q(0, tact_);
}

// Barrel shifts of DE by B (the count is B bits 4:0, for BRLC bits 3:0). No flags.
Z80NOPCODE ope_28(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // bsla de,b
    Z80N_T_BEGIN(cpu);
    rf->de = static_cast<uint16_t>(static_cast<uint32_t>(rf->de) << (rf->b & 31));
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_29(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // bsra de,b
    Z80N_T_BEGIN(cpu);
    rf->de = static_cast<uint16_t>(static_cast<int32_t>(static_cast<int16_t>(rf->de)) >> (rf->b & 31));
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_2A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // bsrl de,b
    Z80N_T_BEGIN(cpu);
    rf->de = static_cast<uint16_t>(static_cast<uint32_t>(rf->de) >> (rf->b & 31));
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_2B(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // bsrf de,b (the vacated top bits are set to one)
    Z80N_T_BEGIN(cpu);
    rf->de = static_cast<uint16_t>(~(static_cast<uint32_t>(static_cast<uint16_t>(~rf->de)) >> (rf->b & 31)));
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_2C(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // brlc de,b (rotate left by B bits 3:0)
    Z80N_T_BEGIN(cpu);
    const unsigned n = rf->b & 15;
    const uint32_t v = rf->de;
    rf->de = static_cast<uint16_t>((v << n) | (v >> ((16 - n) & 15)));
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_30(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // mul d,e
    Z80N_T_BEGIN(cpu);
    rf->de = static_cast<uint16_t>(rf->d * rf->e);
    Z80N_RETURN_Q(0, tact_);
}

// ADD rr,A: the sum goes to rr; the carry flag is cleared (the core assigns F.C from a bit it never sets; the
// instruction table calls the carry "not preserved"), the other flags stay.
Z80NOPCODE ope_31(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add hl,a
    Z80N_T_BEGIN(cpu);
    rf->hl = static_cast<uint16_t>(rf->hl + rf->a);
    rf->f &= static_cast<uint8_t>(~CF);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_32(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add de,a
    Z80N_T_BEGIN(cpu);
    rf->de = static_cast<uint16_t>(rf->de + rf->a);
    rf->f &= static_cast<uint8_t>(~CF);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_33(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add bc,a
    Z80N_T_BEGIN(cpu);
    rf->bc = static_cast<uint16_t>(rf->bc + rf->a);
    rf->f &= static_cast<uint8_t>(~CF);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_93(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // pixeldn: HL one pixel line down in ULA screen addressing
    Z80N_T_BEGIN(cpu);
    uint16_t hl = rf->hl;
    if ((hl & 0x0700) != 0x0700)
        hl = static_cast<uint16_t>(hl + 0x0100);
    else if ((hl & 0x00E0) != 0x00E0)
        hl = static_cast<uint16_t>((hl & 0xF8FF) + 0x0020);
    else
        hl = static_cast<uint16_t>((hl & 0xF81F) + 0x0800);
    rf->hl = hl;
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_94(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // pixelad: HL = screen address of pixel (x = E, y = D)
    Z80N_T_BEGIN(cpu);
    rf->hl = static_cast<uint16_t>(0x4000 + ((rf->d & 0xC0) << 5) + ((rf->d & 0x07) << 8) + ((rf->d & 0x38) << 2) +
                                   (rf->e >> 3));
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_95(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // setae: A = pixel mask of the x coordinate in E
    Z80N_T_BEGIN(cpu);
    rf->a = static_cast<uint8_t>(0x80 >> (rf->e & 7));
    Z80N_RETURN_Q(0, tact_);
}

// ---------------------------------------------------------------------------------------------------------------------
// With operands

Z80NOPCODE ope_27(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // test n: flags as AND A,n, A unchanged (11 T)
    Z80N_T_BEGIN(cpu);
    const uint8_t n = Z80nRd(cpu, rf->pc++, true);
    rf->f = log_f[rf->a & n] | HF;
    Z80N_RETURN_Q(0x28, tact_);
}

// add rr,nn: 16 T, each operand cycle carries one idle T (the core gives every cycle of the instruction 4 T)
static Z80NINLINE void Z80nAddRrNn(Z80nCPU *cpu, Z80nRegs *&rf, uint16_t &rr, int &tact_)
{
    const uint16_t pc0 = rf->pc;
    const uint8_t lo = Z80nRd(cpu, rf->pc++, true);
    cpuidle(pc0, 1);
    const uint8_t hi = Z80nRd(cpu, rf->pc++, true);
    cpuidle(static_cast<uint16_t>(pc0 + 1), 1);
    rr = static_cast<uint16_t>(rr + (lo | (hi << 8)));
}

Z80NOPCODE ope_34(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add hl,nn
    Z80N_T_BEGIN(cpu);
    Z80nAddRrNn(cpu, rf, rf->hl, tact_);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_35(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add de,nn
    Z80N_T_BEGIN(cpu);
    Z80nAddRrNn(cpu, rf, rf->de, tact_);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_36(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // add bc,nn
    Z80N_T_BEGIN(cpu);
    Z80nAddRrNn(cpu, rf, rf->bc, tact_);
    Z80N_RETURN_Q(0, tact_);
}

// push nn (23 T): the operand is big-endian (high byte first); 3 T idle at the stack pointer, then the two writes
Z80NOPCODE ope_8A(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // push nn
    Z80N_T_BEGIN(cpu);
    const uint8_t hi = Z80nRd(cpu, rf->pc++, true);
    const uint8_t lo = Z80nRd(cpu, rf->pc++, true);
    cpuidle(rf->sp, 3);
    Z80nWd(cpu, --rf->sp, hi);
    Z80nWd(cpu, --rf->sp, lo);
    Z80N_RETURN_Q(0, tact_);
}

// nextreg: no port cycle; the host gets (register, value). t80n_mcode.vhd (X"91" / X"92"): after the operand cycles the T80 runs two
// (n,nn) or one (n,A) more 3 T machine cycles whose NoRead is '0' (t80na.vhd: MREQ and RD at T1) at the address of the last operand
// byte - PC is only incremented at the end - so they are real, ULA-contended memory reads whose data is dropped. The write is driven
// from the start of the first of those cycles (the host's callback sees that T: +14 for n,nn, +11 for n,A from the ED prefix).
Z80NOPCODE ope_91(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // nextreg n,nn (20 T)
    Z80N_T_BEGIN(cpu);
    const uint8_t reg = Z80nRd(cpu, rf->pc++, true);
    const uint8_t value = Z80nRd(cpu, rf->pc++, true);
    Z80nNextRegWrite(cpu, reg, value, tact_);  // Z80N_dout_o rises with the first of the two cycles
    (void)Z80nRd(cpu, static_cast<uint16_t>(rf->pc - 1), true);
    (void)Z80nRd(cpu, static_cast<uint16_t>(rf->pc - 1), true);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_92(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // nextreg n,a (17 T)
    Z80N_T_BEGIN(cpu);
    const uint8_t reg = Z80nRd(cpu, rf->pc++, true);
    Z80nNextRegWrite(cpu, reg, rf->a, tact_);  // Z80N_dout_o rises with the first of the two cycles
    (void)Z80nRd(cpu, static_cast<uint16_t>(rf->pc - 1), true);
    (void)Z80nRd(cpu, static_cast<uint16_t>(rf->pc - 1), true);
    Z80N_RETURN_Q(0, tact_);
}

// ---------------------------------------------------------------------------------------------------------------------
// I/O

Z80NOPCODE ope_90(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // outinb: OUT (C),(HL); HL++ (16 T, B and the flags unchanged)
    Z80N_T_BEGIN(cpu);
    cpuidle(Z80N_IR(rf), 1);  // the 5th T of the second M1
    const uint8_t value = Z80nRd(cpu, rf->hl);
    cputact(1);
    Z80nPout(cpu, rf->bc, value);
    cputact(3);
    rf->hl = static_cast<uint16_t>(rf->hl + 1);
    Z80N_RETURN_Q(0, tact_);
}

Z80NOPCODE ope_98(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // jp (c): PC = (PC & #C000) | (IN (C) << 6) (13 T)
    Z80N_T_BEGIN(cpu);
    cputact(1);
    const uint8_t value = Z80nPin(cpu, rf->bc);
    cputact(4);
    rf->pc = static_cast<uint16_t>((rf->pc & 0xC000) | ((value << 6) & 0x3FFF));
    Z80N_RETURN_Q(0, tact_);
}

// ---------------------------------------------------------------------------------------------------------------------
// Block copies with an exclusion byte. The core sets the flags like LDI (S, Z, C kept; H = N = 0; P/V = BC != 0 after
// the decrement; X and Y from (A + byte) bits 3 and 1), also on every repeat, and does not put PC bits into X / Y.
// ldix / lddx are single steps (16 T), the repeating forms take 21 T per repeat, 16 on the last (the core: a 5 T
// "write" cycle - without the MREQ when the byte equals A - and a further 5 T cycle for the repeat).

static Z80NINLINE void Z80nBlockXFlags(Z80nRegs *rf, uint8_t value)
{
    uint8_t n = static_cast<uint8_t>(value + rf->a);
    n = static_cast<uint8_t>((n & F3) + ((n << 4) & F5));
    rf->f = static_cast<uint8_t>((rf->f & ~(NF | HF | PV | F3 | F5)) | n);
    if (rf->bc)
        rf->f |= PV;
}

// One element: read from `src`, write to DE unless the byte equals A, then the flags. Returns after the write cycle.
static Z80NINLINE void Z80nBlockXStep(Z80nCPU *cpu, Z80nRegs *&rf, uint16_t src, int &tact_)
{
    const uint8_t value = Z80nRd(cpu, src);
    rf->bc = static_cast<uint16_t>(rf->bc - 1);
    if (value != rf->a)
    {
        Z80nWd(cpu, rf->de, value);
        cpuidle(rf->de, 2);
    }
    else
    {
        cpuidle(rf->de, 5);
    }
    Z80nBlockXFlags(rf, value);
}

Z80NOPCODE ope_A4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ldix
    Z80N_T_BEGIN(cpu);
    Z80nBlockXStep(cpu, rf, rf->hl, tact_);
    rf->hl = static_cast<uint16_t>(rf->hl + 1);
    rf->de = static_cast<uint16_t>(rf->de + 1);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_AC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // lddx
    Z80N_T_BEGIN(cpu);
    Z80nBlockXStep(cpu, rf, rf->hl, tact_);
    rf->hl = static_cast<uint16_t>(rf->hl - 1);
    rf->de = static_cast<uint16_t>(rf->de + 1);
    Z80N_RETURN_Q(0x28, tact_);
}

static Z80NINLINE void Z80nBlockXRepeat(Z80nRegs *&rf, int &tact_, Z80nCPU *cpu)
{
    if (rf->bc)
    {
        rf->pc = static_cast<uint16_t>(rf->pc - 2);
        cpuidle(rf->de, 5);
    }
}

Z80NOPCODE ope_B4(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ldirx
    Z80N_T_BEGIN(cpu);
    Z80nBlockXStep(cpu, rf, rf->hl, tact_);
    rf->hl = static_cast<uint16_t>(rf->hl + 1);
    rf->de = static_cast<uint16_t>(rf->de + 1);
    Z80nBlockXRepeat(rf, tact_, cpu);
    Z80N_RETURN_Q(0x28, tact_);
}

Z80NOPCODE ope_BC(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // lddrx
    Z80N_T_BEGIN(cpu);
    Z80nBlockXStep(cpu, rf, rf->hl, tact_);
    rf->hl = static_cast<uint16_t>(rf->hl - 1);
    rf->de = static_cast<uint16_t>(rf->de + 1);
    Z80nBlockXRepeat(rf, tact_, cpu);
    Z80N_RETURN_Q(0x28, tact_);
}

// ldpirx: the source address is (HL & #FFF8) | (DE & 7); HL does not change; repeats like ldirx
Z80NOPCODE ope_B7(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ldpirx
    Z80N_T_BEGIN(cpu);
    Z80nBlockXStep(cpu, rf, static_cast<uint16_t>((rf->hl & 0xFFF8) | (rf->de & 7)), tact_);
    rf->de = static_cast<uint16_t>(rf->de + 1);
    Z80nBlockXRepeat(rf, tact_, cpu);
    Z80N_RETURN_Q(0x28, tact_);
}

// ldirscale (ED B6) is decoded by the FPGA core but its register effects are commented out in the VHDL: it behaves
// like ldirx (the scaling would add BC' to HL and DE' to DE). Implemented as that.
#define ope_B6 ope_B4

// ldws (14 T): (DE) = (HL); then L and D are incremented alone; flags as INC D
Z80NOPCODE ope_A5(Z80nCPU *cpu, int tact0, Z80nRegs *rf) { // ldws
    Z80N_T_BEGIN(cpu);
    const uint8_t value = Z80nRd(cpu, rf->hl);
    Z80nWd(cpu, rf->de, value);
    rf->l = static_cast<uint8_t>(rf->l + 1);
    inc8(rf, rf->d);
    Z80N_RETURN_Q(0x28, tact_);
}
