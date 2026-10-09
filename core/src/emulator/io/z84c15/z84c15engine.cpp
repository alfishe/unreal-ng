#include "stdafx.h"

#include "z84c15engine.h"

#include <algorithm>
#include <cstddef>

#include "emulator/emulatorcontext.h"
#include "emulator/memory/hostbusoverlay.h"
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
    // An idle cycle of a halted CPU (no pending prefix) in a run that allows more in one go
    if (wasHalted && _z80->halted == 1 && _z80->idleSkipLimit != 0 && _idleInOneGo) [[unlikely]]
        RunIdleCycles();
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

/// region <Idle cycles in one go>

void Z84C15Engine::RunIdleCycles()
{
    Z84CPU* core = _chip.Cpu();
    Z80& z = *_z80;
    const uint16_t fetch = static_cast<uint16_t>(z.pc + 1);  // the byte after the HALT (Z84HaltT)
    uint32_t period = 1;
    if (!z.IdleStepsInert() || !IdleFetchIsPure(fetch, _lastM1Value, period))
        return;

    // The cycle's length by its start phase (t mod period), learned from real cycles: only the phases the run
    // reaches, each once
    uint16_t length[kMaxIdlePeriod] = {};
    uint64_t learned = 0;
    uint32_t t = Z84CpuTstates(core);
    uint32_t checkFrom = t;  // boundaries before it need no INT question (the sources' lower bound)
    uint32_t fetches = 0;    // idle cycles run as arithmetic: one R tick each
    for (;;)
    {
        // Where the step loop stops: the driver's limit, the frame end (both may move: a clock switch)
        const uint32_t stop = std::min(z.idleSkipLimit, z._frameLimit);
        if (t >= stop)
            break;
        if (t >= checkFrom)
        {
            // The question ProcessInterrupts asks at this boundary, whatever IFF1 says (the sources read the host's
            // clock; the chip polls its watchdog there). Taken: the step loop goes on from here. Asserted while
            // the CPU cannot take it (DI : HALT): asked again at every boundary, as the step loop would
            Z84CpuSetTstates(core, t);
            Publish(core);
            const bool asserted = _source.IsIntAsserted(t);
            if (asserted && Z84CpuIntPossible(core))
                break;
            checkFrom = asserted ? t + 1 : NextCheckT(t, stop);
        }

        const uint32_t phase = t % period;
        if (!(learned & (uint64_t{1} << phase)))
        {
            // A real idle cycle: its length at this phase
            Z84CpuSetTstates(core, t);
            Z84CpuStep(core);
            const uint32_t after = Z84CpuTstates(core);
            length[phase] = static_cast<uint16_t>(after - t);
            learned |= uint64_t{1} << phase;
            t = after;
            continue;
        }

        // Idle cycles as arithmetic up to the next question: those that start before `target`
        const uint32_t target = std::min(checkFrom, stop);
        if (period == 1)
        {
            const uint32_t n = (target - t + length[0] - 1) / length[0];
            t += n * length[0];
            fetches += n;
            continue;
        }
        // Whole rounds of the phase cycle from here, when every phase on it is learned, then single cycles
        uint32_t roundT = 0;
        uint32_t roundN = 0;
        for (uint32_t p = phase;;)
        {
            if (!(learned & (uint64_t{1} << p)))
            {
                roundT = 0;
                break;
            }
            roundT += length[p];
            roundN++;
            p = (p + length[p]) % period;
            if (p == phase || roundN > period)
                break;
        }
        if (roundT && roundN <= period)
        {
            const uint32_t rounds = (target - t) / roundT;
            t += rounds * roundT;
            fetches += rounds * roundN;
        }
        while (t < target && (learned & (uint64_t{1} << (t % period))))
        {
            t += length[t % period];
            fetches++;
        }
    }

    Z84CpuSetTstates(core, t);
    _idleCyclesInOneGo += fetches;
    if (fetches)
        z.r_low = static_cast<uint8_t>(((z.r_low + fetches) & 0x7F) | (z.r_low & 0x80));  // Z84_R_INC per cycle
}

bool Z84C15Engine::IdleFetchIsPure(uint16_t fetch, uint8_t opcode, uint32_t& period) const
{
    const Z80& z = *_z80;
    // The plain memory interfaces (not contended: a contention table depends on the frame position)
    if (z.MemIf != z.FastMemIf && z.MemIf != z.OverlayFastMemIf)
        return false;
    if (z.machineM1Hook && !z.machineM1Hook->RepeatM1IsInert(fetch))
        return false;
    if (_agent && !_agent->RepeatFetchIsInert(fetch, opcode))
        return false;
    if (!Z84CpuIdleM1Repeats(_chip.Cpu(), opcode) || !_memory->RepeatFetchIsPure(fetch))
        return false;
    period = 1;
    if (z.MemIf == z.OverlayFastMemIf)
    {
        const HostBusOverlay* overlay = _memory->GetBusOverlay();
        if (overlay && overlay->observesReads && fetch >= overlay->windowStart && fetch < overlay->windowEnd &&
            (!overlay->RepeatFetchIsPure(fetch, period) || period == 0 || period > kMaxIdlePeriod))
            return false;
    }
    return true;
}

uint32_t Z84C15Engine::NextCheckT(uint32_t t, uint32_t stop)
{
    uint32_t due = stop;
    if (_external)
        due = std::min(due, _external->NextAssertT(t));
    const uint64_t chipDue = _chip.NextEventClock();
    if (chipDue != UINT64_MAX && due > t)
    {
        if (ChipClockAt(t) >= chipDue)
            due = t;
        else if (ChipClockAt(due - 1) >= chipDue)
        {
            // The first T whose clock reaches it, in (t, due - 1]: the clock never decreases with T
            uint32_t below = t;
            uint32_t reached = due - 1;
            while (reached - below > 1)
            {
                const uint32_t mid = below + (reached - below) / 2;
                if (ChipClockAt(mid) >= chipDue)
                    reached = mid;
                else
                    below = mid;
            }
            due = reached;
        }
    }
    return std::max(due, t + 1);
}

uint64_t Z84C15Engine::ChipClockAt(uint32_t t)
{
    const uint32_t saved = _z80->tt;
    _z80->tt = (t << 8) | (saved & 0xFF);
    const uint64_t clock = _chip.Clock();
    _z80->tt = saved;
    return clock;
}

/// endregion </Idle cycles in one go>

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
        e._lastM1Value = value;
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
