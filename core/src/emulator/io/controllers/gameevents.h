#pragma once

/// @file gameevents.h
/// @brief A game's events (docs/inprogress/2026-10-26-breakpoint-taps/README.md §7): the facts a play profile names
/// (`events:`, or `haptics.sources`) as the haptic feed found them each live frame, for every consumer, not only the
/// motors: the `game.events` monitor (monitoringapi.h: REST, WebSocket topic, surface readout, CLI, Lua, Python, MCP),
/// a Stream Deck key's `show:`, a script. The feed defines the names when it is set (IHapticFeed::GameEvents) and
/// reports a frame after each OnHapticFrame (IHapticFeed::TakeGameEvents); a replay or seek reports nothing (the feed
/// is held), so the log holds live play only.

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace controllers
{

/// One name of a profile's events
struct GameEventInfo
{
    std::string name;
    /// An event fires on a frame (exec, a write, an envelope restart); a level is a value every frame (a watched
    /// byte, a sound's envelope)
    bool level = false;
};

/// What the feed found in one frame, in the order of its GameEventInfo list
struct GameEventFrame
{
    static constexpr unsigned kMax = 32;
    uint64_t frame = 0;
    uint32_t fired = 0;                ///< bit i: event i fired (or level i was above 0)
    std::array<float, kMax> level{};   ///< 0..1 (an event: 1 when it fired)
    std::array<int32_t, kMax> value{}; ///< the watched byte (a word source: the word); -1 when the source reads none
};

class GameEventLog
{
public:
    static constexpr size_t kRecent = 64;  ///< the fired events kept, oldest dropped first

    struct Event
    {
        std::string name;
        bool level = false;
        uint64_t fires = 0;      ///< frames it fired on since it was defined
        uint64_t lastFrame = 0;  ///< 0: never
        float current = 0;       ///< the last frame's level
        int32_t value = -1;
    };
    struct Fired
    {
        uint64_t seq = 0;  ///< 1, 2, ... since the definition: a poller asks for those after the last it saw
        uint64_t frame = 0;
        std::string name;
    };
    struct Snapshot
    {
        bool defined = false;
        std::string profile;
        uint64_t frame = 0;  ///< the last frame reported
        uint64_t seq = 0;    ///< the newest Fired::seq
        std::vector<Event> events;
        std::vector<Fired> recent;  ///< oldest first
    };

    /// The feed's names (at most GameEventFrame::kMax) and its profile; the counters start over. Any thread
    void Define(const std::vector<GameEventInfo>& events, const std::string& profile);
    /// No feed, or one without events
    void Clear();
    /// One frame (the emulation thread, at the frame end)
    void Record(const GameEventFrame& frame);
    Snapshot Read() const;
    /// Changes when the definition or a reported value changes (a frame with nothing new keeps it)
    uint64_t Revision() const;

private:
    mutable std::mutex _mutex;
    Snapshot _state;
    uint64_t _revision = 0;
};

}  // namespace controllers
