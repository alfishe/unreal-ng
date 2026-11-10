#pragma once

/// @file scrubactions.h
/// @brief The analog scrub of the time-travel history (controller manager debug-and-ttd.md §3.2): `ttd.scrub_back`
/// and `ttd.scrub_forward` take a level 0..1 (a trigger's travel, `curve: scrub`; a button press is the full level),
/// each level a speed on the scrub curve, both held count against each other. A host clock (ScrubClock, 20 ms) turns
/// the frames the speed covered into `ttd.jog_frames` calls of the registry, so the scrub runs on every plane (a pad,
/// a deck key, the WebAPI) and also while the machine is paused. It stops on a level of 0, and at either end of the
/// history or on a refusal (the jog's result is in the activity feed); the next change of the level starts it again.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "emulator/actions/actionregistry.h"

namespace actions
{

/// The scrub curve: a trigger's travel 0..1 -> emulated frames per second. 0 below 0.08 (a resting finger), then 1,
/// 5, 50, 500 and 5000 frames/s at 0.08, 0.30, 0.60, 0.90 and 1.0, exponential between
float ScrubSpeed(float travel);

/// The scrub levels per instance and the clock that moves them. Thread-safe; its thread runs only while a scrub is on
class ScrubClock
{
public:
    /// Moves `instanceId` by `frames` (back < 0) covered at `rate` frames/s; called outside the clock's lock
    using Jog = std::function<void(const std::string& instanceId, int64_t frames, double rate, const ActionOrigin& origin)>;
    static constexpr std::chrono::milliseconds kTick{20};

    /// `threaded` false: no thread, the caller ticks (tests)
    explicit ScrubClock(Jog jog, bool threaded = true);
    ~ScrubClock();
    ScrubClock(const ScrubClock&) = delete;
    ScrubClock& operator=(const ScrubClock&) = delete;

    /// A level of one direction (0 releases it); the clock starts on the first one that is not 0
    void Set(const std::string& instanceId, bool forward, double level, const ActionOrigin& origin);
    void SetAt(const std::string& instanceId, bool forward, double level, const ActionOrigin& origin, uint64_t nowNs);
    /// The jog was refused (the end of the history, no history, a gate): the instance stops until its level changes
    void Stop(const std::string& instanceId);
    /// Jog every instance by the frames covered since its last tick (whole frames; the rest waits)
    void TickAt(uint64_t nowNs);
    /// Instances scrubbing now (tests)
    size_t Active() const;

private:
    struct State
    {
        double back = 0;
        double forward = 0;
        double owed = 0;  ///< frames covered and not moved yet (back < 0)
        uint64_t lastNs = 0;
        bool stopped = false;
        ActionOrigin origin;
    };
    static uint64_t NowNs();
    static bool Moving(const State& state);
    void Run();

    Jog _jog;
    const bool _threaded;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::map<std::string, State> _states;
    std::thread _thread;
    bool _running = false;  ///< the thread is in its loop
    bool _quit = false;
};

/// Registers ttd.scrub_back and ttd.scrub_forward on `registry`, driven by `clock`; the clock outlives the handles
std::vector<RegistrationHandle> RegisterScrubActions(ActionRegistry& registry, ScrubClock& clock);

}  // namespace actions
