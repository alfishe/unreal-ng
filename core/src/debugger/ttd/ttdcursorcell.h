#pragma once

/// @file ttdcursorcell.h
/// @brief The published TTD cursor (control surfaces architecture §7): one TTDCursor that the backend writes at every
/// frame boundary and at the end of every session operation, and that any thread reads without a lock.
///
/// A seqlock over relaxed atomics, not a 16-byte atomic (that is not lock-free on MSVC and needs libatomic on gcc),
/// and no data race in the C++ sense (TSan-clean). Writers are serialized by a mutex (the machine's thread at a frame
/// boundary and a control thread ending an operation may meet); a reader never takes it and retries only while a
/// write is in flight (a handful of stores). Publishing a value equal to the last one changes nothing, so
/// Sequence() moves only when the cursor did: a readout hub compares it to know whether to send.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "debugger/ttd/timetravelhooks.h"

class EmulatorContext;

namespace ttd
{

class TTDCursorCell
{
public:
    /// Store `cursor` (its `paused` is not stored: read it live). Equal to the last one: nothing changes. True when
    /// `history` flipped (a history appeared or went; before the first write there was none): the backend tells the
    /// surface context (NoteHistoryChange), whose default follows the history
    bool Publish(const TTDCursor& cursor)
    {
        std::lock_guard<std::mutex> lock(_writer);
        TTDCursor value = cursor;
        value.paused = false;
        if (_written && value == _last)
            return false;
        const bool flipped = (_written && _last.history) != value.history;
        _last = value;
        _written = true;
        const uint64_t sequence = _sequence.load(std::memory_order_relaxed);
        _sequence.store(sequence + 1, std::memory_order_relaxed);  // odd: a write in flight
        std::atomic_thread_fence(std::memory_order_release);
        _frame.store(value.frame, std::memory_order_relaxed);
        _first.store(value.first, std::memory_order_relaxed);
        _end.store(value.end, std::memory_order_relaxed);
        _tstates.store(static_cast<uint64_t>(value.tinframe) | (static_cast<uint64_t>(value.endTinframe) << 32),
                       std::memory_order_relaxed);
        _flags.store(static_cast<uint32_t>(value.state) | (value.history ? 0x100u : 0u), std::memory_order_relaxed);
        _sequence.store(sequence + 2, std::memory_order_release);
        return flipped;
    }

    /// The last cursor published (all zero before the first); never blocks on a writer for longer than its stores
    TTDCursor Read() const
    {
        TTDCursor cursor;
        for (unsigned attempt = 0;; attempt++)
        {
            const uint64_t before = _sequence.load(std::memory_order_acquire);
            if ((before & 1u) == 0)
            {
                cursor.frame = _frame.load(std::memory_order_relaxed);
                cursor.first = _first.load(std::memory_order_relaxed);
                cursor.end = _end.load(std::memory_order_relaxed);
                const uint64_t tstates = _tstates.load(std::memory_order_relaxed);
                const uint32_t flags = _flags.load(std::memory_order_relaxed);
                std::atomic_thread_fence(std::memory_order_acquire);
                if (_sequence.load(std::memory_order_relaxed) == before)
                {
                    cursor.tinframe = static_cast<uint32_t>(tstates & 0xFFFFFFFFu);
                    cursor.endTinframe = static_cast<uint32_t>(tstates >> 32);
                    cursor.state = static_cast<TTDSessionState>(flags & 0xFFu);
                    cursor.history = (flags & 0x100u) != 0;
                    return cursor;
                }
            }
            // A writer preempted in the middle of its stores: let it finish
            if (attempt >= 64)
                std::this_thread::yield();
        }
    }

    /// Moves (by 2) with every change of the cursor
    uint64_t Sequence() const { return _sequence.load(std::memory_order_acquire); }

private:
    std::mutex _writer;
    TTDCursor _last;       ///< guarded by _writer
    bool _written = false; ///< guarded by _writer
    std::atomic<uint64_t> _sequence{0};
    std::atomic<uint64_t> _frame{0};
    std::atomic<uint64_t> _first{0};
    std::atomic<uint64_t> _end{0};
    std::atomic<uint64_t> _tstates{0};  ///< tinframe | endTinframe << 32
    std::atomic<uint32_t> _flags{0};    ///< state | history << 8
};

/// Frames per second of the machine's own clock: base_z80_frequency / the unscaled frame length, so turbo does not
/// change a time code (Pentagon 3.5 MHz / 71680 T = 48.83 Hz). 50 without a context
double TTDFrameRate(const EmulatorContext* context);

/// The history of the context's instance appeared (`history`) or went: the surface context's default follows it
/// (actions::ContextState::HistoryChanged, owner decision Q-7). Called by a backend when Publish says so; one post at
/// most, never waits
void NoteHistoryChange(const EmulatorContext* context, bool history);

}  // namespace ttd
