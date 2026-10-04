#include "stdafx.h"

#include "z84c15engine.h"

#include <cstddef>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// region <Register file layout contract>

// The library executes on Z80Registers from `pc` on (Z84CpuAttachRegisterFile):
// every field of Z84CpuRegisterFile at the same offset from `pc`
// CPU-LIBRARY-MIGRATION(register-file): the same contract, for the main CPU's engine
namespace
{
constexpr size_t kBase = offsetof(Z80Registers, pc);
}
static_assert(offsetof(Z80Registers, sp) - kBase == offsetof(Z84CpuRegisterFile, sp), "sp");
static_assert(offsetof(Z80Registers, ir_) - kBase == offsetof(Z84CpuRegisterFile, rLow), "r_low");
static_assert(offsetof(Z80Registers, int_flags) - kBase == offsetof(Z84CpuRegisterFile, rHi), "r_hi");
static_assert(offsetof(Z80Registers, bc) - kBase == offsetof(Z84CpuRegisterFile, bc), "bc");
static_assert(offsetof(Z80Registers, de) - kBase == offsetof(Z84CpuRegisterFile, de), "de");
static_assert(offsetof(Z80Registers, hl) - kBase == offsetof(Z84CpuRegisterFile, hl), "hl");
static_assert(offsetof(Z80Registers, af) - kBase == offsetof(Z84CpuRegisterFile, af), "af");
static_assert(offsetof(Z80Registers, ix) - kBase == offsetof(Z84CpuRegisterFile, ix), "ix");
static_assert(offsetof(Z80Registers, iy) - kBase == offsetof(Z84CpuRegisterFile, iy), "iy");
static_assert(offsetof(Z80Registers, alt) - kBase == offsetof(Z84CpuRegisterFile, bcAlt), "alt");
static_assert(offsetof(Z80Registers, memptr) - kBase == offsetof(Z84CpuRegisterFile, memptr), "memptr");
static_assert(offsetof(Z80Registers, q) - kBase == offsetof(Z84CpuRegisterFile, q), "q");
static_assert(offsetof(Z80Registers, eipos) - kBase == offsetof(Z84CpuRegisterFile, reservedEipos), "eipos");
static_assert(offsetof(Z80Registers, haltpos) - kBase == offsetof(Z84CpuRegisterFile, reservedHaltpos), "haltpos");
static_assert(offsetof(Z80Registers, im) - kBase == offsetof(Z84CpuRegisterFile, im), "im");
static_assert(offsetof(Z80Registers, nmi_in_progress) - kBase == offsetof(Z84CpuRegisterFile, nmiInProgress), "nmi");
static_assert(sizeof(Z80Registers) - kBase == sizeof(Z84CpuRegisterFile), "the block ends at nmi_in_progress");

// Z80State::boundary and the library's boundary register use the same values
static_assert(int{Z80_BOUNDARY_PREFIX_DD} == int{Z84CpuBoundaryPrefixDd} && int{Z80_BOUNDARY_PREFIX_FD} == int{Z84CpuBoundaryPrefixFd} &&
                  int{Z80_BOUNDARY_INT_SHADOW} == int{Z84CpuBoundaryIntShadow} &&
                  int{Z80_BOUNDARY_LD_A_IR} == int{Z84CpuBoundaryLdAIr} && int{Z80_BOUNDARY_NMI_ACK} == int{Z84CpuBoundaryNmiAck},
              "boundary values");

/// endregion </Register file layout contract>

Z84C15Engine::Z84C15Engine(EmulatorContext* context, Z80* cpu, Z84Lib::Z84C15& chip)
    : _z80(cpu), _memory(context->pMemory), _chip(chip)
{
    Z84CPU* core = _chip.Cpu();
    Z84CpuAttachRegisterFile(core, reinterpret_cast<Z84CpuRegisterFile*>(&_z80->pc));
    Z84CpuSetMemoryBus(core, &MemRead, this, &MemWrite, this);
    Z84CpuSetPortBus(core, &PortIn, this, &PortOut, this);
    Z84CpuSetIntVectorFn(core, &IntVector, this);
    Z84CpuSetRetiFn(core, &Reti, this);
}

Z84C15Engine::~Z84C15Engine()
{
    Uninstall();
    // The host's registers stay where they are; the library gets its own copy back
    Z84CpuAttachRegisterFile(_chip.Cpu(), nullptr);
}

void Z84C15Engine::Install(IInterruptSource* external)
{
    _external = external;
    _boundarySeen = 0xFF;  // the host's boundary is pushed at the next step
    _z80->SetEngine(this);
    _z80->SetInterruptSource(&_source);
}

void Z84C15Engine::Uninstall()
{
    if (_z80->GetEngine() == this)
        _z80->SetEngine(nullptr);
    if (_z80->GetInterruptSource() == &_source)
        _z80->SetInterruptSource(nullptr);
}

bool Z84C15Engine::IsInstalled() const
{
    return _z80->GetEngine() == this;
}

/// region <Time and boundary>

void Z84C15Engine::Enter()
{
    Z84CPU* core = _chip.Cpu();
    if (_z80->boundary != _boundarySeen)
        Z84CpuSetReg(core, Z84CpuRegBoundary, _z80->boundary);
    Z84CpuSetTstates(core, _z80->t);
}

void Z84C15Engine::Leave(bool wasHalted)
{
    Z84CPU* core = _chip.Cpu();
    _z80->tt = (Z84CpuTstates(core) << 8) | (_z80->tt & 0xFF);
    _boundarySeen = static_cast<uint8_t>(Z84CpuGetReg(core, Z84CpuRegBoundary));
    _z80->boundary = _boundarySeen;

    const uint16_t opword = Z84CpuOpcodeWord(core);
    _z80->opcode = static_cast<uint8_t>(opword);
    _z80->prefix = static_cast<uint16_t>(opword >> 8);

    // HALT entry, as the native op_76 records it
    if (!wasHalted && (_z80->halted & 1))
        _z80->haltpos = static_cast<uint16_t>(_z80->t);
}

// The host counts tt in 1/256 T (Z80::rate is 256): T << 8 keeps the fraction t_l
void Z84C15Engine::Publish(Z84CPU* cpu)
{
    _z80->tt = (Z84CpuTstates(cpu) << 8) | (_z80->tt & 0xFF);
}

void Z84C15Engine::Absorb(Z84CPU* cpu)
{
    Z84CpuSetTstates(cpu, _z80->tt >> 8);
}

/// endregion </Time and boundary>

/// region <ICpuEngine>

void Z84C15Engine::ExecuteStep()
{
    const bool wasHalted = (_z80->halted & 1) != 0;
    Enter();
    Z84CpuStep(_chip.Cpu());
    Leave(wasHalted);
}

void Z84C15Engine::AcknowledgeInterrupt(uint8_t vector)
{
    if (_agent)
        _agent->OnInterruptAcknowledge();
    Enter();
    _vector = vector;
    Z84CpuInt(_chip.Cpu());  // Z80::ProcessInterrupts accepted it with the same boundary rules
    Leave(true);
}

void Z84C15Engine::AcknowledgeNmi()
{
    Enter();
    Z84CpuNmi(_chip.Cpu());
    Leave(true);
}

/// endregion </ICpuEngine>

/// region <Bus callbacks>

uint8_t Z84C15Engine::MemRead(Z84CPU* cpu, uint16_t addr, Z84CpuAccessKind kind, void* user)
{
    Z84C15Engine& e = *static_cast<Z84C15Engine*>(user);
    Z80& z = *e._z80;
    e.Publish(cpu);

    uint8_t value;
    if (kind == Z84CpuAccessM1)
    {
        // Z80::m1_cycle's host half: board logic before the fetch (the Sprinter's DOS signal), the
        // fetch through MemoryReadM1, board logic at the refresh edge
        if (z.machineM1Hook)
            z.NotifyMachineM1Before(addr);
        value = (e._memory->*z.MemIf->MemoryReadM1)(addr, true);
        if (z.machineM1Hook)
            z.NotifyMachineM1(addr);
        if (e._agent)
            e._agent->OnOpcodeFetch(addr, value);
    }
    else
    {
        value = (e._memory->*z.MemIf->MemoryRead)(addr, kind == Z84CpuAccessOperand);
        if (e._agent && e._agent->watchData)
            value = e._agent->OnRead(addr, value);
    }

    if (z.busTraceHook)
        z.busTraceHook('R', addr, value);

    e.Absorb(cpu);
    return value;
}

void Z84C15Engine::MemWrite(Z84CPU* cpu, uint16_t addr, uint8_t value, void* user)
{
    Z84C15Engine& e = *static_cast<Z84C15Engine*>(user);
    Z80& z = *e._z80;
    e.Publish(cpu);
    IZ84BusAgent* agent = (e._agent && e._agent->watchData) ? e._agent : nullptr;
    if (agent)
        value = agent->BeforeWrite(addr, value);
    (e._memory->*z.MemIf->MemoryWrite)(addr, value);
    if (agent)
        agent->AfterWrite(addr, value);
    if (z.busTraceHook)
        z.busTraceHook('W', addr, value);
    e.Absorb(cpu);
}

uint8_t Z84C15Engine::PortIn(Z84CPU* cpu, uint16_t port, void* user)
{
    Z84C15Engine& e = *static_cast<Z84C15Engine*>(user);
    e.Publish(cpu);
    const uint8_t value = e._z80->in(port);
    e.Absorb(cpu);
    return value;
}

void Z84C15Engine::PortOut(Z84CPU* cpu, uint16_t port, uint8_t value, void* user)
{
    Z84C15Engine& e = *static_cast<Z84C15Engine*>(user);
    e.Publish(cpu);
    e._z80->out(port, value);
    e.Absorb(cpu);
}

uint8_t Z84C15Engine::IntVector(Z84CPU*, void* user)
{
    return static_cast<Z84C15Engine*>(user)->_vector;
}

void Z84C15Engine::Reti(Z84CPU*, void* user)
{
    // The on-chip chain already saw RETI inside the library; the board's devices now
    Z84C15Engine& e = *static_cast<Z84C15Engine*>(user);
    if (e._observer)
        e._observer->OnChipReti();
    if (e._external)
        e._external->OnReti();
}

/// endregion </Bus callbacks>

/// region <Interrupt source: the on-chip daisy chain, then the board's /INT>

bool Z84C15Engine::ChainSource::IsIntAsserted(uint32_t t)
{
    const bool chip = _engine._chip.IntPending();
    const bool board = _engine._external && _engine._external->IsIntAsserted(t);
    return chip || board;
}

uint8_t Z84C15Engine::ChainSource::AcknowledgeInterrupt(uint32_t t)
{
    // The chain has priority over the board's /INT (MAME tmpz84c015: "ctc -> sio -> pio -> ext"): the chip drives the
    // vector. The board still sees the acknowledge cycle (/M1 with /IORQ): its own INT logic reacts to it as to any
    // INTA - the Sprinter's PLD presets its INT flip-flop on (/IO or /M1) = 0 (SP2_1K30.TDF:744), so a frame or
    // keyboard INT pending at the same moment ends there; the byte it would answer is not on the bus
    if (_engine._chip.IntPending())
    {
        const uint8_t vector = _engine._chip.AcknowledgeInterrupt();
        if (_engine._external)
            (void)_engine._external->AcknowledgeInterrupt(t);
        if (_engine._observer)
            _engine._observer->OnChipAcknowledge(vector);
        return vector;
    }
    return _engine._external ? _engine._external->AcknowledgeInterrupt(t) : 0xFF;
}

void Z84C15Engine::ChainSource::OnReti()
{
    // Only the native interpreter calls this; on the engine RETI comes through the library (Reti)
    _engine._chip.OnReti();
    if (_engine._observer)
        _engine._observer->OnChipReti();
    if (_engine._external)
        _engine._external->OnReti();
}

/// endregion </Interrupt source>
