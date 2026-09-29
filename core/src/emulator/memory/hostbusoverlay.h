#pragma once

/// @file hostbusoverlay.h
/// @brief A device that sees the host CPU's memory accesses in one address
/// window (neogs-zxdma-design.md §5.2).
///
/// An overlay is installed with Core::SetBusOverlay only while its device
/// needs it. The host Z80 then uses the overlay memory interfaces
/// (Memory::MemoryReadOverlay / MemoryWriteOverlay), which make the normal
/// access first - model overrides, access tracking, TTD hooks and breakpoints
/// all run as without an overlay - and then call the overlay for addresses in
/// its window. With no overlay installed the Z80 uses the plain fast / debug
/// interfaces, so an overlay costs nothing until a device installs one.
///
/// One overlay at a time. Install and remove it on the emulation thread (or
/// with the emulation paused): the overlay functions read the pointer without
/// a lock.

#include <cstdint>

class HostBusOverlay
{
public:
    virtual ~HostBusOverlay() = default;

    /// Window [windowStart, windowEnd) of the host address space the overlay
    /// sees; windowEnd = 0x10000 covers the whole space
    uint16_t windowStart = 0x0000;
    uint32_t windowEnd = 0x10000;

    /// A host read in the window, after the normal read. `normal` is what the
    /// normal read returned; the result is what the CPU gets. `romPaged`: ROM
    /// is mapped at #0000 (the ZX-bus /CSROM condition).
    virtual uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) = 0;

    /// A host write in the window, after the normal write
    virtual void onWrite(uint16_t addr, uint8_t value, bool romPaged) = 0;
};
