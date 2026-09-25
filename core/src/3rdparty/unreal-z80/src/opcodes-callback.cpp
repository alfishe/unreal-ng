// opcodes-callback.cpp - host-bus (z80ex-style) specialization of the opcode
// units.
//
// Compiled exclusively for the Z80CpuSetMemoryBus integration: every memory
// access calls the host callback directly - no per-access bus-mode branch.
// This is the mode z80ex itself uses, so the head-to-head benchmark compares
// identical integration shapes. The opcode bodies live in the opcodes-*.inc
// units, shared with opcodes-flat.cpp.

#include "z80cpu-dispatch.h"

namespace Z80Cb
{

// Memory read: 3 T-states. isExecution marks an M1 opcode fetch. tact_ is
// the caller's register accumulator (Z80_T_BEGIN); the current T is
// published into cpu->t before the host callback (hosts inspect it there).
// Callbacks are never null (stubs installed by the API), so no guard here.
static Z80INLINE uint8_t Z80RdKindT(Z80CPU* cpu, uint16_t addr, bool isExecution, Z80CpuAccessKind kind, int& tact)
{
    Z80ContendT(cpu, addr, kind, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    return cpu->memRead(cpu, addr, isExecution ? 1 : 0, cpu->memReadData);
}

// isExecution marks instruction-byte fetches at PC (operand/displacement/
// address bytes; hosts use it for execution tracking) - a 3 T cycle, so it
// contends as Z80CpuAccessOperand; the M1 cycle itself goes through Z80M1T.
static Z80INLINE uint8_t Z80RdT(Z80CPU* cpu, uint16_t addr, bool isExecution, int& tact)
{
    return Z80RdKindT(cpu, addr, isExecution, isExecution ? Z80CpuAccessOperand : Z80CpuAccessRead, tact);
}

static Z80INLINE uint8_t Z80RdT(Z80CPU* cpu, uint16_t addr, int& tact)
{
    return Z80RdKindT(cpu, addr, false, Z80CpuAccessRead, tact);
}

// Memory write: 3 T-states, published before the host callback.
static Z80INLINE void Z80WdT(Z80CPU* cpu, uint16_t addr, uint8_t val, int& tact)
{
    Z80ContendT(cpu, addr, Z80CpuAccessWrite, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    cpu->memWrite(cpu, addr, val, cpu->memWriteData);
}

// Opcode fetch cycle: refresh tick, 4 T total (3 published at the fetch
// callback, +1 accumulated). PC is advanced before the fetch call (z80ex
// semantics): the post-call path then needs no reload-and-increment of it.
static Z80INLINE uint8_t Z80M1T(Z80CPU* cpu, int& tact)
{
    // R refresh tick: R7 lives in r_hi and every reader of r_low masks
    // bit 7 (LD A,R, the register API), so a plain 8-bit increment is
    // equivalent to the masked form - one instruction instead of three.
    cpu->r_low++;
    const uint16_t addr = cpu->pc++;
    const uint8_t opcode = Z80RdKindT(cpu, addr, true, Z80CpuAccessM1, tact);
    cpu->opword = opcode;  // opcode + prefix byte cleared, one store
    tact += 1;

    return opcode;
}

// Port I/O: the surrounding T is owned by the handler via cputact; the
// accumulator is published so the port callback observes the current T.
// Handlers call these one T into the IO cycle (the IORQ T-state, see
// op_D3), so the cycle start is tact - 1 for the pre-IORQ contention hook;
// the post hook extends the cycle after the access (ULA "C:1, C:3").
static Z80INLINE uint8_t Z80PinT(Z80CPU* cpu, uint16_t port, int& tact)
{
    Z80ContendT(cpu, port, Z80CpuAccessPortIn, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->in(port);
    Z80ContendT(cpu, port, Z80CpuAccessPortInPost, tact, tact);
    return value;
}

static Z80INLINE void Z80PoutT(Z80CPU* cpu, uint16_t port, uint8_t val, int& tact)
{
    Z80ContendT(cpu, port, Z80CpuAccessPortOut, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    cpu->out(port, val);
    Z80ContendT(cpu, port, Z80CpuAccessPortOutPost, tact, tact);
}

// HALT quantum (z80step.inc): one 4 T M1 cycle at PC, reported to the
// contention hook like any M1 (waits extend the quantum).
static Z80INLINE void Z80HaltT(Z80CPU* cpu, int& tact)
{
    Z80ContendT(cpu, cpu->pc, Z80CpuAccessM1, tact, tact);
    tact += 4;
}

// Call-site shims: thread the caller's tact_ accumulator into the T-aware
// primitives while keeping the ported opcode bodies' call shapes unchanged
// (including the two Z80Rd arities: with/without the isExecution flag).
#define Z80Rd(...) Z80RdT(__VA_ARGS__, tact_)
#define Z80Wd(cpu, addr, val) Z80WdT(cpu, addr, val, tact_)
#define Z80M1(cpu) Z80M1T(cpu, tact_)
#define Z80Pin(cpu, port) Z80PinT(cpu, port, tact_)
#define Z80Pout(cpu, port, val) Z80PoutT(cpu, port, val, tact_)

#include "z80cpu-opcodes.inc"
#include "opcodes-base.inc"
#include "opcodes-cb.inc"
#include "opcodes-dd.inc"
#include "opcodes-fd.inc"
#include "opcodes-ed.inc"
#include "opcodes-xxcb.inc"
#include "z80step.inc"

}  // namespace Z80Cb
