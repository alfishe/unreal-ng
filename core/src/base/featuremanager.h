#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/inifile.h"

// Forward declaration to avoid circular dependency
class EmulatorContext;

namespace
{
// Feature settings filename
constexpr const char* kFeaturesIni = "features.ini";
}  // namespace

namespace Features
{
// Feature IDs
constexpr const char* const kDebugMode = "debugmode";
constexpr const char* const kMemoryTracking = "memorytracking";
constexpr const char* const kBreakpoints = "breakpoints";
constexpr const char* const kCallTrace = "calltrace";
constexpr const char* const kSoundGeneration = "sound";
constexpr const char* const kSoundHQ = "soundhq";
constexpr const char* const kScreenHQ = "screenhq";
constexpr const char* const kRecording = "recording";
constexpr const char* const kSharedMemory = "sharedmemory";
constexpr const char* const kOpcodeProfiler = "opcodeprofiler";
constexpr const char* const kTimeTravel = "timetravel";
constexpr const char* const kOverscan = "overscan";
constexpr const char* const kPortTrace = "porttrace";
constexpr const char* const kFastTape = "fasttape";
constexpr const char* const kTurboTape = "turbotape";
constexpr const char* const kFastDisk = "fastdisk";
constexpr const char* const kTurboMode = "turbomode";
constexpr const char* const kHud = "hud";
constexpr const char* const kKempstonMouse = "kempstonmouse";
constexpr const char* const kGSLightweight = "gs_lightweight";
constexpr const char* const kContention = "contention";

// Feature Aliases
constexpr const char* const kDebugModeAlias = "dbg";
constexpr const char* const kMemoryTrackingAlias = "memtrack";
constexpr const char* const kBreakpointsAlias = "bp";
constexpr const char* const kCallTraceAlias = "ct";
constexpr const char* const kSoundGenerationAlias = "snd";
constexpr const char* const kSoundHQAlias = "hq";
constexpr const char* const kScreenHQAlias = "vhq";
constexpr const char* const kRecordingAlias = "rec";
constexpr const char* const kSharedMemoryAlias = "shm";
constexpr const char* const kOpcodeProfilerAlias = "op";
constexpr const char* const kTimeTravelAlias = "ttd";
constexpr const char* const kOverscanAlias = "osc";
constexpr const char* const kPortTraceAlias = "pt";
constexpr const char* const kFastTapeAlias = "ftape";
constexpr const char* const kTurboTapeAlias = "ttape";
constexpr const char* const kFastDiskAlias = "fdisk";
constexpr const char* const kTurboModeAlias = "turbo";
constexpr const char* const kHudAlias = "hud";
constexpr const char* const kKempstonMouseAlias = "kmouse";
constexpr const char* const kGSLightweightAlias = "gslw";
constexpr const char* const kContentionAlias = "cont";

// Feature Descriptions
constexpr const char* const kDebugModeDesc = "Master debug mode, enables/disables all debug features for performance";
constexpr const char* const kMemoryTrackingDesc = "Collect memory access counters and statistics";
constexpr const char* const kBreakpointsDesc = "Enable or disable breakpoint handling";
constexpr const char* const kCallTraceDesc = "Collect call trace information for debugging";
constexpr const char* const kSoundGenerationDesc = "Enable or disable sound generation";
constexpr const char* const kSoundHQDesc =
    "Enable high-quality DSP (FIR filters, oversampling). Disable for low-quality/faster audio.";
constexpr const char* const kScreenHQDesc =
    "Enable per-t-state video rendering for demo multicolor effects. Disable for batch 8-pixel rendering (25x faster).";
constexpr const char* const kRecordingDesc = "Enable recording subsystem (video, audio, GIF capture)";
constexpr const char* const kSharedMemoryDesc =
    "Export emulator memory via shared memory for external tool access. Disable for benchmarking/headless usage.";
constexpr const char* const kOpcodeProfilerDesc =
    "Track Z80 opcode execution stats and trace for debugging and crash forensics.";
constexpr const char* const kTimeTravelDesc =
    "Record execution history for rewind and reverse debugging (time-travel debug).";
constexpr const char* const kOverscanDesc =
    "Pentagon overscan mode (384x304). Shows invisible border areas for demo development. Pentagon only.";
constexpr const char* const kPortTraceDesc =
    "Structured port I/O trace recorder (ring buffer of IN/OUT events for peripheral diagnostics).";
constexpr const char* const kFastTapeDesc =
    "Fast tape loading: serve vanilla ROM tape loads instantly via the LD-BYTES trap. Custom loaders fall back to signal emulation.";
constexpr const char* const kTurboTapeDesc =
    "Turbo tape loading: engage turbo mode automatically while the tape signal path plays, so blocks the LD-BYTES trap cannot serve "
    "(headerless, custom-timed, pulse streams) still load at warp speed. Warp ends with the read-gap watchdog, end-of-tape or any stop.";
constexpr const char* const kFastDiskDesc =
    "Fast disk loading: FDC timing compression and TR-DOS ROM read-loop traps for instant floppy disk operations.";
constexpr const char* const kTurboModeDesc =
    "Turbo mode: run the whole emulation as fast as possible (max speed, audio muted unless turbo_audio is on). "
    "Forced off and blocked from re-enabling while TTD recording is active, so the recorded run reflects real "
    "timing and no code path is skipped by the accelerated loop.";
constexpr const char* const kHudDesc =
    "On-screen HUD: indicators and messages over the emulator picture. Zero cost when disabled.";

constexpr const char* const kKempstonMouseDesc =
    "Kempston Mouse on the bus (when fitted by the machine config [INPUT] Mouse=KEMPSTON). Off: the mouse ports are not decoded.";

constexpr const char* const kGSLightweightDesc =
    "General Sound lightweight personality: fit the in-tree ProTracker player card (no coprocessor firmware needed). Off keeps the "
    "personality from [SOUND] GSType; runtime switching carries the host mailbox across.";

constexpr const char* const kContentionDesc =
    "Video memory contention on the machines that have it (48K / 128K / +2 ULA, +2A / +3 gate array): the CPU waits "
    "for the screen fetches. Off runs those machines uncontended, for comparison. No effect on machines without "
    "contention. Cannot change while the machine is bound to a TTD timeline (it changes timing).";

// Categories
constexpr const char* const kCategoryDebug = "debug";
constexpr const char* const kCategoryAnalysis = "analysis";
constexpr const char* const kCategoryPerformance = "performance";

// Feature States
constexpr const char* const kStateOn = "on";
constexpr const char* const kStateOff = "off";
constexpr const char* const kStateLow = "low";
constexpr const char* const kStateHigh = "high";
}  // namespace Features

/// @brief FeatureManager manages runtime togglable features for debugging, analysis, and performance.
///
/// Features can be enabled/disabled or set to a specific mode. States are persisted in features.ini (UTF-8).
///
/// Usage:
/// - Register features at startup with metadata and default values.
/// - Query or set feature state/mode at runtime.
/// - Load/save state from/to features.ini.
/// - Integrate with CLI for user control.
class FeatureManager
{
public:
    // Explicitly require EmulatorContext for construction
    explicit FeatureManager(EmulatorContext* context);
    FeatureManager() = delete;

    /// @brief Struct describing a feature toggle.
    struct FeatureInfo
    {
        std::string id;                           // Unique identifier (canonical name)
        std::string alias;                        // Optional short/alt name
        std::string description;                  // Description for docs/help
        bool enabled = false;                     // Current on/off state
        std::string mode = "default";             // Current mode (default: "default")
        std::vector<std::string> availableModes;  // Supported modes (e.g., {"off", "on", "detailed"})
        std::string category;                     // Category for grouping (optional)
    };

    void registerFeature(const FeatureInfo& info);
    void removeFeature(const std::string& idOrAlias);
    void clear();
    bool setFeature(const std::string& idOrAlias, bool enabled);
    bool setMode(const std::string& idOrAlias, const std::string& mode);
    std::string getMode(const std::string& idOrAlias) const;
    bool isEnabled(const std::string& idOrAlias) const;
    /// @brief Whether a feature by this id or alias exists (tells "refused" from "unknown")
    bool hasFeature(const std::string& idOrAlias) const;
    /// @brief Why setFeature(idOrAlias, enabled) would be refused right now, as one
    /// sentence a user can act on; empty when it would not be (or the feature is unknown).
    /// TTD holds features while it records or replays: see isTtdRecordingActive /
    /// isTtdTimelineBound, and TimeTravelManager::RecordingGuard for the capture flags.
    std::string refusalReason(const std::string& idOrAlias, bool enabled) const;
    std::vector<FeatureInfo> listFeatures() const;
    void setDefaults();
    void loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;
    void onFeatureChanged(const std::string& changedFeatureId = "");
    void onTtdRecordingStarted();
    void onTtdRecordingStopped();
    bool isTtdRecordingActive() const;
    /// @brief True while the machine is bound to a TTD timeline: recording, replaying
    /// history (seek/step) or positioned in it (Detached). Features that change what
    /// the guest code does (fasttape, turbotape, fastdisk) are off for all of it, or a
    /// replay would diverge from what was recorded. Pacing-only acceleration (turbo
    /// mode, speed) is locked by isTtdRecordingActive() alone.
    bool isTtdTimelineBound() const;

    EmulatorContext* context() const
    {
        return _context;
    }

private:
    /// True when TTD currently forces this feature off (see isTtdTimelineBound / isTtdRecordingActive)
    bool isMaskedByTtd(const std::string& id) const;

    /// Find a feature by id or alias. Caller must hold _mutex.
    FeatureInfo* findFeature(const std::string& idOrAlias);
    const FeatureInfo* findFeature(const std::string& idOrAlias) const;

    /// @brief Engage/disengage Core turbo mode to match the 'turbomode' feature and
    /// the current TTD-recording gate. Unlike fasttape/turbotape/fastdisk (which are
    /// polled lazily and only need isEnabled() masked), nothing polls turbo mode every
    /// frame, so the engine state has to be pushed here explicitly. Idempotent: only
    /// calls Core if the actual state disagrees with the desired one.
    void syncTurboModeWithTtdState();

    EmulatorContext* _context;
    std::unordered_map<std::string, FeatureInfo> _features;  // id -> FeatureInfo
    std::unordered_map<std::string, std::string> _aliases;   // alias -> id
    mutable bool _dirty = false;                             // Track if the state changed and save is required

    // TTD recording lock, driven only by TimeTravelManager (onTtdRecordingStarted /
    // onTtdRecordingStopped) - not by the 'timetravel' feature toggle, which merely
    // arms the capture machinery. Masking needs no saved states: the stored feature
    // values are untouched and show through again once the lock is released.
    mutable bool _ttdShortcutOverrideActive = false;

    /// Guards _features/_aliases/_dirty: the WebAPI/HTTP thread mutates them
    /// while the MessageCenter worker reads them (e.g. HudModel feature
    /// notifications). Recursive so public methods may call each other.
    mutable std::recursive_mutex _mutex;
};