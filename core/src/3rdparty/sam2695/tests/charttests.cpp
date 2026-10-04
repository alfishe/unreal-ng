// libsam2695 tests - the MIDI implementation chart rows built in SAM-3 (datasheet §2-1, §3, p.15-29):
// GM / GS / Dream SysEx, the GS part parameters, the GS part and drum NRPNs, the chip NRPNs that are
// not effects, the pedals, portamento, mode messages, layering and bank select. One test per row (the
// conformance table in README.md names them). Rendering is the dry mode unless a test says otherwise.
#include "channel/channel.h"
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

std::shared_ptr<const Sf2Bank> SinglePreset(BSample s, std::map<int, int> gens)
{
    Sf2Builder b;
    b.AddSample(s);
    b.presets.push_back(SimplePreset("P", 0, 0, 0, std::move(gens)));
    return b.Load();
}

// RMS of the left channel over [from, to) after `setup` and a note at 0 (program 1 = DC, or the sine)
double Level(const std::shared_ptr<const ISoundBank>& bank, const std::function<void(TestSynth&)>& setup,
             std::initializer_list<int> note, size_t from = 2000, size_t to = 3000, bool right = false)
{
    TestSynth ts(bank);
    setup(ts);
    ts.Send(0, note);
    ts.RunTo(to + 64);
    return right ? ts.RmsR(from, to) : ts.RmsL(from, to);
}

double Frequency(const std::shared_ptr<const ISoundBank>& bank, const std::function<void(TestSynth&)>& setup,
                 std::initializer_list<int> note, size_t from = 1000, size_t to = 12000)
{
    TestSynth ts(bank);
    setup(ts);
    ts.Send(0, note);
    ts.RunTo(to + 64);
    return ts.FrequencyL(from, to);
}

// Peak-to-peak frequency swing in cents, sampled every `step` frames
double Swing(const TestSynth& ts, size_t from, size_t to, size_t step = 400)
{
    double lo = 1e9, hi = 0;
    for (size_t a = from; a + step <= to; a += step)
    {
        const double f = ts.FrequencyL(a, a + step);
        lo = std::fmin(lo, f);
        hi = std::fmax(hi, f);
    }
    return 1200.0 * std::log2(hi / lo);
}

const auto kNone = [](TestSynth&) {};

} // namespace

// ---- GM / GS system messages ----

TEST(Chart, GmSystemOn)
{
    // F0 7E 7F 09 01 F7: every part back to GM defaults, sound stops; the chip's own settings (NRPN
    // 37xxh) and the GM master volume stay
    TestSynth ts(BasicBank());
    ts.Send(0, {0xC0, 1, 0xB0, 7, 20, 0x90, 60, 100});
    ts.Nrpn(0, 0, 0x37, 0x07, 0x50);                 // master volume 3707h
    ts.Nrpn(0, 0, 0x37, 0x5F, 0x20);                 // effects word: reverb only
    ts.Send(0, {0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x40, 0xF7});
    ts.Gs(0, 0x40, 0x11, 0x02, {0x05});              // part 1 to channel 6
    ts.Send(320, {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7});
    ts.RunTo(640);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].program, 0);
    CHECK_EQ_I(r.channels[0].volume, 100);
    CHECK_EQ_I(r.channels[0].rxChannel, 0);
    CHECK_EQ_I(r.activeVoices, 0);
    CHECK_EQ_I(r.masterVolume, 0x50);
    CHECK_EQ_I(r.gmVolume, 0x40);
    CHECK_EQ_I(r.effectsWord, 0x20);
    CHECK_EQ_I(r.sysExReceived, 2 + 1);
}

TEST(Chart, GsReset)
{
    // F0 41 10 42 12 40 00 7F 00 41 F7: parts, master tune / key shift / pan, drum edits, reverb and
    // chorus back to their defaults; NRPN 37xxh settings stay
    TestSynth ts(BasicBank());
    ts.Send(0, {0xC2, 1, 0xB0, 80, 0, 0xB0, 81, 5});
    ts.Gs(0, 0x40, 0x00, 0x00, {0x00, 0x07, 0x0E, 0x08}); // master tune +100 cents
    ts.Gs(0, 0x40, 0x00, 0x05, {0x4C});                   // key shift +12
    ts.Gs(0, 0x40, 0x00, 0x06, {0x00});                   // master pan left
    ts.Nrpn(0, 0, 0x37, 0x13, 0x7F);                      // hard clipping
    ts.RunTo(32);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.reverbProgram, 0);
    CHECK_EQ_I(r.chorusProgram, 5);
    CHECK_NEAR(r.masterTuneCents, 100.0, 1e-4);
    CHECK_EQ_I(r.keyShift, 12);
    ts.Gs(64, 0x40, 0x00, 0x7F, {0x00});
    ts.RunTo(128);
    r = Report(ts);
    CHECK_EQ_I(r.channels[2].program, 0);
    CHECK_EQ_I(r.reverbProgram, 4);
    CHECK_EQ_I(r.chorusProgram, 2);
    CHECK_NEAR(r.masterTuneCents, 0.0, 1e-6);
    CHECK_EQ_I(r.keyShift, 0);
    CHECK_EQ_I(r.gmPan, 0x40);
    CHECK(!r.softClip); // NRPN 3713h is the chip's, not GS
}

TEST(Chart, GmMasterVolume)
{
    // F0 7F 7F 04 01 00 ll F7 (datasheet p.27): the GM bus volume, linear like NRPN 3722h
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const double full = Level(bank, kNone, {0xC0, 1, 0x90, 60, 127});
    const double half = Level(bank, [](TestSynth& ts) { ts.Send(0, {0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x40, 0xF7}); },
                              {0xC0, 1, 0x90, 60, 127});
    CHECK_NEAR(half / full, 64.0 / 127.0, 1e-5);
}

TEST(Chart, GmVolumeAndPan)
{
    // NRPN 3722h GM volume (linear) = GS master volume 40 00 04; NRPN 3723h GM pan = GS master pan
    // 40 00 06 ("same as", datasheet p.16), a balance on the GM bus
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const std::initializer_list<int> note = {0xC0, 1, 0x90, 60, 127};
    const double full = Level(bank, kNone, note);
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Nrpn(0, 0, 0x37, 0x22, 0x20); }, note) / full, 32.0 / 127.0, 1e-5);
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x00, 0x04, {0x20}); }, note) / full, 32.0 / 127.0, 1e-5);
    // pan: hard left keeps the left side whole and silences the right; 60h takes half off the left
    auto left = [](TestSynth& ts) { ts.Nrpn(0, 0, 0x37, 0x23, 0x00); };
    CHECK_NEAR(Level(bank, left, note) / full, 1.0, 1e-6);
    CHECK_EQ_I(Level(bank, left, note, 2000, 3000, true), 0);
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x00, 0x06, {0x60}); }, note) / full, 0.5, 1e-6);
}

TEST(Chart, NrpnMasterVolume)
{
    // NRPN 3707h master volume, 0-7Fh linear on the output (power-up 7Fh)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const std::initializer_list<int> note = {0xC0, 1, 0x90, 60, 127};
    const double full = Level(bank, kNone, note);
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Nrpn(0, 3, 0x37, 0x07, 0x40); }, note) / full, 64.0 / 127.0, 1e-5);
    CHECK_EQ_I(Level(bank, [](TestSynth& ts) { ts.Nrpn(0, 0, 0x37, 0x07, 0x00); }, note), 0);
}

TEST(Chart, GsMasterTune)
{
    // 40 00 00 + four nibbles: 00 07 0E 08 = +100.0 cents (datasheet p.27)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const double f = Frequency(bank, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x00, 0x00, {0x00, 0x07, 0x0E, 0x08}); },
                               {0x90, 69, 100});
    CHECK_NEAR(f, 375.0 * std::exp2(100.0 / 1200.0), 0.03);
    const double g = Frequency(bank, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x00, 0x00, {0x00, 0x01, 0x08, 0x00}); },
                               {0x90, 69, 100}); // 0180h = -64.0 cents
    CHECK_NEAR(g, 375.0 * std::exp2(-64.0 / 1200.0), 0.03);
}

TEST(Chart, GsMasterKeyShift)
{
    // 40 00 05: transposes the melodic parts (the key, so the zone too); the drums stay
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto shift = [](TestSynth& ts) { ts.Gs(0, 0x40, 0x00, 0x05, {0x4C}); };
    CHECK_NEAR(Frequency(bank, shift, {0x90, 69, 100}), 750.0, 0.05);
    const double drum = Frequency(bank, kNone, {0x99, 46, 100});
    TestSynth ts(bank);
    shift(ts);
    ts.Send(0, {0x99, 46, 100, 0x90, 57, 100});
    ts.RunTo(320);
    CHECK_EQ_I(Report(ts).channels[9].activeVoices, 1);
    CHECK_NEAR(Frequency(bank, shift, {0x99, 46, 100}), drum, 0.01);
    // note off finds the shifted note by the key it was played with
    ts.Send(320, {0x80, 57, 0});
    ts.RunTo(1600);
    CHECK_EQ_I(Report(ts).channels[0].activeVoices, 0);
}

TEST(Chart, SysExDeviceId)
{
    // NRPN 3757h: 20h accepts every device ID (power-up); 0-1Fh only that one, plus the universal 7Fh
    TestSynth ts(BasicBank());
    ts.Nrpn(0, 0, 0x37, 0x57, 0x10);
    ts.Gs(0, 0x40, 0x00, 0x04, {0x11}, 0x00); // device 00h: ignored
    ts.RunTo(32);
    CHECK_EQ_I(Report(ts).gmVolume, 0x7F);
    CHECK_EQ_I(Report(ts).deviceId, 0x10);
    ts.Gs(32, 0x40, 0x00, 0x04, {0x22}, 0x10); // device 10h: taken
    ts.RunTo(64);
    CHECK_EQ_I(Report(ts).gmVolume, 0x22);
    ts.Send(64, {0xF0, 0x7F, 0x05, 0x04, 0x01, 0x00, 0x33, 0xF7}); // universal, device 5: ignored
    ts.RunTo(96);
    CHECK_EQ_I(Report(ts).gmVolume, 0x22);
    ts.Send(96, {0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x44, 0xF7}); // universal all-call
    ts.RunTo(128);
    CHECK_EQ_I(Report(ts).gmVolume, 0x44);
    // the datasheet's own messages carry device 00h: accepted by default
    TestSynth any(BasicBank());
    any.Gs(0, 0x40, 0x00, 0x04, {0x12}, 0x00);
    any.RunTo(32);
    CHECK_EQ_I(Report(any).gmVolume, 0x12);
}

TEST(Chart, GsChecksumDontCare)
{
    // "x or xx means don't care" (datasheet p.29): a wrong checksum is still applied
    TestSynth ts(BasicBank());
    ts.Send(0, {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x04, 0x33, 0x00, 0xF7});
    ts.RunTo(32);
    CHECK_EQ_I(Report(ts).gmVolume, 0x33);
}

// ---- GS part parameters ----

TEST(Chart, PartChannelAssign)
{
    // 40 1p 02 nn: part p receives channel nn (16 = off); several parts may share a channel. Block 2 is
    // part 2 (index 1), block 0 part 10.
    TestSynth ts(BasicBank());
    ts.Gs(0, 0x40, 0x12, 0x02, {0x00});
    ts.Send(0, {0x90, 60, 100, 0x91, 62, 100});
    ts.RunTo(320);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[1].rxChannel, 0);
    CHECK_EQ_I(r.channels[0].activeVoices, 1);
    CHECK_EQ_I(r.channels[1].activeVoices, 1); // the channel 0 note; channel 1 reaches no part now
    CHECK_EQ_I(r.activeVoices, 2);
    ts.Send(320, {0xB0, 120, 0}); // all sound off reaches both parts
    ts.Gs(320, 0x40, 0x11, 0x02, {0x10});
    ts.Gs(320, 0x40, 0x12, 0x02, {0x10});
    ts.Send(400, {0x90, 60, 100});
    ts.RunTo(800);
    r = Report(ts);
    CHECK_EQ_I(r.channels[0].rxChannel, kPartOff);
    CHECK_EQ_I(r.activeVoices, 0);
}

TEST(Chart, PartRhythmAllocation)
{
    // 40 1p 15 vv: a part plays sounds (0) or a drum set (1); any number of rhythm parts
    TestSynth ts(BasicBank());
    ts.Gs(0, 0x40, 0x12, 0x15, {0x01}); // part 2 (channel 2) plays drums
    ts.Gs(0, 0x40, 0x10, 0x15, {0x00}); // part 10 (channel 10) plays sounds
    ts.Send(0, {0x91, 36, 100, 0x99, 60, 100, 0xC9, 1});
    ts.RunTo(320);
    const SynthReport r = Report(ts);
    CHECK(r.channels[1].rhythm);
    CHECK_EQ_I(r.channels[1].preset, 4);
    CHECK_EQ_I(r.channels[1].activeVoices, 1);
    CHECK(!r.channels[9].rhythm);
    CHECK_EQ_I(r.channels[9].preset, 1);
    CHECK_EQ_I(r.channels[9].activeVoices, 1);
}

TEST(Chart, VoiceReserve)
{
    // 40 01 10 + 16 bytes (part 10, parts 1-9, parts 11-16; defaults 2 / 2 / 0): a part within its reserve
    // keeps its voices when another part needs one
    SynthConfig cfg;
    cfg.polyphony = 4;
    TestSynth ts(BasicBank(), cfg);
    ts.Send(0, {0xC0, 2, 0x90, 60, 20});                                  // part 1: oldest, quietest
    ts.Send(32, {0xCA, 2, 0x9A, 60, 120, 0x9A, 62, 120, 0x9A, 64, 120}); // part 11: reserve 0
    ts.RunTo(320);
    CHECK_EQ_I(Report(ts).activeVoices, 4);
    ts.Send(320, {0xCB, 2, 0x9B, 60, 120});
    ts.RunTo(640);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].activeVoices, 1);  // protected by its reserve
    CHECK_EQ_I(r.channels[10].activeVoices, 2);
    CHECK_EQ_I(r.channels[0].voiceReserve, 2);
    // reserves all 0: the quietest, oldest voice goes again
    ts.Gs(640, 0x40, 0x01, 0x10, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    ts.Send(640, {0xCC, 2, 0x9C, 60, 120});
    ts.RunTo(960);
    r = Report(ts);
    CHECK_EQ_I(r.channels[0].activeVoices, 0);
    CHECK_EQ_I(r.channels[0].voiceReserve, 0);
}

TEST(Chart, ScaleTuning)
{
    // 40 1p 40 + 12 values, cents + 40h for C .. B; no effect on a rhythm part
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto tune = [](TestSynth& ts) {
        ts.Gs(0, 0x40, 0x11, 0x40, {0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40 + 30, 0x40, 0x40});
        ts.Gs(0, 0x40, 0x10, 0x40, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    };
    CHECK_NEAR(Frequency(bank, tune, {0x90, 69, 100}), 375.0 * std::exp2(30.0 / 1200.0), 0.03); // A: +30
    CHECK_NEAR(Frequency(bank, tune, {0x90, 71, 100}), 375.0 * std::exp2(2.0 / 12.0), 0.03);     // B: as is
    CHECK_NEAR(Frequency(bank, tune, {0x99, 46, 100}), Frequency(bank, kNone, {0x99, 46, 100}), 0.01);
}

TEST(Chart, VelocitySense)
{
    // 40 1p 1A depth / 1B offset: velocity x depth / 40h + (offset - 40h), within 1-127
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const double v127 = Level(bank, kNone, {0xC0, 1, 0x90, 60, 127});
    const double v116 = Level(bank, kNone, {0xC0, 1, 0x90, 60, 116});
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x11, 0x1A, {0x7F}); }, {0xC0, 1, 0x90, 60, 64}) / v127,
               1.0, 1e-6);
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Gs(0, 0x40, 0x11, 0x1B, {0x50}); }, {0xC0, 1, 0x90, 60, 100}) / v116,
               1.0, 1e-6);
}

TEST(Chart, ControllerDestinations)
{
    // 40 2p 0x (mod), 1x (bend), 2x (CAF): pitch / TVF / amplitude / LFO1 rate / LFO1 depths
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    // mod pitch control +12 semitones at full wheel (LFO1 depth off so the pitch is steady)
    CHECK_NEAR(Frequency(bank,
                         [](TestSynth& ts) {
                             ts.Gs(0, 0x40, 0x21, 0x00, {0x4C});
                             ts.Gs(0, 0x40, 0x21, 0x04, {0x00});
                             ts.Send(0, {0xB0, 1, 127});
                         },
                         {0x90, 69, 100}),
               750.0, 0.05);
    // bend pitch control = the bend range: 4Ch = 12 semitones, full bend up = +12 x 8191/8192
    CHECK_NEAR(Frequency(bank,
                         [](TestSynth& ts) {
                             ts.Gs(0, 0x40, 0x21, 0x10, {0x4C});
                             ts.Send(0, {0xE0, 0x7F, 0x7F});
                         },
                         {0x90, 69, 100}),
               375.0 * std::exp2(8191.0 / 8192.0), 0.05);
    // CAF amplitude -100 %: full pressure silences the part
    CHECK_EQ_I(Level(bank,
                     [](TestSynth& ts) {
                         ts.Gs(0, 0x40, 0x21, 0x22, {0x00});
                         ts.Send(0, {0xD0, 127});
                     },
                     {0xC0, 1, 0x90, 60, 127}),
               0);
    // CAF TVF cutoff: -150 cents per step: 00h = -9600 cents darkens a 375 Hz tone
    const double open = Level(bank, kNone, {0x90, 69, 127}, 2000, 6000);
    const double dark = Level(bank,
                              [](TestSynth& ts) {
                                  ts.Gs(0, 0x40, 0x21, 0x21, {0x00});
                                  ts.Send(0, {0xD0, 127});
                              },
                              {0x90, 69, 127}, 2000, 6000);
    CHECK(dark < open * 0.1);
    // LFO1 TVA depth from the wheel, and the wheel's LFO1 rate control (+10 Hz at 7Fh): count the
    // amplitude dips of a DC tone over one second
    auto dips = [&bank](bool faster) {
        TestSynth ts(bank);
        ts.Gs(0, 0x40, 0x21, 0x06, {0x7F});
        ts.Gs(0, 0x40, 0x21, 0x04, {0x00});
        if (faster)
            ts.Gs(0, 0x40, 0x21, 0x03, {0x7F});
        ts.Send(0, {0xB0, 1, 127, 0xC0, 1, 0x90, 60, 127});
        ts.RunTo(39500);
        const double top = ts.PeakL(2000, 39500);
        int n = 0;
        bool low = false;
        for (size_t i = 2000; i < 39500; i += 32)
        {
            const bool now = ts.L(i) < 0.5 * top;
            if (now && !low)
                n++;
            low = now;
        }
        return n;
    };
    CHECK_NEAR(dips(false), 8, 1);  // the vibrato LFO's SF2 default, 8.18 Hz
    CHECK_NEAR(dips(true), 18, 1);  // + 10 Hz
}

TEST(Chart, AssignableControllers)
{
    // CC1 / CC2 numbers (40 1p 1F / 20, defaults 10h / 11h) and their destinations (40 2p 4x / 5x)
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    CHECK_NEAR(Frequency(bank,
                         [](TestSynth& ts) {
                             ts.Gs(0, 0x40, 0x21, 0x40, {0x4C});
                             ts.Send(0, {0xB0, 16, 127});
                         },
                         {0x90, 69, 100}),
               750.0, 0.05);
    CHECK_NEAR(Frequency(bank,
                         [](TestSynth& ts) {
                             ts.Gs(0, 0x40, 0x21, 0x50, {0x34}); // CC2 pitch -12
                             ts.Send(0, {0xB0, 17, 127});
                         },
                         {0x90, 69, 100}),
               187.5, 0.05);
    // CC1 moved to controller 20: controller 16 no longer acts
    auto moved = [](int cc) {
        return [cc](TestSynth& ts) {
            ts.Gs(0, 0x40, 0x11, 0x1F, {20});
            ts.Gs(0, 0x40, 0x21, 0x40, {0x4C});
            ts.Send(0, {0xB0, cc, 127});
        };
    };
    CHECK_NEAR(Frequency(bank, moved(16), {0x90, 69, 100}), 375.0, 0.05);
    CHECK_NEAR(Frequency(bank, moved(20), {0x90, 69, 100}), 750.0, 0.05);
}

// ---- GS part NRPNs 01xxh (relative, 40h = no change) ----

TEST(Chart, PartNrpnVibrato)
{
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto render = [&bank](std::initializer_list<std::pair<int, int>> nrpns) {
        auto ts = std::make_unique<TestSynth>(bank);
        for (auto [lsb, v] : nrpns)
            ts->Nrpn(0, 0, 0x01, lsb, v);
        ts->Send(0, {0x90, 69, 100});
        ts->RunTo(40000);
        return ts;
    };
    CHECK(Swing(*render({}), 2000, 20000) < 1.0);
    // 0109h depth +40 cents on the vibrato LFO (8.18 Hz by default)
    const double depth = Swing(*render({{0x09, 0x40 + 40}}), 2000, 20000);
    CHECK(depth > 65.0 && depth < 81.0);
    // 010Ah delay 7Fh: from at least 2^-5 s, + 63 x 75 timecents = 0.48 s without vibrato
    auto late = render({{0x09, 0x40 + 40}, {0x0A, 0x7F}});
    CHECK(Swing(*late, 1200, 16000) < 1.0);
    CHECK(Swing(*late, 22000, 38000) > 60.0);
    // 0108h rate: +20 cents per step; 7Fh = x 2.07: the frequency swings twice as often
    auto crossings = [](const TestSynth& ts) {
        int n = 0;
        const double mid = 375.0;
        bool above = false;
        for (size_t a = 2000; a + 200 <= 39000; a += 200)
        {
            const bool now = ts.FrequencyL(a, a + 200) > mid;
            if (now && !above)
                n++;
            above = now;
        }
        return n;
    };
    const int slow = crossings(*render({{0x09, 0x40 + 40}}));
    const int fast = crossings(*render({{0x09, 0x40 + 40}, {0x08, 0x7F}}));
    CHECK_NEAR(static_cast<double>(fast) / slow, std::exp2(63.0 * 20.0 / 1200.0), 0.25);
}

TEST(Chart, PartNrpnFilter)
{
    // 0120h cutoff (60 cents per step) and 0121h resonance (3 cB per step) on a filter tuned to the tone
    const int fc = static_cast<int>(std::lround(1200.0 * std::log2(375.0 / 8.17579891564)));
    std::shared_ptr<const ISoundBank> bank =
        SinglePreset(SineSample("s"), {{G(Gen::SampleModes), 1}, {G(Gen::InitialFilterFc), fc + 2400}});
    auto level = [&bank](int cutoff, int resonance) {
        return Level(bank,
                     [&](TestSynth& ts) {
                         ts.Nrpn(0, 0, 0x01, 0x20, cutoff);
                         ts.Nrpn(0, 0, 0x01, 0x21, resonance);
                     },
                     {0x90, 69, 127}, 3000, 7000);
    };
    const double open = level(0x40, 0x40);
    // -40 steps = -2400 cents: the cutoff sits on the tone, Butterworth: -3 dB
    CHECK_NEAR(20.0 * std::log10(level(0x40 - 40, 0x40) / open), -3.01, 0.15);
    // resonance +63 steps = 189 cB: the peak height on the tone, less the 1/sqrt(q) DC compensation
    const double q = std::pow(10.0, (18.9 - 3.01) / 20.0);
    CHECK_NEAR(20.0 * std::log10(level(0x40 - 40, 0x7F) / open), 20.0 * std::log10(std::sqrt(q)), 0.2);
}

TEST(Chart, PartNrpnEnvelope)
{
    // 0163h / 0164h / 0166h: attack / decay / release, 75 timecents per step; a lengthening starts from
    // at least 2^-5 s
    std::shared_ptr<const ISoundBank> bank = SinglePreset(
        DcSample("dc"), {{G(Gen::SampleModes), 1}, {G(Gen::DecayVolEnv), 0}, {G(Gen::SustainVolEnv), 600},
                         {G(Gen::ReleaseVolEnv), 0}});
    auto run = [&bank](int lsb, int value, uint64_t off) {
        auto ts = std::make_unique<TestSynth>(bank);
        ts->Nrpn(0, 0, 0x01, lsb, value);
        ts->Send(0, {0x90, 60, 127});
        ts->Send(off, {0x80, 60, 0});
        ts->RunTo(off + 9000);
        return ts;
    };
    // the unmodified note: 37 samples each of delay, attack and hold (SF2 defaults), then the decay
    auto plain = run(0x64, 0x40, 30000);
    const double peak = plain->L(100);
    CHECK_NEAR(20.0 * std::log10(plain->L(3787) / peak), -(3787.0 - 111.0) / kInternalRate * 100.0, 0.05);
    // attack 7Fh: 2^((-6000 + 63 x 75) / 1200) = 0.479 s, linear in amplitude
    auto slow = run(0x63, 0x7F, 30000);
    const double attackSamples = std::exp2((-6000.0 + 63 * 75) / 1200.0) * kInternalRate;
    CHECK_NEAR(slow->L(37 + 3750) / peak, 3750.0 / attackSamples, 0.005);
    // decay 0: 2^(-4800 / 1200) s = 62.5 ms per 100 dB: the -60 dB sustain is reached by 0.1 s
    auto fast = run(0x64, 0x00, 30000);
    CHECK_NEAR(20.0 * std::log10(fast->L(3787) / peak), -60.0, 0.05);
    // release 0: 62.5 ms instead of 1 s per 100 dB
    auto shortRel = run(0x66, 0x00, 3000);
    CHECK_EQ_I(shortRel->L(3000 + 2400), 0);
    CHECK(plain->L(30000 + 2400) > 0.0f);
}

TEST(Chart, DrumNrpn)
{
    // 18rr pitch (semitones), 1Arr level, 1Crr pan of drum key rr, on channel 10's table
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const std::initializer_list<int> drum = {0x99, 46, 127};
    const double base = Frequency(bank, kNone, drum);
    CHECK_NEAR(Frequency(bank, [](TestSynth& ts) { ts.Nrpn(0, 9, 0x18, 46, 0x4C); }, drum), 2.0 * base, 0.02);
    const double full = Level(bank, kNone, drum);
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Nrpn(0, 9, 0x1A, 46, 0x40); }, drum) / full,
               (64.0 / 127.0) * (64.0 / 127.0), 1e-4);
    CHECK_EQ_I(Level(bank, [](TestSynth& ts) { ts.Nrpn(0, 9, 0x1C, 46, 0x00); }, drum, 2000, 3000, true), 0);
    // other keys untouched; an edit received on another channel goes to the other table
    CHECK_NEAR(Frequency(bank, [](TestSynth& ts) { ts.Nrpn(0, 9, 0x18, 42, 0x4C); }, drum), base, 0.01);
    CHECK_NEAR(Frequency(bank, [](TestSynth& ts) { ts.Nrpn(0, 3, 0x18, 46, 0x4C); }, drum), base, 0.01);
    CHECK_NEAR(Frequency(bank,
                         [](TestSynth& ts) {
                             ts.Gs(0, 0x40, 0x14, 0x15, {0x01}); // part 4 (channel 4) plays drums
                             ts.Nrpn(0, 3, 0x18, 46, 0x4C);
                         },
                         {0x93, 46, 127}),
               2.0 * base, 0.02);
}

// ---- pedals, portamento, modes ----

TEST(Chart, Sostenuto)
{
    // CC 66 holds the notes sounding when it goes down, and only those
    TestSynth ts(BasicBank());
    ts.Send(0, {0xC0, 2, 0x90, 60, 100});
    ts.Send(320, {0xB0, 66, 127, 0x90, 64, 100});
    ts.Send(640, {0x80, 60, 0, 0x80, 64, 0});
    ts.RunTo(960);
    CHECK_EQ_I(Report(ts).activeVoices, 2); // 64 in its release, 60 held
    ts.RunTo(40000);
    CHECK_EQ_I(Report(ts).activeVoices, 1); // 64 has died (1 s release)
    // the damper also down: lifting sostenuto keeps it until the damper goes up
    ts.Send(40000, {0xB0, 64, 127, 0xB0, 66, 0});
    ts.RunTo(40320);
    CHECK_EQ_I(Report(ts).activeVoices, 1);
    ts.Send(40320, {0xB0, 64, 0});
    ts.RunTo(80000);
    CHECK_EQ_I(Report(ts).activeVoices, 0);
}

TEST(Chart, SoftPedal)
{
    // CC 67: a note started under the pedal is 3 dB softer and an octave darker; a held note is not
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    const double normal = Level(bank, kNone, {0xC0, 1, 0x90, 60, 127});
    CHECK_NEAR(Level(bank, [](TestSynth& ts) { ts.Send(0, {0xB0, 67, 127}); }, {0xC0, 1, 0x90, 60, 127}) / normal,
               std::pow(10.0, -30.0 / 200.0), 1e-4);
    TestSynth ts(bank);
    ts.Send(0, {0xC0, 1, 0x90, 60, 127});
    ts.Send(1000, {0xB0, 67, 127});
    ts.RunTo(3100);
    CHECK_NEAR(ts.RmsL(2000, 3000) / normal, 1.0, 1e-6);
}

TEST(Chart, Portamento)
{
    // CC 65 on: a note glides from the part's last key; CC 5 = 64 is 0.119 s per octave
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    auto run = [&bank](bool on) {
        auto ts = std::make_unique<TestSynth>(bank);
        ts->Send(0, {0xB0, 5, 64, 0xB0, 65, on ? 127 : 0, 0x90, 57, 100});
        ts->Send(3000, {0x80, 57, 0, 0x90, 69, 100});
        ts->RunTo(16000);
        return ts;
    };
    auto glide = run(true);
    const double octave = 0.005 * std::exp2(64.0 / 14.0);
    // mid-glide: at 25 ms after the second note (+1 ms delay) the pitch is 1200 x 24 ms / octave time short
    const double f = glide->FrequencyL(3037 + 750, 3037 + 1125);
    CHECK_NEAR(1200.0 * std::log2(f / 375.0), -1200.0 + 1200.0 * (0.025 / octave), 25.0);
    CHECK_NEAR(glide->FrequencyL(3037 + 6000, 15000), 375.0, 0.02); // arrived
    CHECK_NEAR(run(false)->FrequencyL(3037 + 750, 3037 + 1125), 375.0, 0.5);
}

TEST(Chart, OmniModeMessages)
{
    // CC 124 / 125 (omni off / on) are not in the chip's chart: they act as All Notes Off only (MIDI
    // 1.0); the chip stays in Omni Off / Poly
    TestSynth ts(BasicBank());
    ts.Send(0, {0x90, 60, 100, 0x90, 62, 100});
    ts.Send(320, {0xB0, 125, 0});
    ts.RunTo(1600);
    CHECK_EQ_I(Report(ts).activeVoices, 0);
    ts.Send(1600, {0x90, 60, 100, 0x90, 64, 100, 0x91, 60, 100});
    ts.RunTo(1700);
    const SynthReport r = Report(ts);
    CHECK_EQ_I(r.channels[0].activeVoices, 2); // still poly
    CHECK_EQ_I(r.channels[1].activeVoices, 1); // still omni off: channel 1 is part 1
}

TEST(Chart, TwoLayerInstrument)
{
    // a 2-layer instrument (datasheet §8-1) takes two voices: one per bank zone on the key
    Sf2Builder b;
    const int s = b.AddSample(SineSample("s"));
    const int d = b.AddSample(DcSample("d"));
    BPreset p = SimplePreset("Layered", 0, 0, s);
    BZone second;
    second.sample = d;
    p.zones.push_back(second);
    b.presets.push_back(p);
    SynthConfig cfg;
    cfg.polyphony = 3;
    TestSynth ts(b.Load(), cfg);
    ts.Send(0, {0x90, 60, 100});
    ts.RunTo(320);
    CHECK_EQ_I(Report(ts).activeVoices, 2);
    ts.Send(320, {0x90, 64, 100}); // needs two: one is stolen from the first note
    ts.RunTo(640);
    SynthReport r = Report(ts);
    CHECK_EQ_I(r.activeVoices, 3);
    CHECK_EQ_I(r.voicesStolen, 1);
    ts.Send(640, {0x80, 64, 0, 0x80, 60, 0});
    ts.RunTo(1600);
    CHECK_EQ_I(Report(ts).activeVoices, 0);
}

TEST(Chart, BankSelectLsbIgnored)
{
    // the chip's bank select is CC 0 alone ("Bank select: refer to sounds list", p.26): CC 32 is ignored
    Sf2Builder b;
    const int s = b.AddSample(SineSample("s"));
    b.presets.push_back(SimplePreset("Capital", 0, 6, s));
    b.presets.push_back(SimplePreset("Mt32", 127, 6, s));
    TestSynth ts(b.Load());
    ts.Send(0, {0xB0, 0, 127, 0xB0, 32, 5, 0xC0, 6});
    ts.RunTo(32);
    CHECK_EQ_I(Report(ts).channels[0].preset, 1);
    CHECK_EQ_I(Report(ts).channels[0].bankMsb, 127);
}
