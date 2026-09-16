// §12.1/12.2 vector suite — exact-value and behavioural vectors beyond the
// core unit tests: loop-overrun regression (the endAddr complement bug),
// full calcStep sweep, per-width golden frames, interpolation carry,
// envelope rate matrix, LFO/pan/clip vectors and FM feature vectors.
// Shares tests/testfw.h; the counters live in the opl4tests.cpp TU.
#include "testfw.h"

#include <functional>

namespace opl4test
{

namespace
{

// The exact engine output chain for one PCM slot, rebuilt from public
// primitives only (unity mix 0xF9=0, silent FM): sample -> env -> TL -> pan
// -> ClampRail. Comparing rendered frames against this is a golden vector.
// Output is normalized to [-1, 1] via kNormScale.
inline float GoldenFrame(int16_t sample, uint16_t panAtt)
{
    const int32_t v = VolFactor(VolFactor(sample, 0), 0);
    // Wide rail: ±(32767 << kRailShift)
    constexpr int32_t kRail = 32767 << kRailShift;
    const int32_t clamped = std::max(-kRail - 1, std::min(kRail, VolFactor(v, panAtt)));
    return static_cast<float>(clamped) * kNormScale;
}

// Parameterised PCM key-on. Register order matters: the wave-low write
// triggers the tone fetch which rewrites banks 5..9 (D7), so envelope
// registers must come after it (same contract as KeyOnPcmSlot).
inline void KeyOnPcmParam(Opl4& c, int slot, uint64_t t, int oct, int fn,
                          uint8_t pan, uint8_t ar, uint8_t d1r, uint8_t dl,
                          uint8_t d2r, uint8_t rc, uint8_t rr, uint8_t tl,
                          bool prvb = false, bool damp = false)
{
    const uint8_t b = static_cast<uint8_t>(0x08 + slot);
    c.WriteWave(t, 0x02, 0x10); // header base 4, MA 0
    c.WriteWave(t, static_cast<uint8_t>(b + 24),
                static_cast<uint8_t>(0x01 | ((fn & 0x7F) << 1)));
    c.WriteWave(t, b, 0x80); // wave 384 -> fetch from kHdrBase
    c.WriteWave(t, static_cast<uint8_t>(b + 48),
                static_cast<uint8_t>(((oct & 0xF) << 4) | (prvb ? 0x08 : 0)
                                     | ((fn >> 7) & 7)));
    c.WriteWave(t, static_cast<uint8_t>(b + 24 * 6), static_cast<uint8_t>((ar << 4) | d1r));
    c.WriteWave(t, static_cast<uint8_t>(b + 24 * 7), static_cast<uint8_t>((dl << 4) | d2r));
    c.WriteWave(t, static_cast<uint8_t>(b + 24 * 8), static_cast<uint8_t>((rc << 4) | rr));
    c.WriteWave(t, static_cast<uint8_t>(b + 24 * 3), static_cast<uint8_t>((tl << 1) | 0x01));
    c.WriteWave(t, static_cast<uint8_t>(b + 24 * 4),
                static_cast<uint8_t>(0x80 | (damp ? 0x40 : 0) | pan));
}

// 8 distinct 16-bit samples (never zero, never clip-relevant).
inline void WriteDistinct16(WaveMemory& mem, int count)
{
    uint8_t raw[64];
    for (int i = 0; i < count; i++)
    {
        const uint16_t v = static_cast<uint16_t>(0x8000 + i * 0x0F11);
        raw[i * 2] = static_cast<uint8_t>(v >> 8);
        raw[i * 2 + 1] = static_cast<uint8_t>(v & 0xFF);
    }
    mem.WriteSram(kSmpBase, raw, static_cast<uint32_t>(count * 2));
    mem.ClearDirty();
}

inline std::vector<float> CaptureFrames(TestChip& tc, uint64_t outSteps)
{
    tc.chip.Run(outSteps * kOutClocks);
    std::vector<float> out;
    float buf[16384];
    size_t n;
    while ((n = tc.chip.Render(buf, 8192)) > 0)
        out.insert(out.end(), buf, buf + n * 2);
    return out;
}

inline int StepsUntil(TestChip& tc, int slot,
                      const std::function<bool(const PcmSlot&)>& pred, int maxSteps)
{
    uint64_t t = 0;
    for (int k = 1; k <= maxSteps; k++)
    {
        t += kOutClocks;
        tc.chip.Run(t);
        if (pred(tc.chip.PcmForTest().Slots()[slot]))
            return k;
    }
    return maxSteps + 1;
}

} // namespace

// ---------------------------------------------------------------------------
// Loop overrun (§5.4): wrap sequence, overrun carry, E=0 degenerate, one-shot
// ---------------------------------------------------------------------------
void VecLoopOverrun()
{
    std::printf("VecLoopOverrun\n");
    {
        // Regression for the endAddr complement bug: oct 1 / fn 0 gives a
        // step of exactly 65536 (fraction 0, one sample per output step).
        // end 4, loop 1: 0,1,2,3 -> wrap to 1 (4 + loop - end).
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 1, 4, nullptr);
        WriteDistinct16(tc.mem, 8);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].endAddr, 0x10000 - 4);
        const int seq1[] = {1, 2, 3, 1, 2, 3, 1, 2, 3, 1};
        uint64_t t = 0;
        for (int k = 0; k < 10; k++)
        {
            t += kOutClocks;
            tc.chip.Run(t);
            CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].pos, seq1[k]);
        }
    }
    {
        // Overrun carries into the loop: step exactly 2 (oct 2 / fn 0 =
        // 131072): 0,2 -> 1,3 -> 2,1,3,2,1...
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 1, 4, nullptr);
        WriteDistinct16(tc.mem, 8);
        KeyOnPcmParam(tc.chip, 0, 0, 2, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        const int seq2[] = {2, 1, 3, 2, 1, 3, 2, 1};
        uint64_t t = 0;
        for (int k = 0; k < 8; k++)
        {
            t += kOutClocks;
            tc.chip.Run(t);
            CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].pos, seq2[k]);
        }
    }
    {
        // Stored end complement S = 0 (a full 64 KiB sample): the loop test
        // pos + S >= 0x10000 can never trip, so playback stays linear with
        // the natural 16-bit wrap and loopAddr is irrelevant (openMSX-exact;
        // the old pre-negated engine wrapped every step and scanned memory).
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 5, 0, nullptr);
        WriteDistinct16(tc.mem, 8);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].endAddr, 0);
        uint64_t t = 0;
        for (int k = 1; k <= 4; k++)
        {
            t += kOutClocks;
            tc.chip.Run(t);
            CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].pos, k);
        }
    }
    {
        // One-shot: end far away, loop 0 — the position just counts up.
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 0x1000, nullptr);
        WriteDistinct16(tc.mem, 8);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        uint64_t t = 0;
        for (int k = 1; k <= 8; k++)
        {
            t += kOutClocks;
            tc.chip.Run(t);
            CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].pos, k);
        }
    }
}

// ---------------------------------------------------------------------------
// calcStep sweep (§5.3): full oct x fn grid against the closed form
// ---------------------------------------------------------------------------
void VecCalcStepSweep()
{
    std::printf("VecCalcStepSweep\n");
    int bad = 0;
    for (int oct = -8; oct <= 7; oct++)
        for (int fn = 0; fn < 1024; fn++)
        {
            const uint32_t want = (oct == -8)
                ? 0u
                : (static_cast<uint32_t>(1024 + fn) << (8 + oct)) >> 3;
            if (CalcStep(oct, fn) != want)
                bad++;
        }
    CHECK_EQ_I(bad, 0);

    // Vibrato term sweeps: the depth value adds to fnumber before the shift.
    bad = 0;
    for (int oct = -8; oct <= 7; oct++)
        for (int fn : {0, 511, 1023})
            for (int vib : {0, 1, 127, 255})
            {
                const uint32_t want = (oct == -8)
                    ? 0u
                    : (static_cast<uint32_t>(1024 + fn + vib) << (8 + oct)) >> 3;
                if (CalcStep(oct, fn, vib) != want)
                    bad++;
            }
    CHECK_EQ_I(bad, 0);

    // Strictly increasing in fn within an octave; exactly doubling per
    // octave — exact only while the >>3 drops no set bits (oct >= -5;
    // the closed form itself is asserted above for the full grid).
    bad = 0;
    for (int oct = -5; oct <= 7; oct++)
    {
        for (int fn = 0; fn < 1023; fn++)
            if (!(CalcStep(oct, fn) < CalcStep(oct, fn + 1)))
                bad++;
        if (oct < 7)
            for (int fn = 0; fn < 1024; fn++)
                if (CalcStep(oct + 1, fn) != CalcStep(oct, fn) * 2)
                    bad++;
    }
    CHECK_EQ_I(bad, 0);

    // Truncation-floor vectors below the exact domain (oct < -5):
    CHECK_EQ_I(CalcStep(-6, 1), static_cast<uint32_t>((1025u << 2) >> 3));
    CHECK_EQ_I(CalcStep(-7, 5), static_cast<uint32_t>((1029u << 1) >> 3));
    CHECK_EQ_I(CalcStep(-8, 1023), 0u); // frozen octave

    CHECK_EQ_I(CalcStep(1, 0), 65536);  // exact 1 sample/step (vector tests)
    CHECK_EQ_I(CalcStep(2, 0), 131072); // exact 2 samples/step
    CHECK_EQ_I(CalcStep(0, 512), 49152); // 0.75 samples/step
}

// ---------------------------------------------------------------------------
// Per-width golden frames: the rendered stream equals the public-primitive
// chain exactly (unity env/TL, pan centre, unity PCM mix)
// ---------------------------------------------------------------------------
void VecWidthGolden()
{
    std::printf("VecWidthGolden\n");
    struct Case
    {
        uint8_t bits;
        int nbytes;
        const uint8_t* raw;
    };
    const uint8_t raw8[8] = {0x00, 0x7F, 0x80, 0xFF, 0x55, 0xAA, 0x01, 0xFE};
    const uint8_t raw12[12] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                               0xDE, 0xF0, 0x0F, 0x1A, 0x2B, 0x3C};
    const uint8_t raw16[16] = {0x7F, 0xFF, 0x80, 0x00, 0x12, 0x34, 0xFF, 0xFF,
                               0x00, 0x01, 0x55, 0x55, 0xAA, 0xAA, 0xFE, 0xDC};
    const Case cases[3] = {{0, 8, raw8}, {1, 12, raw12}, {2, 16, raw16}};

    for (const Case& cs : cases)
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, cs.bits, kSmpBase, 0, 8, nullptr);
        tc.mem.WriteSram(kSmpBase, cs.raw, static_cast<uint32_t>(cs.nbytes));
        tc.mem.ClearDirty();
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0); // step 1

        const auto sampleAt = [&cs](int p) -> int16_t
        {
            switch (cs.bits)
            {
            case 0:
                return static_cast<int16_t>(cs.raw[p] << 8);
            case 1:
            {
                const int a = (p / 2) * 3;
                if (p & 1)
                    return static_cast<int16_t>((cs.raw[a + 2] << 8) | (cs.raw[a + 1] & 0xF0));
                return static_cast<int16_t>((cs.raw[a] << 8) | ((cs.raw[a + 1] << 4) & 0xF0));
            }
            default:
                return static_cast<int16_t>((cs.raw[p * 2] << 8) | cs.raw[p * 2 + 1]);
            }
        };

        const std::vector<float> frames = CaptureFrames(tc, 17);
        CHECK_EQ_I(frames.size() / 2, 17u);
        for (int k = 0; k < 16; k++)
        {
            const float want = GoldenFrame(sampleAt(k & 7), 0);
            CHECK_EQ_F(frames[k * 2 + 0], want); // left
            CHECK_EQ_F(frames[k * 2 + 1], want); // right (pan centre)
        }
    }
}

// ---------------------------------------------------------------------------
// Interpolation carry: oct 0 / fn 512 = 0.75 samples per step — the 16.16
// fraction cycles C000,8000,4000,0000 and the +1 neighbour wraps at the end
// ---------------------------------------------------------------------------
void VecInterpGolden()
{
    std::printf("VecInterpGolden\n");
    TestChip tc;
    WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 8, nullptr);
    uint8_t raw[16];
    for (int i = 0; i < 8; i++)
    {
        const uint16_t v = static_cast<uint16_t>(0x1000 + i * 0x0800);
        raw[i * 2] = static_cast<uint8_t>(v >> 8);
        raw[i * 2 + 1] = static_cast<uint8_t>(v & 0xFF);
    }
    tc.mem.WriteSram(kSmpBase, raw, 16);
    tc.mem.ClearDirty();
    KeyOnPcmParam(tc.chip, 0, 0, 0, 512, 0, 15, 0, 0, 0, 15, 0, 0); // step 49152

    const auto d = [&raw](int p)
    { return (raw[p * 2] << 8) | raw[p * 2 + 1]; };

    // Mirror of the engine: interpolate first, advance after.
    uint32_t sp = 0;
    int pos = 0;
    const std::vector<float> frames = CaptureFrames(tc, 17);
    CHECK_EQ_I(frames.size() / 2, 17u);
    for (int k = 0; k < 16; k++)
    {
        const uint32_t frac = sp & 0xFFFF;
        int next = pos + 1;
        if (next >= 8)
            next -= 8; // same wrap as NextPos (end 8, loop 0)
        const int64_t s = (static_cast<int64_t>(d(pos)) * (0x10000 - frac)
                           + static_cast<int64_t>(d(next)) * frac) >> 16;
        const float want = GoldenFrame(static_cast<int16_t>(s), 0);
        CHECK_EQ_F(frames[k * 2 + 0], want);
        CHECK_EQ_F(frames[k * 2 + 1], want);
        sp += 49152;
        if (sp >= 0x10000)
        {
            pos += static_cast<int>(sp >> 16);
            while (pos >= 8)
                pos -= 8;
            sp &= 0xFFFF;
        }
    }
}

// ---------------------------------------------------------------------------
// Envelope rate matrix (§5.2): attack monotonicity, decay freeze at DL,
// release rates, RC speedup
// ---------------------------------------------------------------------------
void VecEnvMatrix()
{
    std::printf("VecEnvMatrix\n");
    // AR 15 is zero-time: full scale immediately after key-on.
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].envVol, 0);
    }
    // Attack time non-increasing across AR 6..14 (rc 15: rate = 4*AR).
    int prev = -1;
    for (int ar = 6; ar <= 14; ar += 2)
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, static_cast<uint8_t>(ar), 0, 0, 0, 15, 0, 0);
        const int steps = StepsUntil(tc, 0, [](const PcmSlot& s) { return s.envVol == 0; }, 30000);
        CHECK(steps <= 30000);
        if (prev > 0)
            CHECK(steps < prev); // strictly faster for these spaced rates
        prev = steps;
    }
    // AR 0 never attacks (rate 0 = infinity row, zero increment).
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 0, 0, 0, 0, 15, 0, 0);
        tc.chip.Run(500 * kOutClocks);
        CHECK(tc.chip.PcmForTest().Slots()[0].envVol > 600);
    }
    // Decay reaches DL (256) and freezes there with D2R 0; the overshoot is
    // bounded by one increment (<= 8 index units).
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 8, 8, 0, 15, 0, 0);
        // Decay to DL 256 at rate 32 (D1R 8, rc 15): row 0 / shift 4 —
        // 0.5 index per 16 samples, ~8192 steps; assert with margin.
        const int steps = StepsUntil(
            tc, 0, [](const PcmSlot& s) { return s.state == EgPhase::Sus; }, 12000);
        CHECK(steps <= 12000);
        const PcmSlot& s = tc.chip.PcmForTest().Slots()[0];
        CHECK(s.envVol >= 256 && s.envVol <= 256 + 8);
        tc.chip.Run((steps + 300) * kOutClocks);
        CHECK_EQ_I(tc.chip.PcmForTest().Slots()[0].envVol, s.envVol); // frozen
    }
    // Release: RR 15 turns the voice off quickly; RR 0 holds.
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 15, 0);
        tc.chip.WriteWave(100 * kOutClocks, 0x68, 0x00); // key off (bank 4)
        CHECK(StepsUntil(tc, 0, [](const PcmSlot& s) { return s.state == EgPhase::Off; }, 400) <= 400);
    }
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        tc.chip.WriteWave(100 * kOutClocks, 0x68, 0x00);
        tc.chip.Run(400 * kOutClocks);
        CHECK(tc.chip.PcmForTest().Slots()[0].state == EgPhase::Rel); // held
    }
    // Rate correction: with oct 4, rc 0 adds 2*(4+0) to the rate, so the
    // rc-0 attack is strictly faster than rc 15 (correction disabled).
    {
        TestChip tcA, tcB;
        for (TestChip* tc : {&tcA, &tcB})
        {
            WriteToneHeader(tc->mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
            WriteMaxSampleData(tc->mem);
        }
        KeyOnPcmParam(tcA.chip, 0, 0, 4, 0, 0, 8, 0, 0, 0, 15, 0, 0);
        KeyOnPcmParam(tcB.chip, 0, 0, 4, 0, 0, 8, 0, 0, 0, 0, 0, 0);
        const auto done = [](const PcmSlot& s) { return s.envVol == 0; };
        const int tA = StepsUntil(tcA, 0, done, 20000);
        const int tB = StepsUntil(tcB, 0, done, 20000);
        CHECK(tA <= 20000 && tB <= 20000);
        CHECK(tB < tA); // rc 0: rate 40 vs rc 15: rate 32
    }
}

// ---------------------------------------------------------------------------
// DAMP (two-tier forced decay) and pseudo reverb (-18 dB rate override)
// ---------------------------------------------------------------------------
void VecDamp()
{
    std::printf("VecDamp\n");
    // Damp overrides D1R with the manual two-tier curve and reaches Off
    // even with D1R 0 / D2R 0; the control freezes at zero attenuation.
    TestChip damp, ctrl;
    for (TestChip* tc : {&damp, &ctrl})
    {
        WriteToneHeader(tc->mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc->mem);
    }
    KeyOnPcmParam(damp.chip, 0, 0, 1, 0, 0, 15, 0, 15, 0, 15, 0, 0, false, true);
    KeyOnPcmParam(ctrl.chip, 0, 0, 1, 0, 0, 15, 0, 15, 0, 15, 0, 0);
    CHECK(StepsUntil(damp, 0, [](const PcmSlot& s) { return s.state == EgPhase::Off; }, 600) <= 600);
    ctrl.chip.Run(600 * kOutClocks);
    CHECK(ctrl.chip.PcmForTest().Slots()[0].state != EgPhase::Off);
    CHECK_EQ_I(ctrl.chip.PcmForTest().Slots()[0].envVol, 0); // frozen loud
}

void VecPrvb()
{
    std::printf("VecPrvb\n");
    // Pseudo reverb caps the decay rate at 20 while above -18 dB, so the
    // same D1R 15 voice takes visibly longer to reach its sustain level.
    TestChip prvb, ctrl;
    for (TestChip* tc : {&prvb, &ctrl})
    {
        WriteToneHeader(tc->mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc->mem);
    }
    KeyOnPcmParam(prvb.chip, 0, 0, 1, 0, 0, 15, 15, 8, 0, 15, 0, 0, true, false);
    KeyOnPcmParam(ctrl.chip, 0, 0, 1, 0, 0, 15, 15, 8, 0, 15, 0, 0);
    const auto sus = [](const PcmSlot& s) { return s.state == EgPhase::Sus; };
    const int tP = StepsUntil(prvb, 0, sus, 25000);
    const int tC = StepsUntil(ctrl, 0, sus, 25000);
    CHECK(tC <= 25000);
    CHECK(tP > tC); // rate 20 above -18 dB vs rate 60
    CHECK(tP <= 25000);
}

// ---------------------------------------------------------------------------
// PCM LFO: AM lowers RMS, vibrato drifts the position within bounds
// ---------------------------------------------------------------------------
void VecAmVib()
{
    std::printf("VecAmVib\n");
    const auto rms = [](const std::vector<float>& v)
    {
        double acc = 0.0;
        for (float x : v)
            acc += static_cast<double>(x) * x;
        return acc / static_cast<double>(v.size());
    };

    TestChip am, noam;
    for (TestChip* tc : {&am, &noam})
    {
        WriteToneHeader(tc->mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc->mem);
        KeyOnPcmParam(tc->chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        tc->chip.WriteWave(0, 0x08 + 24 * 5, 0x38); // bank 5: LFO speed 7, vib 0
    }
    am.chip.WriteWave(0, 0x08 + 24 * 9, 0x07); // bank 9: AM depth 7
    const double rAm = rms(CaptureFrames(am, 4000));
    const double rNo = rms(CaptureFrames(noam, 4000));
    CHECK(rNo > 0.0);
    CHECK(rAm < rNo * 0.98); // clearly modulated down
    CHECK(rAm > rNo * 0.25); // but never near-silence

    // Vibrato depth 7 swings the effective fnumber by at most ~60 units,
    // so after 2000 steps the position drifts but stays bounded.
    TestChip vib, ctrl;
    for (TestChip* tc : {&vib, &ctrl})
    {
        WriteToneHeader(tc->mem, kHdrBase, 2, kSmpBase, 0, 0x1000, nullptr);
        WriteDistinct16(tc->mem, 8);
        KeyOnPcmParam(tc->chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        tc->chip.WriteWave(0, 0x08 + 24 * 5, 0x38); // LFO speed 7, vib 0
    }
    vib.chip.WriteWave(0, 0x08 + 24 * 5, 0x3F); // speed 7 + vibrato depth 7
    int maxDrift = 0;
    bool drifted = false;
    uint64_t t = 0;
    for (int k = 1; k <= 2000; k++)
    {
        t += kOutClocks;
        vib.chip.Run(t);
        ctrl.chip.Run(t);
        const int drift = std::abs(static_cast<int>(vib.chip.PcmForTest().Slots()[0].pos)
                                   - static_cast<int>(ctrl.chip.PcmForTest().Slots()[0].pos));
        maxDrift = std::max(maxDrift, drift);
        if (drift > 0)
            drifted = true;
    }
    // Bound: depth 7 swings fn by <= 60 units ((15 * kVibDepth[7]) / 12);
    // the LFO speed-7 quarter cycle is ~1463 samples, so the accumulated
    // position lag peaks near 2 * 1463 * 30 * 64 / 65536 ≈ 86 samples.
    CHECK(drifted);
    CHECK(maxDrift <= 120);
}

// ---------------------------------------------------------------------------
// All 16 pan codes end-to-end against the pan table ratios (D8)
// ---------------------------------------------------------------------------
void VecPanSweep()
{
    std::printf("VecPanSweep\n");
    float peakL[16] = {}, peakR[16] = {};
    for (int p = 0; p < 16; p++)
    {
        TestChip tc;
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem, 0x6000); // mid-scale: no clipping
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, static_cast<uint8_t>(p), 15, 0, 0, 0, 15, 0, 0);
        const std::vector<float> frames = CaptureFrames(tc, 60);
        for (size_t i = 0; i < frames.size() / 2; i++)
        {
            peakL[p] = std::max(peakL[p], std::fabs(frames[i * 2]));
            peakR[p] = std::max(peakR[p], std::fabs(frames[i * 2 + 1]));
        }
    }
    const float baseL = peakL[0];
    const float baseR = peakR[0];
    CHECK(baseL > 20000.0f * kNormScale && baseR > 20000.0f * kNormScale);
    for (int p = 0; p < 16; p++)
    {
        const float wantL = static_cast<float>(VolFactor(20000, kPanTable[p].left)) / 20000.0f;
        const float wantR = static_cast<float>(VolFactor(20000, kPanTable[p].right)) / 20000.0f;
        if (wantL < 0.01f)
            CHECK(peakL[p] < 1.0f * kNormScale); // attenuated to silence
        else
            CHECK(std::fabs(peakL[p] / baseL - wantL) < 0.02f);
        if (wantR < 0.01f)
            CHECK(peakR[p] < 1.0f * kNormScale);
        else
            CHECK(std::fabs(peakR[p] / baseR - wantR) < 0.02f);
    }
}

// ---------------------------------------------------------------------------
// Negative sum and foreign-blob save robustness
// ---------------------------------------------------------------------------
void VecNegativeClip()
{
    std::printf("VecNegativeClip\n");
    TestChip tc;
    WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
    WriteMaxSampleData(tc.mem, 0x8000); // -32768
    KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
    KeyOnPcmParam(tc.chip, 1, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
    const std::vector<float> frames = CaptureFrames(tc, 30);
    CHECK(frames.size() >= 60);
    // Two slots at -32768 sum to -65536, within wide rail (±131068).
    // Normalized: -65536 / 131072 ≈ -0.5
    for (size_t i = 0; i < frames.size(); i++)
        CHECK(frames[i] < -0.49f && frames[i] > -0.51f);
}

void VecSaveRobust()
{
    std::printf("VecSaveRobust\n");
    const auto run = [](TestChip& tc)
    {
        WriteToneHeader(tc.mem, kHdrBase, 2, kSmpBase, 0, 4, nullptr);
        WriteMaxSampleData(tc.mem);
        KeyOnFmCh0(tc.chip, 0);
        KeyOnPcmParam(tc.chip, 0, 0, 1, 0, 0, 15, 0, 0, 0, 15, 0, 0);
        return CaptureFrames(tc, 400);
    };
    TestChip a, b;
    const std::vector<float> outA = run(a);
    std::vector<uint8_t> garbage(a.chip.StateSize(), 0xAA);
    b.chip.LoadState(garbage.data()); // wrong magic: must be refused
    const std::vector<float> outB = run(b);
    CHECK_EQ_I(outA.size(), outB.size());
    CHECK(outA.size() == outB.size()
          && std::equal(outA.begin(), outA.end(), outB.begin()));
    float m = 0.0f;
    for (float v : outA)
        m = std::max(m, std::fabs(v));
    CHECK(m > 1000.0f * kNormScale); // sanity: the comparison is not vacuous
}

// ---------------------------------------------------------------------------
// FM vectors: 4-op algorithms, rhythm mode, timers, LFO, KSL
// ---------------------------------------------------------------------------
namespace
{

// Operator voice over the engine's register-linear op map: bank b, slot k
// uses regs 0x20+k / 0x40+k / 0x60+k / 0x80+k (k 0..21; the classic map's
// gap slots 6/7/14/15 belong to no channel). (Unused on the ymfm backend
// build, which exposes no operator internals.)
[[maybe_unused]] inline void FmOpVoice(Opl4& c, uint64_t t, int bank, int k, uint8_t flags20)
{
    c.WriteFm(t, bank, static_cast<uint8_t>(0x20 + k), flags20);
    c.WriteFm(t, bank, static_cast<uint8_t>(0x40 + k), 0x00); // TL 0
    c.WriteFm(t, bank, static_cast<uint8_t>(0x60 + k), 0xF5); // AR 15 DR 5
    c.WriteFm(t, bank, static_cast<uint8_t>(0x80 + k), 0x0F); // SL 0 RR 15
}

inline double RmsOf(const std::vector<float>& v)
{
    double acc = 0.0;
    for (float x : v)
        acc += static_cast<double>(x) * x;
    return acc / static_cast<double>(v.size());
}

} // namespace

void VecFm4Op()
{
    std::printf("VecFm4Op\n");
#if !defined(OPL4_FM_YMFM) // pins the in-tree engine via per-channel
    // peaks; the ymfm backend zeroes taps
    const auto run4 = [](TestChip& tc, uint8_t conn)
    {
        tc.chip.WriteWave(0, 0xF8, 0x00); // FM block mix unity (sweep method):
        // the default mix (~1/3) puts a full TL0 carrier at mean-square
        // 0.0044 — below the level floor — so the checks below need the
        // same unity-gain condition the TL ladder sweep uses.
        tc.chip.WriteFm(0, 1, 0x05, 0x01); // 0x105: NEW (must come first)
        tc.chip.WriteFm(0, 1, 0x04, conn); // 0x104: 4-op connection select
        // Distinct voices per operator so the algorithm choice is audible:
        // ch0 mod (0x20) mult 1, ch0 car (0x23) mult 2 (TL 16), ch3 mod
        // (0x28) mult 4, ch3 car (0x2B) mult 8. The classic pairing joins
        // ch0 with ch3: these four slots form one complete cascade in 4-op
        // mode and two independent channels (ch0, ch3) in 2-op mode.
        // EGT=1 (sustaining): FmOpVoice's DR5 with EGT0 is non-sustaining —
        // it decays ~25 dB across the 400-frame window, which made the
        // level check marginal under the old shallow-mod bug and failing
        // after the modulator-domain fix. A held envelope pins the
        // steady-state level instead of the decay slope.
        FmOpVoice(tc.chip, 0, 0, 0, 0x21);
        FmOpVoice(tc.chip, 0, 0, 3, 0x22);
        tc.chip.WriteFm(0, 0, 0x43, 0x10); // ch0 car TL 16
        FmOpVoice(tc.chip, 0, 0, 8, 0x24);
        FmOpVoice(tc.chip, 0, 0, 11, 0x28);
        tc.chip.WriteFm(0, 0, 0xC0, 0x30); // CHA+CHB: both sides (include)
        tc.chip.WriteFm(0, 0, 0xC3, 0x30);
        // Audible carriers: fnum 0x200 block 4 (~390 Hz; B0 bits 1:0 are
        // the fnum high bits) — whole cycles per window keep the level
        // measure robust.
        tc.chip.WriteFm(0, 0, 0xA0, 0x00);
        tc.chip.WriteFm(0, 0, 0xA3, 0x00);
        tc.chip.WriteFm(0, 0, 0xB0, 0x32); // fnum 0x200, block 4, key on ch0
        tc.chip.WriteFm(0, 0, 0xB3, 0x32); // key on ch3
        return CaptureFrames(tc, 400);
    };
    TestChip a, b;
    const std::vector<float> fa = run4(a, 0x00); // all pairs 2-op
    const std::vector<float> fb = run4(b, 0x3F); // all six pairs 4-op
    CHECK(RmsOf(fa) > 1000.0 * kNormScale);
    CHECK(RmsOf(fb) > 1000.0 * kNormScale);
    CHECK(fa.size() == fb.size() && !std::equal(fa.begin(), fa.end(), fb.begin()));
    float pk = 0.0f;
    for (int ch = 0; ch < 4; ch++)
        pk = std::max(pk, a.chip.ChannelPeak({ChannelGroup::Fm, static_cast<uint8_t>(ch)}));
    CHECK(pk > 0.05f);
#endif // !OPL4_FM_YMFM
}

void VecFmRhythm()
{
    std::printf("VecFmRhythm\n");
#if !defined(OPL4_FM_YMFM) // pins the in-tree rhythm model via
    // per-channel peaks; the ymfm backend zeroes taps
    TestChip tc;
    // NEW first: without 0x105 the bank-1 register file aliases back to
    // bank 0 (ymfm-modelled quirk) and the percussion voices never load.
    tc.chip.WriteFm(0, 1, 0x05, 0x01);
    // Classic datasheet voice set on the register-linear op map: BD = ch6
    // pair (regs 0x30/0x33, normal 2-op FM); HH = ch7 mod (0x31), SD =
    // ch7 car (0x34); TOM = ch8 mod (0x32, single sine op), CY = ch8 car
    // (0x35). Channels 9..11 stay on plain bank-1 duty.
    for (int k = 16; k <= 21; k++) // slots 0x30-0x35
        FmOpVoice(tc.chip, 0, 0, static_cast<uint8_t>(k), 0x01);
    tc.chip.WriteFm(0, 0, 0xA6, 0x21); // ch6 frequency, block 1
    tc.chip.WriteFm(0, 0, 0xB6, 0x04);
    tc.chip.WriteFm(0, 0, 0xA7, 0x21); // ch7 (SD/HH envelope pitch)
    tc.chip.WriteFm(0, 0, 0xB7, 0x04);
    tc.chip.WriteFm(0, 0, 0xA8, 0x21); // ch8: TOM is a sine op, needs pitch
    tc.chip.WriteFm(0, 0, 0xB8, 0x04);
    tc.chip.WriteFm(0, 0, 0xBD, 0x3F); // rhythm + all five key-ons
    tc.chip.Run(400 * kOutClocks);
    CHECK(tc.chip.ChannelPeak({ChannelGroup::Fm, 6}) > 0.02f);  // BD
    CHECK(tc.chip.ChannelPeak({ChannelGroup::Fm, 7}) > 0.02f);  // HH + SD
    CHECK(tc.chip.ChannelPeak({ChannelGroup::Fm, 8}) > 0.02f);  // TOM + CY
    CHECK(tc.chip.ChannelPeak({ChannelGroup::Fm, 9}) < 0.001f); // unkeyed
    CHECK(tc.chip.ChannelPeak({ChannelGroup::Fm, 11}) < 0.001f); // unclaimed
#endif // !OPL4_FM_YMFM
}

void VecFmTimers()
{
    std::printf("VecFmTimers\n");
    {
        // T1 with load 0xFF: (0x100-0xFF)*4 = 4 FM ticks = 2736 master clocks.
        TestChip tc;
        tc.chip.WriteFm(0, 0, 0x02, 0xFF);
        tc.chip.WriteFm(0, 0, 0x04, 0x01);
        CHECK((tc.chip.ReadStatus(2735) & 0x40) == 0);
        CHECK((tc.chip.ReadStatus(2736) & 0x40) != 0);
        CHECK((tc.chip.ReadStatus(4000) & 0x40) != 0); // sticky until reset
        tc.chip.WriteFm(5000, 0, 0x04, 0xC1);        // reset + mask + enable
        CHECK((tc.chip.ReadStatus(5001) & 0x40) == 0);
        tc.chip.Run(20000);
        CHECK((tc.chip.ReadStatus(20000) & 0x40) == 0); // masked: stays clear
    }
    {
        // T2 with load 0xFF: 16 FM ticks = 10944 master clocks.
        TestChip tc;
        tc.chip.WriteFm(0, 0, 0x03, 0xFF);
        tc.chip.WriteFm(0, 0, 0x04, 0x02);
        CHECK((tc.chip.ReadStatus(10943) & 0x20) == 0);
        CHECK((tc.chip.ReadStatus(10944) & 0x20) != 0);
    }
}

void VecFmLfo()
{
    std::printf("VecFmLfo\n");
    {
        // AM on the carrier (reg 0x23 bit 7): the envelope index rides
        // the 13-bit AM triangle, so RMS drops over a full LFO period.
        TestChip am, noam;
        KeyOnFmCh0(am.chip, 0);
        KeyOnFmCh0(noam.chip, 0);
        am.chip.WriteFm(0, 0, 0x23, 0x81); // classic carrier: AM on, mult 1
        const double rAm = RmsOf(CaptureFrames(am, 9000));
        const double rNo = RmsOf(CaptureFrames(noam, 9000));
        CHECK(rNo > 0.0);
        CHECK(rAm < rNo);
    }
    {
        // Vibrato changes the waveform: streams are no longer identical.
        TestChip vib, novib;
        KeyOnFmCh0(vib.chip, 0);
        KeyOnFmCh0(novib.chip, 0);
        vib.chip.WriteFm(0, 0, 0x23, 0x41); // classic carrier: VIB on, mult 1
        const std::vector<float> a = CaptureFrames(vib, 3000);
        const std::vector<float> b = CaptureFrames(novib, 3000);
        CHECK(a.size() == b.size() && !std::equal(a.begin(), a.end(), b.begin()));
    }
}

void VecFmKsl()
{
    std::printf("VecFmKsl\n");
#if !defined(OPL4_FM_YMFM) // pins the in-tree KSL model via per-channel
    // peaks; the ymfm backend zeroes taps
    TestChip ksl, noksl;
    for (TestChip* tc : {&ksl, &noksl})
    {
        tc->chip.WriteFm(0, 0, 0x20, 0x01); // classic ch0 modulator
        tc->chip.WriteFm(0, 0, 0x23, 0x01); // classic ch0 carrier
        tc->chip.WriteFm(0, 0, 0x60, 0xF5);
        tc->chip.WriteFm(0, 0, 0x63, 0xF5);
        tc->chip.WriteFm(0, 0, 0x80, 0x0F);
        tc->chip.WriteFm(0, 0, 0x83, 0x0F);
        tc->chip.WriteFm(0, 0, 0xA0, 0x03);
        tc->chip.WriteFm(0, 0, 0xB0, 0x3F); // fn 0x303, block 7, key on
    }
    ksl.chip.WriteFm(0, 0, 0x43, 0xC0);  // carrier TL 0 + KSL 11 (6 dB/oct)
    noksl.chip.WriteFm(0, 0, 0x43, 0x00); // carrier TL 0, KSL off
    ksl.chip.Run(300 * kOutClocks);
    noksl.chip.Run(300 * kOutClocks);
    const float pOn = ksl.chip.ChannelPeak({ChannelGroup::Fm, 0});
    const float pOff = noksl.chip.ChannelPeak({ChannelGroup::Fm, 0});
    CHECK(pOff > 0.05f);
    CHECK(pOn < pOff); // key-scale attenuation at block 7
#endif // !OPL4_FM_YMFM
}

// ---------------------------------------------------------------------------
// Table integrity (§12.1): the rate machinery tables must be fully
// initialised — a short std::array initializer zero-fills silently, which
// is exactly the rate-63 crawl the envelope vectors would miss without it.
// ---------------------------------------------------------------------------
void VecTableIntegrity()
{
    std::printf("VecTableIntegrity\n");
    for (int r = 56; r < 64; r++)
        CHECK_EQ_I(kEgRateSelect[r], RateRow(12)); // rate-15 band: inc 4
    // No rate 4..63 may alias the all-zero infinity row (rates 0..3 do).
    for (int r = 4; r < 64; r++)
        CHECK(kEgRateSelect[r] != RateRow(14));
    // Every finite row 0..12 has a nonzero period average.
    for (int row = 0; row <= 12; row++)
    {
        int sum = 0;
        for (int i = 0; i < 8; i++)
            sum += kEgInc[RateRow(row) + i];
        CHECK(sum > 0);
    }
    CHECK_EQ_I(kEgRateShift[0], 12);
    CHECK_EQ_I(kEgRateShift[48], 0);
    CHECK_EQ_I(kEgRateShift[63], 0);
}

int RunVectorTests()
{
    VecTableIntegrity();
    VecLoopOverrun();
    VecCalcStepSweep();
    VecWidthGolden();
    VecInterpGolden();
    VecEnvMatrix();
    VecDamp();
    VecPrvb();
    VecAmVib();
    VecPanSweep();
    VecNegativeClip();
    VecSaveRobust();
    VecFm4Op();
    VecFmRhythm();
    VecFmTimers();
    VecFmLfo();
    VecFmKsl();
    return 0;
}

} // namespace opl4test
