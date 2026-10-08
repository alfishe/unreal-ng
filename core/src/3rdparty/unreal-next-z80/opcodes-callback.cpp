// opcodes-callback.cpp - the opcode units on the host bus (the only bus of
// the Z80N fork; unreal-z80 also builds them for a flat and a paged bus).
//
// Every memory access calls the host callback directly. Changes against
// unreal-z80 0.5.0 (README.md): the read callback gets the bus-cycle kind;
// the clock is taken back from cpu->t after every callback (the host's
// external /WAIT, contention, Z80nCpuAddWaitStates); a halted CPU's M1 quantum is a real
// M1 read at PC. The opcode bodies live in the opcodes-*.inc units.

#include "z80ncpu-dispatch.h"

namespace Z80nCb
{

// Memory read: 3 T-states. tact_ is the caller's register accumulator
// (Z80N_T_BEGIN); the current T is published into cpu->t before the host
// callback (hosts inspect it there) and taken back after it (the host may
// have added its /WAIT). Callbacks are never null (stubs installed by the
// API), so no guard here.
static Z80NINLINE uint8_t Z80nRdKindT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t addr, Z80nCpuAccessKind kind, int& tact)
{
    Z80nContendRT(rf, cpu, addr, kind, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->memRead(cpu, addr, kind, cpu->memReadData);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;  // reloaded, not kept across the call (see Z80nContendRT)
    return value;
}

// isExecution marks instruction-byte fetches at PC (operand/displacement/
// address bytes) - a 3 T cycle of kind Z80nCpuAccessOperand; the M1 cycle
// itself goes through Z80nM1T. 
static Z80NINLINE uint8_t Z80nRdT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t addr, bool isExecution, int& tact)
{
    const uint8_t value = Z80nRdKindT(rf, cpu, addr, isExecution ? Z80nCpuAccessOperand : Z80nCpuAccessRead, tact);
    return value;
}

static Z80NINLINE uint8_t Z80nRdT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t addr, int& tact)
{
    const uint8_t value = Z80nRdKindT(rf, cpu, addr, Z80nCpuAccessRead, tact);
    return value;
}

// Memory write: 3 T-states, published before the host callback, taken back after it.
static Z80NINLINE void Z80nWdT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t addr, uint8_t val, int& tact)
{
    Z80nContendRT(rf, cpu, addr, Z80nCpuAccessWrite, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    cpu->memWrite(cpu, addr, val, cpu->memWriteData);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;
}

// Opcode fetch cycle: refresh tick, 4 T total (3 published at the fetch
// callback, +1 accumulated). PC is advanced before the
// fetch call (z80ex semantics): the post-call path then needs no
// reload-and-increment of it.
// M1 at a PC the caller already holds (Step: read once, also stored as
// m1pc - a reload after that store would be needed otherwise, since the
// context may alias the register file as far as the compiler knows).
static Z80NINLINE uint8_t Z80nM1AtT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t addr, int& tact)
{
    Z80N_R_INC(rf);  // refresh tick, bit 7 of r_low kept (see z80ncpu-internal.h)
    rf->pc = static_cast<uint16_t>(addr + 1);
    const uint8_t opcode = Z80nRdKindT(rf, cpu, addr, Z80nCpuAccessM1, tact);
    cpu->opword = opcode;  // opcode + prefix byte cleared, one store
    tact += 1;

    return opcode;
}

static Z80NINLINE uint8_t Z80nM1T(Z80nRegs*& rf, Z80nCPU* cpu, int& tact)
{
    return Z80nM1AtT(rf, cpu, rf->pc, tact);
}

// Port I/O: the surrounding T is owned by the handler via cputact; the
// accumulator is published so the port callback observes the current T, and
// taken back after it. Handlers call these one T into the IO cycle (the IORQ
// T-state, see op_D3), so the cycle start is tact - 1 for the pre-IORQ
// contention hook; the post hook extends the cycle
// after the access.
static Z80NINLINE uint8_t Z80nPinT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t port, int& tact)
{
    Z80nContendRT(rf, cpu, port, Z80nCpuAccessPortIn, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->in(port);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;
    Z80nContendRT(rf, cpu, port, Z80nCpuAccessPortInPost, tact, tact);
    return value;
}

static Z80NINLINE void Z80nPoutT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t port, uint8_t val, int& tact)
{
    Z80nContendRT(rf, cpu, port, Z80nCpuAccessPortOut, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    cpu->out(port, val);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;
    Z80nContendRT(rf, cpu, port, Z80nCpuAccessPortOutPost, tact, tact);
}

// HALT quantum (z80nstep.inc): one M1 cycle at the byte after the HALT (the
// Z80's PC points past it; the engine keeps PC on the HALT), read through the
// host like any opcode fetch - the board sees these cycles and may stretch
// them - 4 T plus the host's waits.
// unreal-ng docs/inprogress/2026-10-02-halt-fetch-address
static Z80NINLINE void Z80nHaltT(Z80nRegs*& rf, Z80nCPU* cpu, int& tact)
{
    const uint16_t fetch = static_cast<uint16_t>(rf->pc + 1);
    static_cast<void>(Z80nRdKindT(rf, cpu, fetch, Z80nCpuAccessM1, tact));
    tact += 1;
}

// Internal (no-MREQ) T-states with addr on the address bus (cpuidle): the
// plain count without a contention hook; with one, one Z80nCpuAccessInternal
// call per T-state at its start, its waits inserted before that T. No MREQ,
// so no waits but the hook's.
static Z80NINLINE void Z80nIdleT(Z80nRegs*& rf, Z80nCPU* cpu, uint16_t addr, int n, int& tact)
{
    if (__builtin_expect(cpu->contend == nullptr, 1))
    {
        tact += n;
        return;
    }
    for (int i = 0; i < n; i++)
    {
        tact += Z80nContendSlow(cpu, addr, Z80nCpuAccessInternal, tact);
        tact += 1;
    }
    rf = cpu->regs;  // reloaded after the out-of-line hook (see Z80nContendRT)
}



// NEXTREG n,x / n,A: no bus cycle of its own; the host gets (register, value) with cpu->t published at the cycle where
// the core performs the write, and may change the clock (a CPU speed switch) like any callback.
static Z80NINLINE void Z80nNextRegWriteT(Z80nRegs*& rf, Z80nCPU* cpu, uint8_t reg, uint8_t value, int& tact)
{
    if (cpu->nextRegFn)
    {
        cpu->t = static_cast<uint32_t>(tact);
        cpu->nextRegFn(cpu, reg, value, cpu->nextRegData);
        tact = static_cast<int>(cpu->t);
        rf = cpu->regs;
    }
}

// Call-site shims: thread the caller's tact_ accumulator into the T-aware
// primitives while keeping the ported opcode bodies' call shapes unchanged
// (including the two Z80nRd arities: with/without the isExecution flag).
#define Z80nRd(...) Z80nRdT(rf, __VA_ARGS__, tact_)
#define Z80nWd(cpu, addr, val) Z80nWdT(rf, cpu, addr, val, tact_)
#define Z80nM1(cpu) Z80nM1T(rf, cpu, tact_)
#define Z80nPin(cpu, port) Z80nPinT(rf, cpu, port, tact_)
#define Z80nPout(cpu, port, val) Z80nPoutT(rf, cpu, port, val, tact_)
#define Z80nNextRegWrite(cpu, reg, value, tact) Z80nNextRegWriteT(rf, cpu, reg, value, tact)
#define cpuidle(addr, n) Z80nIdleT(rf, cpu, static_cast<uint16_t>(addr), (n), tact_)

#include "z80ncpu-opcodes.inc"
#include "opcodes-base.inc"
#include "opcodes-cb.inc"
#include "opcodes-dd.inc"
#include "opcodes-fd.inc"
#include "opcodes-z80n.inc"
#include "opcodes-ed.inc"
#include "opcodes-xxcb.inc"
#include "z80nstep.inc"

}  // namespace Z80nCb
