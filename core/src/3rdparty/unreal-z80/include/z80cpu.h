// z80cpu.h - public C API of the standalone Z80 CPU core (unreal-z80).
//
// Extraction of the emulator core's Z80 CPU (core/src/emulator/cpu/*) into a
// self-contained, dependency-free library, structured after the z80ex library
// (thirdparty/z80ex/z80ex.h): opaque CPU context, host-supplied bus callbacks,
// one-instruction-per-call stepping, host-driven INT/NMI, register accessors.
//
// Differences from z80ex, by design:
//  - full undocumented behavior inherited from the core: MEMPTR (WZ), the Q
//    register (Zilog SCF/CCF XCF flavor), IXH/IXL/IYH/IYL, SLL, IN (C),
//    OUT (C),0, X/Y flags (F3/F5) everywhere
//  - optional flat-memory fast path (Z80CpuAttachMemory) that bypasses the
//    read/write callbacks with inline 64K array accesses, and a paged bus
//    (Z80CpuAttachPageTables) for bank-switched hosts
//  - no per-T-state callback: memory/IO contention is a wait-state hook
//    (Z80CpuSetContendFn) fired at the first T of every bus cycle, and the
//    bus callbacks observe the exact T of their access via Z80CpuTstates()
//  - MEMPTR and Q are exposed through the register API
//
// Naming note: the public API follows the project PascalCase convention; the
// ported opcode implementation units intentionally keep the core's internal
// naming (op_XX/ope_XX/opx_XX, *_f tables) so they stay diffable against
// core/src/emulator/cpu.

#ifndef Z80CPU_H
#define Z80CPU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Z80CPU Z80CPU;

// Bus callbacks. All receive the CPU context and the userData bound at setup
// time. m1State is non-zero for every instruction-stream read: the opcode and
// prefix fetches (M1) and the operand, displacement and address bytes that
// follow them (an "execution" access, as opposed to a data read). A host that
// needs the M1 cycles alone gets them from the contention hook
// (Z80CpuAccessM1 vs Z80CpuAccessOperand) or from the instruction structure.
typedef uint8_t (*Z80CpuMemReadFn)(Z80CPU* cpu, uint16_t addr, int m1State, void* userData);
typedef void (*Z80CpuMemWriteFn)(Z80CPU* cpu, uint16_t addr, uint8_t value, void* userData);
typedef uint8_t (*Z80CpuPortInFn)(Z80CPU* cpu, uint16_t port, void* userData);
typedef void (*Z80CpuPortOutFn)(Z80CPU* cpu, uint16_t port, uint8_t value, void* userData);

// Supplies the interrupt vector byte during a maskable interrupt acknowledge
// (M1 + IORQ active). Only used in IM2; return 0xFF if no device drives the bus.
typedef uint8_t (*Z80CpuIntVectorFn)(Z80CPU* cpu, void* userData);

// Fired when a RETI instruction (ED 4D) completes - for PIO/CTC-style devices.
typedef void (*Z80CpuRetiFn)(Z80CPU* cpu, void* userData);

// Fired when a RETN instruction (ED 45 and its aliases ED 55/65/75)
// completes, after IFF1 has been restored from IFF2 - lets a host that
// tracks the NMI service session (e.g. an NMI-paged ROM) observe its end.
typedef void (*Z80CpuRetnFn)(Z80CPU* cpu, void* userData);

// Bus-cycle kinds reported to the contention (wait-state) hook.
typedef enum
{
    Z80CpuAccessM1 = 0,    // opcode/prefix fetch (M1, refresh), 4 T cycle;
                           // also every 4 T quantum of a halted CPU
                           // (addr = PC, i.e. the HALT opcode)
    Z80CpuAccessOperand,   // instruction byte after the opcode (immediate,
                           // displacement, address), 3 T cycle at PC
    Z80CpuAccessRead,      // data memory read, 3 T cycle
    Z80CpuAccessWrite,     // memory write, 3 T cycle
    Z80CpuAccessPortIn,    // port read, fired BEFORE IORQ (T = IO cycle start)
    Z80CpuAccessPortOut,   // port write, fired BEFORE IORQ (T = IO cycle start)
    Z80CpuAccessPortInPost,   // port read, fired AFTER the port callback (T = IORQ T)
    Z80CpuAccessPortOutPost   // port write, fired AFTER the port callback (T = IORQ T)
} Z80CpuAccessKind;

// Wait-state (memory/IO contention) hook. Called at the first T-state of
// every bus cycle on the callback and paged buses (never on the flat bus)
// with Z80CpuTstates() equal to that T-state and addr = the address/port on
// the bus. The returned number of T-states is inserted BEFORE the cycle
// proceeds, so the memory/port callback then observes T = cycle start +
// waits + cycle offset (3 T for memory, +1 = IORQ for ports). Port cycles
// fire twice: the *Post kinds run after the port callback with T at IORQ
// and their waits extend the cycle (ULA-style "C:1, C:3" patterns).
// Return 0 for uncontended accesses. The interrupt acknowledge sequences
// report their memory cycles too: the two stack pushes of Z80CpuInt/
// Z80CpuNmi as Z80CpuAccessWrite and the IM2 vector-table reads as
// Z80CpuAccessRead, each at the first T of its 3 T cycle (after the 7 T /
// 5 T acknowledge M1), with their waits extending the acknowledge.
typedef int (*Z80CpuContendFn)(Z80CPU* cpu, uint16_t addr, Z80CpuAccessKind kind, void* userData);

// Register selectors for Z80CpuGetReg/Z80CpuSetReg.
// R  = low 7 bits of the refresh counter | current R7
// R7 = bit 7 of the refresh counter only
// Memptr/Q expose the undocumented internal registers (see z80.h in the core).
// Halted = the HALT latch (same value as Z80CpuHalted); setting it lets a
// host that mirrors the CPU state in its own structures restore it.
typedef enum
{
    Z80CpuRegAf = 0,
    Z80CpuRegBc,
    Z80CpuRegDe,
    Z80CpuRegHl,
    Z80CpuRegAfAlt,
    Z80CpuRegBcAlt,
    Z80CpuRegDeAlt,
    Z80CpuRegHlAlt,
    Z80CpuRegIx,
    Z80CpuRegIy,
    Z80CpuRegPc,
    Z80CpuRegSp,
    Z80CpuRegI,
    Z80CpuRegR,
    Z80CpuRegR7,
    Z80CpuRegIm,
    Z80CpuRegIff1,
    Z80CpuRegIff2,
    Z80CpuRegMemptr,
    Z80CpuRegQ,
    Z80CpuRegHalted,  // HALT latch (0/1); settable so a host can own HALT policy
    Z80CpuRegCount
} Z80CpuReg;

// Library version string, e.g. "0.2.0".
const char* Z80CpuVersion(void);

// Lifecycle. The CPU starts in the post-reset state with no bus wired:
// with no memory callbacks and no flat memory attached, reads return 0xFF
// and writes are discarded.
Z80CPU* Z80CpuCreate(void);
void Z80CpuDestroy(Z80CPU* cpu);

// Reset (power-on semantics of the core: PC=0, SP=0xFFFF, AF=0xFFFF, IM0,
// IFF1/IFF2=0, I=R=0, Q=0, MEMPTR=0; general registers cleared for
// deterministic tests - a real chip leaves them undefined).
void Z80CpuReset(Z80CPU* cpu);

// Execute one instruction (or burn 4 T-states while halted). Returns the
// number of T-states consumed. HALT burns 4 T per call (plus any waits the
// contention hook inserts for that M1 quantum) and advances R, matching the
// core's 1-T-per-iteration loop averaged over 4 iterations.
int Z80CpuStep(Z80CPU* cpu);

// Direct step entry for hot host loops: Z80CpuStep is a wrapper around the
// active bus-mode variant; calling the variant directly saves one call and
// one indirect jump per instruction (~10% of core time in a frame loop).
// The entry changes whenever the bus mode changes (Z80CpuAttachMemory /
// Z80CpuAttachPageTables), so re-fetch it after those calls. Unlike
// Z80CpuStep it returns the T-state counter AFTER the instruction (the same
// value Z80CpuTstates() then reports, 32-bit wrap); a frame loop that works
// in absolute T needs no subtraction, one that wants the delta subtracts
// the previous value. Executes exactly one instruction, like Z80CpuStep.
typedef int (*Z80CpuStepFn)(Z80CPU* cpu);
Z80CpuStepFn Z80CpuStepEntry(const Z80CPU* cpu);

// Maskable interrupt request, to be raised by the host at an instruction
// boundary. Returns the T-states consumed (13 in IM0/IM1, 19 in IM2) if
// accepted, or 0 if rejected (IFF1=0, or the instruction right after EI).
// On acceptance: pushes PC, IFF1=IFF2=0, PC=0x38 (IM0/IM1) or the IM2
// vector-table target, MEMPTR=target, HALT released.
int Z80CpuInt(Z80CPU* cpu);

// Non-maskable interrupt request at an instruction boundary. Always accepted.
// 11 T-states (plus any contention waits of the two stack writes; the
// return value is the total): push PC, PC=0x0066, MEMPTR=0x0066, IFF2=IFF1, IFF1=0,
// HALT released. RETN (or IFF1 restore) ends the NMI session flag.
int Z80CpuNmi(Z80CPU* cpu);

// Non-zero while the CPU is halted (HALT executed, no INT/NMI accepted yet).
int Z80CpuHalted(const Z80CPU* cpu);

// 1 if a maskable interrupt would currently be accepted (IFF1 set and the
// post-EI delay has passed).
int Z80CpuIntPossible(const Z80CPU* cpu);

// Total T-states since the last reset (also settable for snapshot restore).
uint32_t Z80CpuTstates(const Z80CPU* cpu);
void Z80CpuSetTstates(Z80CPU* cpu, uint32_t tstates);

// Address of the first byte (first prefix) of the instruction currently
// executing / last executed - stable from the M1 fetch until the next
// Z80CpuStep. Port decoders that gate on the instruction address (TR-DOS
// style ROM-only ports) read it from inside the port callbacks.
uint16_t Z80CpuInstructionPc(const Z80CPU* cpu);

uint16_t Z80CpuGetReg(const Z80CPU* cpu, Z80CpuReg reg);
void Z80CpuSetReg(Z80CPU* cpu, Z80CpuReg reg, uint16_t value);

// The whole register file in one call - for hosts that keep the CPU state in
// their own structures and move it in and out around every step (a debugger
// or snapshot code writes those directly). Equivalent to Z80CpuGetReg/
// Z80CpuSetReg for every selector, with the same value conventions (r = low
// 7 bits of the refresh counter | R7; set masks iff1/iff2 to bit 0, q to
// bits 3/5, clamps im to 2), but without one call and one selector dispatch
// per register on the host's hot path.
typedef struct Z80CpuRegisters
{
    uint16_t af, bc, de, hl;
    uint16_t afAlt, bcAlt, deAlt, hlAlt;
    uint16_t ix, iy, pc, sp;
    uint16_t memptr;
    uint8_t i, r, im;
    uint8_t iff1, iff2;
    uint8_t q;
    uint8_t halted;
} Z80CpuRegisters;

void Z80CpuGetRegisters(const Z80CPU* cpu, Z80CpuRegisters* regs);
void Z80CpuSetRegisters(Z80CPU* cpu, const Z80CpuRegisters* regs);

// Bus wiring. Any callback may be null: memory reads return 0xFF, writes and
// port writes are discarded, port reads return 0xFF, INT vector = 0xFF
// (null pointers are replaced by internal stubs, so the bus primitives
// never test for null).
void Z80CpuSetMemoryBus(Z80CPU* cpu, Z80CpuMemReadFn readFn, void* readData,
                        Z80CpuMemWriteFn writeFn, void* writeData);
void Z80CpuSetPortBus(Z80CPU* cpu, Z80CpuPortInFn inFn, void* inData,
                      Z80CpuPortOutFn outFn, void* outData);
void Z80CpuSetIntVectorFn(Z80CPU* cpu, Z80CpuIntVectorFn fn, void* userData);
void Z80CpuSetRetiFn(Z80CPU* cpu, Z80CpuRetiFn fn, void* userData);
void Z80CpuSetRetnFn(Z80CPU* cpu, Z80CpuRetnFn fn, void* userData);
void Z80CpuSetContendFn(Z80CPU* cpu, Z80CpuContendFn fn, void* userData);

// Paged-memory bus: the host owns two tables of Z80CPU_PAGE_COUNT pointers
// to Z80CPU_PAGE_SIZE-byte blocks (read side / write side, indexed by the
// address's top bits). The library keeps a rebased copy of the entries
// (so the hot path needs no offset mask): call Z80CpuSyncPageTables after
// changing any entry (bank switch, ROM paging) - one call per switch. A
// null entry routes that window through the memory callbacks with T
// published exactly as on the callback bus (use it for screen RAM, ROM
// write protection, or pages under debugger access breakpoints). Direct
// pages fire the contention hook but not the callbacks. Pass null tables to
// detach. While a flat 64K block is attached (Z80CpuAttachMemory) it takes
// precedence.
// Page granularity: 16K by default (four entries, the unreal-ng bank shape).
// Build with -DZ80CPU_PAGE_SHIFT=13 (8K) or 12 (4K) to let the host keep
// the non-screen part of a 16K window direct (e.g. 5B00h-7FFFh: sysvars,
// stack, code) while the screen stays observed; the host then fills 2 or 4
// entries per 16K bank. Measured cost of 8K on Apple M1: -2% on the paged
// microbenchmark, neutral on frames (docs/perf-analysis-2026-09-24.md,
// round 4).
#ifndef Z80CPU_PAGE_SHIFT
#define Z80CPU_PAGE_SHIFT 14
#endif
#define Z80CPU_PAGE_SIZE (1 << Z80CPU_PAGE_SHIFT)
#define Z80CPU_PAGE_COUNT (0x10000 >> Z80CPU_PAGE_SHIFT)
void Z80CpuAttachPageTables(Z80CPU* cpu, uint8_t* const* readPages, uint8_t* const* writePages);
void Z80CpuSyncPageTables(Z80CPU* cpu);

// Flat-memory fast path: attach a 64K byte array. While attached, memory
// reads/writes bypass the callbacks entirely (pure inline array access) -
// the fastest execution mode, suitable for flat-RAM guests and benchmarks.
// Pass null to detach and fall back to the callbacks.
// The buffer must stay valid and unmodified in size while attached.
void Z80CpuAttachMemory(Z80CPU* cpu, uint8_t* memory64k);

// Value written by the undocumented OUT (C),0 (ED 71). Real NMOS Z80s write
// 0; the core exposes this because some clones write 0xFF. Default: 0.
void Z80CpuSetOutC0Value(Z80CPU* cpu, uint8_t value);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // Z80CPU_H
