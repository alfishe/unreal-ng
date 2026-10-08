// z80ncpu.h - public C API of the Z80N CPU core (the ZX Spectrum Next's CPU), library unreal-next-z80.
//
// A Z80 core with the Z80N extended instructions (all under the ED prefix), for the machine that owns memory,
// ports and the NextREG file. Forked from the z84c15 core (itself a fork of unreal-z80 0.5.0, commit a0433ec),
// without the chip's on-chip block; every change is listed in README.md. The API has the same shape as the z84c15
// and unreal-z80 libraries under its own prefix (Z80nCpu), so a host adapter written for one is written for all:
//  - opaque CPU context, host-supplied bus callbacks, one instruction per call, host-driven INT/NMI, register
//    accessors, zero-copy register file
//  - full undocumented behavior: MEMPTR (WZ), Q (Zilog SCF/CCF), IXH/IXL/IYH/IYL, SLL, IN (C), X/Y flags (F3/F5)
//  - NMOS baseline: OUT (C),0 writes 0 (Z80nCpuSetOutC0Value), LD A,I / LD A,R followed by an accepted INT clears
//    P/V (Z80nCpuSetLdAirQuirk); which quirks the Next's T80-based core shows is an open question, found with
//    the Next's test programs
//  - only the callback bus: every memory and port cycle reaches the host, which owns paging, contention and the
//    28 MHz wait state
//  - the memory read callback gets the bus-cycle kind (opcode fetch, operand fetch, data read)
//  - a callback may advance the clock (Z80nCpuAddWaitStates)
//  - Z80N: the instructions of the "Z80N additions" section at the end (NEXTREG through a callback, the stackless
//    NMI of NextREG #C0 bit 3); sizes and T-states from https://table.specnext.dev/

#ifndef Z80NCPU_H
#define Z80NCPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Z80nCPU Z80nCPU;

// Bus-cycle kinds reported to the contention (wait-state) hook.
typedef enum
{
    Z80nCpuAccessM1 = 0,    // opcode/prefix fetch (M1, refresh), 4 T cycle;
                           // also every 4 T quantum of a halted CPU
                           // (addr = PC, i.e. the HALT opcode)
    Z80nCpuAccessOperand,   // instruction byte after the opcode (immediate,
                           // displacement, address), 3 T cycle at PC
    Z80nCpuAccessRead,      // data memory read, 3 T cycle
    Z80nCpuAccessWrite,     // memory write, 3 T cycle
    Z80nCpuAccessPortIn,    // port read, fired BEFORE IORQ (T = IO cycle start)
    Z80nCpuAccessPortOut,   // port write, fired BEFORE IORQ (T = IO cycle start)
    Z80nCpuAccessPortInPost,   // port read, fired AFTER the port callback (T = IORQ T)
    Z80nCpuAccessPortOutPost,  // port write, fired AFTER the port callback (T = IORQ T)
    Z80nCpuAccessInternal      // one internal (no-MREQ) T-state with addr on the
                              // address bus: IR after M1 (INC rr, ADD HL,rr,
                              // PUSH ...), the displacement (JR, (IX+d)), HL
                              // (INC (HL), RLD), SP, DE / HL / BC (block
                              // repeats) - fired once per T-state, at its start
} Z80nCpuAccessKind;

// Bus callbacks. All receive the CPU context and the userData bound at setup
// time. The memory read callback gets the kind of the bus cycle: Z80nCpuAccessM1
// for an opcode or prefix fetch (also every M1 of a halted CPU, at PC, and the
// operation byte of DD CB d op / FD CB d op is an operand), Z80nCpuAccessOperand
// for the immediate / displacement / address bytes after it, Z80nCpuAccessRead
// for a data read (the IM2 vector-table reads included). Z80nCpuTstates() inside
// a callback is the T of the access (after the cycle's 3 T, before the chip's
// programmed waits); a callback may add the board's external /WAIT with
// Z80nCpuAddWaitStates, never otherwise change the clock.
typedef uint8_t (*Z80nCpuMemReadFn)(Z80nCPU* cpu, uint16_t addr, Z80nCpuAccessKind kind, void* userData);
typedef void (*Z80nCpuMemWriteFn)(Z80nCPU* cpu, uint16_t addr, uint8_t value, void* userData);
typedef uint8_t (*Z80nCpuPortInFn)(Z80nCPU* cpu, uint16_t port, void* userData);
typedef void (*Z80nCpuPortOutFn)(Z80nCPU* cpu, uint16_t port, uint8_t value, void* userData);

// Supplies the interrupt vector byte during a maskable interrupt acknowledge
// (M1 + IORQ active). Only used in IM2; return 0xFF if no device drives the bus.
typedef uint8_t (*Z80nCpuIntVectorFn)(Z80nCPU* cpu, void* userData);

// Fired when a RETI instruction (ED 4D) completes - for PIO/CTC-style devices.
typedef void (*Z80nCpuRetiFn)(Z80nCPU* cpu, void* userData);

// Fired when a RETN instruction (ED 45 and its aliases ED 55/65/75)
// completes, after IFF1 has been restored from IFF2 - lets a host that
// tracks the NMI service session (e.g. an NMI-paged ROM) observe its end.
typedef void (*Z80nCpuRetnFn)(Z80nCPU* cpu, void* userData);

// Wait-state (memory/IO contention) hook. Called at the first T-state of
// every bus cycle with Z80nCpuTstates() equal to that T-state and addr = the address/port on
// the bus. The returned number of T-states is inserted BEFORE the cycle
// proceeds, so the memory/port callback then observes T = cycle start +
// waits + cycle offset (3 T for memory, +1 = IORQ for ports). Port cycles
// fire twice: the *Post kinds run after the port callback with T at IORQ
// and their waits extend the cycle (ULA-style "C:1, C:3" patterns).
// Internal (no-MREQ) T-states fire as Z80nCpuAccessInternal, one call per
// T-state with its start as the current T; the waits go before that T (the
// Ferranti ULA of the 48K/128K contends them, the +2A/+3 gate array does not:
// return 0 for them there). The per-instruction addresses and counts are
// FUSE's (checked against its no-MREQ checkpoints).
// Return 0 for uncontended accesses. The interrupt acknowledge sequences
// report their memory cycles too: the two stack pushes of Z80nCpuInt/
// Z80nCpuNmi as Z80nCpuAccessWrite and the IM2 vector-table reads as
// Z80nCpuAccessRead, each at the first T of its 3 T cycle (after the 7 T /
// 5 T acknowledge M1), with their waits extending the acknowledge.
typedef int (*Z80nCpuContendFn)(Z80nCPU* cpu, uint16_t addr, Z80nCpuAccessKind kind, void* userData);

// Register selectors for Z80nCpuGetReg/Z80nCpuSetReg.
// R  = low 7 bits of the refresh counter | current R7
// R7 = bit 7 of the refresh counter only
// Memptr/Q expose the undocumented internal registers (see z80.h in unreal-ng).
// Halted = the HALT latch (same value as Z80nCpuHalted); setting it lets a
// host that mirrors the CPU state in its own structures restore it.
typedef enum
{
    Z80nCpuRegAf = 0,
    Z80nCpuRegBc,
    Z80nCpuRegDe,
    Z80nCpuRegHl,
    Z80nCpuRegAfAlt,
    Z80nCpuRegBcAlt,
    Z80nCpuRegDeAlt,
    Z80nCpuRegHlAlt,
    Z80nCpuRegIx,
    Z80nCpuRegIy,
    Z80nCpuRegPc,
    Z80nCpuRegSp,
    Z80nCpuRegI,
    Z80nCpuRegR,
    Z80nCpuRegR7,
    Z80nCpuRegIm,
    Z80nCpuRegIff1,
    Z80nCpuRegIff2,
    Z80nCpuRegMemptr,
    Z80nCpuRegQ,
    Z80nCpuRegHalted,  // HALT latch (0/1); settable so a host can own HALT policy
    Z80nCpuRegBoundary,       // Z80nCpuBoundary: instruction-boundary state (see below), settable
    Z80nCpuRegNmiInProgress,  // set by NMI acknowledge, cleared by RETN
    Z80nCpuRegCount
} Z80nCpuReg;

// Library version string: the unreal-z80 version it was forked from and the
// fork's own revision, e.g. "0.5.0-z80n.1".
const char* Z80nCpuVersion(void);

// Lifecycle. The CPU starts in the post-reset state with no bus wired:
// with no memory callbacks, reads return 0xFF
// and writes are discarded.
Z80nCPU* Z80nCpuCreate(void);
void Z80nCpuDestroy(Z80nCPU* cpu);

// Reset (power-on semantics of the core: PC=0, SP=0xFFFF, AF=0xFFFF, IM0,
// IFF1/IFF2=0, I=R=0, Q=0, MEMPTR=0; general registers cleared for
// deterministic tests - a real chip leaves them undefined).
void Z80nCpuReset(Z80nCPU* cpu);

// Execute one instruction (or burn 4 T-states while halted). Returns the
// number of T-states consumed. A prefix chain is split only where the chip
// splits it: DD/FD followed by another DD/FD is a redundant prefix, an
// instruction of its own, and ends the step (4 T for it plus the 4 T M1 of
// the next prefix, which is then pending - Z80nCpuBoundaryPrefixDd/Fd); the
// next call runs the instruction that prefix introduces. Every step is bounded, even
// on memory filled with DD/FD. A halted CPU runs one M1 cycle per call: a
// read of the byte at PC (the HALT opcode) through the memory callback as
// Z80nCpuAccessM1, 4 T plus waits, R advanced - the M1 cycles the board sees.
int Z80nCpuStep(Z80nCPU* cpu);

// Direct step entry for hot host loops: calling it saves one call per
// instruction. Unlike Z80nCpuStep it returns the T-state counter AFTER the
// instruction (the same value Z80nCpuTstates() then reports, 32-bit wrap).
// Executes exactly one instruction, like Z80nCpuStep.
typedef int (*Z80nCpuStepFn)(Z80nCPU* cpu);
Z80nCpuStepFn Z80nCpuStepEntry(const Z80nCPU* cpu);

// Maskable interrupt request, to be raised by the host at an instruction
// boundary. Returns the T-states consumed (13 in IM0/IM1, 19 in IM2) if
// accepted, or 0 if rejected (IFF1=0, a pending prefix, or the INT shadow:
// right after EI, and right after a RETN/RETI that set IFF1 - the chip
// copies IFF2 to IFF1 too late for that boundary's INT sampling).
// On acceptance: pushes PC, IFF1=IFF2=0, PC=0x38 (IM0/IM1) or the IM2
// vector-table target, MEMPTR=target, HALT released.
int Z80nCpuInt(Z80nCPU* cpu);

// Non-maskable interrupt request at an instruction boundary. Accepted
// except while a prefix is pending and right after another NMI acknowledge
// (no two NMI responses without an instruction between them): returns 0,
// retry after the next step. The EI shadow does not block NMI.
// 11 T-states (plus any contention waits of the two stack writes; the
// return value is the total): push PC, PC=0x0066, MEMPTR=0x0066, IFF1=0 (IFF2
// unchanged: it keeps the pre-NMI state for RETN, also across nested NMIs),
// HALT released. RETN (or IFF1 restore) ends the NMI session flag.
int Z80nCpuNmi(Z80nCPU* cpu);

// Non-zero while the CPU is halted (HALT executed, no INT/NMI accepted yet).
int Z80nCpuHalted(const Z80nCPU* cpu);

// 1 if a maskable interrupt would currently be accepted (IFF1 set, no INT
// shadow and no pending prefix).
int Z80nCpuIntPossible(const Z80nCPU* cpu);

// Total T-states since the last reset (also settable for snapshot restore,
// and by a host that keeps its own time base: set before a step, read after).
uint32_t Z80nCpuTstates(const Z80nCPU* cpu);
void Z80nCpuSetTstates(Z80nCPU* cpu, uint32_t tstates);

// The board's external /WAIT: `tstates` more T-states for the bus cycle in
// progress. Called from inside a memory or port callback (the core takes the
// clock back after every callback); the chip's programmed waits for the same
// cycle are added after the callback, so both add up (PS0182 p. 309).
void Z80nCpuAddWaitStates(Z80nCPU* cpu, uint32_t tstates);

// Address of the first byte (first prefix) of the instruction currently
// executing / last executed - stable from the M1 fetch until the next
// Z80nCpuStep. Behind redundant prefixes the instruction starts at its last
// DD/FD (each earlier prefix was a step of its own). Port decoders that gate on the instruction address (TR-DOS
// style ROM-only ports) read it from inside the port callbacks.
uint16_t Z80nCpuInstructionPc(const Z80nCPU* cpu);

// The last dispatched opcode byte (low) and the prefix class it ran under
// (high: 0x00 none or DD/FD, 0xCB incl. DDCB/FDCB, 0xED), for a host's
// profiler or debugger view. Stable from the step's end to the next step.
uint16_t Z80nCpuOpcodeWord(const Z80nCPU* cpu);

uint16_t Z80nCpuGetReg(const Z80nCPU* cpu, Z80nCpuReg reg);
void Z80nCpuSetReg(Z80nCPU* cpu, Z80nCpuReg reg, uint16_t value);

// The whole register file in one call - for hosts that keep the CPU state in
// their own structures and move it in and out around every step (a debugger
// or snapshot code writes those directly). Equivalent to Z80nCpuGetReg/
// Z80nCpuSetReg for every selector, with the same value conventions (r = low
// 7 bits of the refresh counter | R7; set masks iff1/iff2 to bit 0, q to
// bits 3/5, clamps im to 2), but without one call and one selector dispatch
// per register on the host's hot path.
typedef struct Z80nCpuRegisters
{
    uint16_t af, bc, de, hl;
    uint16_t afAlt, bcAlt, deAlt, hlAlt;
    uint16_t ix, iy, pc, sp;
    uint16_t memptr;
    uint8_t i, r, im;
    uint8_t iff1, iff2;
    uint8_t q;
    uint8_t halted;
    uint8_t boundary;       // Z80nCpuBoundary
    uint8_t nmiInProgress;
} Z80nCpuRegisters;

// Instruction-boundary state: what the CPU carries from one boundary to the
// next beyond the registers, i.e. what decides the next INT/NMI acceptance.
// One value at a time - each is a property of the last fetched opcode or the
// last acknowledge. A snapshot that stores Z80nCpuRegisters in full (with
// nmiInProgress) restores the CPU exactly at any boundary; an unknown value
// sets None.
typedef enum
{
    Z80nCpuBoundaryNone = 0,
    Z80nCpuBoundaryPrefixDd,  // a redundant prefix ended the step, DD is pending: INT and NMI refused
    Z80nCpuBoundaryPrefixFd,  // same, FD pending
    Z80nCpuBoundaryIntShadow, // after EI, or a RETN/RETI that set IFF1: INT refused (NMI accepted)
    Z80nCpuBoundaryLdAIr,     // after LD A,I / LD A,R: an INT accepted here clears P/V
    Z80nCpuBoundaryNmiAck     // an NMI was just acknowledged: a second NMI refused
} Z80nCpuBoundary;

void Z80nCpuGetRegisters(const Z80nCPU* cpu, Z80nCpuRegisters* regs);
void Z80nCpuSetRegisters(Z80nCPU* cpu, const Z80nCpuRegisters* regs);

// Zero-copy register file. The engine executes directly on this block: a
// host that keeps its own register structure with exactly this layout
// attaches it once and never copies registers around a step. Packed,
// native (little-endian) 16-bit words; the 8-bit halves of the pairs follow
// the little-endian byte order (low byte first).
// Conventions the host must follow while attached:
//  - R: rLow bits 0-6 are the refresh counter (the engine advances them,
//    bit 7 of rLow is left as it is), rHi bit 7 is R7. LD R,A writes
//    rLow = A and rHi = A & 0x80. The observable R is
//    (rLow & 0x7F) | (rHi & 0x80).
//  - iff1/iff2 are 0 or 1; im is 0..2; q holds only bits 3 and 5.
//  - halted bit 0 is the live HALT latch: the engine sets it on HALT,
//    clears it on INT/NMI acknowledge, and runs halted M1 quanta while it
//    is set. Bit 1 is engine-owned: set between the steps of a split
//    prefix chain (Z80nCpuBoundaryPrefixDd/Fd), for one step's routing. Test
//    bit 0 for "halted"; leave bit 1 alone.
//  - reservedEipos/reservedHaltpos are host-owned: the engine never reads
//    or writes them (they exist so the block is contiguous with a host
//    structure that keeps its EI-shadow/HALT-entry bookkeeping there).
// Layout is a contract: every offset is pinned by the static assertions
// below (a drift is a compile error, never silent corruption).
#pragma pack(push, 1)
typedef struct Z80nCpuRegisterFile
{
    uint16_t pc;
    uint16_t sp;
    uint8_t rLow;     // refresh counter (bits 0-6), bit 7 host-stable
    uint8_t i;
    uint8_t rHi;      // bit 7 = R7
    uint8_t iff1;
    uint8_t iff2;
    uint8_t halted;   // bit 0 = HALT latch, bit 1 engine-owned (see above)
    uint16_t bc, de, hl, af;
    uint16_t ix, iy;
    uint16_t bcAlt, deAlt, hlAlt, afAlt;
    uint16_t memptr;
    uint8_t q;
    int32_t reservedEipos;     // host-owned, never touched by the engine
    uint16_t reservedHaltpos;  // host-owned, never touched by the engine
    uint8_t im;
    uint8_t nmiInProgress;     // set by NMI acknowledge, cleared by RETN
} Z80nCpuRegisterFile;
#pragma pack(pop)

#if defined(__cplusplus)
#define Z80NCPU_STATIC_ASSERT(c, m) static_assert(c, m)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define Z80NCPU_STATIC_ASSERT(c, m) _Static_assert(c, m)
#else  /* C89/C99: a negative array size is the compile error */
#define Z80NCPU_SA_CAT2(a, b) a##b
#define Z80NCPU_SA_CAT(a, b) Z80NCPU_SA_CAT2(a, b)
#define Z80NCPU_STATIC_ASSERT(c, m) typedef char Z80NCPU_SA_CAT(z80ncpu_layout_check_, __LINE__)[(c) ? 1 : -1]
#endif
Z80NCPU_STATIC_ASSERT(sizeof(Z80nCpuRegisterFile) == 41, "Z80nCpuRegisterFile size");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, pc) == 0, "pc");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, sp) == 2, "sp");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, rLow) == 4, "rLow");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, i) == 5, "i");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, rHi) == 6, "rHi");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, iff1) == 7, "iff1");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, iff2) == 8, "iff2");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, halted) == 9, "halted");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, bc) == 10, "bc");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, de) == 12, "de");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, hl) == 14, "hl");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, af) == 16, "af");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, ix) == 18, "ix");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, iy) == 20, "iy");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, bcAlt) == 22, "bcAlt");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, deAlt) == 24, "deAlt");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, hlAlt) == 26, "hlAlt");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, afAlt) == 28, "afAlt");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, memptr) == 30, "memptr");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, q) == 32, "q");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, reservedEipos) == 33, "reservedEipos");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, reservedHaltpos) == 37, "reservedHaltpos");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, im) == 39, "im");
Z80NCPU_STATIC_ASSERT(offsetof(Z80nCpuRegisterFile, nmiInProgress) == 40, "nmiInProgress");

// Attach a host-owned register file: from now on every register access of
// the engine (execution, reset, interrupts, the Get/Set register APIs) goes
// to this memory, with no copy: its current contents become the CPU state
// as they are (the host's registers win). The block must stay valid until
// detached. Pass null to detach: the attached contents are copied back into
// the engine's own storage, which becomes active again, so no state is lost
// switching back. Attaching another block while one is attached switches
// straight to it (its contents win again).
void Z80nCpuAttachRegisterFile(Z80nCPU* cpu, Z80nCpuRegisterFile* file);

// The register file the engine currently executes on (the attached host
// block, or the engine's own storage) - a direct, always-current view.
Z80nCpuRegisterFile* Z80nCpuRegisterFilePtr(Z80nCPU* cpu);

// Bus wiring. Any callback may be null: memory reads return 0xFF, writes and
// port writes are discarded, port reads return 0xFF, INT vector = 0xFF
// (null pointers are replaced by internal stubs, so the bus primitives
// never test for null). The callback bus is the only bus of this fork.
void Z80nCpuSetMemoryBus(Z80nCPU* cpu, Z80nCpuMemReadFn readFn, void* readData,
                        Z80nCpuMemWriteFn writeFn, void* writeData);
void Z80nCpuSetPortBus(Z80nCPU* cpu, Z80nCpuPortInFn inFn, void* inData,
                      Z80nCpuPortOutFn outFn, void* outData);
void Z80nCpuSetIntVectorFn(Z80nCPU* cpu, Z80nCpuIntVectorFn fn, void* userData);
void Z80nCpuSetRetiFn(Z80nCPU* cpu, Z80nCpuRetiFn fn, void* userData);
void Z80nCpuSetRetnFn(Z80nCPU* cpu, Z80nCpuRetnFn fn, void* userData);
void Z80nCpuSetContendFn(Z80nCPU* cpu, Z80nCpuContendFn fn, void* userData);

// Value written by the undocumented OUT (C),0 (ED 71). NMOS Z80s write 0 (the default here);
// a CMOS part writes 0xFF.
void Z80nCpuSetOutC0Value(Z80nCPU* cpu, uint8_t value);

// NMOS (default 1): an INT accepted right after LD A,I / LD A,R clears P/V; 0 = CMOS behavior.
void Z80nCpuSetLdAirQuirk(Z80nCPU* cpu, int enabled);

// ---- Z80N additions ----

// NEXTREG n,x and NEXTREG n,A: the core spends no port cycle; the host is told (register, value) at the cycle
// where the write happens (cpu->t is published; the callback may advance the clock, like any callback).
typedef void (*Z80nCpuNextRegFn)(Z80nCPU* cpu, uint8_t reg, uint8_t value, void* userData);
void Z80nCpuSetNextRegFn(Z80nCPU* cpu, Z80nCpuNextRegFn fn, void* userData);

// Stackless NMI (NextREG #C0 bit 3). With the mode on, the NMI acknowledge still counts its two write cycles and
// moves SP down by 2, but writes no memory: the return address goes to `store` (low, high); the next RETN (any of
// ED 45 / 55 / 5D / 65 / 6D / 75 / 7D) reads it back through `load` instead of the stack (SP up by 2).
typedef void (*Z80nCpuNmiStoreFn)(Z80nCPU* cpu, uint8_t low, uint8_t high, void* userData);
typedef uint16_t (*Z80nCpuNmiLoadFn)(Z80nCPU* cpu, void* userData);
void Z80nCpuSetStacklessNmi(Z80nCPU* cpu, int enabled, Z80nCpuNmiStoreFn store, Z80nCpuNmiLoadFn load, void* userData);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // Z80NCPU_H
