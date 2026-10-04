// libsam2695 tests - the effects block (SAM-4): reverb and chorus programs and parameters, the spatial
// effect, the equalizer, post-effects routing, sends and their scaling, the output stage (clipping,
// codec gain), the effects word as the effects' switch, and the effects' state in the blob.
//
// The wet signal of an effect is taken as the difference of two renders that differ only in the send
// (the chip path is linear below the soft-clip knee), so each test sees exactly what the effect adds.
#include "fx/chorus.h"
#include "fx/effects.h"
#include "synthhelper.h"
#include "testfw.h"

#include <cmath>
#include <functional>

using namespace sam2695;
using namespace sam2695test;

namespace
{

SynthReport Report(const TestSynth& ts)
{
    SynthReport r;
    ts.synth.Describe(r);
    return r;
}

std::vector<uint8_t> Save(const Synth& s)
{
    std::vector<uint8_t> blob(s.StateSize());
    s.SaveState(blob.data());
    return blob;
}

using Setup = std::function<void(TestSynth&)>;

// A chip-mode render: the effects word first, then `setup`, then the notes; up to `until`
std::unique_ptr<TestSynth> Chip(const std::shared_ptr<const ISoundBank>& bank, int word, const Setup& setup,
                                uint64_t until)
{
    auto ts = std::make_unique<TestSynth>(bank, SynthConfig{}, true);
    ts->Nrpn(0, 0, 0x37, 0x5F, word);
    setup(*ts);
    ts->RunTo(until);
    return ts;
}

struct Wet
{
    std::vector<float> l, r;
    // frequency of the left channel from rising zero crossings over [a, b)
    double Frequency(size_t a, size_t b) const
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (size_t i = a + 1; i < b && i < l.size(); i++)
            if (l[i - 1] < 0.0f && l[i] >= 0.0f)
            {
                const double t = static_cast<double>(i - 1) + l[i - 1] / static_cast<double>(l[i - 1] - l[i]);
                if (first < 0.0)
                    first = t;
                else
                    crossings++;
                last = t;
            }
        return crossings > 0 ? crossings * static_cast<double>(kInternalRate) / (last - first) : 0.0;
    }
    double Rms(size_t a, size_t b, bool right = false) const
    {
        const std::vector<float>& x = right ? r : l;
        double s = 0;
        size_t n = 0;
        for (size_t i = a; i < b && i < x.size(); i++, n++)
            s += static_cast<double>(x[i]) * x[i];
        return n ? std::sqrt(s / n) : 0.0;
    }
};

// What the effect adds: render with the send `cc` at `value` minus the render with it at 0
Wet WetOf(const std::shared_ptr<const ISoundBank>& bank, int word, int cc, int value, const Setup& setup,
          std::initializer_list<int> note, uint64_t off, uint64_t until)
{
    // the note is the last three bytes of `note`; its channel takes the send and the note off
    const int status = *(note.begin() + (note.size() - 3)), key = *(note.begin() + (note.size() - 2));
    const int channel = status & 0x0F;
    std::unique_ptr<TestSynth> run[2];
    for (int k = 0; k < 2; k++)
        run[k] = Chip(bank, word,
                      [&](TestSynth& ts) {
                          setup(ts);
                          ts.Send(0, {0xB0 | channel, cc, k == 0 ? value : 0});
                          ts.Send(0, note);
                          ts.Send(off, {0x80 | channel, key, 0});
                      },
                      until);
    Wet w;
    for (size_t i = 0; i < run[0]->Frames(); i++)
    {
        w.l.push_back(run[0]->L(i) - run[1]->L(i));
        w.r.push_back(run[0]->R(i) - run[1]->R(i));
    }
    return w;
}

// Decay slope of a tail in dB per second: RMS in 20 ms windows over [a, b), least squares
double SlopeDbPerSecond(const TestSynth& ts, size_t a, size_t b)
{
    const size_t win = 750;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (size_t s = a; s + win <= b; s += win)
    {
        const double rms = std::sqrt(0.5 * (ts.RmsL(s, s + win) * ts.RmsL(s, s + win) + ts.RmsR(s, s + win) * ts.RmsR(s, s + win)));
        const double x = static_cast<double>(s) / kInternalRate, y = 20.0 * std::log10(rms + 1e-30);
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
        n++;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

const Setup kNone = [](TestSynth&) {};

} // namespace

// ---- reverb ----

TEST(Fx, ReverbTailAcrossState)
{
    // save in the middle of a reverb + chorus tail, load into a fresh chip: identical continuation
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto start = [](TestSynth& ts) {
        ts.Send(0, {0xB0, 91, 127, 0xB0, 93, 127, 0x90, 69, 100});
        ts.Send(7500, {0x80, 69, 0});
    };
    auto a = Chip(bank, kPowerUpEffectsWord, start, 15000);
    const std::vector<uint8_t> blob = Save(a->synth);
    a->RunTo(60000);
    TestSynth b(bank, SynthConfig{}, true);
    CHECK(b.synth.LoadState(blob.data(), blob.size()));
    const size_t from = b.synth.InternalPosition();
    CHECK_EQ_I(from, 15000 / kControlBlock * kControlBlock);
    b.RunTo(60000);
    bool same = b.Frames() == 60000 / kControlBlock * kControlBlock - from;
    for (size_t i = 0; i < b.Frames() && same; i++)
        same = a->out[(from + i) * 2] == b.out[i * 2] && a->out[(from + i) * 2 + 1] == b.out[i * 2 + 1];
    CHECK(same);
    CHECK(a->RmsL(40000, 60000) > 1e-4); // the tail is still there, long after the note
    CHECK(Save(a->synth) == Save(b.synth));
}

TEST(Fx, ReverbPrograms)
{
    // programs 0-5 (CC 80): the decay time Describe() states, measured on the tail of a 0.1 s noise burst
    // (a broadband input: a pure tone would excite single tank modes); room1 < room2 < room3 < hall1 <
    // hall2. Renders 6 x 0.7 s through the tank: about 10 ms.
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    double previous = 0.0;
    for (int p = 0; p < 6; p++)
    {
        auto ts = Chip(bank, 0x20,
                       [p](TestSynth& t) {
                           t.Send(0, {0xB0, 80, p, 0xB0, 91, 127, 0xC0, 3, 0x90, 69, 127});
                           t.Send(3750, {0x80, 69, 0});
                       },
                       3750 + 22500);
        const double expected = Report(*ts).reverbDecaySeconds;
        const double measured = -60.0 / SlopeDbPerSecond(*ts, 3750 + 3750, 3750 + 22500);
        CHECK_NEAR(measured / expected, 1.0, 0.25);
        if (p < 5)
        {
            CHECK(expected > previous);
            previous = expected;
        }
        CHECK_EQ_I(Report(*ts).reverbProgram, p);
    }
}

TEST(Fx, ReverbDelayPrograms)
{
    // program 6, delay: echo after REV_TIME 18h = 70 ms, feedback 22h; program 7, pan delay: REV_TIME 7Fh
    // = 358 ms, the echoes alternate left and right
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const std::initializer_list<int> burst = {0xC0, 1, 0x90, 60, 127}; // DC, 10 ms
    Wet d = WetOf(bank, 0x20, 91, 127, [](TestSynth& ts) { ts.Send(0, {0xB0, 80, 6}); }, burst, 375, 9000);
    const size_t echo = 2625;
    CHECK(d.Rms(600, 2600) < 1e-7);
    const double first = d.Rms(echo + 100, echo + 350), second = d.Rms(2 * echo + 100, 2 * echo + 350);
    CHECK(first > 0.01);
    CHECK_NEAR(second / first, 0x22 / 128.0, 0.01);
    CHECK(d.l == d.r);
    Wet p = WetOf(bank, 0x20, 91, 127, [](TestSynth& ts) { ts.Send(0, {0xB0, 80, 7}); }, burst, 375, 28000);
    const size_t pan = 13440;
    CHECK(p.Rms(pan + 100, pan + 350) > 0.01);
    CHECK(p.Rms(pan + 100, pan + 350, true) < 1e-7);
    CHECK(p.Rms(2 * pan + 100, 2 * pan + 350, true) > 0.01);
    CHECK(p.Rms(2 * pan + 100, 2 * pan + 350) < 1e-7);
}

TEST(Fx, ReverbTimeLevelFeedback)
{
    // GS 40 01 33 level (linear), 34 time (-32 steps halve the decay time), 35 delay feedback
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto tail = [&bank](const Setup& extra) {
        return Chip(bank, 0x20,
                    [&](TestSynth& t) {
                        extra(t);
                        t.Send(0, {0xB0, 91, 127, 0xC0, 3, 0x90, 69, 127});
                        t.Send(3750, {0x80, 69, 0});
                    },
                    26250);
    };
    auto full = tail(kNone);
    auto shorter = tail([](TestSynth& t) { t.Gs(0, 0x40, 0x01, 0x34, {0x7F - 32}); });
    CHECK_NEAR(SlopeDbPerSecond(*shorter, 7500, 18000) / SlopeDbPerSecond(*full, 7500, 26250), 2.0, 0.3);
    CHECK_NEAR(Report(*shorter).reverbDecaySeconds / Report(*full).reverbDecaySeconds, 0.5, 1e-9);
    auto half = tail([](TestSynth& t) { t.Gs(0, 0x40, 0x01, 0x33, {0x24}); });
    CHECK_NEAR(half->RmsL(8000, 20000) / full->RmsL(8000, 20000), 0x24 / 72.0, 1e-4);
    auto none = tail([](TestSynth& t) { t.Gs(0, 0x40, 0x01, 0x33, {0x00}); });
    CHECK_EQ_I(none->PeakL(5000, 26250), 0);
    // feedback 0 on the delay program: one echo only; 40h: each echo half the one before
    const std::initializer_list<int> burst = {0xC0, 1, 0x90, 60, 127};
    auto echoes = [&](int fb) {
        return WetOf(
            bank, 0x20, 91, 127, [fb](TestSynth& ts) { ts.Send(0, {0xB0, 80, 6}); ts.Gs(0, 0x40, 0x01, 0x35, {fb}); },
            burst, 375, 9000);
    };
    Wet none2 = echoes(0), some = echoes(0x40);
    CHECK(none2.Rms(2625 + 100, 2625 + 350) > 0.01);
    CHECK(none2.Rms(5250 + 100, 5250 + 350) < 1e-7);
    CHECK_NEAR(some.Rms(5250 + 100, 5250 + 350) / some.Rms(2625 + 100, 2625 + 350), 0.5, 0.01);
}

// ---- chorus ----

TEST(Fx, ChorusPrograms)
{
    // programs 0-7 (CC 81): every one returns a signal; the modulated ones (0-5) differ left and right,
    // the delays (6, 7) do not; the short delay sits at CHR_DEL 7Fh = 40 ms; the feedback delay repeats
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    for (int p = 0; p < 6; p++)
    {
        Wet w = WetOf(bank, 0x10, 93, 127, [p](TestSynth& ts) { ts.Send(0, {0xB0, 81, p}); }, {0x90, 69, 100}, 18000,
                      18000);
        CHECK(w.Rms(4000, 18000) > 0.002);
        double diff = 0;
        for (size_t i = 4000; i < 18000; i++)
            diff = std::fmax(diff, std::fabs(w.l[i] - w.r[i]));
        CHECK(diff > 1e-3);
        CHECK_EQ_I(Report(*Chip(bank, 0x10, [p](TestSynth& ts) { ts.Send(0, {0xB0, 81, p}); }, 32)).chorusProgram, p);
    }
    const std::initializer_list<int> burst = {0xC0, 1, 0x90, 60, 127};
    Wet s = WetOf(bank, 0x10, 93, 127, [](TestSynth& ts) { ts.Send(0, {0xB0, 81, 6}); }, burst, 375, 6000);
    CHECK(s.l == s.r);
    CHECK(s.Rms(0, 37 + 1500 - 2) < 1e-9);
    CHECK(s.Rms(37 + 1500 + 40, 37 + 1500 + 300) > 0.01);
    CHECK(s.Rms(37 + 3000 + 40, 37 + 3000 + 300) < 1e-7); // no feedback
    Wet f = WetOf(bank, 0x10, 93, 127, [](TestSynth& ts) { ts.Send(0, {0xB0, 81, 7}); }, burst, 375, 6000);
    CHECK_NEAR(f.Rms(37 + 3000 + 100, 37 + 3000 + 300) / f.Rms(37 + 1500 + 100, 37 + 1500 + 300),
               0x50 / 127.0 * 0.85, 0.01);
}

TEST(Fx, ChorusParameters)
{
    // GS 40 01 3A level, 3B feedback, 3C delay, 3D rate, 3E depth
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const std::initializer_list<int> burst = {0xC0, 1, 0x90, 60, 127};
    // delay 40h on the short delay: 40 ms x 2^(-63/24) = 6.2 ms = 231 samples
    Wet d = WetOf(bank, 0x10, 93, 127, [](TestSynth& ts) { ts.Send(0, {0xB0, 81, 6}); ts.Gs(0, 0x40, 0x01, 0x3C, {0x40}); },
                  burst, 375, 3000);
    const size_t at = 37 + static_cast<size_t>(std::lround(40.0 * std::exp2(-63.0 / 24.0) * 37.5));
    CHECK(d.Rms(0, at - 2) < 1e-9);
    CHECK(d.Rms(at + 40, at + 300) > 0.01);
    // level 0: nothing returns
    Wet z = WetOf(bank, 0x10, 93, 127, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x01, 0x3A, {0x00}); }, {0x90, 69, 100},
                  12000, 12000);
    CHECK_EQ_I(z.Rms(0, 12000), 0);
    // feedback on the short delay: a second repeat at f / 127 x 0.85
    Wet fb = WetOf(bank, 0x10, 93, 127, [](TestSynth& ts) { ts.Send(0, {0xB0, 81, 6}); ts.Gs(0, 0x40, 0x01, 0x3B, {0x7F}); },
                   burst, 375, 6000);
    CHECK_NEAR(fb.Rms(37 + 3000 + 100, 37 + 3000 + 300) / fb.Rms(37 + 1500 + 100, 37 + 1500 + 300), 0.85, 0.01);
    // depth 0 on chorus3: left and right taps coincide; rate changes how fast the wet signal moves
    Wet still = WetOf(bank, 0x10, 93, 127, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x01, 0x3E, {0x00}); }, {0x90, 69, 100},
                      12000, 12000);
    CHECK(still.l == still.r);
    // the moving delay bends the wet tone's pitch (Doppler): the faster the rate, the wider the swing
    auto swing = [&bank](int rate) {
        Wet w = WetOf(bank, 0x10, 93, 127, [rate](TestSynth& ts) { ts.Gs(0, 0x40, 0x01, 0x3D, {rate}); },
                      {0x90, 69, 100}, 30000, 30000);
        double lo = 1e9, hi = 0;
        for (size_t a = 4000; a + 400 <= 30000; a += 400)
        {
            const double f = w.Frequency(a, a + 400);
            lo = std::fmin(lo, f);
            hi = std::fmax(hi, f);
        }
        return 1200.0 * std::log2(hi / lo);
    };
    CHECK(swing(0x40) > 5.0 * swing(0x03));
}

// ---- spatial effect, equalizer, routing ----

TEST(Fx, SpatialEffect)
{
    // NRPN 3720h volume, 372Ch delay (0 = 0.25 ms = 9 samples), 372Dh input (0 stereo L - R, 7Fh mono L + R)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto run = [&bank](int word, int volume, int input, int pan) {
        return Chip(bank, word,
                    [&](TestSynth& ts) {
                        ts.Nrpn(0, 0, 0x37, 0x20, volume);
                        ts.Nrpn(0, 0, 0x37, 0x2C, 0x00);
                        ts.Nrpn(0, 0, 0x37, 0x2D, input);
                        ts.Send(0, {0xB0, 10, pan, 0x90, 69, 100});
                    },
                    4000);
    };
    auto plain = run(0x00, 0x7F, 0x00, 0);
    auto wide = run(0x08, 0x7F, 0x00, 0);
    double worst = 0;
    for (size_t i = 100; i < 4000; i++)
    {
        worst = std::fmax(worst, std::fabs(wide->R(i) + plain->L(i - 9)));
        worst = std::fmax(worst, std::fabs(wide->L(i) - plain->L(i) - plain->L(i - 9)));
    }
    CHECK(worst < 1e-6);
    CHECK_EQ_I(plain->PeakR(0, 4000), 0);
    // a centered voice has no L - R: the stereo input leaves it alone; the mono input does not
    auto centered = run(0x00, 0x7F, 0x00, 64);
    auto stereoIn = run(0x08, 0x7F, 0x00, 64);
    auto monoIn = run(0x08, 0x7F, 0x7F, 64);
    CHECK(centered->out == stereoIn->out);
    CHECK(!(centered->out == monoIn->out));
    // volume 0 (the power-up value): inaudible although switched on
    CHECK(run(0x08, 0x00, 0x00, 0)->out == plain->out);
}

TEST(Fx, EqualizerBands)
{
    // NRPN 3700h-3703h gains (00h -12 dB .. 40h 0 dB .. 7Fh +11.8 dB), 3708h-370Bh corners
    CHECK_NEAR(Equalizer::BandGainDb(0x60), 6.0, 1e-12);
    CHECK_NEAR(Equalizer::BandGainDb(0x20), -6.0, 1e-12);
    CHECK_NEAR(Equalizer::BandFrequency(0, 0x0C), 444.1, 0.1);
    CHECK_NEAR(Equalizer::BandFrequency(1, 0x1B), 892.9, 0.1);
    CHECK_NEAR(Equalizer::BandFrequency(2, 0x72), 3770.1, 0.1);
    CHECK_NEAR(Equalizer::BandFrequency(3, 0x40), 9448.8, 0.1);
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto gainDb = [&bank](int word, int key, const Setup& setup) {
        auto level = [&](int w) {
            auto ts = Chip(bank, w,
                           [&](TestSynth& t) {
                               setup(t);
                               t.Send(0, {0x90, key, 100});
                           },
                           20000);
            return ts->RmsL(6000, 20000);
        };
        return 20.0 * std::log10(level(word) / level(0x00));
    };
    // power-up: +6 dB low shelf at 444 Hz, so a 47 Hz tone is 6 dB up
    CHECK_NEAR(gainDb(0x03, 33, kNone), 6.0, 0.2);
    // low shelf at 4.7 kHz cut 12 dB on a 375 Hz tone
    auto cutLow = [](TestSynth& t) { t.Nrpn(0, 0, 0x37, 0x08, 0x7F); t.Nrpn(0, 0, 0x37, 0x00, 0x00); t.Nrpn(0, 0, 0x37, 0x03, 0x40); };
    CHECK_NEAR(gainDb(0x03, 69, cutLow), -12.0, 0.3);
    // high shelf at 148 Hz boosted on a 3 kHz tone
    auto boostHigh = [](TestSynth& t) { t.Nrpn(0, 0, 0x37, 0x0B, 0x01); t.Nrpn(0, 0, 0x37, 0x03, 0x7F); t.Nrpn(0, 0, 0x37, 0x00, 0x40); };
    CHECK_NEAR(gainDb(0x03, 105, boostHigh), 11.81, 0.2);
    // the two peaking bands centered on 364 Hz, on a 375 Hz tone
    auto midLow = [](TestSynth& t) {
        t.Nrpn(0, 0, 0x37, 0x09, 0x0B); t.Nrpn(0, 0, 0x37, 0x01, 0x7F); t.Nrpn(0, 0, 0x37, 0x00, 0x40); t.Nrpn(0, 0, 0x37, 0x03, 0x40);
    };
    auto midHigh = [](TestSynth& t) {
        t.Nrpn(0, 0, 0x37, 0x0A, 0x0B); t.Nrpn(0, 0, 0x37, 0x02, 0x00); t.Nrpn(0, 0, 0x37, 0x00, 0x40); t.Nrpn(0, 0, 0x37, 0x03, 0x40);
    };
    CHECK_NEAR(gainDb(0x03, 69, midLow), 11.8, 0.2);
    CHECK_NEAR(gainDb(0x03, 69, midHigh), -12.0, 0.2);
    // 2-band mode (375Fh EQ bits 10): the shelves only
    CHECK_NEAR(gainDb(0x02, 69, midLow), 0.0, 0.01);
    CHECK_NEAR(gainDb(0x02, 69, cutLow), -12.0, 0.3);
}

TEST(Fx, PostEffectsRouting)
{
    // NRPN 3718h (GM bus) and 371Ah (reverb / chorus returns) through the post effects (EQ, spatial)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto cutLow = [](TestSynth& t) { t.Nrpn(0, 0, 0x37, 0x08, 0x7F); t.Nrpn(0, 0, 0x37, 0x00, 0x00); t.Nrpn(0, 0, 0x37, 0x03, 0x40); };
    auto run = [&](int gm, int fx) {
        return Chip(bank, 0x23,
                    [&](TestSynth& t) {
                        cutLow(t);
                        t.Nrpn(0, 0, 0x37, 0x18, gm);
                        t.Nrpn(0, 0, 0x37, 0x1A, fx);
                        t.Send(0, {0xB0, 91, 127, 0x90, 69, 127});
                        t.Send(7500, {0x80, 69, 0});
                    },
                    20000);
    };
    auto both = run(0x7F, 0x7F), dryOut = run(0x00, 0x7F), fxOut = run(0x7F, 0x00);
    // the dry note (before the reverb builds up much): 12 dB down only through the post effects
    const double dryDb = 20.0 * std::log10(dryOut->RmsL(1000, 1600) / both->RmsL(1000, 1600));
    CHECK(dryDb > 9.0 && dryDb < 12.3);
    // the tail (reverb only): down only when the returns go through the post effects
    const double tailDb = 20.0 * std::log10(fxOut->RmsL(9000, 20000) / both->RmsL(9000, 20000));
    CHECK(tailDb > 9.0 && tailDb < 12.3);
    CHECK_NEAR(dryOut->RmsL(9000, 20000) / both->RmsL(9000, 20000), 1.0, 1e-6);
}

// ---- sends ----

TEST(Fx, SendLevels)
{
    // CC 91 / 93: the SF2 send generators (200 per mille at 127/128) - linear in the controller
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    for (auto [word, cc] : {std::pair<int, int>{0x20, 91}, {0x10, 93}})
    {
        Wet full = WetOf(bank, word, cc, 127, kNone, {0x90, 69, 100}, 7500, 15000);
        Wet half = WetOf(bank, word, cc, 64, kNone, {0x90, 69, 100}, 7500, 15000);
        CHECK(full.Rms(2000, 15000) > 0.005);
        CHECK_NEAR(half.Rms(2000, 15000) / full.Rms(2000, 15000), 64.0 / 127.0, 1e-4);
    }
    // power-up sends: reverb 40 (GS), chorus 0
    auto ts = Chip(bank, 0x30, [](TestSynth& t) { t.Send(0, {0x90, 69, 100}); t.Send(3000, {0x80, 69, 0}); }, 12000);
    CHECK(ts->RmsL(6000, 12000) > 1e-4);
    auto off = Chip(bank, 0x30, [](TestSynth& t) { t.Send(0, {0xB0, 91, 0, 0x90, 69, 100}); t.Send(3000, {0x80, 69, 0}); },
                    12000);
    CHECK_EQ_I(off->PeakL(6000, 12000), 0);
}

TEST(Fx, SendScaling)
{
    // NRPN 3715h / 3716h: GM reverb / chorus send scaling, 40h = as sent
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    Wet base = WetOf(bank, 0x20, 91, 127, kNone, {0x90, 69, 100}, 7500, 15000);
    Wet low = WetOf(bank, 0x20, 91, 127, [](TestSynth& t) { t.Nrpn(0, 0, 0x37, 0x15, 0x20); }, {0x90, 69, 100}, 7500, 15000);
    CHECK_NEAR(low.Rms(2000, 15000) / base.Rms(2000, 15000), 0.5, 1e-4);
    Wet more = WetOf(bank, 0x20, 91, 127, [](TestSynth& t) { t.Nrpn(0, 0, 0x37, 0x15, 0x7F); }, {0x90, 69, 100}, 7500, 15000);
    CHECK_NEAR(more.Rms(2000, 15000) / base.Rms(2000, 15000), 127.0 / 64.0, 1e-4);
    Wet none = WetOf(bank, 0x10, 93, 127, [](TestSynth& t) { t.Nrpn(0, 0, 0x37, 0x16, 0x00); }, {0x90, 69, 100}, 7500, 15000);
    CHECK_EQ_I(none.Rms(0, 15000), 0);
}

TEST(Fx, DrumNoteSends)
{
    // NRPN 1Drr / 1Err: the reverb / chorus send depth of drum key rr (7Fh = the part's send)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto wet = [&](int word, int cc, int msb, int value) {
        return WetOf(bank, word, cc, 127, [=](TestSynth& t) { t.Nrpn(0, 9, msb, 46, value); }, {0xB9, 0, 0, 0x99, 46, 127},
                     7500, 15000)
            .Rms(2000, 15000);
    };
    CHECK_EQ_I(wet(0x20, 91, 0x1D, 0x00), 0);
    CHECK_NEAR(wet(0x20, 91, 0x1D, 0x40) / wet(0x20, 91, 0x1D, 0x7F), 64.0 / 127.0, 1e-4);
    CHECK_EQ_I(wet(0x10, 93, 0x1E, 0x00), 0);
    CHECK_NEAR(wet(0x10, 93, 0x1E, 0x40) / wet(0x10, 93, 0x1E, 0x7F), 64.0 / 127.0, 1e-4);
}

// ---- output stage ----

TEST(Fx, SoftClipping)
{
    // NRPN 3713h: soft clipping (power-up) never reaches full scale; hard clipping stops at it
    CHECK_EQ_I(SoftClip(0.5f) == 0.5f, 1);
    CHECK_EQ_I(SoftClip(kSoftClipKnee) == kSoftClipKnee, 1);
    CHECK(SoftClip(1.0f) < 1.0f && SoftClip(1.0f) > 0.85f);
    CHECK(SoftClip(1000.0f) < 1.0f);
    CHECK_NEAR(SoftClip(-2.0f), -SoftClip(2.0f), 0.0);
    CHECK_NEAR((SoftClip(kSoftClipKnee + 1e-3f) - kSoftClipKnee) / 1e-3, 1.0, 0.01); // slope 1 at the knee
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto chord = [&bank](int mode) {
        return Chip(bank, 0x00,
                    [mode](TestSynth& t) {
                        t.Nrpn(0, 0, 0x37, 0x13, mode);
                        for (int ch : {0, 1, 2, 3, 4, 5, 6, 7, 8, 10})
                            t.Send(0, {0xC0 | ch, 1, 0xB0 | ch, 7, 127, 0x90 | ch, 60, 127});
                    },
                    3000);
    };
    auto soft = chord(0x00);
    CHECK(soft->PeakL(0, 3000) < 1.0 && soft->PeakL(0, 3000) > 0.95);
    CHECK_EQ_I(chord(0x7F)->PeakL(0, 3000) == 1.0, 1);
    // the dry render mode does not clip
    TestSynth dry(bank);
    for (int ch : {0, 1, 2, 3, 4, 5, 6, 7, 8, 10})
        dry.Send(0, {0xC0 | ch, 1, 0xB0 | ch, 7, 127, 0x90 | ch, 60, 127});
    dry.RunTo(3000);
    CHECK(dry.PeakL(0, 3000) > 3.0);
}

TEST(Fx, CodecGain)
{
    // Dream SysEx port 12h (codec control 0): OUTG[5:0] 39h = 0 dB, 1 dB steps; DACSEL = 0 or DACMUTE = 1
    // mute; port 14h is the ADC side (no output effect)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto level = [&bank](int port, int value) {
        TestSynth ts(bank);
        ts.Send(0, {0xF0, 0x00, 0x20, 0x00, 0x00, 0x00, 0x12, 0x33, 0x77, port, (value >> 12) & 15, (value >> 8) & 15,
                    (value >> 4) & 15, value & 15, 0x00, 0xF7});
        ts.Send(0, {0xC0, 1, 0x90, 60, 127});
        ts.RunTo(3100);
        return ts.RmsL(2000, 3000);
    };
    const double unity = level(0x12, kCodec0Default);
    CHECK_NEAR(level(0x12, 0x1B73) / unity, std::pow(10.0, -6.0 / 20.0), 1e-5);
    CHECK_NEAR(level(0x12, 0x1B7F) / unity, std::pow(10.0, 6.0 / 20.0), 1e-5);
    CHECK_NEAR(level(0x12, 0x1B50) / unity, std::pow(10.0, -43.5 / 20.0), 1e-7);
    CHECK_EQ_I(level(0x12, 0x1B39), 0); // DACSEL = 0
    CHECK_EQ_I(level(0x12, 0x1BF9), 0); // DACMUTE = 1
    CHECK_NEAR(level(0x14, 0x477D) / unity, 1.0, 1e-9);
    CHECK_NEAR(Effects::CodecGainDb(0x11), -40.0, 0.0);
    CHECK_NEAR(Effects::CodecGainDb(0x05), -58.5, 0.0);
}

TEST(Fx, EffectsWordSwitchesEffects)
{
    // NRPN 375Fh switches the effects themselves, not only their voice cost: reverb off = no tail; a
    // reverb switched off loses its tail, switched on again it starts empty
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto run = [&bank](int word, const Setup& extra) {
        return Chip(bank, word,
                    [&](TestSynth& t) {
                        t.Send(0, {0xB0, 91, 127, 0x90, 69, 127});
                        t.Send(3000, {0x80, 69, 0});
                        extra(t);
                    },
                    20000);
    };
    CHECK_EQ_I(run(0x00, kNone)->PeakL(4000, 20000), 0);
    auto on = run(0x20, kNone);
    CHECK(on->RmsL(10000, 20000) > 1e-4);
    CHECK_EQ_I(Report(*on).polyphonyLimit, 50);
    auto cut = run(0x20, [](TestSynth& t) { t.Nrpn(8000, 0, 0x37, 0x5F, 0x00); t.Nrpn(9000, 0, 0x37, 0x5F, 0x20); });
    CHECK(cut->RmsL(4000, 7900) > 1e-4);
    CHECK_EQ_I(cut->PeakL(8032, 20000), 0);
    CHECK_EQ_I(Report(*cut).polyphonyLimit, 50);
}

TEST(Fx, ProgramSelect)
{
    // CC 80 / 81 (DREAM) and GS 40 01 30 / 31 / 38: a program sets its parameter defaults; the reverb
    // character selects the algorithm alone; out-of-range programs are ignored; MIDI reset restores 4 / 2
    TestSynth ts(BasicBank(), SynthConfig{}, true);
    ts.Send(0, {0xB0, 80, 3, 0xB0, 81, 5});
    ts.RunTo(32);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.reverbProgram, 3);
    CHECK_EQ_I(r.chorusProgram, 5);
    CHECK_NEAR(r.reverbDecaySeconds, 1.90, 1e-9);
    ts.Send(32, {0xB0, 80, 9, 0xB0, 81, 8});
    ts.Gs(32, 0x40, 0x01, 0x31, {0x05});
    ts.RunTo(64);
    r = Report(ts);
    CHECK_EQ_I(r.reverbProgram, 3);
    CHECK_EQ_I(r.reverbCharacter, 5);
    CHECK_EQ_I(r.chorusProgram, 5);
    ts.Gs(64, 0x40, 0x01, 0x30, {0x01});
    ts.Gs(64, 0x40, 0x01, 0x38, {0x07});
    ts.RunTo(96);
    r = Report(ts);
    CHECK_EQ_I(r.reverbProgram, 1);
    CHECK_EQ_I(r.reverbCharacter, 1);
    CHECK_EQ_I(r.chorusProgram, 7);
    ts.Send(96, {0xFF});
    ts.RunTo(128);
    r = Report(ts);
    CHECK_EQ_I(r.reverbProgram, 4);
    CHECK_EQ_I(r.chorusProgram, 2);
    CHECK_NEAR(r.reverbDecaySeconds, 2.60, 1e-9);
}

TEST(Render, Modes)
{
    // with every effect off and the signal below the soft-clip knee, the chip path equals the dry path
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto dry = std::make_unique<TestSynth>(bank);
    auto chip = Chip(bank, 0x00, kNone, 0);
    for (TestSynth* t : {dry.get(), chip.get()})
    {
        t->Send(0, {0xB0, 91, 127, 0xB0, 93, 127, 0x90, 69, 100, 0x91, 64, 90});
        t->Send(3000, {0x80, 69, 0});
        t->RunTo(8000);
    }
    CHECK(dry->out == chip->out);
    CHECK(dry->PeakL(0, 8000) > 0.1);
}
