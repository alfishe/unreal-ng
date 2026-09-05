#include "stdafx.h"

#include "emulator/io/tape/tapeturbocontroller.h"

#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / destructors>

TapeTurboController::TapeTurboController(EmulatorContext* context, Tape& tape)
    : _context(context), _tape(tape)
{
    _logger = context->pModuleLogger;
}

TapeTurboController::~TapeTurboController()
{
    // Defensive: never leave a warp we own behind a destructing Core
    if (_autoTurboActive && _context && _context->pCore)
    {
        _context->pCore->DisableTurboMode();
        _autoTurboActive = false;
    }

    _context = nullptr;
}

/// endregion </Constructors / destructors>

/// region <Controller interface>

void TapeTurboController::handleFrameEnd()
{
    // Degenerate contexts (no Core yet) cannot warp — nothing to orchestrate
    if (!_context || !_context->pCore)
        return;

    const TapePlaybackState state = _tape.GetPlaybackState();
    const bool playing = (state == TapePlaybackState::Playing);

    // Playback fully over (watchdog freeze, end-of-tape, stop/eject): a fresh
    // playback session starts with a clean slate (E2/E3 recovery)
    if (!playing)
        _suppressedThisSession = false;

    if (_autoTurboActive && !_context->pCore->IsTurboMode())
    {
        // E6: the warp we engaged was switched off from outside (the user's
        // manual toggle) — treat that as an explicit "no warp" vote for the
        // remainder of this playback session and stand down without touching
        // anything else
        _autoTurboActive = false;
        if (playing)
            _suppressedThisSession = true;
    }
    else if (_autoTurboActive && (!playing || !IsFeatureEnabled()))
    {
        // E2/E3/E4: playback ended or the feature was toggled off — disable
        // the turbo we own and return to normal speed
        _context->pCore->DisableTurboMode();
        _autoTurboActive = false;
    }
    else if (!_autoTurboActive && playing && IsFeatureEnabled() && !_context->pCore->IsTurboMode() &&
             !_suppressedThisSession)
    {
        // E1: signal playback is running (user Play, $0564 anchor or sustained
        // EAR-polling resume) and nobody else owns turbo — engage warp. Silent
        // by design (turbo without audio); the read-gap watchdog will bring
        // the machine back to 50 Hz within ~3 emulated seconds of the loader
        // going quiet (design §4.1)
        _context->pCore->EnableTurboMode(false);
        _autoTurboActive = true;
    }
    // else: E5 — turbo is on but not ours (manual). Leave it completely alone.
}

/// endregion </Controller interface>

/// region <Helper methods>

bool TapeTurboController::IsFeatureEnabled() const
{
    // Live lookup — CLI / WebAPI / Qt toggles take effect at the next frame.
    // Every real Emulator instance owns a FeatureManager; the null case only
    // arises in degenerate contexts that must not warp anyway (same lazy-gate
    // discipline as TapeFastLoad::IsArmed).
    FeatureManager* featureManager = _context ? _context->pFeatureManager : nullptr;
    return featureManager != nullptr && featureManager->isEnabled(Features::kTurboTape);
}

/// endregion </Helper methods>
