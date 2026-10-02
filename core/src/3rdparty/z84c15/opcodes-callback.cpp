// opcodes-callback.cpp - the opcode units on the host bus (the only bus of
// the Z84C15 fork; unreal-z80 also builds them for a flat and a paged bus).
//
// Every memory access calls the host callback directly. Changes against
// unreal-z80 0.5.0 (README.md): the read callback gets the bus-cycle kind;
// the clock is taken back from cpu->t after every callback (the host's
// external /WAIT, Z84CpuAddWaitStates); the on-chip wait generator adds its
// programmed waits after each access; a halted CPU's M1 quantum is a real
// M1 read at PC. The opcode bodies live in the opcodes-*.inc units.

#include "z84cpu-dispatch.h"

namespace Z84Cb
{

// Memory read: 3 T-states. tact_ is the caller's register accumulator
// (Z84_T_BEGIN); the current T is published into cpu->t before the host
// callback (hosts inspect it there) and taken back after it (the host may
// have added its /WAIT). Callbacks are never null (stubs installed by the
// API), so no guard here.
static Z84INLINE uint8_t Z84RdKindT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, int& tact)
{
    Z84ContendRT(rf, cpu, addr, kind, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->memRead(cpu, addr, kind, cpu->memReadData);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;  // reloaded, not kept across the call (see Z84ContendRT)
    return value;
}

// isExecution marks instruction-byte fetches at PC (operand/displacement/
// address bytes) - a 3 T cycle of kind Z84CpuAccessOperand; the M1 cycle
// itself goes through Z84M1T. Both get the chip's memory waits.
static Z84INLINE uint8_t Z84RdT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, bool isExecution, int& tact)
{
    const uint8_t value = Z84RdKindT(rf, cpu, addr, isExecution ? Z84CpuAccessOperand : Z84CpuAccessRead, tact);
    Z84WaitMemRT(rf, cpu, addr, tact);
    return value;
}

static Z84INLINE uint8_t Z84RdT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, int& tact)
{
    const uint8_t value = Z84RdKindT(rf, cpu, addr, Z84CpuAccessRead, tact);
    Z84WaitMemRT(rf, cpu, addr, tact);
    return value;
}

// Memory write: 3 T-states, published before the host callback, taken back after it.
static Z84INLINE void Z84WdT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, uint8_t val, int& tact)
{
    Z84ContendRT(rf, cpu, addr, Z84CpuAccessWrite, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    cpu->memWrite(cpu, addr, val, cpu->memWriteData);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;
    Z84WaitMemRT(rf, cpu, addr, tact);
}

// Opcode fetch cycle: refresh tick, 4 T total (3 published at the fetch
// callback, the chip's M1 waits, +1 accumulated). PC is advanced before the
// fetch call (z80ex semantics): the post-call path then needs no
// reload-and-increment of it.
// M1 at a PC the caller already holds (Step: read once, also stored as
// m1pc - a reload after that store would be needed otherwise, since the
// context may alias the register file as far as the compiler knows).
static Z84INLINE uint8_t Z84M1AtT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, int& tact)
{
    Z84_R_INC(rf);  // refresh tick, bit 7 of r_low kept (see z84cpu-internal.h)
    rf->pc = static_cast<uint16_t>(addr + 1);
    const uint8_t opcode = Z84RdKindT(rf, cpu, addr, Z84CpuAccessM1, tact);
    cpu->opword = opcode;  // opcode + prefix byte cleared, one store
    Z84WaitM1RT(rf, cpu, addr, opcode, tact);
    tact += 1;

    return opcode;
}

static Z84INLINE uint8_t Z84M1T(Z84Regs*& rf, Z84CPU* cpu, int& tact)
{
    return Z84M1AtT(rf, cpu, rf->pc, tact);
}

// Port I/O: the surrounding T is owned by the handler via cputact; the
// accumulator is published so the port callback observes the current T, and
// taken back after it. Handlers call these one T into the IO cycle (the IORQ
// T-state, see op_D3), so the cycle start is tact - 1 for the pre-IORQ
// contention hook; the post hook and the chip's I/O waits extend the cycle
// after the access.
static Z84INLINE uint8_t Z84PinT(Z84Regs*& rf, Z84CPU* cpu, uint16_t port, int& tact)
{
    Z84ContendRT(rf, cpu, port, Z84CpuAccessPortIn, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->in(port);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;
    Z84ContendRT(rf, cpu, port, Z84CpuAccessPortInPost, tact, tact);
    Z84WaitIoRT(rf, cpu, port, tact);
    return value;
}

static Z84INLINE void Z84PoutT(Z84Regs*& rf, Z84CPU* cpu, uint16_t port, uint8_t val, int& tact)
{
    Z84ContendRT(rf, cpu, port, Z84CpuAccessPortOut, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    cpu->out(port, val);
    tact = static_cast<int>(cpu->t);
    rf = cpu->regs;
    Z84ContendRT(rf, cpu, port, Z84CpuAccessPortOutPost, tact, tact);
    Z84WaitIoRT(rf, cpu, port, tact);
}

// HALT quantum (z84step.inc): one M1 cycle at PC (the HALT opcode), read
// through the host like any opcode fetch - the board sees these cycles and
// may stretch them - with the chip's M1 waits; 4 T plus waits.
static Z84INLINE void Z84HaltT(Z84Regs*& rf, Z84CPU* cpu, int& tact)
{
    const uint16_t pc = rf->pc;
    const uint8_t opcode = Z84RdKindT(rf, cpu, pc, Z84CpuAccessM1, tact);
    Z84WaitM1RT(rf, cpu, pc, opcode, tact);
    tact += 1;
}

// Internal (no-MREQ) T-states with addr on the address bus (cpuidle): the
// plain count without a contention hook; with one, one Z84CpuAccessInternal
// call per T-state at its start, its waits inserted before that T. No MREQ,
// so no programmed waits.
static Z84INLINE void Z84IdleT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, int n, int& tact)
{
    if (__builtin_expect(cpu->contend == nullptr, 1))
    {
        tact += n;
        return;
    }
    for (int i = 0; i < n; i++)
    {
        tact += Z84ContendSlow(cpu, addr, Z84CpuAccessInternal, tact);
        tact += 1;
    }
    rf = cpu->regs;  // reloaded after the out-of-line hook (see Z84ContendRT)
}


// Call-site shims: thread the caller's tact_ accumulator into the T-aware
// primitives while keeping the ported opcode bodies' call shapes unchanged
// (including the two Z84Rd arities: with/without the isExecution flag).
#define Z84Rd(...) Z84RdT(rf, __VA_ARGS__, tact_)
#define Z84Wd(cpu, addr, val) Z84WdT(rf, cpu, addr, val, tact_)
#define Z84M1(cpu) Z84M1T(rf, cpu, tact_)
#define Z84Pin(cpu, port) Z84PinT(rf, cpu, port, tact_)
#define Z84Pout(cpu, port, val) Z84PoutT(rf, cpu, port, val, tact_)
#define cpuidle(addr, n) Z84IdleT(rf, cpu, static_cast<uint16_t>(addr), (n), tact_)

#include "z84cpu-opcodes.inc"
#include "opcodes-base.inc"
#include "opcodes-cb.inc"
#include "opcodes-dd.inc"
#include "opcodes-fd.inc"
#include "opcodes-ed.inc"
#include "opcodes-xxcb.inc"
#include "z84step.inc"

}  // namespace Z84Cb
