// z84cpu-internal.h - internal header of the Z84C15 core (fork of unreal-z80 0.5.0).
//
// The Z84CPU struct reproduces the register layout of the core's Z80Registers
// (core/src/emulator/cpu/z80.h) member-for-member so that the opcode units
// ported from op_*.cpp compile with minimal edits. Memory/IO access goes
// through the host bus callbacks (the only bus of this fork), and every bus
// cycle passes the on-chip wait-state generator (Z84WaitGen below).

#ifndef Z84CPU_INTERNAL_H
#define Z84CPU_INTERNAL_H

#include <cstdint>

#include "z84cpu.h"

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
#define Z84FAST __fastcall
#define Z84INLINE __forceinline
#define __builtin_expect(x, y) (x)
#else
#define Z84FAST
#define Z84INLINE inline
#endif
#define Z84OPCODE int Z84FAST
#define Z84LOGIC uint8_t Z84FAST

struct Z84Regs;
typedef int (Z84FAST* STEPFUNC)(struct Z84CPU*, int tact0, struct Z84Regs* rf);
typedef uint8_t (Z84FAST* LOGICFUNC)(struct Z84Regs*, uint8_t byte);

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
// contention model, see z84cpu.h) and before the RETI device hook. Every
// handler exit path - including early returns - carries its T back through
// the return value, so no exit-time publish machinery is needed; pure
// handlers with no bus cycles skip even the local accumulator (`return
// tact0` is their whole T budget). Bus callbacks must not modify cpu->t;
// timing is advanced by the core.
#define cputact(a) (tact_ += (a))

// The refresh address the CPU puts on the bus in internal cycles after M1: I high, R low (bit 7 from r_hi)
#define Z84_IR(rf) static_cast<uint16_t>(((rf)->i << 8) | ((rf)->r_low & 0x7F) | ((rf)->r_hi & 0x80))

struct Z84AltRegs
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
// 16-bit unions). It is the engine's view of the public Z84CpuRegisterFile
// (z84cpu.h) - same size and offsets, pinned by static_asserts in
// z84cpu.cpp - so a host can attach its own block and the engine works on it
// in place (Z84CpuAttachRegisterFile). The hot path (Step, handlers, bus
// primitives) reaches it through `rf`, loaded from cpu->regs once per
// instruction and passed as an argument, reloaded only after out-of-line
// calls (docs/hot-cold-path-guide.md, rules 2.2 and 2.5); the API and cold
// code use Z84R(cpu).
#pragma pack(push, 1)  // outside the body: packs the struct itself (host layout)
struct Z84Regs
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
            uint8_t halted;  // HALT latch (bit 0) | Z84_PENDING_PREFIX (bit 1)
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
    Z84AltRegs alt;

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
struct Z84CPU
{
    // Active register file: &ownedRegs, or the host block attached with
    // Z84CpuAttachRegisterFile. First member: one load from the context.
    Z84Regs* regs;


    // ---- execution state ----
    uint32_t t;         // T-states since reset (plain, unscaled)
    // Last dispatched opcode byte and the prefix class it ran under, as one
    // 16-bit word: the M1 fetch stores the opcode with a zero prefix byte in
    // a single store (no per-step prefix reset); the CB/ED/xxCB dispatchers
    // set the prefix byte after their own M1. Read by Z84CpuIntPossible (EI
    // shadow: 0xFB unprefixed or under DD/FD) and the boundary register.
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
    Z84CpuMemReadFn memRead;
    void* memReadData;
    Z84CpuMemWriteFn memWrite;
    void* memWriteData;
    Z84CpuPortInFn portIn;
    void* portInData;
    Z84CpuPortOutFn portOut;
    void* portOutData;
    Z84CpuIntVectorFn intVector;
    void* intVectorData;
    Z84CpuRetiFn retiCallback;
    void* retiData;
    Z84CpuRetnFn retnCallback;  // null = no RETN notification
    void* retnData;
    Z84CpuContendFn contend;  // wait-state hook (null = uncontended)
    void* contendData;

    uint16_t m1pc;  // address of the current instruction's first byte


    // Step entry: always &Z84Cb::Step (the callback bus is the only bus of
    // this fork; kept as a pointer so Z84CpuStepEntry keeps its contract).
    int (*stepFn)(Z84CPU*);

    // ---- on-chip block (research-cpu-z84c15.md section 4) ----
    // The wait-state generator: programmed by Z84Lib::Z84C15 (WCR / MWBR
    // writes, power-on), applied by the bus primitives. The hot path tests
    // wait.active only.
    struct Z84WaitGen
    {
        uint8_t wcrProgrammed;  // WCR as software wrote it (0 until written)
        uint8_t wcr;            // effective WCR: #FF in the power-on window
        uint8_t mwbr;           // memory wait boundary (power-on #F0: all of 64K)
        uint8_t powerOnM1Left;  // M1 cycles left in the power-on window
        uint8_t afterEd;        // the last M1 fetched ED: the next M1 gets the RETI rule
        bool active;            // wcr != 0: some programmed wait is possible
    } wait;

    // The chip's RETI watcher (the daisy chain), before the host's RETI callback
    void (*chipReti)(void* chip);
    void* chipData;

    // Callbacks are never null (Z84CpuCreate/Set* install stubs), so the
    // hot paths carry no null test.
    Z84INLINE uint8_t in(uint16_t port)
    {
        return portIn(this, port, portInData);
    }

    Z84INLINE void out(uint16_t port, uint8_t val)
    {
        portOut(this, port, val, portOutData);
    }

    // RETN leaves the NMI session (core Z80::retn).
    Z84INLINE void retn() { regs->nmi_in_progress = false; }

    // Default register storage (standalone hosts, tests, benchmarks).
    alignas(8) Z84Regs ownedRegs;  // default storage (also the detach target)
};

// Register access outside the hot path (API, INT/NMI, reset).
#define Z84R(cpu) (*(cpu)->regs)

// Refresh counter tick (every M1, the halted NOP, the INT/NMI acknowledge):
// the low 7 bits count, bit 7 of r_low is left as it is - a host sharing the
// register file (Z84CpuAttachRegisterFile) keeps its own meaning for that
// bit, and LD R,A is the only instruction that writes it.
#define Z84_R_INC(rf) ((rf)->r_low = static_cast<uint8_t>((((rf)->r_low + 1) & 0x7F) | ((rf)->r_low & 0x80)))
#define Z84_R_DEC(rf) ((rf)->r_low = static_cast<uint8_t>((((rf)->r_low - 1) & 0x7F) | ((rf)->r_low & 0x80)))

// ---- standardized T-model machinery (see the cputact block above) ----

// Wait-state hook at the first T of a bus cycle.
// cycleStart is the T the cycle begins at; the returned waits are added to
// the accumulator before the cycle's own T. The hot path is one predictable
// null test; the call itself lives out of line so that the extra opaque
// call site does not disturb register allocation in the bus primitives
// (measured: inline call cost 7-10% on the callback bus, cold call ~0).
// Z84COLD: out-of-line slow path (contention hook, the wait generator).
// Plain noinline/cold on every compiler - the same code shape for clang,
// GCC and MSVC. Clang's preserve_most convention was measured on these
// functions (docs/perf-analysis-2026-09-24.md, round 2): +7% on the paged
// microbenchmark but -3% frames/s on screen-heavy code, and clang-only,
// so it is not used.
#if defined(_MSC_VER)
#define Z84COLD __declspec(noinline)
#else
#define Z84COLD __attribute__((noinline, cold))
#endif
Z84COLD int Z84ContendSlow(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, int cycleStart);

Z84INLINE void Z84ContendT(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, int cycleStart, int& tact)
{
    if (__builtin_expect(cpu->contend != nullptr, 0))
        tact += Z84ContendSlow(cpu, addr, kind, cycleStart);
}

// Same, for the bus primitives that carry the handler's register-file
// pointer: reloaded from the context after the out-of-line hook, so it is
// dead across the call (no callee-saved register, no save/restore in the
// handler prologue - it lives in an argument register all instruction long).
Z84INLINE void Z84ContendRT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, int cycleStart, int& tact)
{
    if (__builtin_expect(cpu->contend != nullptr, 0))
    {
        tact += Z84ContendSlow(cpu, addr, kind, cycleStart);
        rf = cpu->regs;
    }
}

// ---- on-chip wait-state generator (z84waits.cpp) ----
// Programmed waits per bus cycle (PS0182 p. 318-320; research-cpu-z84c15.md
// section 4.1; design section 6). Each is out of line and taken only while
// wait.active: with WCR = 0, the BIOS's setting, a bus cycle pays one byte
// test. The waits follow the access (as the host's external /WAIT does), so
// a callback sees its cycle start at T - 3. The rf-carrying forms reload the
// register-file pointer after the call, as Z84ContendRT does.
namespace Z84Lib
{
Z84COLD int Z84WaitM1(Z84CPU* cpu, uint16_t addr, uint8_t opcode);  // opcode fetch, halted M1, NMI restart M1
Z84COLD int Z84WaitMemory(Z84CPU* cpu, uint16_t addr);  // operand, data read / write, INT push, IM2 vector read
Z84COLD int Z84WaitIo(Z84CPU* cpu, uint16_t port);      // after IORQ; none for the on-chip ports
int Z84WaitInta(const Z84CPU* cpu);                      // inside the INT acknowledge
bool Z84OnChipPort(uint16_t port);                       // the fixed ports, decoded by A7-A0 only
}  // namespace Z84Lib

Z84INLINE void Z84WaitMemRT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, int& tact)
{
    if (__builtin_expect(cpu->wait.active, 0))
    {
        tact += Z84Lib::Z84WaitMemory(cpu, addr);
        rf = cpu->regs;
    }
}

Z84INLINE void Z84WaitM1RT(Z84Regs*& rf, Z84CPU* cpu, uint16_t addr, uint8_t opcode, int& tact)
{
    if (__builtin_expect(cpu->wait.active, 0))
    {
        tact += Z84Lib::Z84WaitM1(cpu, addr, opcode);
        rf = cpu->regs;
    }
}

Z84INLINE void Z84WaitIoRT(Z84Regs*& rf, Z84CPU* cpu, uint16_t port, int& tact)
{
    if (__builtin_expect(cpu->wait.active, 0))
    {
        tact += Z84Lib::Z84WaitIo(cpu, port);
        rf = cpu->regs;
    }
}

// Seed the accumulator from the tact0 argument (T-touching handlers). No
// address-of, no RAII: the accumulator stays in a register for the whole
// handler and the final T travels back through the return value (Step
// publishes cpu->t from it). A scope-exit publisher would take &tact_ and
// spill the accumulator to the stack for its whole lifetime - measured as
// a 2-15% loss on port/ALU-adjacent workloads on Apple Silicon.
#define Z84_T_BEGIN(cpu)                                                                      \
    int tact_ = tact0;                                                                        \
    (void)tact_

// Pure handlers (no bus cycles, no cputact) never advance the accumulator:
// their exit T is tact0 itself - `return tact0;` is their whole T budget,
// with no local accumulator, no RAII and no cpu->t store (Step publishes
// the returned value once per instruction).



// Flush pending accumulator ticks into cpu->t (before host-visible hooks
// that are not bus primitives, i.e. the RETI notification).
#define Z84_T_PUBLISH(cpu) ((cpu)->t = static_cast<uint32_t>(tact_))

#if defined(_MSC_VER)
#define Z84NOINLINE __declspec(noinline)
#else
#define Z84NOINLINE __attribute__((noinline))
#endif

// Per-instruction epilogue, shared by every handler through Z84_RETURN_Q:
// publishes cpu->t, sets Q and returns the T after the instruction. Two
// leaf variants selected at compile time by the handler's own flag-writer
// class (every handler ends with Z84_RETURN_Q(0x28, t) if it writes F
// through the ALU flag path, Z84_RETURN_Q(0, t) otherwise - the classes
// are those of z80test's z80ccf: ALU/INC/DEC/rotates/DAA/CPL/SCF/CCF/ADD HL
// in the base map, CB 00-7F, ED IN r,(C)/ADC-SBC HL/NEG/LD A,I/LD A,R/RRD/
// RLD/block ops; not POP AF, EX AF,AF', RES/SET). No table lookup, no
// opcode/prefix read in the epilogue. Handlers end with a jump into it and
// it returns straight to the host - Step itself ends with a jump into the
// handler - so one instruction costs one call/return pair and no state has
// to survive any call. Plain `return f(...)` everywhere: every compiler
// emits the jump at -O2, and correctness never depends on it.
[[maybe_unused]] Z84NOINLINE static int Z84FinishFlags(Z84CPU* cpu, int tact, Z84Regs* rf)
{
    cpu->t = static_cast<uint32_t>(tact);
    rf->q = rf->f & 0x28;
    return tact;
}

[[maybe_unused]] Z84NOINLINE static int Z84FinishNoFlags(Z84CPU* cpu, int tact, Z84Regs* rf)
{
    cpu->t = static_cast<uint32_t>(tact);
    rf->q = 0;
    return tact;
}

// rf->halted bit 1: a redundant DD/FD prefix ended the step and the next
// prefix (in cpu->opword) is pending. It shares the byte with the HALT latch
// so Step's one slow-path test covers both (ddfd_prefixes, HaltedStep).
#define Z84_PENDING_PREFIX 2

// cpu->opword values that stand for a boundary state rather than a fetched
// opcode (see the table above Z84CpuIntPossible in z84cpu.cpp): the INT
// shadow is EI's own opword, reused by RETN/RETI when they set IFF1; the
// NMI-acknowledge marker uses prefix byte FF, which no M1 fetch produces.
#define Z84_OPWORD_INT_SHADOW 0x00FB
#define Z84_OPWORD_NMI_ACK 0xFF66

#define Z84_RETURN_Q(qmask, t) return ((qmask) ? Z84FinishFlags(cpu, (t), rf) : Z84FinishNoFlags(cpu, (t), rf))

// ---- library-private namespace ----
// Everything below with external linkage (the flag/DAA tables and their
// initializer) or with generic names (the ALU micro-ops) lives in Z84Lib,
// so the library links into a host that already defines the same C-style
// table names (the unreal-ng core has global log_f/inc_f/daatab/...). The
// using-directive keeps the ported opcode bodies unqualified; this header is
// private to the library sources.
namespace Z84Lib
{

// ---- flag / decode tables (z84tables.cpp / z84tables-data.inc) ----
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


void Z84TablesInit(void);

// ---- inlined micro-operations (ported from core cpulogic.cpp) ----

Z84INLINE void and8(Z84Regs* rf, uint8_t src)
{
    rf->a &= src;
    rf->f = log_f[rf->a] | HF;
}

Z84INLINE void or8(Z84Regs* rf, uint8_t src)
{
    rf->a |= src;
    rf->f = log_f[rf->a];
}

Z84INLINE void xor8(Z84Regs* rf, uint8_t src)
{
    rf->a ^= src;
    rf->f = log_f[rf->a];
}

// BIT n,(HL) and xxCB BIT n,(xx+d): X/Y flags come from MEMPTR high byte.
Z84INLINE void bitmem(Z84Regs* rf, uint8_t src, uint8_t bit)
{
    rf->f = log_f[src & (1 << bit)] | HF | (rf->f & CF);
    rf->f = (rf->f & ~(F3 | F5)) | (rf->memh & (F3 | F5));
}

Z84INLINE void op_set(uint8_t& src, uint8_t bit)
{
    src |= (1 << bit);
}

Z84INLINE void res(uint8_t& src, uint8_t bit)
{
    src &= ~(1 << bit);
}

// BIT n,r: X/Y flags come from the operand itself.
Z84INLINE void bit(Z84Regs* rf, uint8_t src, uint8_t bit)
{
    rf->f = log_f[src & (1 << bit)] | HF | (rf->f & CF) | (src & (F3 | F5));
}

Z84INLINE uint8_t resbyte(uint8_t src, uint8_t bit)
{
    return src & ~(1 << bit);
}

Z84INLINE uint8_t setbyte(uint8_t src, uint8_t bit)
{
    return src | (1 << bit);
}

Z84INLINE void inc8(Z84Regs* rf, uint8_t& x)
{
    rf->f = inc_f[x] | (rf->f & CF);
    x++;
}

Z84INLINE void dec8(Z84Regs* rf, uint8_t& x)
{
    rf->f = dec_f[x] | (rf->f & CF);
    x--;
}

Z84INLINE void add8(Z84Regs* rf, uint8_t src)
{
    rf->f = adc_f[rf->a + src * 0x100];
    rf->a += src;
}

Z84INLINE void sub8(Z84Regs* rf, uint8_t src)
{
    rf->f = sbc_f[rf->a * 0x100 + src];
    rf->a -= src;
}

Z84INLINE void adc8(Z84Regs* rf, uint8_t src)
{
    uint8_t carry = ((rf->f)&CF);
    rf->f = adc_f[rf->a + src * 0x100 + 0x10000 * carry];
    rf->a += src + carry;
}

Z84INLINE void sbc8(Z84Regs* rf, uint8_t src)
{
    uint8_t carry = ((rf->f)&CF);
    rf->f = sbc_f[rf->a * 0x100 + src + 0x10000 * carry];
    rf->a -= src + carry;
}

Z84INLINE void cp8(Z84Regs* rf, uint8_t src)
{
    rf->f = cp_f[rf->a * 0x100 + src];
}

}  // namespace Z84Lib

using namespace Z84Lib;

#endif  // Z84CPU_INTERNAL_H
