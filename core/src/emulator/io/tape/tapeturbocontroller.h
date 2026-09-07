#pragma once

#include "stdafx.h"

#include "emulator/io/tape/tape.h"

class EmulatorContext;
class ModuleLogger;

/// Turbo tape loading controller (design:
/// docs/inprogress/2026-09-04-turbo-tape-loading).
///
/// Tiny orchestrator that rides the existing tape state machine: while signal
/// playback is Running (user Play, the $0564 ROM anchor or the sustained
/// EAR-polling resume started it), it engages the emulator's turbo mode so the
/// real loader decodes at warp speed; when playback leaves Running — read-gap
/// watchdog freeze, natural end-of-tape, ERR_NR stop, manual stop/eject — it
/// stands back down to normal speed. Machine timing per frame is untouched by
/// turbo mode, so a warp load is byte- and frame-identical to a real-speed
/// load (design §3.1); this controller only decides *when* the wall-clock
/// throttle is absent.
///
/// Ownership contract (design §6.2): the controller only ever disables a turbo
/// it engaged itself (`_autoTurboActive`). A manual turbo toggle by the user is
/// never overridden, and a manual disable during playback suppresses auto-warp
/// for the rest of that playback session (E6) — otherwise warp could not be
/// turned off while a tape plays.
///
/// Ticked once per emulated frame from MainLoop::OnFrameEnd(), immediately
/// after Tape::handleFrameEnd() so every transition (watchdog freeze included)
/// is observed in the same frame it happens — worst-case reaction latency is
/// one frame (design §6.1).
class TapeTurboController
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_TAPE;
    ModuleLogger* _logger;
    /// endregion </ModuleLogger definitions>

    /// region <Fields>
protected:
    EmulatorContext* _context;
    Tape& _tape;

    // Turbo we engaged — the only turbo we may disable (ownership guard, E6)
    bool _autoTurboActive = false;

    // Manual turbo interaction during this playback session vetoes auto-warp
    // until playback fully stops (design §6.2)
    bool _suppressedThisSession = false;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    TapeTurboController(EmulatorContext* context, Tape& tape);
    virtual ~TapeTurboController();
    /// endregion </Constructors / destructors>

    /// region <Controller interface>
public:
    /// Per-frame evaluation of the engage/disengage matrix (design §6.1 rows
    /// E1-E6). Call after Tape::handleFrameEnd() so the state read is the
    /// post-transition one.
    void handleFrameEnd();

    /// Whether warp currently engaged by this controller is active.
    bool IsAutoTurboActive() const { return _autoTurboActive; }
    /// endregion </Controller interface>

    /// region <Helper methods>
protected:
    /// Live 'turbotape' feature lookup — CLI / WebAPI / Qt toggles take effect
    /// at the very next frame. A missing FeatureManager (degenerate contexts)
    /// means no auto-warp, mirroring the trap's lazy gate (design §6.1).
    bool IsFeatureEnabled() const;
    /// endregion </Helper methods>
};

// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
class TapeTurboControllerCUT : public TapeTurboController
{
public:
    TapeTurboControllerCUT(EmulatorContext* context, Tape& tape) : TapeTurboController(context, tape) {};

    bool Suppressed() const { return _suppressedThisSession; };
};
