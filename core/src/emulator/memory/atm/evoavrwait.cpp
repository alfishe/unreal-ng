#include "stdafx.h"

#include "evoavrwait.h"

#include <algorithm>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

uint32_t EvoAvrWait::Access(uint32_t service, uint32_t after, uint64_t now, uint32_t baseClockHz, Eeprom eeprom)
{
    if (!_timing.avrClockHz || !baseClockHz)
        return 0;
    // The AVR's clock, absolute, from the base-clock T-states
    const uint64_t avrNow = now * _timing.avrClockHz / baseClockHz;
    // More than a second ahead is no AVR work (an EEPROM write is 8.5 ms): the time base started again under the
    // model (a snapshot load); the phase is unknown, start from here
    const uint64_t horizon = avrNow + _timing.avrClockHz;
    if (_state.loopResume > horizon || _state.eepromReadyAt > horizon)
        _state = State{};
    // Still busy after the previous access (a Gluk write's I2C / EEPROM work): the interrupt is taken at once, the
    // flag waits for the work and then a whole pass
    const uint64_t busy = _state.loopResume > avrNow ? _state.loopResume - avrNow : 0;
    const uint64_t elapsed = avrNow > _state.loopResume ? avrNow - _state.loopResume : 0;
    const uint32_t checks = _timing.waitChecksPerLoop ? _timing.waitChecksPerLoop : 1;
    const uint32_t loop = std::max<uint32_t>(1, (_timing.loopCycles ? _timing.loopCycles : 1) / checks);
    // The main loop looks at the flag once per pass (TS: after each task), and a pass starts when the previous
    // access is done: right behind it a whole pass is left, long after it anywhere in one. The interrupt steals its
    // cycles from the loop on top (reference-evo-com-port.md §3)
    const uint32_t phase = loop - static_cast<uint32_t>(elapsed % loop);
    const uint64_t serveAt = avrNow + _timing.isrCycles + busy + phase;
    uint64_t release = serveAt + service;
    if (eeprom == Eeprom::Read && _state.eepromReadyAt > serveAt)
        release += _state.eepromReadyAt - serveAt;   // read_eeprom: eeprom_busy_wait() before the release
    uint64_t resume = release + after;
    if (eeprom == Eeprom::Write)
    {
        // write_eeprom after the release: eeprom_busy_wait(), then the write starts and runs on its own
        const uint64_t start = std::max(release, _state.eepromReadyAt);
        resume = start + after;
        _state.eepromReadyAt = start + static_cast<uint64_t>(_timing.avrClockHz) * kEepromWriteMicros / 1000000;
    }
    _state.loopResume = resume;
    return static_cast<uint32_t>(release - avrNow);
}

uint32_t EvoAvrWait::ToCpuClocks(uint32_t avrCycles, uint64_t cpuHz) const
{
    const uint32_t avrHz = _timing.avrClockHz ? _timing.avrClockHz : 1;
    return static_cast<uint32_t>((static_cast<uint64_t>(avrCycles) * cpuHz + avrHz - 1) / avrHz);
}

uint64_t EvoAvrWait::BaseNow(const EmulatorContext* context)
{
    if (!context || !context->pCore || !context->pCore->GetZ80())
        return 0;
    const uint32_t multiplier = context->emulatorState.current_z80_frequency_multiplier
                                    ? context->emulatorState.current_z80_frequency_multiplier
                                    : 1u;
    return context->emulatorState.t_states + context->pCore->GetZ80()->t / multiplier;
}

uint64_t EvoAvrWait::CpuHz(const EmulatorContext* context)
{
    if (!context)
        return 0;
    return context->emulatorState.current_z80_frequency ? context->emulatorState.current_z80_frequency
                                                        : context->emulatorState.base_z80_frequency;
}

void EvoAvrWait::HoldCpu(EmulatorContext* context, uint32_t avrCycles) const
{
    if (!avrCycles || !context || !context->pCore || !context->pCore->GetZ80())
        return;
    context->pCore->GetZ80()->AddWaitStates(ToCpuClocks(avrCycles, CpuHz(context)));
}
