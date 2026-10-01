// z80cpu-internal.h - internal header of the standalone Z80 core (PoC 017).
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
#else
#define Z80FAST
#define Z80INLINE inline
#endif
#define Z80OPCODE void Z80FAST
#define Z80LOGIC uint8_t Z80FAST

typedef void (Z80FAST* STEPFUNC)(struct Z80CPU*);
typedef uint8_t (Z80FAST* LOGICFUNC)(struct Z80CPU*, uint8_t byte);

// T-state accounting: the core scales by cpu->rate (256 for 1x); the
// standalone core counts plain T-states, so cputact(n) adds exactly n.
#define cputact(a) (cpu->t += (a))

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

// The CPU state. Register block layout mirrors the packed core Z80Registers
// (little-endian byte order in the 16-bit unions, same as the core).
struct Z80CPU
{
    // ---- register file (packed, core-compatible layout) ----
#pragma pack(push, 1)
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

    int32_t eipos;     // T-state of the last EI (kept for state parity/dumps)
    uint16_t haltpos;  // T-state when HALT was entered

    uint8_t im;              // interrupt mode 0/1/2
    bool nmi_in_progress;    // RETN clears this (ported retn() method)
#pragma pack(pop)

    // ---- execution state ----
    uint32_t t;         // T-states since reset (plain, unscaled)
    uint16_t prefix;    // decoded prefix (0x0000/0xCB/0xED/0xDDCB/...)
    uint8_t opcode;     // last dispatched opcode byte
    uint16_t prev_pc;   // PC of the previous instruction
    uint16_t m1_pc;     // PC at the M1 fetch of the current instruction
    uint16_t last_branch;
    uint8_t outc0;      // value written by OUT (C),0
    uint8_t halt_cycle;

    // EI delay bookkeeping (core equivalent: the "t != eipos" check in
    // ProcessInterrupts): true while the post-EI shadow lasts, i.e. at the
    // instruction boundary directly after EI (real Z80: INT is recognized
    // only after the instruction following EI). Set from the opcode in Step.
    bool intSuppress;

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

    // ---- bus-cycle primitives (mirror Z80::rd/wd/in/out/m1_cycle) ----

    // Memory read: 3 T-states. isExecution marks an M1 opcode fetch for
    // hosts emulating memory contention.
    Z80INLINE uint8_t rd(uint16_t addr, bool isExecution = false)
    {
        t += 3;
        (void)isExecution;
        if (mem)
            return mem[addr];
        return memRead ? memRead(this, addr, isExecution ? 1 : 0, memReadData) : 0xFF;
    }

    // Memory write: 3 T-states.
    Z80INLINE void wd(uint16_t addr, uint8_t val)
    {
        t += 3;
        if (mem)
        {
            mem[addr] = val;
            return;
        }
        if (memWrite)
            memWrite(this, addr, val, memWriteData);
    }

    // Raw read/write without T accounting or bus hooks (interrupt vector and
    // stack pushes are pre-timed by the INT/NMI handlers, like the core's
    // MemIf reads inside HandleINT).
    Z80INLINE uint8_t RawRead(uint16_t addr) const
    {
        if (mem)
            return mem[addr];
        return memRead ? memRead(const_cast<Z80CPU*>(this), addr, 0, memReadData) : 0xFF;
    }

    Z80INLINE void RawWrite(uint16_t addr, uint8_t val)
    {
        if (mem)
        {
            mem[addr] = val;
            return;
        }
        if (memWrite)
            memWrite(this, addr, val, memWriteData);
    }

    Z80INLINE uint8_t in(uint16_t port)
    {
        return portIn ? portIn(this, port, portInData) : 0xFF;
    }

    Z80INLINE void out(uint16_t port, uint8_t val)
    {
        if (portOut)
            portOut(this, port, val, portOutData);
    }

    // Opcode fetch cycle: refresh tick, 4 T total (rd() adds 3, +1 here).
    Z80INLINE uint8_t m1_cycle()
    {
        if (prefix == 0x0000)
            m1_pc = pc;

        r_low = ((r_low + 1) & 0x7f) | (r_low & 0x80);
        opcode = rd(pc, true);
        pc++;
        t += 1;

        return opcode;
    }

    // RETN leaves the NMI session (core Z80::retn).
    Z80INLINE void retn() { nmi_in_progress = false; }
};

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

Z80INLINE void and8(Z80CPU* cpu, uint8_t src)
{
    cpu->a &= src;
    cpu->f = log_f[cpu->a] | HF;
}

Z80INLINE void or8(Z80CPU* cpu, uint8_t src)
{
    cpu->a |= src;
    cpu->f = log_f[cpu->a];
}

Z80INLINE void xor8(Z80CPU* cpu, uint8_t src)
{
    cpu->a ^= src;
    cpu->f = log_f[cpu->a];
}

// BIT n,(HL) and xxCB BIT n,(xx+d): X/Y flags come from MEMPTR high byte.
Z80INLINE void bitmem(Z80CPU* cpu, uint8_t src, uint8_t bit)
{
    cpu->f = log_f[src & (1 << bit)] | HF | (cpu->f & CF);
    cpu->f = (cpu->f & ~(F3 | F5)) | (cpu->memh & (F3 | F5));
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
Z80INLINE void bit(Z80CPU* cpu, uint8_t src, uint8_t bit)
{
    cpu->f = log_f[src & (1 << bit)] | HF | (cpu->f & CF) | (src & (F3 | F5));
}

Z80INLINE uint8_t resbyte(uint8_t src, uint8_t bit)
{
    return src & ~(1 << bit);
}

Z80INLINE uint8_t setbyte(uint8_t src, uint8_t bit)
{
    return src | (1 << bit);
}

Z80INLINE void inc8(Z80CPU* cpu, uint8_t& x)
{
    cpu->f = inc_f[x] | (cpu->f & CF);
    x++;
}

Z80INLINE void dec8(Z80CPU* cpu, uint8_t& x)
{
    cpu->f = dec_f[x] | (cpu->f & CF);
    x--;
}

Z80INLINE void add8(Z80CPU* cpu, uint8_t src)
{
    cpu->f = adc_f[cpu->a + src * 0x100];
    cpu->a += src;
}

Z80INLINE void sub8(Z80CPU* cpu, uint8_t src)
{
    cpu->f = sbc_f[cpu->a * 0x100 + src];
    cpu->a -= src;
}

Z80INLINE void adc8(Z80CPU* cpu, uint8_t src)
{
    uint8_t carry = ((cpu->f)&CF);
    cpu->f = adc_f[cpu->a + src * 0x100 + 0x10000 * carry];
    cpu->a += src + carry;
}

Z80INLINE void sbc8(Z80CPU* cpu, uint8_t src)
{
    uint8_t carry = ((cpu->f)&CF);
    cpu->f = sbc_f[cpu->a * 0x100 + src + 0x10000 * carry];
    cpu->a -= src + carry;
}

Z80INLINE void cp8(Z80CPU* cpu, uint8_t src)
{
    cpu->f = cp_f[cpu->a * 0x100 + src];
}

#endif  // Z80CPU_INTERNAL_H
