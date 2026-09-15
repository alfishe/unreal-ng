// opl4fmcompare — differential comparator: the in-tree Opl4Fm vs the
// Opl4FmYmfm adapter (ymfm OPL3 core), one register stream into both
// (harness design §4: state diff fires first, samples last).
//
// Agreement levels:
//   - exact (CHECK): NEW/NEW2 flags, bank-1 aliasing, timers T1/T2 with
//     their status bits, raw register-file storage — both models implement
//     the same audited semantics, so any mismatch is an adapter bug;
//   - tight band: pitch of a per-engine-addressed voice (both engines run
//     the same 684-clock FM grid; phase-step formulas are equivalent);
//   - reported, not asserted: classic-map interpretation. Opl4Fm maps the
//     per-operator register families linearly (op = reg-0x20, channels
//     pair {2i, 2i+1}) while YMF262 silicon — and ymfm — use the classic
//     layout with gaps at 0x27/0x2F (ops 0-5/6-11/12-17 at +0/+8/+0x10,
//     channels pair {i, i+3}). The classic-voice scenario below measures
//     the divergence: it is the prime suspect for FM "wrong instruments"
//     (and silent channels) on real register streams, which is what the
//     ymfm backend exists to A/B — findings, not comparator failures.
//     Two more reported divergences surface there: (a) Opl4Fm's 0xC0
//     routing bits EXCLUDE a channel from L/R (0x30 = fully silent,
//     0x00 = both sides) — YMF262 CHA/CHB INCLUDE (0x30 = both); (b)
//     an untouched carrier has AR 0 in Opl4Fm (never attacks, silent)
//     — compounded by the map divergence since classic scripts write
//     envelope registers to addresses our map routes elsewhere.
#include "testfw.h"
#include "opl4fmymfm.h"

#include <cmath>

namespace opl4test
{

namespace
{

using EngineA = Opl4Fm;     // in-tree compact model
using EngineB = Opl4FmYmfm; // ymfm OPL3 core

// Deterministic LCG so the sweep is identical across runs and machines.
uint32_t Lcg(uint32_t& s)
{
    s = s * 1664525u + 1013904223u;
    return s >> 8;
}

void Step(EngineA& a, EngineB& b, int32_t& la, int32_t& ra,
          int32_t& lb, int32_t& rb, std::array<int32_t, 18>& ta,
          std::array<int32_t, 18>& tb)
{
    // One Advance() per FM step only: Opl4Fm::Advance (and, for contract
    // parity, the adapter's) already self-ticks the timers — an extra
    // AdvanceTimers() call would double the timer rate on both engines.
    a.Advance(la, ra, ta);
    b.Advance(lb, rb, tb);
}

// Engines used standalone (not through Opl4, which Reset()s them on chip
// reset) must be Reset() explicitly: Opl4Fm's default constructor leaves the
// channel->operator map zeroed and the routing registers silent.
void Init(EngineA& a, EngineB& b)
{
    a.Reset();
    b.Reset();
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

// Level 2 — state: flags, aliasing, timers, register storage.
void CompareFlagsTimersRegs()
{
    std::printf("FmCompare: flags/timers/registers\n");
    EngineA a;
    EngineB b;
    Init(a, b);

    // Bank-1 aliasing while NEW is clear: 0x30 must land in bank 0 on both.
    a.WriteReg(1, 0x30, 0x11);
    b.WriteReg(1, 0x30, 0x11);
    CHECK(!a.NewMode() && !b.NewMode());
    CHECK(!a.New2() && !b.New2());
    CHECK_EQ_I(a.Regs()[0x30], 0x11);
    CHECK_EQ_I(b.YmfmReg(0x30), 0x11);
    CHECK_EQ_I(b.YmfmReg(0x130), 0x00); // bank 1 untouched

    // 0x105 arms NEW/NEW2 and opens bank 1 on both.
    a.WriteReg(1, 0x05, 0x03);
    b.WriteReg(1, 0x05, 0x03);
    CHECK(a.NewMode() && b.NewMode());
    CHECK(a.New2() && b.New2());
    a.WriteReg(1, 0x30, 0x22);
    b.WriteReg(1, 0x30, 0x22);
    CHECK_EQ_I(b.YmfmReg(0x130), 0x22);

    // Timers T1/T2: load, enable, expiry, mask, reset — status must track
    // identically step for step (the audited semantics, field-for-field).
    const struct
    {
        uint8_t reg, data;
    } timerScript[] = {
        {0x02, 0xFE}, // T1 load: period (0x100-0xFE)*4 = 8 steps
        {0x03, 0xFF}, // T2 load: period (0x100-0xFF)*16 = 16 steps
        {0x04, 0x03}, // both enabled, unmasked
    };
    for (const auto& w : timerScript)
    {
        a.WriteReg(0, w.reg, w.data);
        b.WriteReg(0, w.reg, w.data);
    }
    std::array<int32_t, 18> ta, tb;
    int32_t la, ra, lb, rb;
    bool sawT1 = false, sawT2 = false;
    for (int step = 0; step < 20; step++)
    {
        Step(a, b, la, ra, lb, rb, ta, tb);
        CHECK_EQ_I(a.Status() & 0x60, b.Status() & 0x60);
        sawT1 = sawT1 || (a.Status() & EngineA::kStatusT1);
        sawT2 = sawT2 || (a.Status() & EngineA::kStatusT2);
        if (step == 17) // both latched (T1@7, T2@15): clear, keep enabled, mask T2
        {
            a.WriteReg(0, 0x04, 0xA3); // RST | enables | T2 mask (0x20)
            b.WriteReg(0, 0x04, 0xA3);
        }
    }
    CHECK(sawT1 && sawT2);
    CHECK_EQ_I(a.Status() & EngineA::kStatusT2, 0); // cleared + masked

    // Register storage sweep: 512 pseudo-random bytes through both banks
    // (0x104/0x105 kept stable; 0x04 written without bit 7 — ymfm ORs the
    // RST bit into the stored byte, ours stores it raw).
    uint32_t seed = 0xBEEF;
    for (int bank = 0; bank < 2; bank++)
        for (int reg = 0; reg < 0x100; reg++)
        {
            if (bank == 1 && (reg == 0x04 || reg == 0x05))
                continue;
            if (bank == 0 && reg == 0x04)
                continue;
            uint8_t data = static_cast<uint8_t>(Lcg(seed));
            a.WriteReg(static_cast<uint8_t>(bank), static_cast<uint8_t>(reg), data);
            b.WriteReg(static_cast<uint8_t>(bank), static_cast<uint8_t>(reg), data);
        }
    for (int r = 0; r < 0x200; r++)
    {
        if (r == 0x04 || r == 0x104 || r == 0x105)
            continue;
        if (a.Regs()[r] != b.YmfmReg(r))
        {
            CHECK_EQ_I(a.Regs()[r], b.YmfmReg(r)); // report first mismatch
            return;
        }
    }
    CHECK(true); // full register file agrees byte for byte
}

// Per-engine-addressed pure-carrier voice (silent modulator). The two
// engines' pitch must agree tightly: same 684-clock grid, equivalent
// datasheet phase-step math.
//   Opl4Fm (linear map): carrier TL at 0x21, F-number/keys ch0.
//   ymfm   (classic map): carrier TL at 0x23, C0 output bits set.
void VoicePerEngine(EngineA& a, EngineB& b)
{
    a.WriteReg(0, 0x20, 0x01);  // mod: mult 1
    a.WriteReg(0, 0x21, 0x01);  // car: mult 1
    a.WriteReg(0, 0x40, 0x3F);  // mod TL: silent
    a.WriteReg(0, 0x61, 0xF0);  // car AR 15
    a.WriteReg(0, 0x81, 0x00);  // car SL 0 RR 0
    a.WriteReg(0, 0xA0, 0x03);  // F-number 0x303, block 2
    a.WriteReg(0, 0xB0, 0x2B);
    a.WriteReg(0, 0xC0, 0x00);  // our routing bits EXCLUDE sides: 0 = both

    b.WriteReg(0, 0x20, 0x01);
    b.WriteReg(0, 0x23, 0x01);  // classic carrier
    b.WriteReg(0, 0x40, 0x3F);
    b.WriteReg(0, 0x63, 0xF0);
    b.WriteReg(0, 0x83, 0x00);
    b.WriteReg(0, 0xA0, 0x03);
    b.WriteReg(0, 0xB0, 0x2B);
    b.WriteReg(0, 0xC0, 0x30); // classic CHA+CHB INCLUDE: both stereo sides
}

// Level 3 — samples: pitch (tight) and envelope shape (reported).
void CompareVoiceStreams()
{
    std::printf("FmCompare: voice streams (per-engine addressing)\n");
    EngineA a;
    EngineB b;
    Init(a, b);
    a.WriteReg(1, 0x05, 0x01); // NEW: bank-1 register file on both
    b.WriteReg(1, 0x05, 0x01);
    VoicePerEngine(a, b);

    const size_t n = 6000;
    std::vector<int32_t> la(n), lb(n);
    std::array<int32_t, 18> ta, tb;
    int32_t ra, rb;
    for (size_t i = 0; i < n; i++)
    {
        Step(a, b, la[i], ra, lb[i], rb, ta, tb);
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

    // Envelope: instant attack then release. The decay-rate machinery is a
    // documented numeric difference (compact model vs ymfm 5.11 counter),
    // so only the shape class is asserted: silence after key-off in both.
    a.WriteReg(0, 0xB0, 0x0B); // key off (block/fnum kept)
    b.WriteReg(0, 0xB0, 0x0B);
    double tailA = 0, tailB = 0;
    for (int i = 0; i < 20000; i++)
    {
        int32_t xa, xb;
        Step(a, b, xa, ra, xb, rb, ta, tb);
        tailA += std::fabs(double(xa));
        tailB += std::fabs(double(xb));
    }
    std::printf("  release: |tail| sum ours=%.0f ymfm=%.0f\n", tailA, tailB);
    CHECK(tailA / (tailA + tailB + 1e-30) < 0.999);
    CHECK(tailB / (tailA + tailB + 1e-30) < 0.999);
}

// Classic-map voice, IDENTICAL bytes into both engines (what a real OPL3
// driver emits for ch0). Reported, not asserted: measures the linear-vs-
// classic operator-map divergence — the "wrong instruments" suspect.
void ReportClassicMapDivergence()
{
    std::printf("FmCompare: classic-map voice (divergence report)\n");
    EngineA a;
    EngineB b;
    Init(a, b);
    a.WriteReg(1, 0x05, 0x01);
    b.WriteReg(1, 0x05, 0x01);

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
    {
        a.WriteReg(w[0], w[1], w[2]);
        b.WriteReg(w[0], w[1], w[2]);
    }

    const size_t n = 6000;
    std::vector<int32_t> la(n), lb(n);
    std::array<int32_t, 18> ta, tb;
    int32_t ra, rb;
    for (size_t i = 0; i < n; i++)
        Step(a, b, la[i], ra, lb[i], rb, ta, tb);

    const double rmsA = Rms(la, n);
    const double rmsB = Rms(lb, n);
    const double za = ZeroCrossRate(la, n);
    const double zb = ZeroCrossRate(lb, n);
    const bool diverged = std::fabs(rmsA / (rmsB + 1e-30) - 1.0) > 0.25
        || std::fabs(za / (zb + 1e-30) - 1.0) > 0.01;
    std::printf("  identical bytes: rms ours=%.0f ymfm=%.0f, pitch ratio %.4f"
                " -> %s\n",
                rmsA, rmsB, za / (zb + 1e-30),
                diverged
                    ? "DIVERGENT (linear vs classic operator map: envelope"
                      " writes land on the wrong operators — our ch0 carrier"
                      " keeps AR 0 and never attacks; plus 0xC0 routing"
                      " polarity: 0x31 excludes both sides here, includes"
                      " both on YMF262)"
                    : "agree");
    CHECK(true); // informational: the finding is reported above
}

// TTD contract on the adapter: save is pure (a chip that saved renders
// identically to one that never did) and restore is an exact continuation.
void CompareAdapterSaveRestore()
{
    std::printf("FmCompare: adapter save/restore\n");
    EngineB src, twin, restored;
    src.Reset();
    twin.Reset();
    restored.Reset();
    for (EngineB* e : {&src, &twin, &restored})
    {
        e->WriteReg(1, 0x05, 0x01);
        e->WriteReg(0, 0x20, 0x01);
        e->WriteReg(0, 0x23, 0x01);
        e->WriteReg(0, 0x40, 0x3F);
        e->WriteReg(0, 0x63, 0xF0);
        e->WriteReg(0, 0x83, 0x00);
        e->WriteReg(0, 0xA0, 0x03);
        e->WriteReg(0, 0xB0, 0x2B);
        e->WriteReg(0, 0xC0, 0x30);
    }
    std::array<int32_t, 18> t1, t2;
    int32_t l, r;
    for (int i = 0; i < 500; i++)
    {
        src.Advance(l, r, t1);
        twin.Advance(l, r, t2);
    }

    std::vector<uint8_t> blob(EngineB::kStateSize);
    src.SaveState(blob.data());

    restored.LoadState(blob.data());
    for (int i = 0; i < 2000; i++)
    {
        int32_t ls, lt, lr;
        src.Advance(ls, lr, t1);
        twin.Advance(lt, lr, t2);
        restored.Advance(l, r, t2);
        CHECK_EQ_I(ls, l); // restored continues the saved chip exactly
        if (ls != l)
            return;
        CHECK_EQ_I(ls, lt); // saving was side-effect free
        if (ls != lt)
            return;
    }
    CHECK_EQ_I(src.Status(), restored.Status());
    CHECK_EQ_I(src.Status(), twin.Status());
}

} // namespace

void RunFmBackendCompareTests()
{
    std::printf("\n--- FM backend differential (in-tree vs ymfm OPL3) ---\n");
    CompareFlagsTimersRegs();
    CompareVoiceStreams();
    ReportClassicMapDivergence();
    CompareAdapterSaveRestore();
}

} // namespace opl4test
