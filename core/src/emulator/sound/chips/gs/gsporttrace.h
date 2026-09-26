#pragma once
#include "stdafx.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "common/ringbuffer.h"

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
/// Runtime-gated by FeatureManager feature "porttrace_gs" (alias "ptgs").
/// ============================================================================

/// Which side of the card originated the event
enum class GSTraceSide : uint8_t
{
    Host = 0,       // ZX-side port access (#B3/#BB/#33), main Z80 is the actor
    GsInternal = 1, // GS-side port access (0x00-0x0B), GS Z80 is the actor
    DacFetch = 2,   // GS Z80 read in 0x6000-0x7FFF: DAC channel sample latch
    Interrupt = 3,  // 37.5 kHz periodic interrupt accepted (or NMI)
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
    uint16_t port = 0;        // Host: #B3/#BB/#33. GsInternal: 0x00-0x0B. DacFetch: 0x6000-0x7FFF address. Interrupt: unused (0)
    uint16_t pc = 0;          // GS CPU PC at the time of the event
    uint8_t  value = 0;       // Data byte (port value, or DAC sample byte)
    uint8_t  channel = 0;     // DacFetch only: channel index 0-3
    GSTraceSide side = GSTraceSide::Host;
    uint8_t  flags = 0;       // GSTraceFlags bitfield

    bool isOut() const { return flags & GSTraceFlags::kDirectionOut; }
    bool isNmi() const { return flags & GSTraceFlags::kNmi; }
};

/// Cheap always-on activity counters (no ring buffer, no feature flag) - the
/// first thing to check when triaging "is the GS coprocessor doing anything
/// at all": cpuSteps/interruptsAccepted prove the core is executing,
/// dacFetches/lastDacFetchGsCycle prove it is actually producing audio data.
struct GSActivityCounters
{
    uint64_t cpuSteps = 0;             // z80ex_step() calls that returned > 0 t-states
    uint64_t interruptsAccepted = 0;   // z80ex_int() acceptances (37.5 kHz periodic)
    uint64_t nmisAccepted = 0;         // z80ex_nmi() acceptances (#33 bit6)

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

/// Single-producer (GS coprocessor callbacks, emulator thread), occasional
/// consumers (CLI/WebAPI/MCP/Lua/Python). Deliberately smaller/simpler than
/// PortDiagnosticRecorder (no filter DSL, no export formats) - the GS port
/// space is 12 addresses wide and a raw capture is already small.
class GSPortTraceRecorder
{
public:
    static constexpr size_t kDefaultCapacity = 65536;  // 1.5 MB - GS traffic is orders of magnitude sparser than main-Z80

    GSPortTraceRecorder() : _events(std::make_unique<RingBuffer<GSTraceEvent>>(kDefaultCapacity)) {}

    void start()
    {
        _events->clear();
        _capturing.store(true, std::memory_order_release);
    }
    void stop() { _capturing.store(false, std::memory_order_release); }
    void pause() { _paused.store(true, std::memory_order_release); }
    void resume() { _paused.store(false, std::memory_order_release); }
    void clear() { _events->clear(); }

    bool isCapturing() const
    {
        return _capturing.load(std::memory_order_acquire) && !_paused.load(std::memory_order_acquire);
    }
    bool isArmed() const { return _capturing.load(std::memory_order_acquire); }

    void record(const GSTraceEvent& event)
    {
        if (isCapturing())
            _events->push(event);
    }

    std::vector<GSTraceEvent> getAll() const { return _events->getAll(); }
    std::vector<GSTraceEvent> getLast(size_t count) const
    {
        std::vector<GSTraceEvent> all = _events->getAll();
        if (all.size() <= count)
            return all;
        return std::vector<GSTraceEvent>(all.end() - static_cast<long>(count), all.end());
    }
    size_t eventCount() const { return _events->size(); }
    uint64_t totalProduced() const { return _events->totalEventsProduced(); }
    uint64_t totalEvicted() const { return _events->totalEventsEvicted(); }

private:
    std::atomic<bool> _capturing{false};
    std::atomic<bool> _paused{false};
    std::unique_ptr<RingBuffer<GSTraceEvent>> _events;
};
