// z80step.inc - one Z80 instruction step, specialized per bus mode.
//
// Included inside namespace Z80Flat / Z80Cb by opcodes-flat.cpp and
// opcodes-callback.cpp; Z80Rd/Z80Wd/Z80M1 and the dispatch tables resolve to
// the enclosing mode's compile-time variants. Body is a port of the core's
// Z80Step() (halted branch, M1 fetch + dispatch, Q maintenance) identical
// for both modes; the post-EI interrupt shadow is derived at
// Z80CpuIntPossible time from cpu->opcode instead of stored per step, and
// the RETI hook fires from the ope_4D handler itself (z80ex-style).

// HALT: no opcode processing until INT/NMI releases the CPU. Burns the 4-T
// quantum and ticks R (core: 1 T per loop iteration, R every 4th). The halted
// CPU keeps running M1 cycles at PC (the HALT opcode): Z80HaltT reports each
// quantum to the contention hook as an M1 cycle on the buses that fire it (a
// no-op on the flat bus).
//
// Placement is chosen per bus build, from the disassembly of Step's entry:
// the halted path may call the hook, and wherever that call sits it shapes
// the prologue of the hot path.
//   - paged (Z80_HALT_OUT_OF_LINE): Step's hot path needs no callee-saved
//     registers (direct page reads); an inline hook call would give every
//     step a full prologue (measured -1% frames/s), so the halted quantum
//     lives in a cold function and Step keeps its leaf entry (+5% vs inline);
//   - callback / flat: the callback Step already saves registers for the host
//     read callback, so the inline branch adds nothing to the hot path,
//     while moving it out of line made the compiler save one more register
//     pair on every step (measured -5% on the callback bus).
static Z80INLINE int HaltedQuantum(Z80CPU* cpu, Z80Regs* rf)
{
    int tact_ = static_cast<int>(cpu->t);
    Z80HaltT(rf, cpu, tact_);
    cpu->t = static_cast<uint32_t>(tact_);
    Z80_R_INC(rf);  // refresh tick, bit 7 of r_low kept
    rf->q = 0;    // the halted CPU executes NOPs, which do not write F
    return static_cast<int>(cpu->t);
}

#ifdef Z80_HALT_OUT_OF_LINE
Z80COLD static int HaltedStep(Z80CPU* cpu) { return HaltedQuantum(cpu, cpu->regs); }
#endif

int Step(Z80CPU* cpu)
{
    // The active register file, loaded once per instruction and passed to
    // the handler as an argument: every register access in the instruction
    // is then one base-register access (no reload of cpu->regs after the
    // byte stores that may alias it).
    Z80Regs* rf = cpu->regs;
#ifdef Z80_HALT_OUT_OF_LINE
    if (__builtin_expect(rf->halted != 0, 0))
        return HaltedStep(cpu);
#else
    if (rf->halted)
        return HaltedQuantum(cpu, rf);
#endif

    // Seed the accumulator once per instruction and pass it to the handler
    // as the tact0 argument (see the T-model notes in z80cpu-internal.h).
    // Step ends with a jump into the handler; the handler ends with a jump
    // into Z80FinishStep, which publishes cpu->t, maintains Q and returns
    // the T after the instruction straight to the host (Z80CpuStepEntry
    // contract; Z80CpuStep subtracts for the delta).
    const int t0 = static_cast<int>(cpu->t);  // instruction start T (accumulator seed)

    // Regular bus cycle: fetch opcode (M1, which also clears the prefix
    // byte through the opword store) and dispatch.
    const uint16_t pc = rf->pc;
    cpu->m1pc = pc;
    int tact_ = t0;
    const uint8_t opcode = Z80M1AtT(rf, cpu, pc, tact_);
    return (normal_opcode[opcode])(cpu, tact_, rf);
}
