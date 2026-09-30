#pragma once

/// @file hostbusoverlay.h
/// @brief A device that sees the host CPU's memory accesses in one address
/// window (neogs-zxdma-design.md §5.2).
///
/// An overlay is installed with Core::AddBusOverlay only while its device
/// needs it. The host Z80 then uses the overlay memory interfaces
/// (Memory::MemoryReadOverlay / MemoryWriteOverlay), which make the normal
/// access first - model overrides, access tracking, TTD hooks and breakpoints
/// all run as without an overlay - and then call the overlay for addresses in
/// its window. With no overlay installed the Z80 uses the plain fast / debug
/// interfaces, so an overlay costs nothing until a device installs one.
///
/// Several overlays may be installed at once (Core::AddBusOverlay): a machine's
/// own bus logic (TSConf's FM window) and a card's (NeoGS ZX-DMA) are
/// independent devices on the same bus. With one installed, the memory
/// interface calls it directly; with two or more, Core points the memory
/// interface at a HostBusOverlayChain that forwards to each in install order.
/// Install and remove overlays on the emulation thread (or with the emulation
/// paused): the overlay functions read the pointers without a lock.

#include <cstddef>
#include <cstdint>

class HostBusOverlay
{
public:
    virtual ~HostBusOverlay() = default;

    /// Window [windowStart, windowEnd) of the host address space the overlay
    /// sees; windowEnd = 0x10000 covers the whole space
    uint16_t windowStart = 0x0000;
    uint32_t windowEnd = 0x10000;

    /// false: a write-only overlay (a memory write intercept - TSConf's FM
    /// window, the Sprinter's video shadow, ZX-Evo flash writes). Its reads
    /// cost one flag test and onRead is never called. Fixed while installed
    bool observesReads = true;

    /// A host read in the window, after the normal read. `normal` is what the
    /// normal read returned; the result is what the CPU gets. `romPaged`: ROM
    /// is mapped at #0000 (the ZX-bus /CSROM condition).
    virtual uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) = 0;

    /// An opcode fetch (M1) in the window, after the normal read. Overlays that do not care what kind of read
    /// it is see it as an execution read; wait-state rules tell it from an operand byte (a ZX-Evo's code cache,
    /// the Scorpion turbo's M1 wait: docs/inprogress/2026-09-29-machine-waits/tdd.md)
    virtual uint8_t onReadM1(uint16_t addr, uint8_t normal, bool romPaged)
    {
        return onRead(addr, normal, true, romPaged);
    }

    /// A host write in the window, after the normal write
    virtual void onWrite(uint16_t addr, uint8_t value, bool romPaged) = 0;

    /// The Z80 accepted an interrupt (its acknowledge is an I/O cycle). Called while the overlay is installed,
    /// whatever its window; default: nothing
    virtual void onInterruptAcknowledge() {}
};

/// Two or more installed overlays seen as one (owned by Core, never installed
/// by a device). It observes reads when any member does. Its window is the
/// whole address space and each member's
/// window is checked on every access, so a member may move its window while
/// installed (TSConf moves the FM window with FMAPS), as a single overlay may. Reads pass through the members in install
/// order, each getting the previous one's result as `normal`; writes reach
/// every member whose window holds the address.
class HostBusOverlayChain final : public HostBusOverlay
{
public:
    static constexpr size_t kMaxOverlays = 4;

    /// Replace the members (Core, under its memory-interface lock)
    void Assign(HostBusOverlay* const* overlays, size_t count);
    size_t Count() const { return _count; }

    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    uint8_t onReadM1(uint16_t addr, uint8_t normal, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;
    void onInterruptAcknowledge() override;

private:
    HostBusOverlay* _members[kMaxOverlays] = {};
    size_t _count = 0;
};
