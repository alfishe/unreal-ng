// z80ncpu.cpp - API implementation of the Z80N core (fork of the z84c15 core).
//
// Step loop, Q-register maintenance and interrupt entry are ports of the
// corresponding logic in core/src/emulator/cpu/z80.cpp (Z80Step, Z80::Reset,
// Z80::ProcessInterrupts NMI branch, Z80::HandleINT), stripped of emulator
// context, debugger, contention and frame-loop concerns.

#include "z80ncpu-internal.h"

#include <cstring>
#include "z80ncpu-dispatch.h"

#include <cstdlib>
#include <new>

const char* Z80nCpuVersion(void)
{
    return "0.5.0-z80n.1";
}

// Null-bus stubs: an unwired callback is a stub, never a null pointer, so
// the bus primitives carry no per-access null test (measured ~1-2% on the
// callback bus). Semantics unchanged: reads 0xFF, writes discarded, INT
// vector 0xFF.
static uint8_t NullMemRead(Z80nCPU*, uint16_t, Z80nCpuAccessKind, void*) { return 0xFF; }
static void NullMemWrite(Z80nCPU*, uint16_t, uint8_t, void*) {}
static uint8_t NullPortIn(Z80nCPU*, uint16_t, void*) { return 0xFF; }
static void NullPortOut(Z80nCPU*, uint16_t, uint8_t, void*) {}
static uint8_t NullIntVector(Z80nCPU*, void*) { return 0xFF; }

// DDCB/FDCB destination registers: b,c,d,e,h,l,<trash>,a - pointers into
// the active register file, rebound whenever it changes.
static void BindDirectRegisters(Z80nCPU* cpu)
{
    cpu->directRegisters[0] = &Z80nR(cpu).b;
    cpu->directRegisters[1] = &Z80nR(cpu).c;
    cpu->directRegisters[2] = &Z80nR(cpu).d;
    cpu->directRegisters[3] = &Z80nR(cpu).e;
    cpu->directRegisters[4] = &Z80nR(cpu).h;
    cpu->directRegisters[5] = &Z80nR(cpu).l;
    cpu->directRegisters[6] = &cpu->trashRegister;
    cpu->directRegisters[7] = &Z80nR(cpu).a;
}

Z80nCPU* Z80nCpuCreate(void)
{
    Z80nTablesInit();

    Z80nCPU* cpu = new (std::nothrow) Z80nCPU{};
    if (!cpu)
        return nullptr;

    Z80nCpuSetMemoryBus(cpu, nullptr, nullptr, nullptr, nullptr);
    Z80nCpuSetPortBus(cpu, nullptr, nullptr, nullptr, nullptr);
    Z80nCpuSetIntVectorFn(cpu, nullptr, nullptr);

    // NMOS baseline (unreal-z80): OUT (C),0 drives 0, the LD A,I quirk is on; the variant is switchable
    cpu->outc0 = 0x00;
    cpu->ldAirQuirk = 1;

    cpu->regs = &cpu->ownedRegs;
    BindDirectRegisters(cpu);

    Z80nCpuReset(cpu);

    // The callback bus, the only bus of this fork (an unwired bus reads 0xFF)
    cpu->stepFn = &Z80nCb::Step;
    return cpu;
}

void Z80nCpuDestroy(Z80nCPU* cpu)
{
    delete cpu;
}

// Port of Z80::Reset(): deterministic post-reset state (general registers
// cleared - a real chip leaves them undefined).
void Z80nCpuReset(Z80nCPU* cpu)
{
    Z80nR(cpu).nmi_in_progress = false;

    cpu->t = 3;  // the reset sequence itself costs 3 T-states (core parity)

    Z80nR(cpu).int_flags = 0;  // IM0, IFF1=IFF2=0, not halted (also clears r_hi)
    Z80nR(cpu).ir_ = 0;        // I = R = 0
    Z80nR(cpu).pc = 0x0000;
    Z80nR(cpu).im = 0;
    Z80nR(cpu).sp = 0xFFFF;  // real chip behavior
    Z80nR(cpu).af = 0xFFFF;  // real chip behavior
    Z80nR(cpu).q = 0;

    Z80nR(cpu).bc = 0;
    Z80nR(cpu).de = 0;
    Z80nR(cpu).hl = 0;
    Z80nR(cpu).ix = 0;
    Z80nR(cpu).iy = 0;

    Z80nR(cpu).alt.af = 0;
    Z80nR(cpu).alt.bc = 0;
    Z80nR(cpu).alt.de = 0;
    Z80nR(cpu).alt.hl = 0;

    Z80nR(cpu).memptr = 0;

    cpu->opword = 0;
    cpu->halt_cycle = 0;
    cpu->m1pc = 0;

    // Bus wiring intentionally survives a reset.
}

int Z80nCpuHalted(const Z80nCPU* cpu)
{
    return Z80nR(cpu).halted & 1;
}

// Boundary state derived from the dispatched opcode word: cpu->opword holds
// the last M1-fetched opcode and its prefix class from step end until the
// next step (acknowledges overwrite it). Exactly one of these, or none,
// describes the boundary (Z80nCpuBoundary):
//   0x00FB           INT shadow: EI ran (unprefixed or DD/FD FB; CB FB =
//                    SET 7,E and ED FB = NOP share the byte but not the
//                    shadow), or a RETN/RETI set IFF1 (ope_45/ope_4D store
//                    0x00FB then: IFF2 reaches IFF1 too late for this
//                    boundary's INT sampling - Weissflog 2021, Sainz de
//                    Baranda 2022). INT refused, NMI accepted
//   0x00DD / 0x00FD  with Z80N_PENDING_PREFIX in the halted byte: a redundant
//                    prefix ended the step, this prefix is fetched and its
//                    instruction not yet run: INT and NMI refused (see
//                    ddfd_prefixes); the flag is what Step tests, opword
//                    says which prefix
//   0xED57 / 0xED5F  LD A,I / LD A,R ran: an INT accepted here clears P/V
//   0xFF66           an NMI was just acknowledged: a second NMI is refused
//                    until an instruction has run (Sainz de Baranda 2022).
//                    The prefix byte FF never comes from an M1 fetch
static constexpr uint16_t kOpwordIntShadow = Z80N_OPWORD_INT_SHADOW;
static constexpr uint16_t kOpwordNmiAck = Z80N_OPWORD_NMI_ACK;

static bool Z80nCpuPrefixPending(const Z80nCPU* cpu)
{
    return (Z80nR(cpu).halted & Z80N_PENDING_PREFIX) != 0;
}

int Z80nCpuIntPossible(const Z80nCPU* cpu)
{
    if (!Z80nR(cpu).iff1)
        return 0;
    return (cpu->opword == kOpwordIntShadow || Z80nCpuPrefixPending(cpu)) ? 0 : 1;
}

static Z80nCpuBoundary Z80nCpuGetBoundary(const Z80nCPU* cpu)
{
    if (Z80nCpuPrefixPending(cpu))
        return cpu->opword == 0x00DD ? Z80nCpuBoundaryPrefixDd : Z80nCpuBoundaryPrefixFd;
    switch (cpu->opword)
    {
        case kOpwordIntShadow: return Z80nCpuBoundaryIntShadow;
        case 0xED57:
        case 0xED5F: return Z80nCpuBoundaryLdAIr;
        case kOpwordNmiAck: return Z80nCpuBoundaryNmiAck;
        default: return Z80nCpuBoundaryNone;
    }
}

// Inverse: the opword that reproduces the state, and the pending flag next
// to the HALT latch (bit 0 kept). Unknown values set None.
static void Z80nCpuSetBoundary(Z80nCPU* cpu, unsigned boundary)
{
    uint16_t opword = 0;
    switch (boundary)
    {
        case Z80nCpuBoundaryPrefixDd: opword = 0x00DD; break;
        case Z80nCpuBoundaryPrefixFd: opword = 0x00FD; break;
        case Z80nCpuBoundaryIntShadow: opword = kOpwordIntShadow; break;
        case Z80nCpuBoundaryLdAIr: opword = 0xED57; break;
        case Z80nCpuBoundaryNmiAck: opword = kOpwordNmiAck; break;
        default: break;
    }
    const bool pending = boundary == Z80nCpuBoundaryPrefixDd || boundary == Z80nCpuBoundaryPrefixFd;
    cpu->opword = opword;
    Z80nR(cpu).halted = static_cast<uint8_t>((Z80nR(cpu).halted & 1) | (pending ? Z80N_PENDING_PREFIX : 0));
}

uint32_t Z80nCpuTstates(const Z80nCPU* cpu)
{
    return cpu->t;
}

void Z80nCpuSetTstates(Z80nCPU* cpu, uint32_t tstates)
{
    cpu->t = tstates;
}

// One instruction: the callback bus's Step (opcodes-callback.cpp, step body
// in z80nstep.inc).
int Z80nCpuStep(Z80nCPU* cpu)
{
    const uint32_t t0 = cpu->t;
    return static_cast<int>(static_cast<uint32_t>(cpu->stepFn(cpu)) - t0);
}

Z80nCpuStepFn Z80nCpuStepEntry(const Z80nCPU* cpu)
{
    return cpu->stepFn;
}

// Memory cycles of the interrupt acknowledge sequences (stack push, IM2
// vector-table read). Each is a regular 3 T bus cycle: the contention hook
// fires at its first T, cpu->t is published before the memory callback so
// the host observes the exact access T, the clock is taken back after it
// (the host's external /WAIT) - the same contract as an instruction's Z80nRd/Z80nWd. tact is the running T
// accumulator of the acknowledge; returns the T after the cycle.
static int Z80nCpuAckWrite(Z80nCPU* cpu, uint16_t addr, uint8_t val, int tact)
{
    Z80nContendT(cpu, addr, Z80nCpuAccessWrite, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    cpu->memWrite(cpu, addr, val, cpu->memWriteData);
    tact = static_cast<int>(cpu->t);
    return tact;
}

static uint8_t Z80nCpuAckRead(Z80nCPU* cpu, uint16_t addr, int& tact)
{
    Z80nContendT(cpu, addr, Z80nCpuAccessRead, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->memRead(cpu, addr, Z80nCpuAccessRead, cpu->memReadData);
    tact = static_cast<int>(cpu->t);
    return value;
}

// Push PC (M2 = PCH at SP-1, M3 = PCL at SP-2) through the acknowledge
// memory cycles; tact enters at the first T of M2 and leaves after M3.
static int Z80nCpuPushPc(Z80nCPU* cpu, int tact)
{
    uint16_t sp = Z80nR(cpu).sp;
    tact = Z80nCpuAckWrite(cpu, --sp, Z80nR(cpu).pch, tact);
    tact = Z80nCpuAckWrite(cpu, --sp, Z80nR(cpu).pcl, tact);
    Z80nR(cpu).sp = sp;
    return tact;
}

// Port of the NMI branch of Z80::ProcessInterrupts(): 11 T total
// (M1=5T restart fetch, M2/M3=3T+3T push).
// The NMI's push in the stackless mode: two 3 T write cycles with the memory masked (no MREQ: no contention hook, no
// memory callback), SP down by 2, the bytes to the host's store.
static int Z80nCpuPushPcStackless(Z80nCPU* cpu, int tact)
{
    Z80nR(cpu).sp = static_cast<uint16_t>(Z80nR(cpu).sp - 2);
    cpu->nmiStore(cpu, Z80nR(cpu).pcl, Z80nR(cpu).pch, cpu->nmiData);
    cpu->stacklessRetn = 1;
    return tact + 6;
}

int Z80nCpuNmi(Z80nCPU* cpu)
{
    // Not between a prefix and its instruction (see ddfd_prefixes), and not
    // twice without an instruction between: the host retries at the next
    // boundary.
    if (Z80nCpuPrefixPending(cpu) || cpu->opword == kOpwordNmiAck)
        return 0;

    Z80nR(cpu).nmi_in_progress = true;

    // If halted: unblock by moving PC past the HALT (return lands after it).
    // Keyed on the HALT latch, not on the byte at PC: an interrupt taken at
    // the boundary before a not-yet-executed HALT must return to it.
    if (Z80nR(cpu).halted & 1)
        Z80nR(cpu).pc++;

    // The acknowledge starts with an M1 cycle (opcode fetch, ignored) that
    // performs a refresh like any M1: R increases (FUSE, z80ex, silicon).
    Z80N_R_INC(cpu->regs);

    // M1 = 5 T restart fetch (an opcode-fetch cycle whose byte is ignored: no
    // host read; the chip's M1 waits apply), then the two 3 T push cycles
    // (11 T + waits)
    const int t0 = static_cast<int>(cpu->t);
    const int restart = t0 + 5;
    const int tact = (cpu->stacklessNmi && cpu->nmiStore) ? Z80nCpuPushPcStackless(cpu, restart) : Z80nCpuPushPc(cpu, restart);
    cpu->t = static_cast<uint32_t>(tact);

    Z80nR(cpu).pc = 0x0066;
    Z80nR(cpu).memptr = 0x0066;
    Z80nR(cpu).halted = 0;
    cpu->halt_cycle = 0;
    Z80nR(cpu).q = 0;  // the acknowledge cycle writes no flags

    // Maskable ints disabled in the handler; IFF2 is left alone and keeps
    // the pre-NMI state for RETN (UM0080 table 1 "Accept NMI: IFF1 0, IFF2
    // unchanged"; Sean Young's nested-NMI hardware test: IFF1 is not copied
    // to IFF2 - so a nested NMI does not lose the outer state).
    Z80nR(cpu).iff1 = 0;
    cpu->opword = kOpwordNmiAck;

    return tact - t0;
}

// Port of Z80::HandleINT() preceded by the acceptance checks of
// ProcessInterrupts(): IM0/IM1 = 13 T, IM2 = 19 T.
int Z80nCpuInt(Z80nCPU* cpu)
{
    if (!Z80nCpuIntPossible(cpu))
        return 0;

    // If halted: unblock by moving PC past the HALT (see Z80nCpuNmi)
    if (Z80nR(cpu).halted & 1)
        Z80nR(cpu).pc++;

    // Real-chip quirk of the NMOS Z80: LD A,I / LD A,R (ED 57 / ED 5F) copy IFF2 into P/V late in the
    // instruction, and an INT accepted at the very next boundary clears IFF2 before that copy settles - P/V then
    // reads 0. A CMOS part keeps it (Z80nCpuSetLdAirQuirk(cpu, 0)).
    if (cpu->ldAirQuirk && (cpu->opword == 0xED57 || cpu->opword == 0xED5F))
        Z80nR(cpu).f &= ~PV;

    // The acknowledge cycle is an M1 cycle with refresh: R increases in
    // every mode (FUSE, z80ex, silicon).
    Z80N_R_INC(cpu->regs);

    uint16_t handlerAddress;
    const int t0 = static_cast<int>(cpu->t);
    int tact;

    // The INTA cycle: 7 T (the host's INT acknowledge logic may stretch it through the memory callbacks that follow)
    const int inta = 7;

    if (Z80nR(cpu).im < 2)
    {
        // IM0/IM1 restart at 0x38. (IM0's bus-byte execution is not modeled,
        // matching the core: im < 2 -> 0x38.)
        // M1 = 7 T ack, M2/M3 = 3 T + 3 T push: 13 T + waits
        tact = Z80nCpuPushPc(cpu, t0 + inta);
        handlerAddress = 0x38;
    }
    else
    {
        // IM2 in machine-cycle order: vector byte from the data bus during
        // the acknowledge (M1), push PC (M2/M3), then read the handler address
        // from the vector table (M4/M5) - so a stack that overlaps the table
        // supplies the freshly pushed bytes, as on the chip: 19 T + waits.
        const uint8_t vector = cpu->intVector(cpu, cpu->intVectorData);
        tact = Z80nCpuPushPc(cpu, t0 + inta);
        const uint16_t vectorAddress = static_cast<uint16_t>(vector + Z80nR(cpu).i * 0x100);
        const uint8_t low = Z80nCpuAckRead(cpu, vectorAddress, tact);
        const uint8_t high = Z80nCpuAckRead(cpu, static_cast<uint16_t>(vectorAddress + 1), tact);
        handlerAddress = static_cast<uint16_t>(low + 0x100 * high);
    }
    cpu->t = static_cast<uint32_t>(tact);
    const int duration = tact - t0;

    Z80nR(cpu).pc = handlerAddress;
    Z80nR(cpu).memptr = handlerAddress;
    Z80nR(cpu).halted = 0;
    cpu->halt_cycle = 0;
    Z80nR(cpu).q = 0;  // the acknowledge cycle writes no flags

    // No double acceptance until EI
    Z80nR(cpu).iff1 = 0;
    Z80nR(cpu).iff2 = 0;
    cpu->opword = 0;  // no boundary state after an acknowledge

    return duration;
}

uint16_t Z80nCpuGetReg(const Z80nCPU* cpu, Z80nCpuReg reg)
{
    switch (reg)
    {
        case Z80nCpuRegAf: return Z80nR(cpu).af;
        case Z80nCpuRegBc: return Z80nR(cpu).bc;
        case Z80nCpuRegDe: return Z80nR(cpu).de;
        case Z80nCpuRegHl: return Z80nR(cpu).hl;
        case Z80nCpuRegAfAlt: return Z80nR(cpu).alt.af;
        case Z80nCpuRegBcAlt: return Z80nR(cpu).alt.bc;
        case Z80nCpuRegDeAlt: return Z80nR(cpu).alt.de;
        case Z80nCpuRegHlAlt: return Z80nR(cpu).alt.hl;
        case Z80nCpuRegIx: return Z80nR(cpu).ix;
        case Z80nCpuRegIy: return Z80nR(cpu).iy;
        case Z80nCpuRegPc: return Z80nR(cpu).pc;
        case Z80nCpuRegSp: return Z80nR(cpu).sp;
        case Z80nCpuRegI: return Z80nR(cpu).i;
        case Z80nCpuRegR: return (Z80nR(cpu).r_low & 0x7F) | (Z80nR(cpu).r_hi & 0x80);
        case Z80nCpuRegR7: return (Z80nR(cpu).r_hi & 0x80) >> 7;
        case Z80nCpuRegIm: return Z80nR(cpu).im;
        case Z80nCpuRegIff1: return Z80nR(cpu).iff1;
        case Z80nCpuRegIff2: return Z80nR(cpu).iff2;
        case Z80nCpuRegMemptr: return Z80nR(cpu).memptr;
        case Z80nCpuRegQ: return Z80nR(cpu).q;
        case Z80nCpuRegHalted: return Z80nR(cpu).halted & 1;
        case Z80nCpuRegBoundary: return static_cast<uint16_t>(Z80nCpuGetBoundary(cpu));
        case Z80nCpuRegNmiInProgress: return Z80nR(cpu).nmi_in_progress ? 1 : 0;
        default: return 0;
    }
}

void Z80nCpuSetReg(Z80nCPU* cpu, Z80nCpuReg reg, uint16_t value)
{
    switch (reg)
    {
        case Z80nCpuRegAf: Z80nR(cpu).af = value; break;
        case Z80nCpuRegBc: Z80nR(cpu).bc = value; break;
        case Z80nCpuRegDe: Z80nR(cpu).de = value; break;
        case Z80nCpuRegHl: Z80nR(cpu).hl = value; break;
        case Z80nCpuRegAfAlt: Z80nR(cpu).alt.af = value; break;
        case Z80nCpuRegBcAlt: Z80nR(cpu).alt.bc = value; break;
        case Z80nCpuRegDeAlt: Z80nR(cpu).alt.de = value; break;
        case Z80nCpuRegHlAlt: Z80nR(cpu).alt.hl = value; break;
        case Z80nCpuRegIx: Z80nR(cpu).ix = value; break;
        case Z80nCpuRegIy: Z80nR(cpu).iy = value; break;
        case Z80nCpuRegPc: Z80nR(cpu).pc = value; break;
        case Z80nCpuRegSp: Z80nR(cpu).sp = value; break;
        case Z80nCpuRegI: Z80nR(cpu).i = static_cast<uint8_t>(value); break;
        case Z80nCpuRegR:
            Z80nR(cpu).r_low = static_cast<uint8_t>(value);  // as LD R,A
            Z80nR(cpu).r_hi = value & 0x80;
            break;
        case Z80nCpuRegR7: Z80nR(cpu).r_hi = static_cast<uint8_t>(value & 0x80); break;
        case Z80nCpuRegIm: Z80nR(cpu).im = static_cast<uint8_t>(value > 2 ? 2 : value); break;
        case Z80nCpuRegIff1: Z80nR(cpu).iff1 = static_cast<uint8_t>(value & 1); break;
        case Z80nCpuRegIff2: Z80nR(cpu).iff2 = static_cast<uint8_t>(value & 1); break;
        case Z80nCpuRegMemptr: Z80nR(cpu).memptr = value; break;
        case Z80nCpuRegQ: Z80nR(cpu).q = static_cast<uint8_t>(value & 0x28); break;
        case Z80nCpuRegHalted:
            Z80nR(cpu).halted = static_cast<uint8_t>((Z80nR(cpu).halted & Z80N_PENDING_PREFIX) | (value ? 1 : 0));
            break;
        case Z80nCpuRegBoundary: Z80nCpuSetBoundary(cpu, value); break;
        case Z80nCpuRegNmiInProgress: Z80nR(cpu).nmi_in_progress = value != 0; break;
        default: break;
    }
}

void Z80nCpuGetRegisters(const Z80nCPU* cpu, Z80nCpuRegisters* regs)
{
    regs->af = Z80nR(cpu).af;
    regs->bc = Z80nR(cpu).bc;
    regs->de = Z80nR(cpu).de;
    regs->hl = Z80nR(cpu).hl;
    regs->afAlt = Z80nR(cpu).alt.af;
    regs->bcAlt = Z80nR(cpu).alt.bc;
    regs->deAlt = Z80nR(cpu).alt.de;
    regs->hlAlt = Z80nR(cpu).alt.hl;
    regs->ix = Z80nR(cpu).ix;
    regs->iy = Z80nR(cpu).iy;
    regs->pc = Z80nR(cpu).pc;
    regs->sp = Z80nR(cpu).sp;
    regs->memptr = Z80nR(cpu).memptr;
    regs->i = Z80nR(cpu).i;
    regs->r = static_cast<uint8_t>((Z80nR(cpu).r_low & 0x7F) | (Z80nR(cpu).r_hi & 0x80));
    regs->im = Z80nR(cpu).im;
    regs->iff1 = Z80nR(cpu).iff1;
    regs->iff2 = Z80nR(cpu).iff2;
    regs->q = Z80nR(cpu).q;
    regs->halted = Z80nR(cpu).halted & 1;
    regs->boundary = static_cast<uint8_t>(Z80nCpuGetBoundary(cpu));
    regs->nmiInProgress = Z80nR(cpu).nmi_in_progress ? 1 : 0;
}

void Z80nCpuSetRegisters(Z80nCPU* cpu, const Z80nCpuRegisters* regs)
{
    Z80nR(cpu).af = regs->af;
    Z80nR(cpu).bc = regs->bc;
    Z80nR(cpu).de = regs->de;
    Z80nR(cpu).hl = regs->hl;
    Z80nR(cpu).alt.af = regs->afAlt;
    Z80nR(cpu).alt.bc = regs->bcAlt;
    Z80nR(cpu).alt.de = regs->deAlt;
    Z80nR(cpu).alt.hl = regs->hlAlt;
    Z80nR(cpu).ix = regs->ix;
    Z80nR(cpu).iy = regs->iy;
    Z80nR(cpu).pc = regs->pc;
    Z80nR(cpu).sp = regs->sp;
    Z80nR(cpu).memptr = regs->memptr;
    Z80nR(cpu).i = regs->i;
    Z80nR(cpu).r_low = regs->r;  // as LD R,A: the full byte, R7 also in r_hi
    Z80nR(cpu).r_hi = static_cast<uint8_t>(regs->r & 0x80);
    Z80nR(cpu).im = static_cast<uint8_t>(regs->im > 2 ? 2 : regs->im);
    Z80nR(cpu).iff1 = static_cast<uint8_t>(regs->iff1 & 1);
    Z80nR(cpu).iff2 = static_cast<uint8_t>(regs->iff2 & 1);
    Z80nR(cpu).q = static_cast<uint8_t>(regs->q & 0x28);
    Z80nR(cpu).halted = regs->halted ? 1 : 0;
    Z80nCpuSetBoundary(cpu, regs->boundary);
    Z80nR(cpu).nmi_in_progress = regs->nmiInProgress != 0;
}

void Z80nCpuSetMemoryBus(Z80nCPU* cpu, Z80nCpuMemReadFn readFn, void* readData,
                        Z80nCpuMemWriteFn writeFn, void* writeData)
{
    cpu->memRead = readFn ? readFn : NullMemRead;
    cpu->memReadData = readData;
    cpu->memWrite = writeFn ? writeFn : NullMemWrite;
    cpu->memWriteData = writeData;
}

void Z80nCpuSetPortBus(Z80nCPU* cpu, Z80nCpuPortInFn inFn, void* inData,
                      Z80nCpuPortOutFn outFn, void* outData)
{
    cpu->portIn = inFn ? inFn : NullPortIn;
    cpu->portInData = inData;
    cpu->portOut = outFn ? outFn : NullPortOut;
    cpu->portOutData = outData;
}

void Z80nCpuSetIntVectorFn(Z80nCPU* cpu, Z80nCpuIntVectorFn fn, void* userData)
{
    cpu->intVector = fn ? fn : NullIntVector;
    cpu->intVectorData = userData;
}

void Z80nCpuSetRetiFn(Z80nCPU* cpu, Z80nCpuRetiFn fn, void* userData)
{
    cpu->retiCallback = fn;
    cpu->retiData = userData;
}

void Z80nCpuSetRetnFn(Z80nCPU* cpu, Z80nCpuRetnFn fn, void* userData)
{
    cpu->retnCallback = fn;
    cpu->retnData = userData;
}

void Z80nCpuSetContendFn(Z80nCPU* cpu, Z80nCpuContendFn fn, void* userData)
{
    cpu->contend = fn;
    cpu->contendData = userData;
}

// Out-of-line half of the contention hook (see Z80nContendT): publishes the
// cycle-start T so the host's lookup sees it, then returns the waits.
int Z80nContendSlow(Z80nCPU* cpu, uint16_t addr, Z80nCpuAccessKind kind, int cycleStart)
{
    cpu->t = static_cast<uint32_t>(cycleStart);
    return cpu->contend(cpu, addr, kind, cpu->contendData);
}

uint16_t Z80nCpuInstructionPc(const Z80nCPU* cpu)
{
    return cpu->m1pc;
}

uint16_t Z80nCpuOpcodeWord(const Z80nCPU* cpu)
{
    return cpu->opword;
}

void Z80nCpuSetOutC0Value(Z80nCPU* cpu, uint8_t value)
{
    cpu->outc0 = value;
}

void Z80nCpuAddWaitStates(Z80nCPU* cpu, uint32_t tstates)
{
    cpu->t += tstates;
}

// ---- zero-copy register file ----------------------------------------------

// The engine's internal view (anonymous 8/16-bit unions) and the public
// plain-field struct describe the same 41 bytes.
static_assert(sizeof(Z80nRegs) == sizeof(Z80nCpuRegisterFile), "register file size");
static_assert(offsetof(Z80nRegs, pc) == offsetof(Z80nCpuRegisterFile, pc), "pc");
static_assert(offsetof(Z80nRegs, sp) == offsetof(Z80nCpuRegisterFile, sp), "sp");
static_assert(offsetof(Z80nRegs, ir_) == offsetof(Z80nCpuRegisterFile, rLow), "r_low");
static_assert(offsetof(Z80nRegs, int_flags) == offsetof(Z80nCpuRegisterFile, rHi), "r_hi");
static_assert(offsetof(Z80nRegs, bc) == offsetof(Z80nCpuRegisterFile, bc), "bc");
static_assert(offsetof(Z80nRegs, de) == offsetof(Z80nCpuRegisterFile, de), "de");
static_assert(offsetof(Z80nRegs, hl) == offsetof(Z80nCpuRegisterFile, hl), "hl");
static_assert(offsetof(Z80nRegs, af) == offsetof(Z80nCpuRegisterFile, af), "af");
static_assert(offsetof(Z80nRegs, ix) == offsetof(Z80nCpuRegisterFile, ix), "ix");
static_assert(offsetof(Z80nRegs, iy) == offsetof(Z80nCpuRegisterFile, iy), "iy");
static_assert(offsetof(Z80nRegs, alt) == offsetof(Z80nCpuRegisterFile, bcAlt), "alt");
static_assert(offsetof(Z80nRegs, memptr) == offsetof(Z80nCpuRegisterFile, memptr), "memptr");
static_assert(offsetof(Z80nRegs, q) == offsetof(Z80nCpuRegisterFile, q), "q");
static_assert(offsetof(Z80nRegs, reservedEipos) == offsetof(Z80nCpuRegisterFile, reservedEipos), "eipos");
static_assert(offsetof(Z80nRegs, reservedHaltpos) == offsetof(Z80nCpuRegisterFile, reservedHaltpos), "haltpos");
static_assert(offsetof(Z80nRegs, im) == offsetof(Z80nCpuRegisterFile, im), "im");
static_assert(offsetof(Z80nRegs, nmi_in_progress) == offsetof(Z80nCpuRegisterFile, nmiInProgress), "nmi");
static_assert(sizeof(bool) == 1, "nmi_in_progress is one byte");

void Z80nCpuAttachRegisterFile(Z80nCPU* cpu, Z80nCpuRegisterFile* file)
{
    if (file)
    {
        cpu->regs = reinterpret_cast<Z80nRegs*>(file);
    }
    else
    {
        if (cpu->regs != &cpu->ownedRegs)
            std::memcpy(&cpu->ownedRegs, cpu->regs, sizeof(Z80nRegs));
        cpu->regs = &cpu->ownedRegs;
    }
    BindDirectRegisters(cpu);
}

Z80nCpuRegisterFile* Z80nCpuRegisterFilePtr(Z80nCPU* cpu)
{
    return reinterpret_cast<Z80nCpuRegisterFile*>(cpu->regs);
}

void Z80nCpuSetLdAirQuirk(Z80nCPU* cpu, int enabled)
{
    cpu->ldAirQuirk = enabled ? 1 : 0;
}

void Z80nCpuSetNextRegFn(Z80nCPU* cpu, Z80nCpuNextRegFn fn, void* userData)
{
    cpu->nextRegFn = fn;
    cpu->nextRegData = userData;
}

void Z80nCpuSetStacklessNmi(Z80nCPU* cpu, int enabled, Z80nCpuNmiStoreFn store, Z80nCpuNmiLoadFn load, void* userData)
{
    cpu->stacklessNmi = enabled ? 1 : 0;
    cpu->nmiStore = store;
    cpu->nmiLoad = load;
    cpu->nmiData = userData;
    if (!enabled)
        cpu->stacklessRetn = 0;
}
