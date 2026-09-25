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

/// @file audio_activity_notification_test.cpp
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

    /// Program a loud steady tone on AY channel A. Register CONFIG (period,
    /// mixer, volume) persists across frames like real hardware; the
    /// per-frame `_frameHadActivity` bookkeeping flag does not - it's reset
    /// by handleFrameStart() every frame (soundchip_turbosound(fm).cpp) and
    /// only set again by an actual register write THIS frame. A real tune
    /// re-writes registers every frame; this test's RunDrivenFrame() mirrors
    /// that with TouchAyRegister() so the flag - and hence the notification
    /// this test is about - is genuinely exercised every frame, not just once.
    void StartAyTone()
    {
        WriteAy(0, 0x20);   // Tone A period low
        WriteAy(1, 0x00);   // Tone A period high
        WriteAy(7, 0x3E);   // Mixer: tone A on, everything else off
        WriteAy(8, 0x0F);   // Channel A volume: max, no envelope
    }

    /// Idempotent re-write of the channel A volume register - marks THIS
    /// frame as having activity, same as a real tune's per-frame register
    /// refresh, without changing anything audible.
    void TouchAyRegister() { WriteAy(8, 0x0F); }

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
