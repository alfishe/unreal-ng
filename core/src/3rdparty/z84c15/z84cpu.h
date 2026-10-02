// z84cpu.h - public C API of the Z84C15 CPU core (fork of unreal-z80 0.5.0).
//
// The Zilog Z84C15's CPU: a CMOS Z84C00 core plus the on-chip wait-state
// generator, which stretches the core's own bus cycles. Forked from the
// unreal-z80 library (an extraction of unreal-ng's native Z80 core) at
// commit a0433ec, version 0.5.0; every change is listed in README.md. The
// API keeps unreal-z80's shape under the Z84Cpu prefix, so this library links
// beside unreal-z80 (the General Sound card) and the native core:
//  - opaque CPU context, host-supplied bus callbacks, one instruction per
//    call, host-driven INT/NMI, register accessors, zero-copy register file
//  - full undocumented behavior: MEMPTR (WZ), Q (Zilog SCF/CCF), IXH/IXL/IYH/
//    IYL, SLL, IN (C), X/Y flags (F3/F5)
//  - CMOS core (research-cpu-z84c15.md section 3): OUT (C),0 writes #FF, and
//    LD A,I / LD A,R followed by an accepted INT keep P/V (no NMOS quirk)
//  - only the callback bus: every memory and port cycle reaches the host,
//    which owns paging and the board's /WAIT logic
//  - the memory read callback gets the bus-cycle kind (opcode fetch, operand
//    fetch, data read): the wait generator and the host tell them apart
//  - a callback may advance the clock (Z84CpuAddWaitStates): the board's
//    external /WAIT, added after the chip's programmed waits
//  - the on-chip block (wait generator, chip selects, watchdog, CTC/SIO/PIO,
//    daisy chain) is the C++ class Z84Lib::Z84C15 in z84c15.h

#ifndef Z84CPU_H
#define Z84CPU_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Z84CPU Z84CPU;

// Bus-cycle kinds reported to the contention (wait-state) hook.
typedef enum
{
    Z84CpuAccessM1 = 0,    // opcode/prefix fetch (M1, refresh), 4 T cycle;
                           // also every 4 T quantum of a halted CPU
                           // (addr = PC, i.e. the HALT opcode)
    Z84CpuAccessOperand,   // instruction byte after the opcode (immediate,
                           // displacement, address), 3 T cycle at PC
    Z84CpuAccessRead,      // data memory read, 3 T cycle
    Z84CpuAccessWrite,     // memory write, 3 T cycle
    Z84CpuAccessPortIn,    // port read, fired BEFORE IORQ (T = IO cycle start)
    Z84CpuAccessPortOut,   // port write, fired BEFORE IORQ (T = IO cycle start)
    Z84CpuAccessPortInPost,   // port read, fired AFTER the port callback (T = IORQ T)
    Z84CpuAccessPortOutPost,  // port write, fired AFTER the port callback (T = IORQ T)
    Z84CpuAccessInternal      // one internal (no-MREQ) T-state with addr on the
                              // address bus: IR after M1 (INC rr, ADD HL,rr,
                              // PUSH ...), the displacement (JR, (IX+d)), HL
                              // (INC (HL), RLD), SP, DE / HL / BC (block
                              // repeats) - fired once per T-state, at its start
} Z84CpuAccessKind;

// Bus callbacks. All receive the CPU context and the userData bound at setup
// time. The memory read callback gets the kind of the bus cycle: Z84CpuAccessM1
// for an opcode or prefix fetch (also every M1 of a halted CPU, at PC, and the
// operation byte of DD CB d op / FD CB d op is an operand), Z84CpuAccessOperand
// for the immediate / displacement / address bytes after it, Z84CpuAccessRead
// for a data read (the IM2 vector-table reads included). Z84CpuTstates() inside
// a callback is the T of the access (after the cycle's 3 T, before the chip's
// programmed waits); a callback may add the board's external /WAIT with
// Z84CpuAddWaitStates, never otherwise change the clock.
typedef uint8_t (*Z84CpuMemReadFn)(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, void* userData);
typedef void (*Z84CpuMemWriteFn)(Z84CPU* cpu, uint16_t addr, uint8_t value, void* userData);
typedef uint8_t (*Z84CpuPortInFn)(Z84CPU* cpu, uint16_t port, void* userData);
typedef void (*Z84CpuPortOutFn)(Z84CPU* cpu, uint16_t port, uint8_t value, void* userData);

// Supplies the interrupt vector byte during a maskable interrupt acknowledge
// (M1 + IORQ active). Only used in IM2; return 0xFF if no device drives the bus.
typedef uint8_t (*Z84CpuIntVectorFn)(Z84CPU* cpu, void* userData);

// Fired when a RETI instruction (ED 4D) completes - for PIO/CTC-style devices.
typedef void (*Z84CpuRetiFn)(Z84CPU* cpu, void* userData);

// Fired when a RETN instruction (ED 45 and its aliases ED 55/65/75)
// completes, after IFF1 has been restored from IFF2 - lets a host that
// tracks the NMI service session (e.g. an NMI-paged ROM) observe its end.
typedef void (*Z84CpuRetnFn)(Z84CPU* cpu, void* userData);

// Wait-state (memory/IO contention) hook. Called at the first T-state of
// every bus cycle with Z84CpuTstates() equal to that T-state and addr = the address/port on
// the bus. The returned number of T-states is inserted BEFORE the cycle
// proceeds, so the memory/port callback then observes T = cycle start +
// waits + cycle offset (3 T for memory, +1 = IORQ for ports). Port cycles
// fire twice: the *Post kinds run after the port callback with T at IORQ
// and their waits extend the cycle (ULA-style "C:1, C:3" patterns).
// Internal (no-MREQ) T-states fire as Z84CpuAccessInternal, one call per
// T-state with its start as the current T; the waits go before that T (the
// Ferranti ULA of the 48K/128K contends them, the +2A/+3 gate array does not:
// return 0 for them there). The per-instruction addresses and counts are
// FUSE's (checked against its no-MREQ checkpoints).
// Return 0 for uncontended accesses. The interrupt acknowledge sequences
// report their memory cycles too: the two stack pushes of Z84CpuInt/
// Z84CpuNmi as Z84CpuAccessWrite and the IM2 vector-table reads as
// Z84CpuAccessRead, each at the first T of its 3 T cycle (after the 7 T /
// 5 T acknowledge M1), with their waits extending the acknowledge.
typedef int (*Z84CpuContendFn)(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, void* userData);

// Register selectors for Z84CpuGetReg/Z84CpuSetReg.
// R  = low 7 bits of the refresh counter | current R7
// R7 = bit 7 of the refresh counter only
// Memptr/Q expose the undocumented internal registers (see z80.h in unreal-ng).
// Halted = the HALT latch (same value as Z84CpuHalted); setting it lets a
// host that mirrors the CPU state in its own structures restore it.
typedef enum
{
    Z84CpuRegAf = 0,
    Z84CpuRegBc,
    Z84CpuRegDe,
    Z84CpuRegHl,
    Z84CpuRegAfAlt,
    Z84CpuRegBcAlt,
    Z84CpuRegDeAlt,
    Z84CpuRegHlAlt,
    Z84CpuRegIx,
    Z84CpuRegIy,
    Z84CpuRegPc,
    Z84CpuRegSp,
    Z84CpuRegI,
    Z84CpuRegR,
    Z84CpuRegR7,
    Z84CpuRegIm,
    Z84CpuRegIff1,
    Z84CpuRegIff2,
    Z84CpuRegMemptr,
    Z84CpuRegQ,
    Z84CpuRegHalted,  // HALT latch (0/1); settable so a host can own HALT policy
    Z84CpuRegBoundary,       // Z84CpuBoundary: instruction-boundary state (see below), settable
    Z84CpuRegNmiInProgress,  // set by NMI acknowledge, cleared by RETN
    Z84CpuRegCount
} Z84CpuReg;

// Library version string: the unreal-z80 version it was forked from and the
// fork's own revision, e.g. "0.5.0-z84c15.1".
const char* Z84CpuVersion(void);

// Lifecycle. The CPU starts in the post-reset state with no bus wired:
// with no memory callbacks, reads return 0xFF
// and writes are discarded.
Z84CPU* Z84CpuCreate(void);
void Z84CpuDestroy(Z84CPU* cpu);

// Reset (power-on semantics of the core: PC=0, SP=0xFFFF, AF=0xFFFF, IM0,
// IFF1/IFF2=0, I=R=0, Q=0, MEMPTR=0; general registers cleared for
// deterministic tests - a real chip leaves them undefined).
void Z84CpuReset(Z84CPU* cpu);

// Execute one instruction (or burn 4 T-states while halted). Returns the
// number of T-states consumed. A prefix chain is split only where the chip
// splits it: DD/FD followed by another DD/FD is a redundant prefix, an
// instruction of its own, and ends the step (4 T for it plus the 4 T M1 of
// the next prefix, which is then pending - Z84CpuBoundaryPrefixDd/Fd); the
// next call runs the instruction that prefix introduces. Every step is bounded, even
// on memory filled with DD/FD. A halted CPU runs one M1 cycle per call: a
// read of the byte at PC (the HALT opcode) through the memory callback as
// Z84CpuAccessM1, 4 T plus waits, R advanced - the M1 cycles the board sees.
int Z84CpuStep(Z84CPU* cpu);

// Direct step entry for hot host loops: calling it saves one call per
// instruction. Unlike Z84CpuStep it returns the T-state counter AFTER the
// instruction (the same value Z84CpuTstates() then reports, 32-bit wrap).
// Executes exactly one instruction, like Z84CpuStep.
typedef int (*Z84CpuStepFn)(Z84CPU* cpu);
Z84CpuStepFn Z84CpuStepEntry(const Z84CPU* cpu);

// Maskable interrupt request, to be raised by the host at an instruction
// boundary. Returns the T-states consumed (13 in IM0/IM1, 19 in IM2) if
// accepted, or 0 if rejected (IFF1=0, a pending prefix, or the INT shadow:
// right after EI, and right after a RETN/RETI that set IFF1 - the chip
// copies IFF2 to IFF1 too late for that boundary's INT sampling).
// On acceptance: pushes PC, IFF1=IFF2=0, PC=0x38 (IM0/IM1) or the IM2
// vector-table target, MEMPTR=target, HALT released.
int Z84CpuInt(Z84CPU* cpu);

// Non-maskable interrupt request at an instruction boundary. Accepted
// except while a prefix is pending and right after another NMI acknowledge
// (no two NMI responses without an instruction between them): returns 0,
// retry after the next step. The EI shadow does not block NMI.
// 11 T-states (plus any contention waits of the two stack writes; the
// return value is the total): push PC, PC=0x0066, MEMPTR=0x0066, IFF1=0 (IFF2
// unchanged: it keeps the pre-NMI state for RETN, also across nested NMIs),
// HALT released. RETN (or IFF1 restore) ends the NMI session flag.
int Z84CpuNmi(Z84CPU* cpu);

// Non-zero while the CPU is halted (HALT executed, no INT/NMI accepted yet).
int Z84CpuHalted(const Z84CPU* cpu);

// 1 if a maskable interrupt would currently be accepted (IFF1 set, no INT
// shadow and no pending prefix).
int Z84CpuIntPossible(const Z84CPU* cpu);

// Total T-states since the last reset (also settable for snapshot restore,
// and by a host that keeps its own time base: set before a step, read after).
uint32_t Z84CpuTstates(const Z84CPU* cpu);
void Z84CpuSetTstates(Z84CPU* cpu, uint32_t tstates);

// The board's external /WAIT: `tstates` more T-states for the bus cycle in
// progress. Called from inside a memory or port callback (the core takes the
// clock back after every callback); the chip's programmed waits for the same
// cycle are added after the callback, so both add up (PS0182 p. 309).
void Z84CpuAddWaitStates(Z84CPU* cpu, uint32_t tstates);

// Address of the first byte (first prefix) of the instruction currently
// executing / last executed - stable from the M1 fetch until the next
// Z84CpuStep. Behind redundant prefixes the instruction starts at its last
// DD/FD (each earlier prefix was a step of its own). Port decoders that gate on the instruction address (TR-DOS
// style ROM-only ports) read it from inside the port callbacks.
uint16_t Z84CpuInstructionPc(const Z84CPU* cpu);

// The last dispatched opcode byte (low) and the prefix class it ran under
// (high: 0x00 none or DD/FD, 0xCB incl. DDCB/FDCB, 0xED), for a host's
// profiler or debugger view. Stable from the step's end to the next step.
uint16_t Z84CpuOpcodeWord(const Z84CPU* cpu);

uint16_t Z84CpuGetReg(const Z84CPU* cpu, Z84CpuReg reg);
void Z84CpuSetReg(Z84CPU* cpu, Z84CpuReg reg, uint16_t value);

// The whole register file in one call - for hosts that keep the CPU state in
// their own structures and move it in and out around every step (a debugger
// or snapshot code writes those directly). Equivalent to Z84CpuGetReg/
// Z84CpuSetReg for every selector, with the same value conventions (r = low
// 7 bits of the refresh counter | R7; set masks iff1/iff2 to bit 0, q to
// bits 3/5, clamps im to 2), but without one call and one selector dispatch
// per register on the host's hot path.
typedef struct Z84CpuRegisters
{
    uint16_t af, bc, de, hl;
    uint16_t afAlt, bcAlt, deAlt, hlAlt;
    uint16_t ix, iy, pc, sp;
    uint16_t memptr;
    uint8_t i, r, im;
    uint8_t iff1, iff2;
    uint8_t q;
    uint8_t halted;
    uint8_t boundary;       // Z84CpuBoundary
    uint8_t nmiInProgress;
} Z84CpuRegisters;

// Instruction-boundary state: what the CPU carries from one boundary to the
// next beyond the registers, i.e. what decides the next INT/NMI acceptance.
// One value at a time - each is a property of the last fetched opcode or the
// last acknowledge. A snapshot that stores Z84CpuRegisters in full (with
// nmiInProgress) restores the CPU exactly at any boundary; an unknown value
// sets None.
typedef enum
{
    Z84CpuBoundaryNone = 0,
    Z84CpuBoundaryPrefixDd,  // a redundant prefix ended the step, DD is pending: INT and NMI refused
    Z84CpuBoundaryPrefixFd,  // same, FD pending
    Z84CpuBoundaryIntShadow, // after EI, or a RETN/RETI that set IFF1: INT refused (NMI accepted)
    Z84CpuBoundaryLdAIr,     // after LD A,I / LD A,R: an INT accepted here clears P/V
    Z84CpuBoundaryNmiAck     // an NMI was just acknowledged: a second NMI refused
} Z84CpuBoundary;

void Z84CpuGetRegisters(const Z84CPU* cpu, Z84CpuRegisters* regs);
void Z84CpuSetRegisters(Z84CPU* cpu, const Z84CpuRegisters* regs);

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
//    prefix chain (Z84CpuBoundaryPrefixDd/Fd), for one step's routing. Test
//    bit 0 for "halted"; leave bit 1 alone.
//  - reservedEipos/reservedHaltpos are host-owned: the engine never reads
//    or writes them (they exist so the block is contiguous with a host
//    structure that keeps its EI-shadow/HALT-entry bookkeeping there).
// Layout is a contract: every offset is pinned by the static assertions
// below (a drift is a compile error, never silent corruption).
#pragma pack(push, 1)
typedef struct Z84CpuRegisterFile
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
} Z84CpuRegisterFile;
#pragma pack(pop)

#if defined(__cplusplus)
#define Z84CPU_STATIC_ASSERT(c, m) static_assert(c, m)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define Z84CPU_STATIC_ASSERT(c, m) _Static_assert(c, m)
#else  /* C89/C99: a negative array size is the compile error */
#define Z84CPU_SA_CAT2(a, b) a##b
#define Z84CPU_SA_CAT(a, b) Z84CPU_SA_CAT2(a, b)
#define Z84CPU_STATIC_ASSERT(c, m) typedef char Z84CPU_SA_CAT(z84cpu_layout_check_, __LINE__)[(c) ? 1 : -1]
#endif
Z84CPU_STATIC_ASSERT(sizeof(Z84CpuRegisterFile) == 41, "Z84CpuRegisterFile size");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, pc) == 0, "pc");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, sp) == 2, "sp");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, rLow) == 4, "rLow");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, i) == 5, "i");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, rHi) == 6, "rHi");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, iff1) == 7, "iff1");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, iff2) == 8, "iff2");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, halted) == 9, "halted");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, bc) == 10, "bc");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, de) == 12, "de");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, hl) == 14, "hl");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, af) == 16, "af");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, ix) == 18, "ix");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, iy) == 20, "iy");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, bcAlt) == 22, "bcAlt");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, deAlt) == 24, "deAlt");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, hlAlt) == 26, "hlAlt");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, afAlt) == 28, "afAlt");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, memptr) == 30, "memptr");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, q) == 32, "q");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, reservedEipos) == 33, "reservedEipos");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, reservedHaltpos) == 37, "reservedHaltpos");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, im) == 39, "im");
Z84CPU_STATIC_ASSERT(offsetof(Z84CpuRegisterFile, nmiInProgress) == 40, "nmiInProgress");

// Attach a host-owned register file: from now on every register access of
// the engine (execution, reset, interrupts, the Get/Set register APIs) goes
// to this memory, with no copy: its current contents become the CPU state
// as they are (the host's registers win). The block must stay valid until
// detached. Pass null to detach: the attached contents are copied back into
// the engine's own storage, which becomes active again, so no state is lost
// switching back. Attaching another block while one is attached switches
// straight to it (its contents win again).
void Z84CpuAttachRegisterFile(Z84CPU* cpu, Z84CpuRegisterFile* file);

// The register file the engine currently executes on (the attached host
// block, or the engine's own storage) - a direct, always-current view.
Z84CpuRegisterFile* Z84CpuRegisterFilePtr(Z84CPU* cpu);

// Bus wiring. Any callback may be null: memory reads return 0xFF, writes and
// port writes are discarded, port reads return 0xFF, INT vector = 0xFF
// (null pointers are replaced by internal stubs, so the bus primitives
// never test for null). The callback bus is the only bus of this fork.
void Z84CpuSetMemoryBus(Z84CPU* cpu, Z84CpuMemReadFn readFn, void* readData,
                        Z84CpuMemWriteFn writeFn, void* writeData);
void Z84CpuSetPortBus(Z84CPU* cpu, Z84CpuPortInFn inFn, void* inData,
                      Z84CpuPortOutFn outFn, void* outData);
void Z84CpuSetIntVectorFn(Z84CPU* cpu, Z84CpuIntVectorFn fn, void* userData);
void Z84CpuSetRetiFn(Z84CPU* cpu, Z84CpuRetiFn fn, void* userData);
void Z84CpuSetRetnFn(Z84CPU* cpu, Z84CpuRetnFn fn, void* userData);
void Z84CpuSetContendFn(Z84CPU* cpu, Z84CpuContendFn fn, void* userData);

// Value written by the undocumented OUT (C),0 (ED 71). NMOS Z80s write 0;
// the Z84C15's CMOS core writes 0xFF (research-cpu-z84c15.md section 3), the
// default here.
void Z84CpuSetOutC0Value(Z84CPU* cpu, uint8_t value);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // Z84CPU_H
