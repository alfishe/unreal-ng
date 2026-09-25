// opcodes-paged.cpp - paged-memory specialization of the opcode units.
//
// Compiled exclusively for the Z80CpuAttachPageTables integration: every
// memory access indexes the rebased copy of the host's page tables
// (Z80CPU_PAGE_COUNT pointers per side, entry i pre-offset by -i*page size
// so page[addr] needs no mask). A direct page is an inline array access
// (contention hook only); a null page entry routes the access through the
// host memory callbacks with T published exactly as on the callback bus. Ports are always
// host callbacks. The opcode bodies live in the opcodes-*.inc units, shared
// with the flat and callback builds; only the bus primitives below differ.

#include "z80cpu-dispatch.h"

namespace Z80Paged
{

// Slow paths live out of line so the hot path of every memory handler is a
// leaf function (no callee-saved spills, no frame record - the shape the
// flat build gets for free):
//  - contention hook installed: cold function, advanced T handed back packed
//    in the return value (waits before the cycle, +3 T, T published before a
//    host callback - identical to the inline form);
//  - null page (host-observed window): a cold function whose host callback
//    is a tail call, so the screen path costs one extra direct branch and no
//    extra frame. T is published before the callback as on the callback bus.
// Cold-path return convention (portable leaf trick): the cold function hands
// back the CPU pointer together with the advanced T and the value in a
// 16-byte struct, i.e. in x0/x1 (or rax/rdx). The inline caller reassigns its
// own `cpu` and `tact` from the return, so nothing of the caller's state has
// to survive the call in a callee-saved register - the handler keeps no
// spills and no prologue beyond the frame record, on every compiler.
struct Z80ColdRet
{
    Z80CPU* cpu;
    uint64_t packed;  // (T << 32) | value
};

Z80COLD static Z80ColdRet Z80RdContended(Z80CPU* cpu, uint16_t addr, bool isExecution, Z80CpuAccessKind kind, int tact)
{
    Z80ContendT(cpu, addr, kind, tact, tact);
    tact += 3;
    const uint8_t* page = cpu->pageReadRebased[addr >> Z80CPU_PAGE_SHIFT];
    uint8_t value;
    if (page)
        value = page[addr];
    else
    {
        cpu->t = static_cast<uint32_t>(tact);
        value = cpu->memRead(cpu, addr, isExecution ? 1 : 0, cpu->memReadData);
    }
    return {cpu, (static_cast<uint64_t>(static_cast<uint32_t>(tact)) << 32) | value};
}

Z80COLD static Z80ColdRet Z80WdContended(Z80CPU* cpu, uint16_t addr, uint8_t val, int tact)
{
    Z80ContendT(cpu, addr, Z80CpuAccessWrite, tact, tact);
    tact += 3;
    uint8_t* page = cpu->pageWriteRebased[addr >> Z80CPU_PAGE_SHIFT];
    if (page)
        page[addr] = val;
    else
    {
        cpu->t = static_cast<uint32_t>(tact);
        cpu->memWrite(cpu, addr, val, cpu->memWriteData);
    }
    return {cpu, static_cast<uint64_t>(static_cast<uint32_t>(tact)) << 32};
}

Z80COLD static Z80ColdRet Z80RdNullPage(Z80CPU* cpu, uint16_t addr, bool isExecution, int tactAfter)
{
    cpu->t = static_cast<uint32_t>(tactAfter);
    const uint8_t value = cpu->memRead(cpu, addr, isExecution ? 1 : 0, cpu->memReadData);
    return {cpu, (static_cast<uint64_t>(static_cast<uint32_t>(tactAfter)) << 32) | value};
}

Z80COLD static Z80ColdRet Z80WdNullPage(Z80CPU* cpu, uint16_t addr, uint8_t val, int tactAfter)
{
    cpu->t = static_cast<uint32_t>(tactAfter);
    cpu->memWrite(cpu, addr, val, cpu->memWriteData);
    return {cpu, static_cast<uint64_t>(static_cast<uint32_t>(tactAfter)) << 32};
}

static Z80INLINE uint8_t Z80RdKindT(Z80Regs*& rf, Z80CPU*& cpu, uint16_t addr, bool isExecution, Z80CpuAccessKind kind, int& tact)
{
    Z80ColdRet r;
    if (__builtin_expect(cpu->contend == nullptr, 1))
    {
        const uint8_t* page = cpu->pageReadRebased[addr >> Z80CPU_PAGE_SHIFT];
        tact += 3;
        if (__builtin_expect(page != nullptr, 1))
            return page[addr];
        r = Z80RdNullPage(cpu, addr, isExecution, tact);
    }
    else
        r = Z80RdContended(cpu, addr, isExecution, kind, tact);
    cpu = r.cpu;
    rf = cpu->regs;  // cold path only: rf is dead across the call (see Z80ContendRT)
    tact = static_cast<int>(static_cast<uint32_t>(r.packed >> 32));
    return static_cast<uint8_t>(r.packed);
}

// isExecution marks instruction-byte fetches at PC (operand/displacement/
// address bytes; hosts use it for execution tracking) - a 3 T cycle, so it
// contends as Z80CpuAccessOperand; the M1 cycle itself goes through Z80M1T.
static Z80INLINE uint8_t Z80RdT(Z80Regs*& rf, Z80CPU*& cpu, uint16_t addr, bool isExecution, int& tact)
{
    return Z80RdKindT(rf, cpu, addr, isExecution, isExecution ? Z80CpuAccessOperand : Z80CpuAccessRead, tact);
}

static Z80INLINE uint8_t Z80RdT(Z80Regs*& rf, Z80CPU*& cpu, uint16_t addr, int& tact)
{
    return Z80RdKindT(rf, cpu, addr, false, Z80CpuAccessRead, tact);
}

static Z80INLINE void Z80WdT(Z80Regs*& rf, Z80CPU*& cpu, uint16_t addr, uint8_t val, int& tact)
{
    Z80ColdRet r;
    if (__builtin_expect(cpu->contend == nullptr, 1))
    {
        uint8_t* page = cpu->pageWriteRebased[addr >> Z80CPU_PAGE_SHIFT];
        tact += 3;
        if (__builtin_expect(page != nullptr, 1))
        {
            page[addr] = val;
            return;
        }
        r = Z80WdNullPage(cpu, addr, val, tact);
    }
    else
        r = Z80WdContended(cpu, addr, val, tact);
    cpu = r.cpu;
    rf = cpu->regs;
    tact = static_cast<int>(static_cast<uint32_t>(r.packed >> 32));
}

// Opcode fetch cycle: refresh tick, 4 T total. PC advances before the fetch
// (z80ex semantics), matching the other builds' Z80M1T shape.
// M1 at a PC the caller already holds (Step: read once, also stored as
// m1pc - a reload after that store would be needed otherwise, since the
// context may alias the register file as far as the compiler knows).
static Z80INLINE uint8_t Z80M1AtT(Z80Regs*& rf, Z80CPU*& cpu, uint16_t addr, int& tact)
{
    Z80_R_INC(rf);  // refresh tick, bit 7 of r_low kept (see z80cpu-internal.h)
    rf->pc = static_cast<uint16_t>(addr + 1);
    const uint8_t opcode = Z80RdKindT(rf, cpu, addr, true, Z80CpuAccessM1, tact);
    cpu->opword = opcode;  // opcode + prefix byte cleared, one store
    tact += 1;

    return opcode;
}

static Z80INLINE uint8_t Z80M1T(Z80Regs*& rf, Z80CPU*& cpu, int& tact)
{
    return Z80M1AtT(rf, cpu, rf->pc, tact);
}

// Port I/O: identical to the callback bus (see opcodes-callback.cpp).
static Z80INLINE uint8_t Z80PinT(Z80Regs*& rf, Z80CPU* cpu, uint16_t port, int& tact)
{
    Z80ContendRT(rf, cpu, port, Z80CpuAccessPortIn, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->in(port);
    rf = cpu->regs;
    Z80ContendRT(rf, cpu, port, Z80CpuAccessPortInPost, tact, tact);
    return value;
}

static Z80INLINE void Z80PoutT(Z80Regs*& rf, Z80CPU* cpu, uint16_t port, uint8_t val, int& tact)
{
    Z80ContendRT(rf, cpu, port, Z80CpuAccessPortOut, tact - 1, tact);
    cpu->t = static_cast<uint32_t>(tact);
    cpu->out(port, val);
    rf = cpu->regs;
    Z80ContendRT(rf, cpu, port, Z80CpuAccessPortOutPost, tact, tact);
}

// HALT quantum (z80step.inc): one 4 T M1 cycle at PC, reported to the
// contention hook like any M1 (waits extend the quantum).
static Z80INLINE void Z80HaltT(Z80Regs*& rf, Z80CPU* cpu, int& tact)
{
    Z80ContendRT(rf, cpu, rf->pc, Z80CpuAccessM1, tact, tact);
    tact += 4;
}

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
#define Z80_HALT_OUT_OF_LINE 1  // see z80step.inc: keeps the paged Step entry prologue-free
#include "z80step.inc"

}  // namespace Z80Paged
