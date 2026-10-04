// libsam2695 tests - the voice model: envelopes, LFOs, filter, interpolation, loops, pitch, modulators.
#include "common/conv.h"
#include "synthcore.h"
#include "synthhelper.h"
#include "testfw.h"
#include "voice/envelope.h"
#include "voice/filter.h"
#include "voice/lfo.h"

#include <cmath>
#include <algorithm>
#include <complex>

using namespace sam2695;
using namespace sam2695test;

namespace
{

constexpr double kPi = 3.14159265358979323846;

// Attenuation of a 7-bit "negative unipolar concave" default modulator at value v, in centibels
double ConcaveCb(int v)
{
    return 960.0 * ConcaveCurve(1.0 - v / 128.0);
}

// Least-squares fit of a sine of known frequency; returns residual RMS / fitted amplitude RMS
double SineResidual(const TestSynth& ts, size_t from, size_t to, double hz)
{
    to = std::min(to, ts.Frames());
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
    for (size_t i = from; i < to; i++)
    {
        const double w = 2.0 * kPi * hz * static_cast<double>(i) / kInternalRate;
        const double s = std::sin(w), c = std::cos(w), y = ts.L(i);
        ss += s * s;
        sc += s * c;
        cc += c * c;
        ys += y * s;
        yc += y * c;
    }
    const double det = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double err = 0, sig = 0;
    for (size_t i = from; i < to; i++)
    {
        const double w = 2.0 * kPi * hz * static_cast<double>(i) / kInternalRate;
        const double fit = a * std::sin(w) + b * std::cos(w);
        err += (ts.L(i) - fit) * (ts.L(i) - fit);
        sig += fit * fit;
    }
    return std::sqrt(err / sig);
}

std::shared_ptr<const Sf2Bank> OnePreset(BSample s, std::map<int, int> gens)
{
    Sf2Builder b;
    b.AddSample(s);
    b.presets.push_back(SimplePreset("P", 0, 0, 0, std::move(gens)));
    return b.Load();
}

} // namespace

TEST(Voice, EnvelopeTimecents)
{
    // stage boundaries in whole samples, levels exact to SF2 2.04 (attack linear in amplitude, decay /
    // release 100 dB per time constant)
    CHECK_NEAR(TimecentsToSeconds(-1200), 0.5, 1e-12);
    CHECK_NEAR(TimecentsToSeconds(0), 1.0, 1e-12);
    CHECK_EQ_I(SecondsToSamples(TimecentsToSeconds(-12000), kInternalRate), 37); // 0.977 ms
    EnvTimes t;
    t.delay = 100;
    t.attack = 200;
    t.hold = 50;
    t.decay = 1000;
    t.sustain = 300.0f;
    t.release = 2000;
    Envelope e;
    e.Start(t, true);
    e.Advance(50);
    CHECK(e.stage == EnvStage::Delay);
    CHECK_EQ_I(e.Gain(), 0);
    e.Advance(150);
    CHECK(e.stage == EnvStage::Attack);
    CHECK_NEAR(e.Gain(), 0.5, 1e-7);
    e.Advance(100);
    CHECK(e.stage == EnvStage::Hold);
    CHECK_NEAR(e.Gain(), 1.0, 0);
    e.Advance(50);
    CHECK(e.stage == EnvStage::Decay);
    e.Advance(100);
    CHECK_NEAR(e.Level(), 100.0, 1e-4);
    CHECK_NEAR(e.Gain(), std::pow(10.0, -0.5), 1e-6);
    e.Advance(199);
    CHECK(e.stage == EnvStage::Decay);
    e.Advance(1);
    CHECK(e.stage == EnvStage::Sustain);
    CHECK_NEAR(e.Level(), 300.0, 0);
    e.Advance(100000);
    CHECK(e.stage == EnvStage::Sustain);
    e.Release();
    e.Advance(1000);
    CHECK_NEAR(e.Level(), 800.0, 1e-3);
    e.Advance(399);
    CHECK(!e.Done());
    e.Advance(1);
    CHECK(e.Done());

    // release from inside the attack starts at the attack's level in centibels
    Envelope a;
    a.Start(t, true);
    a.Advance(200); // attack half way
    a.Release();
    CHECK_NEAR(a.Level(), -200.0 * std::log10(0.5), 1e-3);
    // release inside the delay: nothing has sounded, the voice ends at once
    Envelope d;
    d.Start(t, true);
    d.Advance(10);
    d.Release();
    CHECK(d.Done());

    // through the synth: attack -1200 tc (0.5 s = 18750 samples) on a DC sample
    TestSynth ts(OnePreset(DcSample("dc"), {{G(Gen::SampleModes), 1}, {G(Gen::AttackVolEnv), -1200}}));
    ts.Send(0, {0xB0, 7, 127, 0x90, 60, 127});
    ts.RunTo(18750 + 64);
    const double full = 0.5 * std::cos(kPi / 4) * std::pow(10.0, -3.0 * ConcaveCb(127) / 200.0);
    CHECK_NEAR(ts.L(9375), full * 0.5, full * 0.002);
    CHECK_NEAR(ts.L(18750 + 32), full, full * 0.002);
}

TEST(Voice, ModEnvelope)
{
    EnvTimes t;
    t.attack = 10;
    t.decay = 1000;
    t.sustain = 0.6f; // generator 400 (40 % down)
    t.release = 500;
    Envelope e;
    e.Start(t, false);
    e.Advance(5);
    CHECK_NEAR(e.Level(), ConvexCurve(0.5), 1e-7); // the attack follows the SF2 convex curve
    CHECK(e.Level() > 0.8f);
    e.Advance(5 + 200);
    CHECK_NEAR(e.Level(), 0.8, 1e-6);
    e.Advance(200);
    CHECK(e.stage == EnvStage::Sustain);
    CHECK_NEAR(e.Level(), 0.6, 1e-7);
    e.Release();
    e.Advance(100);
    CHECK_NEAR(e.Level(), 0.4, 1e-6);
    e.Advance(200);
    CHECK(e.Done());
}

TEST(Voice, Lfo)
{
    Lfo l;
    l.Start(10, static_cast<uint32_t>(4294967296.0 / 100.0)); // 100-sample period after a 10-sample delay
    l.Advance(9);
    CHECK_EQ_I(l.Value(), 0);
    l.Advance(1 + 25);
    CHECK_NEAR(l.Value(), 1.0, 1e-5);
    l.Advance(50);
    CHECK_NEAR(l.Value(), -1.0, 1e-5);
    l.Advance(25);
    CHECK_NEAR(l.Value(), 0.0, 1e-5);
}

TEST(Voice, FilterResponse)
{
    auto magnitude = [](const BiquadCoeffs& c, double hz) {
        const std::complex<double> z = std::polar(1.0, -2.0 * kPi * hz / kInternalRate);
        const std::complex<double> num = static_cast<double>(c.b0) + static_cast<double>(c.b1) * z + static_cast<double>(c.b2) * z * z;
        const std::complex<double> den = 1.0 + static_cast<double>(c.a1) * z + static_cast<double>(c.a2) * z * z;
        return std::abs(num / den);
    };
    bool bypass = true;
    BiquadCoeffs c = LowPassCoeffs(1000.0, 0.0, kInternalRate, bypass);
    CHECK(!bypass);
    CHECK_NEAR(magnitude(c, 0.0), 1.0, 1e-5);
    CHECK_NEAR(magnitude(c, 1000.0), std::sqrt(0.5), 1e-3); // Butterworth: -3 dB at the cutoff
    CHECK(magnitude(c, 8000.0) < 0.02);
    // Q = 120 cB: peak q = 10^((12 - 3.01) / 20) above a DC gain lowered to 1 / sqrt(q)
    c = LowPassCoeffs(1000.0, 120.0, kInternalRate, bypass);
    const double q = std::pow(10.0, (12.0 - 3.01) / 20.0);
    CHECK_NEAR(magnitude(c, 0.0), 1.0 / std::sqrt(q), 1e-4);
    CHECK_NEAR(magnitude(c, 1000.0) / magnitude(c, 0.0), q, 1e-3);
    // default cutoff 13500 cents (19.9 kHz) with Q = 0: bypassed
    LowPassCoeffs(AbsCentsToHz(13500), 0.0, kInternalRate, bypass);
    CHECK(bypass);

    // through the synth: a 2-pole low-pass at 500 Hz takes a 375 Hz sine down by its response
    auto play = [](int fc) {
        TestSynth ts(OnePreset(SineSample("sine"), {{G(Gen::SampleModes), 1}, {G(Gen::InitialFilterFc), fc}}));
        ts.Send(0, {0x90, 69, 127});
        ts.RunTo(4000);
        return ts.RmsL(2000, 4000);
    };
    const double open = play(13500);
    const double closed = play(static_cast<int>(std::lround(1200.0 * std::log2(500.0 / 8.17579891564))));
    const double velocityCut = -2400.0 * (1.0 - 127.0 / 128.0); // default velocity-to-cutoff modulator
    bool by = false;
    const BiquadCoeffs expect = LowPassCoeffs(500.0 * std::exp2(velocityCut / 1200.0), 0.0, kInternalRate, by);
    CHECK_NEAR(closed / open, magnitude(expect, 375.0), 0.01);
}

TEST(Voice, Interpolation)
{
    // a 4.69 kHz loop (8 frames per cycle) played a fifth up, 7.02 kHz = 0.19 of the internal rate:
    // where interpolation matters, sinc is cleaner than cubic, cubic than linear. A 375 Hz tone is
    // clean in every mode.
    const Interpolation modes[3] = {Interpolation::Linear, Interpolation::Cubic, Interpolation::Sinc};
    double high[3] = {}, low[3] = {};
    for (int m = 0; m < 3; m++)
    {
        SynthConfig cfg;
        cfg.interpolation = modes[m];
        TestSynth hi(OnePreset(SineSample("sine8", 8, 100), {{G(Gen::SampleModes), 1}}), cfg);
        hi.Send(0, {0x90, 76, 127});
        hi.RunTo(6000);
        high[m] = SineResidual(hi, 2000, 6000, 37500.0 / 8.0 * std::exp2(7.0 / 12.0));
        TestSynth lo(OnePreset(SineSample("sine"), {{G(Gen::SampleModes), 1}}), cfg);
        lo.Send(0, {0x90, 76, 127});
        lo.RunTo(6000);
        low[m] = SineResidual(lo, 2000, 6000, 375.0 * std::exp2(7.0 / 12.0));
    }
    CHECK(high[2] < high[1]);
    CHECK(high[1] < high[0]);
    CHECK(high[2] < 3e-3);
    for (double r : low)
        CHECK(r < 3e-4);
}

TEST(Voice, LoopModes)
{
    // 600 frames: +level for 0..299 (loop 100..299), -level for 300..599 (the tail after the loop)
    BSample s;
    s.name = "loop";
    for (int i = 0; i < 600; i++)
        s.frames.push_back(i < 300 ? 16000 : -16000);
    s.loopStart = 100;
    s.loopEnd = 300;
    auto play = [&s](int mode) {
        auto ts = std::make_unique<TestSynth>(
            OnePreset(s, {{G(Gen::SampleModes), mode}, {G(Gen::AttackVolEnv), -32768}, {G(Gen::ReleaseVolEnv), 0}}));
        ts->Send(0, {0x90, 69, 127});
        ts->Send(2000, {0x80, 69, 0});
        ts->RunTo(4000);
        return ts;
    };
    auto none = play(0);
    CHECK(none->PeakL(700, 2000) == 0.0);        // played once to its end, then silent
    CHECK(none->L(100) > 0.0f && none->L(400) < 0.0f);
    auto loop = play(1);
    CHECK(loop->L(1500) > 0.0f);                 // still in the loop
    CHECK(loop->L(3000) > 0.0f);                 // the release keeps looping, never reaches the tail
    double minAfter = 0.0;
    for (size_t i = 2000; i < 4000; i++)
        minAfter = std::fmin(minAfter, loop->L(i));
    CHECK(minAfter >= 0.0);
    auto untilRelease = play(3);
    CHECK(untilRelease->L(1500) > 0.0f);
    double minRel = 0.0;
    for (size_t i = 2000; i < 2700; i++)
        minRel = std::fmin(minRel, untilRelease->L(i));
    CHECK(minRel < 0.0);                         // after the release it plays on into the tail
    CHECK(untilRelease->PeakL(2800, 4000) == 0.0);
}

TEST(Voice, PhaseFixedPoint)
{
    // key 70 on a sample recorded at the internal rate: increment = 2^(1/12) in 32.32; after 32 000
    // samples (37 of them the default 1 ms delay, which holds the sample) the phase is exactly 31 963
    // increments (modulo the loop): no accumulated rounding
    std::shared_ptr<const Sf2Bank> bank = OnePreset(SineSample("sine"), {{G(Gen::SampleModes), 1}});
    SynthConfig cfg;
    SynthCore core;
    core.Configure(cfg);
    core.SetBank(bank.get());
    core.PowerOn();
    MidiMessage on;
    on.status = 0x90;
    on.data1 = 70;
    on.data2 = 100;
    core.BeginBlock();
    core.Message(on, 0);
    float l[kControlBlock], r[kControlBlock];
    core.RenderSegment(0, kControlBlock, l, r);
    for (int b = 1; b < 1000; b++)
    {
        core.BeginBlock();
        core.RenderSegment(0, kControlBlock, l, r);
    }
    const Voice& v = core.VoiceAt(0);
    CHECK(v.state == VoiceState::On);
    const uint64_t expectedInc = static_cast<uint64_t>(std::exp2(1.0 / 12.0) * 4294967296.0);
    CHECK_EQ_I(v.increment, expectedInc);
    const uint64_t loopStartP = static_cast<uint64_t>(v.loopStart) << 32;
    const uint64_t loopEndP = static_cast<uint64_t>(v.loopEnd) << 32;
    uint64_t expected = (static_cast<uint64_t>(v.start) << 32) + expectedInc * (32000u - 37u);
    if (expected >= loopEndP)
        expected = loopStartP + (expected - loopStartP) % (loopEndP - loopStartP);
    CHECK_EQ_I(v.phase, expected);
}

TEST(Voice, PitchAndBend)
{
    std::shared_ptr<const ISoundBank> bank = OnePreset(SineSample("sine"), {{G(Gen::SampleModes), 1}});
    auto freq = [&bank](std::initializer_list<int> setup, int key) {
        TestSynth ts(bank);
        ts.Send(0, setup);
        ts.Send(0, {0x90, key, 100});
        ts.RunTo(12000);
        return ts.FrequencyL(1000, 12000);
    };
    CHECK_NEAR(freq({}, 69), 375.0, 0.01);
    CHECK_NEAR(freq({}, 81), 750.0, 0.02);
    // full bend up, default range 2 semitones ("+-1 tone", datasheet p.26)
    CHECK_NEAR(freq({0xE0, 0x7F, 0x7F}, 69), 375.0 * std::exp2(2.0 / 12.0 * 8191.0 / 8192.0), 0.02);
    CHECK_NEAR(freq({0xE0, 0x00, 0x00}, 69), 375.0 * std::exp2(-2.0 / 12.0), 0.02);
    // RPN 0 = 12 semitones
    CHECK_NEAR(freq({0xB0, 101, 0, 0xB0, 100, 0, 0xB0, 6, 12, 0xE0, 0x7F, 0x7F}, 69),
               375.0 * std::exp2(8191.0 / 8192.0), 0.05);
}

TEST(Voice, VelocityCurve)
{
    // amplitude follows the SF2 concave curve of the default velocity modulator (960 cB)
    std::shared_ptr<const ISoundBank> bank = OnePreset(DcSample("dc"), {{G(Gen::SampleModes), 1}, {G(Gen::AttackVolEnv), -32768}});
    auto level = [&bank](int vel) {
        TestSynth ts(bank);
        ts.Send(0, {0x90, 60, vel});
        ts.RunTo(640);
        return ts.L(600);
    };
    const double l127 = level(127), l64 = level(64), l20 = level(20);
    CHECK_NEAR(l64 / l127, std::pow(10.0, -(ConcaveCb(64) - ConcaveCb(127)) / 200.0), 1e-4);
    CHECK_NEAR(l20 / l127, std::pow(10.0, -(ConcaveCb(20) - ConcaveCb(127)) / 200.0), 1e-4);
    // = 40 dB per decade of velocity, the curve FluidSynth measures (README "FluidSynth comparison")
    CHECK_NEAR(20.0 * std::log10(l64 / l127), 40.0 * std::log10(64.0 / 127.0), 0.15);
}

TEST(Voice, StaticAttenuation)
{
    auto level = [](int att) {
        TestSynth ts(OnePreset(DcSample("dc"), {{G(Gen::SampleModes), 1},
                                                {G(Gen::AttackVolEnv), -32768},
                                                {G(Gen::InitialAttenuation), att}}));
        ts.Send(0, {0x90, 60, 127});
        ts.RunTo(640);
        return ts.L(600);
    };
    // the bank's 100 cB counts as 40 cB (E-mu scaling, kStaticAttenuationScale)
    CHECK_NEAR(level(100) / level(0), std::pow(10.0, -40.0 / 200.0), 1e-5);
}

TEST(Voice, Pan)
{
    auto play = [](int pan, std::initializer_list<int> setup) {
        auto ts = std::make_unique<TestSynth>(OnePreset(
            DcSample("dc"), {{G(Gen::SampleModes), 1}, {G(Gen::AttackVolEnv), -32768}, {G(Gen::Pan), pan}}));
        ts->Send(0, setup);
        ts->Send(0, {0x90, 60, 127});
        ts->RunTo(640);
        return ts;
    };
    auto left = play(-500, {});
    CHECK(left->L(600) > 0.0f);
    CHECK_EQ_I(left->R(600), 0);
    auto center = play(0, {});
    CHECK_NEAR(center->L(600), center->R(600), 1e-7);
    auto quarter = play(-250, {}); // constant-power sin / cos law
    CHECK_NEAR(quarter->R(600) / quarter->L(600), std::tan(kPi / 8), 1e-5);
    auto cc = play(0, {0xB0, 10, 0}); // CC 10 = 0: hard left through the default modulator
    CHECK_EQ_I(cc->R(600), 0);
}
