#include "stdafx.h"
#include "pch.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/chips/soundchip_turbosound.h"
#include "emulator/sound/chips/soundchip_turbosoundfm.h"

/// TSFM output-stage tests (TSFM design §12.4, implementation plan P6).
///
/// TsfmBitIdentity: the §11 envelope - the same port-write stream on a legacy
/// SoundChip_TurboSound and a SoundChip_TurboSoundFM with both chips at
/// YM2149 must render byte-identical audio (SSG-only traffic, chips selected
/// only with 0xFE/0xFF, addresses < 0x10, FM never enabled). TSFM's chip 0 is
/// the 0xFE chip, legacy's _chip0 is the 0xFF chip, so the per-chip buffers
/// and AY log records compare with chip indices swapped.
///
/// HoldNoJitter / MuteAtHoldInput drive the §6.2 sample-and-hold directly;
/// TsfmGain pins the §7.1 loudness references (carrier TL=0 -> +-0.075,
/// SSG A vol 15 -> +-0.15).

namespace
{

/// Pentagon frame length in T-states (the ini's config.frame)
constexpr uint32_t PENTAGON_FRAME = 71680;

/// Frame buffer size in bytes (interleaved int16 stereo, audio.h layout)
constexpr size_t FRAME_BUFFER_BYTES = MAX_SAMPLES_PER_FRAME * AUDIO_CHANNELS * sizeof(int16_t);

/// Log-sink trampoline: the sink is a plain function pointer
void LogSinkTrampoline(void* context, const AYLogRecord& record)
{
    static_cast<std::vector<AYLogRecord>*>(context)->push_back(record);
}

/// The FM note used by MuteAtHoldInput and TsfmGain: channel 2 (the OPN CSM
/// channel), three modulators at TL 0x7F (silent), carrier at TL 0 (the
/// design §7.1 reference: word +-8168), fastest attack, ~770 Hz
struct FmNoteProgram
{
    uint8_t modulatorTl = 0x7F;
    uint8_t carrierTl = 0x00;
};

}  // namespace

/// region <Shared fixture: direct device pair, no CPU>

class TsfmOutput_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr) << "Failed to create emulator";
        _context = _emulator->GetContext();

        // Pentagon has no hardware turbo, so AudioTstate(t) == t: the raw
        // time counter is the device's T-state axis. tt's low byte is the
        // fractional part, kept at zero.
        _context->pCore->GetZ80()->tt = 0;
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

    /// Park the CPU time counter at integer T-state t (port callbacks sync
    /// the core to exactly this position)
    void SetT(uint64_t t)
    {
        _context->pCore->GetZ80()->tt = uint32_t(t) << 8;
    }

    /// Program an FM note on board chip 0 with the FM path in `fmEnabled`
    /// state (control word 0xFA = chip 0 + FM on, design §5.1)
    void ProgramFmNote(SoundChip_TurboSoundFM& device, const FmNoteProgram& note)
    {
        SetT(100);
        device.portDeviceOutMethod(PORT_FFFD, 0xFA);
        const uint8_t setup[][2] = {
            {0x42, note.modulatorTl}, {0x46, note.modulatorTl},
            {0x4A, note.modulatorTl}, {0x4E, note.carrierTl},
            {0x52, 0x1F}, {0x56, 0x1F}, {0x5A, 0x1F}, {0x5E, 0x1F},  // AR: fastest
            // ch2 fnum 0x100 - the 0xA0 region is a latched pair: the upper
            // write only latches, the lower write commits both halves
            {0xA6, 0x41},
            {0xA2, 0x00},
            {0x28, 0xF2},  // key on, channel 2, all four operators
        };
        for (const auto& [reg, data] : setup)
        {
            device.portDeviceOutMethod(PORT_FFFD, reg);
            device.portDeviceOutMethod(PORT_BFFD, data);
        }
    }

    /// Run `frames` frames of the render loop with no further port traffic;
    /// returns the peak absolute sample over the FM-only buffer of chip 0,
    /// measured from `skipFrames` on (attack + FIR settling excluded)
    int16_t RunFmNotePeak(SoundChip_TurboSoundFM& device, int frames, int skipFrames)
    {
        int16_t peak = 0;
        for (int frame = 0; frame < frames; frame++)
        {
            SetT(0);
            device.handleFrameStart();
            SetT(PENTAGON_FRAME);
            device.handleStep();
            device.handleFrameEnd();

            if (frame < skipFrames)
                continue;
            const int16_t* fm = device.getFmBuffer(0);
            const size_t samples = device.getRenderedSamplesThisFrame() * AUDIO_CHANNELS;
            for (size_t i = 0; i < samples; i++)
            {
                const int16_t v = fm[i] < 0 ? int16_t(-fm[i]) : fm[i];
                if (v > peak)
                    peak = v;
            }
        }
        return peak;
    }
};

/// endregion

/// region <TsfmBitIdentity>

class TsfmBitIdentity_Test : public TsfmOutput_Test
{
protected:
    /// One §11 run: 200 frames x 50 port writes (10 000 total) of SSG-only
    /// traffic through both devices, everything memcmp-compared per frame
    void RunBitIdentity(uint32_t seed, bool hq, size_t rate)
    {
        SCOPED_TRACE(testing::Message() << "seed " << seed << (hq ? " HQ " : " LQ ") << rate);

        std::unique_ptr<SoundChip_TurboSound> legacy = std::make_unique<SoundChip_TurboSound>(_context);
        std::unique_ptr<SoundChip_TurboSoundFM> tsfm = std::make_unique<SoundChip_TurboSoundFM>(_context);

        // §11 setup: both chips at YM2149 on both devices (TSFM forces it in
        // its reset sequence; the legacy device defaults to the AY DAC table)
        legacy->getChip(0)->setChipModel(AYChipModel::YM2149);
        legacy->getChip(1)->setChipModel(AYChipModel::YM2149);
        legacy->setCoreRate(rate);
        legacy->setHQEnabled(hq);
        tsfm->setCoreRate(rate);
        tsfm->setHQEnabled(hq);

        // AY log taps + native-rate taps (SSG on both devices, FM taps on
        // TSFM only - they must carry pure silence)
        std::vector<AYLogRecord> legacyLog;
        std::vector<AYLogRecord> tsfmLog;
        legacy->setLogSink(LogSinkTrampoline, &legacyLog);
        tsfm->setLogSink(LogSinkTrampoline, &tsfmLog);
        legacy->getNativeTap()->activate();
        tsfm->getNativeTap()->activate();
        tsfm->getFmNativeTap(0)->activate();
        tsfm->getFmNativeTap(1)->activate();

        // Deterministic SSG-only traffic, §11 envelope: chip selects only
        // 0xFE/0xFF (both keep FM muted), addresses < 0x10, data bytes
        struct Event
        {
            int frame;
            uint64_t t;
            uint16_t port;
            uint8_t value;
        };
        std::vector<Event> events;
        {
            uint32_t lcg = seed * 2654435761u + 1;
            auto next = [&lcg](uint32_t bound)
            {
                lcg = lcg * 1664525u + 1013904223u;
                return lcg % bound;
            };
            for (int frame = 0; frame < kFrames; frame++)
            {
                uint64_t t = 16 + next(64);
                for (int i = 0; i < kWritesPerFrame; i++)
                {
                    t += 29 + next(200);
                    const uint32_t kind = next(8);
                    if ((frame == 0 && i == 0) || kind == 0)
                        events.push_back({frame, t, PORT_FFFD, uint8_t(next(2) ? 0xFF : 0xFE)});
                    else if (kind < 5)
                        events.push_back({frame, t, PORT_FFFD, uint8_t(next(0x10))});  // SSG address
                    else
                        events.push_back({frame, t, PORT_BFFD, uint8_t(next(256))});
                }
            }
        }

        std::vector<float> legacyTap;
        std::vector<float> tsfmTap;
        std::vector<float> fmTap;
        legacyTap.reserve(8192);
        tsfmTap.reserve(8192);
        fmTap.reserve(8192);

        size_t eventIndex = 0;
        for (int frame = 0; frame < kFrames; frame++)
        {
            // Frame base: T axis rebased to the new frame (the machine's
            // AdjustFrameCounters ran before OnFrameStart)
            SetT(0);
            legacy->handleFrameStart();
            tsfm->handleFrameStart();

            // Deliver this frame's events strictly in T order, at each
            // event's own T-state so both cores sync identically
            while (eventIndex < events.size() && events[eventIndex].frame == frame)
            {
                const Event& e = events[eventIndex];
                ASSERT_LT(e.t, uint64_t(PENTAGON_FRAME - 100)) << "traffic generator overran the frame";
                SetT(e.t);
                legacy->portDeviceOutMethod(e.port, e.value);
                tsfm->portDeviceOutMethod(e.port, e.value);
                eventIndex++;
            }

            SetT(PENTAGON_FRAME);
            legacy->handleStep();
            tsfm->handleStep();
            legacy->handleFrameEnd();
            tsfm->handleFrameEnd();

            // Combined buffer: byte-identical
            const size_t legacySamples = legacy->getRenderedSamplesThisFrame();
            ASSERT_GT(legacySamples, size_t(0)) << "frame " << frame << " rendered nothing";
            ASSERT_EQ(tsfm->getRenderedSamplesThisFrame(), legacySamples) << "frame " << frame;
            EXPECT_EQ(memcmp(legacy->getAudioBuffer(), tsfm->getAudioBuffer(), FRAME_BUFFER_BYTES), 0)
                << "combined buffer differs at frame " << frame;

            // Per-chip SSG buffers, chip indices swapped (TSFM chip 0 is the
            // 0xFE chip, legacy _chip0 is the 0xFF chip)
            EXPECT_EQ(memcmp(legacy->getChipBuffer(0), tsfm->getChipBuffer(1), FRAME_BUFFER_BYTES), 0)
                << "chip0<->chip1 differs at frame " << frame;
            EXPECT_EQ(memcmp(legacy->getChipBuffer(1), tsfm->getChipBuffer(0), FRAME_BUFFER_BYTES), 0)
                << "chip1<->chip0 differs at frame " << frame;

            // FM-only buffers: pure silence (FM never enabled, no key-on)
            const int16_t* fm0 = tsfm->getFmBuffer(0);
            const int16_t* fm1 = tsfm->getFmBuffer(1);
            for (size_t i = 0; i < legacySamples * AUDIO_CHANNELS; i++)
            {
                EXPECT_EQ(fm0[i], 0) << "FM 0 leaked into the mix at frame " << frame;
                EXPECT_EQ(fm1[i], 0) << "FM 1 leaked into the mix at frame " << frame;
            }

            // Native 218.75 kHz SSG tap: bitwise-identical float stream
            legacyTap.clear();
            tsfmTap.clear();
            fmTap.clear();
            legacyTap.resize(legacy->getNativeTap()->available() * 2);
            tsfmTap.resize(tsfm->getNativeTap()->available() * 2);
            const size_t legacyFramesPopped = legacy->getNativeTap()->pop(legacyTap.data(), legacyTap.size() / 2);
            const size_t tsfmFramesPopped = tsfm->getNativeTap()->pop(tsfmTap.data(), tsfmTap.size() / 2);
            ASSERT_EQ(tsfmFramesPopped, legacyFramesPopped) << "tap frame counts differ at frame " << frame;
            ASSERT_EQ(legacyTap.size(), tsfmTap.size());
            if (!legacyTap.empty())
                EXPECT_EQ(memcmp(legacyTap.data(), tsfmTap.data(), legacyTap.size() * sizeof(float)), 0)
                    << "native tap stream differs at frame " << frame;

            // FM taps: active but carrying only zeros (the raw pre-mute DAC
            // stream of an unkeyed chip)
            fmTap.resize(tsfm->getFmNativeTap(0)->available() * 2);
            tsfm->getFmNativeTap(0)->pop(fmTap.data(), fmTap.size() / 2);
            for (float v : fmTap)
                EXPECT_EQ(v, 0.0f) << "FM 0 tap carried non-silence at frame " << frame;
        }

        // AY log records: same everything, chip index swapped
        ASSERT_EQ(tsfmLog.size(), legacyLog.size()) << "log record counts differ";
        for (size_t i = 0; i < legacyLog.size(); i++)
        {
            EXPECT_EQ(legacyLog[i].frame, tsfmLog[i].frame) << "record " << i;
            EXPECT_EQ(legacyLog[i].tacts, tsfmLog[i].tacts) << "record " << i;
            EXPECT_EQ(legacyLog[i].pc, tsfmLog[i].pc) << "record " << i;
            EXPECT_EQ(legacyLog[i].port, tsfmLog[i].port) << "record " << i;
            EXPECT_EQ(legacyLog[i].reg, tsfmLog[i].reg) << "record " << i;
            EXPECT_EQ(legacyLog[i].value, tsfmLog[i].value) << "record " << i;
            EXPECT_EQ(uint8_t(1 - legacyLog[i].chip), tsfmLog[i].chip) << "record " << i;
        }
        EXPECT_EQ(legacyLog.size(), size_t(kFrames * kWritesPerFrame)) << "traffic generator lost writes";
    }

    static constexpr int kFrames = 200;        // design §12.4
    static constexpr int kWritesPerFrame = 50; // 10 000 writes total
};

TEST_F(TsfmBitIdentity_Test, BitIdenticalToLegacyWhileFmSilent)
{
    // P6 gate (design §12.4): §11, 10 000 writes x 200 frames x HQ/LQ across
    // EVERY supported core rate - 44.1 k .. 192 k (SoundManager's rate set):
    // the filters are designed per rate, never pinned to one frequency, the
    // same as the legacy TurboSound device. Seed 1 sweeps all six rates,
    // seeds 2-3 keep the original three-rate depth. This is the pinned
    // regression gate for the whole output stage, so it deliberately exceeds
    // the 50 ms budget (pure DSP, no CPU emulation; comparable to the
    // sanctioned player-boot E2E tests).
    const size_t rates[] = {44100, 48000, 88200, 96000, 176400, 192000};
    for (bool hq : {true, false})
        for (size_t rate : rates)
            RunBitIdentity(1, hq, rate);
    const size_t seedRates[] = {44100, 48000, 96000};
    for (uint32_t seed = 2; seed <= 3; seed++)
        for (bool hq : {true, false})
            for (size_t rate : seedRates)
                RunBitIdentity(seed, hq, rate);
}

/// endregion

/// region <HoldNoJitter>

TEST_F(TsfmOutput_Test, HoldNoJitter)
{
    // §6.2: at /6 the FM word period is 72 T and the half-tick grid 8 T, so
    // every word must be held for exactly 9 half-ticks - regardless of where
    // the word lands inside a half-tick. Synthetic words at exact 72 T
    // spacing, aligned and offset grids
    for (const uint64_t offset : {0u, 4u})
    {
        SCOPED_TRACE(testing::Message() << "offset " << offset);
        auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);

        constexpr int kWords = 16;
        for (int i = 0; i < kWords; i++)
        {
            const uint64_t t = 72 + offset + 72 * uint64_t(i);
            const int16_t value = int16_t(1000 + i * 137);  // unique, nonzero
            device->chip(0)->words.push(t, value);
        }

        // Drive half-ticks h = 8, 16, 24, ... past the last word and count
        // how many consecutive half-ticks each word's value stays in hold
        int counts[kWords] = {};
        const uint64_t lastT = 72 + offset + 72 * (kWords - 1);
        for (uint64_t h = 8; h <= lastT + 72; h += 8)
        {
            device->fmHalfTick(0, h);
            const double hold = device->outputState(0)->hold;
            for (int i = 0; i < kWords; i++)
                if (hold == double(1000 + i * 137) / 32768.0)
                    counts[i]++;
        }

        for (int i = 0; i < kWords; i++)
        {
            // The last word is never replaced, so it stays in hold for every
            // remaining driven half-tick - only replaced words pin the exact
            // hold length
            if (i < kWords - 1)
                EXPECT_EQ(counts[i], 9) << "word " << i << " (t = " << 72 + offset + 72 * i
                                         << ") held for " << counts[i] << " half-ticks";
            else
                EXPECT_GE(counts[i], 9) << "the last word was never consumed";
        }
        EXPECT_TRUE(device->chip(0)->words.empty()) << "not every word was consumed";
    }
}

/// endregion

/// region <MuteAtHoldInput>

TEST_F(TsfmOutput_Test, MuteAtHoldInput)
{
    // §6.2 mute-at-hold-input: the board mute grounds the DAC data line at
    // the hold register's input, so toggling 0xFA <-> 0xFE mid-note can never
    // produce a sample above the steady-state peak (no click, and the
    // decimator state stays warmed: the filter keeps seeing 0.0 while muted)
    auto runSession = [this](bool toggle) -> int16_t
    {
        auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
        ProgramFmNote(*device, FmNoteProgram{});
        if (!toggle)
            return RunFmNotePeak(*device, 60, 6);

        int16_t peak = 0;
        for (int frame = 0; frame < 60; frame++)
        {
            SetT(0);
            device->handleFrameStart();

            // Toggle the mute mid-note every frame: 0xFE mutes FM, 0xFA
            // re-enables it (chip 0 and register mode on both words)
            SetT(2000);
            device->portDeviceOutMethod(PORT_FFFD, uint8_t((frame & 1) ? 0xFA : 0xFE));

            SetT(PENTAGON_FRAME);
            device->handleStep();
            device->handleFrameEnd();

            if (frame < 6)
                continue;
            const int16_t* fm = device->getFmBuffer(0);
            const size_t samples = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS;
            for (size_t i = 0; i < samples; i++)
            {
                const int16_t v = fm[i] < 0 ? int16_t(-fm[i]) : fm[i];
                if (v > peak)
                    peak = v;
            }
        }
        return peak;
    };

    const int16_t steadyPeak = runSession(false);
    const int16_t togglePeak = runSession(true);

    ASSERT_GT(steadyPeak, 1000) << "the FM note never sounded";
    // The grounded-DAC step at each toggle is a bounded discontinuity (at
    // most the steady amplitude), which rings the 20 kHz FIR by up to the
    // Gibbs bound (~9% + passband ripple) - the real board's analog LPF
    // rings the same way. A stale-hold or DC-dump bug feeds the filter a
    // full-scale step and lands far above this bound
    EXPECT_LE(togglePeak, steadyPeak + steadyPeak / 5) << "the mute toggle clicked above the steady-state peak";
}

/// endregion

/// region <TsfmGain>

class TsfmGain_Test : public TsfmOutput_Test
{
};

TEST_F(TsfmGain_Test, Reference)
{
    // §7.1 gain references, pre-chain at 44.1 k:
    //   one carrier, TL = 0 -> ymfm word +-8168 -> FM buffer peak 0.075 x
    //   32767 = 2458 (+-5% for the FIR passband ripple)
    //   SSG channel A, vol 15 -> DAC 1.0 x pan 0.9 / 3 -> +-0.15 after the
    //   DC blocker -> chip buffer left peak 4915. The band-limited square
    //   overshoots the nominal half-amplitude (Gibbs ~9% + passband ripple,
    //   measured ~0.177), so the upper bound carries the overshoot margin
    //   while still catching a 2x gain error at ~0.30
    {
        auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
        device->setCoreRate(44100);
        ProgramFmNote(*device, FmNoteProgram{});
        const int16_t fmPeak = RunFmNotePeak(*device, 40, 10);
        EXPECT_GE(fmPeak, int16_t(2458 * 0.95)) << "FM too quiet: " << fmPeak;
        EXPECT_LE(fmPeak, int16_t(2458 * 1.05)) << "FM too loud: " << fmPeak;
    }

    // The same reference at the supported rate extremes: the FM decimator
    // is redesigned per rate (§6.3), so the note must land in the same band at
    // 88.2 k and 192 k - a filter pinned to one frequency would not
    for (const size_t rate : {size_t(88200), size_t(192000)})
    {
        SCOPED_TRACE(testing::Message() << "rate " << rate);
        auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
        device->setCoreRate(rate);
        ProgramFmNote(*device, FmNoteProgram{});
        const int16_t fmPeak = RunFmNotePeak(*device, 40, 10);
        EXPECT_GE(fmPeak, int16_t(2458 * 0.95)) << "FM too quiet: " << fmPeak;
        EXPECT_LE(fmPeak, int16_t(2458 * 1.05)) << "FM too loud: " << fmPeak;
    }

    {
        auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
        device->setCoreRate(44100);
        SetT(100);
        device->portDeviceOutMethod(PORT_FFFD, 0xFE);  // board chip 0, register mode
        const uint8_t setup[][2] = {
            {0x00, 100},  // tone period A ~549 Hz
            {0x01, 0x00},
            {0x07, 0xFE},  // mixer: channel A tone only
            {0x08, 15},    // channel A volume 15
        };
        for (const auto& [reg, data] : setup)
        {
            device->portDeviceOutMethod(PORT_FFFD, reg);
            device->portDeviceOutMethod(PORT_BFFD, data);
        }

        int16_t peak = 0;
        for (int frame = 0; frame < 40; frame++)
        {
            SetT(0);
            device->handleFrameStart();
            SetT(PENTAGON_FRAME);
            device->handleStep();
            device->handleFrameEnd();

            if (frame < 10)
                continue;
            const int16_t* chip = device->getChipBuffer(0);
            const size_t samples = device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS;
            for (size_t i = 0; i < samples; i += 2)  // left channel only
            {
                const int16_t v = chip[i] < 0 ? int16_t(-chip[i]) : chip[i];
                if (v > peak)
                    peak = v;
            }
        }
        EXPECT_GE(peak, int16_t(4915 * 0.90)) << "SSG too quiet: " << peak;
        EXPECT_LE(peak, int16_t(4915 * 1.33)) << "SSG too loud: " << peak;
    }
}

TEST_F(TsfmGain_Test, LiveCoreRateSwitch)
{
    // SoundManager::applyCoreRate switches a RUNNING device at a frame
    // boundary (44.1 k -> 48 k, say). The switch must redesign all six
    // decimators for the new rate - taps scaled per input side, phase step
    // for the new output rate - and re-attach the FM slaves, exactly as the
    // legacy device redesigns its four SSG filters. Checked on a device that
    // has already rendered a note, not a fresh one
    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    device->setCoreRate(44100);
    ProgramFmNote(*device, FmNoteProgram{});
    const int16_t peak441 = RunFmNotePeak(*device, 20, 10);
    EXPECT_GE(peak441, int16_t(2458 * 0.95)) << "FM too quiet at 44.1 k: " << peak441;
    const size_t samples441 = device->getRenderedSamplesThisFrame();

    for (const size_t rate : {size_t(48000), size_t(96000), size_t(44100)})
    {
        SCOPED_TRACE(testing::Message() << "rate " << rate);
        device->setCoreRate(rate);

        // Filter designs: SSG side 96 taps at 218.75 kHz, FM side 192 taps at
        // 437.5 kHz, both with the phase step of the new output rate
        for (int chip = 0; chip < 2; chip++)
        {
            const FilterDecimator& ssg = device->getChip(chip)->decimatorLeft();
            const FilterDecimator& fm = device->outputState(chip)->decimator;
            EXPECT_EQ(ssg.taps(), 96u);
            EXPECT_EQ(fm.taps(), 192u);
            EXPECT_NEAR(ssg.samplesPerOutput(), FilterDecimator::INPUT_RATE / double(rate), 1e-9);
            EXPECT_NEAR(fm.samplesPerOutput(), 2.0 * FilterDecimator::INPUT_RATE / double(rate), 1e-9);

            // Bit-for-bit the designs FilterDecimator_Test validates for
            // this rate (the SSG one is the legacy device's, §11)
            FilterDecimator expectSsg;
            FilterDecimator expectFm;
            expectSsg.configure(double(rate));
            expectFm.configure(double(rate), FilterDecimator::Quality::Reference, false, 2.0 * FilterDecimator::INPUT_RATE);
            EXPECT_EQ(ssg.coefficients(), expectSsg.coefficients()) << "SSG design differs from the legacy design";
            EXPECT_EQ(fm.coefficients(), expectFm.coefficients()) << "FM design differs from the 437.5 kHz design";
        }

        // Rendering: the note keeps its §7.1 level through the switch (the
        // slaves are re-attached, so the FM buffer is filled at the new
        // cadence), and the per-frame sample count follows the rate
        const int16_t peak = RunFmNotePeak(*device, 12, 4);
        EXPECT_GE(peak, int16_t(2458 * 0.95)) << "FM too quiet after switch: " << peak;
        EXPECT_LE(peak, int16_t(2458 * 1.05)) << "FM too loud after switch: " << peak;
        const size_t samples = device->getRenderedSamplesThisFrame();
        EXPECT_NEAR(double(samples), double(samples441) * double(rate) / 44100.0, 2.0)
            << "frame sample count does not follow the new rate";
    }
}

/// endregion
