#pragma once
#include "stdafx.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "common/ringbuffer.h"
#include "debugger/history/devicehistory.h"

class EmulatorContext;

/// ============================================================================
/// GENERAL SOUND PORT/ACTIVITY TRACER
/// ============================================================================
/// Structured, low-overhead ring buffer of I/O and DAC events for the GS
/// coprocessor (SoundChip_GeneralSound) - the GS-scoped counterpart of the
/// main-Z80 PortDiagnosticRecorder (core/src/emulator/ports/portdiagrecorder.h).
///
/// The GS card has its own independent Z80 core, its own 8-bit GS-side port
/// space (0x00-0x0B) and a separate 16-bit host-side port space (#B3/#BB/#33
/// as seen by the MAIN Z80). Both are folded into one event stream here so a
/// single trace answers "is the GS coprocessor alive and doing DAC pushes".
///
/// The events go to a device history (debugger/history/devicehistory.h): SoundManager owns the machine's one, named
/// by the card's slot in the registry (`zxbus.1`), and every personality it fits (LLE, LW, NeoGS) records into it, so
/// a personality switch keeps the trace; a card on its own (a test, a card inside another card) records into its own.
/// ============================================================================

/// Which side of the card originated the event
enum class GSTraceSide : uint8_t
{
    Host = 0,       // ZX-side port access (#B3/#BB/#33), main Z80 is the actor
    GsInternal = 1, // GS-side port access (0x00-0x0B), GS Z80 is the actor
    DacFetch = 2,   // GS Z80 read in 0x6000-0x7FFF: DAC channel sample latch
    Interrupt = 3,  // 37.5 kHz periodic interrupt accepted (or NMI)
    ZxDma = 4,      // NeoGS ZX-DMA byte: the host read or wrote card RAM through #0000-#3FFF
};

namespace GSTraceFlags
{
constexpr uint8_t kDirectionOut = 1u << 0;  // 0 = IN/read, 1 = OUT/write
constexpr uint8_t kNmi          = 1u << 1;  // Interrupt event was an NMI, not the periodic INT
}  // namespace GSTraceFlags

/// One structured record per GS I/O operation, DAC fetch, or interrupt. 24 bytes.
struct GSTraceEvent
{
    int64_t  timestamp = 0;   // GS cycle domain (12 MHz), from totalGsCycles() - monotonic across frames
    uint32_t frameNumber = 0; // Emulator (ZX) frame counter at event time
    uint16_t port = 0;        // Host: #B3/#BB/#33. GsInternal: 0x00-0x0B. DacFetch: 0x6000-0x7FFF address. Interrupt: unused (0). ZxDma: card RAM address 15:0
    uint16_t pc = 0;          // GS CPU PC at the time of the event (ZxDma: the host CPU's PC)
    uint8_t  value = 0;       // Data byte (port value, DAC sample byte, ZxDma: the byte the host got / wrote)
    uint8_t  channel = 0;     // DacFetch: channel index 0-3. ZxDma: card RAM address 20:16
    GSTraceSide side = GSTraceSide::Host;
    uint8_t  flags = 0;       // GSTraceFlags bitfield

    bool isOut() const { return flags & GSTraceFlags::kDirectionOut; }
    bool isNmi() const { return flags & GSTraceFlags::kNmi; }

    void ToState(StateNode& node) const;
};

/// The GS card's history (gsporttrace.h)
using GSHistory = history::DeviceHistory<GSTraceEvent>;

/// The registry id of the General Sound card on this machine (the slot of the card on the GS host ports); `gs`
/// when the registry names none
std::string GSHistoryDevice(const EmulatorContext* context);

/// Cheap always-on activity counters (no ring buffer, no feature flag) - the
/// first thing to check when triaging "is the GS coprocessor doing anything
/// at all": cpuSteps/interruptsAccepted prove the core is executing,
/// dacFetches/lastDacFetchGsCycle prove it is actually producing audio data.
struct GSActivityCounters
{
    uint64_t cpuSteps = 0;             // Z80CpuStep() calls: instructions (a redundant DD/FD prefix is its own)
    uint64_t interruptsAccepted = 0;   // Z80CpuInt() acceptances (37.5 kHz periodic)
    uint64_t nmisAccepted = 0;         // Z80CpuNmi() acceptances (#33 bit6)

    // 37.5 kHz interrupt accounting (GS pitch-stability triage): the DAC
    // sample clock IS the interrupt clock - the firmware ISR performs
    // exactly one LD A,(DE) sample fetch per acceptance, so a module's
    // playback rate - and therefore its pitch - is the ACCEPTED rate, not
    // the generated one. Triage invariant:
    //   interruptPeriods == interruptsAccepted + interruptsCoalesced
    // (+1 for one request still in flight); a gap anywhere else means
    // samples are vanishing. interruptsCoalesced is the one honest loss: a
    // second 320-cycle boundary arrived while the previous request was
    // still pending (one flip-flop, so they merge) - real hardware loses
    // that sample too, the handler is genuinely slower than the period.
    uint64_t interruptPeriods = 0;     // 320-cycle boundaries crossed (INT flip-flop set events)
    uint64_t interruptsCoalesced = 0;  // boundaries that merged into a still-pending request

    uint64_t dacFetches = 0;           // Reads in 0x6000-0x7FFF (any channel)
    uint64_t volumeLatchWrites = 0;    // GS-side OUT to ports 0x06-0x09
    uint64_t hostCommandsReceived = 0; // ZX OUT #BB
    // Dead since the 2026-09-21 single-latch mailbox rewrite (was: 16-deep
    // command FIFO full). A same-direction OUT #BB before the card consumes
    // the pending one now just overwrites the latch - nothing increments
    // this on either personality. Kept for TTD/API layout stability; see
    // docs/inprogress/2026-09-19-general-sound/diagnostics-gaps-proposal.md
    // for a real command-clobber counter proposal.
    uint64_t hostCommandsDropped = 0;
    uint64_t hostDataWritten = 0;      // ZX OUT #B3
    // LLE: dead for the same reason as hostCommandsDropped (single latch,
    // no drop path). LW: counts overflow of the internal param-ordering
    // buffer (PARAM_QUEUE_CAPACITY=16 in soundchip_gslw.cpp), NOT a mailbox
    // FIFO - a real, LW-specific backlog condition.
    uint64_t hostDataDropped = 0;
    uint64_t hostDataRead = 0;          // ZX IN #B3
    int64_t  lastDacFetchGsCycle = -1; // totalGsCycles() at the most recent DAC fetch, -1 = never
    uint32_t lastDacFetchFrame = 0;    // Frame number of the most recent DAC fetch
};

/// Fold one card's counters into another across a runtime personality
/// switch (SoundManager::switchGeneralSoundCard): the monotonic counters
/// add up so triage totals survive the handoff (the per-card invariant
/// interruptPeriods == interruptsAccepted + interruptsCoalesced survives
/// summing both sides); lastDacFetch* keeps the most recent sighting.
/// Personalities without a coprocessor re-zero cpuSteps afterwards - their
/// data model defines it as always 0.
inline void accumulateGSActivityCounters(GSActivityCounters& dst, const GSActivityCounters& src)
{
    dst.cpuSteps += src.cpuSteps;
    dst.interruptsAccepted += src.interruptsAccepted;
    dst.nmisAccepted += src.nmisAccepted;
    dst.interruptPeriods += src.interruptPeriods;
    dst.interruptsCoalesced += src.interruptsCoalesced;
    dst.dacFetches += src.dacFetches;
    dst.volumeLatchWrites += src.volumeLatchWrites;
    dst.hostCommandsReceived += src.hostCommandsReceived;
    dst.hostCommandsDropped += src.hostCommandsDropped;
    dst.hostDataWritten += src.hostDataWritten;
    dst.hostDataDropped += src.hostDataDropped;
    dst.hostDataRead += src.hostDataRead;
    if (src.lastDacFetchGsCycle > dst.lastDacFetchGsCycle)
    {
        dst.lastDacFetchGsCycle = src.lastDacFetchGsCycle;
        dst.lastDacFetchFrame = src.lastDacFetchFrame;
    }
}

/// The card's face of its history: the session verbs of the GS port trace (start, stop, pause, resume, clear) on
/// whichever history the card records into. Single producer (the card, emulator thread), occasional consumers
/// (CLI / WebAPI / MCP / Lua / Python).
class GSPortTraceRecorder
{
public:
    static constexpr size_t kDefaultCapacity = 65536;  // 1.5 MB - GS traffic is orders of magnitude sparser than main-Z80

    GSPortTraceRecorder() : _own("gs", "GS port and activity events", kDefaultCapacity), _history(&_own) {}

    /// The machine's history (SoundManager), or the card's own for nullptr. Set with the emulation thread parked
    void attach(GSHistory* history, const EmulatorContext* context)
    {
        _history = history ? history : &_own;
        _context = context;
    }

    /// A new session: the ring empty, recording
    void start()
    {
        std::string error;
        _history->Configure({ .mode = history::Mode::Ring, .limit = kDefaultCapacity }, error);
        _stopped.store(false, std::memory_order_release);
    }
    /// The session ends; its events stay readable until the next start or clear
    void stop()
    {
        _history->Pause(true);
        _stopped.store(true, std::memory_order_release);
    }
    void pause() { _history->Pause(true); }
    void resume()
    {
        if (!_stopped.load(std::memory_order_acquire))
            _history->Pause(false);
    }
    void clear() { _history->Clear(); }

    bool isCapturing() const { return _history->On(); }
    bool isArmed() const
    {
        return !_stopped.load(std::memory_order_acquire) && _history->GetStats().config.mode != history::Mode::Off;
    }

    void record(const GSTraceEvent& event)
    {
        // A silent TTD replay re-executes what the history already holds
        if (_history->On() && !(_context && ReplayActive(_context)))
            _history->Push(event);
    }

    std::vector<GSTraceEvent> getAll() const { return _history->Snapshot(); }
    std::vector<GSTraceEvent> getLast(size_t count) const { return _history->Snapshot(count); }
    size_t eventCount() const { return static_cast<size_t>(_history->GetStats().kept); }
    uint64_t totalProduced() const { return _history->GetStats().written; }
    uint64_t totalEvicted() const { return _history->GetStats().dropped; }

private:
    static bool ReplayActive(const EmulatorContext* context);

    GSHistory _own;
    GSHistory* _history;
    const EmulatorContext* _context = nullptr;
    std::atomic<bool> _stopped{ false };
};
