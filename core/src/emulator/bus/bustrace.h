#pragma once

/// @file bustrace.h
/// @brief The bus analyzer's bounded rings of events and findings, and the queries every automation plane
/// shares (technical-design.md §3.1). Not thread-safe: BusTap guards it.
///
/// Time order (the MachineEventJournal rule): an event earlier than the newest one of the same epoch means the
/// machine went back in time (a TTD seek, then live running); the events after it belong to a future that no
/// longer happens and are dropped first (`rewound`). The epoch counts machine resets, where the frame counter
/// restarts.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "emulator/bus/bustraceevent.h"

/// `kinds`: event kind names, plus the groups "spi" (every SPI transaction kind) and "sd" (every SD protocol
/// kind). Empty = no filter
struct BusEventQuery
{
    std::vector<BusEventKind> kinds;
    int slot = -1;
    uint64_t since = 0;
    int64_t frameFrom = -1;
    int64_t frameTo = -1;
    size_t limit = 200;  ///< the newest N matches (0 = all)
};

struct BusFindingQuery
{
    std::vector<BusRule> rules;
    uint64_t since = 0;
    size_t limit = 200;
};

bool BusEventQueryFromStrings(const std::string& kinds, const std::string& slot, const std::string& since, const std::string& from,
                              const std::string& to, const std::string& limit, BusEventQuery& query, std::string& error);
bool BusFindingQueryFromStrings(const std::string& rules, const std::string& since, const std::string& limit, BusFindingQuery& query,
                                std::string& error);

/// A bounded ring that can also drop its newest entries (a rewind)
template <typename T>
class BusRing
{
public:
    void Allocate(size_t capacity)
    {
        _capacity = capacity ? capacity : 1;
        _ring = std::make_unique<T[]>(_capacity);
        _head = 0;
        _count = 0;
        _evicted = 0;
    }
    void Free()
    {
        _ring.reset();
        _capacity = 0;
        _head = 0;
        _count = 0;
    }
    bool Allocated() const { return _ring != nullptr; }
    size_t Capacity() const { return _capacity; }
    size_t Size() const { return _count; }
    uint64_t Evicted() const { return _evicted; }
    void Clear()
    {
        _head = 0;
        _count = 0;
        _evicted = 0;
    }
    void Push(const T& item)
    {
        if (!_ring)
            return;
        _ring[_head] = item;
        _head = (_head + 1) % _capacity;
        if (_count < _capacity)
            _count++;
        else
            _evicted++;
    }
    /// The newest entry (Size() > 0)
    const T& Back() const { return _ring[(_head + _capacity - 1) % _capacity]; }
    void PopBack()
    {
        if (!_count)
            return;
        _head = (_head + _capacity - 1) % _capacity;
        _count--;
    }
    /// Oldest first
    template <typename F>
    void ForEach(F&& fn) const
    {
        if (!_ring)
            return;
        const size_t first = (_head + _capacity - _count) % _capacity;
        for (size_t i = 0; i < _count; i++)
            fn(_ring[(first + i) % _capacity]);
    }

private:
    std::unique_ptr<T[]> _ring;
    size_t _capacity = 0;
    size_t _head = 0;
    size_t _count = 0;
    uint64_t _evicted = 0;
};

class BusTrace
{
public:
    static constexpr size_t kDefaultCapacity = 65536;
    static constexpr size_t kFindingCapacity = 4096;

    /// Allocate the rings (tracing on: events; checks on: findings). Content is dropped on a resize
    void EnableEvents(size_t capacity);
    void DisableEvents();
    void EnableFindings();
    void DisableFindings();
    bool EventsOn() const { return _events.Allocated(); }
    bool FindingsOn() const { return _findings.Allocated(); }
    void Clear();

    /// `event.seq` is assigned here; returns it
    uint64_t Append(BusTraceEvent event);
    uint64_t AddFinding(BusFinding finding);

    std::vector<BusTraceEvent> Events(const BusEventQuery& query) const;
    std::vector<BusFinding> Findings(const BusFindingQuery& query) const;

    size_t Size() const { return _events.Size(); }
    size_t Capacity() const { return _events.Capacity(); }
    uint64_t Evicted() const { return _events.Evicted(); }
    uint64_t Rewound() const { return _rewound; }
    uint64_t LastSeq() const { return _seq; }
    size_t FindingsSize() const { return _findings.Size(); }
    uint64_t FindingsEvicted() const { return _findings.Evicted(); }
    uint64_t FindingsTotal() const { return _findingSeq; }
    /// Findings raised per rule since the last clear (also the evicted ones)
    uint64_t FindingsOf(BusRule rule) const { return _byRule[static_cast<size_t>(rule)]; }

private:
    template <typename T>
    static bool Later(const T& a, const T& b)  // a after b, same epoch
    {
        return a.epoch == b.epoch && (a.frame > b.frame || (a.frame == b.frame && a.t > b.t));
    }

    BusRing<BusTraceEvent> _events;
    BusRing<BusFinding> _findings;
    uint64_t _seq = 0;
    uint64_t _findingSeq = 0;
    uint64_t _rewound = 0;
    uint64_t _byRule[static_cast<size_t>(BusRule::Count)] = {};
};
