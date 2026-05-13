#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "3rdparty/simpleini/simpleini.h"

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
    std::vector<FeatureInfo> listFeatures() const;
    void setDefaults();
    void loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;
    void onFeatureChanged();

    EmulatorContext* context() const
    {
        return _context;
    }

private:
    FeatureInfo* findFeature(const std::string& idOrAlias);
    const FeatureInfo* findFeature(const std::string& idOrAlias) const;

    EmulatorContext* _context;
    std::unordered_map<std::string, FeatureInfo> _features;  // id -> FeatureInfo
    std::unordered_map<std::string, std::string> _aliases;   // alias -> id
    mutable bool _dirty = false;                             // Track if the state changed and save is required
};