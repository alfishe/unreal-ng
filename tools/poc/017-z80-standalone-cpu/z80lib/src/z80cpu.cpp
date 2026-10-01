// z80cpu.cpp - API implementation of the standalone Z80 core (PoC 017).
//
// Step loop, Q-register maintenance and interrupt entry are ports of the
// corresponding logic in core/src/emulator/cpu/z80.cpp (Z80Step, Z80::Reset,
// Z80::ProcessInterrupts NMI branch, Z80::HandleINT), stripped of emulator
// context, debugger, contention and frame-loop concerns.

#include "z80cpu-internal.h"
#include "z80cpu-opcodes.h"

#include <cstdlib>
#include <new>

const char* Z80CpuVersion(void)
{
    return "0.1.0-poc";
}

Z80CPU* Z80CpuCreate(void)
{
    Z80TablesInit();

    Z80CPU* cpu = new (std::nothrow) Z80CPU{};
    if (!cpu)
        return nullptr;

    // DDCB/FDCB destination registers: b,c,d,e,h,l,<trash>,a
    cpu->directRegisters[0] = &cpu->b;
    cpu->directRegisters[1] = &cpu->c;
    cpu->directRegisters[2] = &cpu->d;
    cpu->directRegisters[3] = &cpu->e;
    cpu->directRegisters[4] = &cpu->h;
    cpu->directRegisters[5] = &cpu->l;
    cpu->directRegisters[6] = &cpu->trashRegister;
    cpu->directRegisters[7] = &cpu->a;

    Z80CpuReset(cpu);
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
    cpu->last_branch = 0x0000;
    cpu->nmi_in_progress = false;

    cpu->t = 3;  // the reset sequence itself costs 3 T-states (core parity)

    cpu->int_flags = 0;  // IM0, IFF1=IFF2=0, not halted (also clears r_hi)
    cpu->ir_ = 0;        // I = R = 0
    cpu->pc = 0x0000;
    cpu->im = 0;
    cpu->sp = 0xFFFF;  // real chip behavior
    cpu->af = 0xFFFF;  // real chip behavior
    cpu->q = 0;

    cpu->bc = 0;
    cpu->de = 0;
    cpu->hl = 0;
    cpu->ix = 0;
    cpu->iy = 0;

    cpu->alt.af = 0;
    cpu->alt.bc = 0;
    cpu->alt.de = 0;
    cpu->alt.hl = 0;

    cpu->memptr = 0;
    cpu->eipos = 0;
    cpu->haltpos = 0;

    cpu->prefix = 0;
    cpu->opcode = 0;
    cpu->prev_pc = 0;
    cpu->m1_pc = 0;
    cpu->halt_cycle = 0;
    cpu->intSuppress = false;

    // Bus wiring intentionally survives a reset.
}

int Z80CpuHalted(const Z80CPU* cpu)
{
    return cpu->halted ? 1 : 0;
}

int Z80CpuIntPossible(const Z80CPU* cpu)
{
    return (cpu->iff1 && !cpu->intSuppress) ? 1 : 0;
}

uint32_t Z80CpuTstates(const Z80CPU* cpu)
{
    return cpu->t;
}

void Z80CpuSetTstates(Z80CPU* cpu, uint32_t tstates)
{
    cpu->t = tstates;
}

// Port of the HALT branch of Z80::Z80Step(): no opcode processing happens
// until INT/NMI releases the CPU. The core burns 1 T per loop iteration and
// advances R every 4th; one step call here burns the 4-T quantum and ticks R.
static int Z80CpuStepHalted(Z80CPU* cpu)
{
    cpu->t += 4;
    cpu->r_low = ((cpu->r_low + 1) & 0x7f) | (cpu->r_low & 0x80);
    return 4;
}

int Z80CpuStep(Z80CPU* cpu)
{
    if (cpu->halted)
        return Z80CpuStepHalted(cpu);

    const uint32_t t0 = cpu->t;

    // If a HALTed CPU is released by INT/NMI the release itself advanced PC
    // past the HALT opcode (see Z80CpuInt/Z80CpuNmi), so plain dispatch here.

    cpu->prev_pc = cpu->m1_pc;

    // Save F before execution for the Q-register update (see below)
    const uint8_t prev_f = cpu->f;

    // Regular bus cycle: fetch opcode (M1) and execute
    cpu->prefix = 0x0000;
    cpu->opcode = cpu->m1_cycle();
    (normal_opcode[cpu->opcode])(cpu);

    // Q register maintenance, ported from Z80Step():
    //  - flags changed: Q captures the new YF/XF bits
    //  - SCF/CCF keep the Q value they set inside their handlers
    //  - anything else clears Q (not a flag-modifying instruction)
    if (cpu->f != prev_f)
    {
        cpu->q = cpu->f & 0x28;
    }
    else if (cpu->opcode == 0x37 || cpu->opcode == 0x3F)
    {
        // SCF/CCF set Q internally, preserve their value
    }
    else
    {
        cpu->q = 0;
    }

    // Post-EI interrupt shadow (the core expresses this as "cpu.t != cpu.eipos"
    // in ProcessInterrupts): a maskable interrupt becomes acceptable only
    // after the instruction following EI has executed. op_FB leaves eipos = t
    // at the end of EI, so the shadow lasts exactly one instruction here.
    cpu->intSuppress = (cpu->opcode == 0xFB);

    // RETI notification (z80ex-style device hook)
    if (cpu->retiCallback && cpu->opcode == 0x4D && cpu->prefix == 0xED)
        cpu->retiCallback(cpu, cpu->retiData);

    return static_cast<int>(cpu->t - t0);
}

// Push PC via raw writes - the T-states are accounted by the caller's
// interrupt duration (exactly like Z80::HandleINT/ProcessInterrupts).
static void Z80CpuPushPc(Z80CPU* cpu)
{
    uint16_t sp = cpu->sp;
    cpu->RawWrite(--sp, cpu->pch);
    cpu->RawWrite(--sp, cpu->pcl);
    cpu->sp = sp;
}

// Port of the NMI branch of Z80::ProcessInterrupts(): 11 T total
// (M1=5T restart fetch, M2/M3=3T+3T push).
int Z80CpuNmi(Z80CPU* cpu)
{
    cpu->nmi_in_progress = true;

    // If halted: unblock by moving PC past the HALT (return lands after it)
    if (cpu->RawRead(cpu->pc) == 0x76)
        cpu->pc++;

    cpu->t += 11;
    Z80CpuPushPc(cpu);

    cpu->pc = 0x0066;
    cpu->memptr = 0x0066;
    cpu->halted = 0;
    cpu->halt_cycle = 0;

    // IFF2 keeps a copy of IFF1 for RETN; maskable ints disabled in handler
    cpu->iff2 = cpu->iff1;
    cpu->iff1 = 0;
    cpu->intSuppress = false;

    return 11;
}

// Port of Z80::HandleINT() preceded by the acceptance checks of
// ProcessInterrupts(): IM0/IM1 = 13 T, IM2 = 19 T.
int Z80CpuInt(Z80CPU* cpu)
{
    if (!Z80CpuIntPossible(cpu))
        return 0;

    // If halted: unblock by moving PC past the HALT
    if (cpu->RawRead(cpu->pc) == 0x76)
        cpu->pc++;

    uint16_t handlerAddress;
    int duration;

    if (cpu->im < 2)
    {
        // IM0/IM1 restart at 0x38. (IM0's bus-byte execution is not modeled,
        // matching the core: im < 2 -> 0x38.)
        handlerAddress = 0x38;
        duration = 13;  // M1=7T ack, M2/M3=3T+3T push
    }
    else
    {
        // IM2: vector byte from the data bus, then the 2-byte vector fetch.
        // All reads are raw: their cycles are inside the 19 T.
        uint8_t vector = cpu->intVector ? cpu->intVector(cpu, cpu->intVectorData) : 0xFF;
        uint16_t vectorAddress = vector + cpu->i * 0x100;
        handlerAddress = cpu->RawRead(vectorAddress) + 0x100 * cpu->RawRead(vectorAddress + 1);
        duration = 19;  // M1=7T ack, M2/M3 push, M4/M5 vector read
    }

    cpu->t += static_cast<uint32_t>(duration);
    Z80CpuPushPc(cpu);

    cpu->pc = handlerAddress;
    cpu->memptr = handlerAddress;
    cpu->halted = 0;
    cpu->halt_cycle = 0;

    // No double acceptance until EI
    cpu->iff1 = 0;
    cpu->iff2 = 0;
    cpu->intSuppress = false;

    return duration;
}

uint16_t Z80CpuGetReg(const Z80CPU* cpu, Z80CpuReg reg)
{
    switch (reg)
    {
        case Z80CpuRegAf: return cpu->af;
        case Z80CpuRegBc: return cpu->bc;
        case Z80CpuRegDe: return cpu->de;
        case Z80CpuRegHl: return cpu->hl;
        case Z80CpuRegAfAlt: return cpu->alt.af;
        case Z80CpuRegBcAlt: return cpu->alt.bc;
        case Z80CpuRegDeAlt: return cpu->alt.de;
        case Z80CpuRegHlAlt: return cpu->alt.hl;
        case Z80CpuRegIx: return cpu->ix;
        case Z80CpuRegIy: return cpu->iy;
        case Z80CpuRegPc: return cpu->pc;
        case Z80CpuRegSp: return cpu->sp;
        case Z80CpuRegI: return cpu->i;
        case Z80CpuRegR: return (cpu->r_low & 0x7F) | (cpu->r_hi & 0x80);
        case Z80CpuRegR7: return (cpu->r_hi & 0x80) >> 7;
        case Z80CpuRegIm: return cpu->im;
        case Z80CpuRegIff1: return cpu->iff1;
        case Z80CpuRegIff2: return cpu->iff2;
        case Z80CpuRegMemptr: return cpu->memptr;
        case Z80CpuRegQ: return cpu->q;
        default: return 0;
    }
}

void Z80CpuSetReg(Z80CPU* cpu, Z80CpuReg reg, uint16_t value)
{
    switch (reg)
    {
        case Z80CpuRegAf: cpu->af = value; break;
        case Z80CpuRegBc: cpu->bc = value; break;
        case Z80CpuRegDe: cpu->de = value; break;
        case Z80CpuRegHl: cpu->hl = value; break;
        case Z80CpuRegAfAlt: cpu->alt.af = value; break;
        case Z80CpuRegBcAlt: cpu->alt.bc = value; break;
        case Z80CpuRegDeAlt: cpu->alt.de = value; break;
        case Z80CpuRegHlAlt: cpu->alt.hl = value; break;
        case Z80CpuRegIx: cpu->ix = value; break;
        case Z80CpuRegIy: cpu->iy = value; break;
        case Z80CpuRegPc: cpu->pc = value; break;
        case Z80CpuRegSp: cpu->sp = value; break;
        case Z80CpuRegI: cpu->i = static_cast<uint8_t>(value); break;
        case Z80CpuRegR:
            cpu->r_low = value & 0x7F;
            cpu->r_hi = value & 0x80;
            break;
        case Z80CpuRegR7: cpu->r_hi = static_cast<uint8_t>(value & 0x80); break;
        case Z80CpuRegIm: cpu->im = static_cast<uint8_t>(value > 2 ? 2 : value); break;
        case Z80CpuRegIff1: cpu->iff1 = static_cast<uint8_t>(value & 1); break;
        case Z80CpuRegIff2: cpu->iff2 = static_cast<uint8_t>(value & 1); break;
        case Z80CpuRegMemptr: cpu->memptr = value; break;
        case Z80CpuRegQ: cpu->q = static_cast<uint8_t>(value & 0x28); break;
        default: break;
    }
}

void Z80CpuSetMemoryBus(Z80CPU* cpu, Z80CpuMemReadFn readFn, void* readData,
                        Z80CpuMemWriteFn writeFn, void* writeData)
{
    cpu->memRead = readFn;
    cpu->memReadData = readData;
    cpu->memWrite = writeFn;
    cpu->memWriteData = writeData;
}

void Z80CpuSetPortBus(Z80CPU* cpu, Z80CpuPortInFn inFn, void* inData,
                      Z80CpuPortOutFn outFn, void* outData)
{
    cpu->portIn = inFn;
    cpu->portInData = inData;
    cpu->portOut = outFn;
    cpu->portOutData = outData;
}

void Z80CpuSetIntVectorFn(Z80CPU* cpu, Z80CpuIntVectorFn fn, void* userData)
{
    cpu->intVector = fn;
    cpu->intVectorData = userData;
}

void Z80CpuSetRetiFn(Z80CPU* cpu, Z80CpuRetiFn fn, void* userData)
{
    cpu->retiCallback = fn;
    cpu->retiData = userData;
}

void Z80CpuAttachMemory(Z80CPU* cpu, uint8_t* memory64k)
{
    cpu->mem = memory64k;
}

void Z80CpuSetOutC0Value(Z80CPU* cpu, uint8_t value)
{
    cpu->outc0 = value;
}
