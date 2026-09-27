// z80step.inc - one Z80 instruction step, specialized per bus mode.
//
// Included inside namespace Z80Flat / Z80Cb by opcodes-flat.cpp and
// opcodes-callback.cpp; Z80Rd/Z80Wd/Z80M1 and the dispatch tables resolve to
// the enclosing mode's compile-time variants. Body is a port of the core's
// Z80Step() (halted branch, M1 fetch + dispatch, Q maintenance) identical
// for both modes; the post-EI interrupt shadow is derived at
// Z80CpuIntPossible time from cpu->opcode instead of stored per step, and
// the RETI hook fires from the ope_4D handler itself (z80ex-style). A step
// never runs more than one prefix ahead: a redundant DD/FD prefix is a step
// of its own (see ddfd_prefixes / PendingPrefixStep).

// HALT: no opcode processing until INT/NMI releases the CPU. Burns the 4-T
// quantum and ticks R (core: 1 T per loop iteration, R every 4th). The halted
// CPU keeps running M1 cycles at PC (the HALT opcode): Z80HaltT reports each
// quantum to the contention hook as an M1 cycle on the buses that fire it (a
// no-op on the flat bus).
static Z80INLINE int HaltedQuantum(Z80CPU* cpu, Z80Regs* rf)
{
    int tact_ = static_cast<int>(cpu->t);
    Z80HaltT(rf, cpu, tact_);
    cpu->t = static_cast<uint32_t>(tact_);
    Z80_R_INC(rf);  // refresh tick, bit 7 of r_low kept
    rf->q = 0;    // the halted CPU executes NOPs, which do not write F
    return static_cast<int>(cpu->t);
}

// A redundant DD/FD prefix ended the previous step with the next prefix
// already fetched (ddfd_prefixes): the instruction it introduces starts at
// that prefix and continues with the byte after it.
static Z80INLINE int PendingPrefixStep(Z80CPU* cpu, Z80Regs* rf)
{
    rf->halted = 0;
    cpu->m1pc = static_cast<uint16_t>(rf->pc - 1);
    return ddfd_prefixes(cpu, cpu->opcode, static_cast<int>(cpu->t), rf);
}

// Step's one slow path: the halted byte is non-zero for a HALT (bit 0) or a
// pending prefix (Z80_PENDING_PREFIX). Out of line on every bus: Step then
// tests one byte and the compiler shrink-wraps the register saves past it.
// (Round 7 kept the halted quantum inline on the callback and flat buses,
// where it measured free; adding the pending-prefix branch to the inline
// block made the callback Step save one more register pair on every step,
// -7% frames/s. Out of line, all three buses measure within 1% of the
// pre-split build - see docs/hot-cold-path-guide.md 2.3.)
Z80COLD static int SlowStep(Z80CPU* cpu)
{
    Z80Regs* rf = cpu->regs;
    if (rf->halted & Z80_PENDING_PREFIX)
        return PendingPrefixStep(cpu, rf);
    return HaltedQuantum(cpu, rf);
}

int Step(Z80CPU* cpu)
{
    // The active register file, loaded once per instruction and passed to
    // the handler as an argument: every register access in the instruction
    // is then one base-register access (no reload of cpu->regs after the
    // byte stores that may alias it).
    Z80Regs* rf = cpu->regs;
    if (__builtin_expect(rf->halted != 0, 0))
        return SlowStep(cpu);

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
