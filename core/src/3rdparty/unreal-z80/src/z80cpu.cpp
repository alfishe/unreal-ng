// z80cpu.cpp - API implementation of the standalone Z80 core (unreal-z80).
//
// Step loop, Q-register maintenance and interrupt entry are ports of the
// corresponding logic in core/src/emulator/cpu/z80.cpp (Z80Step, Z80::Reset,
// Z80::ProcessInterrupts NMI branch, Z80::HandleINT), stripped of emulator
// context, debugger, contention and frame-loop concerns.

#include "z80cpu-internal.h"

#include <cstring>
#include "z80cpu-dispatch.h"

#include <cstdlib>
#include <new>

const char* Z80CpuVersion(void)
{
    return "0.3.0";
}

// Null-bus stubs: an unwired callback is a stub, never a null pointer, so
// the bus primitives carry no per-access null test (measured ~1-2% on the
// callback bus). Semantics unchanged: reads 0xFF, writes discarded, INT
// vector 0xFF.
static uint8_t NullMemRead(Z80CPU*, uint16_t, int, void*) { return 0xFF; }
static void NullMemWrite(Z80CPU*, uint16_t, uint8_t, void*) {}
static uint8_t NullPortIn(Z80CPU*, uint16_t, void*) { return 0xFF; }
static void NullPortOut(Z80CPU*, uint16_t, uint8_t, void*) {}
static uint8_t NullIntVector(Z80CPU*, void*) { return 0xFF; }

// DDCB/FDCB destination registers: b,c,d,e,h,l,<trash>,a - pointers into
// the active register file, rebound whenever it changes.
static void BindDirectRegisters(Z80CPU* cpu)
{
    cpu->directRegisters[0] = &Z80R(cpu).b;
    cpu->directRegisters[1] = &Z80R(cpu).c;
    cpu->directRegisters[2] = &Z80R(cpu).d;
    cpu->directRegisters[3] = &Z80R(cpu).e;
    cpu->directRegisters[4] = &Z80R(cpu).h;
    cpu->directRegisters[5] = &Z80R(cpu).l;
    cpu->directRegisters[6] = &cpu->trashRegister;
    cpu->directRegisters[7] = &Z80R(cpu).a;
}

Z80CPU* Z80CpuCreate(void)
{
    Z80TablesInit();

    Z80CPU* cpu = new (std::nothrow) Z80CPU{};
    if (!cpu)
        return nullptr;

    Z80CpuSetMemoryBus(cpu, nullptr, nullptr, nullptr, nullptr);
    Z80CpuSetPortBus(cpu, nullptr, nullptr, nullptr, nullptr);
    Z80CpuSetIntVectorFn(cpu, nullptr, nullptr);

    cpu->regs = &cpu->ownedRegs;
    BindDirectRegisters(cpu);

    Z80CpuReset(cpu);

    // Default integration: callback bus (an unwired bus reads 0xFF, matching
    // the pre-split generic path). Z80CpuAttachMemory switches to the flat
    // dispatch set.
    cpu->stepFn = &Z80Cb::Step;
    return cpu;
}

void Z80CpuDestroy(Z80CPU* cpu)
{
    delete cpu;
}

// Port of Z80::Reset(): deterministic post-reset state (general registers
// cleared - a real chip leaves them undefined).
void Z80CpuReset(Z80CPU* cpu)
{
    Z80R(cpu).nmi_in_progress = false;

    cpu->t = 3;  // the reset sequence itself costs 3 T-states (core parity)

    Z80R(cpu).int_flags = 0;  // IM0, IFF1=IFF2=0, not halted (also clears r_hi)
    Z80R(cpu).ir_ = 0;        // I = R = 0
    Z80R(cpu).pc = 0x0000;
    Z80R(cpu).im = 0;
    Z80R(cpu).sp = 0xFFFF;  // real chip behavior
    Z80R(cpu).af = 0xFFFF;  // real chip behavior
    Z80R(cpu).q = 0;

    Z80R(cpu).bc = 0;
    Z80R(cpu).de = 0;
    Z80R(cpu).hl = 0;
    Z80R(cpu).ix = 0;
    Z80R(cpu).iy = 0;

    Z80R(cpu).alt.af = 0;
    Z80R(cpu).alt.bc = 0;
    Z80R(cpu).alt.de = 0;
    Z80R(cpu).alt.hl = 0;

    Z80R(cpu).memptr = 0;

    cpu->opword = 0;
    cpu->halt_cycle = 0;
    cpu->m1pc = 0;

    // Bus wiring intentionally survives a reset.
}

int Z80CpuHalted(const Z80CPU* cpu)
{
    return Z80R(cpu).halted ? 1 : 0;
}

// Post-EI shadow derived from the dispatched opcode: cpu->opcode holds the
// last executed instruction's opcode from step end until the next step, and
// EI is exactly opcode 0xFB, so a maskable interrupt becomes acceptable only
// once the instruction following EI has run. (Replaces the pre-split per-step
// intSuppress store; acceptance itself implies a non-EI last opcode, so no
// clearing is needed on INT/NMI entry.)
int Z80CpuIntPossible(const Z80CPU* cpu)
{
    if (!Z80R(cpu).iff1)
        return 0;
    // EI is 0xFB unprefixed or behind a DD/FD prefix (prefix byte 0); CB FB
    // (SET 7,E) and ED FB (NOP) share the byte but must not shadow the next
    // boundary.
    const bool lastWasEi = cpu->opword == 0x00FB;
    return lastWasEi ? 0 : 1;
}

uint32_t Z80CpuTstates(const Z80CPU* cpu)
{
    return cpu->t;
}

void Z80CpuSetTstates(Z80CPU* cpu, uint32_t tstates)
{
    cpu->t = tstates;
}

// One instruction: tail-jumps through the bus-mode entry (see
// z80cpu-dispatch.h). The mode functions live in opcodes-flat.cpp /
// opcodes-callback.cpp and share the step body in z80step.inc; the stable
// indirect target predicts perfectly (a load-and-test dispatch on cpu->mem
// measured consistently slower).
int Z80CpuStep(Z80CPU* cpu)
{
    const uint32_t t0 = cpu->t;
    return static_cast<int>(static_cast<uint32_t>(cpu->stepFn(cpu)) - t0);
}

Z80CpuStepFn Z80CpuStepEntry(const Z80CPU* cpu)
{
    return cpu->stepFn;
}

// Memory cycles of the interrupt acknowledge sequences (stack push, IM2
// vector-table read). Each is a regular 3 T bus cycle: the contention hook
// fires at its first T (callback/paged buses only - the flat bus never
// fires it, as for instructions), its waits are inserted before the cycle,
// and cpu->t is published before the memory callback so the host observes
// the exact access T - the same contract as an instruction's Z80Rd/Z80Wd.
// tact is the running T accumulator of the acknowledge; returns the T after
// the cycle.
static int Z80CpuAckWrite(Z80CPU* cpu, uint16_t addr, uint8_t val, int tact)
{
    if (!cpu->mem)
        Z80ContendT(cpu, addr, Z80CpuAccessWrite, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    cpu->RawWrite(addr, val);
    return tact;
}

static uint8_t Z80CpuAckRead(Z80CPU* cpu, uint16_t addr, int& tact)
{
    if (!cpu->mem)
        Z80ContendT(cpu, addr, Z80CpuAccessRead, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    return cpu->RawRead(addr);
}

// Push PC (M2 = PCH at SP-1, M3 = PCL at SP-2) through the acknowledge
// memory cycles; tact enters at the first T of M2 and leaves after M3.
static int Z80CpuPushPc(Z80CPU* cpu, int tact)
{
    uint16_t sp = Z80R(cpu).sp;
    tact = Z80CpuAckWrite(cpu, --sp, Z80R(cpu).pch, tact);
    tact = Z80CpuAckWrite(cpu, --sp, Z80R(cpu).pcl, tact);
    Z80R(cpu).sp = sp;
    return tact;
}

// Port of the NMI branch of Z80::ProcessInterrupts(): 11 T total
// (M1=5T restart fetch, M2/M3=3T+3T push).
int Z80CpuNmi(Z80CPU* cpu)
{
    Z80R(cpu).nmi_in_progress = true;

    // If halted: unblock by moving PC past the HALT (return lands after it).
    // Keyed on the HALT latch, not on the byte at PC: an interrupt taken at
    // the boundary before a not-yet-executed HALT must return to it.
    if (Z80R(cpu).halted)
        Z80R(cpu).pc++;

    // The acknowledge starts with an M1 cycle (opcode fetch, ignored) that
    // performs a refresh like any M1: R increases (FUSE, z80ex, silicon).
    Z80_R_INC(cpu->regs);

    // M1 = 5 T restart fetch, then the two 3 T push cycles (11 T + waits)
    const int t0 = static_cast<int>(cpu->t);
    const int tact = Z80CpuPushPc(cpu, t0 + 5);
    cpu->t = static_cast<uint32_t>(tact);

    Z80R(cpu).pc = 0x0066;
    Z80R(cpu).memptr = 0x0066;
    Z80R(cpu).halted = 0;
    cpu->halt_cycle = 0;
    Z80R(cpu).q = 0;  // the acknowledge cycle writes no flags

    // IFF2 keeps a copy of IFF1 for RETN; maskable ints disabled in handler
    Z80R(cpu).iff2 = Z80R(cpu).iff1;
    Z80R(cpu).iff1 = 0;

    return tact - t0;
}

// Port of Z80::HandleINT() preceded by the acceptance checks of
// ProcessInterrupts(): IM0/IM1 = 13 T, IM2 = 19 T.
int Z80CpuInt(Z80CPU* cpu)
{
    if (!Z80CpuIntPossible(cpu))
        return 0;

    // If halted: unblock by moving PC past the HALT (see Z80CpuNmi)
    if (Z80R(cpu).halted)
        Z80R(cpu).pc++;

    // Real-chip quirk: LD A,I / LD A,R (ED 57 / ED 5F) copy IFF2 into P/V
    // late in the instruction, and an INT accepted at the very next boundary
    // clears IFF2 before that copy settles - P/V then reads 0 (interrupts
    // "were disabled"). Only these two instructions expose IFF2 in F.
    if (cpu->opword == 0xED57 || cpu->opword == 0xED5F)
        Z80R(cpu).f &= ~PV;

    // The acknowledge cycle is an M1 cycle with refresh: R increases in
    // every mode (FUSE, z80ex, silicon).
    Z80_R_INC(cpu->regs);

    uint16_t handlerAddress;
    const int t0 = static_cast<int>(cpu->t);
    int tact;

    if (Z80R(cpu).im < 2)
    {
        // IM0/IM1 restart at 0x38. (IM0's bus-byte execution is not modeled,
        // matching the core: im < 2 -> 0x38.)
        // M1 = 7 T ack, M2/M3 = 3 T + 3 T push: 13 T + waits
        tact = Z80CpuPushPc(cpu, t0 + 7);
        handlerAddress = 0x38;
    }
    else
    {
        // IM2 in machine-cycle order: vector byte from the data bus during
        // the acknowledge (M1), push PC (M2/M3), then read the handler address
        // from the vector table (M4/M5) - so a stack that overlaps the table
        // supplies the freshly pushed bytes, as on the chip: 19 T + waits.
        const uint8_t vector = cpu->intVector(cpu, cpu->intVectorData);
        tact = Z80CpuPushPc(cpu, t0 + 7);
        const uint16_t vectorAddress = static_cast<uint16_t>(vector + Z80R(cpu).i * 0x100);
        const uint8_t low = Z80CpuAckRead(cpu, vectorAddress, tact);
        const uint8_t high = Z80CpuAckRead(cpu, static_cast<uint16_t>(vectorAddress + 1), tact);
        handlerAddress = static_cast<uint16_t>(low + 0x100 * high);
    }
    cpu->t = static_cast<uint32_t>(tact);
    const int duration = tact - t0;

    Z80R(cpu).pc = handlerAddress;
    Z80R(cpu).memptr = handlerAddress;
    Z80R(cpu).halted = 0;
    cpu->halt_cycle = 0;
    Z80R(cpu).q = 0;  // the acknowledge cycle writes no flags

    // No double acceptance until EI
    Z80R(cpu).iff1 = 0;
    Z80R(cpu).iff2 = 0;

    return duration;
}

uint16_t Z80CpuGetReg(const Z80CPU* cpu, Z80CpuReg reg)
{
    switch (reg)
    {
        case Z80CpuRegAf: return Z80R(cpu).af;
        case Z80CpuRegBc: return Z80R(cpu).bc;
        case Z80CpuRegDe: return Z80R(cpu).de;
        case Z80CpuRegHl: return Z80R(cpu).hl;
        case Z80CpuRegAfAlt: return Z80R(cpu).alt.af;
        case Z80CpuRegBcAlt: return Z80R(cpu).alt.bc;
        case Z80CpuRegDeAlt: return Z80R(cpu).alt.de;
        case Z80CpuRegHlAlt: return Z80R(cpu).alt.hl;
        case Z80CpuRegIx: return Z80R(cpu).ix;
        case Z80CpuRegIy: return Z80R(cpu).iy;
        case Z80CpuRegPc: return Z80R(cpu).pc;
        case Z80CpuRegSp: return Z80R(cpu).sp;
        case Z80CpuRegI: return Z80R(cpu).i;
        case Z80CpuRegR: return (Z80R(cpu).r_low & 0x7F) | (Z80R(cpu).r_hi & 0x80);
        case Z80CpuRegR7: return (Z80R(cpu).r_hi & 0x80) >> 7;
        case Z80CpuRegIm: return Z80R(cpu).im;
        case Z80CpuRegIff1: return Z80R(cpu).iff1;
        case Z80CpuRegIff2: return Z80R(cpu).iff2;
        case Z80CpuRegMemptr: return Z80R(cpu).memptr;
        case Z80CpuRegQ: return Z80R(cpu).q;
        case Z80CpuRegHalted: return Z80R(cpu).halted ? 1 : 0;
        default: return 0;
    }
}

void Z80CpuSetReg(Z80CPU* cpu, Z80CpuReg reg, uint16_t value)
{
    switch (reg)
    {
        case Z80CpuRegAf: Z80R(cpu).af = value; break;
        case Z80CpuRegBc: Z80R(cpu).bc = value; break;
        case Z80CpuRegDe: Z80R(cpu).de = value; break;
        case Z80CpuRegHl: Z80R(cpu).hl = value; break;
        case Z80CpuRegAfAlt: Z80R(cpu).alt.af = value; break;
        case Z80CpuRegBcAlt: Z80R(cpu).alt.bc = value; break;
        case Z80CpuRegDeAlt: Z80R(cpu).alt.de = value; break;
        case Z80CpuRegHlAlt: Z80R(cpu).alt.hl = value; break;
        case Z80CpuRegIx: Z80R(cpu).ix = value; break;
        case Z80CpuRegIy: Z80R(cpu).iy = value; break;
        case Z80CpuRegPc: Z80R(cpu).pc = value; break;
        case Z80CpuRegSp: Z80R(cpu).sp = value; break;
        case Z80CpuRegI: Z80R(cpu).i = static_cast<uint8_t>(value); break;
        case Z80CpuRegR:
            Z80R(cpu).r_low = static_cast<uint8_t>(value);  // as LD R,A
            Z80R(cpu).r_hi = value & 0x80;
            break;
        case Z80CpuRegR7: Z80R(cpu).r_hi = static_cast<uint8_t>(value & 0x80); break;
        case Z80CpuRegIm: Z80R(cpu).im = static_cast<uint8_t>(value > 2 ? 2 : value); break;
        case Z80CpuRegIff1: Z80R(cpu).iff1 = static_cast<uint8_t>(value & 1); break;
        case Z80CpuRegIff2: Z80R(cpu).iff2 = static_cast<uint8_t>(value & 1); break;
        case Z80CpuRegMemptr: Z80R(cpu).memptr = value; break;
        case Z80CpuRegQ: Z80R(cpu).q = static_cast<uint8_t>(value & 0x28); break;
        case Z80CpuRegHalted: Z80R(cpu).halted = static_cast<uint8_t>(value ? 1 : 0); break;
        default: break;
    }
}

void Z80CpuGetRegisters(const Z80CPU* cpu, Z80CpuRegisters* regs)
{
    regs->af = Z80R(cpu).af;
    regs->bc = Z80R(cpu).bc;
    regs->de = Z80R(cpu).de;
    regs->hl = Z80R(cpu).hl;
    regs->afAlt = Z80R(cpu).alt.af;
    regs->bcAlt = Z80R(cpu).alt.bc;
    regs->deAlt = Z80R(cpu).alt.de;
    regs->hlAlt = Z80R(cpu).alt.hl;
    regs->ix = Z80R(cpu).ix;
    regs->iy = Z80R(cpu).iy;
    regs->pc = Z80R(cpu).pc;
    regs->sp = Z80R(cpu).sp;
    regs->memptr = Z80R(cpu).memptr;
    regs->i = Z80R(cpu).i;
    regs->r = static_cast<uint8_t>((Z80R(cpu).r_low & 0x7F) | (Z80R(cpu).r_hi & 0x80));
    regs->im = Z80R(cpu).im;
    regs->iff1 = Z80R(cpu).iff1;
    regs->iff2 = Z80R(cpu).iff2;
    regs->q = Z80R(cpu).q;
    regs->halted = Z80R(cpu).halted ? 1 : 0;
}

void Z80CpuSetRegisters(Z80CPU* cpu, const Z80CpuRegisters* regs)
{
    Z80R(cpu).af = regs->af;
    Z80R(cpu).bc = regs->bc;
    Z80R(cpu).de = regs->de;
    Z80R(cpu).hl = regs->hl;
    Z80R(cpu).alt.af = regs->afAlt;
    Z80R(cpu).alt.bc = regs->bcAlt;
    Z80R(cpu).alt.de = regs->deAlt;
    Z80R(cpu).alt.hl = regs->hlAlt;
    Z80R(cpu).ix = regs->ix;
    Z80R(cpu).iy = regs->iy;
    Z80R(cpu).pc = regs->pc;
    Z80R(cpu).sp = regs->sp;
    Z80R(cpu).memptr = regs->memptr;
    Z80R(cpu).i = regs->i;
    Z80R(cpu).r_low = regs->r;  // as LD R,A: the full byte, R7 also in r_hi
    Z80R(cpu).r_hi = static_cast<uint8_t>(regs->r & 0x80);
    Z80R(cpu).im = static_cast<uint8_t>(regs->im > 2 ? 2 : regs->im);
    Z80R(cpu).iff1 = static_cast<uint8_t>(regs->iff1 & 1);
    Z80R(cpu).iff2 = static_cast<uint8_t>(regs->iff2 & 1);
    Z80R(cpu).q = static_cast<uint8_t>(regs->q & 0x28);
    Z80R(cpu).halted = regs->halted ? 1 : 0;
}

void Z80CpuSetMemoryBus(Z80CPU* cpu, Z80CpuMemReadFn readFn, void* readData,
                        Z80CpuMemWriteFn writeFn, void* writeData)
{
    cpu->memRead = readFn ? readFn : NullMemRead;
    cpu->memReadData = readData;
    cpu->memWrite = writeFn ? writeFn : NullMemWrite;
    cpu->memWriteData = writeData;
}

void Z80CpuSetPortBus(Z80CPU* cpu, Z80CpuPortInFn inFn, void* inData,
                      Z80CpuPortOutFn outFn, void* outData)
{
    cpu->portIn = inFn ? inFn : NullPortIn;
    cpu->portInData = inData;
    cpu->portOut = outFn ? outFn : NullPortOut;
    cpu->portOutData = outData;
}

void Z80CpuSetIntVectorFn(Z80CPU* cpu, Z80CpuIntVectorFn fn, void* userData)
{
    cpu->intVector = fn ? fn : NullIntVector;
    cpu->intVectorData = userData;
}

void Z80CpuSetRetiFn(Z80CPU* cpu, Z80CpuRetiFn fn, void* userData)
{
    cpu->retiCallback = fn;
    cpu->retiData = userData;
}

void Z80CpuSetRetnFn(Z80CPU* cpu, Z80CpuRetnFn fn, void* userData)
{
    cpu->retnCallback = fn;
    cpu->retnData = userData;
}

// Bus-mode selection: flat block > page tables > host callbacks.
static void Z80CpuSelectStep(Z80CPU* cpu)
{
    if (cpu->mem)
        cpu->stepFn = &Z80Flat::Step;
    else if (cpu->pageRead && cpu->pageWrite)
        cpu->stepFn = &Z80Paged::Step;
    else
        cpu->stepFn = &Z80Cb::Step;
}

void Z80CpuAttachMemory(Z80CPU* cpu, uint8_t* memory64k)
{
    cpu->mem = memory64k;
    Z80CpuSelectStep(cpu);
}

void Z80CpuAttachPageTables(Z80CPU* cpu, uint8_t* const* readPages, uint8_t* const* writePages)
{
    cpu->pageRead = readPages;
    cpu->pageWrite = writePages;
    Z80CpuSyncPageTables(cpu);
    Z80CpuSelectStep(cpu);
}

void Z80CpuSyncPageTables(Z80CPU* cpu)
{
    for (int i = 0; i < Z80CPU_PAGE_COUNT; ++i)
    {
        uint8_t* r = cpu->pageRead ? cpu->pageRead[i] : nullptr;
        uint8_t* w = cpu->pageWrite ? cpu->pageWrite[i] : nullptr;
        cpu->pageReadRebased[i] = r ? r - i * Z80CPU_PAGE_SIZE : nullptr;
        cpu->pageWriteRebased[i] = w ? w - i * Z80CPU_PAGE_SIZE : nullptr;
    }
}

void Z80CpuSetContendFn(Z80CPU* cpu, Z80CpuContendFn fn, void* userData)
{
    cpu->contend = fn;
    cpu->contendData = userData;
}

// Out-of-line half of the contention hook (see Z80ContendT): publishes the
// cycle-start T so the host's lookup sees it, then returns the waits.
int Z80ContendSlow(Z80CPU* cpu, uint16_t addr, Z80CpuAccessKind kind, int cycleStart)
{
    cpu->t = static_cast<uint32_t>(cycleStart);
    return cpu->contend(cpu, addr, kind, cpu->contendData);
}

uint16_t Z80CpuInstructionPc(const Z80CPU* cpu)
{
    return cpu->m1pc;
}

void Z80CpuSetOutC0Value(Z80CPU* cpu, uint8_t value)
{
    cpu->outc0 = value;
}

// ---- zero-copy register file ----------------------------------------------

// The engine's internal view (anonymous 8/16-bit unions) and the public
// plain-field struct describe the same 41 bytes.
static_assert(sizeof(Z80Regs) == sizeof(Z80CpuRegisterFile), "register file size");
static_assert(offsetof(Z80Regs, pc) == offsetof(Z80CpuRegisterFile, pc), "pc");
static_assert(offsetof(Z80Regs, sp) == offsetof(Z80CpuRegisterFile, sp), "sp");
static_assert(offsetof(Z80Regs, ir_) == offsetof(Z80CpuRegisterFile, rLow), "r_low");
static_assert(offsetof(Z80Regs, int_flags) == offsetof(Z80CpuRegisterFile, rHi), "r_hi");
static_assert(offsetof(Z80Regs, bc) == offsetof(Z80CpuRegisterFile, bc), "bc");
static_assert(offsetof(Z80Regs, de) == offsetof(Z80CpuRegisterFile, de), "de");
static_assert(offsetof(Z80Regs, hl) == offsetof(Z80CpuRegisterFile, hl), "hl");
static_assert(offsetof(Z80Regs, af) == offsetof(Z80CpuRegisterFile, af), "af");
static_assert(offsetof(Z80Regs, ix) == offsetof(Z80CpuRegisterFile, ix), "ix");
static_assert(offsetof(Z80Regs, iy) == offsetof(Z80CpuRegisterFile, iy), "iy");
static_assert(offsetof(Z80Regs, alt) == offsetof(Z80CpuRegisterFile, bcAlt), "alt");
static_assert(offsetof(Z80Regs, memptr) == offsetof(Z80CpuRegisterFile, memptr), "memptr");
static_assert(offsetof(Z80Regs, q) == offsetof(Z80CpuRegisterFile, q), "q");
static_assert(offsetof(Z80Regs, reservedEipos) == offsetof(Z80CpuRegisterFile, reservedEipos), "eipos");
static_assert(offsetof(Z80Regs, reservedHaltpos) == offsetof(Z80CpuRegisterFile, reservedHaltpos), "haltpos");
static_assert(offsetof(Z80Regs, im) == offsetof(Z80CpuRegisterFile, im), "im");
static_assert(offsetof(Z80Regs, nmi_in_progress) == offsetof(Z80CpuRegisterFile, nmiInProgress), "nmi");
static_assert(sizeof(bool) == 1, "nmi_in_progress is one byte");

void Z80CpuAttachRegisterFile(Z80CPU* cpu, Z80CpuRegisterFile* file)
{
    if (file)
    {
        cpu->regs = reinterpret_cast<Z80Regs*>(file);
    }
    else
    {
        if (cpu->regs != &cpu->ownedRegs)
            std::memcpy(&cpu->ownedRegs, cpu->regs, sizeof(Z80Regs));
        cpu->regs = &cpu->ownedRegs;
    }
    BindDirectRegisters(cpu);
}

Z80CpuRegisterFile* Z80CpuRegisterFilePtr(Z80CPU* cpu)
{
    return reinterpret_cast<Z80CpuRegisterFile*>(cpu->regs);
}
