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
    void Apply(coordinator::SessionEffect effect, const std::string& reason) override;
    void MachineReset() override;

private:
    EmulatorContext* _context;
};
}  // namespace ttd
