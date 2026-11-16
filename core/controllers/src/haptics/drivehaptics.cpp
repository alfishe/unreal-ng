#include "haptics/drivehaptics.h"

#include <algorithm>

namespace controllers
{

namespace
{
constexpr uint8_t kCoils = kRumbleHdLeft | kRumbleHdRight | kHapticClicker;
constexpr uint8_t kMotors = kRumbleLow | kRumbleHigh;
}  // namespace

DriveHaptics::DriveHaptics(std::shared_ptr<IHapticFeed> inner, HapticMixer& mixer,
                           std::function<std::vector<HostDeviceId>()> targets, ActuatorsFunction actuators,
                           ClicksFunction clicks)
    : _inner(std::move(inner)), _mixer(mixer), _targets(std::move(targets)), _actuators(std::move(actuators)),
      _clicks(std::move(clicks))
{
}

void DriveHaptics::OnHapticFrame(const HapticFrame& frame)
{
    if (_inner)
        _inner->OnHapticFrame(frame);
    _last = {};
    if (_held.load(std::memory_order_acquire) || !frame.drive.present)
        return;
    const uint8_t stepMs = frame.drive.stepMs > 0 ? frame.drive.stepMs : kDefaultStepMs;
    if (frame.drive.steps > 0)
        PlaySteps(frame.frame, frame.drive.steps, stepMs);
}

void DriveHaptics::PlaySteps(uint64_t frame, uint16_t steps, uint8_t stepMs)
{
    // A frame's steps play when the frame is fed, never queued behind earlier ones: the emulator runs frames in bursts
    // ahead of the audio, so a queue on the host's clock fell behind the drive (owner check 2026-11-13: the knocks
    // lagged the HUD's track). Whether a step is lone or part of a seek is told by the emulated frames
    const bool joined = _stepped && frame - _stepFrame <= 1;
    _stepped = true;
    _stepFrame = frame;
    HapticClicks clicks;
    clicks.wave = HapticClicks::Wave::Click;
    clicks.pulseUs = kPulseUs;
    clicks.periodMs = stepMs;
    // A fast disk packs a whole seek into a frame: it plays at the drive's pace, at most kSeekMaxMs (the next frame's
    // train replaces it)
    clicks.count = static_cast<uint8_t>(std::min<uint32_t>({steps, std::max<uint32_t>(1, kSeekMaxMs / stepMs), 255u}));
    HapticEffect motor;
    motor.levels.high = kStepMotor;
    if (!joined && steps == 1)
    {
        // A lone step (the next track): one dry knock
        clicks.level = kTickLevel;
        motor.durationMs = kMotorTickMs;
        _last.step = Step::Tick;
    }
    else
    {
        // A seek: a knock per step at the step rate
        clicks.level = kSeekLevel;
        motor.durationMs = uint32_t(clicks.count) * stepMs + kRingMs;
        motor.shape = HapticShape::Adsr;
        motor.releaseMs = kRingMs;
        _last.step = Step::Seek;
    }
    _last.knocks = clicks.count;
    _last.periodMs = stepMs;
    for (const HostDeviceId& device : _targets())
    {
        const uint8_t actuators = _actuators ? _actuators(device) : 0;
        if ((actuators & kCoils) && _clicks)
            _clicks(device, clicks);
        else if (actuators & kMotors)
            _mixer.Play(device, motor);
    }
}

void DriveHaptics::OnHapticHold(bool held)
{
    _held.store(held, std::memory_order_release);
    if (_inner)
        _inner->OnHapticHold(held);
    if (!held)
        return;
    // A replay or a seek: the series ends (a train already sent plays out)
    _stepped = false;
}

std::vector<HapticWatch> DriveHaptics::Watches() const
{
    return _inner ? _inner->Watches() : std::vector<HapticWatch>{};
}

std::vector<HapticStream> DriveHaptics::Streams() const
{
    return _inner ? _inner->Streams() : std::vector<HapticStream>{};
}

std::vector<HapticProbe> DriveHaptics::Probes() const
{
    return _inner ? _inner->Probes() : std::vector<HapticProbe>{};
}

std::vector<std::string> DriveHaptics::FrameConditions() const
{
    return _inner ? _inner->FrameConditions() : std::vector<std::string>{};
}

std::vector<GameEventInfo> DriveHaptics::GameEvents() const
{
    return _inner ? _inner->GameEvents() : std::vector<GameEventInfo>{};
}

std::string DriveHaptics::GameEventsProfile() const
{
    return _inner ? _inner->GameEventsProfile() : std::string{};
}

bool DriveHaptics::TakeGameEvents(GameEventFrame& frame)
{
    return _inner && _inner->TakeGameEvents(frame);
}

}  // namespace controllers
