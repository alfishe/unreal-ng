#include "featuremanager.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>

#include "3rdparty/simpleini/simpleini.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/recording/recordingmanager.h"
#include "emulator/video/screen.h"

FeatureManager::FeatureManager(EmulatorContext* context) : _context(context)
{
    setDefaults();
}

/// @brief Register a new feature with metadata and default values.
/// @param info Feature information structure containing all metadata
void FeatureManager::registerFeature(const FeatureInfo& info)
{
    _features[info.id] = info;
    if (!info.alias.empty())
    {
        _aliases[info.alias] = info.id;
    }
}

/// @brief Remove a feature by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature to remove
void FeatureManager::removeFeature(const std::string& idOrAlias)
{
    auto* feature = findFeature(idOrAlias);
    if (feature)
    {
        _aliases.erase(feature->alias);
        _features.erase(feature->id);
        _dirty = true;
    }
}

/// @brief Remove all features (reset to empty).
void FeatureManager::clear()
{
    _features.clear();
    _aliases.clear();
    _dirty = true;
}

/// @brief Set feature enabled/disabled by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @param enabled Whether to enable or disable the feature
/// @return true if the feature was found and updated, false if feature not found
bool FeatureManager::setFeature(const std::string& idOrAlias, bool enabled)
{
    auto* feature = findFeature(idOrAlias);
    if (feature)
    {
        bool valueChanged = (feature->enabled != enabled);
        feature->enabled = enabled;

        // Always call onFeatureChanged() to ensure caches are synchronized,
        // even if the value didn't change. This handles the edge case where
        // UpdateFeatureCache() was never called during initialization.
        // Only mark dirty if value actually changed (for persistence purposes).
        if (valueChanged)
        {
            _dirty = true;
        }

        onFeatureChanged();

        return true;
    }
    else
    {
        // Feature not found
        return false;
    }
}

/// @brief Set mode for a feature by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @param mode New mode to set for the feature
/// @return true if the feature was found and updated, false if feature not found
bool FeatureManager::setMode(const std::string& idOrAlias, const std::string& mode)
{
    auto* feature = findFeature(idOrAlias);
    if (feature && feature->mode != mode)
    {
        feature->mode = mode;
        _dirty = true;
        onFeatureChanged();
        return true;
    }
    else if (feature)
    {
        // Feature found but no change needed
        return true;
    }
    else
    {
        // Feature not found
        return false;
    }
}

/// @brief Get mode for a feature by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @return Current mode of the feature, or empty string if not found
std::string FeatureManager::getMode(const std::string& idOrAlias) const
{
    const auto* feature = findFeature(idOrAlias);
    return feature ? feature->mode : "";
}

/// @brief Query if a feature is enabled by id or alias.
/// @param idOrAlias Unique identifier or alias of the feature
/// @return true if the feature is enabled, false otherwise or if not found
bool FeatureManager::isEnabled(const std::string& idOrAlias) const
{
    const auto* feature = findFeature(idOrAlias);
    return feature ? feature->enabled : false;
}

/// @brief List all features and their metadata.
/// @return Vector containing FeatureInfo for all registered features
std::vector<FeatureManager::FeatureInfo> FeatureManager::listFeatures() const
{
    std::vector<FeatureInfo> out;
    for (const auto& kv : _features)
    {
        out.push_back(kv.second);
    }

    return out;
}

/// @brief Set all features to their default values (for startup/reset).
/// Registers default features with their initial states.
void FeatureManager::setDefaults()
{
    // Example: register default features here. Extend as needed.
    clear();

    registerFeature({Features::kDebugMode,
                     Features::kDebugModeAlias,
                     Features::kDebugModeDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryDebug});
    registerFeature({Features::kMemoryTracking,
                     Features::kMemoryTrackingAlias,
                     Features::kMemoryTrackingDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryAnalysis});
    registerFeature({Features::kBreakpoints,
                     Features::kBreakpointsAlias,
                     Features::kBreakpointsDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryDebug});
    registerFeature({Features::kCallTrace,
                     Features::kCallTraceAlias,
                     Features::kCallTraceDesc,
                     false,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryAnalysis});
    registerFeature({Features::kSoundGeneration,
                     Features::kSoundGenerationAlias,
                     Features::kSoundGenerationDesc,
                     true,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kSoundHQ,
                     Features::kSoundHQAlias,
                     Features::kSoundHQDesc,
                     true,
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kScreenHQ,
                     Features::kScreenHQAlias,
                     Features::kScreenHQDesc,
                     true,  // ON by default - demo compatibility
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});
    registerFeature({Features::kRecording,
                     Features::kRecordingAlias,
                     Features::kRecordingDesc,
                     false,  // OFF by default - heavy functionality
                     "",
                     {Features::kStateOff, Features::kStateOn},
                     Features::kCategoryPerformance});

    _dirty = false;
}

/// @brief Load feature states from features.ini (UTF-8). If missing, uses defaults.
/// @param path Path to the features.ini file
void FeatureManager::loadFromFile(const std::string& path)
{
    if (!std::filesystem::exists(path))
    {
        return;
    }

    CSimpleIniA ini;
    ini.SetUnicode(true);
    if (ini.LoadFile(path.c_str()) < 0)
    {
        std::cerr << "Failed to load " << path << std::endl;
        return;
    }

    // Traverse all sections (feature ids) in the file
    auto sections = std::list<CSimpleIniA::Entry>{};
    ini.GetAllSections(sections);
    for (const auto& entry : sections)
    {
        const char* section = entry.pItem;
        auto it = _features.find(section);
        if (it == _features.end())
            continue;  // Only override registered features
        FeatureInfo& f = it->second;

        const char* state = ini.GetValue(section, "state", nullptr);
        if (state)
        {
            std::string s = state;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            f.enabled = (s == Features::kStateOn || s == "true" || s == "1");
        }

        const char* mode = ini.GetValue(section, "mode", nullptr);
        if (mode)
        {
            f.mode = mode;
        }
    }

    // Features state fully match the settings file
    _dirty = false;

    // Recalculate all cached flags
    onFeatureChanged();
}

/// @brief Save current feature states to features.ini (UTF-8).
/// @param path Path where to save the features.ini file
void FeatureManager::saveToFile(const std::string& path) const
{
    CSimpleIniA ini;
    ini.SetUnicode(true);

    for (const auto& [id, f] : _features)
    {
        ini.SetValue(id.c_str(), "state", f.enabled ? Features::kStateOn : Features::kStateOff);
        ini.SetValue(id.c_str(), "mode", f.mode.c_str());
    }

    if (ini.SaveFile(path.c_str()) < 0)
    {
        std::cerr << "Failed to save " << path << std::endl;
    }

    _dirty = false;
}

/// @brief Call when a feature state or mode changes. Triggers save if needed.
/// Automatically saves to features.ini if any changes were made.
void FeatureManager::onFeatureChanged()
{
    // Update the feature cache in Memory class if it exists
    if (_context && _context->pCore && _context->pCore->GetMemory())
    {
        _context->pCore->GetMemory()->UpdateFeatureCache();

        // Synchronize master switch with feature changes
        _context->pCore->GetZ80()->isDebugMode = _features[Features::kDebugMode].enabled;
    }

    // Update feature cache in SoundManager if it exists
    if (_context && _context->pSoundManager)
    {
        _context->pSoundManager->UpdateFeatureCache();
    }

    // Update feature cache in RecordingManager if it exists
    if (_context && _context->pRecordingManager)
    {
        _context->pRecordingManager->UpdateFeatureCache();
    }

    // Update feature cache in Screen (for ScreenHQ toggle) if it exists
    if (_context && _context->pScreen)
    {
        _context->pScreen->UpdateFeatureCache();
    }

    if (_dirty)
    {
        saveToFile(kFeaturesIni);
        _dirty = false;
    }
}

/// @brief Find a feature by id or alias (mutable).
/// @param idOrAlias Unique identifier or alias of the feature
/// @return Pointer to the FeatureInfo if found, nullptr otherwise
FeatureManager::FeatureInfo* FeatureManager::findFeature(const std::string& idOrAlias)
{
    // Try to find the feature by its canonical id
    auto it = _features.find(idOrAlias);
    if (it != _features.end())
        return &it->second;

    // If not found, try to resolve as an alias
    auto ait = _aliases.find(idOrAlias);
    if (ait != _aliases.end())
    {
        // Look up the canonical id from the alias and return the feature if it exists
        auto fit = _features.find(ait->second);
        if (fit != _features.end())
            return &fit->second;
    }

    // Feature not found
    return nullptr;
}

/// @brief Find a feature by id or alias (const).
/// @param idOrAlias Unique identifier or alias of the feature
/// @return Pointer to the const FeatureInfo if found, nullptr otherwise
const FeatureManager::FeatureInfo* FeatureManager::findFeature(const std::string& idOrAlias) const
{
    // Try to find the feature by its canonical id
    auto it = _features.find(idOrAlias);
    if (it != _features.end())
        return &it->second;

    // If not found, try to resolve as an alias
    auto ait = _aliases.find(idOrAlias);
    if (ait != _aliases.end())
    {
        // Look up the canonical id from the alias and return the feature if it exists
        auto fit = _features.find(ait->second);
        if (fit != _features.end())
            return &fit->second;
    }

    // Feature not found
    return nullptr;
}