#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

/// @file emulog_category.h
/// Per-category runtime state for the hierarchical zero-cost logging system.

namespace emu::log {

/// Log severity levels
enum class Level : uint8_t
{
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Error = 4,
    Off   = 255,
};

/// Compile-time/runtime category ID via FNV-1a hash
constexpr uint32_t hash_fnv1a(std::string_view name)
{
    uint32_t hash = 2166136261u;
    for (char c : name)
    {
        hash ^= static_cast<uint32_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

/// Per-category runtime state.
/// The `enabled` flag is the primary hot-path gate — checked with a single
/// relaxed atomic load. When disabled (the common case), the cost is ~1-2ns
/// with near-perfect branch prediction.
struct CategoryState
{
    std::atomic<bool> enabled{false};
    std::atomic<uint8_t> level{static_cast<uint8_t>(Level::Off)};
    const char* name;           // "fdc.read"
    uint32_t id;                // hash_fnv1a(name)
    CategoryState* parent{nullptr};  // "fdc" for "fdc.read"

    bool is_enabled(Level msg_level) const noexcept
    {
        return enabled.load(std::memory_order_relaxed) &&
               static_cast<uint8_t>(msg_level) >=
               level.load(std::memory_order_relaxed);
    }
};

}  // namespace emu::log
