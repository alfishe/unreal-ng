#pragma once

/// @file neogszxdma.h
/// @brief NeoGS ZX-DMA: the host reads and writes card RAM through its own
/// memory cycles at #0000-#3FFF (neogs-zxdma-design.md §2, §5.4-§5.6; FPGA
/// dma/dma_zx.v, zxbus/zxbus.v).
///
/// While the ZX module runs (CST bit 7 of module 1):
///  - a host read of #0000-#3FFF with ROM paged there (/CSROM) returns the
///    byte fetched by the previous read and starts a fetch at the module's
///    address (the first read is junk);
///  - a host write there goes to card RAM (and to host RAM too when RAM is
///    paged: the normal write already did that);
///  - the address moves on by one per byte;
///  - an access while the previous byte is still pending is held with /WAIT.
///
/// The object is a HostBusOverlay the card installs only while needed:
///  - Off: not installed (the ZX module not selected, or no recent activity);
///  - Watch: installed, the module selected but not running. Host accesses to
///    the window only bring the card up to date, so a start is seen at exactly
///    the right access;
///  - Divert: the module runs.
/// Watch is a window of `watchFrames` frames, opened and renewed by selecting
/// the module, by a host port access that finds it selected, and by the end
/// of a transfer; it closes at a frame end (§5.4).

#include <cstdint>
#include <limits>

#include "emulator/memory/hostbusoverlay.h"

class NeoGSDma;
class NeoGSMemory;

class NeoGSZxDma final : public HostBusOverlay
{
public:
    enum class Mode : uint8_t { Off, Watch, Divert };
    enum class Pending : uint8_t { None, Read, Write };

    /// What the card lends the module (SoundChip_NeoGS implements it)
    struct Host
    {
        virtual ~Host() = default;
        virtual void zxCatchUp() = 0;                        // run the card to the host's current time
        virtual bool zxHostNowUnits(int64_t& now) const = 0; // the host's current time in card units
        virtual double zxUnitsPerHostT() const = 0;          // card units per host T-state
        virtual void zxAddHostWait(uint32_t tStates) = 0;    // /WAIT on the host CPU
        virtual int64_t zxUnitsPerCycle() const = 0;         // current card clock
        virtual void zxStall(int64_t units) = 0;             // card CPU off the bus
        virtual int64_t zxStallUntil() const = 0;            // end of a running stall (a burst)
        virtual bool zxInstall(bool installed) = 0;          // Core::SetBusOverlay(this / nullptr)
        virtual uint32_t zxFrame() const = 0;                // host frame counter
        virtual void zxReschedule() = 0;                     // our next event changed
        virtual void zxLateStart(int64_t units) = 0;         // a start was seen late (log once)
        virtual void zxTrace(bool write, uint32_t address, uint8_t value) = 0; // port trace (when capturing)
    };

    // Timing, card clocks (neogs-zxdma-design.md §2.4; estimates from the Verilog)
    static constexpr int READ_DONE_CLOCKS = 8;   // host read start -> byte in the latch
    static constexpr int WRITE_GRANT_CLOCKS = 5; // host write end -> grant
    static constexpr int CARD_STALL_CLOCKS = 6;  // card CPU off the bus per byte
    static constexpr int BURST_SLOT_CLOCKS = 2;  // a byte slot taken from a running SD/MP3 burst
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

    NeoGSZxDma(Host& host, NeoGSDma& dma, NeoGSMemory& mem) : _host(host), _dma(dma), _mem(mem)
    {
        windowStart = 0x0000; // the host's ROM area: only #0000-#3FFF (zxbus.v:234)
        windowEnd = 0x4000;
    }

    /// Remove the overlay: the card calls this before it is destroyed (the
    /// Host is gone by the time this object's destructor runs)
    void shutdown();

    // Settings ([NGS] ZxDmaWatch, ZxDmaWatchFrames)
    void setWatchAlways(bool always);
    void setWatchFrames(uint32_t frames) { _watchFrames = frames; }
    /// `false` for Fpga=D (no DMA): the overlay is never installed
    void setAvailable(bool available);

    /// Power-on / FPGA reset: back to Off, nothing pending
    void reset();

    // Card-side register writes (after NeoGSDma has stored them)
    void onModuleSelectWritten();
    void onControlWritten(bool wasRunning, int64_t now);

    /// A host port access (#B3 / #BB / #33), after its catch-up
    void onHostPortAccess();
    /// Frame end: close an expired watch window
    void onFrameEnd();

    /// Card-side completion of the pending byte
    int64_t nextEvent() const { return _pending == Pending::None ? kNever : _pendingDone; }
    void run(int64_t now);

    // HostBusOverlay
    uint8_t onRead(uint16_t addr, uint8_t normal, bool isExecution, bool romPaged) override;
    void onWrite(uint16_t addr, uint8_t value, bool romPaged) override;

    // Introspection (automation, tests)
    Mode mode() const { return _mode; }
    bool installed() const { return _installed; }
    uint8_t readLatch() const { return _readLatch; }
    Pending pending() const { return _pending; }
    uint32_t pendingAddress() const { return _pendingAddress; }
    uint64_t bytesRead() const { return _bytesRead; }
    uint64_t bytesWritten() const { return _bytesWritten; }
    uint64_t bytesDropped() const { return _bytesDropped; }
    uint64_t waitTStates() const { return _waitTStates; }
    uint64_t lateStarts() const { return _lateStarts; }
    uint64_t lateStartUnits() const { return _lateStartUnits; }
    bool watchAlways() const { return _watchAlways; }
    uint32_t watchFrames() const { return _watchFrames; }
    /// Frames until the watch window closes; -1 when closed
    int32_t watchFramesLeft() const;

    /// TTD snapshot (NeoGS layout 3): latch, pending byte, watch window and
    /// the statistics. The mode is not stored - it follows from the module's
    /// registers (NeoGSDma) and the window; loadState re-selects it and
    /// installs or removes the overlay accordingly
    static constexpr size_t STATE_SIZE = 96;
    void saveState(uint8_t* dst) const;
    void loadState(const uint8_t* src);

private:
    void updateMode();
    void renewWatch() { _watchUntilFrame = _host.zxFrame() + _watchFrames; _watchOpen = true; }
    /// Catch the card up and settle a byte due by then; false: not diverting
    bool syncForAccess(int64_t& accessEnd);
    /// /WAIT for the pending byte; `accessEnd` moves by the wait
    void waitForPending(int64_t& accessEnd, Pending incoming);
    int64_t clocks(int n) const { return static_cast<int64_t>(n) * _host.zxUnitsPerCycle(); }
    void complete();
    void stallCard(int64_t requestAt, int64_t& doneAt);

    Host& _host;
    NeoGSDma& _dma;
    NeoGSMemory& _mem;

    bool _available = true;
    bool _watchAlways = false;
    uint32_t _watchFrames = 5;
    bool _watchOpen = false;
    uint32_t _watchUntilFrame = 0;

    Mode _mode = Mode::Off;
    bool _installed = false;

    uint8_t _readLatch = 0xFF;
    Pending _pending = Pending::None;
    int64_t _pendingDone = kNever;
    uint32_t _pendingAddress = 0;
    uint8_t _pendingData = 0;

    int64_t _lastDoneAt = std::numeric_limits<int64_t>::min() / 2; // the byte completed last (its /WAIT may still apply)

    uint64_t _bytesRead = 0;
    uint64_t _bytesWritten = 0;
    uint64_t _bytesDropped = 0;
    uint64_t _waitTStates = 0;
    uint64_t _lateStarts = 0;
    uint64_t _lateStartUnits = 0;
};
