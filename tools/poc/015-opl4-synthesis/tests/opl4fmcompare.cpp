// opl4fmcompare — differential comparator: the in-tree Opl4Fm vs the
// Opl4FmYmfm adapter (ymfm OPL3 core), one register stream into both,
// each engine behind its own FmBus (harness design §4: state diff fires
// first, samples last).
//
// Agreement levels (rearchitecture Step 2): the bus layer owns every
// guest-visible bit, so flag/timer/aliasing/register equivalence is now
// structural — both engines receive identical de-aliased writes from the
// same bus code — and the old trivial exact-check level was retired with
// the engines' duplicate bus fields. What remains:
//   - asserted bands: identical classic-map register bytes (a real OPL3
//     driver stream) produce voices whose pitch, level and release class
//     agree — the in-tree engine adopted the canonical YMF262 operator
//     map, include-semantics 0xC0 routing (CHA/CHB/CHC/CHD) and the
//     creeping AR 0..3 rates (kStateVersion 3), so the classic-map
//     scenario that used to report linear-vs-classic divergence now
//     asserts agreement;
//   - reported, not asserted: residual numeric envelopes (mid-rate
//     shift-ladder domain, 0.09375 vs 0.1875 dB) — same shape class,
//     slightly different curves.
#include "testfw.h"
#include "fm/fmbus.h"
#include "fm/fmsynthymfm.h"

#include <cmath>

namespace opl4test
{

namespace
{

using EngineA = Opl4Fm;     // in-tree compact model
using EngineB = Opl4FmYmfm; // ymfm OPL3 core

// Both engines sit behind their own FmBus: the write path (aliasing, NEW)
// and the routing path are identical by construction, so the comparison
// isolates synthesis. Opl4Fm fills per-channel taps that its bus routes;
// Opl4FmYmfm emits a pre-mixed pair its bus passes through.
struct Rig
{
    EngineA a;
    EngineB b;
    FmBus busA;
    FmBus busB;

    Rig()
    {
        // Engines used standalone (not through Opl4, which Reset()s them on
        // chip reset) must be Reset() explicitly.
        a.Reset();
        b.Reset();
        busA.SetSynth(&a);
        busB.SetSynth(&b);
        busA.Reset();
        busB.Reset();
    }

    void Write(int bank, uint8_t reg, uint8_t data)
    {
        busA.Write(static_cast<uint8_t>(bank), reg, data);
        busB.Write(static_cast<uint8_t>(bank), reg, data);
    }
};

void Step(Rig& rig, int32_t& la, int32_t& ra, int32_t& lb, int32_t& rb,
          std::array<int32_t, 18>& ta, std::array<int32_t, 18>& tb)
{
    // One bus Advance per FM step: the bus ticks the timers exactly once.
    rig.busA.Advance(ta, la, ra);
    rig.busB.Advance(tb, lb, rb);
}

double Rms(const std::vector<int32_t>& v, size_t n)
{
    double acc = 0;
    for (size_t i = 0; i < n; i++)
        acc += static_cast<double>(v[i]) * v[i];
    return std::sqrt(acc / static_cast<double>(n));
}

double ZeroCrossRate(const std::vector<int32_t>& v, size_t n)
{
    double mean = 0;
    for (size_t i = 0; i < n; i++)
        mean += v[i];
    mean /= static_cast<double>(n);
    int zc = 0;
    for (size_t i = 1; i < n; i++)
        if ((v[i - 1] >= mean) != (v[i] >= mean))
            zc++;
    return zc / static_cast<double>(n);
}

// Classic-map pure-carrier voice (silent modulator), IDENTICAL register
// bytes into both engines — what a real OPL3 driver emits for ch0. Both
// engines must land every write on the same operators (canonical
// YMF262 slot map) and agree on pitch and level.
void VoiceClassic(Rig& rig)
{
    const uint8_t w[][3] = {
        {0, 0x20, 0x01}, // ch0 modulator: mult 1
        {0, 0x23, 0x01}, // ch0 carrier (classic +3): mult 1
        {0, 0x40, 0x3F}, // mod TL: silent
        {0, 0x43, 0x00}, // car TL 0
        {0, 0x60, 0xF0}, // mod AR 15
        {0, 0x63, 0xF0}, // car AR 15
        {0, 0x80, 0x00}, // mod SL 0 RR 0
        {0, 0x83, 0x00}, // car SL 0 RR 0
        {0, 0xA0, 0x03}, // F-number 0x303
        {0, 0xB0, 0x2B}, // block 2, key on
        {0, 0xC0, 0x30}, // CHA+CHB (include): both stereo sides
    };
    for (const auto& x : w)
        rig.Write(x[0], x[1], x[2]);
}

// Samples: identical register bytes, asserted agreement bands on pitch and
// level; release compared by shape class.
void CompareVoiceStreams()
{
    std::printf("FmCompare: voice streams (identical classic bytes)\n");
    Rig rig;
    rig.Write(1, 0x05, 0x01); // NEW: bank-1 register file on both
    VoiceClassic(rig);

    const size_t n = 6000;
    std::vector<int32_t> la(n), lb(n);
    std::array<int32_t, 18> ta, tb;
    int32_t ra, rb;
    for (size_t i = 0; i < n; i++)
    {
        Step(rig, la[i], ra, lb[i], rb, ta, tb);
        (void)ra;
        (void)rb;
    }

    const double za = ZeroCrossRate(la, n);
    const double zb = ZeroCrossRate(lb, n);
    const double rmsA = Rms(la, n);
    const double rmsB = Rms(lb, n);
    std::printf("  pitch: zero-cross/sample ours=%.5f ymfm=%.5f (ratio %.4f)"
                "\n  level: rms ours=%.0f ymfm=%.0f (ratio %.3f)\n",
                za, zb, za / (zb + 1e-30), rmsA, rmsB,
                rmsA / (rmsB + 1e-30));
    CHECK(std::fabs(za / (zb + 1e-30) - 1.0) < 0.005);
    CHECK(rmsA > 1000 && rmsB > 1000); // both keyed and audible
    CHECK(std::fabs(rmsA / (rmsB + 1e-30) - 1.0) < 0.05); // level within 5%

    // Envelope: instant attack then release. The decay-rate machinery is a
    // documented numeric difference (compact model vs ymfm 5.11 counter),
    // so only the shape class is asserted: silence after key-off in both.
    rig.Write(0, 0xB0, 0x0B); // key off (block/fnum kept)
    double tailA = 0, tailB = 0;
    for (int i = 0; i < 20000; i++)
    {
        int32_t xa, xb;
        Step(rig, xa, ra, xb, rb, ta, tb);
        tailA += std::fabs(double(xa));
        tailB += std::fabs(double(xb));
    }
    std::printf("  release: |tail| sum ours=%.0f ymfm=%.0f\n", tailA, tailB);
    CHECK(tailA / (tailA + tailB + 1e-30) < 0.999);
    CHECK(tailB / (tailA + tailB + 1e-30) < 0.999);
}

// Classic-map voice, IDENTICAL bytes into both engines (what a real OPL3
// driver emits for ch0, CNT additive + CHA only). Asserted MATCH: after
// the kStateVersion-3 classic-map adoption both engines interpret every
// register identically — this is the scenario that used to report the
// linear-vs-classic divergence behind FM "wrong instruments".
void CompareClassicMapVoice()
{
    std::printf("FmCompare: classic-map voice (asserted match)\n");
    Rig rig;
    rig.Write(1, 0x05, 0x01);

    const uint8_t script[][3] = {
        {0, 0x20, 0x01}, // ch0 modulator: mult 1
        {0, 0x23, 0x01}, // ch0 carrier (classic): mult 1
        {0, 0x40, 0x3F}, // mod TL silent
        {0, 0x43, 0x10}, // car TL -12 dB
        {0, 0x60, 0xF0}, {0, 0x63, 0xF0}, // AR 15
        {0, 0x80, 0x00}, {0, 0x83, 0x00},
        {0, 0xA0, 0x03}, {0, 0xB0, 0x2B}, {0, 0xC0, 0x31},
    };
    for (const auto& w : script)
        rig.Write(w[0], w[1], w[2]);

    const size_t n = 6000;
    std::vector<int32_t> la(n), lb(n);
    std::array<int32_t, 18> ta, tb;
    int32_t ra, rb;
    for (size_t i = 0; i < n; i++)
        Step(rig, la[i], ra, lb[i], rb, ta, tb);

    const double rmsA = Rms(la, n);
    const double rmsB = Rms(lb, n);
    const double za = ZeroCrossRate(la, n);
    const double zb = ZeroCrossRate(lb, n);
    const bool diverged = std::fabs(rmsA / (rmsB + 1e-30) - 1.0) > 0.25
        || std::fabs(za / (zb + 1e-30) - 1.0) > 0.01;
    std::printf("  identical bytes: rms ours=%.0f ymfm=%.0f, pitch ratio %.4f"
                " -> %s\n",
                rmsA, rmsB, za / (zb + 1e-30), diverged ? "DIVERGENT" : "agree");
    CHECK(!diverged); // the classic map is shared now — agreement required
}

// TTD contract on the adapter: save is pure (a chip that saved renders
// identically to one that never did) and restore is an exact continuation.
// The bus chunk rides along (POD, restored by construction); the engine
// chunk is the adapter's own layout.
void CompareAdapterSaveRestore()
{
    std::printf("FmCompare: adapter save/restore\n");
    EngineB src, twin, restored;
    FmBus busSrc, busTwin, busRestored;
    busSrc.SetSynth(&src);
    busTwin.SetSynth(&twin);
    busRestored.SetSynth(&restored);
    src.Reset();
    twin.Reset();
    restored.Reset();
    busSrc.Reset();
    busTwin.Reset();
    busRestored.Reset();
    for (FmBus* bus : {&busSrc, &busTwin, &busRestored})
    {
        bus->Write(1, 0x05, 0x01);
        bus->Write(0, 0x20, 0x01);
        bus->Write(0, 0x23, 0x01);
        bus->Write(0, 0x40, 0x3F);
        bus->Write(0, 0x63, 0xF0);
        bus->Write(0, 0x83, 0x00);
        bus->Write(0, 0xA0, 0x03);
        bus->Write(0, 0xB0, 0x2B);
        bus->Write(0, 0xC0, 0x30);
    }
    std::array<int32_t, 18> t1, t2;
    int32_t l, r;
    for (int i = 0; i < 500; i++)
    {
        busSrc.Advance(t1, l, r);
        busTwin.Advance(t2, l, r);
    }

    std::vector<uint8_t> blob(EngineB::kStateSize);
    std::vector<uint8_t> busBlob(FmBus::kStateSize);
    src.SaveState(blob.data());
    busSrc.SaveState(busBlob.data());

    restored.LoadState(blob.data());
    busRestored.LoadState(busBlob.data());
    for (int i = 0; i < 2000; i++)
    {
        int32_t ls, lt, lr;
        busSrc.Advance(t1, ls, lr);
        busTwin.Advance(t2, lt, lr);
        busRestored.Advance(t2, l, r);
        CHECK_EQ_I(ls, l); // restored continues the saved chip exactly
        if (ls != l)
            return;
        CHECK_EQ_I(ls, lt); // saving was side-effect free
        if (ls != lt)
            return;
    }
    // No engine status exists to compare any more (bus owns it; the retired
    // checks compared flags neither side ever set).
}

// The CRYOGENT drum patch (guest capture, exact bytes): 0x104 pairing of
// ch0 (master) + ch3 (slave) and every operator/C0/pitch register.
// muteOps bit 0..3 silences op1..op4 (TL max) for chain bisection.
void PatchDrumVoice(Rig& rig, bool pair, uint8_t muteOps)
{
    rig.Write(1, 0x05, 0x01); // NEW
    if (pair)
        rig.Write(1, 0x04, 0x01); // 0x104 bit 0: pair ch0 (master) + ch3 (slave)
    // op1 = ch0 mod (0x20): M2, EGT 0, KSR 0
    rig.Write(0, 0x20, 0x02);
    rig.Write(0, 0x40, (muteOps & 1) ? 0x3F : 0x0C); // TL12 (max = probe off)
    rig.Write(0, 0x60, 0xFA); // AR15 DR10
    rig.Write(0, 0x80, 0x2A); // SL2 RR10
    rig.Write(1, 0xE0, 0x00); // WS0
    // op2 = ch0 car (0x23): M1, EGT 0, KSR 0
    rig.Write(0, 0x23, 0x01);
    rig.Write(0, 0x43, (muteOps & 2) ? 0x3F : 0x17); // TL23
    rig.Write(0, 0x63, 0xF3); // AR15 DR3
    rig.Write(0, 0x83, 0xFA); // SL15 RR10
    rig.Write(1, 0xE3, 0x02); // WS2
    // op3 = ch3 mod (0x28): M4, EGT 0, KSR 0
    rig.Write(0, 0x28, 0x04);
    rig.Write(0, 0x48, (muteOps & 4) ? 0x3F : 0x1D); // TL29
    rig.Write(0, 0x68, 0xC2); // AR12 DR2
    rig.Write(0, 0x88, 0xFA); // SL15 RR10
    rig.Write(1, 0xE8, 0x01); // WS1
    // op4 = ch3 car (0x2B): M1, EGT 0, KSR 1
    rig.Write(0, 0x2B, 0x11);
    rig.Write(0, 0x4B, (muteOps & 8) ? 0x3F : 0x05); // TL5
    rig.Write(0, 0x6B, 0xC1); // AR12 DR1
    rig.Write(0, 0x8B, 0xFA); // SL15 RR10
    rig.Write(1, 0xEB, 0x00); // WS0
    // C0/C3: route CHA+CHB (3), FB7, CON 1 -> alg 11; A0/A3 pitch.
    // fnum 517, block 1 -> A0=0x05, B0=0x26 when keyed.
    rig.Write(0, 0xC0, 0x3F);
    rig.Write(0, 0xC3, 0x3F);
    rig.Write(0, 0xA0, 0x05);
    rig.Write(0, 0xA3, 0x05);
}

// 4-op drum voice differential: the CRYOGENT (MFM Music sample 2, melody
// 7) percussion patch — master ch0 + slave ch3, alg 11 (O1 + O3 + O4),
// FB7 op1 noise over a TL5/DR1 op4 sine tail — the exact register bytes the
// guest capture printed. This is the voice class that exposed the 4-op
// ownership defect: identical bytes must render the same hit-and-tail
// level profile on both backends.
//
// Variants bisect the chain: A the full patch (guest keys masters only);
// B additionally keys the slave B3 (both-kon style); C silences op1 so the
// O2->O3->O4 half is measured alone; D drops the 0x104 pair (slave keyed
// as a plain 2-op channel) proving the slave slots sound at all.
void CompareFourOpDrum()
{
    std::printf("FmCompare: 4-op drum voice (CRYOGENT ch0+ch3, alg 11)\n");
    enum Variant { kFull, kSlaveKon, kNoOp1, kNoPair };
    const char* names[] = {"A full", "B +slaveKon", "C noOp1", "D noPair+slaveKon"};
    const int kSegs = 12;
    double allA[kNoPair + 1][kSegs] = {};
    double allB[kNoPair + 1][kSegs] = {};
    for (int variant = kFull; variant <= kNoPair; variant++)
    {
        Rig rig;
        PatchDrumVoice(rig, variant != kNoPair,
                       variant == kNoOp1 ? 1u : 0u);
        std::vector<int32_t> la(1000), lb(1000);
        double rmsA[kSegs], rmsB[kSegs];
        std::array<int32_t, 18> ta, tb;
        int32_t ra, rb;

        // Settle phase between configuration and kon. ymfm keys a 0xB0
        // write through the channel's CURRENT operator list (write() ->
        // keyonoff), while the 4-op/2-op operator map is only re-derived at
        // clock time (clock() -> assign_operators(), DYNAMIC_OPS) — so the
        // 0x104 pairing reaches the kon path only after at least one clock.
        // Real drivers always have thousands of clocks between 0x104 and
        // 0xB0; without this phase the kons land on the init-time 2-op
        // lists and ymfm keys {op1,op2} instead of the four-op chain (all
        // keyed-off silence here; outputs discarded).
        for (int i = 0; i < 16; i++)
            Step(rig, la[0], ra, lb[0], rb, ta, tb);

        // kon: fnum 517, block 1 — the guest's exact bytes (A0=0x05,
        // B0=0x26; the guest keys masters only)
        rig.Write(0, 0xB0, 0x26);
        if (variant == kSlaveKon || variant == kNoPair)
            rig.Write(0, 0xB3, 0x26); // slave kon too

        // Spectral metrics use the guest test's definitions: hf =
        // sqrt(diff-energy/energy) (white noise = sqrt(2), sine =
        // 2*sin(pi*f/fs)), zc = sign-crossing rate per sample.
        double eA = 0, dA = 0, eB = 0, dB = 0;
        long long zcA = 0, zcB = 0;
        int32_t prevA = 0, prevB = 0;

        for (int s = 0; s < kSegs; s++)
        {
            for (int i = 0; i < 1000; i++)
            {
                Step(rig, la[i], ra, lb[i], rb, ta, tb);
                eA += double(la[i]) * la[i];
                dA += double(la[i] - prevA) * (la[i] - prevA);
                zcA += (prevA < 0) != (la[i] < 0) ? 1 : 0;
                prevA = la[i];
                eB += double(lb[i]) * lb[i];
                dB += double(lb[i] - prevB) * (lb[i] - prevB);
                zcB += (prevB < 0) != (lb[i] < 0) ? 1 : 0;
                prevB = lb[i];
            }
            rmsA[s] = Rms(la, 1000);
            rmsB[s] = Rms(lb, 1000);
            allA[variant][s] = rmsA[s];
            allB[variant][s] = rmsB[s];
        }
        std::printf("  %-17s ours:", names[variant]);
        for (int s = 0; s < kSegs; s++)
            std::printf(" %.0f", rmsA[s]);
        std::printf("\n  %-17s ymfm:", "");
        for (int s = 0; s < kSegs; s++)
            std::printf(" %.0f", rmsB[s]);
        std::printf("\n  %-17s hf ours=%.3f zc=%.4f | ymfm=%.3f zc=%.4f\n",
                    "spectral:", std::sqrt(dA / (eA + 1e-30)),
                    zcA / (1000.0 * kSegs), std::sqrt(dB / (eB + 1e-30)),
                    zcB / (1000.0 * kSegs));
    }
    // Asserted bands (calibrated on both backends, both build trees):
    //   A: the guest scenario — the drum hit is audible on both backends
    //      (the pre-fix defect: in-tree collapsed to hiss because the slave
    //      ops never sounded under the master's kon) and sustains a tail
    //      (op4 TL5/DR1); steady segments agree within a few percent, the
    //      attack segment (FB7 burst + AR ramps) within 15%, the tail
    //      within 15% (documented mid-rate decay numeric residual).
    //   B == A exactly: under 0x104 pairing the slave kon is a no-op on
    //      both backends (empty operator list / dead slave path).
    //   C: the O2->O3->O4 half alone is audible and agrees at the attack
    //      within 10% — the chain the ownership defect used to silence.
    //   D: the same slave slots sound as a plain 2-op pair (attack within
    //      12%), proving variant differences come from pairing, not from
    //      dead operator slots.
    CHECK(allA[kFull][0] > 12000 && allB[kFull][0] > 12000);
    CHECK(allA[kFull][kSegs - 1] > 0.75 * allA[kFull][0]);
    CHECK(allB[kFull][kSegs - 1] > 0.75 * allB[kFull][0]);
    CHECK(std::fabs(allA[kFull][0] / allB[kFull][0] - 1.0) < 0.15);
    CHECK(std::fabs(allA[kFull][kSegs - 1] / allB[kFull][kSegs - 1] - 1.0) < 0.15);
    for (int s = 0; s < kSegs; s++)
    {
        CHECK(std::fabs(allA[kSlaveKon][s] - allA[kFull][s]) < 1e-9);
        CHECK(std::fabs(allB[kSlaveKon][s] - allB[kFull][s]) < 1e-9);
    }
    CHECK(allA[kNoOp1][0] > 12000 && allB[kNoOp1][0] > 12000);
    CHECK(allA[kNoOp1][kSegs - 1] > 0.75 * allA[kNoOp1][0]);
    CHECK(allB[kNoOp1][kSegs - 1] > 0.75 * allB[kNoOp1][0]);
    CHECK(std::fabs(allA[kNoOp1][0] / allB[kNoOp1][0] - 1.0) < 0.10);
    CHECK(allA[kNoPair][0] > 12000 && allB[kNoPair][0] > 12000);
    CHECK(std::fabs(allA[kNoPair][0] / allB[kNoPair][0] - 1.0) < 0.12);
}

// One retrigger measurement run: six kon/koff cycles, rms per half-cycle,
// koff decay profiles (first/last cycle) and the guest's spectral metrics,
// both backends.
void RetriggerRun(const char* label, uint8_t muteOps, int cycles)
{
    Rig rig;
    PatchDrumVoice(rig, true, muteOps);
    std::array<int32_t, 18> ta, tb;
    int32_t ra, rb, xa, xb;
    for (int i = 0; i < 16; i++) // map refresh settle (see CompareFourOpDrum)
        Step(rig, xa, ra, xb, rb, ta, tb);

    // Exact fundamental period of the drum voice (fnum 517, block 1:
    // 49516.4/(2^20/517/2) = 48.8 Hz -> 1015 samples): rms windows then
    // align to whole cycles regardless of the phase-reset-on-kon
    // difference between the backends (ymfm zeroes phase at kon, the
    // in-tree engine free-runs), removing window-alignment artifacts.
    const int kHalf = 1015;
    double rmsA[12], rmsB[12];
    double eA = 0, dA = 0, eB = 0, dB = 0;
    long long zcA = 0, zcB = 0;
    int32_t prevA = 0, prevB = 0;
    int idx = 0;
    // koff-half decay profile: 10 sub-segments for the first and last
    // cycle (release-rate diagnosis: constant ratio = rate diff, constant
    // offset = start-level diff).
    double profA[2][10] = {};
    double profB[2][10] = {};
    int profCycle = -1;
    const int kSubSeg = kHalf / 10;
    auto measureSeg = [&](int n, bool koffHalf)
    {
        double sa = 0, sb = 0;
        double subA = 0, subB = 0;
        int sub = 0;
        for (int i = 0; i < n; i++)
        {
            Step(rig, xa, ra, xb, rb, ta, tb);
            sa += double(xa) * xa;
            sb += double(xb) * xb;
            if (koffHalf && profCycle >= 0)
            {
                subA += double(xa) * xa;
                subB += double(xb) * xb;
                if ((i + 1) % kSubSeg == 0 && sub < 10)
                {
                    profA[profCycle][sub] = std::sqrt(subA / kSubSeg);
                    profB[profCycle][sub] = std::sqrt(subB / kSubSeg);
                    subA = subB = 0;
                    sub++;
                }
            }
            eA += double(xa) * xa;
            dA += double(xa - prevA) * (xa - prevA);
            zcA += (prevA < 0) != (xa < 0) ? 1 : 0;
            prevA = xa;
            eB += double(xb) * xb;
            dB += double(xb - prevB) * (xb - prevB);
            zcB += (prevB < 0) != (xb < 0) ? 1 : 0;
            prevB = xb;
        }
        rmsA[idx] = std::sqrt(sa / n);
        rmsB[idx] = std::sqrt(sb / n);
        idx++;
    };
    for (int cyc = 0; cyc < cycles; cyc++)
    {
        profCycle = (cyc == 0 || cyc == cycles - 1) ? (cyc == 0 ? 0 : 1) : -1;
        rig.Write(0, 0xB0, 0x26); // kon
        measureSeg(kHalf, false);
        rig.Write(0, 0xB0, 0x06); // koff (block/fnum kept)
        measureSeg(kHalf, true);
    }
    for (int c = 0; c < 2; c++)
    {
        if (cycles < 2)
            break;
        std::printf("  %-17s cyc%d ours:", "koff profile", c * (cycles - 1));
        for (int i = 0; i < 10; i++)
            std::printf(" %.0f", profA[c][i]);
        std::printf("\n  %-17s cyc%d ymfm:", "", c * (cycles - 1));
        for (int i = 0; i < 10; i++)
            std::printf(" %.0f", profB[c][i]);
        std::printf("\n");
    }
    std::printf("  %-17s ours:", label);
    for (int s = 0; s < idx; s++)
        std::printf(" %.0f", rmsA[s]);
    std::printf("\n  %-17s ymfm:", "");
    for (int s = 0; s < idx; s++)
        std::printf(" %.0f", rmsB[s]);
    std::printf("\n  %-17s hf ours=%.3f zc=%.4f | ymfm=%.3f zc=%.4f\n",
                "spectral:", std::sqrt(dA / (eA + 1e-30)),
                zcA / (kHalf * 2.0 * cycles), std::sqrt(dB / (eB + 1e-30)),
                zcB / (kHalf * 2.0 * cycles));
}

// Guest-realistic percussion drive: the MFM player re-keys the drum
// channels every note (kon ... koff ...), unlike the single sustained kon
// above — with RR10 the voice then spends its life in burst+release, the
// regime the guest test's hf/zc metrics actually see. Six kon/koff cycles
// per variant (plus a single-shot control); per-op mutes bisect which
// operator drives any divergence.
void CompareFourOpDrumRetrigger()
{
    std::printf("FmCompare: 4-op drum retriggers (CRYOGENT ch0+ch3)\n");
    RetriggerRun("single", 0, 1);
    RetriggerRun("full", 0, 6);
    RetriggerRun("noOp1", 1, 6);
    RetriggerRun("noOp2", 2, 6);
    RetriggerRun("noOp3", 4, 6);
    RetriggerRun("noOp4", 8, 6);
}

// Guest-realistic lead voice: the CRYOGENT 2-op leads (ch15-17) - FB7
// feedback, AM+VIB on both operators (e1k1), WS0 sines, TL 10/5 - whose
// corrected guest-sweep readings sit near the noise band (hf ~ 1.0). The
// drum cases never exercise the AM/VIB LFO paths; this one holds them to
// the same A/B standard. Four pitches x sustained kon; FB7 self-modulation
// is chaotic, so sample-exact equality is impossible by construction -
// statistics must agree.
void CompareLeadVoice()
{
    std::printf("FmCompare: 2-op lead voice, FB7 + AM/VIB (CRYOGENT ch15)\n");
    const uint16_t fnums[] = {0x1A0, 0x300, 0x1A0, 0x300};
    const uint8_t blocks[] = {3, 3, 4, 4};
    for (int variant = 0; variant < 4; variant++)
    {
        Rig rig;
        rig.Write(1, 0x05, 0x03); // OPL3 mode + NEW2, as the guest writes it
        const uint8_t script[][3] = {
            {1, 0x30, 0x31}, // ch15 modulator: AM1 VIB1 mult 1
            {1, 0x33, 0x31}, // ch15 carrier:   AM1 VIB1 mult 1
            {1, 0x50, 0x0A}, // mod TL 10
            {1, 0x53, 0x05}, // car TL 5
            {1, 0x70, 0xAF}, // mod AR10 DR15
            {1, 0x73, 0xC7}, // car AR12 DR7
            {1, 0x90, 0x14}, // mod SL1 RR4
            {1, 0x93, 0x24}, // car SL2 RR4
            {1, 0xF0, 0x00}, // mod WS0
            {1, 0xF3, 0x00}, // car WS0
            {1, 0xC6, 0x3F}, // FB7 additive, out 3 (L+R)
            {1, 0xA6, static_cast<uint8_t>(fnums[variant] & 0xFF)},
            {1, 0xB6, static_cast<uint8_t>(0x20 | (blocks[variant] << 2) | (fnums[variant] >> 8))}, // key on
        };
        for (const auto& w : script)
            rig.Write(w[0], w[1], w[2]);

        const int n = 12000;
        std::array<int32_t, 18> ta, tb;
        int32_t ra, rb;
        std::vector<int32_t> la(n), lb(n);
        for (int i = 0; i < 16; i++) // map refresh settle (see CompareFourOpDrum)
            Step(rig, la[0], ra, lb[0], rb, ta, tb);
        for (int i = 0; i < n; i++)
            Step(rig, la[i], ra, lb[i], rb, ta, tb);

        double eA = 0, dA = 0, eB = 0, dB = 0;
        long long zcA = 0, zcB = 0;
        for (int i = 0; i < n; i++)
        {
            eA += double(la[i]) * la[i];
            eB += double(lb[i]) * lb[i];
            if (i > 0)
            {
                dA += double(la[i] - la[i - 1]) * (la[i] - la[i - 1]);
                dB += double(lb[i] - lb[i - 1]) * (lb[i] - lb[i - 1]);
                zcA += (la[i - 1] < 0) != (la[i] < 0) ? 1 : 0;
                zcB += (lb[i - 1] < 0) != (lb[i] < 0) ? 1 : 0;
            }
        }
        const double rmsA = std::sqrt(eA / n);
        const double rmsB = std::sqrt(eB / n);
        const double hfA = std::sqrt(dA / (eA + 1e-30));
        const double hfB = std::sqrt(dB / (eB + 1e-30));
        const double zcRa = zcA / double(n);
        const double zcRb = zcB / double(n);
        std::printf("  fnum=0x%03X blk=%u: rms ours=%.0f ymfm=%.0f (x%.3f)"
                    " hf ours=%.3f ymfm=%.3f zc ours=%.5f ymfm=%.5f\n",
                    fnums[variant], blocks[variant], rmsA, rmsB,
                    rmsA / (rmsB + 1e-30), hfA, hfB, zcRa, zcRb);
        // Calibrated bands: rms agrees within 0.5%, hf/zc character within
        // noise (hf ~ 1.03 / zc ~ 0.31 in BOTH engines - the FB7 lead is
        // genuinely bright; that is the timbre, not a defect). Bands hold
        // margin, not the measurement.
        CHECK(rmsA > 5000 && rmsB > 5000); // loud, keyed lead
        CHECK(std::fabs(rmsA / (rmsB + 1e-30) - 1.0) < 0.05);
        CHECK(std::fabs(hfA - hfB) < 0.05);
        CHECK(std::fabs(zcRa - zcRb) < 0.05);
    }
}

// JAMMED2 (mfm_sample_3 module 5) pad voice: the ins2 modulator is
// AR15/DR0/EGT1 — register decay rate 0, so the envelope HOLDS the attack
// peak (ymfm effective_rate: rawrate 0 -> 0 before ksrval; Nuked gates
// every increment on reg_rate != 0) and the FB7-additive loop
// self-oscillates as a broadband wash for as long as the key is held.
// The old rate-0 "creep" (keycode applied at DR0) decayed the modulator
// to SL6 instead — 18 dB below the loop-gain boundary — collapsing the
// wash into a quiet limit-cycle sine (zc 0.02 vs 0.44 on both
// references). Guards the freeze level AND the sustained noise character.
void CompareDr0PadVoice()
{
    std::printf("FmCompare: 2-op DR0 pad voice, FB7 additive sustain (JAMMED2 ins2)\n");
    const uint8_t tls[] = {0x0C, 0x12}; // TL 12/18: above the chaos boundary
    for (int variant = 0; variant < 2; variant++)
    {
        Rig rig;
        rig.Write(1, 0x05, 0x03); // OPL3 mode + NEW2, as the guest writes it
        const uint8_t script[][3] = {
            {0, 0x22, 0x31}, // ch2 modulator: EGT1 KSR1 mult 1
            {0, 0x25, 0x11}, // ch2 carrier:   EGT1 KSR0 mult 1
            {0, 0x42, tls[variant]}, // mod TL (loop-gain sweep)
            {0, 0x45, 0x3F}, // car TL: silent — the feedback loop alone
            {0, 0x62, 0xF0}, // mod AR15 DR0 — the rate-0 freeze under test
            {0, 0x65, 0xF1}, // car AR15 DR1
            {0, 0x82, 0x67}, // mod SL6 RR7
            {0, 0x85, 0x95}, // car SL9 RR5
            {0, 0xC2, 0x3F}, // FB7 additive, out 3 (L+R)
            {0, 0xA2, 0xA0}, // fnum 0x1A0
            {0, 0xB2, 0x31}, // block 4, key on
        };
        for (const auto& w : script)
            rig.Write(w[0], w[1], w[2]);

        const int n = 12000;
        std::array<int32_t, 18> ta, tb;
        int32_t ra, rb;
        std::vector<int32_t> la(n), lb(n);
        for (int i = 0; i < 16; i++) // map refresh settle (see CompareFourOpDrum)
            Step(rig, la[0], ra, lb[0], rb, ta, tb);
        for (int i = 0; i < n; i++)
            Step(rig, la[i], ra, lb[i], rb, ta, tb);

        double eA = 0, dA = 0, eB = 0, dB = 0;
        long long zcA = 0, zcB = 0;
        for (int i = 0; i < n; i++)
        {
            eA += double(la[i]) * la[i];
            eB += double(lb[i]) * lb[i];
            if (i > 0)
            {
                dA += double(la[i] - la[i - 1]) * (la[i] - la[i - 1]);
                dB += double(lb[i] - lb[i - 1]) * (lb[i] - lb[i - 1]);
                zcA += (la[i - 1] < 0) != (la[i] < 0) ? 1 : 0;
                zcB += (lb[i - 1] < 0) != (lb[i] < 0) ? 1 : 0;
            }
        }
        const double rmsA = std::sqrt(eA / n);
        const double rmsB = std::sqrt(eB / n);
        const double hfA = std::sqrt(dA / (eA + 1e-30));
        const double hfB = std::sqrt(dB / (eB + 1e-30));
        const double zcRa = zcA / double(n);
        const double zcRb = zcB / double(n);
        std::printf("  TL=%2u: rms ours=%.0f ymfm=%.0f (x%.3f)"
                    " hf ours=%.3f ymfm=%.3f zc ours=%.5f ymfm=%.5f\n",
                    tls[variant], rmsA, rmsB, rmsA / (rmsB + 1e-30), hfA, hfB, zcRa, zcRb);
        // The DR0 freeze holds the modulator at the attack peak: the loop
        // stays loud and chaotic in BOTH engines (zc ~ 0.44-0.55, hf ~
        // 1.25-1.44 at these TLs). A rate-0 decay regresses to zc ~ 0.02.
        CHECK(rmsA > 3000 && rmsB > 3000);
        CHECK(std::fabs(rmsA / (rmsB + 1e-30) - 1.0) < 0.05);
        CHECK(std::fabs(hfA - hfB) < 0.05);
        CHECK(std::fabs(zcRa - zcRb) < 0.05);
        CHECK(zcRa > 0.2 && zcRb > 0.2); // noise wash, not a limit-cycle sine
    }
}

// Key-on semantics (ymfm start_attack / clock_keystate, Nuked-OPL3): FM
// attack starts from the CURRENT envelope level, the phase restarts, and
// the key is sampled once per clock. HAPERT's accordion (MFM ins 3: FB7
// FM, AR2 modulator/AR3 carrier) re-keyed every note with KOFF/A0/KON in
// one burst; the old engine dropped each note to silence and faded it
// back in over ~0.4 s.
struct TwoStreams
{
    std::vector<int32_t> a, b;
};

void Run(Rig& rig, TwoStreams& s, size_t n)
{
    std::array<int32_t, 18> ta, tb;
    for (size_t i = 0; i < n; i++)
    {
        int32_t la, ra, lb, rb;
        Step(rig, la, ra, lb, rb, ta, tb);
        s.a.push_back(la + ra);
        s.b.push_back(lb + rb);
    }
}

double RmsRange(const std::vector<int32_t>& v, size_t from, size_t n)
{
    double acc = 0;
    for (size_t i = from; i < from + n; i++)
        acc += static_cast<double>(v[i]) * v[i];
    return std::sqrt(acc / static_cast<double>(n));
}

double BestShapeCorr(const std::vector<int32_t>& x, const std::vector<int32_t>& y, size_t from, size_t n)
{
    double best = -1.0;
    for (int lag = -8; lag <= 8; lag++)
    {
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = from + 8; i < from + n - 8; i++)
        {
            const double u = x[i], w = y[static_cast<size_t>(static_cast<long>(i) + lag)];
            xy += u * w;
            xx += u * u;
            yy += w * w;
        }
        if (xx > 0 && yy > 0)
            best = std::max(best, xy / std::sqrt(xx * yy));
    }
    return best;
}

void PatchAccordion(Rig& rig)
{
    rig.Write(1, 0x05, 0x01);
    const uint8_t w[][2] = {
        {0x20, 0x61}, {0x23, 0x61}, {0x40, 0x20}, {0x43, 0x06}, {0x60, 0x21}, {0x63, 0x31},
        {0x80, 0x22}, {0x83, 0x02}, {0xE0, 0x00}, {0xE3, 0x00}, {0xC0, 0x3E},
        {0xA0, 0xE8}, {0xB0, 0x31}, // block 4, fnum 0x1E8, key on
    };
    for (const auto& x : w)
        rig.Write(0, x[0], x[1]);
}

void CompareRetriggerKeepsLevel()
{
    std::printf("FmCompare: re-key of a sounding note (KOFF/A0/KON between clocks)\n");
    Rig rig;
    PatchAccordion(rig);
    TwoStreams s;
    Run(rig, s, 74000); // ~1.5 s: past the AR2/AR3 attack
    const size_t cut = s.a.size();
    rig.Write(0, 0xB0, 0x11); // key off
    rig.Write(0, 0xA0, 0x02); // next note, same burst
    rig.Write(0, 0xB0, 0x32); // key on
    Run(rig, s, 15000);
    const double before = RmsRange(s.a, cut - 5000, 5000);
    const double after = RmsRange(s.a, cut, 5000);
    const double corr = BestShapeCorr(s.a, s.b, cut, 15000);
    std::printf("  level after/before: ours %.3f ymfm %.3f  shape corr vs ymfm %.3f\n", after / before,
                RmsRange(s.b, cut, 5000) / RmsRange(s.b, cut - 5000, 5000), corr);
    CHECK(after > 0.8 * before); // no drop to silence at the re-key
    CHECK(corr > 0.95);
}

void CompareRekeyAfterGapStartsFromLevel()
{
    std::printf("FmCompare: key-off, one clock, key-on — attack from the current level\n");
    Rig rig;
    PatchAccordion(rig);
    TwoStreams s;
    Run(rig, s, 74000);
    rig.Write(0, 0xB0, 0x11);
    Run(rig, s, 1);
    const size_t cut = s.a.size();
    rig.Write(0, 0xB0, 0x31);
    Run(rig, s, 2000);
    const double ours = RmsRange(s.a, cut, 1000), ymfm = RmsRange(s.b, cut, 1000);
    std::printf("  first 20 ms rms: ours %.0f ymfm %.0f (x%.3f)\n", ours, ymfm, ours / ymfm);
    CHECK(ours > 0.8 * ymfm && ours < 1.25 * ymfm);
}

double TimeToFraction(const std::vector<int32_t>& v, double fraction)
{
    double peak = 0;
    for (size_t i = 0; i + 256 <= v.size(); i += 256)
        peak = std::max(peak, RmsRange(v, i, 256));
    for (size_t i = 0; i + 256 <= v.size(); i += 256)
        if (RmsRange(v, i, 256) >= fraction * peak)
            return static_cast<double>(i);
    return -1;
}

void CompareSlowAttackTiming()
{
    std::printf("FmCompare: slow attack timing (96 dB envelope floor)\n");
    Rig rig;
    rig.Write(1, 0x05, 0x01);
    const uint8_t w[][2] = {
        {0x20, 0x21}, {0x23, 0x21}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xFF}, {0x63, 0x20},
        {0x80, 0x0F}, {0x83, 0x0F}, {0xC0, 0x31}, {0xA0, 0x40}, {0xB0, 0x32},
    };
    for (const auto& x : w)
        rig.Write(0, x[0], x[1]);
    TwoStreams s;
    Run(rig, s, 49716 * 4);
    const double ours = TimeToFraction(s.a, 0.5), ymfm = TimeToFraction(s.b, 0.5);
    std::printf("  AR2 time to -6 dB: ours %.0f ymfm %.0f samples (x%.3f)\n", ours, ymfm, ours / ymfm);
    CHECK(ours > 0.9 * ymfm && ours < 1.1 * ymfm);
}

void CompareNtsKsrDecay()
{
    std::printf("FmCompare: NTS (bank-0 reg 0x08) picks the KSR keycode bit\n");
    Rig rig;
    rig.Write(1, 0x05, 0x01);
    rig.Write(0, 0x08, 0x40); // NTS: keycode takes F-number bit 8
    const uint8_t w[][2] = {
        {0x20, 0x31}, {0x23, 0x31}, {0x40, 0x3F}, {0x43, 0x00}, {0x60, 0xFF}, {0x63, 0xF3},
        {0x80, 0x0F}, {0x83, 0xF2}, {0xC0, 0x31},
        {0xA0, 0x9A}, {0xB0, 0x31}, // fnum 0x19A: bit 8 set, bit 9 clear
    };
    for (const auto& x : w)
        rig.Write(0, x[0], x[1]);
    TwoStreams s;
    Run(rig, s, 49716);
    const double ours = RmsRange(s.a, 44000, 4000), ymfm = RmsRange(s.b, 44000, 4000);
    const double db = 20.0 * std::log10(ours / ymfm);
    std::printf("  level after 1 s: ours %.0f ymfm %.0f (%+.2f dB)\n", ours, ymfm, db);
    CHECK(std::fabs(db) < 1.0);
}

} // namespace

void RunFmBackendCompareTests()
{
    std::printf("\n--- FM backend differential (in-tree vs ymfm OPL3) ---\n");
    CompareVoiceStreams();
    CompareClassicMapVoice();
    CompareAdapterSaveRestore();
    CompareFourOpDrum();
    CompareFourOpDrumRetrigger();
    CompareLeadVoice();
    CompareDr0PadVoice();
    CompareRetriggerKeepsLevel();
    CompareRekeyAfterGapStartsFromLevel();
    CompareSlowAttackTiming();
    CompareNtsKsrDecay();
}

} // namespace opl4test
