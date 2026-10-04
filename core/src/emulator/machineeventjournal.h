#pragma once

/// @file machineeventjournal.h
/// @brief A machine's own configuration events in time order: what changed the machine's setup, when
/// and from which instruction (Sprinter: the PLD port table, CNF / turbo, ALL_MODE, RGMOD, HOLD, the
/// frame length, #7FFD / #1FFD, the bitstream load, F12, Ctrl+Alt+Del, resets).
///
/// The video change log (videowritelog.h) keeps the last two frames of video latch writes; this journal
/// keeps the events that answer "who turned turbo on", "who wrote #1FFD" over a whole session. The
/// video change log's frames list the journal's events of the same frame (/video/changes), and a machine
/// that keeps one exposes it through PortDecoder::GetMachineEventJournal.
///
/// Cost: the machine appends from code that already runs for the change (a port handler's case, the
/// frame end); nothing when the journal is off. A full journal drops its oldest events (`dropped`).
///
/// Time: frame (EmulatorState::frame_counter), T in the frame (base T-states, 3.5 MHz) and PC, plus an
/// epoch that counts the machine's resets (the frame counter restarts there). Events are kept in time
/// order: an event earlier than the newest one in the same epoch means the machine went back in time
/// (TTD seek, then live again), and the events after it - a future that no longer happens - are removed
/// first. The machine does not append while a TTD replay re-executes recorded history
/// (EmulatorContext::ttdReplayActive), so a seek never duplicates events.
///
/// Threading: the emulator thread appends; any thread reads a copy (one mutex, cold paths only).
///
/// Worked example (Sprinter, SP.ZX): the launcher's stub writes CNF #07 at frame 1312, T 40210, PC #5B0E:
/// event {kind "cnf", port #0024 ..., value #07, text "CNF/SYS #07: map 0, turbo requested, #7FFD and #1FFD on"}.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct MachineEvent
{
    uint64_t seq = 0;      ///< 1, 2, ... in append order (kept across Clear, so a reader can ask "since")
    uint32_t epoch = 0;    ///< the machine's resets seen by the journal (the frame counter restarts at each)
    uint64_t frame = 0;    ///< EmulatorState::frame_counter
    uint32_t t = 0;        ///< T in the frame (base T-states, 3.5 MHz)
    uint16_t pc = 0;       ///< the instruction that caused it (0 for a host action: a key, the RESET button)
    int32_t port = -1;     ///< the port address of the write, -1 = none
    int32_t value = -1;    ///< the value written / the new state, -1 = none
    int32_t previous = -1; ///< the state before, -1 = unknown
    const char* kind = ""; ///< a static name ("cnf", "port_7ffd", "reset" ...)
    std::string text;      ///< one line for people
    std::vector<std::string> details;  ///< more lines (the port decodes a table write changed ...)
};

class MachineEventJournal
{
public:
    static constexpr size_t kCapacity = 8192;

    /// What a reader asks for
    struct Filter
    {
        std::vector<std::string> kinds;  ///< empty = all
        uint64_t sinceSeq = 0;           ///< events with seq > sinceSeq
        int64_t frameFrom = -1;          ///< -1 = any
        int64_t frameTo = -1;
        size_t limit = 200;              ///< the newest `limit` of the matches (0 = all)
    };

    struct Snapshot
    {
        std::vector<MachineEvent> events;  ///< matches, oldest first
        size_t matched = 0;                ///< matches before the limit
        uint64_t appended = 0;             ///< events appended since the last Clear
        uint64_t dropped = 0;              ///< removed because the journal was full
        uint64_t rewound = 0;              ///< removed because the machine went back in time (TTD)
        size_t held = 0;
        bool enabled = false;
        uint32_t epoch = 0;
    };

    bool Enabled() const { return _enabled; }
    void SetEnabled(bool on);
    void Clear();

    /// The machine reset (frame counter back to 0): later events start a new epoch
    void NextEpoch();
    uint32_t Epoch() const;

    /// Emulator thread. `event.epoch` and `seq` are filled here
    void Append(MachineEvent event);

    Snapshot Read(const Filter& filter) const;

    /// "a,b,c" -> {"a", "b", "c"} (spaces ignored)
    static std::vector<std::string> SplitKinds(const std::string& text);

private:
    mutable std::mutex _mutex;
    std::deque<MachineEvent> _events;
    bool _enabled = true;
    uint32_t _epoch = 0;
    uint64_t _nextSeq = 1;
    uint64_t _appended = 0;
    uint64_t _dropped = 0;
    uint64_t _rewound = 0;
};
