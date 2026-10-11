#pragma once

/// @file ttdsessionport.h
/// @brief The TTD session as the machine state coordinator sees it (coordinator::ISessionSink): the machine's
/// mode and the policy's session effects, mapped onto ITimeTravelHooks (architecture.md §3).
///
/// Works with whichever implementation is behind EmulatorContext::pTimeTravelHooks (the engine's controller, or
/// v1 where a test selects it), read at each call. M1 maps each effect onto the hook call its callers made
/// before (EndSession, OnLoad(Media), StopRecording, OnMachineReset), so the session behaves as it did.

#include "emulator/coordinator/ports.h"

class EmulatorContext;

namespace ttd
{
class TTDSessionPort : public coordinator::ISessionSink
{
public:
    explicit TTDSessionPort(EmulatorContext* context) : _context(context) {}

    coordinator::MachineMode Mode() const override;
    bool Replaying() const override;
    void Apply(coordinator::SessionEffect effect, const std::string& reason) override;
    void MachineReset() override;

private:
    EmulatorContext* _context;
};

/// TTD's state changed (a transition, a replay entered or left, a recording paused or resumed): the coordinator
/// reads the mode again and broadcasts a change (architecture §5, M4). A context without a coordinator (a unit
/// test's bare context): the feature manager hears the replay directly, or, without one, the CPU's debug mode
/// follows the replay
void NoteSessionChange(EmulatorContext* context);
}  // namespace ttd
