#pragma once

/// @file drivehaptics.h
/// @brief The floppy drive under the hand (haptics.md §4.4): the machine's drive mechanics played on the pad. Every step
/// of the head is one short dry knock on the voice coils (a raw pulse a fraction of a millisecond long): a lone step
/// (the next track while reading) knocks once, a seek's series rattles at the step rate (6 ms per step: 167 knocks a
/// second) for as long as the steps last. The turning disk plays nothing: a hum or a swell on the motors felt too
/// strong (owner check 2026-11-13). A decorator: it plays the drive and passes everything else to the feed it
/// wraps (the game profile's or the sound haptics; none is fine).

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "emulator/io/controllers/ihapticfeed.h"
#include "haptics/hapticmixer.h"

namespace controllers
{

class DriveHaptics : public IHapticFeed
{
public:
    /// Plays a train of clicks on a device's voice coils or clicker (HostInputService: only while the service owns the
    /// pads and the gate is open)
    using ClicksFunction = std::function<void(const HostDeviceId& device, const HapticClicks& clicks)>;
    using ActuatorsFunction = std::function<uint8_t(const HostDeviceId& device)>;

    /// @p inner: the feed it wraps (may be null)
    DriveHaptics(std::shared_ptr<IHapticFeed> inner, HapticMixer& mixer, std::function<std::vector<HostDeviceId>()> targets,
                 ActuatorsFunction actuators, ClicksFunction clicks);

    void OnHapticFrame(const HapticFrame& frame) override;
    void OnHapticHold(bool held) override;
    std::vector<HapticWatch> Watches() const override;
    std::vector<HapticStream> Streams() const override;
    std::vector<HapticProbe> Probes() const override;
    std::vector<std::string> FrameConditions() const override;
    std::vector<GameEventInfo> GameEvents() const override;
    std::string GameEventsProfile() const override;
    bool TakeGameEvents(GameEventFrame& frame) override;

    /// TUNING (on a Steam Deck in the hand):
    /// - kPulseUs: one knock's length on the voice coils in microseconds; shorter is drier (owner check 2026-11-13:
    ///   600 us at 0 dB felt too long and too strong; 200 us at -14 / -16 dB was not felt at all: 400 us at -6 / -8 dB)
    /// - kTickLevel: a lone step's knock (0..1 maps to -24..0 dB); kSeekLevel: a seek's knocks
    /// - a step in the frame after another one continues its series (a seek's steps come a few per frame)
    /// - kSeekMaxMs: a fast-disk seek (all steps in one frame) plays at the drive's pace up to this long
    /// - kStepMotor: a pad without coils: a lone step is a short pulse of its high motor, a seek holds it at this level
    static constexpr uint16_t kPulseUs = 400;
    static constexpr float kTickLevel = 0.5f;  ///< -6 dB
    static constexpr float kSeekLevel = 0.4f;  ///< -8 dB
    static constexpr uint32_t kRingMs = 40;  ///< a pad without coils: its high motor's release after a seek
    static constexpr uint32_t kSeekMaxMs = 600;
    static constexpr uint32_t kMotorTickMs = 15;
    static constexpr float kStepMotor = 0.15f;
    static constexpr uint8_t kDefaultStepMs = 6;

    /// What the last frame did (tests)
    enum class Step : uint8_t
    {
        None,
        Tick,
        Seek,
    };
    struct Played
    {
        Step step = Step::None;
        uint16_t knocks = 0;
        uint16_t periodMs = 0;
    };
    const Played& Last() const
    {
        return _last;
    }

private:
    void PlaySteps(uint64_t frame, uint16_t steps, uint8_t stepMs);

    std::shared_ptr<IHapticFeed> _inner;
    HapticMixer& _mixer;
    std::function<std::vector<HostDeviceId>()> _targets;
    ActuatorsFunction _actuators;
    ClicksFunction _clicks;
    std::atomic<bool> _held{false};
    bool _stepped = false;     ///< a step was played
    uint64_t _stepFrame = 0;   ///< the emulated frame of the last step
    Played _last;
};

}  // namespace controllers
