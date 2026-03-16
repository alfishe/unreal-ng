#pragma once

/// @file emulog.h
/// Zero-cost hierarchical logging system for real-time monitoring.
///
/// Usage:
///   // In .cpp file (file-level):
///   #include "common/emulog.h"
///   EMU_LOG_DEFINE_CATEGORY(log_fdc,     "fdc");
///   EMU_LOG_DEFINE_CATEGORY(log_fdc_cmd, "fdc.command");
///
///   // In functions:
///   EMU_LOG_INFO(log_fdc_cmd, "READ_SECTOR T%d/H%d/S%d", track, side, sector);
///
/// When disabled (the common case), cost is ~1-2ns per call site
/// (one relaxed atomic load + branch-not-taken).

#include <cstdint>
#include <cstdio>
#include <chrono>

#include "emulog_category.h"
#include "emulog_registry.h"
#include "spsc_ringbuffer.h"

namespace emu::log {

/// Cold-path emit function. Only called when logging is actually enabled.
/// Formats the message and writes it to the SPSC ring buffer.
template<typename... Args>
void emit(CategoryState& cat, Level level, const char* fmt, Args... args)
{
    // Format into thread-local buffer
    thread_local char buf[4096];
    int len = std::snprintf(buf, sizeof(buf), fmt, args...);
    if (len <= 0)
        return;
    if (static_cast<size_t>(len) >= sizeof(buf))
        len = sizeof(buf) - 1;

    // Write to SPSC ring buffer (non-blocking)
    auto* ring = LoggerRegistry::instance().ringBuffer();
    if (ring)
    {
        ring->try_write(cat.id, static_cast<uint8_t>(level),
                        buf, static_cast<uint32_t>(len));
    }
}

}  // namespace emu::log

// =============================================================================
// Category definition macro
// =============================================================================

/// Define a log category with automatic registration.
/// Creates a static CategoryState and a static CategoryRegistrar that
/// registers it with the global LoggerRegistry on program startup.
///
/// @param var Variable name prefix (used as var##_state, var##_reg)
/// @param path Dot-separated category path ("fdc.read")
#define EMU_LOG_DEFINE_CATEGORY(var, path)                                     \
    static ::emu::log::CategoryState var##_state{                              \
        {false},                                                               \
        {static_cast<uint8_t>(::emu::log::Level::Off)},                        \
        path,                                                                  \
        ::emu::log::hash_fnv1a(path),                                          \
        nullptr                                                                \
    };                                                                         \
    static ::emu::log::CategoryRegistrar var##_reg{var##_state}

// =============================================================================
// Logging macros — hot-path check is 1 relaxed atomic load
// =============================================================================

/// General logging macro with explicit level.
#define EMU_LOG(cat, lvl, fmt, ...)                                            \
    do                                                                         \
    {                                                                          \
        if (__builtin_expect(                                                  \
                cat##_state.is_enabled(::emu::log::Level::lvl), 0))            \
            ::emu::log::emit(cat##_state, ::emu::log::Level::lvl,              \
                             fmt, ##__VA_ARGS__);                              \
    } while (0)

#define EMU_LOG_TRACE(cat, fmt, ...) EMU_LOG(cat, Trace, fmt, ##__VA_ARGS__)
#define EMU_LOG_DEBUG(cat, fmt, ...) EMU_LOG(cat, Debug, fmt, ##__VA_ARGS__)
#define EMU_LOG_INFO(cat, fmt, ...)  EMU_LOG(cat, Info,  fmt, ##__VA_ARGS__)
#define EMU_LOG_WARN(cat, fmt, ...)  EMU_LOG(cat, Warn,  fmt, ##__VA_ARGS__)
#define EMU_LOG_ERROR(cat, fmt, ...) EMU_LOG(cat, Error, fmt, ##__VA_ARGS__)
