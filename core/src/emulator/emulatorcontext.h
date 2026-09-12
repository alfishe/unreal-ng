#pragma once

#include "stdafx.h"

#include <atomic>
#include <mutex>
#include <string>
#include "common/modulelogger.h"
#include "common/uuid.h"
#include "common/sound/audiodevicedescriptor.h"
#include "emulator/platform.h"
#include "corestate.h"
#include "emulator/io/tape/tape.h"
#include "emulator/notifications.h"

using unreal::UUID;

class Core;
class Emulator;
class Keyboard;
class MainLoop;
class Memory;
class WD1793;
class PortDecoder;
class Screen;
class UlaContention;
class TapeFastLoad;
class TapeTurboController;
class SoundManager;
#ifdef ENABLE_RECORDING
class RecordingManager;
#endif
class DebugManager;
class Z80Disassembler;
class FeatureManager;

// TTD manager lives in the ttd namespace - forward-declare so the context
// can hold a pointer without pulling the full TTD headers into every consumer.
namespace ttd { class TimeTravelManager; class TTDAccessProbe; }

#include "debugger/ttd/ttdprobe.h"  // inline member - needs full definition

// Create callback type for audio
// User in emulator/sound/soundmanager and client/GUI
// void audioCallback(int16_t* samples, size_t numSamples);
typedef void (*AudioCallback)(void* obj, int16_t*, size_t);

class EmulatorContext
{
    /// region <Child object references>
public:
    // Unique identifier for this emulator instance
    unreal::UUID emulatorId;

    // Advanced logger instance
    ModuleLogger* pModuleLogger = nullptr;

	// Global emulator configuration (MemoryRead from ini file)
	CONFIG config;

    // Runtime state
    CoreState coreState;

	// Emulated system state (ports, flags including peripheral devices)
	EmulatorState emulatorState;

	// Temporary state for all extended platform features
	// TODO: rework and put into appropriate platform / state classes
	TEMP temporary;

	// Host system properties / context
	HOST host;

    // Main emulation loop
    MainLoop* pMainLoop = nullptr;

	// Computer system instance
	Core* pCore = nullptr;

	// Keyboard controller instance
	Keyboard* pKeyboard = nullptr;

	// Memory controller instance
	Memory* pMemory = nullptr;

	// Model-specific port decoder
	PortDecoder* pPortDecoder = nullptr;

    // Tape input instance
    Tape* pTape = nullptr;

    // Fast tape loading trap (LD-BYTES $0556 hook) instance
    TapeFastLoad* pTapeFastLoad = nullptr;

    // Turbo tape loading controller (auto-warp while the signal path plays)
    TapeTurboController* pTapeTurboController = nullptr;

    // BDI - Beta Disk Interface controller instance
    WD1793* pBetaDisk = nullptr;

	// Video controller parameters and logic
	Screen* pScreen = nullptr;

	// Standalone ULA contention component (memory/IO contention + floating bus)
	UlaContention* pUlaContention = nullptr;

    // Audio callback (will be triggered after each video frame render and provide audio samples for host system)
    // Using std::atomic to ensure proper memory ordering between UI thread (setting) and emulator thread (reading)
    std::atomic<void*> pAudioManagerObj;
    std::atomic<AudioCallback> pAudioCallback;

    /// Audio ring occupancy cell in STEREO FRAMES, owned by the app-side
    /// sound manager (outlives the emulator) and registered together with the
    /// audio callback. The DRC controller (SoundManager::updateDrcControl)
    /// reads it once per frame as its process variable; MainLoop reads it for
    /// the emergency refill path. nullptr = no audio device attached -> DRC
    /// disengaged (unity bypass).
    std::atomic<const std::atomic<uint32_t>*> pAudioRingOccupancy{nullptr};

    /// Native sample rate of the attached audio device (audio-sync design
    /// Fix 3). 0 = same as CORE_SAMPLING_RATE. The DRC resampler uses
    /// device/core as its base ratio; ring occupancy is in DEVICE-rate frames.
    std::atomic<uint32_t> pAudioDeviceSampleRate{0};

    /// Full realtime-observable device/ring state (audiodevicedescriptor.h),
    /// owned by the frontend's sound manager. Superset of the two cells
    /// above (occupancy cell points INTO it); monitoring consumers (WebAPI,
    /// diagnostics) read it lock-free. nullptr = no device attached.
    std::atomic<const AudioDeviceDescriptor*> pAudioDeviceDescriptor{nullptr};

    /// Video presentation latency in microseconds (EMA), stamped by the GUI
    /// frame source at paint time: wall-clock delta between the emulation
    /// thread latching the finished frame and the GUI copying it for paint.
    /// Together with AudioDeviceDescriptor::audioLatencyMs this yields the
    /// realtime A/V offset (audio late = audioLatency - videoLatency).
    std::atomic<uint32_t> pVideoPresentLatencyUs{0};

    // Sound manager
    SoundManager* pSoundManager = nullptr;

#ifdef ENABLE_RECORDING
    // Recording manager (video/audio capture for recordings)
    RecordingManager* pRecordingManager = nullptr;
#endif

	// Debug manager (includes Breakpoints, Labels and Disassembler)
	DebugManager* pDebugManager = nullptr;

    // Feature toggle manager
    FeatureManager* pFeatureManager = nullptr;

    // Time-travel debugging manager (owned by Emulator, lives across the
    // lifetime of the context). May be null on minimal builds without TTD.
    ttd::TimeTravelManager* pTimeTravelManager = nullptr;

    // TTD silent-replay mode flag (parent TDD 8.2 + Appendix C).
    //
    // Set by TimeTravelManager::EnterReplayMode() before any intra-frame
    // replay (SeekTo with tInFrame > 0, StepBackInstruction, reverse-search
    // probes). Cleared by ExitReplayMode(). Read by every suppression site
    // listed in Appendix C - breakpoints skip, analyzers dispatch no-op,
    // keyboard matrix mutation blocked, recording capture skipped, video
    // frame refresh notifications dropped, audio host buffer muted (device
    // state still advances).
    //
    // Plain bool (not atomic) - replay runs under the existing pause
    // discipline: EnterReplayMode / RunTStates / ExitReplayMode happen on
    // the control thread with the emulator paused, and the suppression
    // checks are read from the same thread.
    bool ttdReplayActive = false;

    // Phase 4 - reverse-search access probe (parent TDD 9.2). Inline
    // instance: every hot-path call site (MemoryWriteDebug, MemoryReadDebug,
    // Z80 M1 cycle, DecodePortOut) reads `ttdProbe.IsArmed()` with one
    // predictable branch. Cost when not armed: ~1 cycle.
    ttd::TTDAccessProbe ttdProbe;

    // Per-frame coverage collection (reverse-search index). Read on the
    // instruction-fetch path, so it is a plain bool rather than a call into
    // TimeTravelManager: one predictable, almost-always-false branch. Set by
    // StartRecording and cleared by StopRecording / InvalidateSession, all of
    // which run on the control thread with the emulator paused.
    bool ttdCoverageActive = false;
    /// endregion </Child object references>

    /// region <Run-control claim (GDB TDD 3.3 / parent TDD 7.2)>
    //
    // Advisory owner token: while a surface (GDB, Qt timeline, WebAPI, ...) holds the claim
    // with the target paused, other surfaces' Resume/Step/Seek/state-writes are refused.
    // Pause and read-only queries are always allowed. The claim is NOT taken automatically
    // by Emulator::Pause() - explicit surfaces take it.
    //
    // Mutex guards take/release only; never held during emulator work.
public:
    struct RunControlState
    {
        bool claimed = false;
        std::string surfaceLabel;  // "gdb", "webapi", "qt-timeline", "lua", "cli", ... for error/UI
        std::string ownerUuid;     // String form for diagnostics / JSON responses
    };

    /// Attempt to take the run-control claim.
    /// @param owner UUID of the claiming surface (use UUID::Generate() at surface startup).
    /// @param surfaceLabel Human-readable label for error messages and UI.
    /// @param errorReason Optional: filled with a reason string when returning false.
    /// @return true if claim taken (or already held by the same owner - idempotent);
    ///         false if held by a different owner.
    bool TakeRunControl(const UUID& owner, const std::string& surfaceLabel,
                        std::string* errorReason = nullptr);

    /// Release the claim. No-op if @p owner does not match the current holder.
    void ReleaseRunControl(const UUID& owner);

    /// Identity check: does @p owner currently hold the claim? No blocking.
    bool HasRunControl(const UUID& owner) const;

    /// Is the claim held by anyone?
    bool IsRunControlClaimed() const;

    /// Snapshot for UI status / diagnostics. Thread-safe.
    RunControlState GetRunControlState() const;

private:
    struct RunControlClaim
    {
        UUID owner;                   // Nil UUID = unclaimed
        std::string surfaceLabel;
        mutable std::mutex mutex;     // Guards take/release only; never held during emulator work
    };
    RunControlClaim _runControlClaim;
    /// endregion </Run-control claim>

    /// region <Parent object references>
public:
    Emulator* pEmulator;
    /// endregion </Parent object references>

    /// region <Timing helpers>
    //
    // config holds the RASTER BASE timings (Pentagon: 71680 T per frame, 224 per
    // line; ZX48/128 and Scorpion: 69888 / 224). The Z80 actually executes those
    // scaled by the composed clock multiplier, so anything reporting or bounding
    // CPU time must scale too - reading config.frame raw reports 3.5 MHz numbers
    // while the CPU is running at 7 or 14 MHz.

    /// @brief Composed CPU clock multiplier relative to the base 3.5 MHz clock
    /// @details Two independent things fold into this value:
    ///          - guest HARDWARE turbo (Scorpion ZS-256 7 MHz, ATM/Profi 14 MHz),
    ///            which keeps the 20 ms frame and genuinely gives the guest N x
    ///            T-states inside it;
    ///          - the host speed control, which instead makes frames run faster in
    ///            wall-clock time.
    ///          For CPU-time purposes the composed value is the correct one. Code
    ///          that must tell the two apart (audio sample budgeting) uses
    ///          EmulatorState::HostSpeedMultiplier() instead.
    /// @return Clock multiplier (1, 2, 4, ...); never 0
    /// @note EmulatorState::current_z80_frequency_multiplier is 0 until Core
    ///       initialises it; that transient is reported as x1 so callers polling
    ///       during start-up never see a zero-length frame.
    /// @see EmulatorState::HostSpeedMultiplier
    uint8_t GetCpuClockMultiplier() const
    {
        const uint8_t multiplier = emulatorState.current_z80_frequency_multiplier;
        return multiplier > 0 ? multiplier : 1;
    }

    /// @brief T-states the Z80 executes per emulated frame at the current clock
    /// @details This is the per-frame limit that Z80::Z80FrameCycle and the
    ///          Emulator run loops enforce: the raster base scaled by the composed
    ///          clock multiplier. At 3.5 MHz on Pentagon it is 71680; at 14 MHz the
    ///          same frame is 286720 T-states long.
    /// @return T-states per frame at the current CPU clock
    /// @see GetBaseFrameTStates, GetCpuClockMultiplier
    uint32_t GetFrameTStates() const { return config.frame * GetCpuClockMultiplier(); }

    /// @brief Raster base T-states per frame, unscaled by the clock multiplier
    /// @details Pentagon 71680; ZX48/128 and Scorpion ZS-256 69888. This is the
    ///          hardware raster figure and does not move when the CPU clock changes.
    /// @return T-states per frame at the base 3.5 MHz clock
    /// @see GetFrameTStates
    uint32_t GetBaseFrameTStates() const { return config.frame; }

    /// @brief T-states per raster line at the current clock
    /// @return T-states per line, scaled by the composed clock multiplier
    /// @see GetBaseLineTStates
    uint32_t GetLineTStates() const { return config.t_line * GetCpuClockMultiplier(); }

    /// @brief Raster base T-states per line, unscaled by the clock multiplier
    /// @return T-states per line at the base 3.5 MHz clock (224 on every model here)
    /// @see GetLineTStates
    uint32_t GetBaseLineTStates() const { return config.t_line; }

    /// @brief In-frame T-state at which the INT pulse starts, at the current clock
    /// @return Offset from frame start, in T-states at the current CPU clock
    /// @see GetIntEndTState
    uint32_t GetIntStartTState() const { return config.intstart * GetCpuClockMultiplier(); }

    /// @brief In-frame T-state at which the INT pulse ends, at the current clock
    /// @details INT is asserted for config.intlen T-states starting at
    ///          config.intstart, both scaled by the clock multiplier.
    /// @return Offset from frame start, in T-states at the current CPU clock
    /// @note May exceed GetFrameTStates() when the pulse straddles the frame
    ///       boundary; Z80::Z80FrameCycle wraps it explicitly.
    /// @see GetIntStartTState
    uint32_t GetIntEndTState() const
    {
        return (config.intstart + config.intlen) * GetCpuClockMultiplier();
    }

    /// endregion </Timing helpers>

    /// region <Constructors / destructors>
public:
    EmulatorContext();                      // Default constructor with LogTrace default logging level
    EmulatorContext(LoggerLevel level);     // Constructor allowing to specify default logging level
    EmulatorContext(Emulator* emulator, LoggerLevel level = LoggerLevel::LogTrace);    // Constructor registering reference to parent Emulator object
    virtual ~EmulatorContext();
    /// endregion </Constructors / destructors>
};
