// z84cpu.cpp - API implementation of the Z84C15 core (fork of unreal-z80 0.5.0).
//
// Step loop, Q-register maintenance and interrupt entry are ports of the
// corresponding logic in core/src/emulator/cpu/z80.cpp (Z80Step, Z80::Reset,
// Z80::ProcessInterrupts NMI branch, Z80::HandleINT), stripped of emulator
// context, debugger, contention and frame-loop concerns.

#include "z84cpu-internal.h"

#include <cstring>
#include "z84cpu-dispatch.h"

#include <cstdlib>
#include <new>

const char* Z84CpuVersion(void)
{
    return "0.5.0-z84c15.1";
}

// Null-bus stubs: an unwired callback is a stub, never a null pointer, so
// the bus primitives carry no per-access null test (measured ~1-2% on the
// callback bus). Semantics unchanged: reads 0xFF, writes discarded, INT
// vector 0xFF.
static uint8_t NullMemRead(Z84CPU*, uint16_t, Z84CpuAccessKind, void*) { return 0xFF; }
static void NullMemWrite(Z84CPU*, uint16_t, uint8_t, void*) {}
static uint8_t NullPortIn(Z84CPU*, uint16_t, void*) { return 0xFF; }
static void NullPortOut(Z84CPU*, uint16_t, uint8_t, void*) {}
static uint8_t NullIntVector(Z84CPU*, void*) { return 0xFF; }

// DDCB/FDCB destination registers: b,c,d,e,h,l,<trash>,a - pointers into
// the active register file, rebound whenever it changes.
static void BindDirectRegisters(Z84CPU* cpu)
{
    cpu->directRegisters[0] = &Z84R(cpu).b;
    cpu->directRegisters[1] = &Z84R(cpu).c;
    cpu->directRegisters[2] = &Z84R(cpu).d;
    cpu->directRegisters[3] = &Z84R(cpu).e;
    cpu->directRegisters[4] = &Z84R(cpu).h;
    cpu->directRegisters[5] = &Z84R(cpu).l;
    cpu->directRegisters[6] = &cpu->trashRegister;
    cpu->directRegisters[7] = &Z84R(cpu).a;
}

Z84CPU* Z84CpuCreate(void)
{
    Z84TablesInit();

    Z84CPU* cpu = new (std::nothrow) Z84CPU{};
    if (!cpu)
        return nullptr;

    Z84CpuSetMemoryBus(cpu, nullptr, nullptr, nullptr, nullptr);
    Z84CpuSetPortBus(cpu, nullptr, nullptr, nullptr, nullptr);
    Z84CpuSetIntVectorFn(cpu, nullptr, nullptr);

    // CMOS core: OUT (C),0 drives #FF (research-cpu-z84c15.md section 3)
    cpu->outc0 = 0xFF;

    // No programmed waits until a Z84Lib::Z84C15 powers the chip on
    cpu->wait = {};
    cpu->wait.mwbr = 0xF0;

    cpu->regs = &cpu->ownedRegs;
    BindDirectRegisters(cpu);

    Z84CpuReset(cpu);

    // The callback bus, the only bus of this fork (an unwired bus reads 0xFF)
    cpu->stepFn = &Z84Cb::Step;
    return cpu;
}

void Z84CpuDestroy(Z84CPU* cpu)
{
    delete cpu;
}

// Port of Z80::Reset(): deterministic post-reset state (general registers
// cleared - a real chip leaves them undefined).
void Z84CpuReset(Z84CPU* cpu)
{
    Z84R(cpu).nmi_in_progress = false;

    cpu->t = 3;  // the reset sequence itself costs 3 T-states (core parity)

    Z84R(cpu).int_flags = 0;  // IM0, IFF1=IFF2=0, not halted (also clears r_hi)
    Z84R(cpu).ir_ = 0;        // I = R = 0
    Z84R(cpu).pc = 0x0000;
    Z84R(cpu).im = 0;
    Z84R(cpu).sp = 0xFFFF;  // real chip behavior
    Z84R(cpu).af = 0xFFFF;  // real chip behavior
    Z84R(cpu).q = 0;

    Z84R(cpu).bc = 0;
    Z84R(cpu).de = 0;
    Z84R(cpu).hl = 0;
    Z84R(cpu).ix = 0;
    Z84R(cpu).iy = 0;

    Z84R(cpu).alt.af = 0;
    Z84R(cpu).alt.bc = 0;
    Z84R(cpu).alt.de = 0;
    Z84R(cpu).alt.hl = 0;

    Z84R(cpu).memptr = 0;

    cpu->opword = 0;
    cpu->halt_cycle = 0;
    cpu->m1pc = 0;

    // Bus wiring intentionally survives a reset.
}

int Z84CpuHalted(const Z84CPU* cpu)
{
    return Z84R(cpu).halted & 1;
}

// Boundary state derived from the dispatched opcode word: cpu->opword holds
// the last M1-fetched opcode and its prefix class from step end until the
// next step (acknowledges overwrite it). Exactly one of these, or none,
// describes the boundary (Z84CpuBoundary):
//   0x00FB           INT shadow: EI ran (unprefixed or DD/FD FB; CB FB =
//                    SET 7,E and ED FB = NOP share the byte but not the
//                    shadow), or a RETN/RETI set IFF1 (ope_45/ope_4D store
//                    0x00FB then: IFF2 reaches IFF1 too late for this
//                    boundary's INT sampling - Weissflog 2021, Sainz de
//                    Baranda 2022). INT refused, NMI accepted
//   0x00DD / 0x00FD  with Z84_PENDING_PREFIX in the halted byte: a redundant
//                    prefix ended the step, this prefix is fetched and its
//                    instruction not yet run: INT and NMI refused (see
//                    ddfd_prefixes); the flag is what Step tests, opword
//                    says which prefix
//   0xED57 / 0xED5F  LD A,I / LD A,R ran: an INT accepted here clears P/V
//   0xFF66           an NMI was just acknowledged: a second NMI is refused
//                    until an instruction has run (Sainz de Baranda 2022).
//                    The prefix byte FF never comes from an M1 fetch
static constexpr uint16_t kOpwordIntShadow = Z84_OPWORD_INT_SHADOW;
static constexpr uint16_t kOpwordNmiAck = Z84_OPWORD_NMI_ACK;

static bool Z84CpuPrefixPending(const Z84CPU* cpu)
{
    return (Z84R(cpu).halted & Z84_PENDING_PREFIX) != 0;
}

int Z84CpuIntPossible(const Z84CPU* cpu)
{
    if (!Z84R(cpu).iff1)
        return 0;
    return (cpu->opword == kOpwordIntShadow || Z84CpuPrefixPending(cpu)) ? 0 : 1;
}

static Z84CpuBoundary Z84CpuGetBoundary(const Z84CPU* cpu)
{
    if (Z84CpuPrefixPending(cpu))
        return cpu->opword == 0x00DD ? Z84CpuBoundaryPrefixDd : Z84CpuBoundaryPrefixFd;
    switch (cpu->opword)
    {
        case kOpwordIntShadow: return Z84CpuBoundaryIntShadow;
        case 0xED57:
        case 0xED5F: return Z84CpuBoundaryLdAIr;
        case kOpwordNmiAck: return Z84CpuBoundaryNmiAck;
        default: return Z84CpuBoundaryNone;
    }
}

// Inverse: the opword that reproduces the state, and the pending flag next
// to the HALT latch (bit 0 kept). Unknown values set None.
static void Z84CpuSetBoundary(Z84CPU* cpu, unsigned boundary)
{
    uint16_t opword = 0;
    switch (boundary)
    {
        case Z84CpuBoundaryPrefixDd: opword = 0x00DD; break;
        case Z84CpuBoundaryPrefixFd: opword = 0x00FD; break;
        case Z84CpuBoundaryIntShadow: opword = kOpwordIntShadow; break;
        case Z84CpuBoundaryLdAIr: opword = 0xED57; break;
        case Z84CpuBoundaryNmiAck: opword = kOpwordNmiAck; break;
        default: break;
    }
    const bool pending = boundary == Z84CpuBoundaryPrefixDd || boundary == Z84CpuBoundaryPrefixFd;
    cpu->opword = opword;
    Z84R(cpu).halted = static_cast<uint8_t>((Z84R(cpu).halted & 1) | (pending ? Z84_PENDING_PREFIX : 0));
}

uint32_t Z84CpuTstates(const Z84CPU* cpu)
{
    return cpu->t;
}

void Z84CpuSetTstates(Z84CPU* cpu, uint32_t tstates)
{
    cpu->t = tstates;
}

// One instruction: the callback bus's Step (opcodes-callback.cpp, step body
// in z84step.inc).
int Z84CpuStep(Z84CPU* cpu)
{
    const uint32_t t0 = cpu->t;
    return static_cast<int>(static_cast<uint32_t>(cpu->stepFn(cpu)) - t0);
}

Z84CpuStepFn Z84CpuStepEntry(const Z84CPU* cpu)
{
    return cpu->stepFn;
}

// Memory cycles of the interrupt acknowledge sequences (stack push, IM2
// vector-table read). Each is a regular 3 T bus cycle: the contention hook
// fires at its first T, cpu->t is published before the memory callback so
// the host observes the exact access T, the clock is taken back after it
// (the host's external /WAIT), then the chip's memory waits follow - the
// same contract as an instruction's Z84Rd/Z84Wd. tact is the running T
// accumulator of the acknowledge; returns the T after the cycle.
static int Z84CpuAckWrite(Z84CPU* cpu, uint16_t addr, uint8_t val, int tact)
{
    Z84ContendT(cpu, addr, Z84CpuAccessWrite, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    cpu->memWrite(cpu, addr, val, cpu->memWriteData);
    tact = static_cast<int>(cpu->t);
    if (cpu->wait.active)
        tact += Z84Lib::Z84WaitMemory(cpu, addr);
    return tact;
}

static uint8_t Z84CpuAckRead(Z84CPU* cpu, uint16_t addr, int& tact)
{
    Z84ContendT(cpu, addr, Z84CpuAccessRead, tact, tact);
    tact += 3;
    cpu->t = static_cast<uint32_t>(tact);
    const uint8_t value = cpu->memRead(cpu, addr, Z84CpuAccessRead, cpu->memReadData);
    tact = static_cast<int>(cpu->t);
    if (cpu->wait.active)
        tact += Z84Lib::Z84WaitMemory(cpu, addr);
    return value;
}

// Push PC (M2 = PCH at SP-1, M3 = PCL at SP-2) through the acknowledge
// memory cycles; tact enters at the first T of M2 and leaves after M3.
static int Z84CpuPushPc(Z84CPU* cpu, int tact)
{
    uint16_t sp = Z84R(cpu).sp;
    tact = Z84CpuAckWrite(cpu, --sp, Z84R(cpu).pch, tact);
    tact = Z84CpuAckWrite(cpu, --sp, Z84R(cpu).pcl, tact);
    Z84R(cpu).sp = sp;
    return tact;
}

// Port of the NMI branch of Z80::ProcessInterrupts(): 11 T total
// (M1=5T restart fetch, M2/M3=3T+3T push).
int Z84CpuNmi(Z84CPU* cpu)
{
    // Not between a prefix and its instruction (see ddfd_prefixes), and not
    // twice without an instruction between: the host retries at the next
    // boundary.
    if (Z84CpuPrefixPending(cpu) || cpu->opword == kOpwordNmiAck)
        return 0;

    Z84R(cpu).nmi_in_progress = true;

    // If halted: unblock by moving PC past the HALT (return lands after it).
    // Keyed on the HALT latch, not on the byte at PC: an interrupt taken at
    // the boundary before a not-yet-executed HALT must return to it.
    if (Z84R(cpu).halted & 1)
        Z84R(cpu).pc++;

    // The acknowledge starts with an M1 cycle (opcode fetch, ignored) that
    // performs a refresh like any M1: R increases (FUSE, z80ex, silicon).
    Z84_R_INC(cpu->regs);

    // M1 = 5 T restart fetch (an opcode-fetch cycle whose byte is ignored: no
    // host read; the chip's M1 waits apply), then the two 3 T push cycles
    // (11 T + waits)
    const int t0 = static_cast<int>(cpu->t);
    int restart = t0 + 5;
    if (cpu->wait.active)
        restart += Z84Lib::Z84WaitM1(cpu, Z84R(cpu).pc, 0x00);
    const int tact = Z84CpuPushPc(cpu, restart);
    cpu->t = static_cast<uint32_t>(tact);

    Z84R(cpu).pc = 0x0066;
    Z84R(cpu).memptr = 0x0066;
    Z84R(cpu).halted = 0;
    cpu->halt_cycle = 0;
    Z84R(cpu).q = 0;  // the acknowledge cycle writes no flags

    // Maskable ints disabled in the handler; IFF2 is left alone and keeps
    // the pre-NMI state for RETN (UM0080 table 1 "Accept NMI: IFF1 0, IFF2
    // unchanged"; Sean Young's nested-NMI hardware test: IFF1 is not copied
    // to IFF2 - so a nested NMI does not lose the outer state).
    Z84R(cpu).iff1 = 0;
    cpu->opword = kOpwordNmiAck;

    return tact - t0;
}

// Port of Z80::HandleINT() preceded by the acceptance checks of
// ProcessInterrupts(): IM0/IM1 = 13 T, IM2 = 19 T.
int Z84CpuInt(Z84CPU* cpu)
{
    if (!Z84CpuIntPossible(cpu))
        return 0;

    // If halted: unblock by moving PC past the HALT (see Z84CpuNmi)
    if (Z84R(cpu).halted & 1)
        Z84R(cpu).pc++;

    // No LD A,I / LD A,R quirk: on the NMOS Z80 an INT accepted right after
    // ED 57 / ED 5F clears P/V; the CMOS core keeps it ("On CMOS Z80 CPU,
    // we've fixed this problem", Zilog Z80 Family Q&A p. 3-130;
    // research-cpu-z84c15.md section 3). unreal-z80 clears it here.

    // The acknowledge cycle is an M1 cycle with refresh: R increases in
    // every mode (FUSE, z80ex, silicon).
    Z84_R_INC(cpu->regs);

    uint16_t handlerAddress;
    const int t0 = static_cast<int>(cpu->t);
    int tact;

    // The INTA cycle: 7 T plus the chip's daisy-chain and vector waits
    // (WCR bits 7-5, design section 6); no memory cycle, so no memory waits
    const int inta = 7 + Z84Lib::Z84WaitInta(cpu);

    if (Z84R(cpu).im < 2)
    {
        // IM0/IM1 restart at 0x38. (IM0's bus-byte execution is not modeled,
        // matching the core: im < 2 -> 0x38.)
        // M1 = 7 T ack, M2/M3 = 3 T + 3 T push: 13 T + waits
        tact = Z84CpuPushPc(cpu, t0 + inta);
        handlerAddress = 0x38;
    }
    else
    {
        // IM2 in machine-cycle order: vector byte from the data bus during
        // the acknowledge (M1), push PC (M2/M3), then read the handler address
        // from the vector table (M4/M5) - so a stack that overlaps the table
        // supplies the freshly pushed bytes, as on the chip: 19 T + waits.
        const uint8_t vector = cpu->intVector(cpu, cpu->intVectorData);
        tact = Z84CpuPushPc(cpu, t0 + inta);
        const uint16_t vectorAddress = static_cast<uint16_t>(vector + Z84R(cpu).i * 0x100);
        const uint8_t low = Z84CpuAckRead(cpu, vectorAddress, tact);
        const uint8_t high = Z84CpuAckRead(cpu, static_cast<uint16_t>(vectorAddress + 1), tact);
        handlerAddress = static_cast<uint16_t>(low + 0x100 * high);
    }
    cpu->t = static_cast<uint32_t>(tact);
    const int duration = tact - t0;

    Z84R(cpu).pc = handlerAddress;
    Z84R(cpu).memptr = handlerAddress;
    Z84R(cpu).halted = 0;
    cpu->halt_cycle = 0;
    Z84R(cpu).q = 0;  // the acknowledge cycle writes no flags

    // No double acceptance until EI
    Z84R(cpu).iff1 = 0;
    Z84R(cpu).iff2 = 0;
    cpu->opword = 0;  // no boundary state after an acknowledge

    return duration;
}

uint16_t Z84CpuGetReg(const Z84CPU* cpu, Z84CpuReg reg)
{
    switch (reg)
    {
        case Z84CpuRegAf: return Z84R(cpu).af;
        case Z84CpuRegBc: return Z84R(cpu).bc;
        case Z84CpuRegDe: return Z84R(cpu).de;
        case Z84CpuRegHl: return Z84R(cpu).hl;
        case Z84CpuRegAfAlt: return Z84R(cpu).alt.af;
        case Z84CpuRegBcAlt: return Z84R(cpu).alt.bc;
        case Z84CpuRegDeAlt: return Z84R(cpu).alt.de;
        case Z84CpuRegHlAlt: return Z84R(cpu).alt.hl;
        case Z84CpuRegIx: return Z84R(cpu).ix;
        case Z84CpuRegIy: return Z84R(cpu).iy;
        case Z84CpuRegPc: return Z84R(cpu).pc;
        case Z84CpuRegSp: return Z84R(cpu).sp;
        case Z84CpuRegI: return Z84R(cpu).i;
        case Z84CpuRegR: return (Z84R(cpu).r_low & 0x7F) | (Z84R(cpu).r_hi & 0x80);
        case Z84CpuRegR7: return (Z84R(cpu).r_hi & 0x80) >> 7;
        case Z84CpuRegIm: return Z84R(cpu).im;
        case Z84CpuRegIff1: return Z84R(cpu).iff1;
        case Z84CpuRegIff2: return Z84R(cpu).iff2;
        case Z84CpuRegMemptr: return Z84R(cpu).memptr;
        case Z84CpuRegQ: return Z84R(cpu).q;
        case Z84CpuRegHalted: return Z84R(cpu).halted & 1;
        case Z84CpuRegBoundary: return static_cast<uint16_t>(Z84CpuGetBoundary(cpu));
        case Z84CpuRegNmiInProgress: return Z84R(cpu).nmi_in_progress ? 1 : 0;
        default: return 0;
    }
}

void Z84CpuSetReg(Z84CPU* cpu, Z84CpuReg reg, uint16_t value)
{
    switch (reg)
    {
        case Z84CpuRegAf: Z84R(cpu).af = value; break;
        case Z84CpuRegBc: Z84R(cpu).bc = value; break;
        case Z84CpuRegDe: Z84R(cpu).de = value; break;
        case Z84CpuRegHl: Z84R(cpu).hl = value; break;
        case Z84CpuRegAfAlt: Z84R(cpu).alt.af = value; break;
        case Z84CpuRegBcAlt: Z84R(cpu).alt.bc = value; break;
        case Z84CpuRegDeAlt: Z84R(cpu).alt.de = value; break;
        case Z84CpuRegHlAlt: Z84R(cpu).alt.hl = value; break;
        case Z84CpuRegIx: Z84R(cpu).ix = value; break;
        case Z84CpuRegIy: Z84R(cpu).iy = value; break;
        case Z84CpuRegPc: Z84R(cpu).pc = value; break;
        case Z84CpuRegSp: Z84R(cpu).sp = value; break;
        case Z84CpuRegI: Z84R(cpu).i = static_cast<uint8_t>(value); break;
        case Z84CpuRegR:
            Z84R(cpu).r_low = static_cast<uint8_t>(value);  // as LD R,A
            Z84R(cpu).r_hi = value & 0x80;
            break;
        case Z84CpuRegR7: Z84R(cpu).r_hi = static_cast<uint8_t>(value & 0x80); break;
        case Z84CpuRegIm: Z84R(cpu).im = static_cast<uint8_t>(value > 2 ? 2 : value); break;
        case Z84CpuRegIff1: Z84R(cpu).iff1 = static_cast<uint8_t>(value & 1); break;
        case Z84CpuRegIff2: Z84R(cpu).iff2 = static_cast<uint8_t>(value & 1); break;
        case Z84CpuRegMemptr: Z84R(cpu).memptr = value; break;
        case Z84CpuRegQ: Z84R(cpu).q = static_cast<uint8_t>(value & 0x28); break;
        case Z84CpuRegHalted:
            Z84R(cpu).halted = static_cast<uint8_t>((Z84R(cpu).halted & Z84_PENDING_PREFIX) | (value ? 1 : 0));
            break;
        case Z84CpuRegBoundary: Z84CpuSetBoundary(cpu, value); break;
        case Z84CpuRegNmiInProgress: Z84R(cpu).nmi_in_progress = value != 0; break;
        default: break;
    }
}

void Z84CpuGetRegisters(const Z84CPU* cpu, Z84CpuRegisters* regs)
{
    regs->af = Z84R(cpu).af;
    regs->bc = Z84R(cpu).bc;
    regs->de = Z84R(cpu).de;
    regs->hl = Z84R(cpu).hl;
    regs->afAlt = Z84R(cpu).alt.af;
    regs->bcAlt = Z84R(cpu).alt.bc;
    regs->deAlt = Z84R(cpu).alt.de;
    regs->hlAlt = Z84R(cpu).alt.hl;
    regs->ix = Z84R(cpu).ix;
    regs->iy = Z84R(cpu).iy;
    regs->pc = Z84R(cpu).pc;
    regs->sp = Z84R(cpu).sp;
    regs->memptr = Z84R(cpu).memptr;
    regs->i = Z84R(cpu).i;
    regs->r = static_cast<uint8_t>((Z84R(cpu).r_low & 0x7F) | (Z84R(cpu).r_hi & 0x80));
    regs->im = Z84R(cpu).im;
    regs->iff1 = Z84R(cpu).iff1;
    regs->iff2 = Z84R(cpu).iff2;
    regs->q = Z84R(cpu).q;
    regs->halted = Z84R(cpu).halted & 1;
    regs->boundary = static_cast<uint8_t>(Z84CpuGetBoundary(cpu));
    regs->nmiInProgress = Z84R(cpu).nmi_in_progress ? 1 : 0;
}

void Z84CpuSetRegisters(Z84CPU* cpu, const Z84CpuRegisters* regs)
{
    Z84R(cpu).af = regs->af;
    Z84R(cpu).bc = regs->bc;
    Z84R(cpu).de = regs->de;
    Z84R(cpu).hl = regs->hl;
    Z84R(cpu).alt.af = regs->afAlt;
    Z84R(cpu).alt.bc = regs->bcAlt;
    Z84R(cpu).alt.de = regs->deAlt;
    Z84R(cpu).alt.hl = regs->hlAlt;
    Z84R(cpu).ix = regs->ix;
    Z84R(cpu).iy = regs->iy;
    Z84R(cpu).pc = regs->pc;
    Z84R(cpu).sp = regs->sp;
    Z84R(cpu).memptr = regs->memptr;
    Z84R(cpu).i = regs->i;
    Z84R(cpu).r_low = regs->r;  // as LD R,A: the full byte, R7 also in r_hi
    Z84R(cpu).r_hi = static_cast<uint8_t>(regs->r & 0x80);
    Z84R(cpu).im = static_cast<uint8_t>(regs->im > 2 ? 2 : regs->im);
    Z84R(cpu).iff1 = static_cast<uint8_t>(regs->iff1 & 1);
    Z84R(cpu).iff2 = static_cast<uint8_t>(regs->iff2 & 1);
    Z84R(cpu).q = static_cast<uint8_t>(regs->q & 0x28);
    Z84R(cpu).halted = regs->halted ? 1 : 0;
    Z84CpuSetBoundary(cpu, regs->boundary);
    Z84R(cpu).nmi_in_progress = regs->nmiInProgress != 0;
}

void Z84CpuSetMemoryBus(Z84CPU* cpu, Z84CpuMemReadFn readFn, void* readData,
                        Z84CpuMemWriteFn writeFn, void* writeData)
{
    cpu->memRead = readFn ? readFn : NullMemRead;
    cpu->memReadData = readData;
    cpu->memWrite = writeFn ? writeFn : NullMemWrite;
    cpu->memWriteData = writeData;
}

void Z84CpuSetPortBus(Z84CPU* cpu, Z84CpuPortInFn inFn, void* inData,
                      Z84CpuPortOutFn outFn, void* outData)
{
    cpu->portIn = inFn ? inFn : NullPortIn;
    cpu->portInData = inData;
    cpu->portOut = outFn ? outFn : NullPortOut;
    cpu->portOutData = outData;
}

void Z84CpuSetIntVectorFn(Z84CPU* cpu, Z84CpuIntVectorFn fn, void* userData)
{
    cpu->intVector = fn ? fn : NullIntVector;
    cpu->intVectorData = userData;
}

void Z84CpuSetRetiFn(Z84CPU* cpu, Z84CpuRetiFn fn, void* userData)
{
    cpu->retiCallback = fn;
    cpu->retiData = userData;
}

void Z84CpuSetRetnFn(Z84CPU* cpu, Z84CpuRetnFn fn, void* userData)
{
    cpu->retnCallback = fn;
    cpu->retnData = userData;
}

void Z84CpuSetContendFn(Z84CPU* cpu, Z84CpuContendFn fn, void* userData)
{
    cpu->contend = fn;
    cpu->contendData = userData;
}

// Out-of-line half of the contention hook (see Z84ContendT): publishes the
// cycle-start T so the host's lookup sees it, then returns the waits.
int Z84ContendSlow(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, int cycleStart)
{
    cpu->t = static_cast<uint32_t>(cycleStart);
    return cpu->contend(cpu, addr, kind, cpu->contendData);
}

uint16_t Z84CpuInstructionPc(const Z84CPU* cpu)
{
    return cpu->m1pc;
}

uint16_t Z84CpuOpcodeWord(const Z84CPU* cpu)
{
    return cpu->opword;
}

void Z84CpuSetOutC0Value(Z84CPU* cpu, uint8_t value)
{
    cpu->outc0 = value;
}

void Z84CpuAddWaitStates(Z84CPU* cpu, uint32_t tstates)
{
    cpu->t += tstates;
}

// ---- zero-copy register file ----------------------------------------------

// The engine's internal view (anonymous 8/16-bit unions) and the public
// plain-field struct describe the same 41 bytes.
static_assert(sizeof(Z84Regs) == sizeof(Z84CpuRegisterFile), "register file size");
static_assert(offsetof(Z84Regs, pc) == offsetof(Z84CpuRegisterFile, pc), "pc");
static_assert(offsetof(Z84Regs, sp) == offsetof(Z84CpuRegisterFile, sp), "sp");
static_assert(offsetof(Z84Regs, ir_) == offsetof(Z84CpuRegisterFile, rLow), "r_low");
static_assert(offsetof(Z84Regs, int_flags) == offsetof(Z84CpuRegisterFile, rHi), "r_hi");
static_assert(offsetof(Z84Regs, bc) == offsetof(Z84CpuRegisterFile, bc), "bc");
static_assert(offsetof(Z84Regs, de) == offsetof(Z84CpuRegisterFile, de), "de");
static_assert(offsetof(Z84Regs, hl) == offsetof(Z84CpuRegisterFile, hl), "hl");
static_assert(offsetof(Z84Regs, af) == offsetof(Z84CpuRegisterFile, af), "af");
static_assert(offsetof(Z84Regs, ix) == offsetof(Z84CpuRegisterFile, ix), "ix");
static_assert(offsetof(Z84Regs, iy) == offsetof(Z84CpuRegisterFile, iy), "iy");
static_assert(offsetof(Z84Regs, alt) == offsetof(Z84CpuRegisterFile, bcAlt), "alt");
static_assert(offsetof(Z84Regs, memptr) == offsetof(Z84CpuRegisterFile, memptr), "memptr");
static_assert(offsetof(Z84Regs, q) == offsetof(Z84CpuRegisterFile, q), "q");
static_assert(offsetof(Z84Regs, reservedEipos) == offsetof(Z84CpuRegisterFile, reservedEipos), "eipos");
static_assert(offsetof(Z84Regs, reservedHaltpos) == offsetof(Z84CpuRegisterFile, reservedHaltpos), "haltpos");
static_assert(offsetof(Z84Regs, im) == offsetof(Z84CpuRegisterFile, im), "im");
static_assert(offsetof(Z84Regs, nmi_in_progress) == offsetof(Z84CpuRegisterFile, nmiInProgress), "nmi");
static_assert(sizeof(bool) == 1, "nmi_in_progress is one byte");

void Z84CpuAttachRegisterFile(Z84CPU* cpu, Z84CpuRegisterFile* file)
{
    if (file)
    {
        cpu->regs = reinterpret_cast<Z84Regs*>(file);
    }
    else
    {
        if (cpu->regs != &cpu->ownedRegs)
            std::memcpy(&cpu->ownedRegs, cpu->regs, sizeof(Z84Regs));
        cpu->regs = &cpu->ownedRegs;
    }
    BindDirectRegisters(cpu);
}

Z84CpuRegisterFile* Z84CpuRegisterFilePtr(Z84CPU* cpu)
{
    return reinterpret_cast<Z84CpuRegisterFile*>(cpu->regs);
}
