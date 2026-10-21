#pragma once

/// @file activityfeed.h
/// @brief "Who did what": every action the registry ran, for display only (Stream Deck integration architecture §5,
/// protocol.md §4). A surface shows the actions run by the others: a deck flashes the key bound to the same id (the
/// default, owner decision Q-3), optionally writes "PAUSE · pad" on its strip; the look is the surface's (the
/// scenario's `settings.activity`, which also ignores `ttd.seek_by` by default: a Dial scrub already shows on the
/// TTD strip). Never executed, never journaled.
///
/// The actions worker records each job it ran (merged deltas are one job) after its result. The feed keeps the last
/// kCapacity entries; a Delta of the same action, instance, origin and session within kMergeMs of the entry it
/// would follow is merged into it (deltas summed, the higher rate kept; the merged entry gets a new seq and moves to
/// the end), so a Dial scrub is one line that updates, not fifty.
///
/// Readers: Since(seq) pages (the REST / CLI / script `activity` verb), and listeners (the WebSocket `surface`
/// topic, S4): called on the recording thread after each Record, outside the feed's lock; a listener only stores
/// and wakes its loop. Per-session rules (not its own actions, its target instance only, at most 20 per second,
/// `dropped`) are the consumer's.

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/actions/actionregistry.h"
#include "emulator/state/statenode.h"

namespace actions
{

struct ActivityEntry
{
    uint64_t seq = 0;
    std::string instance;
    std::string id;  ///< the action id
    ValueKind kind = ValueKind::Press;
    ActionValue value;  ///< as it ran (merged: the summed delta)
    execution::Origin origin = execution::Origin::Gui;
    std::string identity;  ///< ActionOrigin::identity (a companion's id, a device id, the gate identity)
    std::string session;   ///< ActionOrigin::session (a surface session never gets its own actions back)
    std::string surface;   ///< a human name: the device, the companion, "GUI", the agent's client name
    bool ok = false;
    std::string error;
    std::string message;
    int64_t tMs = 0;       ///< wall clock, ms since the epoch (the last merge)
    int64_t firstMs = 0;   ///< the first job merged into it
    uint32_t merged = 1;   ///< jobs in this entry
};

/// The origin as the protocol names it: gui, host_device, companion, webapi, mcp, cli, lua, python
const char* ActivityOriginName(execution::Origin origin);
/// The value as the protocol sends it: press {p}, delta {d, rate}, level {level}, text {text}; params beside them
StateNode ActivityValueNode(const ActionValue& value);
/// One entry as every plane reports it: {seq, instance, id, value, origin, surface, ok, error?, message?, t_ms,
/// merged}
StateNode ActivityEntryNode(const ActivityEntry& entry);

class ActivityFeed
{
public:
    static constexpr size_t kCapacity = 256;
    static constexpr int64_t kMergeMs = 100;

    using Clock = std::function<int64_t()>;  ///< ms since the epoch
    using Listener = std::function<void(const ActivityEntry& entry)>;

    /// The process's feed (never destroyed)
    static ActivityFeed& Instance();
    explicit ActivityFeed(Clock clock = {});

    /// A job ran (the actions worker)
    void Record(const std::string& instance, const ActionInfo& info, const ActionValue& value,
                const ActionOrigin& origin, const ActionResult& result);

    struct Page
    {
        std::vector<ActivityEntry> entries;  ///< oldest first
        uint64_t last = 0;                   ///< the newest seq in the feed (the next `since`)
        uint64_t dropped = 0;                ///< entries after `since` that the ring no longer holds
    };
    /// Entries newer than `since` (0: everything kept), of `instance` only when not "", at most `limit` (the newest)
    Page Since(uint64_t since, const std::string& instance = {}, size_t limit = 64) const;
    uint64_t LastSeq() const;

    /// Called after every Record (and merge) on the recording thread; keep it short. 0 is never an id
    uint64_t AddListener(Listener listener);
    void RemoveListener(uint64_t id);

    /// Tests
    void Clear();

private:
    Clock _clock;
    mutable std::mutex _mutex;
    std::deque<ActivityEntry> _entries;
    uint64_t _nextSeq = 1;
    /// The seqs the ring dropped, ascending (the last kEvictedKept; merged entries leave gaps, not drops)
    std::deque<uint64_t> _evicted;
    static constexpr size_t kEvictedKept = 4096;
    std::mutex _listenersMutex;
    std::map<uint64_t, std::shared_ptr<Listener>> _listeners;
    uint64_t _nextListener = 1;
};

}  // namespace actions
