#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/audioactivityindicators.h"

/// @file audioactivityindicators_test.cpp
/// @brief Regression: SoundManager::handleFrameEnd() must only drive the
/// TurboSound-slot device's handleFrameEnd() ONCE per frame.
///
/// `SoundManager::handleFrameEnd()` used to call
/// `_turboSound->handleFrameEnd()` twice: once at the top of the function
/// (required - it drains TSFM's word queues to end-of-frame before the mix)
/// and again later "to finalize the HUD notification". Both
/// `SoundChip_TurboSound::handleFrameEnd()` and
/// `SoundChip_TurboSoundFM::handleFrameEnd()` post `NC_AUDIO_ACTIVITY`
/// whenever `_frameHadActivity` is true, and neither call resets that flag
/// (or `_wasActive`/`_wasTurboSound`/`_wasFM`) - so while a generator is
/// active, EVERY frame posted two identical `NC_AUDIO_ACTIVITY` events
/// instead of one. The second call's word-queue drain was a harmless no-op
/// (nothing new lands in the queue between the two calls within one frame),
/// so the fix is simply removing the second call, not resetting flags.
///
/// Activity itself is measured on the sources' rendered output, not on
/// register writes: the audio-settings LED is the per-frame flag, the HUD
/// nudge a one-second hold of it (AudioActivityIndicators). The suites
/// below pin that contract, including the 128K ROM case that motivated it -
/// the ROM writes the AY at start-up and polls the keypad through the AY I/O
/// port, which must light neither.

namespace
{

/// Counts NC_AUDIO_ACTIVITY deliveries for one AudioSource, by emulator id.
struct AudioActivityCounter : public Observer
{
    unreal::UUID watchId;
    AudioSource watchSource;
    std::atomic<int> hits{0};

    AudioActivityCounter(const unreal::UUID& id, AudioSource source)
        : watchId(id), watchSource(source)
    {
    }

    // Observer base has no virtual method; signature must match ObserverCallbackMethod.
    void onEvent(int /*id*/, Message* message)
    {
        auto* p = dynamic_cast<AudioActivityPayload*>(message->obj);
        if (!p)
            return;
        if (p->emulatorId == watchId && p->source == watchSource)
            hits.fetch_add(1);
    }
};

} // namespace

class AudioActivityNotification_Test : public ::testing::TestWithParam<TurboSoundKind>
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    std::unique_ptr<SoundManager> _sound;

    static constexpr uint32_t PENTAGON_FRAME = 71680;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->config.frame = PENTAGON_FRAME;
        _context->config.sound.turboSoundKind = GetParam();

        _sound = std::make_unique<SoundManager>(_context);
        _sound->reset();

        ASSERT_NE(_sound->getTurboSound(), nullptr);
        ASSERT_EQ(_sound->getTurboSound()->hasFm(), GetParam() == TurboSoundKind::FM);
    }

    void TearDown() override
    {
        _sound.reset();
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    void SetT(uint32_t t) { _context->pCore->GetZ80()->tt = t << 8; }

    void WriteAy(uint8_t reg, uint8_t value)
    {
        _sound->getTurboSound()->portDeviceOutMethod(PORT_FFFD, reg);
        _sound->getTurboSound()->portDeviceOutMethod(PORT_BFFD, value);
    }

    /// Program a loud steady tone on AY channel A. Register config (period,
    /// mixer, volume) persists across frames like real hardware, so the tone
    /// keeps sounding - and the device keeps reporting activity - every frame.
    void StartAyTone()
    {
        WriteAy(0, 0x20);   // Tone A period low
        WriteAy(1, 0x00);   // Tone A period high
        WriteAy(7, 0x3E);   // Mixer: tone A on, everything else off
        WriteAy(8, 0x0F);   // Channel A volume: max, no envelope
    }

    /// Idempotent re-write of the channel A volume register, same as a real
    /// tune's per-frame register refresh, without changing anything audible.
    void TouchAyRegister() { WriteAy(8, 0x0F); }

    void StopAyTone() { WriteAy(8, 0x00); }

    /// Audio-settings LED of AY chip 0 / the HUD hold of it
    bool Led() const { return _sound->device(AudioSourceType::AY1_All)->activeRecently; }
    bool HudHold() const { return _sound->getActivityIndicators().held(AudioSourceType::AY1_All); }

    /// One emulated frame with no register traffic
    void RunIdleFrame()
    {
        SetT(0);
        _sound->handleFrameStart();
        SetT(PENTAGON_FRAME / 2);
        _sound->handleStep();
        SetT(PENTAGON_FRAME);
        _sound->handleStep();
        _sound->handleFrameEnd();
    }

    /// One emulated frame, same shape as SoundMuteContract_Test::RunDrivenFrame.
    void RunDrivenFrame()
    {
        SetT(0);
        _sound->handleFrameStart();
        TouchAyRegister();
        SetT(PENTAGON_FRAME / 2);
        _sound->handleStep();
        SetT(PENTAGON_FRAME);
        _sound->handleStep();
        _sound->handleFrameEnd();
    }
};

INSTANTIATE_TEST_SUITE_P(TurboSoundKinds, AudioActivityNotification_Test,
                         ::testing::Values(TurboSoundKind::AY, TurboSoundKind::FM),
                         [](const ::testing::TestParamInfo<TurboSoundKind>& info) {
                             return info.param == TurboSoundKind::AY ? "AY" : "FM";
                         });

TEST_P(AudioActivityNotification_Test, ActiveFrame_PostsExactlyOneNotificationPerFrame)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    // Only chip0 is driven (StartAyTone), never chip1/TurboSound-mode, so the
    // AY/TS notification always reports AudioSource::AY regardless of slot kind.
    AudioActivityCounter counter(_context->emulatorId, AudioSource::AY);
    Observer* obsPtr = &counter;
    ObserverCallbackMethod cb = static_cast<ObserverCallbackMethod>(&AudioActivityCounter::onEvent);
    mc.AddObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);

    StartAyTone();
    RunDrivenFrame();

    // First frame after activity starts always posts (state changed from
    // silent to active) - and must post EXACTLY once, not twice.
    EXPECT_TRUE(TestWait::ForExactly(counter.hits, 1))
        << "expected exactly 1 NC_AUDIO_ACTIVITY after the first active frame, got "
        << counter.hits.load()
        << " - SoundManager::handleFrameEnd() must call the TurboSound-slot "
           "device's handleFrameEnd() exactly once per frame";

    // A second, otherwise-identical frame (still active, no state change)
    // must post exactly once more (the "refresh HUD TTL while active"
    // branch) - not zero, not two.
    RunDrivenFrame();
    EXPECT_TRUE(TestWait::ForExactly(counter.hits, 2))
        << "expected exactly 2 NC_AUDIO_ACTIVITY after the second active frame, got "
        << counter.hits.load();

    mc.RemoveObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);
}

/// Per-source on/off counter for NC_AUDIO_ACTIVITY
struct AudioActivityOnOffCounter : public Observer
{
    unreal::UUID watchId;
    AudioSource watchSource;
    std::atomic<int> on{0};
    std::atomic<int> off{0};

    AudioActivityOnOffCounter(const unreal::UUID& id, AudioSource source) : watchId(id), watchSource(source) {}

    void onEvent(int /*id*/, Message* message)
    {
        auto* p = dynamic_cast<AudioActivityPayload*>(message->obj);
        if (!p || p->emulatorId != watchId || p->source != watchSource)
            return;
        (p->active ? on : off).fetch_add(1);
    }
};

/// LED = this frame's output, HUD = that output held for a second: a short
/// note keeps the HUD nudge up (no flicker) and ends it with exactly one
/// "inactive" once the hold runs out, while the LED has long gone dark
TEST_P(AudioActivityNotification_Test, ShortNote_LedFollowsFrame_HudHoldsOneSecond)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    AudioActivityOnOffCounter counter(_context->emulatorId, AudioSource::AY);
    Observer* obsPtr = &counter;
    ObserverCallbackMethod cb = static_cast<ObserverCallbackMethod>(&AudioActivityOnOffCounter::onEvent);
    mc.AddObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);

    StartAyTone();
    RunDrivenFrame();
    EXPECT_TRUE(Led());
    EXPECT_TRUE(HudHold());

    // Note off: the LED drops within a few frames (DC-blocker tail), the HUD
    // hold stays on well past that
    StopAyTone();
    int frames = 0;
    while (Led() && frames < 20)
    {
        RunIdleFrame();
        frames++;
    }
    EXPECT_FALSE(Led()) << "LED still lit " << frames << " frames after note off";
    EXPECT_TRUE(HudHold()) << "HUD hold ended with the note - the nudge would flicker";
    EXPECT_EQ(counter.off.load(), 0);

    // Hold runs out HOLD_FRAMES after the last frame with sound
    for (int i = 0; i < AudioActivityIndicators::HOLD_FRAMES; i++)
        RunIdleFrame();
    EXPECT_FALSE(HudHold());
    EXPECT_TRUE(TestWait::ForExactly(counter.off, 1)) << "expected exactly one inactive, got " << counter.off.load();

    // Silence stays silent: no more posts at all
    const int onBefore = counter.on.load();
    for (int i = 0; i < 10; i++)
        RunIdleFrame();
    EXPECT_TRUE(TestWait::ForExactly(counter.off, 1));
    EXPECT_EQ(counter.on.load(), onBefore);

    mc.RemoveObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);
}

/// Register traffic that leaves the chip silent is not activity: the AY I/O
/// port (R14/R15), the mixer with every volume at 0, volume 0 re-written
TEST_P(AudioActivityNotification_Test, SilentRegisterTraffic_IsNotActivity)
{
    for (int frame = 0; frame < 10; frame++)
    {
        SetT(0);
        _sound->handleFrameStart();
        WriteAy(7, 0x7F);   // mixer: port A output, generators off
        WriteAy(14, 0xFF);  // I/O port A (128K keypad / RS232)
        WriteAy(15, 0x00);
        WriteAy(8, 0x00);
        WriteAy(9, 0x00);
        WriteAy(10, 0x00);
        WriteAy(7, 0x38);   // tones on, still all volumes 0
        SetT(PENTAGON_FRAME);
        _sound->handleStep();
        _sound->handleFrameEnd();

        EXPECT_FALSE(Led()) << "frame " << frame;
        EXPECT_FALSE(HudHold()) << "frame " << frame;
    }
}

/// The reported case: a 128K machine idling in its ROM writes the AY (start-up
/// init, keypad polling through the AY I/O port) but plays nothing - neither
/// the LED nor the HUD nudge may light
class AudioActivityRom128_Test : public ::testing::TestWithParam<TurboSoundKind>
{
};

INSTANTIATE_TEST_SUITE_P(TurboSoundKinds, AudioActivityRom128_Test,
                         ::testing::Values(TurboSoundKind::AY, TurboSoundKind::FM),
                         [](const ::testing::TestParamInfo<TurboSoundKind>& info) {
                             return info.param == TurboSoundKind::AY ? "AY" : "FM";
                         });

TEST_P(AudioActivityRom128_Test, IdleRom_WritesAy_ButNoActivity)
{
    Emulator* emulator = EmulatorTestHelper::CreateEmulatorWithTurboSoundKind("PENTAGON", GetParam());
    ASSERT_NE(emulator, nullptr);
    SoundManager* sm = emulator->GetContext()->pSoundManager;
    ASSERT_NE(sm, nullptr);
    ITurboSoundDevice* ts = sm->getTurboSound();
    ASSERT_NE(ts, nullptr);

    int soundFrames = 0;
    for (int frame = 0; frame < 250; frame++)
    {
        emulator->RunFrame(true);
        if (sm->device(AudioSourceType::AY1_All)->activeRecently || sm->device(AudioSourceType::AY2_All)->activeRecently ||
            sm->getActivityIndicators().held(AudioSourceType::AY1_All) || sm->getActivityIndicators().held(AudioSourceType::AY2_All))
            soundFrames++;
    }

    EXPECT_TRUE(ts->getChip(0)->hasBeenWritten()) << "the ROM never touched the AY - test proves nothing";
    EXPECT_EQ(soundFrames, 0) << "idle ROM reported AY activity";

    // Control: the same pipeline does report a real tone
    auto write = [&](uint8_t reg, uint8_t value) {
        ts->portDeviceOutMethod(PORT_FFFD, reg);
        ts->portDeviceOutMethod(PORT_BFFD, value);
    };
    write(0, 0x20);
    write(1, 0x00);
    write(7, 0x3E);
    write(8, 0x0F);
    emulator->RunFrame(true);
    emulator->RunFrame(true);
    EXPECT_TRUE(sm->device(AudioSourceType::AY1_All)->activeRecently) << "a real tone was not detected";
    EXPECT_TRUE(sm->getActivityIndicators().held(AudioSourceType::AY1_All));

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// Same contract for a non-AY source: one beeper edge lights the LED for the
/// frames its output carries sound, the HUD nudge is held a second past that
/// and then ended with exactly one "inactive"
TEST_P(AudioActivityNotification_Test, BeeperEdge_LedAndHudFromOneMeasurement)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    AudioActivityOnOffCounter counter(_context->emulatorId, AudioSource::Beeper);
    Observer* obsPtr = &counter;
    ObserverCallbackMethod cb = static_cast<ObserverCallbackMethod>(&AudioActivityOnOffCounter::onEvent);
    mc.AddObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);

    auto beeperLed = [&] { return _sound->device(AudioSourceType::Beeper)->activeRecently; };
    auto beeperHeld = [&] { return _sound->getActivityIndicators().held(AudioSourceType::Beeper); };

    SetT(0);
    _sound->handleFrameStart();
    _sound->getBeeper().handlePortOut(0x10, PENTAGON_FRAME / 2);  // EAR high: one edge, then held
    SetT(PENTAGON_FRAME);
    _sound->handleStep();
    _sound->handleFrameEnd();
    EXPECT_TRUE(beeperLed());
    EXPECT_TRUE(beeperHeld());

    // A level held high is not sound: the LED goes dark once the edge has passed
    int frames = 0;
    while (beeperLed() && frames < 20)
    {
        RunIdleFrame();
        frames++;
    }
    EXPECT_FALSE(beeperLed()) << "beeper held high keeps the LED lit";
    EXPECT_TRUE(beeperHeld());

    for (int i = 0; i < AudioActivityIndicators::HOLD_FRAMES; i++)
        RunIdleFrame();
    EXPECT_FALSE(beeperHeld());
    EXPECT_TRUE(TestWait::ForExactly(counter.off, 1)) << "expected exactly one inactive, got " << counter.off.load();

    mc.RemoveObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);
}

/// Pause: no more frames will come, so every LED and nudge goes dark at once
TEST_P(AudioActivityNotification_Test, Pause_EndsLedsAndNudgesAtOnce)
{
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    AudioActivityOnOffCounter counter(_context->emulatorId, AudioSource::AY);
    Observer* obsPtr = &counter;
    ObserverCallbackMethod cb = static_cast<ObserverCallbackMethod>(&AudioActivityOnOffCounter::onEvent);
    mc.AddObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);

    StartAyTone();
    RunDrivenFrame();
    ASSERT_TRUE(Led());
    ASSERT_TRUE(HudHold());

    _sound->onEmulatorPaused();
    EXPECT_FALSE(Led());
    EXPECT_FALSE(HudHold());
    EXPECT_TRUE(TestWait::ForExactly(counter.off, 1)) << "pause must end the AY nudge, got " << counter.off.load();

    mc.RemoveObserver(NC_AUDIO_ACTIVITY, obsPtr, cb);
}
