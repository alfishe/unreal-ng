#include "stdafx.h"

#include "ttdsessionport.h"

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

namespace ttd
{
using coordinator::MachineMode;
using coordinator::SessionEffect;

MachineMode TTDSessionPort::Mode() const
{
    ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    if (ttd == nullptr)
        return MachineMode::Live;
    // A recording the user started is the one the guard protects, also while it is paused for browsing (D8, D43):
    // the same question the media refusals asked before
    if (!ttd->RecordingGuard(TTDGuardedAction::LoadDisk).empty())
        return MachineMode::Recording;
    if (ttd->IsReplayActive())
        return MachineMode::Replaying;
    if (ttd->GetState() == TTDSessionState::Detached)
        return MachineMode::Detached;
    if (ttd->IsRecording())
        return MachineMode::BlackBox;   // a background recording: the black box, a debugger's history
    return MachineMode::Live;
}

bool TTDSessionPort::Replaying() const
{
    return _context && _context->ttdReplayActive;
}

void NoteSessionChange(EmulatorContext* context)
{
    if (!context)
        return;
    if (context->pMachineOps)
    {
        context->pMachineOps->SessionChanged();
        return;
    }
    coordinator::ModeState to;
    to.replaying = context->ttdReplayActive;
    if (context->pFeatureManager)
        context->pFeatureManager->OnModeChanged({}, to);
    else if (to.replaying && context->pCore)
    {
        context->pCore->GetZ80()->isDebugMode = true;
        context->pCore->SelectMemoryInterface();
    }
}

void TTDSessionPort::Apply(SessionEffect effect, const std::string& reason)
{
    ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr;
    if (ttd == nullptr)
        return;
    switch (effect)
    {
        case SessionEffect::None:
            break;
        case SessionEffect::EndKeepHistory:
            ttd->EndSession(reason.c_str());
            break;
        case SessionEffect::StopThenMediaChanged:
            ttd->StopRecording();
            ttd->EndSessionFor(TTDChange::Media, reason.c_str());
            break;
        case SessionEffect::MediaChanged:
            ttd->EndSessionFor(TTDChange::Media, reason.c_str());
            break;
        case SessionEffect::MachineChanged:
            ttd->EndSessionFor(TTDChange::Machine, reason.c_str());
            break;
        case SessionEffect::TimingChanged:
            ttd->EndSessionFor(TTDChange::Timing, reason.c_str());
            break;
    }
}

void TTDSessionPort::MachineReset()
{
    if (ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr)
        ttd->OnMachineReset();
}
}  // namespace ttd
