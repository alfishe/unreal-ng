#pragma once

#include "3rdparty/simpleini/simpleini.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

// Forward declaration to avoid circular dependency
class EmulatorContext;

namespace
{
    // Feature settings filename
    constexpr const char* kFeaturesIni = "features.ini";
}

namespace Features
{
    // Feature IDs
    constexpr const char* const kDebugMode = "debugmode";
    constexpr const char* const kMemoryTracking = "memorytracking";
    constexpr const char* const kBreakpoints = "breakpoints";
    constexpr const char* const kCallTrace = "calltrace";

    // Feature Aliases
    constexpr const char* const kDebugModeAlias = "dbg";
    constexpr const char* const kMemoryTrackingAlias = "memtrack";
    constexpr const char* const kBreakpointsAlias = "bp";
    constexpr const char* const kCallTraceAlias = "ct";

    // Feature Descriptions
    constexpr const char* const kDebugModeDesc = "Master debug mode, enables/disables all debug features for performance";
    constexpr const char* const kMemoryTrackingDesc = "Collect memory access counters and statistics";
    constexpr const char* const kBreakpointsDesc = "Enable or disable breakpoint handling";
    constexpr const char* const kCallTraceDesc = "Collect call trace information for debugging";

    // Categories
    constexpr const char* const kCategoryDebug = "debug";
    constexpr const char* const kCategoryAnalysis = "analysis";

    // Feature States
    constexpr const char* const kStateOn = "on";
    constexpr const char* const kStateOff = "off";
}

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
        std::string id;                          // Unique identifier (canonical name)
        std::string alias;                       // Optional short/alt name
        std::string description;                 // Description for docs/help
        bool enabled = false;                    // Current on/off state
        std::string mode = "default";            // Current mode (default: "default")
        std::vector<std::string> availableModes; // Supported modes (e.g., {"off", "on", "detailed"})
        std::string category;                    // Category for grouping (optional)
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

    EmulatorContext* context() const { return _context; }

private:
    FeatureInfo* findFeature(const std::string& idOrAlias);
    const FeatureInfo* findFeature(const std::string& idOrAlias) const;

    EmulatorContext* _context;
    std::unordered_map<std::string, FeatureInfo> _features; // id -> FeatureInfo
    std::unordered_map<std::string, std::string> _aliases;  // alias -> id
    mutable bool _dirty = false;                            // Track if the state changed and save is required
}; 