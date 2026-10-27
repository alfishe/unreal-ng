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
    /// true: the overlay does nothing for an access to a window mapped to ROM (a RAM wait rule: the Scorpion
    /// turbo's DRAM slots). The memory interface then skips the call there, after the normal access, as the
    /// overlay would have returned. A chain does not take it: its members check for themselves. Fixed while
    /// installed
    bool ignoresRom = false;

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

    /// true: onBeforeWrite is called for a write in the window before the normal write stores the byte (a screen that draws only
    /// when its inputs change draws what the old byte showed first: TS-Conf). Fixed while installed
    bool observesWriteStart = false;
    virtual void onBeforeWrite(uint16_t addr) { (void)addr; }

    /// The Z80 accepted an interrupt (its acknowledge is an I/O cycle). Called while the overlay is installed,
    /// whatever its window; default: nothing
    virtual void onInterruptAcknowledge() {}

    /// A halted CPU's idle opcode fetch at `addr` (in the window, the overlay observes reads), repeated with
    /// nothing else in between: true when the overlay's only effect on it is wait states that depend on nothing
    /// but the fetch's start clock modulo `period` (set it; 1: no or constant waits). An engine may then run such
    /// fetches in one go (Z84C15Engine). Default: false - every fetch must run
    virtual bool RepeatFetchIsPure(uint16_t addr, uint32_t& period) const
    {
        (void)addr;
        (void)period;
        return false;
    }
    /// How far a true RepeatFetchIsPure holds: fetches at `addr` that start at a frame T-state in [t, the result)
    /// follow it; a rule that follows the frame position (a fetch window) ends its stretch where it changes.
    /// UINT32_MAX (the default): up to the frame end
    virtual uint32_t RepeatFetchPureUntil(uint16_t addr, uint32_t t) const
    {
        (void)addr;
        (void)t;
        return UINT32_MAX;
    }
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
    void onBeforeWrite(uint16_t addr) override;
    void onInterruptAcknowledge() override;
    /// Every member that sees the fetch is pure; the period is their least common multiple
    bool RepeatFetchIsPure(uint16_t addr, uint32_t& period) const override;
    /// The shortest stretch of the members that see the fetch
    uint32_t RepeatFetchPureUntil(uint16_t addr, uint32_t t) const override;

private:
    HostBusOverlay* _members[kMaxOverlays] = {};
    size_t _count = 0;
};
