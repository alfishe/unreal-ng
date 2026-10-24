#pragma once

/// @file hapticmixer.h
/// @brief The haptic effect mixer and gate (haptics.md §3, §6.3, H-4, H-5): effects per device with their envelopes,
/// mixed by the loudest channel, lower priorities ducked under a higher one, written to the backend that owns the
/// device; a closed gate stops every motor at once and drops the effects. Host output only: nothing here is journaled
/// or reaches the emulation.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "hostbackend.h"

namespace controllers
{

/// haptics.md §6.1 `shape`: `pulse` holds the levels, `adsr` shapes them, `pattern` steps through levels
enum class HapticShape : uint8_t
{
    Pulse,
    Adsr,
    Pattern,
};

/// One effect: peak levels, a length and an envelope (haptics.md §6.1)
struct HapticEffect
{
    HapticEffect() = default;
    HapticEffect(const RumbleLevels& peak, uint32_t ms, bool isFeedback = false)
        : levels(peak), durationMs(ms), feedback(isFeedback)
    {
    }

    RumbleLevels levels;
    uint32_t durationMs = 100;
    /// The debug feedback (debug-and-ttd.md §7: a breakpoint hit, identify): plays through a closed gate, which holds
    /// back only the game's haptics
    bool feedback = false;

    HapticShape shape = HapticShape::Pulse;
    /// adsr: attack to the peak, decay to `sustain` times the peak, hold, release to zero within durationMs
    uint32_t attackMs = 0;
    uint32_t decayMs = 0;
    float sustain = 1.0f;
    uint32_t releaseMs = 0;
    /// pattern: (step length ms, level 0..1) repeated over durationMs
    std::vector<std::pair<uint32_t, float>> pattern;
    /// The mixer ducks lower priorities while a higher one plays (haptics.md §6.3)
    uint8_t priority = 0;

    /// The envelope 0..1 at @p ms from the start
    float Envelope(uint32_t ms) const;
};

class HapticMixer
{
public:
    using Clock = std::chrono::steady_clock;
    /// Writes a device's actuators; false when the device has none (the mixer then forgets it)
    using Writer = std::function<bool(const HostDeviceId& device, const RumbleLevels& levels)>;

    /// While a shaped effect plays, the levels are written at this period (haptics.md §6.3: HID rate)
    static constexpr std::chrono::milliseconds kEnvelopePeriod{8};
    /// A lower priority plays at this share under a higher one
    static constexpr float kDuck = 0.25f;

    /// @p threaded: a worker ends the effects on time; tests pass false and call Tick
    explicit HapticMixer(Writer writer, bool threaded = true);
    ~HapticMixer();
    HapticMixer(const HapticMixer&) = delete;
    HapticMixer& operator=(const HapticMixer&) = delete;

    /// A game effect is ignored while the gate is closed; a feedback one plays
    void Play(const HostDeviceId& device, const HapticEffect& effect);
    /// H-5: closed on pause, seek, replay, turbo, focus loss; closing stops the game's effects at once
    void SetGate(bool open);
    bool GateOpen() const;
    /// The master strength 0..1 (H-6)
    void SetStrength(float strength);
    /// The instance a device follows started replaying (IHapticFeed::OnHapticHold): its game effects stop at once,
    /// feedback stays; the gate of the other instances is not touched
    void StopGameEffects(const HostDeviceId& device);
    /// The device went away: its effects are dropped without a write
    void Forget(const HostDeviceId& device);
    /// Stop every motor (exit)
    void StopAll();

    /// Ends the effects due by @p now and writes the levels that changed
    void Tick(Clock::time_point now);
    /// What was last written to a device (tests, the debug view)
    RumbleLevels Written(const HostDeviceId& device) const;

private:
    struct Active
    {
        HapticEffect effect;
        Clock::time_point start;
        Clock::time_point end;
    };
    void TickLocked(Clock::time_point now, std::vector<std::pair<HostDeviceId, RumbleLevels>>& writes);
    void Write(const std::vector<std::pair<HostDeviceId, RumbleLevels>>& writes);
    void Run();

    Writer _writer;
    mutable std::mutex _mutex;
    std::condition_variable _wake;
    std::map<HostDeviceId, std::vector<Active>> _active;
    std::map<HostDeviceId, RumbleLevels> _written;
    bool _gate = true;
    float _strength = 1.0f;
    bool _stopping = false;
    std::thread _thread;
};

}  // namespace controllers
