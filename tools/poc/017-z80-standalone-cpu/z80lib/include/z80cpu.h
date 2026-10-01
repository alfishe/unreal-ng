// z80cpu.h - public C API of the standalone Z80 CPU core (PoC 017).
//
// Extraction of the emulator core's Z80 CPU (core/src/emulator/cpu/*) into a
// self-contained, dependency-free library, structured after the z80ex library
// (scratch/z80ex-dl/z80ex.h): opaque CPU context, host-supplied bus callbacks,
// one-instruction-per-call stepping, host-driven INT/NMI, register accessors.
//
// Differences from z80ex, by design:
//  - full undocumented behavior inherited from the core: MEMPTR (WZ), the Q
//    register (Zilog SCF/CCF XCF flavor), IXH/IXL/IYH/IYL, SLL, IN (C),
//    OUT (C),0, X/Y flags (F3/F5) everywhere
//  - optional flat-memory fast path (Z80CpuAttachMemory) that bypasses the
//    read/write callbacks with inline 64K array accesses
//  - no per-T-state callback (contention is emulated host-side by inspecting
//    Z80CpuTstates() from inside the bus callbacks)
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
// time. m1State is non-zero when the read is an opcode fetch (M1 active).
typedef uint8_t (*Z80CpuMemReadFn)(Z80CPU* cpu, uint16_t addr, int m1State, void* userData);
typedef void (*Z80CpuMemWriteFn)(Z80CPU* cpu, uint16_t addr, uint8_t value, void* userData);
typedef uint8_t (*Z80CpuPortInFn)(Z80CPU* cpu, uint16_t port, void* userData);
typedef void (*Z80CpuPortOutFn)(Z80CPU* cpu, uint16_t port, uint8_t value, void* userData);

// Supplies the interrupt vector byte during a maskable interrupt acknowledge
// (M1 + IORQ active). Only used in IM2; return 0xFF if no device drives the bus.
typedef uint8_t (*Z80CpuIntVectorFn)(Z80CPU* cpu, void* userData);

// Fired when a RETI instruction (ED 4D) completes - for PIO/CTC-style devices.
typedef void (*Z80CpuRetiFn)(Z80CPU* cpu, void* userData);

// Register selectors for Z80CpuGetReg/Z80CpuSetReg.
// R  = low 7 bits of the refresh counter | current R7
// R7 = bit 7 of the refresh counter only
// Memptr/Q expose the undocumented internal registers (see z80.h in the core).
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
    Z80CpuRegCount
} Z80CpuReg;

// Library version string, e.g. "0.1.0-poc".
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
// number of T-states consumed. HALT burns 4 T per call and advances R,
// matching the core's 1-T-per-iteration loop averaged over 4 iterations.
int Z80CpuStep(Z80CPU* cpu);

// Maskable interrupt request, to be raised by the host at an instruction
// boundary. Returns the T-states consumed (13 in IM0/IM1, 19 in IM2) if
// accepted, or 0 if rejected (IFF1=0, or the instruction right after EI).
// On acceptance: pushes PC, IFF1=IFF2=0, PC=0x38 (IM0/IM1) or the IM2
// vector-table target, MEMPTR=target, HALT released.
int Z80CpuInt(Z80CPU* cpu);

// Non-maskable interrupt request at an instruction boundary. Always accepted.
// 11 T-states: push PC, PC=0x0066, MEMPTR=0x0066, IFF2=IFF1, IFF1=0,
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

uint16_t Z80CpuGetReg(const Z80CPU* cpu, Z80CpuReg reg);
void Z80CpuSetReg(Z80CPU* cpu, Z80CpuReg reg, uint16_t value);

// Bus wiring. Any callback may be null: memory reads return 0xFF, writes and
// port writes are discarded, port reads return 0xFF, INT vector = 0xFF.
void Z80CpuSetMemoryBus(Z80CPU* cpu, Z80CpuMemReadFn readFn, void* readData,
                        Z80CpuMemWriteFn writeFn, void* writeData);
void Z80CpuSetPortBus(Z80CPU* cpu, Z80CpuPortInFn inFn, void* inData,
                      Z80CpuPortOutFn outFn, void* outData);
void Z80CpuSetIntVectorFn(Z80CPU* cpu, Z80CpuIntVectorFn fn, void* userData);
void Z80CpuSetRetiFn(Z80CPU* cpu, Z80CpuRetiFn fn, void* userData);

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
