#pragma once

/// @file breakpointhitlog.h
/// @brief The per-hit record of a breakpoint that only counts (PLAN #122 V4, docs/inprogress/2026-11-12-re-api-efficiency):
/// when each hit happened (frame, T), where (PC, the address or port, the value) and its number, bounded.
///
/// Written by the emulation thread inside the access (BreakpointManager's count sink), read by any surface without
/// holding the CPU: a short spin lock per record written and per chunk read, so a reader never parks the emulation and
/// the writer waits at most for one chunk's copy. A clear holds the CPU (the manager's ChangeScope).

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

/// One hit. Plain data: the log's storage is allocated without initialising it
struct BreakpointHitRecord
{
    uint64_t frame;   ///< the main CPU: emulatorState.frame_counter; a card CPU: the ZX frame the card runs in
    int64_t cpuTime;  ///< a card CPU: its time in its own unit (GeneralSoundCard::cardTime); -1 for the main CPU
    uint32_t hit;     ///< the breakpoint's hit count after this hit (its number; the hit policy may skip some)
    uint32_t t;       ///< the main CPU: z80 t in the frame; a card CPU: its time since the frame's start, its unit
    uint16_t pc;      ///< the instruction (execute: the one about to run; an access: the instruction doing it)
    uint16_t address; ///< the CPU address, the port, the event kind or the chip register
    int16_t value;    ///< the byte an OUT, an event or a chip register write carried; -1 when none came with the hit
    uint8_t kind;     ///< BreakpointTapKind
    uint8_t reserved;
};

enum class BreakpointHitLogKeep : uint8_t
{
    First,  ///< the first `capacity` hits; later ones are only counted (dropped)
    Last    ///< the newest `capacity` hits; older ones are overwritten (dropped)
};

class BreakpointHitLog
{
public:
    static constexpr uint32_t kDefaultCapacity = 1000;
    static constexpr uint32_t kMaxCapacity = 100000;
    static constexpr uint32_t kReadChunk = 256;

    BreakpointHitLog(uint32_t capacity, BreakpointHitLogKeep keep)
        : _capacity(std::clamp<uint32_t>(capacity, 1, kMaxCapacity)),
          _keep(keep),
          _slots(new BreakpointHitRecord[_capacity])
    {
    }

    uint32_t Capacity() const { return _capacity; }
    BreakpointHitLogKeep Keep() const { return _keep; }

    /// The emulation thread: false once a record that keeps the first hits is full (the hit is only counted, Skip)
    bool Wants() const
    {
        return _keep == BreakpointHitLogKeep::Last || _total.load(std::memory_order_relaxed) < _capacity;
    }

    /// The emulation thread: a hit the full record does not keep, counted without the lock (the slots are frozen)
    void Skip() { _total.store(_total.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

    /// The emulation thread, inside the access
    void Add(const BreakpointHitRecord& record)
    {
        const uint64_t total = _total.load(std::memory_order_relaxed);
        if (_keep == BreakpointHitLogKeep::First && total >= _capacity)
        {
            Skip();
            return;
        }
        Lock();
        _slots[_keep == BreakpointHitLogKeep::Last ? total % _capacity : total] = record;
        _total.store(total + 1, std::memory_order_release);
        Unlock();
    }

    /// With the CPU held (a reset of the hit counts)
    void Clear()
    {
        Lock();
        _total.store(0, std::memory_order_release);
        Unlock();
    }

    struct View
    {
        uint32_t capacity = 0;
        BreakpointHitLogKeep keep = BreakpointHitLogKeep::First;
        uint64_t total = 0;       ///< hits offered to the log since the last reset
        uint64_t stored = 0;      ///< hits it holds (at most capacity)
        uint64_t dropped = 0;     ///< total - stored: after the first `capacity` (First) or overwritten (Last)
        uint64_t oldest = 0;      ///< the index of the oldest record held (0 for First; hit records are indexed from 0)
        uint64_t firstIndex = 0;  ///< the index of records[0]
        bool more = false;        ///< records after the last one returned (ask again from firstIndex + records.size())
        std::vector<BreakpointHitRecord> records;
    };

    /// Any thread: up to `limit` records from index `from` on (an index below the oldest held starts at the oldest),
    /// as they were when the read started
    View Read(uint64_t from, uint32_t limit) const
    {
        View view;
        view.capacity = _capacity;
        view.keep = _keep;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint64_t total = _total.load(std::memory_order_acquire);
            view.total = total;
            view.stored = std::min<uint64_t>(total, _capacity);
            view.dropped = total - view.stored;
            view.oldest = _keep == BreakpointHitLogKeep::Last ? total - view.stored : 0;
            const uint64_t end = view.oldest + view.stored;
            const uint64_t start = std::max(from, view.oldest);
            const uint64_t stop = start < end ? std::min<uint64_t>(end, start + limit) : start;
            view.firstIndex = start;
            view.more = stop < end;
            view.records.clear();
            view.records.reserve(static_cast<size_t>(stop - start));
            bool lapped = false;
            for (uint64_t i = start; i < stop && !lapped;)
            {
                const uint64_t chunkEnd = std::min<uint64_t>(stop, i + kReadChunk);
                Lock();
                // The newest hits overwrite the oldest (Last): what the writer lapped since the read began is gone
                const uint64_t now = _total.load(std::memory_order_acquire);
                lapped = _keep == BreakpointHitLogKeep::Last && now > _capacity && i < now - _capacity;
                if (!lapped)
                    for (; i < chunkEnd; ++i)
                        view.records.push_back(_slots[_keep == BreakpointHitLogKeep::Last ? i % _capacity : i]);
                Unlock();
            }
            if (!lapped)
                return view;
        }
        // Lapped on every attempt (hits faster than a chunk's copy): only the counts
        view.records.clear();
        view.more = view.stored > 0;
        return view;
    }

private:
    void Lock() const
    {
        for (uint32_t spins = 0; _lock.test_and_set(std::memory_order_acquire); ++spins)
            if (spins >= 64)
                std::this_thread::yield();
    }
    void Unlock() const { _lock.clear(std::memory_order_release); }

    const uint32_t _capacity;
    const BreakpointHitLogKeep _keep;
    std::unique_ptr<BreakpointHitRecord[]> _slots;
    // Written by the emulation thread only (Clear: with the CPU held); a slot is written under the lock before the
    // count that publishes it
    std::atomic<uint64_t> _total{0};
    mutable std::atomic_flag _lock = ATOMIC_FLAG_INIT;
};
