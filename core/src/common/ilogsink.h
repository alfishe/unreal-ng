#pragma once

#include <cstdint>

/// @file ilogsink.h
/// Abstract log output sink interface.
///
/// ModuleLogger dispatches formatted messages to zero or more sinks via
/// addSink()/removeSink(). Implementations live in their respective subsystems
/// (e.g., monitoring::IPCLogSink writes to SPSC ring buffer in shared memory).
///
/// This interface is deliberately minimal — it carries only the module/submodule
/// identity and the formatted text. Timestamps and further enrichment are the
/// sink's responsibility.

struct ILogSink
{
    virtual ~ILogSink() = default;

    /// Write a formatted log message to this sink.
    /// @param module    PlatformModulesEnum value (0-12)
    /// @param submodule Submodule bitmask (e.g., SUBMODULE_DISK_FDC)
    /// @param level     LoggerLevel value (1=Trace, 2=Debug, 3=Info, 4=Warning, 5=Error)
    /// @param text      Formatted message text
    /// @param len       Length of text in bytes
    virtual void write(uint8_t module, uint16_t submodule,
                       uint8_t level, const char* text, uint32_t len) = 0;
};
