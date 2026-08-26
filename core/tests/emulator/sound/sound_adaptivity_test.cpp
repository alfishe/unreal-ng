#include "stdafx.h"
#include "pch.h"

#include <cmath>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/beeper.h"
#include "emulator/sound/covox.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "common/timehelper.h"
#include "emulator/sound/soundmanager.h"

/// Sound generation adaptivity tests.
///
/// Every sound generator and DSP stage must derive its per-frame sample count
/// from the machine's actual frame length (config.frame t-states @ 3.5 MHz),
/// never from the 50 Hz SAMPLES_PER_FRAME constant (882). ZX and clone frame
/// lengths differ: ZX48/128/Scorpion 69888T (50.08 Hz, 881 samples),
/// Spectrum +2A/+3 70908T (49.36 Hz, 893 samples), Pentagon 71680T
/// (48.83 Hz, 903 samples) - and hypothetical clones vary further.
/// A hardcoded count leaves an unprocessed/stale tail every frame, audible as
/// frame-rate harmonics and hiss on pure tones.

namespace
{
// Machine frame lengths in t-states @ 3.5 MHz base clock
struct MachineTiming
{
    const char* name;
    uint32_t frameTStates;
    uint32_t frameDurationUs;  // frame_duration_us for accurate audio timing
};

const std::vector<MachineTiming> MACHINE_TIMINGS = {
    {"ZX48/ZX128/Scorpion", 69888, 19968},   // 50.08 Hz -> 881 samples
    {"Spectrum +2A/+3", 70908, 20260},       // 49.36 Hz -> 893 samples
    {"Pentagon", 71680, 20480},              // 48.83 Hz -> 903 samples
    {"Hypothetical 60Hz clone", 58333, 16667},  // ~60 Hz -> 735 samples
    {"Hypothetical slow clone", 80000, 22857},  // 43.75 Hz -> 1008 samples
};

// Expected sample count from frame_duration_us (used by SoundManager)
size_t expectedSamplesFromUs(uint32_t frameDurationUs)
{
    return static_cast<size_t>(
        std::lround(static_cast<double>(frameDurationUs) * AUDIO_SAMPLING_RATE / 1'000'000.0));
}

// Expected sample count from T-states (used by blip_buf-based components: Beeper, Covox, AY)
size_t expectedSamplesFromTstates(uint32_t frameTStates)
{
    return static_cast<size_t>(
        std::lround(frameTStates * (double)AUDIO_SAMPLING_RATE / (double)CPU_CLOCK_RATE));
}

// Count leading stereo pairs that were written over a sentinel fill
size_t countWrittenPairs(const int16_t* buffer, size_t maxPairs, int16_t sentinel)
{
    size_t pairs = 0;
    while (pairs < maxPairs && !(buffer[pairs * 2] == sentinel && buffer[pairs * 2 + 1] == sentinel))
    {
        pairs++;
    }
    return pairs;
}

constexpr int16_t SENTINEL = 0x7C7C;
}  // namespace

class SoundAdaptivity_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();
        ASSERT_EQ(_context->emulatorState.current_z80_frequency_multiplier, 1);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _context->pAudioCallback.store(nullptr, std::memory_order_release);
            _context->pAudioManagerObj.store(nullptr, std::memory_order_release);
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }
};

/// region <End-to-end: SoundManager output size follows the frame length>

namespace
{
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

TEST_F(SoundAdaptivity_Test, SoundManager_CallbackSizeFollowsFrameLength)
{
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    for (const auto& machine : MACHINE_TIMINGS)
    {
        _context->config.frame = machine.frameTStates;
        _context->config.frame_duration_us = machine.frameDurationUs;

        size_t before = capture.callCount;
        sound->handleFrameStart();
        sound->handleFrameEnd();

        ASSERT_GT(capture.callCount, before) << machine.name << ": audio callback was not invoked";
        // Per-frame counts alternate by +-1 around the exact rational value
        // (integer accumulator carries the fraction); cumulative exactness is
        // verified by SoundManager_ExactSampleCountOverAccumulatorPeriod
        EXPECT_NEAR(static_cast<double>(capture.lastNumSamples),
                    static_cast<double>(expectedSamplesFromUs(machine.frameDurationUs) * AUDIO_CHANNELS),
                    static_cast<double>(AUDIO_CHANNELS))
            << machine.name << " (" << machine.frameTStates
            << "T): mixed output size must derive from the frame length";
    }
}

/// endregion </End-to-end>

/// region <Beeper (blip_buf) adaptivity>

TEST_F(SoundAdaptivity_Test, Beeper_SampleCountFollowsFrameLength)
{
    constexpr size_t bufferPairs = MAX_SAMPLES_PER_FRAME + 64;
    std::vector<int16_t> buffer(bufferPairs * AUDIO_CHANNELS);

    Beeper beeper(_context, CPU_CLOCK_RATE, AUDIO_SAMPLING_RATE, buffer.data());

    for (const auto& machine : MACHINE_TIMINGS)
    {
        std::fill(buffer.begin(), buffer.end(), SENTINEL);

        beeper.handleFrameStart();
        beeper.handleFrameEnd(machine.frameTStates);

        size_t written = countWrittenPairs(buffer.data(), bufferPairs, SENTINEL);
        size_t expected = expectedSamplesFromTstates(machine.frameTStates);

        // blip_buf's internal resampling may land one sample either side of
        // the ideal count per frame; anything larger is a hardcoded-rate bug
        EXPECT_NEAR(static_cast<double>(written), static_cast<double>(expected), 1.0)
            << machine.name << " (" << machine.frameTStates << "T)";
    }
}

TEST_F(SoundAdaptivity_Test, Beeper_TurboSwitchFrame_StaysInLockstepWithAccumulator)
{
    // Turbo switches queue the new multiplier (PortDecoder_ATM710::
    // updateTurboMode / Core::SetSpeedMultiplier write next_z80_frequency_
    // multiplier); Z80::Z80FrameCycle applies the queue AFTER SoundManager::
    // handleFrameStart in the MainLoop frame pass. The synth re-clock must
    // therefore follow the QUEUED value - reading current_... left the beeper
    // at the old clock for the whole switch frame while handleFrameEnd closed
    // the blip at the new duration, delivering multiplier-times realtime
    // samples (the "blip delivered 1761 samples, accumulator expects 880"
    // warning at x2 turbo)
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    EmulatorState& state = _context->emulatorState;
    _context->config.frame = 69888;             // ZX timing
    _context->config.frame_duration_us = 19968;
    const size_t expected = expectedSamplesFromUs(_context->config.frame_duration_us);

    // Settle at x1 (also applies the forced initial re-clock from reset())
    state.next_z80_frequency_multiplier = 1;
    sound->handleFrameStart();
    state.current_z80_frequency_multiplier = 1;  // queue applied by Z80FrameCycle
    sound->handleFrameEnd();

    // Queue x2 exactly like the ATM710 turbo port write does, then replay
    // the MainLoop ordering of the switch frame
    state.next_z80_frequency_multiplier = 2;
    sound->handleFrameStart();
    state.current_z80_frequency_multiplier = 2;  // Z80::Z80FrameCycle applies here
    sound->handleFrameEnd();

    // The switch frame itself must deliver realtime samples, not 2x
    EXPECT_NEAR(static_cast<double>(sound->getBeeper().getLastSamplesRead()),
                static_cast<double>(expected), 1.0)
        << "turbo switch frame: beeper must already run at the queued clock";

    // ...and stay in lockstep on the following frame
    sound->handleFrameStart();
    sound->handleFrameEnd();
    EXPECT_NEAR(static_cast<double>(sound->getBeeper().getLastSamplesRead()),
                static_cast<double>(expected), 1.0)
        << "frame after the turbo switch";
}

/// endregion </Beeper>

/// region <Covox (blip_buf) adaptivity>

TEST_F(SoundAdaptivity_Test, Covox_SampleCountFollowsFrameLength)
{
    Covox covox(_context);
    covox.reset();

    for (const auto& machine : MACHINE_TIMINGS)
    {
        _context->config.frame = machine.frameTStates;

        covox.handleFrameStart();

        // Sentinel-fill AFTER frame start (which may clear the buffer)
        int16_t* buffer = covox.getBuffer();
        constexpr size_t bufferPairs = MAX_SAMPLES_PER_FRAME;
        std::fill(buffer, buffer + bufferPairs * AUDIO_CHANNELS, SENTINEL);

        covox.handleFrameEnd();

        size_t written = countWrittenPairs(buffer, bufferPairs, SENTINEL);
        size_t expected = expectedSamplesFromTstates(machine.frameTStates);

        EXPECT_NEAR(static_cast<double>(written), static_cast<double>(expected), 1.0)
            << machine.name << " (" << machine.frameTStates << "T)";
    }
}

/// endregion </Covox>

/// region <TurboSound / AY adaptivity>

TEST_F(SoundAdaptivity_Test, TurboSound_SampleCountFollowsFrameLength)
{
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    SoundChip_TurboSound* turboSound = sound->getTurboSound();
    if (!turboSound)
    {
        GTEST_SKIP() << "TurboSound not available on this model";
    }

    Z80* z80 = _context->pCore->GetZ80();

    for (const auto& machine : MACHINE_TIMINGS)
    {
        _context->config.frame = machine.frameTStates;

        turboSound->handleFrameStart();
        z80->t = machine.frameTStates;  // Simulate reaching frame end
        turboSound->handleStep();

        size_t rendered = turboSound->getRenderedSamplesThisFrame();
        size_t expected = expectedSamplesFromTstates(machine.frameTStates);

        // PLL accumulation may land one sample short of the rounded ideal
        EXPECT_NEAR(static_cast<double>(rendered), static_cast<double>(expected), 1.0)
            << machine.name << " (" << machine.frameTStates << "T)";
    }

    z80->t = 0;
}

/// endregion </TurboSound>

/// region <Exact sample accumulator (audio-sync Fix 1)>

TEST_F(SoundAdaptivity_Test, SoundManager_ExactSampleCountOverAccumulatorPeriod)
{
    // The integer accumulator makes total samples over N frames EXACTLY
    // floor(N * frame * SR / CPU_CLOCK) - drift-free by construction.
    // The old round() emitted a systematic bias (-0.019% Pentagon,
    // +0.047% ZX48) responsible for realtime ring drift and
    // audio-behind-video drift in recordings.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    struct Case
    {
        const char* name;
        uint32_t frame;
        uint32_t frameDurationUs;
        uint32_t frames;  // Full accumulator period
    };
    const Case cases[] = {
        {"Pentagon", 71680, 20480, 125},   // 125 * 903.168 -> exact over period
        {"ZX48/128", 69888, 19968, 625},   // 625 * 880.5888 -> exact over period
    };

    for (const auto& c : cases)
    {
        _context->config.frame = c.frame;
        _context->config.frame_duration_us = c.frameDurationUs;
        sound->reset();  // Restart the accumulator for a clean period

        uint64_t totalStereoSamples = 0;
        for (uint32_t f = 0; f < c.frames; f++)
        {
            sound->handleFrameStart();
            sound->handleFrameEnd();
            totalStereoSamples += capture.lastNumSamples / AUDIO_CHANNELS;
        }

        // Expected: frames * frame_duration_us * AUDIO_SAMPLING_RATE / 1_000_000
        uint64_t expected =
            (static_cast<uint64_t>(c.frames) * c.frameDurationUs * AUDIO_SAMPLING_RATE) / 1'000'000ULL;
        EXPECT_EQ(totalStereoSamples, expected)
            << c.name << ": " << c.frames << " frames must deliver exactly " << expected
            << " samples (zero drift by construction)";
    }
}

TEST_F(SoundAdaptivity_Test, TurboSound_PLLContinuityAcrossFrames)
{
    // The AY phase accumulator must be free-running across frames: zeroing it
    // per frame locked the AY at 903 samples/frame (never 904), a systematic
    // -0.019% rate bias vs the exact accumulator.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    SoundChip_TurboSound* turboSound = sound->getTurboSound();
    if (!turboSound)
        GTEST_SKIP() << "TurboSound not available";

    Z80* z80 = _context->pCore->GetZ80();
    _context->config.frame = 71680;
    turboSound->reset();

    uint64_t total = 0;
    for (int f = 0; f < 125; f++)
    {
        turboSound->handleFrameStart();
        z80->t = 71680;
        turboSound->handleStep();
        total += turboSound->getRenderedSamplesThisFrame();
    }
    z80->t = 0;

    // 125 * 903.168 = 112896; allow FP phase accumulation slack of 2
    EXPECT_NEAR(static_cast<double>(total), 112896.0, 2.0)
        << "AY PLL must carry fractional phase across frames (903/904 pattern)";
}

/// endregion </Exact sample accumulator>

/// region <DRC rate controller (audio-sync Fix 2)>

TEST_F(SoundAdaptivity_Test, DRC_ConvergesToTargetOccupancy)
{
    // Closed loop: a simulated DAC consumes exactly real-time while the
    // emulator produces through the full pipeline (mix -> DRC resampler ->
    // callback). The PI controller must drive ring occupancy to the 70 ms
    // setpoint from both directions and hold it with a small trim.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    std::atomic<uint32_t> occCell{0};
    _context->pAudioRingOccupancy.store(&occCell, std::memory_order_release);

    _context->config.frame = 71680;
    const double consumePerFrame = 71680.0 * 44100.0 / 3500000.0;  // Real-time DAC

    auto runLoop = [&](double startFrames, int frames) -> double {
        sound->reset();
        double ring = startFrames;
        occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
        for (int f = 0; f < frames; f++)
        {
            sound->handleFrameStart();
            sound->handleFrameEnd();
            ring += static_cast<double>(capture.lastNumSamples) / AUDIO_CHANNELS;
            ring = std::max(0.0, ring - consumePerFrame);
            occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
        }
        return ring;
    };

    // From near-empty (emergency refill seeds ~46ms in the real mainloop;
    // start there) and from badly overfull
    for (double startMs : {46.0, 300.0})
    {
        double finalFrames = runLoop(startMs * 44100.0 / 1000.0, 6000);
        double finalMs = finalFrames * 1000.0 / 44100.0;

        EXPECT_NEAR(finalMs, SoundManager::DRC_TARGET_MS, 8.0)
            << "DRC must converge ring occupancy to the setpoint (start " << startMs << "ms)";
        EXPECT_LT(std::abs(sound->getDrcRatio() - 1.0), 0.001)
            << "Converged trim must be small (start " << startMs << "ms)";
    }

    _context->pAudioRingOccupancy.store(nullptr, std::memory_order_release);
}

TEST_F(SoundAdaptivity_Test, DRC_DisengagedWithoutOccupancyCell)
{
    // No audio device attached (headless/tests): DRC must stay at exact
    // unity bypass so output remains bit-identical and sample-exact
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);
    ASSERT_EQ(_context->pAudioRingOccupancy.load(), nullptr);

    sound->handleFrameStart();
    sound->handleFrameEnd();
    EXPECT_EQ(sound->getDrcRatio(), 1.0);
}

/// endregion </DRC rate controller>

TEST_F(SoundAdaptivity_Test, AVLatencyBudget)
{
    // REGRESSION GUARD for audible A/V desync. The DRC pins ring occupancy
    // at DRC_TARGET_MS - that occupancy IS the audio presentation delay
    // (video presents within ~1 frame; audio trails by ring + HW buffer).
    // Perception threshold for audio-late lip-sync is ~45 ms; the device HW
    // buffer adds ~11 ms (2 x 256 frames @ 44.1k). The target must therefore
    // stay at or below ~40 ms, with enough underrun margin (>= 5 device
    // callback periods of ~5.8 ms).
    //
    // If this test fails after changing DRC_TARGET_MS: you have either
    // reintroduced audible audio lag (too high) or removed the underrun
    // safety margin (too low). Confirm with the realtime "A/V" readout in
    // the audio settings window before adjusting the bounds.
    constexpr double HW_BUFFER_MS = 2.0 * 256.0 * 1000.0 / 44100.0;  // ~11.6 ms
    constexpr double PERCEPTION_THRESHOLD_MS = 45.0;
    constexpr double MIN_UNDERRUN_MARGIN_MS = 5.0 * 5.8;  // 5 callback periods

    EXPECT_LE(SoundManager::DRC_TARGET_MS + HW_BUFFER_MS, PERCEPTION_THRESHOLD_MS + 8.0)
        << "Audio presentation delay exceeds the lip-sync perception budget";
    EXPECT_GE(SoundManager::DRC_TARGET_MS, MIN_UNDERRUN_MARGIN_MS)
        << "Ring target too small - underrun risk under scheduler jitter";

    // The emergency-refill trigger must sit BELOW the occupancy sawtooth
    // trough (target - 1 frame): production is bursty, so occupancy dips
    // that far EVERY frame cycle. A threshold above the trough turns the
    // emergency path into a periodic frame injector that spikes occupancy
    // ~+1 frame and fights the DRC forever (shipped once: 70->40 ms target
    // change left the old ~46 ms threshold in place -> intermittent
    // +20..30 ms audio-late excursions).
    constexpr double PENTAGON_FRAME_MS = 71680.0 / 3500.0;  // 20.48 ms (longest frame)
    constexpr double SAWTOOTH_TROUGH_MS = SoundManager::DRC_TARGET_MS - PENTAGON_FRAME_MS;
    EXPECT_LT(SoundManager::EMERGENCY_REFILL_MS, SAWTOOTH_TROUGH_MS - 3.0)
        << "Refill threshold must clear the steady-state occupancy trough with margin";
    EXPECT_GT(SoundManager::EMERGENCY_REFILL_MS, 5.0)
        << "Refill threshold too low to catch genuine stalls before underrun";

    // Windows/WASAPI budget: shared-mode WASAPI ignores our 256-frame period
    // and pulls a 10 ms engine period at a time (miniaudio splits it into
    // back-to-back 256-frame callbacks), occasionally with a ~20 ms gap
    // followed by a double pull. The trough must therefore survive one
    // engine period PLUS the frame clock's wake-up lateness
    // (TimeHelper::FRAME_PACING_JITTER_BUDGET_MS). This is exactly what
    // broke with std::condition_variable::wait_until on MinGW (10-17 ms
    // late): 19.5 - 15 < 10 -> steady "ring errors ... dequeue=N" growth.
    constexpr double WASAPI_SHARED_ENGINE_PERIOD_MS = 10.0;
    EXPECT_GT(SAWTOOTH_TROUGH_MS - TimeHelper::FRAME_PACING_JITTER_BUDGET_MS, WASAPI_SHARED_ENGINE_PERIOD_MS)
        << "Sawtooth trough cannot absorb one WASAPI engine period after frame-clock jitter";

    // Hard-resync trigger: far enough above target that it can only be hit
    // through abnormal events (reroute windows, long stalls), yet low enough
    // that a resync restores the budget in ONE step instead of the DRC
    // grinding down an overfill for minutes at +-0.5%
    EXPECT_GE(SoundManager::HARD_RESYNC_MS, SoundManager::DRC_TARGET_MS * 3.0)
        << "Hard resync must not trigger on normal DRC transients";
    EXPECT_LE(SoundManager::HARD_RESYNC_MS, 250.0)
        << "Hard resync threshold high enough to let audible lag persist";
}

TEST_F(SoundAdaptivity_Test, EmergencyRefill_NeverFiresAtSteadyState)
{
    // Closed loop with the mainloop's refill rule simulated: at converged
    // steady state the instantaneous occupancy sawtooth must NEVER cross the
    // refill threshold - the emergency path is for cold start and stalls
    // only. Sampling right BEFORE each production burst hits the sawtooth
    // trough, the worst case.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    std::atomic<uint32_t> occCell{0};
    _context->pAudioRingOccupancy.store(&occCell, std::memory_order_release);

    _context->config.frame = 71680;
    const double devRate = 44100.0;
    const double consumePerFrame = 71680.0 * devRate / 3500000.0;
    const double refillThresholdFrames = devRate * SoundManager::EMERGENCY_REFILL_MS / 1000.0;

    sound->reset();
    double ring = SoundManager::DRC_TARGET_MS * devRate / 1000.0;
    occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);

    int refillTriggers = 0;
    for (int f = 0; f < 3000; f++)
    {
        // DAC drains first: the trough is right before the production burst
        ring = std::max(0.0, ring - consumePerFrame);
        occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);

        if (f > 500 && ring < refillThresholdFrames)
            refillTriggers++;

        sound->handleFrameStart();
        sound->handleFrameEnd();
        ring += static_cast<double>(capture.lastNumSamples) / AUDIO_CHANNELS;
        occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
    }

    EXPECT_EQ(refillTriggers, 0)
        << "Emergency refill fired at steady state - it would inject extra "
           "frames and spike occupancy above the A/V budget";

    _context->pAudioRingOccupancy.store(nullptr, std::memory_order_release);
}

TEST_F(SoundAdaptivity_Test, DRC_RebasesOnDeviceRateChangeMidRun)
{
    // Device hotplug / OS default-output change: the frontend re-inits the
    // audio device at the new native rate, clears the ring and republishes
    // pAudioDeviceSampleRate. The DRC reads the cell every frame, so the
    // resample ratio must re-base to the new device/core ratio immediately
    // and occupancy must re-converge to the 70 ms setpoint - with no
    // emulator or sound-stack restart.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    std::atomic<uint32_t> occCell{0};
    _context->pAudioRingOccupancy.store(&occCell, std::memory_order_release);
    _context->pAudioDeviceSampleRate.store(44100, std::memory_order_release);

    _context->config.frame = 71680;
    sound->reset();

    double devRate = 44100.0;
    double ring = SoundManager::DRC_TARGET_MS * devRate / 1000.0;  // Start converged at the setpoint
    occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);

    auto runFrames = [&](int frames) {
        for (int f = 0; f < frames; f++)
        {
            sound->handleFrameStart();
            sound->handleFrameEnd();
            ring += static_cast<double>(capture.lastNumSamples) / AUDIO_CHANNELS;
            ring = std::max(0.0, ring - 71680.0 * devRate / 3500000.0);  // Real-time DAC
            occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
        }
    };

    runFrames(2000);
    EXPECT_NEAR(sound->getDrcRatio(), 1.0, 0.005) << "Converged unity before the reroute";

    // Reroute: device re-established at 48000, ring cleared then reseeded by
    // the emergency refill (~46 ms), new rate published
    devRate = 48000.0;
    ring = 46.0 * devRate / 1000.0;
    occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
    _context->pAudioDeviceSampleRate.store(48000, std::memory_order_release);

    sound->handleFrameStart();
    sound->handleFrameEnd();
    EXPECT_NEAR(sound->getDrcRatio(), 48000.0 / 44100.0, 48000.0 / 44100.0 * 0.006)
        << "Base ratio must re-base to the new device rate on the next frame";

    runFrames(6000);
    const double finalMs = ring * 1000.0 / devRate;
    EXPECT_NEAR(finalMs, SoundManager::DRC_TARGET_MS, 8.0) << "Occupancy must re-converge at the new device rate";

    _context->pAudioRingOccupancy.store(nullptr, std::memory_order_release);
    _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
}

TEST_F(SoundAdaptivity_Test, DRC_NativeDeviceRate48k_ConvergesAndConverts)
{
    // Device at native 48000 Hz (audio-sync Fix 3): the DRC resampler's base
    // ratio becomes 48000/44100 and the controller still converges occupancy
    // (measured in DEVICE frames) to the 70 ms setpoint.
    SoundManager* sound = _context->pSoundManager;
    ASSERT_NE(sound, nullptr);

    CallbackCapture capture;
    _context->pAudioCallback.store(&CallbackCapture::callback, std::memory_order_release);
    _context->pAudioManagerObj.store(&capture, std::memory_order_release);

    std::atomic<uint32_t> occCell{0};
    _context->pAudioRingOccupancy.store(&occCell, std::memory_order_release);
    _context->pAudioDeviceSampleRate.store(48000, std::memory_order_release);

    _context->config.frame = 71680;
    const double consumePerFrame = 71680.0 * 48000.0 / 3500000.0;  // Real-time DAC @48k

    sound->reset();
    double ring = SoundManager::DRC_TARGET_MS * 48.0;  // Start at setpoint (in device frames): verify HOLD
    occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);

    uint64_t totalDeviceSamples = 0;
    for (int f = 0; f < 6000; f++)
    {
        sound->handleFrameStart();
        sound->handleFrameEnd();
        totalDeviceSamples += capture.lastNumSamples / AUDIO_CHANNELS;
        ring += static_cast<double>(capture.lastNumSamples) / AUDIO_CHANNELS;
        ring = std::max(0.0, ring - consumePerFrame);
        occCell.store(static_cast<uint32_t>(ring), std::memory_order_relaxed);
    }

    double finalMs = ring * 1000.0 / 48000.0;
    EXPECT_NEAR(finalMs, SoundManager::DRC_TARGET_MS, 8.0) << "Occupancy must hold at setpoint with 48k device";

    // Output volume converted at ~48/44.1: 6000 frames x 903.168 core samples
    double expectedDevice = 6000.0 * 903.168 * 48000.0 / 44100.0;
    EXPECT_NEAR(static_cast<double>(totalDeviceSamples), expectedDevice, expectedDevice * 0.002)
        << "Device stream volume must reflect the 48000/44100 base ratio";

    _context->pAudioRingOccupancy.store(nullptr, std::memory_order_release);
    _context->pAudioDeviceSampleRate.store(0, std::memory_order_release);
}
