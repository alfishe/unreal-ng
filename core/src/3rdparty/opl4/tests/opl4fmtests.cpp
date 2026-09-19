// opl4fmtests — FM engine behaviour through FmBus, asserted against absolute
// expectations: pitch from the F-number formula, envelope timing and levels
// measured to agree with ymfm and Nuked-OPL3 during development (the numbers
// are kept here as constants; the reference engines are not part of this
// library), and the register semantics real MoonBlaster/MFM tunes rely on.
#include "testfw.h"
#include "fm/fmbus.h"

#include <cmath>
#include <vector>

namespace opl4test
{

namespace
{

struct Rig
{
    Opl4Fm engine;
    FmBus bus;

    Rig()
    {
        engine.Reset();
        bus.SetSynth(&engine);
        bus.Reset();
    }

    void Write(int bank, uint8_t reg, uint8_t data) { bus.Write(static_cast<uint8_t>(bank), reg, data); }

    // One FM step (49516.4 Hz): left + right.
    int32_t Step()
    {
        std::array<int32_t, 18> taps;
        int32_t l = 0, r = 0;
        bus.Advance(taps, l, r);
        return l + r;
    }

    void Run(std::vector<int32_t>& out, size_t n)
    {
        for (size_t i = 0; i < n; i++)
            out.push_back(Step());
    }
};

constexpr double kFmRate = static_cast<double>(kMasterClockHz) / static_cast<double>(kFmDivider);

double FnumHz(int fnum, int block)
{
    return fnum * std::pow(2.0, block) * kFmRate / 1048576.0;
}

double RmsRange(const std::vector<int32_t>& v, size_t from, size_t n)
{
    double acc = 0;
    for (size_t i = from; i < from + n; i++)
        acc += static_cast<double>(v[i]) * v[i];
    return std::sqrt(acc / static_cast<double>(n));
}

// hf = sqrt(diff energy / energy) (white noise ~ sqrt 2), zc = sign changes
// per sample: the character metrics the MFM guest investigations used.
void Character(const std::vector<int32_t>& v, double& rms, double& hf, double& zc)
{
    double e = 0, d = 0;
    long long crossings = 0;
    for (size_t i = 0; i < v.size(); i++)
    {
        e += static_cast<double>(v[i]) * v[i];
        if (i > 0)
        {
            d += static_cast<double>(v[i] - v[i - 1]) * (v[i] - v[i - 1]);
            crossings += (v[i - 1] < 0) != (v[i] < 0) ? 1 : 0;
        }
    }
    rms = std::sqrt(e / static_cast<double>(v.size()));
    hf = std::sqrt(d / (e + 1e-30));
    zc = static_cast<double>(crossings) / static_cast<double>(v.size());
}

// Classic-map carrier on ch0 (silent modulator): pitch from the F-number
// formula, audible level, and a release that dies away after key-off.
void TestClassicVoicePitchAndRelease()
{
    std::printf("FmTests: classic-map carrier pitch and release\n");
    Rig rig;
    rig.Write(1, 0x05, 0x01);
    const uint8_t w[][2] = {{0x20, 0x01}, {0x23, 0x01}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xF0}, {0x63, 0xF0},
                            {0x80, 0x00}, {0x83, 0x04}, {0xA0, 0x03}, {0xB0, 0x2B}, {0xC0, 0x30}};
    for (const auto& x : w)
        rig.Write(0, x[0], x[1]);
    std::vector<int32_t> v;
    rig.Run(v, 20000);
    double rms, hf, zc;
    Character(std::vector<int32_t>(v.begin() + 2000, v.end()), rms, hf, zc);
    const double want = 2.0 * FnumHz(0x303, 2) / kFmRate;
    std::printf("  zc/sample %.5f (formula %.5f), rms %.0f\n", zc, want, rms);
    CHECK(std::fabs(zc / want - 1.0) < 0.01);
    CHECK(rms > 1000);

    rig.Write(0, 0xB0, 0x0B); // key off, RR 4
    std::vector<int32_t> tail;
    rig.Run(tail, 60000);
    const double early = RmsRange(tail, 0, 2000), late = RmsRange(tail, 58000, 2000);
    std::printf("  release: %.0f -> %.0f\n", early, late);
    CHECK(early > 0.5 * rms);
    CHECK(late < 0.05 * early);
}

// CRYOGENT (MFM sample 2) 4-op drum: master ch0 + slave ch3, algorithm 11
// (O1 + O3 + O4), FB7 op1 over a TL5/DR1 op4 tail - the guest's exact bytes.
// muteOps bit n silences op n+1 (TL max).
void PatchDrumVoice(Rig& rig, bool pair, uint8_t muteOps)
{
    rig.Write(1, 0x05, 0x01);
    if (pair)
        rig.Write(1, 0x04, 0x01); // 0x104 bit 0: pair ch0 + ch3
    rig.Write(0, 0x20, 0x02);
    rig.Write(0, 0x40, (muteOps & 1) ? 0x3F : 0x0C);
    rig.Write(0, 0x60, 0xFA);
    rig.Write(0, 0x80, 0x2A);
    rig.Write(1, 0xE0, 0x00);
    rig.Write(0, 0x23, 0x01);
    rig.Write(0, 0x43, (muteOps & 2) ? 0x3F : 0x17);
    rig.Write(0, 0x63, 0xF3);
    rig.Write(0, 0x83, 0xFA);
    rig.Write(1, 0xE3, 0x02);
    rig.Write(0, 0x28, 0x04);
    rig.Write(0, 0x48, (muteOps & 4) ? 0x3F : 0x1D);
    rig.Write(0, 0x68, 0xC2);
    rig.Write(0, 0x88, 0xFA);
    rig.Write(1, 0xE8, 0x01);
    rig.Write(0, 0x2B, 0x11);
    rig.Write(0, 0x4B, (muteOps & 8) ? 0x3F : 0x05);
    rig.Write(0, 0x6B, 0xC1);
    rig.Write(0, 0x8B, 0xFA);
    rig.Write(1, 0xEB, 0x00);
    rig.Write(0, 0xC0, 0x3F);
    rig.Write(0, 0xC3, 0x3F);
    rig.Write(0, 0xA0, 0x05);
    rig.Write(0, 0xA3, 0x05);
}

void TestFourOpDrumVoice()
{
    std::printf("FmTests: 4-op drum voice (CRYOGENT ch0+ch3, alg 11)\n");
    enum Variant { kFull, kSlaveKon, kNoOp1, kNoPair };
    constexpr int kSegs = 12;
    double seg[kNoPair + 1][kSegs] = {};
    for (int variant = kFull; variant <= kNoPair; variant++)
    {
        Rig rig;
        PatchDrumVoice(rig, variant != kNoPair, variant == kNoOp1 ? 1u : 0u);
        std::vector<int32_t> v;
        rig.Run(v, 16);
        v.clear();
        rig.Write(0, 0xB0, 0x26); // fnum 517, block 1, key on (masters only, as the guest)
        if (variant == kSlaveKon || variant == kNoPair)
            rig.Write(0, 0xB3, 0x26);
        rig.Run(v, 1000 * kSegs);
        for (int s = 0; s < kSegs; s++)
            seg[variant][s] = RmsRange(v, static_cast<size_t>(s) * 1000, 1000);
        std::printf("  variant %d: hit %.0f tail %.0f\n", variant, seg[variant][0], seg[variant][kSegs - 1]);
    }
    // Audible hit and sustained op4 tail (the 4-op ownership defect left
    // only hiss); under 0x104 pairing the slave's B0 is dead, so keying it
    // changes nothing; the O2->O3->O4 half sounds on its own; the slave
    // slots sound as a plain 2-op pair once unpaired.
    CHECK(seg[kFull][0] > 12000);
    CHECK(seg[kFull][kSegs - 1] > 0.75 * seg[kFull][0]);
    for (int s = 0; s < kSegs; s++)
        CHECK(std::fabs(seg[kSlaveKon][s] - seg[kFull][s]) < 1e-9);
    CHECK(seg[kNoOp1][0] > 12000);
    CHECK(seg[kNoOp1][kSegs - 1] > 0.75 * seg[kNoOp1][0]);
    CHECK(seg[kNoPair][0] > 12000);
}

// CRYOGENT ch15 lead: 2-op FB7 additive with AM/VIB, bank 1. Loud and bright
// (hf ~1.03, zc ~0.31 on the reference engines too - the timbre, not noise).
void TestFb7LeadVoice()
{
    std::printf("FmTests: 2-op FB7 lead (CRYOGENT ch15)\n");
    const uint16_t fnums[] = {0x1A0, 0x300};
    for (uint16_t fnum : fnums)
    {
        Rig rig;
        rig.Write(1, 0x05, 0x03);
        const uint8_t script[][3] = {
            {1, 0x30, 0x31}, {1, 0x33, 0x31}, {1, 0x50, 0x0A}, {1, 0x53, 0x05}, {1, 0x70, 0xAF},
            {1, 0x73, 0xC7}, {1, 0x90, 0x14}, {1, 0x93, 0x24}, {1, 0xF0, 0x00}, {1, 0xF3, 0x00},
            {1, 0xC6, 0x3F}, {1, 0xA6, static_cast<uint8_t>(fnum & 0xFF)},
            {1, 0xB6, static_cast<uint8_t>(0x20 | (3 << 2) | (fnum >> 8))},
        };
        for (const auto& w : script)
            rig.Write(w[0], w[1], w[2]);
        std::vector<int32_t> v;
        rig.Run(v, 16);
        v.clear();
        rig.Run(v, 12000);
        double rms, hf, zc;
        Character(v, rms, hf, zc);
        std::printf("  fnum 0x%03X: rms %.0f hf %.3f zc %.3f\n", fnum, rms, hf, zc);
        CHECK(rms > 5000);
        CHECK(std::fabs(hf - 1.035) < 0.1);
        CHECK(std::fabs(zc - 0.31) < 0.05);
    }
}

// JAMMED2 pad (MFM sample 3 module 5, ins 2): modulator AR15/DR0/EGT1 -
// register decay rate 0 freezes the envelope at the attack peak, so the
// FB7-additive loop stays a loud broadband wash while the key is held. A
// rate-0 "creep" decayed it into a quiet limit-cycle sine (zc ~0.02).
void TestDr0PadFreeze()
{
    std::printf("FmTests: DR0 pad, FB7 additive sustain (JAMMED2 ins 2)\n");
    for (uint8_t tl : {uint8_t(0x0C), uint8_t(0x12)})
    {
        Rig rig;
        rig.Write(1, 0x05, 0x03);
        const uint8_t script[][2] = {{0x22, 0x31}, {0x25, 0x11}, {0x42, tl},   {0x45, 0x3F}, {0x62, 0xF0}, {0x65, 0xF1},
                                     {0x82, 0x67}, {0x85, 0x95}, {0xC2, 0x3F}, {0xA2, 0xA0}, {0xB2, 0x31}};
        for (const auto& w : script)
            rig.Write(0, w[0], w[1]);
        std::vector<int32_t> v;
        rig.Run(v, 16);
        v.clear();
        rig.Run(v, 12000);
        double rms, hf, zc;
        Character(v, rms, hf, zc);
        std::printf("  TL %u: rms %.0f hf %.3f zc %.3f\n", tl, rms, hf, zc);
        CHECK(rms > 3000);
        CHECK(zc > 0.2); // noise wash, not a limit-cycle sine
    }
}

// HAPERT accordion (MFM sample 4, ins 3: FB7-FM, AR2 modulator / AR3
// carrier), re-keyed as MoonBlaster does: KOFF, A0, KON in one burst.
void PatchAccordion(Rig& rig)
{
    rig.Write(1, 0x05, 0x01);
    const uint8_t w[][2] = {{0x20, 0x61}, {0x23, 0x61}, {0x40, 0x20}, {0x43, 0x06}, {0x60, 0x21},
                            {0x63, 0x31}, {0x80, 0x22}, {0x83, 0x02}, {0xE0, 0x00}, {0xE3, 0x00},
                            {0xC0, 0x3E}, {0xA0, 0xE8}, {0xB0, 0x31}};
    for (const auto& x : w)
        rig.Write(0, x[0], x[1]);
}

// FM attack starts from the current envelope level and the key is sampled
// once per clock (ymfm clock_keystate, Nuked): a sounding note re-keyed
// between two clocks keeps its level. The old engine reset the envelope to
// silence on every key-on - a click and a 0.4 s fade-in on every note.
void TestRekeyKeepsLevel()
{
    std::printf("FmTests: re-key of a sounding note keeps its level\n");
    Rig rig;
    PatchAccordion(rig);
    std::vector<int32_t> v;
    rig.Run(v, 74000); // past the AR2/AR3 attack
    const size_t cut = v.size();
    rig.Write(0, 0xB0, 0x11);
    rig.Write(0, 0xA0, 0x02);
    rig.Write(0, 0xB0, 0x32);
    rig.Run(v, 15000);
    const double before = RmsRange(v, cut - 5000, 5000);
    double dip = 1e30;
    for (size_t i = cut; i + 250 <= cut + 5000; i += 250)
        dip = std::min(dip, RmsRange(v, i, 250));
    std::printf("  before %.0f, lowest 5 ms window after the re-key %.0f\n", before, dip);
    CHECK(dip > 0.8 * before);

    // Key-off, one clock, key-on: a real transition - the attack still
    // starts from the current level (reference engines: 0.99x).
    Rig gap;
    PatchAccordion(gap);
    std::vector<int32_t> g;
    gap.Run(g, 74000);
    const double held = RmsRange(g, g.size() - 1000, 1000);
    gap.Write(0, 0xB0, 0x11);
    gap.Run(g, 1);
    const size_t at = g.size();
    gap.Write(0, 0xB0, 0x31);
    gap.Run(g, 1000);
    const double ratio = RmsRange(g, at, 1000) / held;
    std::printf("  key-off/one clock/key-on: first 20 ms at %.3fx the held level\n", ratio);
    CHECK(ratio > 0.8 && ratio < 1.25);
}

double TimeToHalfLevel(const std::vector<int32_t>& v)
{
    double peak = 0;
    for (size_t i = 0; i + 256 <= v.size(); i += 256)
        peak = std::max(peak, RmsRange(v, i, 256));
    for (size_t i = 0; i + 256 <= v.size(); i += 256)
        if (RmsRange(v, i, 256) >= 0.5 * peak)
            return static_cast<double>(i);
    return -1;
}

// 96 dB FM envelope ceiling: an AR2 carrier reaches -6 dB after 27648
// samples, as on ymfm (the PCM-inherited -60 dB floor attacked 17% early).
void TestSlowAttackTiming()
{
    std::printf("FmTests: slow attack timing (96 dB envelope ceiling)\n");
    Rig rig;
    rig.Write(1, 0x05, 0x01);
    const uint8_t w[][2] = {{0x20, 0x21}, {0x23, 0x21}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xFF}, {0x63, 0x20},
                            {0x80, 0x0F}, {0x83, 0x0F}, {0xC0, 0x31}, {0xA0, 0x40}, {0xB0, 0x32}};
    for (const auto& x : w)
        rig.Write(0, x[0], x[1]);
    std::vector<int32_t> v;
    rig.Run(v, static_cast<size_t>(kFmRate * 4));
    const double t = TimeToHalfLevel(v);
    std::printf("  AR2 time to -6 dB: %.0f samples (reference 27648)\n", t);
    CHECK(t > 0.9 * 27648 && t < 1.1 * 27648);
}

// NTS is bank-0 register 0x08 bit 6: it picks F-number bit 8 (set) or 9
// (clear) for the KSR keycode. fnum 0x19A has bit 8 set and bit 9 clear, so
// NTS changes this KSR decay; with NTS set the level after 1 s matches the
// reference engines (550).
void TestNtsSelectsKsrKeycodeBit()
{
    std::printf("FmTests: NTS (bank-0 reg 0x08) picks the KSR keycode bit\n");
    double level[2] = {};
    for (int nts = 0; nts < 2; nts++)
    {
        Rig rig;
        rig.Write(1, 0x05, 0x01);
        rig.Write(0, 0x08, nts ? 0x40 : 0x00);
        const uint8_t w[][2] = {{0x20, 0x31}, {0x23, 0x31}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xFF}, {0x63, 0xF3},
                                {0x80, 0x0F}, {0x83, 0xF2}, {0xC0, 0x31}, {0xA0, 0x9A}, {0xB0, 0x31}};
        for (const auto& x : w)
            rig.Write(0, x[0], x[1]);
        std::vector<int32_t> v;
        rig.Run(v, static_cast<size_t>(kFmRate));
        level[nts] = RmsRange(v, 44000, 4000);
    }
    const double db = 20.0 * std::log10(level[1] / 550.0);
    std::printf("  level after 1 s: NTS clear %.0f, NTS set %.0f (reference 550, %+.2f dB)\n", level[0], level[1], db);
    CHECK(std::fabs(db) < 1.0);
    CHECK(level[0] > 1.4 * level[1]); // the bank-1 misread left NTS always clear (+6 dB)
}

// Native sine purity: the half-step sine table (silicon/ymfm sampling). The
// whole-step table repeated the peak and zero samples at every quadrant
// fold and put a -54 dB third harmonic on every sine.
void TestNativeSinePurity()
{
    std::printf("FmTests: native FM sine purity\n");
    Rig rig;
    rig.Write(1, 0x05, 0x01);
    const uint8_t w[][2] = {{0x20, 0x21}, {0x23, 0x21}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xF0}, {0x63, 0xF0},
                            {0x80, 0x00}, {0x83, 0x00}, {0xC0, 0x31}, {0xA0, 0x44}, {0xB0, 0x32}};
    for (const auto& x : w)
        rig.Write(0, x[0], x[1]);
    std::vector<int32_t> v;
    rig.Run(v, 60000);
    const double f0 = FnumHz(0x244, 4);
    // Harmonics 2..5 against the fundamental by correlation at exact multiples.
    double sig = 0, harm = 0;
    for (int k = 1; k <= 5; k++)
    {
        double c = 0, s = 0;
        for (size_t i = 10000; i < v.size(); i++)
        {
            const double ph = 2.0 * M_PI * k * f0 * static_cast<double>(i) / kFmRate;
            c += v[i] * std::cos(ph);
            s += v[i] * std::sin(ph);
        }
        (k == 1 ? sig : harm) += c * c + s * s;
    }
    const double db = 10.0 * std::log10(harm / sig);
    std::printf("  H2..H5 re H1: %.1f dB\n", db);
    CHECK(db < -54.0);
}

} // namespace

void RunFmEngineTests()
{
    std::printf("\n--- FM engine behaviour ---\n");
    TestClassicVoicePitchAndRelease();
    TestFourOpDrumVoice();
    TestFb7LeadVoice();
    TestDr0PadFreeze();
    TestRekeyKeepsLevel();
    TestSlowAttackTiming();
    TestNtsSelectsKsrKeycodeBit();
    TestNativeSinePurity();
}

} // namespace opl4test
