// libsam2695 tests - the chip level: voice allocation, exclusive classes, timing, state, render, the
// MIDI implementation chart rows of SAM-1 / SAM-2 (SAM-3 rows: charttests.cpp), golden fingerprints.
#include "common/conv.h"
#include "synthhelper.h"
#include "testfw.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

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

// A held DC note per channel (the "Pad" preset: 1 s release), each with its own velocity
void HoldNotes(TestSynth& ts, uint64_t t, std::initializer_list<std::pair<int, int>> chVel)
{
    for (auto [ch, vel] : chVel)
        ts.Send(t, {0xC0 | ch, 2, 0x90 | ch, 60, vel});
}

// RMS per block in dB: the golden fingerprint. Tolerant to libm differences between platforms,
// sharp enough to catch any change of the voice model (README "Tests").
std::vector<double> Fingerprint(const TestSynth& ts, size_t block)
{
    std::vector<double> fp;
    for (size_t b = 0; (b + 1) * block <= ts.Frames(); b++)
    {
        double s = 0;
        for (size_t i = b * block; i < (b + 1) * block; i++)
            s += static_cast<double>(ts.L(i)) * ts.L(i) + static_cast<double>(ts.R(i)) * ts.R(i);
        fp.push_back(10.0 * std::log10(s / (2.0 * block) + 1e-20));
    }
    return fp;
}

void CheckGolden(const char* name, const TestSynth& ts, const std::vector<double>& expected)
{
    const std::vector<double> fp = Fingerprint(ts, 1024);
    if (std::getenv("SAM2695_PRINT_GOLDEN") != nullptr)
    {
        std::printf("    golden %s:", name);
        for (double v : fp)
            std::printf(" %.2f,", v);
        std::printf("\n    sha256 %s\n",
                    DigestHex(Sha256(reinterpret_cast<const uint8_t*>(ts.out.data()), ts.out.size() * sizeof(float))).c_str());
    }
    CHECK_EQ_I(fp.size(), expected.size());
    for (size_t i = 0; i < fp.size() && i < expected.size(); i++)
        CHECK_NEAR(fp[i], expected[i], expected[i] < -150.0 ? 30.0 : expected[i] < -90.0 ? 1.0 : 0.05);
}

} // namespace

TEST(Allocator, EffectsWordPolyphony)
{
    // every row of the NRPN 375Fh table (datasheet p.35)
    const struct
    {
        uint8_t word;
        uint32_t voices;
    } rows[] = {{0x00, 64}, {0x02, 59}, {0x03, 55}, {0x08, 62}, {0x0A, 58}, {0x0B, 54}, {0x0E, 57}, {0x20, 50},
                {0x22, 46}, {0x23, 42}, {0x28, 49}, {0x2A, 45}, {0x2B, 41}, {0x30, 48}, {0x33, 39}, {0x37, 38},
                {0x3B, 38}, {0x45, 38}, {0x4E, 55}, {0x74, 44}, {0x76, 40}, {0x77, 36}, {0x7E, 39}, {0x7F, 35}};
    for (const auto& row : rows)
        CHECK_EQ_I(PolyphonyForEffectsWord(row.word), row.voices);
    CHECK_EQ_I(PolyphonyForEffectsWord(kPowerUpEffectsWord), 38);
}

TEST(Allocator, DefaultAndNrpnPolyphony)
{
    TestSynth ts(BasicBank());
    for (int i = 0; i < 40; i++)
        ts.Send(0, {0xC0 | (i % 9), 1, 0x90 | (i % 9), 40 + i, 100});
    ts.RunTo(64);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.polyphonyLimit, 38);
    CHECK_EQ_I(r.activeVoices, 38);
    CHECK_EQ_I(r.voicesStolen, 2);
    // NRPN 375Fh = 00h: every effect off, 64 voices
    ts.Send(100, {0xB0, 99, 0x37, 0xB0, 98, 0x5F, 0xB0, 6, 0x00});
    for (int i = 0; i < 30; i++)
        ts.Send(100, {0x9A, 30 + i, 100});
    ts.Send(100, {0xCA, 1});
    for (int i = 0; i < 30; i++)
        ts.Send(101, {0x9A, 30 + i, 100});
    ts.RunTo(200);
    r = Report(ts);
    CHECK_EQ_I(r.polyphonyLimit, 64);
    CHECK_EQ_I(r.effectsWord, 0);
    CHECK_EQ_I(r.activeVoices, 64);
    // back to reverb + chorus + 4-band EQ (33h, 39 voices): the excess is stolen at once
    ts.Send(300, {0xB0, 99, 0x37, 0xB0, 98, 0x5F, 0xB0, 6, 0x33});
    ts.RunTo(400);
    r = Report(ts);
    CHECK_EQ_I(r.polyphonyLimit, 39);
    CHECK_EQ_I(r.activeVoices, 39);
}

TEST(Allocator, StealingOrder)
{
    SynthConfig cfg;
    cfg.polyphony = 4;
    TestSynth ts(BasicBank(), cfg);
    // four held notes on channels 0-3; channel 1 is quiet; channel 2 is released (long release)
    HoldNotes(ts, 0, {{0, 100}, {1, 30}, {2, 100}, {3, 90}});
    ts.RunTo(320);
    ts.Send(320, {0x82, 60, 0});
    ts.RunTo(640);
    CHECK_EQ_I(Report(ts).activeVoices, 4);
    // 5th note: the voice in its release goes first
    HoldNotes(ts, 640, {{4, 100}});
    ts.RunTo(960);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[2].activeVoices, 0);
    CHECK_EQ_I(r.channels[4].activeVoices, 1);
    // 6th note: then the quietest held voice (channel 1)
    HoldNotes(ts, 960, {{5, 100}});
    ts.RunTo(1280);
    r = Report(ts);
    CHECK_EQ_I(r.channels[1].activeVoices, 0);
    CHECK_EQ_I(r.channels[0].activeVoices, 1);
    CHECK_EQ_I(r.voicesStolen, 2);
    // 7th note: the quietest held voice left is channel 3 (velocity 90)
    HoldNotes(ts, 1280, {{6, 100}});
    ts.RunTo(1600);
    r = Report(ts);
    CHECK_EQ_I(r.channels[3].activeVoices, 0);
    // 8th note: channels 0, 4, 5, 6 are equally loud: the oldest (channel 0) goes
    HoldNotes(ts, 1600, {{7, 100}});
    ts.RunTo(1920);
    r = Report(ts);
    CHECK_EQ_I(r.channels[0].activeVoices, 0);
    CHECK_EQ_I(r.channels[4].activeVoices, 1);
    CHECK_EQ_I(r.activeVoices, 4);
}

TEST(Allocator, RhythmProtected)
{
    SynthConfig cfg;
    cfg.polyphony = 2;
    TestSynth ts(BasicBank(), cfg);
    ts.Send(0, {0x99, 36, 20});           // a quiet held kick on the rhythm channel
    HoldNotes(ts, 0, {{0, 127}});         // a loud melodic note
    ts.RunTo(320);
    HoldNotes(ts, 320, {{1, 127}});
    ts.RunTo(640);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[9].activeVoices, 1); // the drum survives although it is the quietest
    CHECK_EQ_I(r.channels[0].activeVoices, 0);
}

TEST(Voice, ExclusiveClass)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0x99, 46, 100});   // open hi-hat (class 1)
    ts.Send(0, {0x99, 36, 100});   // kick (no class)
    ts.RunTo(320);
    CHECK_EQ_I(Report(ts).channels[9].activeVoices, 2);
    ts.Send(320, {0x99, 42, 100}); // closed hi-hat (class 1) cuts the open one
    ts.RunTo(352);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[9].activeVoices, 2); // kick + closed
    CHECK_EQ_I(r.fadingVoices, 1);
    ts.RunTo(640);
    CHECK_EQ_I(Report(ts).fadingVoices, 0); // the fade ends after kFadeSamples
    // the same class on another channel is untouched
    ts.Send(640, {0xC1, 0, 0x91, 60, 100});
    ts.Send(640, {0x99, 46, 100});
    ts.RunTo(700);
    CHECK_EQ_I(Report(ts).channels[1].activeVoices, 1);
}

TEST(Timing, SampleAccurateNoteOn)
{
    // a note written at host time t starts at internal sample t (hostTickRate = internal rate), at any
    // phase of the control block: the same note at 1000 and at 1013 gives the same samples, shifted
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    TestSynth a(bank), b(bank);
    a.Send(1000, {0xC0, 1, 0x90, 60, 127});
    b.Send(1013, {0xC0, 1, 0x90, 60, 127});
    a.RunTo(1300);
    b.RunTo(1300);
    // the SF2 defaults: delay -12000 tc and attack -12000 tc, 37 samples each (0.977 ms)
    CHECK_EQ_I(a.L(1036), 0);
    CHECK_EQ_I(a.L(1037), 0); // the attack starts from silence
    CHECK(a.L(1038) > 0.0f);
    CHECK_EQ_I(b.L(1050), 0);
    CHECK(b.L(1051) > 0.0f);
    double worst = 0.0;
    for (size_t i = 0; i < 250; i++)
        worst = std::fmax(worst, std::fabs(a.L(1000 + i) - b.L(1013 + i)));
    CHECK(worst < 1e-5);
    // note-off: the release starts at its sample too (default release 1 ms)
    a.Send(1500, {0x80, 60, 0});
    a.RunTo(1700);
    CHECK_NEAR(a.L(1499), a.L(1400), 1e-6);
    CHECK(a.L(1510) < a.L(1499));
    CHECK_EQ_I(a.L(1600), 0);
}

TEST(Timing, ResetBusy)
{
    // ~50 ms after a reset the chip ignores MIDI (datasheet p.9)
    TestSynth ts(BasicBank());
    ts.synth.Reset(0);
    ts.Send(1000, {0xC0, 1, 0x90, 60, 127}); // 26.7 ms: dropped
    ts.Send(1874, {0x80, 60, 0});            // 49.97 ms: dropped
    ts.Send(1875, {0x90, 62, 127});          // 50 ms: plays (program still 0: sine)
    ts.RunTo(2500);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.bytesDroppedBusy, 8);
    CHECK_EQ_I(r.channels[0].activeVoices, 1);
    CHECK_EQ_I(r.channels[0].program, 0);
    CHECK(ts.PeakL(0, 1875) == 0.0);
}

TEST(State, RoundTrip)
{
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    TestSynth a(bank);
    a.Send(100, {0x90, 69, 100, 0x91, 72, 90, 0xE0, 0x00, 0x50, 0xB1, 1, 100});
    a.Send(4000, {0x80, 69, 0});
    a.Send(6000, {0x99, 46, 100});          // queued in the future: part of the state
    a.RunTo(5000);
    const std::vector<uint8_t> blob = Save(a.synth);
    a.RunTo(9000);

    TestSynth b(bank);
    CHECK(b.synth.LoadState(blob.data(), blob.size()));
    CHECK_EQ_I(b.synth.InternalPosition(), 4992);
    b.RunTo(9000);
    CHECK_EQ_I(b.Frames(), 9000 / kControlBlock * kControlBlock - 4992);
    bool same = b.Frames() > 0;
    for (size_t i = 0; i < b.Frames() && same; i++)
        same = a.out[(4992 + i) * 2] == b.out[i * 2] && a.out[(4992 + i) * 2 + 1] == b.out[i * 2 + 1];
    CHECK(same);
    CHECK(Save(a.synth) == Save(b.synth));
}

TEST(State, RefusesOtherBank)
{
    TestSynth a(BasicBank());
    a.Send(0, {0x90, 69, 100});
    a.RunTo(320);
    const std::vector<uint8_t> blob = Save(a.synth);
    Sf2Builder other;
    other.AddSample(DcSample("x"));
    other.presets.push_back(SimplePreset("X", 0, 0, 0));
    TestSynth b(other.Load());
    b.Send(0, {0x90, 60, 100});
    b.RunTo(320);
    const std::vector<uint8_t> before = Save(b.synth);
    CHECK(!b.synth.LoadState(blob.data(), blob.size()));
    CHECK(Save(b.synth) == before); // untouched
    // a truncated or foreign blob is refused too
    CHECK(!a.synth.LoadState(blob.data(), blob.size() - 1));
    std::vector<uint8_t> bad = blob;
    bad[0] ^= 0xFF;
    CHECK(!a.synth.LoadState(bad.data(), bad.size()));
}

TEST(State, SizeConstant)
{
    TestSynth ts(BasicBank());
    const size_t empty = ts.synth.StateSize();
    for (int i = 0; i < 20; i++)
        ts.Send(0, {0x90, 40 + i, 100});
    ts.RunTo(320);
    CHECK_EQ_I(ts.synth.StateSize(), empty);
    CHECK(empty > 0);
}

TEST(Render, OutputRateIndependent)
{
    // the output rate only changes the render layer: the chip state is identical
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    std::vector<uint8_t> blobs[3];
    const uint32_t rates[3] = {kInternalRate, 44100, 96000};
    for (int i = 0; i < 3; i++)
    {
        SynthConfig cfg;
        cfg.hostTickRate = 3500000;
        cfg.outputRate = rates[i];
        Synth s;
        s.Configure(cfg);
        s.LoadBank(bank);
        s.WriteByte(1000, 0x90);
        s.WriteByte(1000, 69);
        s.WriteByte(1000, 100);
        s.WriteByte(150000, 0x80);
        s.WriteByte(150000, 69);
        s.WriteByte(150000, 0);
        float buf[4096 * 2];
        for (uint64_t t = 70000; t <= 350000; t += 70000)
        {
            s.Run(t);
            while (s.Render(buf, 4096) == 4096)
            {
            }
        }
        blobs[i] = Save(s);
    }
    CHECK(blobs[0] == blobs[1]);
    CHECK(blobs[0] == blobs[2]);
}

TEST(Render, RunSlicingIndependent)
{
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    TestSynth whole(bank), sliced(bank);
    for (TestSynth* t : {&whole, &sliced})
    {
        t->Send(37, {0x90, 69, 100});
        t->Send(1001, {0x91, 64, 80});
        t->Send(2999, {0x80, 69, 0});
    }
    whole.RunTo(4000);
    for (uint64_t t = 1; t <= 4000; t += 1 + (t * 7919) % 173)
        sliced.RunTo(t);
    sliced.RunTo(4000);
    CHECK(whole.out == sliced.out);
}

TEST(Render, ResampledSine)
{
    // 375 Hz at the internal rate, rendered at 48 kHz: same frequency, same level
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    double level[2] = {};
    double hz = 0.0;
    for (int i = 0; i < 2; i++)
    {
        SynthConfig cfg;
        cfg.hostTickRate = kInternalRate;
        cfg.outputRate = i == 0 ? kInternalRate : 48000;
        cfg.outputGain = 1.0f;
        Synth s;
        s.Configure(cfg);
        s.LoadBank(bank);
        for (uint8_t b : {0x90, 69, 127})
            s.WriteByte(0, b);
        s.Run(9600);
        std::vector<float> out(20000 * 2);
        const size_t n = s.Render(out.data(), 20000);
        const size_t from = n / 2, to = n;
        double sum = 0;
        int crossings = 0;
        double first = -1, last = -1;
        for (size_t k = from; k < to; k++)
        {
            sum += static_cast<double>(out[k * 2]) * out[k * 2];
            if (k > from && out[(k - 1) * 2] < 0 && out[k * 2] >= 0)
            {
                const double t = static_cast<double>(k - 1) + out[(k - 1) * 2] / (out[(k - 1) * 2] - out[k * 2]);
                if (first < 0)
                    first = t;
                else
                    crossings++;
                last = t;
            }
        }
        level[i] = std::sqrt(sum / static_cast<double>(to - from));
        if (i == 1)
        {
            // 9600 internal frames -> 12288 at 48 kHz, less the resampler's look-ahead of half its taps
            CHECK(n > 12288 - 40 && n <= 12288);
            hz = crossings * 48000.0 / (last - first);
        }
    }
    CHECK_NEAR(hz, 375.0, 0.05);
    CHECK_NEAR(level[1] / level[0], 1.0, 0.002);
}

// ---- MIDI implementation chart rows (datasheet §3, p.26-27) built in SAM-1 / SAM-2 ----

TEST(Chart, NoteOnOff)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0x90, 60, 100, 0x91, 60, 100});
    ts.RunTo(320);
    CHECK_EQ_I(Report(ts).activeVoices, 2);
    ts.Send(320, {0x90, 60, 0});  // 9n kk 00 = note off
    ts.Send(320, {0x81, 60, 64}); // 8n kk vv, velocity ignored
    ts.RunTo(1600);
    CHECK_EQ_I(Report(ts).activeVoices, 0);
}

TEST(Chart, ProgramChangeAndDrums)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0xC0, 1, 0xC9, 0});
    ts.RunTo(32);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].preset, 1);  // 0:1 "Dc"
    CHECK(r.channels[9].rhythm);
    CHECK_EQ_I(r.channels[9].preset, 4);  // 128:0 "Kit" (presets sort by bank, program)
    // channel 10 bank select does nothing; a program without a kit falls back to kit 0
    ts.Send(32, {0xB9, 0, 5, 0xC9, 16});
    ts.RunTo(64);
    CHECK_EQ_I(Report(ts).channels[9].preset, 4);
    // a missing program on a melodic channel is silent, not another sound
    ts.Send(64, {0xC2, 50});
    ts.RunTo(96);
    CHECK_EQ_I(Report(ts).channels[2].preset, -1);
}

TEST(Chart, BankSelectVariation)
{
    // CC 0 = 127 then a program: the MT-32 variation when the bank has it, else the capital tone
    Sf2Builder b;
    const int s = b.AddSample(SineSample("s"));
    b.presets.push_back(SimplePreset("Capital", 0, 5, s));
    b.presets.push_back(SimplePreset("Mt32", 127, 6, s));
    TestSynth ts(b.Load());
    ts.Send(0, {0xB0, 0, 127, 0xC0, 6, 0xB1, 0, 127, 0xC1, 5, 0xB2, 0, 127, 0xC2, 6, 0xB2, 0, 0});
    ts.RunTo(32);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].preset, 1);
    CHECK_EQ_I(r.channels[1].preset, 0);
    CHECK_EQ_I(r.channels[2].bankMsb, 127); // latched at the program change, not by the later CC 0
}

TEST(Chart, VolumeExpression)
{
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto level = [&bank](int vol, int expr) {
        TestSynth ts(bank);
        ts.Send(0, {0xC0, 1, 0xB0, 7, vol, 0xB0, 11, expr, 0x90, 60, 127});
        ts.RunTo(640);
        return static_cast<double>(ts.L(600));
    };
    const double cb = [](int v) { return 960.0 * ConcaveCurve(1.0 - v / 128.0); }(100) -
                      960.0 * ConcaveCurve(1.0 - 127 / 128.0);
    CHECK_NEAR(level(100, 127) / level(127, 127), std::pow(10.0, -cb / 200.0), 1e-5);
    CHECK_NEAR(level(127, 100) / level(127, 127), std::pow(10.0, -cb / 200.0), 1e-5);
    CHECK_EQ_I(level(0, 127), 0); // volume 0 is silence (concave curve reaches 960 cB)
}

TEST(Chart, SustainPedal)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0xB0, 64, 127, 0x90, 60, 100});
    ts.Send(320, {0x80, 60, 0});
    ts.RunTo(640);
    CHECK_EQ_I(Report(ts).activeVoices, 1); // held by the pedal
    ts.Send(640, {0xB0, 64, 0});
    ts.RunTo(1600);
    CHECK_EQ_I(Report(ts).activeVoices, 0);
}

TEST(Chart, AllSoundAndNotesOff)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0x90, 60, 100, 0x90, 64, 100, 0x91, 60, 100});
    ts.Send(320, {0xB0, 120, 0}); // all sound off: abrupt (fade of kFadeSamples)
    ts.RunTo(320 + 64 + 32);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].activeVoices, 0);
    CHECK_EQ_I(r.channels[1].activeVoices, 1);
    ts.Send(500, {0xB1, 64, 127, 0xB1, 123, 0}); // all notes off respects the sustain pedal
    ts.RunTo(1000);
    CHECK_EQ_I(Report(ts).channels[1].activeVoices, 1);
    ts.Send(1000, {0xB1, 121, 0}); // reset all controllers lifts the pedal
    ts.RunTo(2000);
    CHECK_EQ_I(Report(ts).channels[1].activeVoices, 0);
}

TEST(Chart, ResetAllControllers)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0xB0, 1, 100, 0xB0, 11, 20, 0xE0, 0, 0, 0xB0, 7, 50, 0xB0, 10, 0, 0xB0, 121, 0});
    ts.RunTo(32);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].expression, 127);
    CHECK_EQ_I(r.channels[0].pitchBend, 0x2000);
    CHECK_EQ_I(r.channels[0].volume, 50); // volume and pan are kept (RP-015)
    CHECK_EQ_I(r.channels[0].pan, 0);
}

TEST(Chart, MonoMode)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0xB0, 126, 0, 0x90, 60, 100, 0x90, 62, 100});
    ts.RunTo(1600);
    CHECK_EQ_I(Report(ts).activeVoices, 1);
    ts.Send(1600, {0xB0, 127, 0, 0x90, 60, 100, 0x90, 64, 100});
    ts.RunTo(1700);
    CHECK_EQ_I(Report(ts).channels[0].activeVoices, 2);
}

TEST(Chart, MidiReset)
{
    TestSynth ts(BasicBank());
    ts.Send(0, {0xC0, 1, 0xB0, 7, 20, 0x90, 60, 100, 0xB0, 99, 0x37, 0xB0, 98, 0x5F, 0xB0, 6, 0});
    ts.Send(320, {0xFF});
    ts.RunTo(640);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].program, 0);
    CHECK_EQ_I(r.channels[0].volume, 100);
    CHECK_EQ_I(r.activeVoices, 0);
    CHECK_EQ_I(r.effectsWord, kPowerUpEffectsWord);
}

TEST(Chart, ResetAll45h)
{
    // NRPN 375Fh = 45h restores power-up and stops the firmware ~50 ms
    TestSynth ts(BasicBank());
    ts.Send(0, {0xC0, 1, 0xB0, 99, 0x37, 0xB0, 98, 0x5F, 0xB0, 6, 0x00});
    ts.Send(100, {0xB0, 6, 0x45});
    ts.Send(1000, {0x90, 60, 100});
    ts.Send(2000, {0x90, 60, 100});
    ts.RunTo(2400);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.effectsWord, kPowerUpEffectsWord);
    CHECK_EQ_I(r.polyphonyLimit, 38);
    CHECK_EQ_I(r.channels[0].program, 0);
    CHECK_EQ_I(r.bytesDroppedBusy, 3);
    CHECK_EQ_I(r.activeVoices, 1);
}

TEST(Chart, RpnTuning)
{
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto freq = [&bank](std::initializer_list<int> setup) {
        TestSynth ts(bank);
        ts.Send(0, setup);
        ts.Send(0, {0x90, 69, 100});
        ts.RunTo(12000);
        return ts.FrequencyL(1000, 12000);
    };
    // RPN 1 fine tuning: 7Fh = +(63/64) x 100 cents; RPN 2 coarse: 40h + 12 = one octave
    CHECK_NEAR(freq({0xB0, 101, 0, 0xB0, 100, 1, 0xB0, 6, 0x7F}), 375.0 * std::exp2(63.0 / 64.0 / 12.0), 0.02);
    CHECK_NEAR(freq({0xB0, 101, 0, 0xB0, 100, 2, 0xB0, 6, 0x40 + 12}), 750.0, 0.02);
    // the null RPN 7F 7F ignores data entry
    CHECK_NEAR(freq({0xB0, 101, 127, 0xB0, 100, 127, 0xB0, 6, 0x50}), 375.0, 0.01);
}

TEST(Chart, ModulationWheelAndAftertouch)
{
    // The chip's GS controller matrix replaces the SF2 default modulators 8.4.3 / 8.4.4: the wheel drives
    // LFO1 (the vibrato LFO) by "Mod LFO1 pitch depth" 0Ah = 47.2 cents; channel aftertouch does nothing
    // until 40 2p 2x gives it a destination (datasheet p.26, p.29)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto swing = [&bank](std::initializer_list<int> setup, bool cafDepth = false) {
        TestSynth ts(bank);
        if (cafDepth)
            ts.Gs(0, 0x40, 0x21, 0x24, {0x0A}); // CAF LFO1 pitch depth = the wheel's default
        ts.Send(0, setup);
        ts.Send(0, {0x90, 69, 100});
        ts.RunTo(37500 / 2);
        double lo = 1e9, hi = 0;
        for (size_t from = 1000; from + 1000 < ts.Frames(); from += 400)
        {
            const double f = ts.FrequencyL(from, from + 400);
            lo = std::fmin(lo, f);
            hi = std::fmax(hi, f);
        }
        return 1200.0 * std::log2(hi / lo);
    };
    CHECK(swing({}) < 1.0);
    const double wheel = swing({0xB0, 1, 127});
    CHECK(wheel > 80.0 && wheel < 95.0);   // +-47.2 cents, sampled per 400 frames
    CHECK(swing({0xD0, 127}) < 1.0);       // aftertouch: no default destination
    CHECK_NEAR(swing({0xD0, 127}, true), wheel, 2.0);
}

// ---- golden fingerprints (synthetic bank, exact repeatability within a build) ----

TEST(Golden, ScaleAndDrums)
{
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto render = [&bank](Interpolation mode) {
        SynthConfig cfg;
        cfg.interpolation = mode;
        auto ts = std::make_unique<TestSynth>(bank, cfg);
        for (int i = 0; i < 8; i++)
        {
            const uint64_t t = 1000 + i * 1500;
            ts->Send(t, {0x90, 60 + i * 2, 70 + i * 6});
            ts->Send(t + 1200, {0x80, 60 + i * 2, 0});
            ts->Send(t + 700, {0x99, i % 2 ? 42 : 46, 100});
        }
        ts->Send(5000, {0xE0, 0x00, 0x60, 0xB0, 1, 90});
        ts->RunTo(16384);
        return ts;
    };
    auto a = render(Interpolation::Sinc);
    auto b = render(Interpolation::Sinc);
    CHECK(a->out == b->out); // bit-exact repeat
    CheckGolden("ScaleAndDrums", *a,
                {-200.00, -27.07, -20.71, -20.33, -18.80, -19.98, -19.10, -18.48,
                 -18.84, -18.40, -17.56, -17.34, -18.98, -20.52, -20.94, -20.87});
    CheckGolden("ScaleAndDrumsCubic", *render(Interpolation::Cubic),
                {-200.00, -27.07, -20.71, -20.33, -18.80, -19.98, -19.10, -18.48,
                 -18.84, -18.40, -17.56, -17.34, -18.98, -20.52, -20.94, -20.87});
    CheckGolden("ScaleAndDrumsLinear", *render(Interpolation::Linear),
                {-200.00, -27.08, -20.71, -20.33, -18.80, -19.98, -19.10, -18.48,
                 -18.85, -18.40, -17.56, -17.35, -18.98, -20.52, -20.94, -20.87});
}

TEST(Golden, ModulatedVoice)
{
    // every SAM-2 voice element at once: mod envelope to filter and pitch, resonant filter, mod LFO to
    // pitch / filter / volume, vibrato LFO, key-scaled decay, a bank modulator (CC 2 -> cutoff)
    Sf2Builder b;
    const int s = b.AddSample(SineSample("saw-ish", 50, 16));
    BPreset p = SimplePreset("Rich", 0, 0, s,
                             {{G(Gen::SampleModes), 1},
                              {G(Gen::InitialFilterFc), 7000},
                              {G(Gen::InitialFilterQ), 150},
                              {G(Gen::ModEnvToFilterFc), 3000},
                              {G(Gen::ModEnvToPitch), 50},
                              {G(Gen::AttackModEnv), -3000},
                              {G(Gen::DecayModEnv), -1200},
                              {G(Gen::SustainModEnv), 600},
                              {G(Gen::ModLfoToPitch), 20},
                              {G(Gen::ModLfoToFilterFc), 500},
                              {G(Gen::ModLfoToVolume), 30},
                              {G(Gen::FreqModLfo), 1200},
                              {G(Gen::VibLfoToPitch), 15},
                              {G(Gen::DelayVibLfo), -3000},
                              {G(Gen::DecayVolEnv), -1000},
                              {G(Gen::SustainVolEnv), 200},
                              {G(Gen::KeynumToVolEnvDecay), 50},
                              {G(Gen::ReleaseVolEnv), -2400}});
    BZone& z = p.zones[0];
    z.mods.push_back(ModulatorDef{0x0082, static_cast<uint16_t>(Gen::InitialFilterFc), -2000, 0, 0});
    b.presets.push_back(p);
    std::shared_ptr<const ISoundBank> bank = b.Load();
    TestSynth ts(bank);
    ts.Send(0, {0x90, 57, 110, 0x90, 64, 90});
    ts.Send(6000, {0xB0, 2, 100});
    ts.Send(9000, {0x80, 57, 0, 0x80, 64, 0});
    ts.RunTo(16384);
    CheckGolden("ModulatedVoice", ts,
                {-20.61, -28.87, -33.17, -38.43, -42.42, -41.49, -37.90, -37.90,
                 -39.23, -49.80, -77.37, -84.89, -96.01, -109.85, -113.64, -135.12});
}

TEST(Golden, EffectsChain)
{
    // the chip's whole output path at power-up (effects word 3Bh: reverb hall2, chorus3, spatial at
    // volume 0, the 4-band EQ with +6 dB shelves, soft clipping): notes with reverb and chorus sends, a
    // noise burst, a drum, a program change of both effects mid-way; bit-exact repeat
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto render = [&bank]() {
        auto ts = std::make_unique<TestSynth>(bank, SynthConfig{}, true);
        ts->Send(0, {0xB0, 91, 100, 0xB0, 93, 90, 0xB1, 91, 127, 0xC1, 3, 0xB0, 10, 20, 0xB1, 10, 108});
        ts->Send(1000, {0x90, 64, 100, 0x91, 69, 90});
        ts->Send(3000, {0x81, 69, 0, 0x99, 36, 110});
        ts->Send(6000, {0x80, 64, 0, 0x89, 36, 0});
        ts->Send(8000, {0xB0, 80, 5, 0xB0, 81, 5, 0x90, 57, 100});
        ts->Send(11000, {0x80, 57, 0});
        ts->RunTo(16384);
        return ts;
    };
    auto a = render();
    auto b = render();
    CHECK(a->out == b->out);
    CheckGolden("EffectsChain", *a,
                {-200.00, -14.77, -14.47, -10.95, -11.05, -11.49, -35.98, -23.60,
                 -15.17, -15.32, -16.57, -34.15, -33.72, -34.52, -34.16, -32.27});
}
