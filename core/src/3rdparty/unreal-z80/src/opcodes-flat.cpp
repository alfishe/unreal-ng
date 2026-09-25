// opcodes-flat.cpp - flat-memory specialization of the opcode units.
//
// Compiled exclusively for the Z80CpuAttachMemory fast path: every memory
// access inlines the attached 64K array (cpu->mem, guaranteed non-null while
// this dispatch set is active). The opcode bodies live in the opcodes-*.inc
// units, shared with opcodes-callback.cpp; only the bus primitives below
// differ between the two builds.

#include "z80cpu-dispatch.h"

namespace Z80Flat
{

// Memory read: 3 T-states. tact_ is the caller's register accumulator
// (Z80_T_BEGIN); on the flat array nothing observes intermediate T, so the
// accumulator is NOT published here - the handler's returned final T
// (published by Step) is the only cpu->t store per instruction. isExecution
// marks an M1 opcode fetch for hosts emulating memory contention -
// irrelevant on the flat array.
static Z80INLINE uint8_t Z80RdT(Z80Regs*&, Z80CPU* cpu, uint16_t addr, bool, int& tact)
{
    tact += 3;
    return cpu->mem[addr];
}

static Z80INLINE uint8_t Z80RdT(Z80Regs*& rf, Z80CPU* cpu, uint16_t addr, int& tact)
{
    return Z80RdT(rf, cpu, addr, false, tact);
}

// Memory write: 3 T-states.
static Z80INLINE void Z80WdT(Z80Regs*&, Z80CPU* cpu, uint16_t addr, uint8_t val, int& tact)
{
    tact += 3;
    cpu->mem[addr] = val;
}

// Opcode fetch cycle: refresh tick, 4 T total (all accumulated; Step
// publishes before the handler dispatch). PC is advanced before the fetch
// (z80ex semantics), matching the callback build's Z80M1T shape.
// M1 at a PC the caller already holds (Step: read once, also stored as
// m1pc - a reload after that store would be needed otherwise, since the
// context may alias the register file as far as the compiler knows).
static Z80INLINE uint8_t Z80M1AtT(Z80Regs*& rf, Z80CPU* cpu, uint16_t addr, int& tact)
{
    Z80_R_INC(rf);  // refresh tick, bit 7 of r_low kept (see z80cpu-internal.h)
    rf->pc = static_cast<uint16_t>(addr + 1);
    const uint8_t opcode = Z80RdT(rf, cpu, addr, true, tact);
    cpu->opword = opcode;  // opcode + prefix byte cleared, one store
    tact += 1;

    return opcode;
}

static Z80INLINE uint8_t Z80M1T(Z80Regs*& rf, Z80CPU* cpu, int& tact)
{
    return Z80M1AtT(rf, cpu, rf->pc, tact);
}

// Port I/O: ports stay host callbacks even with flat memory attached, so
// the accumulator is published for the port callback to observe.
static Z80INLINE uint8_t Z80PinT(Z80Regs*& rf, Z80CPU* cpu, uint16_t port, int& tact)
{
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->in(port);
    rf = cpu->regs;  // reloaded, not kept across the call (see Z80ContendRT)
    return value;
}

static Z80INLINE void Z80PoutT(Z80Regs*& rf, Z80CPU* cpu, uint16_t port, uint8_t val, int& tact)
{
    cpu->t = static_cast<uint32_t>(tact);
    cpu->out(port, val);
    rf = cpu->regs;
}

// HALT quantum (z80step.inc): 4 T; the flat bus has no contention hook.
static Z80INLINE void Z80HaltT(Z80Regs*&, Z80CPU*, int& tact)
{
    tact += 4;
}

// Call-site shims: thread the caller's tact_ accumulator into the T-aware
// primitives while keeping the ported opcode bodies' call shapes unchanged
// (including the two Z80Rd arities: with/without the isExecution flag).
#define Z80Rd(...) Z80RdT(rf, __VA_ARGS__, tact_)
#define Z80Wd(cpu, addr, val) Z80WdT(rf, cpu, addr, val, tact_)
#define Z80M1(cpu) Z80M1T(rf, cpu, tact_)
#define Z80Pin(cpu, port) Z80PinT(rf, cpu, port, tact_)
#define Z80Pout(cpu, port, val) Z80PoutT(rf, cpu, port, val, tact_)

#include "z80cpu-opcodes.inc"
#include "opcodes-base.inc"
#include "opcodes-cb.inc"
#include "opcodes-dd.inc"
#include "opcodes-fd.inc"
#include "opcodes-ed.inc"
#include "opcodes-xxcb.inc"
#include "z80step.inc"

}  // namespace Z80Flat
