// z80nstep.inc - one instruction step on the callback bus.
//
// Included inside namespace Z80nCb by opcodes-callback.cpp; Z80nRd/Z80nWd/Z80nM1
// and the dispatch tables resolve to that build. Body is a port of the
// unreal-ng core's Z80Step() (halted branch, M1 fetch + dispatch, Q
// maintenance); the post-EI interrupt shadow is derived at
// Z80nCpuIntPossible time from cpu->opcode instead of stored per step, and
// the RETI hook fires from the ope_4D handler itself (z80ex-style). A step
// never runs more than one prefix ahead: a redundant DD/FD prefix is a step
// of its own (see ddfd_prefixes / PendingPrefixStep).

// HALT: no opcode processing until INT/NMI releases the CPU. The halted CPU
// keeps running M1 cycles at the byte after the HALT (PC stays on the HALT): Z80nHaltT reads that byte
// through the host as an M1 (unreal-z80 only reports the quantum to the
// contention hook), 4 T plus the chip's M1 waits, and R ticks.
static Z80NINLINE int HaltedQuantum(Z80nCPU* cpu, Z80nRegs* rf)
{
    int tact_ = static_cast<int>(cpu->t);
    Z80nHaltT(rf, cpu, tact_);
    cpu->t = static_cast<uint32_t>(tact_);
    Z80N_R_INC(rf);  // refresh tick, bit 7 of r_low kept
    rf->q = 0;    // the halted CPU executes NOPs, which do not write F
    return static_cast<int>(cpu->t);
}

// A redundant DD/FD prefix ended the previous step with the next prefix
// already fetched (ddfd_prefixes): the instruction it introduces starts at
// that prefix and continues with the byte after it.
static Z80NINLINE int PendingPrefixStep(Z80nCPU* cpu, Z80nRegs* rf)
{
    rf->halted = 0;
    cpu->m1pc = static_cast<uint16_t>(rf->pc - 1);
    return ddfd_prefixes(cpu, cpu->opcode, static_cast<int>(cpu->t), rf);
}

// Step's one slow path: the halted byte is non-zero for a HALT (bit 0) or a
// pending prefix (Z80N_PENDING_PREFIX). Out of line on every bus: Step then
// tests one byte and the compiler shrink-wraps the register saves past it.
// (unreal-z80 round 7 kept the halted quantum inline on the callback and flat buses,
// where it measured free; adding the pending-prefix branch to the inline
// block made the callback Step save one more register pair on every step,
// -7% frames/s. Out of line, all three buses measure within 1% of the
// pre-split build - see docs/hot-cold-path-guide.md 2.3.)
Z80NCOLD static int SlowStep(Z80nCPU* cpu)
{
    Z80nRegs* rf = cpu->regs;
    if (rf->halted & Z80N_PENDING_PREFIX)
        return PendingPrefixStep(cpu, rf);
    return HaltedQuantum(cpu, rf);
}

int Step(Z80nCPU* cpu)
{
    // The active register file, loaded once per instruction and passed to
    // the handler as an argument: every register access in the instruction
    // is then one base-register access (no reload of cpu->regs after the
    // byte stores that may alias it).
    Z80nRegs* rf = cpu->regs;
    if (__builtin_expect(rf->halted != 0, 0))
        return SlowStep(cpu);

    // Seed the accumulator once per instruction and pass it to the handler
    // as the tact0 argument (see the T-model notes in z80ncpu-internal.h).
    // Step ends with a jump into the handler; the handler ends with a jump
    // into Z80nFinishStep, which publishes cpu->t, maintains Q and returns
    // the T after the instruction straight to the host (Z80nCpuStepEntry
    // contract; Z80nCpuStep subtracts for the delta).
    const int t0 = static_cast<int>(cpu->t);  // instruction start T (accumulator seed)

    // Regular bus cycle: fetch opcode (M1, which also clears the prefix
    // byte through the opword store) and dispatch.
    const uint16_t pc = rf->pc;
    cpu->m1pc = pc;
    int tact_ = t0;
    const uint8_t opcode = Z80nM1AtT(rf, cpu, pc, tact_);
    return (normal_opcode[opcode])(cpu, tact_, rf);
}
