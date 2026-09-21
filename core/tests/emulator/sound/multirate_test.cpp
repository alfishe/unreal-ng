#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#ifdef ENABLE_RECORDING
#include "encoder_base.h"
#include "recordingmanager.h"
#endif  // ENABLE_RECORDING

/// Multi-rate core tests (multirate plan phase 7).
///
/// The core audio rate is ONE pure function of three inputs with a fixed
/// priority (SoundManager::targetCoreRate): runtime pin > attached device
/// > [SOUND] CoreRate > 44100. The ini value therefore never locks a UI
/// client that has a device attached (a stale CoreRate=44100 next to a 48
/// kHz DAC resolves to 48 kHz) - it only decides the rate while no device
/// is known, which is the headless case (recordings/analyzers at a chosen
/// rate). These tests verify the rate matrix end-to-end: resolution rules,
/// per-frame sample counts, exactness of the integer accumulator at every
/// rate, and pitch invariance (the same emulated tone must measure the same
/// Hz at every core rate - the acceptance criterion from the evaluation
/// doc).

namespace
{
const std::vector<size_t> CORE_RATES = {44100, 48000, 88200, 96000, 176400, 192000};

constexpr uint32_t PENTAGON_FRAME = 71680;

struct CallbackCapture
{
    size_t lastNumSamples = 0;
    size_t callCount = 0;

    static void callback(void* obj, int16_t* samples, size_t numSamples)
    {
        (void)samples;
        auto* self = static_cast<CallbackCapture*>(obj);
        self->lastNumSamples = numSamples;
        self->callCount++;
    }
};
}  // namespace

class Multirate_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
            _context->config.sound.coreRate = 0;
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// region <Core rate resolution rules>

TEST_F(Multirate_Test, CoreRateResolution)
{
    // Table-driven pin x device x ini -> expected rate for the construction
    // resolution (the pin is a SoundManager field, so only the device/ini
    // columns apply at construction; the pin's live behaviour is covered by
    // CoreRatePin_OverridesDeviceAndIni below).
    struct RateRow
    {
        uint32_t deviceCell;      // per-emulator pAudioDeviceSampleRate
        uint32_t defaultPublished;  // process-wide frontend publication
        unsigned ini;             // [SOUND] CoreRate
        uint32_t expected;
        const char* what;
    };
    const RateRow rows[] = {
        // Headless: the ini is the rate source (recordings at a chosen rate)
        {0, 0, 96000, 96000, "ini pins the rate while no device is attached"},
        // The UI-client case a stale ini must NOT be able to lock
        {48000, 0, 44100, 48000, "a connected device beats the ini value"},
        // auto with a supported published default (frontend audio init runs
        // BEFORE emulator creation - the per-context cell is 0 at that point)
        {0, 48000, 0, 48000, "auto matches the device rate published before creation"},
        // Per-context cell outranks the process-wide default
        {96000, 48000, 44100, 96000, "per-context cell beats the published default"},
        // Unsupported inputs fall through the chain
        {22050, 0, 44100, 44100, "unsupported device falls back to the ini"},
        {0, 22050, 96000, 96000, "unsupported default falls back to the ini"},
        {0, 0, 0, 44100, "nothing known - conservative 44100"},
    };

    for (const RateRow& row : rows)
    {
        SCOPED_TRACE(row.what);
        _context->config.sound.coreRate = row.ini;
        _context->pAudioDeviceSampleRate.store(row.deviceCell, std::memory_order_release);
        SoundManager::PublishDefaultDeviceSampleRate(row.defaultPublished);

        SoundManager sound(_context);
        EXPECT_EQ(sound.getCoreRate(), row.expected);
        EXPECT_EQ(sound.getTargetCoreRate(), row.expected) << "target must equal the resolved rate";
    }

    _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
    _context->config.sound.coreRate = 0;
    SoundManager::PublishDefaultDeviceSampleRate(0);  // Reset for other tests
}

/// endregion </Core rate resolution rules>

/// region <Exact sample accumulator at every rate>

TEST_F(Multirate_Test, ExactSampleCountOverPeriodAtEveryRate)
{
    // The integer accumulator must deliver EXACTLY N*frame*rate/CPU_CLOCK
    // samples over N frames at every core rate - drift-free by construction.
    // (At 48k-family rates Pentagon's accumulator period is 25 frames vs 125.)
    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);
    _context->config.frame = PENTAGON_FRAME;

    for (size_t rate : CORE_RATES)
    {
        _context->config.sound.coreRate = static_cast<unsigned>(rate);
        SoundManager sound(_context);
        ASSERT_EQ(sound.getCoreRate(), rate);

        constexpr uint32_t FRAMES = 125;
        uint64_t total = 0;
        for (uint32_t f = 0; f < FRAMES; f++)
        {
            sound.handleFrameStart();
            sound.handleFrameEnd();
            total += capture.lastNumSamples / AUDIO_CHANNELS;
        }

        const uint64_t expected =
            (static_cast<uint64_t>(FRAMES) * PENTAGON_FRAME * rate) / CPU_CLOCK_RATE;
        EXPECT_EQ(total, expected)
            << rate << " Hz: " << FRAMES << " Pentagon frames must deliver exactly "
            << expected << " samples";
    }
}

/// endregion </Exact sample accumulator>

/// region <AY renders the correct sample count at every rate>

TEST_F(Multirate_Test, TurboSound_SampleCountFollowsCoreRate)
{
    Z80* z80 = _context->pCore->GetZ80();
    _context->config.frame = PENTAGON_FRAME;

    for (size_t rate : CORE_RATES)
    {
        _context->config.sound.coreRate = static_cast<unsigned>(rate);
        SoundManager sound(_context);
        ITurboSoundDevice* turboSound = sound.getTurboSound();
        ASSERT_NE(turboSound, nullptr);
        ASSERT_EQ(turboSound->getCoreRate(), rate);

        uint64_t total = 0;
        constexpr int FRAMES = 25;
        for (int f = 0; f < FRAMES; f++)
        {
            turboSound->handleFrameStart();
            z80->t = PENTAGON_FRAME;
            turboSound->handleStep();
            total += turboSound->getRenderedSamplesThisFrame();
        }
        z80->t = 0;

        const double expected = FRAMES * PENTAGON_FRAME * static_cast<double>(rate) / CPU_CLOCK_RATE;
        EXPECT_NEAR(static_cast<double>(total), expected, 2.0)
            << rate << " Hz: AY PLL must render the frame's worth of samples";
    }
}

/// endregion </AY sample count>

/// region <Pitch invariance (acceptance criterion)>

TEST_F(Multirate_Test, Beeper_PitchInvariantAcrossRates)
{
    // A square wave toggled every 512 T-states (~3418 Hz) must measure the
    // same frequency at every core rate. Zero-crossing count over a known
    // duration is the estimator: crossings / (2 * seconds) = Hz.
    // 512 divides the 71680T frame exactly, so the per-frame T-state restart
    // does not perturb the tone phase.
    constexpr uint32_t TOGGLE_PERIOD_T = 512;   // Half-period: 3417.97 Hz tone
    constexpr int FRAMES = 50;                  // ~1.024 s of Pentagon time
    const double emulatedSeconds =
        static_cast<double>(FRAMES) * PENTAGON_FRAME / static_cast<double>(CPU_CLOCK_RATE);

    _context->config.frame = PENTAGON_FRAME;

    for (size_t rate : CORE_RATES)
    {
        const size_t bufferPairs = MAX_SAMPLES_PER_FRAME + 64;
        std::vector<int16_t> buffer(bufferPairs * AUDIO_CHANNELS);
        Beeper beeper(_context, CPU_CLOCK_RATE, rate, buffer.data());

        uint64_t crossings = 0;
        int16_t prev = 0;
        int32_t level = 8000;

        for (int f = 0; f < FRAMES; f++)
        {
            beeper.handleFrameStart();
            for (uint32_t t = 0; t < PENTAGON_FRAME; t += TOGGLE_PERIOD_T)
            {
                beeper.handleTapeAudio(level, t);
                level = -level;
            }
            beeper.handleFrameEnd(PENTAGON_FRAME);

            const int samples = beeper.getLastSamplesRead();
            for (int i = 0; i < samples; i++)
            {
                const int16_t s = buffer[i * 2];  // Left channel
                if ((prev < 0 && s >= 0) || (prev >= 0 && s < 0))
                    crossings++;
                prev = s;
            }
        }

        const double measuredHz = static_cast<double>(crossings) / (2.0 * emulatedSeconds);
        const double expectedHz = static_cast<double>(CPU_CLOCK_RATE) / (2.0 * TOGGLE_PERIOD_T);

        EXPECT_NEAR(measuredHz, expectedHz, expectedHz * 0.005)
            << rate << " Hz core: beeper tone pitch must be invariant to the core rate";
    }
}

TEST_F(Multirate_Test, AY_PitchInvariantAcrossRates)
{
    // Program AY channel A to a 1 kHz-ish tone and verify the rendered pitch
    // is identical at every core rate. AY tone period N -> f = 1.75MHz/(16*N);
    // N=109 -> 1003.44 Hz.
    Z80* z80 = _context->pCore->GetZ80();
    _context->config.frame = PENTAGON_FRAME;

    constexpr uint8_t TONE_PERIOD = 109;
    const double expectedHz = static_cast<double>(PSG_CLOCK_RATE) / (16.0 * TONE_PERIOD);

    for (size_t rate : CORE_RATES)
    {
        _context->config.sound.coreRate = static_cast<unsigned>(rate);
        SoundManager sound(_context);
        ITurboSoundDevice* turboSound = sound.getTurboSound();
        ASSERT_NE(turboSound, nullptr);

        // Program chip 0: channel A tone, full fixed volume
        auto poke = [&](uint8_t reg, uint8_t value) {
            turboSound->portDeviceOutMethod(0xFFFD, reg);
            turboSound->portDeviceOutMethod(0xBFFD, value);
        };
        poke(0, TONE_PERIOD);  // Tone A fine
        poke(1, 0);            // Tone A coarse
        poke(7, 0b00111110);   // Mixer: tone A on, noise off
        poke(8, 15);           // Volume A: max, no envelope

        // Capture the rendered stream, then measure around its actual midline
        // with hysteresis (robust against DC filtering and volume scaling).
        // 25 frames (~0.51 s): ~1027 midline crossings, so the estimator noise
        // stays near 0.1% - ten times inside the 1% tolerance below
        std::vector<int16_t> stream;
        constexpr int FRAMES = 25;
        stream.reserve(FRAMES * (PENTAGON_FRAME / 16));

        for (int f = 0; f < FRAMES; f++)
        {
            turboSound->handleFrameStart();
            z80->t = PENTAGON_FRAME;
            turboSound->handleStep();

            const size_t samples = turboSound->getRenderedSamplesThisFrame();
            const int16_t* buf = turboSound->getChipBuffer(0);
            for (size_t i = 0; i < samples; i++)
                stream.push_back(buf[i * 2]);  // Left channel
        }
        z80->t = 0;

        const auto [minIt, maxIt] = std::minmax_element(stream.begin(), stream.end());
        const int32_t lo = *minIt, hi = *maxIt;
        ASSERT_GT(hi - lo, 1000) << rate << " Hz core: AY output is flat - tone not rendered";

        const int32_t mid = (lo + hi) / 2;
        const int32_t hyst = (hi - lo) / 8;
        uint64_t crossings = 0;
        int state = 0;  // -1 below, +1 above, 0 unknown
        for (int16_t s : stream)
        {
            if (s > mid + hyst && state <= 0)
            {
                if (state < 0)
                    crossings++;  // One midline crossing per polarity flip
                state = 1;
            }
            else if (s < mid - hyst && state >= 0)
            {
                if (state > 0)
                    crossings++;
                state = -1;
            }
        }

        const double seconds = static_cast<double>(stream.size()) / static_cast<double>(rate);
        const double measuredHz = static_cast<double>(crossings) / (2.0 * seconds);

        EXPECT_NEAR(measuredHz, expectedHz, expectedHz * 0.01)
            << rate << " Hz core: AY tone pitch must be invariant to the core rate";
    }
}

/// region <Live core-rate change (device reroute with CoreRate=auto)>

TEST_F(Multirate_Test, LiveCoreRateChange_RederivesPipeline)
{
    // Device reroute at a different native rate with CoreRate=auto: the whole
    // pipeline must re-derive at the next frame boundary - blip resamplers,
    // AY PLL/decimators, character chains, sample accumulator.
    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);
    _context->config.frame = PENTAGON_FRAME;
    _context->config.sound.coreRate = 0;

    SoundManager sound(_context);
    ASSERT_EQ(sound.getCoreRate(), 44100u);

    sound.handleFrameStart();
    sound.handleFrameEnd();
    EXPECT_NEAR(capture.lastNumSamples / 2.0, 903.0, 2.0) << "Pentagon frame at 44.1k";

    // Reroute: applied at the NEXT frame boundary, on the emulation thread
    sound.requestCoreRate(48000);
    sound.handleFrameStart();
    EXPECT_EQ(sound.getCoreRate(), 48000u);
    EXPECT_EQ(sound.getTurboSound()->getCoreRate(), 48000u) << "AY chain must re-derive";
    sound.handleFrameEnd();
    EXPECT_NEAR(capture.lastNumSamples / 2.0, 983.0, 2.0) << "Pentagon frame at 48k";

    // Unsupported rates are ignored
    sound.requestCoreRate(22050);
    sound.handleFrameStart();
    sound.handleFrameEnd();
    EXPECT_EQ(sound.getCoreRate(), 48000u);

    // Equal rate is a no-op (no state reset churn)
    sound.requestCoreRate(48000);
    sound.handleFrameStart();
    EXPECT_EQ(sound.getCoreRate(), 48000u);
}

TEST_F(Multirate_Test, DeviceRateChange_TriggersAutoCoreRerate)
{
    // Frontend reroute path end-to-end: SetAudioDeviceSampleRate on an
    // auto-rate emulator must request the pipeline re-rate (applied at the
    // next frame boundary by the emulator's own SoundManager).
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    ASSERT_EQ(_context->config.sound.coreRate, 0u) << "Test requires CoreRate=auto";
    ASSERT_EQ(sound->getCoreRate(), 44100u);

    _emulator->SetAudioDeviceSampleRate(48000);
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 48000u)
        << "Auto core rate must follow the re-established device rate";

    // An explicitly configured ini rate must NOT shield the core from the
    // device either: a stale CoreRate in a UI client's ini can never lock
    // the rate while a device is attached (device > ini in the chain)
    _context->config.sound.coreRate = 44100;
    _emulator->SetAudioDeviceSampleRate(96000);
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 96000u) << "The attached device must beat the ini value";
    _context->config.sound.coreRate = 0;
    _emulator->SetAudioDeviceSampleRate(0);
}

TEST_F(Multirate_Test, CoreRatePin_OverridesDeviceAndIni)
{
    // The runtime pin ('setting audio_rate' / set_audio_rate) is the only
    // way to hold a rate against a connected device - explicit intent for
    // this run, never persisted to any ini
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    _emulator->SetAudioDeviceSampleRate(48000);
    sound->handleFrameStart();
    ASSERT_EQ(sound->getCoreRate(), 48000u);

    // Pinning: applied at the next frame boundary, never mid-frame
    sound->setCoreRatePin(96000);
    EXPECT_EQ(sound->getCoreRate(), 48000u) << "The pin must not jump mid-frame";
    EXPECT_EQ(sound->getTargetCoreRate(), 96000u) << "The target follows the pin immediately";
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 96000u);
    EXPECT_EQ(sound->getCoreRatePin(), 96000u);

    // Device change under a pin: the core rate holds; only the DRC base
    // ratio (device/core) follows the device
    _emulator->SetAudioDeviceSampleRate(44100);
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 96000u) << "The pin must hold against device changes";

    // Unsupported pin rates are refused and leave the pin untouched
    sound->setCoreRatePin(22050);
    EXPECT_EQ(sound->getCoreRatePin(), 96000u) << "Unsupported rates must not change the pin";
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 96000u);

    // Releasing the pin returns control to the device input
    sound->setCoreRatePin(0);
    EXPECT_EQ(sound->getTargetCoreRate(), 44100u) << "auto must re-target the device rate";
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 44100u);

    _emulator->SetAudioDeviceSampleRate(0);
}

#ifdef ENABLE_RECORDING
namespace
{
/// Minimal always-succeeding encoder: puts the RecordingManager into the
/// recording state without touching files, so the rate deferral path can be
/// exercised directly
class StubEncoder : public EncoderBase
{
public:
    bool Start(const std::string& filename, const EncoderConfig& config) override
    {
        (void)filename;
        (void)config;
        return true;
    }
    void Stop() override {}
    bool IsRecording() const override { return true; }
    std::string GetType() const override { return "stub"; }
    std::string GetDisplayName() const override { return "Stub"; }
    bool SupportsVideo() const override { return false; }
    bool SupportsAudio() const override { return true; }
};
}  // namespace

TEST_F(Multirate_Test, CoreRateChange_DeferredWhileRecording)
{
    // A recording must keep one rate end to end: rate requests (device
    // change or pin) stay pending until the recording stops, then apply at
    // the next frame boundary
    RecordingManager* rm = _context->pRecordingManager;
    ASSERT_NE(rm, nullptr) << "Fixture must create the recording manager";
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    ASSERT_EQ(sound->getCoreRate(), 44100u);

    const std::string path = TestPathHelper::GetUniqueTestScratchPath("multirate_deferred.wav");
    ASSERT_TRUE(rm->StartRecordingWithEncoder(path, std::make_unique<StubEncoder>()));

    sound->setCoreRatePin(48000);
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 44100u) << "A recording must keep one rate end to end";
    EXPECT_EQ(sound->getTargetCoreRate(), 48000u) << "...but the target already tracks the pin";

    rm->StopRecording();
    sound->handleFrameStart();
    EXPECT_EQ(sound->getCoreRate(), 48000u) << "The deferred rate applies once the recording stops";

    sound->setCoreRatePin(0);  // Leave the fixture unpinned
    sound->handleFrameStart();
}
#endif  // ENABLE_RECORDING

/// endregion </Live core-rate change>

/// region <Audio device descriptor (realtime monitoring)>

TEST_F(Multirate_Test, AudioDeviceDescriptor_RegisteredAndObservable)
{
    AudioDeviceDescriptor desc;
    desc.sampleRate.store(48000, std::memory_order_relaxed);
    desc.channels.store(2, std::memory_order_relaxed);
    desc.capacityFrames.store(16384, std::memory_order_relaxed);
    desc.setDeviceName("Test Output");
    desc.occupancyFrames.store(3360, std::memory_order_relaxed);  // 70 ms @ 48k

    EXPECT_NEAR(desc.occupancyMs(), 70.0, 0.01);

    // Registration lifecycle mirrors the frontend bind/unbind flow
    _emulator->SetAudioCallback(nullptr, nullptr, &desc.occupancyFrames, &desc);
    ASSERT_EQ(_emulator->GetAudioDeviceDescriptor(), &desc);
    EXPECT_EQ(_emulator->GetAudioDeviceDescriptor()->sampleRate.load(), 48000u);
    EXPECT_STREQ(_emulator->GetAudioDeviceDescriptor()->deviceName, "Test Output");

    _emulator->ClearAudioCallback();
    EXPECT_EQ(_emulator->GetAudioDeviceDescriptor(), nullptr);

    // No device attached -> occupancyMs reports 0, not a division blowup
    AudioDeviceDescriptor detached;
    EXPECT_EQ(detached.occupancyMs(), 0.0);
}

/// endregion </Audio device descriptor>

/// endregion </Pitch invariance>
