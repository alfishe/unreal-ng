#pragma once

/// @file nextjournal.h
/// @brief The store of the Next's journals (NextREG writes, interrupts, SD, DivMMC): a device history on the pattern
/// (debugger/history/devicehistory.h), registered by its owner under the device's name, behind the journals' own
/// controls (enable, capacity, clear) and queries (seq cursor, filters), which every automation plane already uses.
/// Enable is a ring of the journal's capacity; disable holds it (the events stay readable, nothing more is recorded).

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "debugger/history/devicehistory.h"

template <typename Event>
class NextJournal
{
public:
    NextJournal(std::string device, std::string what, size_t capacity)
        : _history(std::move(device), std::move(what), std::max<size_t>(capacity, 1)),
          _capacity(std::max<size_t>(capacity, 1))
    {
    }

    /// The history the owner registers (EmulatorContext::histories)
    history::DeviceHistory<Event>& History() { return _history; }

    bool Enabled() const { return _history.On(); }
    void SetEnabled(bool on)
    {
        const history::Stats stats = _history.GetStats();
        if (stats.config.mode == history::Mode::Off)
        {
            if (on)
                Configure(history::Mode::Ring);
        }
        else
            _history.Pause(!on);
    }
    /// Resizes the ring (the content is dropped)
    void SetCapacity(size_t capacity)
    {
        _capacity = std::max<size_t>(capacity, 1);
        const history::Stats stats = _history.GetStats();
        if (stats.config.mode != history::Mode::Off)
        {
            Configure(stats.config.mode);
            if (stats.paused)
                _history.Pause(true);
        }
    }
    size_t Capacity() const
    {
        const history::Stats stats = _history.GetStats();
        return stats.config.mode == history::Mode::Off ? _capacity : static_cast<size_t>(stats.config.limit);
    }
    size_t Size() const { return static_cast<size_t>(_history.GetStats().kept); }
    uint64_t Evicted() const { return _history.GetStats().dropped; }
    /// The seq of the newest event (0: none yet); never goes back, a reader keeps its cursor over a clear
    uint64_t LastSeq() const { return _seq.load(std::memory_order_relaxed); }
    void Clear() { _history.Clear(); }

    /// `event.seq` is assigned here; nothing while the journal is off or held
    void Record(Event event)
    {
        if (!_history.On())
            return;
        event.seq = _seq.fetch_add(1, std::memory_order_relaxed) + 1;
        _history.Push(event);
    }

protected:
    /// The events after `since` that `match` accepts, oldest first, the newest `limit` of them (0: all)
    template <typename Match>
    std::vector<Event> Select(uint64_t since, size_t limit, Match&& match) const
    {
        std::vector<Event> out;
        for (const Event& e : _history.Snapshot())
            if (e.seq > since && match(e))
                out.push_back(e);
        if (limit && out.size() > limit)
            out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(limit));
        return out;
    }

private:
    void Configure(history::Mode mode)
    {
        std::string error;
        _history.Configure({ .mode = mode, .limit = _capacity }, error);
    }

    history::DeviceHistory<Event> _history;
    size_t _capacity;
    std::atomic<uint64_t> _seq{ 0 };
};
