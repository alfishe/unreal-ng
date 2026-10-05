// MultiSoundMixer (core/src/emulator/slots/cards/multisound/multisoundmixer): every source's weight per side against
// the schematic table (hardware-reference.md §4.5, three decimals), unit inputs one source at a time; the calibration
// that ties the DAC level to the FM level through volts; the coupling high-pass and the Authentic SAA ladder.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "emulator/slots/cards/multisound/multisoundmixer.h"

namespace
{
constexpr uint32_t kRate = 44100;
constexpr double kPi = 3.14159265358979323846;

struct Rows
{
    explicit Rows(size_t frames)
        : fm(frames * 2), fm2(frames * 2), ssg(frames * 2), ssg2(frames * 2), saa(frames * 2), dac(frames * 2),
          midi(frames * 2), external(frames * 2)
    {
    }

    MultiSoundMixerOutput Output()
    {
        MultiSoundMixerOutput out;
        out.fm[0] = fm.data();
        out.fm[1] = fm2.data();
        out.ssg[0] = ssg.data();
        out.ssg[1] = ssg2.data();
        out.saa = saa.data();
        out.dac = dac.data();
        out.midi = midi.data();
        out.external = external.data();
        return out;
    }

    /// Every row except `except` is silent at frame i
    bool OthersSilent(const std::vector<int16_t>* except, size_t i) const
    {
        for (const std::vector<int16_t>* row : { &fm, &fm2, &ssg, &ssg2, &saa, &dac, &midi, &external })
            if (row != except && ((*row)[i * 2] != 0 || (*row)[i * 2 + 1] != 0))
                return false;
        return true;
    }

    /// The per-chip rows of chip select `chip`
    std::vector<int16_t>& Fm(int chip) { return chip == 0 ? fm : fm2; }
    std::vector<int16_t>& Ssg(int chip) { return chip == 0 ? ssg : ssg2; }

    std::vector<int16_t> fm, fm2, ssg, ssg2, saa, dac, midi, external;   // fm, ssg: chip 0
};

MultiSoundMixer MakeMixer(bool acCoupling = false, MultiSoundRenderMode mode = MultiSoundRenderMode::HiFi)
{
    MultiSoundMixer mixer;
    MultiSoundMixerConfig cfg;
    cfg.outputRate = kRate;
    cfg.renderMode = mode;
    cfg.acCoupling = acCoupling;
    mixer.Configure(cfg);
    return mixer;
}

/// Row value as a fraction of the source's own level (row / (input level x 32767))
double Gain(int16_t row, double inputLevel)
{
    return row / (inputLevel * 32767.0);
}
} // namespace

TEST(MultiSoundMixer_Test, FmEachChipOnItsOwnRowCentreAtUnity)
{
    for (int chip = 0; chip < 2; chip++)
    {
        MultiSoundMixer mixer = MakeMixer();
        const float one = 1.0f;
        MultiSoundMixerInput in;
        in.frames = 1;
        in.fm[chip] = &one;
        Rows rows(1);
        mixer.Mix(in, rows.Output());
        EXPECT_NEAR(Gain(rows.Fm(chip)[0], MultiSoundMixer::kFmFullScale), 1.000, 0.0005) << chip;
        EXPECT_NEAR(Gain(rows.Fm(chip)[1], MultiSoundMixer::kFmFullScale), 1.000, 0.0005) << chip;
        EXPECT_TRUE(rows.OthersSilent(&rows.Fm(chip), 0)) << "the other chip's row stays silent";
    }
}

TEST(MultiSoundMixer_Test, SsgAbcWeightsEachChipOnItsOwnRow)
{
    // A 0.417 L, B 0.213 L + R, C 0.417 R, for either chip
    const double expected[3][2] = { { 0.417, 0.0 }, { 0.213, 0.213 }, { 0.0, 0.417 } };
    for (int chip = 0; chip < 2; chip++)
    {
        for (int channel = 0; channel < 3; channel++)
        {
            MultiSoundMixer mixer = MakeMixer();
            const float one = 1.0f;
            MultiSoundMixerInput in;
            in.frames = 1;
            in.ssg[chip][channel] = &one;
            Rows rows(1);
            mixer.Mix(in, rows.Output());
            const double level = MultiSoundMixer::kSsgChannelFullScale;
            EXPECT_NEAR(Gain(rows.Ssg(chip)[0], level), expected[channel][0], 0.0015) << chip << channel;
            EXPECT_NEAR(Gain(rows.Ssg(chip)[1], level), expected[channel][1], 0.0015) << chip << channel;
            EXPECT_TRUE(rows.OthersSilent(&rows.Ssg(chip), 0));
        }
    }
}

TEST(MultiSoundMixer_Test, StereoSourcesKeepTheirSide)
{
    // SAA 0.833 (pass band), MIDI 1.0, DAC 0.208, external 0.417: L into L only, R into R only
    for (size_t side = 0; side < 2; side++)
    {
        MultiSoundMixer mixer = MakeMixer();
        int16_t saa[2] = {};
        float midi[2] = {};
        int16_t dac[2] = {};
        float external[2] = {};
        saa[side] = 4800;           // one SAA voice at full
        midi[side] = 0.5f;
        dac[side] = 127 * 64;       // one DAC channel at its maximum
        external[side] = 1.0f;      // 1 V on J3
        MultiSoundMixerInput in;
        in.frames = 1;
        in.saa = saa;
        in.midi = midi;
        in.dac = dac;
        in.external = external;
        Rows rows(1);
        mixer.Mix(in, rows.Output());

        const size_t other = 1 - side;
        EXPECT_NEAR(Gain(rows.saa[side], 4800.0 * MultiSoundMixer::kSaaUnit), 10.0 / 12.0, 0.001);
        EXPECT_NEAR(Gain(rows.midi[side], 0.5 * MultiSoundMixer::kMidiFullScale), 1.000, 0.0005);
        EXPECT_NEAR(Gain(rows.dac[side], 127 * 64 * MultiSoundMixer::kDacUnit), 0.208, 0.0015);
        EXPECT_NEAR(Gain(rows.external[side], MultiSoundMixer::kLevelPerVolt), 0.417, 0.0015);
        EXPECT_EQ(rows.saa[other], 0);
        EXPECT_EQ(rows.midi[other], 0);
        EXPECT_EQ(rows.dac[other], 0);
        EXPECT_EQ(rows.external[other], 0);
        EXPECT_EQ(rows.fm[0], 0);
        EXPECT_EQ(rows.ssg[0], 0);
    }
}

TEST(MultiSoundMixer_Test, DacLevelAgainstFmThroughVolts)
{
    // A DAC channel swings its pin by +-2.5 V at full scale into 0.208; FM's YM3014B swings +-1.25 V into 1.0.
    // Full-scale DAC channel / full-scale FM = 2.5 x 0.2083 / 1.25 = 0.4167
    MultiSoundMixer mixer = MakeMixer();
    const float one = 1.0f;
    int16_t dac[2] = { 8192, 0 };       // kChannelFullScale
    MultiSoundMixerInput in;
    in.frames = 1;
    in.fm[0] = &one;
    in.dac = dac;
    Rows rows(1);
    mixer.Mix(in, rows.Output());
    EXPECT_NEAR(static_cast<double>(rows.dac[0]) / rows.fm[0], 2.5 * (10.0 / 48.0) / 1.25, 0.001);
    // SSG channel A at full (peak to peak) against an FM full-scale word: the TSFM board's measured balance x the
    // MultiSound's weights (0.417 vs 1.0)
    EXPECT_NEAR(MultiSoundMixer::kSsgChannelFullScale / MultiSoundMixer::kFmFullScale, 0.30 / 0.70327, 1e-6);
    EXPECT_NEAR(MultiSoundMixer::kFmFullScale, 0.30 * std::pow(10.0, 7.4 / 20.0), 0.0001);
}

TEST(MultiSoundMixer_Test, NullSourcesAreSilentAndNullRowsSkipped)
{
    MultiSoundMixer mixer = MakeMixer(true);
    MultiSoundMixerInput in;
    in.frames = 16;
    Rows rows(16);
    std::fill(rows.fm.begin(), rows.fm.end(), int16_t{ 123 });
    mixer.Mix(in, rows.Output());
    for (size_t i = 0; i < 16; i++)
        EXPECT_TRUE(rows.OthersSilent(nullptr, i)) << i;

    MultiSoundMixerOutput none;     // every row skipped: nothing written, nothing crashes
    mixer.Mix(in, none);
}

TEST(MultiSoundMixer_Test, AcCouplingRemovesTheSsgDc)
{
    // SSG A held at full volume with its tone off: a DC level, which the 10 uF / 24k coupling takes away (0.66 Hz)
    MultiSoundMixer mixer = MakeMixer(true);
    constexpr size_t frames = kRate * 2;
    const std::vector<float> level(frames, 1.0f);
    MultiSoundMixerInput in;
    in.frames = frames;
    in.ssg[0][0] = level.data();
    Rows rows(frames);
    mixer.Mix(in, rows.Output());
    const double first = rows.ssg[0];
    EXPECT_NEAR(Gain(rows.ssg[0], MultiSoundMixer::kSsgChannelFullScale), 0.417, 0.0015);   // the edge passes
    EXPECT_LT(std::abs(rows.ssg[(frames - 1) * 2]), first * 0.0005 + 1);                    // e^-8 after 2 s
    // One time constant (1 / (2 pi 0.663 Hz) = 0.24 s): e^-1 of the step
    const size_t tau = static_cast<size_t>(kRate * 24e3 * 10e-6);
    EXPECT_NEAR(rows.ssg[tau * 2] / first, std::exp(-1.0), 0.01);
}

TEST(MultiSoundMixer_Test, AuthenticSaaLadder)
{
    // A sine on SAA L at 1 kHz and at the ladder's corner: the 1 kHz gain is the reference's 0.825, the corner is
    // -3 dB below the pass band (0.833); HiFi leaves both at 0.833
    for (const double hz : { 1000.0, MultiSoundBoard::SaaCornerHz() })
    {
        for (const MultiSoundRenderMode mode : { MultiSoundRenderMode::HiFi, MultiSoundRenderMode::Authentic })
        {
            MultiSoundMixer mixer = MakeMixer(false, mode);
            constexpr size_t frames = kRate / 10;
            std::vector<int16_t> saa(frames * 2);
            for (size_t i = 0; i < frames; i++)
                saa[i * 2] = static_cast<int16_t>(std::lround(16000.0 * std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate)));
            MultiSoundMixerInput in;
            in.frames = frames;
            in.saa = saa.data();
            Rows rows(frames);
            mixer.Mix(in, rows.Output());
            int peak = 0;
            for (size_t i = frames / 2; i < frames; i++)
                peak = std::max(peak, std::abs(static_cast<int>(rows.saa[i * 2])));
            const double gain = peak / (16000.0 * MultiSoundMixer::kSaaUnit * 32767.0);
            double expected = 10.0 / 12.0;
            if (mode == MultiSoundRenderMode::Authentic)
                expected = hz == 1000.0 ? 0.825 : (10.0 / 12.0) / std::sqrt(2.0);
            EXPECT_NEAR(gain, expected, 0.004) << hz << " Hz, mode " << static_cast<int>(mode);
        }
    }
}

/// The FM trim: the TurboSound FM's gain law, 0.30 x 10^(trim / 20), live; 7.4 dB (the shipped calibration) is the
/// constructed default and kFmFullScale. The SSG and every other source do not move with it
TEST(MultiSoundMixer_Test, FmTrimFollowsTheTsfmGainLaw)
{
    MultiSoundMixer mixer = MakeMixer();
    EXPECT_DOUBLE_EQ(mixer.FmTrimDb(), kMultiSoundDefaultFmTrimDb);
    EXPECT_NEAR(MultiSoundMixer::FmFullScale(kMultiSoundDefaultFmTrimDb), MultiSoundMixer::kFmFullScale, 1e-5);
    for (const double trim : {7.4, 0.0, -6.0, 3.5})
    {
        mixer.SetFmTrimDb(trim);
        const float one = 1.0f;
        MultiSoundMixerInput in;
        in.frames = 1;
        in.fm[0] = &one;
        in.ssg[1][0] = &one;
        Rows rows(1);
        mixer.Mix(in, rows.Output());
        EXPECT_NEAR(rows.fm[0] / 32767.0, 0.30 * std::pow(10.0, trim / 20.0), 1.0 / 32767.0) << trim;
        EXPECT_NEAR(Gain(rows.ssg2[0], MultiSoundMixer::kSsgChannelFullScale), 0.417, 0.0015) << trim;
    }
}
