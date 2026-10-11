#include "stdafx.h"

#include "ttdsessionport.h"

#include "debugger/ttd/timetravelhooks.h"
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
            ttd->OnLoad(TTDLoadKind::Media, reason.c_str());
            break;
        case SessionEffect::MediaChanged:
            ttd->OnLoad(TTDLoadKind::Media, reason.c_str());
            break;
    }
}

void TTDSessionPort::MachineReset()
{
    if (ITimeTravelHooks* ttd = _context ? _context->pTimeTravelHooks : nullptr)
        ttd->OnMachineReset();
}
}  // namespace ttd
