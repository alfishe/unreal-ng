// z80cpu-internal.h - internal header of the standalone Z80 core (unreal-z80).
//
// The Z80CPU struct reproduces the register layout of the core's Z80Registers
// (core/src/emulator/cpu/z80.h) member-for-member so that the opcode units
// ported from op_*.cpp compile with minimal edits. Memory/IO access is routed
// through inline rd/wd/in/out methods: a flat 64K pointer when attached
// (fast path), the bus callbacks otherwise - mirroring the core's
// MemoryInterface dispatch without the emulator dependencies.

#ifndef Z80CPU_INTERNAL_H
#define Z80CPU_INTERNAL_H

#include <cstdint>

#include "z80cpu.h"

// Flag register bits (same values as core/src/emulator/cpu/cpulogic.h)
#define CF 0x01  // Carry
#define NF 0x02  // Add/Subtract
#define PV 0x04  // Parity/Overflow
#define F3 0x08  // Undocumented X flag
#define HF 0x10  // Half carry
#define F5 0x20  // Undocumented Y flag
#define ZF 0x40  // Zero
#define SF 0x80  // Sign

// Calling-convention / inlining macros (ported from cpulogic.h)
#if defined(_MSC_VER)
#define Z80FAST __fastcall
#define Z80INLINE __forceinline
#define __builtin_expect(x, y) (x)
#else
#define Z80FAST
#define Z80INLINE inline
#endif
#define Z80OPCODE int Z80FAST
#define Z80LOGIC uint8_t Z80FAST

struct Z80Regs;
typedef int (Z80FAST* STEPFUNC)(struct Z80CPU*, int tact0, struct Z80Regs* rf);
typedef uint8_t (Z80FAST* LOGICFUNC)(struct Z80Regs*, uint8_t byte);

// T-state accounting: the core scales by cpu->rate (256 for 1x); the
// standalone core counts plain T-states.
//
// Standardized T model: each instruction owns one T accumulator that lives
// in registers for its whole duration. Step seeds it from cpu->t and passes
// it to the handler as the tact0 argument; the handler advances it (cputact
// is a plain register add) and RETURNS the final T through a register -
// there is no Step<->handler handoff through memory at all. Step publishes
// cpu->t once per instruction from the returned value; the only other
// cpu->t writes are the observable points inside handlers: before every
// bus/port callback (hosts inspect the current T there - the documented
// contention model, see z80cpu.h) and before the RETI device hook. Every
// handler exit path - including early returns - carries its T back through
// the return value, so no exit-time publish machinery is needed; pure
// handlers with no bus cycles skip even the local accumulator (`return
// tact0` is their whole T budget). Bus callbacks must not modify cpu->t;
// timing is advanced by the core.
#define cputact(a) (tact_ += (a))

struct Z80AltRegs
{
    union
    {
        uint16_t bc;
        struct
        {
            uint8_t c;
            uint8_t b;
        };
    };
    union
    {
        uint16_t de;
        struct
        {
            uint8_t e;
            uint8_t d;
        };
    };
    union
    {
        uint16_t hl;
        struct
        {
            uint8_t l;
            uint8_t h;
        };
    };
    union
    {
        uint16_t af;
        struct
        {
            uint8_t f;
            uint8_t a;
        };
    };
};

// The register file: packed, member-for-member the layout of the core's
// Z80Registers from pc to nmi_in_progress (little-endian byte order in the
// 16-bit unions). It is the engine's view of the public Z80CpuRegisterFile
// (z80cpu.h) - same size and offsets, pinned by static_asserts in
// z80cpu.cpp - so a host can attach its own block and the engine works on it
// in place (Z80CpuAttachRegisterFile). The hot path (Step, handlers, bus
// primitives) reaches it through `rf`, loaded from cpu->regs once per
// instruction and passed as an argument, reloaded only after out-of-line
// calls (docs/hot-cold-path-guide.md, rules 2.2 and 2.5); the API and cold
// code use Z80R(cpu).
#pragma pack(push, 1)  // outside the body: packs the struct itself (host layout)
struct Z80Regs
{
    union
    {
        uint16_t pc;
        struct
        {
            uint8_t pcl;
            uint8_t pch;
        };
    };
    union
    {
        uint16_t sp;
        struct
        {
            uint8_t spl;
            uint8_t sph;
        };
    };
    union  // IR: r_low keeps the 7-bit refresh counter, i the interrupt vector base
    {
        uint16_t ir_;
        struct
        {
            uint8_t r_low;
            uint8_t i;
        };
    };
    union
    {
        uint32_t int_flags;
        struct
        {
            uint8_t r_hi;    // R bit 7 (survives LD R,A as the refresh high bit)
            uint8_t iff1;    // interrupt flip-flop 1
            uint8_t iff2;    // interrupt flip-flop 2
            uint8_t halted;  // HALT latch
        };
    };
    union
    {
        uint16_t bc;
        struct
        {
            uint8_t c;
            uint8_t b;
        };
    };
    union
    {
        uint16_t de;
        struct
        {
            uint8_t e;
            uint8_t d;
        };
    };
    union
    {
        uint16_t hl;
        struct
        {
            uint8_t l;
            uint8_t h;
        };
    };
    union
    {
        uint16_t af;
        struct
        {
            uint8_t f;
            uint8_t a;
        };
    };
    union
    {
        uint16_t ix;
        struct
        {
            uint8_t xl;
            uint8_t xh;
        };
    };
    union
    {
        uint16_t iy;
        struct
        {
            uint8_t yl;
            uint8_t yh;
        };
    };
    Z80AltRegs alt;

    // Undocumented internals (see core z80.h for full references):
    // MEMPTR/WZ - set by most address-forming instructions, observable
    // through the X/Y flags of BIT n,(HL); Q - YF/XF capture feeding the
    // Zilog SCF/CCF undocumented flag formula.
    union
    {
        uint16_t memptr;
        struct
        {
            uint8_t meml;
            uint8_t memh;
        };
    };
    uint8_t q;

    // Host-owned slots, kept only so the block is contiguous with the core's
    // Z80Registers (eipos/haltpos sit between q and im there). The engine
    // never reads or writes them: with an attached register file they are
    // the host's own EI-shadow / HALT-entry bookkeeping.
    int32_t reservedEipos;
    uint16_t reservedHaltpos;

    uint8_t im;              // interrupt mode 0/1/2
    bool nmi_in_progress;    // RETN clears this (ported retn() method)
};
#pragma pack(pop)

// The CPU state. Register block layout mirrors the packed core Z80Registers
// (little-endian byte order in the 16-bit unions, same as the core).
struct Z80CPU
{
    // Active register file: &ownedRegs, or the host block attached with
    // Z80CpuAttachRegisterFile. First member: one load from the context.
    Z80Regs* regs;


    // ---- execution state ----
    uint32_t t;         // T-states since reset (plain, unscaled)
    // Last dispatched opcode byte and the prefix class it ran under, as one
    // 16-bit word: the M1 fetch stores the opcode with a zero prefix byte in
    // a single store (no per-step prefix reset); the CB/ED/xxCB dispatchers
    // set the prefix byte after their own M1. Read by Z80CpuIntPossible (EI
    // shadow: 0xFB unprefixed or under DD/FD) and the LD A,I/R quirk.
    union
    {
        uint16_t opword;
        struct
        {
            uint8_t opcode;
            uint8_t prefix;  // 0x00 (none or DD/FD), 0xCB (incl. DDCB/FDCB), 0xED
        };
    };
    uint8_t outc0;      // value written by OUT (C),0
    uint8_t halt_cycle;

    // DDCB destination registers ([6] = trash sink for the no-destination case)
    uint8_t trashRegister;
    uint8_t* directRegisters[8];

    // ---- bus ----
    uint8_t* mem;  // flat 64K fast path (null -> callbacks)
    Z80CpuMemReadFn memRead;
    void* memReadData;
    Z80CpuMemWriteFn memWrite;
    void* memWriteData;
    Z80CpuPortInFn portIn;
    void* portInData;
    Z80CpuPortOutFn portOut;
    void* portOutData;
    Z80CpuIntVectorFn intVector;
    void* intVectorData;
    Z80CpuRetiFn retiCallback;
    void* retiData;
    Z80CpuRetnFn retnCallback;  // null = no RETN notification
    void* retnData;
    Z80CpuContendFn contend;  // wait-state hook (null = uncontended)
    void* contendData;

    // Paged bus: host-owned page tables (null = not attached)
    uint8_t* const* pageRead;
    uint8_t* const* pageWrite;
    // Rebased copies (entry i = host pointer - i * page size, null stays
    // null): the hot path indexes page[addr] with no offset mask. Refreshed
    // by Z80CpuAttachPageTables / Z80CpuSyncPageTables.
    uint8_t* pageReadRebased[Z80CPU_PAGE_COUNT];
    uint8_t* pageWriteRebased[Z80CPU_PAGE_COUNT];

    uint16_t m1pc;  // address of the current instruction's first byte


    // Active step entry: &Z80Flat::Step while a flat memory block is
    // attached, &Z80Paged::Step with page tables, &Z80Cb::Step on the
    // callback bus (see z80cpu-dispatch.h).
    // Z80CpuStep tail-jumps through it - a single stable indirect branch
    // per instruction (measured faster than a load-and-test dispatch on
    // this path), and the opcode units themselves contain zero bus-mode
    // branches (they are compiled per mode).
    int (*stepFn)(Z80CPU*);

    // ---- bus-cycle primitives ----
    // The hot opcode path uses the per-mode Z80Rd/Z80Wd/Z80M1 free functions
    // (opcodes-flat.cpp / opcodes-callback.cpp). The members below serve the
    // cold paths: interrupt acknowledge, NMI, host register access.

    // Raw read/write without T accounting or bus hooks (interrupt vector and
    // stack pushes are pre-timed by the INT/NMI handlers, like the core's
    // MemIf reads inside HandleINT).
    Z80INLINE uint8_t RawRead(uint16_t addr) const
    {
        if (mem)
            return mem[addr];
        if (pageRead)
        {
            const uint8_t* page = pageRead[addr >> Z80CPU_PAGE_SHIFT];
            if (page)
                return page[addr & (Z80CPU_PAGE_SIZE - 1)];
        }
        return memRead(const_cast<Z80CPU*>(this), addr, 0, memReadData);
    }

    Z80INLINE void RawWrite(uint16_t addr, uint8_t val)
    {
        if (mem)
        {
            mem[addr] = val;
            return;
        }
        if (pageWrite)
        {
            uint8_t* page = pageWrite[addr >> Z80CPU_PAGE_SHIFT];
            if (page)
            {
                page[addr & (Z80CPU_PAGE_SIZE - 1)] = val;
                return;
            }
        }
        memWrite(this, addr, val, memWriteData);
    }

    // Callbacks are never null (Z80CpuCreate/Set* install stubs), so the
    // hot paths carry no null test.
    Z80INLINE uint8_t in(uint16_t port)
    {
        return portIn(this, port, portInData);
    }

    Z80INLINE void out(uint16_t port, uint8_t val)
    {
        portOut(this, port, val, portOutData);
    }

    // RETN leaves the NMI session (core Z80::retn).
    Z80INLINE void retn() { regs->nmi_in_progress = false; }

    // Default register storage (standalone hosts, tests, benchmarks).
    alignas(8) Z80Regs ownedRegs;  // default storage (also the detach target)
};

// Register access outside the hot path (API, INT/NMI, reset).
#define Z80R(cpu) (*(cpu)->regs)

// Refresh counter tick (every M1, the halted NOP, the INT/NMI acknowledge):
// the low 7 bits count, bit 7 of r_low is left as it is - a host sharing the
// register file (Z80CpuAttachRegisterFile) keeps its own meaning for that
// bit, and LD R,A is the only instruction that writes it.
#define Z80_R_INC(rf) ((rf)->r_low = static_cast<uint8_t>((((rf)->r_low + 1) & 0x7F) | ((rf)->r_low & 0x80)))
#define Z80_R_DEC(rf) ((rf)->r_low = static_cast<uint8_t>((((rf)->r_low - 1) & 0x7F) | ((rf)->r_low & 0x80)))

// ---- standardized T-model machinery (see the cputact block above) ----

// Wait-state hook at the first T of a bus cycle (callback/paged buses).
// cycleStart is the T the cycle begins at; the returned waits are added to
// the accumulator before the cycle's own T. The hot path is one predictable
// null test; the call itself lives out of line so that the extra opaque
// call site does not disturb register allocation in the bus primitives
// (measured: inline call cost 7-10% on the callback bus, cold call ~0).
// Z80COLD: out-of-line slow path (contention hook, host-observed page).
// Plain noinline/cold on every compiler - the same code shape for clang,
// GCC and MSVC. Clang's preserve_most convention was measured on these
// functions (docs/perf-analysis-2026-09-24.md, round 2): +7% on the paged
// microbenchmark but -3% frames/s on screen-heavy code, and clang-only,
// so it is not used.
#if defined(_MSC_VER)
#define Z80COLD __declspec(noinline)
#else
#define Z80COLD __attribute__((noinline, cold))
#endif
Z80COLD int Z80ContendSlow(Z80CPU* cpu, uint16_t addr, Z80CpuAccessKind kind, int cycleStart);

Z80INLINE void Z80ContendT(Z80CPU* cpu, uint16_t addr, Z80CpuAccessKind kind, int cycleStart, int& tact)
{
    if (__builtin_expect(cpu->contend != nullptr, 0))
        tact += Z80ContendSlow(cpu, addr, kind, cycleStart);
}

// Same, for the bus primitives that carry the handler's register-file
// pointer: reloaded from the context after the out-of-line hook, so it is
// dead across the call (no callee-saved register, no save/restore in the
// handler prologue - it lives in an argument register all instruction long).
Z80INLINE void Z80ContendRT(Z80Regs*& rf, Z80CPU* cpu, uint16_t addr, Z80CpuAccessKind kind, int cycleStart, int& tact)
{
    if (__builtin_expect(cpu->contend != nullptr, 0))
    {
        tact += Z80ContendSlow(cpu, addr, kind, cycleStart);
        rf = cpu->regs;
    }
}

// Seed the accumulator from the tact0 argument (T-touching handlers). No
// address-of, no RAII: the accumulator stays in a register for the whole
// handler and the final T travels back through the return value (Step
// publishes cpu->t from it). A scope-exit publisher would take &tact_ and
// spill the accumulator to the stack for its whole lifetime - measured as
// a 2-15% loss on port/ALU-adjacent workloads on Apple Silicon.
#define Z80_T_BEGIN(cpu)                                                                      \
    int tact_ = tact0;                                                                        \
    (void)tact_

// Pure handlers (no bus cycles, no cputact) never advance the accumulator:
// their exit T is tact0 itself - `return tact0;` is their whole T budget,
// with no local accumulator, no RAII and no cpu->t store (Step publishes
// the returned value once per instruction).



// Flush pending accumulator ticks into cpu->t (before host-visible hooks
// that are not bus primitives, i.e. the RETI notification).
#define Z80_T_PUBLISH(cpu) ((cpu)->t = static_cast<uint32_t>(tact_))

#if defined(_MSC_VER)
#define Z80NOINLINE __declspec(noinline)
#else
#define Z80NOINLINE __attribute__((noinline))
#endif

// Per-instruction epilogue, shared by every handler through Z80_RETURN_Q:
// publishes cpu->t, sets Q and returns the T after the instruction. Two
// leaf variants selected at compile time by the handler's own flag-writer
// class (every handler ends with Z80_RETURN_Q(0x28, t) if it writes F
// through the ALU flag path, Z80_RETURN_Q(0, t) otherwise - the classes
// are those of z80test's z80ccf: ALU/INC/DEC/rotates/DAA/CPL/SCF/CCF/ADD HL
// in the base map, CB 00-7F, ED IN r,(C)/ADC-SBC HL/NEG/LD A,I/LD A,R/RRD/
// RLD/block ops; not POP AF, EX AF,AF', RES/SET). No table lookup, no
// opcode/prefix read in the epilogue. Handlers end with a jump into it and
// it returns straight to the host - Step itself ends with a jump into the
// handler - so one instruction costs one call/return pair and no state has
// to survive any call. Plain `return f(...)` everywhere: every compiler
// emits the jump at -O2, and correctness never depends on it.
[[maybe_unused]] Z80NOINLINE static int Z80FinishFlags(Z80CPU* cpu, int tact, Z80Regs* rf)
{
    cpu->t = static_cast<uint32_t>(tact);
    rf->q = rf->f & 0x28;
    return tact;
}

[[maybe_unused]] Z80NOINLINE static int Z80FinishNoFlags(Z80CPU* cpu, int tact, Z80Regs* rf)
{
    cpu->t = static_cast<uint32_t>(tact);
    rf->q = 0;
    return tact;
}

#define Z80_RETURN_Q(qmask, t) return ((qmask) ? Z80FinishFlags(cpu, (t), rf) : Z80FinishNoFlags(cpu, (t), rf))

// ---- library-private namespace ----
// Everything below with external linkage (the flag/DAA tables and their
// initializer) or with generic names (the ALU micro-ops) lives in Z80Lib,
// so the library links into a host that already defines the same C-style
// table names (the unreal-ng core has global log_f/inc_f/daatab/...). The
// using-directive keeps the ported opcode bodies unqualified; this header is
// private to the library sources.
namespace Z80Lib
{

// ---- flag / decode tables (z80tables.cpp / z80tables-data.inc) ----
// Const tables are extracted verbatim from core cputables.h; the large ADC/
// SBC/CP tables are built at first use. Names follow the core aliases used
// by the ported opcode units.
extern const uint8_t inc_f[0x100];
extern const uint8_t dec_f[0x100];
extern const uint8_t rlc_f[0x100];
extern const uint8_t rrc_f[0x100];
extern const uint8_t sra_f[0x100];
extern const uint8_t rl0[0x100];
extern const uint8_t rl1[0x100];
extern const uint8_t rr0[0x100];
extern const uint8_t rr1[0x100];

extern uint8_t log_f[0x100];
extern uint8_t adc_f[0x20000];
extern uint8_t sbc_f[0x20000];
extern uint8_t cp_f[0x10000];
extern uint8_t cpf8b[0x10000];
extern uint8_t rol[0x100];
extern uint8_t ror[0x100];
extern uint8_t rlca_f[0x100];
extern uint8_t rrca_f[0x100];

// DAA lookup, returns the full AF pair (core daa_tabs.cpp)
extern const uint8_t daatab[0x1000];


void Z80TablesInit(void);

// ---- inlined micro-operations (ported from core cpulogic.cpp) ----

Z80INLINE void and8(Z80Regs* rf, uint8_t src)
{
    rf->a &= src;
    rf->f = log_f[rf->a] | HF;
}

Z80INLINE void or8(Z80Regs* rf, uint8_t src)
{
    rf->a |= src;
    rf->f = log_f[rf->a];
}

Z80INLINE void xor8(Z80Regs* rf, uint8_t src)
{
    rf->a ^= src;
    rf->f = log_f[rf->a];
}

// BIT n,(HL) and xxCB BIT n,(xx+d): X/Y flags come from MEMPTR high byte.
Z80INLINE void bitmem(Z80Regs* rf, uint8_t src, uint8_t bit)
{
    rf->f = log_f[src & (1 << bit)] | HF | (rf->f & CF);
    rf->f = (rf->f & ~(F3 | F5)) | (rf->memh & (F3 | F5));
}

Z80INLINE void op_set(uint8_t& src, uint8_t bit)
{
    src |= (1 << bit);
}

Z80INLINE void res(uint8_t& src, uint8_t bit)
{
    src &= ~(1 << bit);
}

// BIT n,r: X/Y flags come from the operand itself.
Z80INLINE void bit(Z80Regs* rf, uint8_t src, uint8_t bit)
{
    rf->f = log_f[src & (1 << bit)] | HF | (rf->f & CF) | (src & (F3 | F5));
}

Z80INLINE uint8_t resbyte(uint8_t src, uint8_t bit)
{
    return src & ~(1 << bit);
}

Z80INLINE uint8_t setbyte(uint8_t src, uint8_t bit)
{
    return src | (1 << bit);
}

Z80INLINE void inc8(Z80Regs* rf, uint8_t& x)
{
    rf->f = inc_f[x] | (rf->f & CF);
    x++;
}

Z80INLINE void dec8(Z80Regs* rf, uint8_t& x)
{
    rf->f = dec_f[x] | (rf->f & CF);
    x--;
}

Z80INLINE void add8(Z80Regs* rf, uint8_t src)
{
    rf->f = adc_f[rf->a + src * 0x100];
    rf->a += src;
}

Z80INLINE void sub8(Z80Regs* rf, uint8_t src)
{
    rf->f = sbc_f[rf->a * 0x100 + src];
    rf->a -= src;
}

Z80INLINE void adc8(Z80Regs* rf, uint8_t src)
{
    uint8_t carry = ((rf->f)&CF);
    rf->f = adc_f[rf->a + src * 0x100 + 0x10000 * carry];
    rf->a += src + carry;
}

Z80INLINE void sbc8(Z80Regs* rf, uint8_t src)
{
    uint8_t carry = ((rf->f)&CF);
    rf->f = sbc_f[rf->a * 0x100 + src + 0x10000 * carry];
    rf->a -= src + carry;
}

Z80INLINE void cp8(Z80Regs* rf, uint8_t src)
{
    rf->f = cp_f[rf->a * 0x100 + src];
}

}  // namespace Z80Lib

using namespace Z80Lib;

#endif  // Z80CPU_INTERNAL_H
