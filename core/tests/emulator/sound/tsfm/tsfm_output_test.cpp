#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
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
/// TsfmGain pins the loudness references: one carrier at TL=0 -> +-0.176 in
/// the FM buffer (the 0.30 code baseline times the shipped 7.4 dB trim, see
/// docs/.../materials/volume/README.md), SSG A vol 15 -> +-0.15.

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
/// design §7.1 reference: word +-8168), fastest attack, fnum 0x100, block 7,
/// carrier MUL 1: phase step ((512 << 7) >> 2) x 2 / 2 = 16384 = 2^20 / 64 -
/// 64 FM words per cycle, 759.5 Hz. (Block 0 / MUL 0 was 2.97 Hz: DC to the
/// output coupling high-pass, which attenuated it by 0.8 dB.)
struct FmNoteProgram
{
    uint8_t modulatorTl = 0x7F;
    uint8_t carrierTl = 0x00;
};

/// FM buffer peak for one carrier at TL 0 with the shipped 7.4 dB trim
/// (derivation in TsfmGain_Test.Reference)
constexpr int kFmOneCarrierPeak = 5744;

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
            {0x3E, 0x01},  // carrier MUL 1
            // ch2 fnum 0x100, block 7 - the 0xA0 region is a latched pair: the
            // upper write only latches, the lower write commits both halves
            {0xA6, 0x39},
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

            // FM-only buffers: pure silence (FM never enabled, no key-on).
            // Scan to the first leak instead of one EXPECT per sample: the
            // assertion is the same, the green path skips ~100k gtest
            // comparisons per session, and a failure still pins frame + sample
            const int16_t* fm0 = tsfm->getFmBuffer(0);
            const int16_t* fm1 = tsfm->getFmBuffer(1);
            const size_t fmSamples = legacySamples * AUDIO_CHANNELS;
            size_t leak0 = 0;
            while (leak0 < fmSamples && fm0[leak0] == 0)
                leak0++;
            size_t leak1 = 0;
            while (leak1 < fmSamples && fm1[leak1] == 0)
                leak1++;
            EXPECT_EQ(leak0, fmSamples)
                << "FM 0 leaked into the mix at frame " << frame << ", sample " << leak0;
            EXPECT_EQ(leak1, fmSamples)
                << "FM 1 leaked into the mix at frame " << frame << ", sample " << leak1;

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
            // stream of an unkeyed chip). Same first-leak scan as above
            fmTap.resize(tsfm->getFmNativeTap(0)->available() * 2);
            tsfm->getFmNativeTap(0)->pop(fmTap.data(), fmTap.size() / 2);
            size_t tapLeak = 0;
            while (tapLeak < fmTap.size() && fmTap[tapLeak] == 0.0f)
                tapLeak++;
            EXPECT_EQ(tapLeak, fmTap.size())
                << "FM 0 tap carried non-silence at frame " << frame << ", sample " << tapLeak;
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

    static constexpr int kFrames = 30;         // design §12.4 asked for 200; 30 keeps the 18-session matrix well under 1 s
    static constexpr int kWritesPerFrame = 50; // 1 500 writes per session
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
    // Seeds 2-3 add traffic-generator depth at the three classic rates. One
    // quality each is enough: seed 1 already crossed every rate with both
    // paths, so the seed axis and the HQ/LQ axis do not need a full cross
    // product (that halved the old 24-session matrix)
    const size_t seedRates[] = {44100, 48000, 96000};
    for (size_t rate : seedRates)
        RunBitIdentity(2, true, rate);
    for (size_t rate : seedRates)
        RunBitIdentity(3, false, rate);
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

/// region <TsfmTimeline>

/// HoldNoJitter pins the word-to-half-tick mapping inside one frame; these
/// pin it across frame boundaries, where the words and the render cursor are
/// rebased separately. A tone with an exactly known period is rendered for
/// many frames; each frame's phase is fitted against one global output-sample
/// index, so any per-frame slip of the FM content relative to the output
/// sample clock shows up as a phase step between consecutive frames.
///
/// Tone: channel 2, algorithm 7, carrier only (modulators at TL 0x7F),
/// fnum 1024, block 7, MUL 1 -> phase step ((2048 << 7) >> 2) = 65536 =
/// 2^20 / 16: exactly 16 FM words per cycle = 1152 T (~3038 Hz).
class TsfmTimeline_Test : public TsfmOutput_Test
{
protected:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kPeriodT = 16.0 * 72.0;
    static constexpr double kMaxSlipT = 4.0;

    // Frame length (the device clips at config.frame - set both) and the
    // per-frame drive: one step at the frame end, or instruction-granular
    uint32_t _frameT = PENTAGON_FRAME;
    bool _stepped = false;
    uint32_t _overshoot = 0;

    /// Worst frame-to-frame slip of one measured run
    struct SlipReport
    {
        double worstT = 0.0;
        int worstFrame = 0;
        int slipped = 0;  // boundaries over kMaxSlipT
        int boundaries = 0;
        int16_t peak = 0;
    };

    /// Opens frame 0 and keys the tone inside it, as a running machine would:
    /// a write before the first frame start would sit on a pseudo-frame of its
    /// own and shift the timeline by its length
    void StartTone(SoundChip_TurboSoundFM& device)
    {
        SetT(0);
        device.handleFrameStart();
        SetT(100);
        device.portDeviceOutMethod(PORT_FFFD, 0xFA);  // chip 0, FM on
        const uint8_t setup[][2] = {
            {0xB2, 0x07},                                            // ch2 algorithm 7
            {0x42, 0x7F}, {0x46, 0x7F}, {0x4A, 0x7F}, {0x4E, 0x00},  // TL: carrier only
            {0x3E, 0x01},                                            // carrier MUL 1
            {0x52, 0x1F}, {0x56, 0x1F}, {0x5A, 0x1F}, {0x5E, 0x1F},  // AR: fastest
            {0xA6, 0x3C},                                            // block 7, fnum hi 4 (latched)
            {0xA2, 0x00},                                            // fnum lo, commits
            {0x28, 0xF2},                                            // key on ch2, all ops
        };
        for (const auto& [reg, data] : setup)
        {
            device.portDeviceOutMethod(PORT_FFFD, reg);
            device.portDeviceOutMethod(PORT_BFFD, data);
        }
    }

    /// Render `frames` frames at the device's current rate and measure the
    /// slips from `settleFrames` on. `frameOpen`: frame 0 was already opened
    /// by StartTone. The mean step is removed - a constant step is a frequency
    /// mismatch between the tone and the nominal output rate, not a slip
    SlipReport RunAndMeasure(SoundChip_TurboSoundFM& device, int frames, int settleFrames, bool frameOpen)
    {
        // Output sample period in T: the mixer's integer accumulator emits
        // one sample per CPU_CLOCK_RATE / rate T on average
        const double omega = 2.0 * kPi * (double(CPU_CLOCK_RATE) / double(device.getCoreRate())) / kPeriodT;

        SlipReport report;
        std::vector<double> phases;
        uint64_t n = 0;  // output-sample index, continuous across frames
        for (int frame = 0; frame < frames; frame++)
        {
            if (frame > 0 || !frameOpen)
            {
                SetT(_overshoot);  // where AdjustFrameCounters left the counter
                device.handleFrameStart();
            }
            if (_stepped)
            {
                // Instruction-granular: steps through the frame, the last
                // instruction runs 0..7 T past its end, then the counter is
                // rebased by the frame length before the frame-end hook
                for (uint32_t t = 500; t < _frameT; t += 500)
                {
                    SetT(t);
                    device.handleStep();
                }
                _overshoot = uint32_t((frame * 5) % 8);
                SetT(_frameT + _overshoot);
                device.handleStep();
                SetT(_overshoot);
            }
            else
            {
                SetT(_frameT);
                device.handleStep();
            }
            device.handleFrameEnd();

            const int16_t* fm = device.getFmBuffer(0);
            const size_t samples = device.getRenderedSamplesThisFrame();

            // Least-squares fit y = a cos(wn) + b sin(wn) over this frame
            double cc = 0, ss = 0, cs = 0, yc = 0, ys = 0;
            for (size_t i = 0; i < samples; i++, n++)
            {
                const double y = fm[i * AUDIO_CHANNELS];
                const double c = std::cos(omega * double(n));
                const double s = std::sin(omega * double(n));
                cc += c * c;
                ss += s * s;
                cs += c * s;
                yc += y * c;
                ys += y * s;
                if (frame >= settleFrames)
                    report.peak = std::max<int16_t>(report.peak, int16_t(std::abs(y)));
            }
            if (frame < settleFrames)
                continue;
            const double det = cc * ss - cs * cs;
            const double a = (yc * ss - ys * cs) / det;
            const double b = (ys * cc - yc * cs) / det;
            phases.push_back(std::atan2(b, a));
        }

        std::vector<double> slips;
        for (size_t k = 1; k < phases.size(); k++)
        {
            double d = phases[k] - phases[k - 1];
            while (d > kPi) d -= 2.0 * kPi;
            while (d < -kPi) d += 2.0 * kPi;
            slips.push_back(d / (2.0 * kPi) * kPeriodT);
        }
        double mean = 0;
        for (const double s : slips)
            mean += s;
        mean /= double(slips.size());

        report.boundaries = int(slips.size());
        for (size_t k = 0; k < slips.size(); k++)
        {
            const double dev = std::abs(slips[k] - mean);
            if (dev > kMaxSlipT)
                report.slipped++;
            if (dev > report.worstT)
            {
                report.worstT = dev;
                report.worstFrame = int(k) + 1 + settleFrames;
            }
        }
        return report;
    }

    static std::string Describe(const SlipReport& r)
    {
        std::ostringstream s;
        s << "FM content slipped " << r.worstT << " T at frame " << r.worstFrame << "; " << r.slipped << " of "
          << r.boundaries << " frame boundaries slipped by more than " << kMaxSlipT << " T";
        return s.str();
    }
};

TEST_F(TsfmTimeline_Test, ContinuousAcrossFrames)
{
    // Deliberately over the 50 ms budget: four sessions of 120 frames of pure
    // DSP; fewer frames at 48 kHz would miss the slips (only ~1 frame in 4)
    for (const bool hq : {true, false})
    {
        for (const size_t rate : {size_t(44100), size_t(48000)})
        {
            SCOPED_TRACE(testing::Message() << (hq ? "HQ " : "LQ ") << rate);
            auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
            device->setCoreRate(rate);
            device->setHQEnabled(hq);
            StartTone(*device);

            const SlipReport r = RunAndMeasure(*device, 120, 10, true);
            ASSERT_GT(r.peak, 1000) << "the FM tone never sounded";
            EXPECT_LE(r.worstT, kMaxSlipT) << Describe(r);
        }
    }
}

TEST_F(TsfmTimeline_Test, ContinuousWithInstructionGranularSteps)
{
    // The real core drives the device once per instruction and ends a frame
    // a few T-states past its boundary; the 128K/+3 frame (70908 T) is not a
    // whole number of 16 T SSG ticks nor of microseconds. The FM timeline must
    // stay continuous under both. Over the 50 ms budget like the test above
    // (~140 steps per frame, 4 sessions of 120 frames)
    const uint32_t configFrame = _context->config.frame;
    for (const uint32_t frameT : {PENTAGON_FRAME, 70908u})
    {
        for (const auto& [rate, hq] : {std::pair<size_t, bool>{44100, true}, std::pair<size_t, bool>{48000, false}})
        {
            SCOPED_TRACE(testing::Message() << "frame " << frameT << (hq ? " HQ " : " LQ ") << rate);
            _context->config.frame = frameT;
            _frameT = frameT;
            _stepped = true;
            _overshoot = 0;
            auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
            device->setCoreRate(rate);
            device->setHQEnabled(hq);
            StartTone(*device);

            const SlipReport r = RunAndMeasure(*device, 120, 10, true);
            ASSERT_GT(r.peak, 1000) << "the FM tone never sounded";
            EXPECT_LE(r.worstT, kMaxSlipT) << Describe(r);
        }
    }
    _context->config.frame = configFrame;
}

TEST_F(TsfmTimeline_Test, ContinuousAfterLiveRateSwitch)
{
    // SoundManager::applyCoreRate switches a RUNNING device at a frame
    // boundary when the output device's rate changes: the sample accumulator
    // restarts and the decimators are redesigned, moving the render loop
    // against the CPU clock by up to one old-rate sample. The switch frame
    // itself may slip once (every filter is re-derived there anyway); after it
    // the FM timeline must be continuous again at the new rate, with no drift
    // pushing the cursor out of its window later on. Each segment's phase
    // tracking restarts at the new rate; the first two frames are excluded
    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    device->setCoreRate(44100);
    StartTone(*device);
    SlipReport r = RunAndMeasure(*device, 40, 10, true);
    ASSERT_GT(r.peak, 1000) << "the FM tone never sounded";
    EXPECT_LE(r.worstT, kMaxSlipT) << "44100 before the switches: " << Describe(r);

    for (const bool hq : {true, false})
    {
        device->setHQEnabled(hq);
        for (const size_t rate : {size_t(48000), size_t(96000), size_t(192000), size_t(44100)})
        {
            SCOPED_TRACE(testing::Message() << (hq ? "HQ " : "LQ ") << "switched to " << rate);
            device->setCoreRate(rate);
            r = RunAndMeasure(*device, 40, 2, false);
            EXPECT_GT(r.peak, 1000) << "the FM tone dropped out after the switch";
            EXPECT_LE(r.worstT, kMaxSlipT) << Describe(r);
        }
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

/// region <FmOutputCoupling>

TEST_F(TsfmOutput_Test, FmCouplingMatchesSchematic)
{
    // Rev C: C14/C15 = 10 uF into R17||R18 (R19||R20) = 24 k||24 k = 12 k at
    // the DA5 virtual grounds, running at the 437.5 kHz half-tick rate
    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    for (int chip = 0; chip < 2; chip++)
    {
        const FilterDCBlocker& coupling = device->outputState(chip)->coupling;
        EXPECT_NEAR(coupling.cutoffHz(), 1.0 / (2.0 * 3.14159265358979323846 * 12000.0 * 10e-6), 1e-12);
        EXPECT_NEAR(coupling.cutoffHz(), 1.33, 0.01);
        EXPECT_DOUBLE_EQ(coupling.sampleRate(), 437500.0);
    }
}

TEST_F(TsfmOutput_Test, FmCouplingReleasesStuckDc)
{
    // Issue #5 / #13: a tune can leave the DAC at a constant word (a stuck
    // carrier, or a DC step at mute - hardware-reference §5.3). The coupling
    // passes the step and then releases it with RC = 120 ms; without it the
    // offset reached the mix for good. Also mute: the grounded data line is
    // a step the other way, released the same way
    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    SetT(100);
    device->portDeviceOutMethod(PORT_FFFD, 0xFA);  // chip 0, FM on
    device->chip(0)->words.push(0, 16384);         // DAC held at +0.5

    const double rcSeconds = 1.0 / (2.0 * 3.14159265358979323846 * kTsfmFmCouplingHz);
    const int64_t halfTicksPerSecond = int64_t(kTsfmFmInputRate);
    int64_t h = 8;
    device->fmHalfTick(0, h);
    EXPECT_NEAR(device->outputState(0)->lastFed, 0.5, 1e-4) << "the step itself passes";

    auto runSeconds = [&](double seconds)
    {
        for (int64_t i = 0; i < int64_t(seconds * double(halfTicksPerSecond)); i++)
            device->fmHalfTick(0, h += 8);
        return device->outputState(0)->lastFed;
    };
    EXPECT_NEAR(runSeconds(rcSeconds), 0.5 * std::exp(-1.0), 2e-3) << "one time constant";
    EXPECT_LT(std::fabs(runSeconds(4.0 * rcSeconds)), 0.5 * 0.01) << "released after 5 RC";

    // Mute: the held +0.5 drops to 0 at the DAC -> a -0.5 step, released too
    device->portDeviceOutMethod(PORT_FFFD, 0xFE);  // chip 0, FM muted
    device->fmHalfTick(0, h += 8);
    EXPECT_NEAR(device->outputState(0)->lastFed, -0.5, 0.01) << "mute step passes";
    EXPECT_LT(std::fabs(runSeconds(5.0 * rcSeconds)), 0.5 * 0.01) << "mute step released after 5 RC";
}

/// endregion

/// region <OutputStageFlush>

TEST_F(TsfmOutput_Test, HQReturnDoesNotReplayPreSwitchAudio)
{
    // ISSUES #7: in LQ the HQ decimators are not fed, so their FIR history
    // still holds the audio from before the LQ period; on the way back to HQ
    // it replayed (~0.3 ms of a tone that stopped long ago). Both TurboSound
    // slot devices: a 1 kHz SSG tone in HQ, LQ, the tone silenced and settled
    // (8 tau of the AY output high-pass), back to HQ - the first HQ samples
    // must continue the settled silence
    auto runFrame = [this](ITurboSoundDevice& device)
    {
        SetT(0);
        device.handleFrameStart();
        SetT(PENTAGON_FRAME);
        device.handleStep();
        device.handleFrameEnd();
    };
    auto poke = [this](ITurboSoundDevice& device, uint8_t reg, uint8_t value)
    {
        SetT(100);
        device.portDeviceOutMethod(PORT_FFFD, reg);
        device.portDeviceOutMethod(PORT_BFFD, value);
    };

    std::unique_ptr<ITurboSoundDevice> devices[] = {std::make_unique<SoundChip_TurboSoundFM>(_context),
                                                    std::make_unique<SoundChip_TurboSound>(_context)};
    const char* names[] = {"TSFM", "TurboSound"};
    const int chipOf0xFE[] = {0, 1};
    const double tauMs = 1000.0 / (2.0 * 3.14159265358979323846 * SoundChip_AY8910::OUTPUT_HIGHPASS_HZ);
    const int settleFrames = int(std::ceil(8.0 * tauMs / 20.0));
    for (int d = 0; d < 2; d++)
    {
        SCOPED_TRACE(names[d]);
        ITurboSoundDevice& device = *devices[d];
        device.setHQEnabled(true);
        SetT(50);
        device.portDeviceOutMethod(PORT_FFFD, 0xFE);  // chip 0 (TSFM: FM muted; legacy: ignored select)
        poke(device, 0, 109);                          // channel A ~1 kHz
        poke(device, 1, 0);
        poke(device, 7, 0x3E);                         // tone A only
        poke(device, 8, 15);
        for (int f = 0; f < 5; f++)
            runFrame(device);

        device.setHQEnabled(false);
        poke(device, 8, 0);
        for (int f = 0; f < settleFrames; f++)
            runFrame(device);

        device.setHQEnabled(true);
        runFrame(device);
        // The 0xFE chip: TSFM's chip 0, the legacy device's chip 1
        const int16_t* buf = device.getChipBuffer(chipOf0xFE[d]);  // left/right interleaved
        int lo = buf[0], hi = buf[0];
        for (size_t i = 0; i < 40; i += 2)  // first 20 samples, left channel
        {
            lo = std::min<int>(lo, buf[i]);
            hi = std::max<int>(hi, buf[i]);
        }
        EXPECT_LT(hi - lo, 64) << "the first HQ samples replayed the pre-switch tone (range " << hi - lo << ")";
    }
}

TEST_F(TsfmOutput_Test, ResumeAfterSuppressionContinuesWithoutStep)
{
    // While synthesis is suppressed (turbo without audio) nothing is
    // rendered, but the chip keeps running: the FM output moves on while the
    // hold, the output coupling and the decimator history keep the pre-gap
    // state. On resume that came back as a step (old coupling charge plus the
    // jump from the stale hold to the live level). A near-DC carrier (2.97 Hz,
    // block 0 / MUL 0) makes any step stand out against a sample-to-sample
    // change of a few LSB; the first resumed frame must be step-free
    auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
    device->setHQEnabled(true);
    SetT(100);
    device->portDeviceOutMethod(PORT_FFFD, 0xFA);  // chip 0, FM on
    const uint8_t setup[][2] = {
        {0x42, 0x7F}, {0x46, 0x7F}, {0x4A, 0x7F}, {0x4E, 0x00},  // carrier only (algorithm 0 output O4)
        {0x52, 0x1F}, {0x56, 0x1F}, {0x5A, 0x1F}, {0x5E, 0x1F},  // AR: fastest
        {0xA6, 0x01}, {0xA2, 0x00},                              // fnum 0x100, block 0: 2.97 Hz
        {0x28, 0xF2},                                            // key on ch2
    };
    for (const auto& [reg, data] : setup)
    {
        device->portDeviceOutMethod(PORT_FFFD, reg);
        device->portDeviceOutMethod(PORT_BFFD, data);
    }
    auto runFrame = [&]()
    {
        SetT(0);
        device->handleFrameStart();
        SetT(PENTAGON_FRAME);
        device->handleStep();
        device->handleFrameEnd();
    };
    auto maxAdjacentStep = [&]()
    {
        const int16_t* fm = device->getFmBuffer(0);
        int step = 0;
        for (size_t i = 2; i < device->getRenderedSamplesThisFrame() * AUDIO_CHANNELS; i += 2)
            step = std::max(step, std::abs(int(fm[i]) - int(fm[i - 2])));
        return step;
    };

    for (int f = 0; f < 4; f++)  // ~80 ms: well up the first quarter period
        runFrame();
    const int steadyStep = maxAdjacentStep();
    ASSERT_GT(std::abs(int(device->getFmBuffer(0)[0])), 500) << "precondition: the carrier is not sounding";

    device->setSynthesisSuppressed(true);
    for (int f = 0; f < 3; f++)  // 60 ms gap: the carrier moves on
        runFrame();
    device->setSynthesisSuppressed(false);
    runFrame();

    EXPECT_LE(maxAdjacentStep(), steadyStep + 16)
        << "a step in the first frame after the gap (steady-state step " << steadyStep << ")";
}

/// endregion

/// region <TimedSsgWrites>

TEST_F(TsfmOutput_Test, SsgWritesLandOnTheirOwnTick)
{
    // An OUT to an SSG register used to reach the generators wherever the
    // render loop happened to stand when the instruction ran: rendering only
    // advances in whole output samples (22.7 us at 44.1 kHz), so a write moved
    // by up to one output sample - "digital" playback through the volume
    // register lost ~14 dB of SNR to that jitter. Writes are timed now: each
    // is applied on the SSG tick (16 T) its T-state falls in, a constant lag
    // behind the CPU.
    //
    // A pulse train on channel A's volume (tone and noise off: the volume is
    // the output level) - 15 at t, 0 at t + 160 T, every start at a different
    // offset - driven like the CPU drives the device: handleStep once per
    // 12 T "instruction", the OUT inside it. Read per tick through the native
    // tap (218.75 kHz, before decimation): every pulse must be exactly 10
    // ticks wide and start at the same lag after its write, to the tick.
    // Both TurboSound-slot devices
    std::unique_ptr<ITurboSoundDevice> devices[] = {std::make_unique<SoundChip_TurboSoundFM>(_context),
                                                    std::make_unique<SoundChip_TurboSound>(_context)};
    const char* names[] = {"TSFM", "TurboSound"};
    const uint8_t selectCmd[] = {0xFE, 0xFF};  // a chip of each device (select word differs)
    for (int d = 0; d < 2; d++)
    {
        SCOPED_TRACE(names[d]);
        ITurboSoundDevice& device = *devices[d];
        device.setHQEnabled(false);  // LQ: the tap is fed the same per tick either way
        device.getNativeTap()->activate();

        auto write = [&](uint64_t t, uint8_t reg, uint8_t value)
        {
            SetT(t);
            device.portDeviceOutMethod(PORT_FFFD, reg);
            device.portDeviceOutMethod(PORT_BFFD, value);
        };

        struct Pulse
        {
            int64_t start;  // absolute T of the "volume 15" write
        };
        std::vector<Pulse> pulses;
        constexpr int kFrames = 2;
        constexpr int64_t kWidthT = 160;
        uint64_t overshoot = 0;  // where the last instruction left the counter (AdjustFrameCounters)
        for (int frame = 0; frame < kFrames; frame++)
        {
            SetT(overshoot);
            device.handleFrameStart();
            if (frame == 0)
            {
                SetT(10);
                device.portDeviceOutMethod(PORT_FFFD, selectCmd[d]);
                write(12, 7, 0x3F);  // tone and noise off on all channels
                write(14, 8, 0);
            }
            // Pulses every 1000 T, the start offset cycling through 0..28 T
            std::vector<std::pair<uint64_t, uint8_t>> events;
            for (uint64_t k = 1; k * 1000 + 400 < PENTAGON_FRAME; k++)
            {
                const uint64_t t = k * 1000 + (k * 7) % 29;
                events.push_back({t, 15});
                events.push_back({t + kWidthT, 0});
                pulses.push_back({int64_t(frame) * PENTAGON_FRAME + int64_t(t)});
            }
            // 12 T instructions until one crosses the frame end, as the CPU
            // does; the next frame starts at its overshoot
            size_t next = 0;
            uint64_t t = overshoot;
            do
            {
                t += 12;
                // The OUT executes inside the instruction ending at t
                while (next < events.size() && events[next].first <= t)
                {
                    write(events[next].first, 8, events[next].second);
                    next++;
                }
                SetT(t);
                device.handleStep();
            } while (t < PENTAGON_FRAME);
            overshoot = t - PENTAGON_FRAME;
            SetT(overshoot);
            device.handleFrameEnd();
        }

        std::vector<float> tap(device.getNativeTap()->available() * 2);
        const size_t ticks = device.getNativeTap()->pop(tap.data(), tap.size() / 2);
        ASSERT_GT(ticks, size_t(4000)) << "the tap saw too few ticks";
        float high = 0.0f;
        for (size_t i = 0; i < ticks; i++)
            high = std::max(high, tap[i * 2]);
        ASSERT_GT(high, 0.05f) << "no pulse reached the output";

        // Tick i sits at render time 16 i - lag: the cursor starts at -lag
        // and runs continuously across frames
        std::vector<int64_t> starts;
        std::vector<int> widths;
        for (size_t i = 1; i < ticks; i++)
        {
            if (tap[i * 2] > high / 2 && tap[(i - 1) * 2] <= high / 2)
            {
                size_t j = i;
                while (j < ticks && tap[j * 2] > high / 2)
                    j++;
                starts.push_back(int64_t(i) * 16 - kTurboSoundRenderLagT);
                widths.push_back(int(j - i));
            }
        }
        ASSERT_EQ(starts.size(), pulses.size()) << "every pulse must appear once";
        int64_t lo = INT64_MAX, hi = INT64_MIN;
        for (size_t k = 0; k < pulses.size(); k++)
        {
            EXPECT_EQ(widths[k], int(kWidthT / 16)) << "pulse " << k << " is not 160 T wide";
            const int64_t lag = starts[k] - pulses[k].start;
            lo = std::min(lo, lag);
            hi = std::max(hi, lag);
        }
        EXPECT_LT(hi - lo, 16) << "pulses start " << lo << ".." << hi << " T after their writes: not on their own tick";
    }
}

/// endregion

/// region <TsfmGain>

class TsfmGain_Test : public TsfmOutput_Test
{
};

TEST_F(TsfmGain_Test, Reference)
{
    // Gain references, pre-chain at 44.1 k:
    //   one carrier, TL = 0 -> ymfm word +-8168 -> 0.2493 x kFmBaseGain 0.30
    //   x 10^(7.4/20) = 0.1753 -> FM buffer peak 0.1753 x 32767 = 5744
    //   (+-5% for the FIR passband ripple). The 7.4 dB comes from the shipped
    //   ini (TSFM_FmTrimDb) and is the real-board measurement of 2026-09-13:
    //   one FM carrier sits 0.4-1.2 dB ABOVE one SSG channel at volume 15 on
    //   the same output (materials/volume/README.md)
    //   SSG channel A, vol 15 -> DAC 1.0 x pan 0.9 / 3 -> +-0.15 after the
    //   DC blocker -> chip buffer left peak 4915. The band-limited square
    //   overshoots the nominal half-amplitude (Gibbs ~9% + passband ripple,
    //   measured ~0.177), so the upper bound carries the overshoot margin
    //   while still catching a 2x gain error at ~0.30
    ASSERT_DOUBLE_EQ(_context->config.sound.tsfmFmTrimDb, 7.4)
        << "the shipped Pentagon ini must carry the hardware-derived FM trim";
    {
        auto device = std::make_unique<SoundChip_TurboSoundFM>(_context);
        device->setCoreRate(44100);
        EXPECT_DOUBLE_EQ(device->fmTrimDb(), 7.4) << "the trim must be applied at construction";
        ProgramFmNote(*device, FmNoteProgram{});
        const int16_t fmPeak = RunFmNotePeak(*device, 40, 10);
        EXPECT_GE(fmPeak, int16_t(kFmOneCarrierPeak * 0.95)) << "FM too quiet: " << fmPeak;
        EXPECT_LE(fmPeak, int16_t(kFmOneCarrierPeak * 1.05)) << "FM too loud: " << fmPeak;
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
        EXPECT_GE(fmPeak, int16_t(kFmOneCarrierPeak * 0.95)) << "FM too quiet: " << fmPeak;
        EXPECT_LE(fmPeak, int16_t(kFmOneCarrierPeak * 1.05)) << "FM too loud: " << fmPeak;
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

TEST_F(TsfmGain_Test, DecimatorQualityAppliesToEveryDecimator)
{
    // [SOUND] DecimatorQuality=HighFidelity: both TurboSound-slot devices
    // build every anti-alias FIR at the HighFidelity tier (192 taps at the SSG
    // rate, 384 at the FM rate, beta 9) and keep it across a live rate switch;
    // the FM decimators stay slaves of chip-0 SSG left
    for (const size_t rate : {size_t(44100), size_t(96000)})
    {
        SCOPED_TRACE(testing::Message() << "rate " << rate);
        FilterDecimator ssgRef;
        FilterDecimator fmRef;
        ssgRef.configure(double(rate), FilterDecimator::Quality::HighFidelity);
        fmRef.configure(double(rate), FilterDecimator::Quality::HighFidelity, false, 2.0 * FilterDecimator::INPUT_RATE);

        auto fm = std::make_unique<SoundChip_TurboSoundFM>(_context);
        fm->setDecimatorQuality(FilterDecimator::Quality::HighFidelity);
        fm->setCoreRate(44100);
        fm->setCoreRate(rate);  // a live switch keeps the tier
        auto legacy = std::make_unique<SoundChip_TurboSound>(_context);
        legacy->setDecimatorQuality(FilterDecimator::Quality::HighFidelity);
        legacy->setCoreRate(rate);
        for (int chip = 0; chip < 2; chip++)
        {
            for (ITurboSoundDevice* device : {static_cast<ITurboSoundDevice*>(fm.get()),
                                              static_cast<ITurboSoundDevice*>(legacy.get())})
            {
                EXPECT_EQ(device->getChip(chip)->decimatorLeft().taps(), 192u);
                EXPECT_EQ(device->getChip(chip)->decimatorRight().coefficients(), ssgRef.coefficients());
            }
            EXPECT_EQ(fm->outputState(chip)->decimator.taps(), 384u);
            EXPECT_EQ(fm->outputState(chip)->decimator.coefficients(), fmRef.coefficients());
        }

        // Slave wiring intact: an HQ render still fills the FM buffer
        fm->setHQEnabled(true);
        ProgramFmNote(*fm, FmNoteProgram{});
        const int16_t peak = RunFmNotePeak(*fm, 12, 6);
        EXPECT_GE(peak, int16_t(kFmOneCarrierPeak * 0.95)) << "HighFidelity FM render lost level: " << peak;
        EXPECT_LE(peak, int16_t(kFmOneCarrierPeak * 1.05)) << "HighFidelity FM render gained level: " << peak;
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
    EXPECT_GE(peak441, int16_t(kFmOneCarrierPeak * 0.95)) << "FM too quiet at 44.1 k: " << peak441;
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
        EXPECT_GE(peak, int16_t(kFmOneCarrierPeak * 0.95)) << "FM too quiet after switch: " << peak;
        EXPECT_LE(peak, int16_t(kFmOneCarrierPeak * 1.05)) << "FM too loud after switch: " << peak;
        const size_t samples = device->getRenderedSamplesThisFrame();
        EXPECT_NEAR(double(samples), double(samples441) * double(rate) / 44100.0, 2.0)
            << "frame sample count does not follow the new rate";
    }
}

/// endregion
