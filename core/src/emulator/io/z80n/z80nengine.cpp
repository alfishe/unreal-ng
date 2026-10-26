#include "stdafx.h"

#include "z80nengine.h"

#include <cstddef>

#include "debugger/ttd/ttdportjournal.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/ulacontention.h"

/// region <Register file layout contract>

// The library executes on Z80Registers from `pc` on (Z80nCpuAttachRegisterFile): every field of Z80nCpuRegisterFile at the same
// offset from `pc` (the contract of the Z84C15 engine)
namespace
{
constexpr size_t kBase = offsetof(Z80Registers, pc);
}
static_assert(offsetof(Z80Registers, sp) - kBase == offsetof(Z80nCpuRegisterFile, sp), "sp");
static_assert(offsetof(Z80Registers, ir_) - kBase == offsetof(Z80nCpuRegisterFile, rLow), "r_low");
static_assert(offsetof(Z80Registers, int_flags) - kBase == offsetof(Z80nCpuRegisterFile, rHi), "r_hi");
static_assert(offsetof(Z80Registers, bc) - kBase == offsetof(Z80nCpuRegisterFile, bc), "bc");
static_assert(offsetof(Z80Registers, de) - kBase == offsetof(Z80nCpuRegisterFile, de), "de");
static_assert(offsetof(Z80Registers, hl) - kBase == offsetof(Z80nCpuRegisterFile, hl), "hl");
static_assert(offsetof(Z80Registers, af) - kBase == offsetof(Z80nCpuRegisterFile, af), "af");
static_assert(offsetof(Z80Registers, ix) - kBase == offsetof(Z80nCpuRegisterFile, ix), "ix");
static_assert(offsetof(Z80Registers, iy) - kBase == offsetof(Z80nCpuRegisterFile, iy), "iy");
static_assert(offsetof(Z80Registers, alt) - kBase == offsetof(Z80nCpuRegisterFile, bcAlt), "alt");
static_assert(offsetof(Z80Registers, memptr) - kBase == offsetof(Z80nCpuRegisterFile, memptr), "memptr");
static_assert(offsetof(Z80Registers, q) - kBase == offsetof(Z80nCpuRegisterFile, q), "q");
static_assert(offsetof(Z80Registers, eipos) - kBase == offsetof(Z80nCpuRegisterFile, reservedEipos), "eipos");
static_assert(offsetof(Z80Registers, haltpos) - kBase == offsetof(Z80nCpuRegisterFile, reservedHaltpos), "haltpos");
static_assert(offsetof(Z80Registers, im) - kBase == offsetof(Z80nCpuRegisterFile, im), "im");
static_assert(offsetof(Z80Registers, nmi_in_progress) - kBase == offsetof(Z80nCpuRegisterFile, nmiInProgress), "nmi");
static_assert(sizeof(Z80Registers) - kBase == sizeof(Z80nCpuRegisterFile), "the block ends at nmi_in_progress");

// Z80State::boundary and the library's boundary register use the same values
static_assert(int{Z80_BOUNDARY_PREFIX_DD} == int{Z80nCpuBoundaryPrefixDd} && int{Z80_BOUNDARY_PREFIX_FD} == int{Z80nCpuBoundaryPrefixFd} &&
                  int{Z80_BOUNDARY_INT_SHADOW} == int{Z80nCpuBoundaryIntShadow} &&
                  int{Z80_BOUNDARY_LD_A_IR} == int{Z80nCpuBoundaryLdAIr} && int{Z80_BOUNDARY_NMI_ACK} == int{Z80nCpuBoundaryNmiAck},
              "boundary values");

/// endregion </Register file layout contract>

Z80NEngine::Z80NEngine(EmulatorContext* context, Z80* cpu) : _context(context), _z80(cpu), _memory(context->pMemory), _cpu(Z80nCpuCreate())
{
    Z80nCpuReset(_cpu);
    Z80nCpuAttachRegisterFile(_cpu, reinterpret_cast<Z80nCpuRegisterFile*>(&_z80->pc));
    Z80nCpuSetMemoryBus(_cpu, &MemRead, this, &MemWrite, this);
    Z80nCpuSetPortBus(_cpu, &PortIn, this, &PortOut, this);
    Z80nCpuSetContendFn(_cpu, &Contend, this);
    Z80nCpuSetIntVectorFn(_cpu, &IntVector, this);
    Z80nCpuSetRetiFn(_cpu, &Reti, this);
    Z80nCpuSetRetnFn(_cpu, &Retn, this);
    Z80nCpuSetNextRegFn(_cpu, &NextReg, this);
    Z80nCpuSetT80Persona(_cpu, 1);  // the Next's CPU is the FPGA core's T80: its flags, not the Zilog die's
}

Z80NEngine::~Z80NEngine()
{
    Uninstall();
    // The host's registers stay where they are; the library gets its own copy back
    Z80nCpuAttachRegisterFile(_cpu, nullptr);
    Z80nCpuDestroy(_cpu);
}

void Z80NEngine::Install()
{
    _boundarySeen = 0xFF;  // the host's boundary is pushed at the next step
    _z80->SetEngine(this);
}

void Z80NEngine::Uninstall()
{
    if (_z80->GetEngine() == this)
        _z80->SetEngine(nullptr);
}

bool Z80NEngine::IsInstalled() const
{
    return _z80->GetEngine() == this;
}

/// region <Time and boundary>

void Z80NEngine::Enter()
{
    if (_z80->boundary != _boundarySeen)
        Z80nCpuSetReg(_cpu, Z80nCpuRegBoundary, _z80->boundary);
    Z80nCpuSetTstates(_cpu, _z80->t);
}

void Z80NEngine::Leave(bool wasHalted)
{
    _z80->tt = (Z80nCpuTstates(_cpu) << 8) | (_z80->tt & 0xFF);
    _boundarySeen = static_cast<uint8_t>(Z80nCpuGetReg(_cpu, Z80nCpuRegBoundary));
    _z80->boundary = _boundarySeen;

    const uint16_t opword = Z80nCpuOpcodeWord(_cpu);
    _z80->opcode = static_cast<uint8_t>(opword);
    _z80->prefix = static_cast<uint16_t>(opword >> 8);

    // HALT entry, as the native op_76 records it
    if (!wasHalted && (_z80->halted & 1))
        _z80->haltpos = static_cast<uint16_t>(_z80->t);
}

// The host counts tt in 1/256 T (Z80::rate is 256): T << 8 keeps the fraction t_l
void Z80NEngine::Publish(Z80nCPU* cpu)
{
    _z80->tt = (Z80nCpuTstates(cpu) << 8) | (_z80->tt & 0xFF);
}

void Z80NEngine::Absorb(Z80nCPU* cpu)
{
    Z80nCpuSetTstates(cpu, _z80->tt >> 8);
}

/// endregion </Time and boundary>

/// region <ICpuEngine>

void Z80NEngine::ExecuteStep()
{
    const bool wasHalted = (_z80->halted & 1) != 0;
    Enter();
    Z80nCpuStep(_cpu);
    Leave(wasHalted);
    if (_nextRegHost)
        _nextRegHost->AfterInstruction();
}

void Z80NEngine::AcknowledgeInterrupt(uint8_t vector)
{
    Enter();
    _vector = vector;
    Z80nCpuInt(_cpu);  // Z80::ProcessInterrupts accepted it with the same boundary rules
    Leave(true);
}

void Z80NEngine::AcknowledgeNmi()
{
    Enter();
    Z80nCpuNmi(_cpu);
    Leave(true);
}

/// endregion </ICpuEngine>

/// region <Bus callbacks>

uint8_t Z80NEngine::MemRead(Z80nCPU* cpu, uint16_t addr, Z80nCpuAccessKind kind, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    Z80& z = *e._z80;
    e.Publish(cpu);

    uint8_t value;
    if (kind == Z80nCpuAccessM1)
    {
        // Z80::m1_cycle's host half: board logic before the fetch, the fetch through MemoryReadM1, board logic at the refresh edge
        if (z.machineM1Hook)
            z.NotifyMachineM1Before(addr);
        value = (e._memory->*z.MemIf->MemoryReadM1)(addr, true);
        if (z.machineM1Hook)
            z.NotifyMachineM1(addr);
    }
    else
        value = (e._memory->*z.MemIf->MemoryRead)(addr, kind == Z80nCpuAccessOperand);

    if (z.busTraceHook)
        z.busTraceHook('R', addr, value);

    e.Absorb(cpu);
    return value;
}

// The memory and port cycles take their waits in the memory interface and Z80::in / out; the internal T-states with an address on
// the bus (INC (HL), the IR cycle after M1, PUSH, JR's displacement ...) wait here. The 48K / 128K form of zxula.vhd holds the clock
// whenever the address on the bus is contended and MREQ was not active at the last edge (o_cpu_contend, 590), so these wait like a
// memory cycle's first T; the +3 form waits on MREQ cycles only (600). POC 036 measured it on the simulated core
int Z80NEngine::Contend(Z80nCPU* cpu, uint16_t addr, Z80nCpuAccessKind kind, void* user)
{
    if (kind != Z80nCpuAccessInternal)
        return 0;
    const Z80NEngine& e = *static_cast<const Z80NEngine*>(user);
    const UlaContention* ula = e._z80->ioContention;  // null without contention and on the +3 gate array (Core::SelectMemoryInterface)
    if (!ula || !ula->IsContendedAt(addr))
        return 0;
    return ula->DelayAt(static_cast<uint32_t>(Z80nCpuTstates(cpu)));
}

void Z80NEngine::MemWrite(Z80nCPU* cpu, uint16_t addr, uint8_t value, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    Z80& z = *e._z80;
    e.Publish(cpu);
    (e._memory->*z.MemIf->MemoryWrite)(addr, value);
    if (z.busTraceHook)
        z.busTraceHook('W', addr, value);
    e.Absorb(cpu);
}

uint8_t Z80NEngine::PortIn(Z80nCPU* cpu, uint16_t port, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    e.Publish(cpu);
    const uint8_t value = e._z80->in(port);
    e.Absorb(cpu);
    return value;
}

void Z80NEngine::PortOut(Z80nCPU* cpu, uint16_t port, uint8_t value, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    e.Publish(cpu);
    e._z80->out(port, value);
    e.Absorb(cpu);
}

uint8_t Z80NEngine::IntVector(Z80nCPU*, void* user)
{
    return static_cast<Z80NEngine*>(user)->_vector;
}

void Z80NEngine::Reti(Z80nCPU*, void* user)
{
    // The host's interrupt source learns of RETI as it does from the native interpreter
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    if (IInterruptSource* source = e._z80->GetInterruptSource())
        source->OnReti();
}

void Z80NEngine::SetStacklessNmi(bool on, INmiReturnStore* store)
{
    _nmiStore = store;
    Z80nCpuSetStacklessNmi(_cpu, on && store ? 1 : 0, &NmiStore, &NmiLoad, this);
}

void Z80NEngine::NmiStore(Z80nCPU*, uint8_t low, uint8_t high, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    if (e._nmiStore)
        e._nmiStore->StoreNmiReturn(low, high);
}

uint16_t Z80NEngine::NmiLoad(Z80nCPU*, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    return e._nmiStore ? e._nmiStore->LoadNmiReturn() : 0;
}

void Z80NEngine::Retn(Z80nCPU*, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    if (e._nextRegHost)
        e._nextRegHost->OnRetn();
}

void Z80NEngine::NextReg(Z80nCPU* cpu, uint8_t reg, uint8_t value, void* user)
{
    Z80NEngine& e = *static_cast<Z80NEngine*>(user);
    if (!e._nextRegHost)
        return;
    e.Publish(cpu);
    constexpr uint16_t kPortRegSelect = 0x243B;  // the NextREG select port
    constexpr uint16_t kPortRegData = 0x253B;    // the NextREG data port
    // TTD: the NEXTREG instruction has no bus cycle, so the port journal would never see it. It is journaled as the pair of writes the
    // register interface is (OUT #243B,reg then OUT #253B,value), so the queries that answer for ports ("the last write to #253B") answer
    // for it as well; a recording and its replay make the same two calls
    if (ttd::TTDPortJournal* journal = e._context->ttdPortWrites) [[unlikely]]
    {
        const uint64_t frame = e._context->emulatorState.frame_counter;
        const uint32_t tInFrame = e._context->emulatorState.TtdTInFrame(e._z80->t);
        journal->OnWrite(kPortRegSelect, reg, frame, tInFrame, e._z80->m1_pc);
        journal->OnWrite(kPortRegData, value, frame, tInFrame, e._z80->m1_pc);
    }
    // The port breakpoints and the port trace see the same pair
    if (PortDecoder* ports = e._context->pPortDecoder)
    {
        ports->OnPortWriteWithoutBusCycle(kPortRegSelect, reg, e._z80->m1_pc);
        ports->OnPortWriteWithoutBusCycle(kPortRegData, value, e._z80->m1_pc);
    }
    e._nextRegHost->WriteNextReg(reg, value);
    e.Absorb(cpu);
}

/// endregion </Bus callbacks>
