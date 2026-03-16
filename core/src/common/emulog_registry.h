#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "emulog_category.h"

namespace emu {
struct SPSCRingBuffer;
}

namespace emu::log {

/// Central registry for all log categories.
/// Manages category registration, hierarchy, and runtime enable/disable.
/// Thread-safe for cold-path operations (registration, reconfiguration).
class LoggerRegistry
{
public:
    static LoggerRegistry& instance();

    /// Register a category (called from static initializers via CategoryRegistrar).
    void registerCategory(CategoryState& state);

    /// Enable/disable categories matching a pattern.
    /// Patterns: "fdc" (exact), "fdc.*" (all children), "*" (all).
    /// @param pattern Category name or wildcard pattern
    /// @param enabled Whether to enable or disable
    /// @param level Minimum log level when enabled
    void setEnabled(const std::string& pattern, bool enabled,
                    Level level = Level::Trace);

    /// Disable all categories.
    void disableAll();

    /// List all registered categories with their current state.
    /// @return Vector of (name, enabled) pairs
    std::vector<std::pair<std::string, bool>> listCategories() const;

    /// Serialize category list into a buffer (for control channel response).
    /// Format: "name:enabled:level\n" per category.
    /// @param buffer Output buffer
    /// @param bufferSize Buffer size
    /// @return Number of bytes written
    size_t serializeCategoryList(char* buffer, size_t bufferSize) const;

    /// Get the SPSC ring buffer for log output.
    emu::SPSCRingBuffer* ringBuffer() { return _ringBuffer; }

    /// Set the SPSC ring buffer (called by MonitoringManager on init).
    void setRingBuffer(emu::SPSCRingBuffer* rb) { _ringBuffer = rb; }

    /// Find a category by name.
    CategoryState* findCategory(const std::string& name) const;

    /// @return Number of registered categories
    size_t categoryCount() const;

private:
    LoggerRegistry() = default;

    void resolveParent(CategoryState& state);

    mutable std::mutex _mutex;  // Cold-path only
    std::unordered_map<uint32_t, CategoryState*> _byId;
    std::unordered_map<std::string, CategoryState*> _byName;
    emu::SPSCRingBuffer* _ringBuffer = nullptr;
};

/// RAII registrar — constructed by EMU_LOG_DEFINE_CATEGORY macro.
/// Registers the category in the global LoggerRegistry on construction.
struct CategoryRegistrar
{
    explicit CategoryRegistrar(CategoryState& state)
    {
        LoggerRegistry::instance().registerCategory(state);
    }
};

}  // namespace emu::log
