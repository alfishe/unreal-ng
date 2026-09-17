// §12.2.1 conformance sweep suite — exhaustive register/field families.
//
// Each family drives one writable register field (or command pattern)
// across its full encoded range and asserts datasheet-derived semantics:
// exact dB steps, monotonicity, boundary behaviour, generation-pattern
// invariants. Assertions are spec-derived, never implementation
// snapshots; bit-exact snapshots are the cosim-oracle's job (tier 2).
//
// Register-map note: both backends use the canonical YMF262 operator map
// (mod = (c%3) + 8*(c/3), car = +3, with the 0x26/0x27 and 0x2E/0x2F gaps)
// and include-semantics routing (C0 bits 4..7 CHA/CHB/CHC/CHD enable a
// side). The in-tree engine adopted the classic map in kStateVersion 3;
// FmOpAddr()/kRoute* carry the shared conventions.
// Backend-internal exactness (operator-internal state, per-channel taps)
// stays guarded `#if !defined(OPL4_FM_YMFM)` per the vector-suite precedent
// (the ymfm adapter zeroes taps and hides operator internals).
#include "testfw.h"

#include <cstring>

namespace opl4test
{

namespace
{

// Routing C0 encodings, classic YMF262 include semantics: bit4 = CHA (L),
// bit5 = CHB (R), bit6 = CHC (L), bit7 = CHD (R); all clear = silent.
constexpr uint8_t kRouteBoth = 0x30;
[[maybe_unused]] constexpr uint8_t kRouteLeft = 0x10;
[[maybe_unused]] constexpr uint8_t kRouteRight = 0x20;
[[maybe_unused]] constexpr uint8_t kRouteNone = 0x00;

// Minimum steady RMS of a TL0 unity-mix carrier. With ymfm scaled 4× to
// match in-tree, both backends produce similar levels (~23170 sine RMS
// int-scale, ~0.177 normalized). The normalized constant reflects both the
// scale change and the kNormScale normalization.
#if defined(OPL4_FM_YMFM)
constexpr double kFmCarrierRmsMin = 4000.0 * 4.0 * kNormScale; // ymfm ×4 + normalize
#else
constexpr double kFmCarrierRmsMin = 16000.0 * kNormScale;       // in-tree + normalize
#endif

// Normalized level of a full-scale 16-bit PCM sample: the chip stream
// carries kRailShift = 2 bits of headroom above the 16-bit DAC rail, and
// the host normalizes by 1/(32768 << 2), so ±FS samples land at ±0.25.
constexpr double kPcmRail = 0.25;
// Normalized output of a 16-bit PCM sample value at unity envelope/TL/pan.
constexpr double PcmNorm(int32_t v) { return v / 32768.0 * kPcmRail; }

// Operator register addresses for channel c (0..8, bank-relative) and
// family base 0x20/0x40/0x60/0x80/0xE0 — the canonical YMF262 layout
// (ch0:(0,3) ch3:(8,11) ch6:(16,19)) with its 0x26/0x27 and 0x2E/0x2F gaps
// (verified: rhythm voices must land on 0x30-0x35).
inline void FmOpAddr(int c, uint8_t base, uint8_t* mod, uint8_t* car)
{
    *mod = static_cast<uint8_t>(base + (c % 3) + 8 * (c / 3));
    *car = static_cast<uint8_t>(*mod + 3);
}

// Advance the core to (base + steps) output frames and drain the
// renderer. Returns interleaved L/R frames (44100 bypass: normalized
// chip-stream floats, kNormScale — full scale ±1.0).
inline std::vector<float> CaptureOutput(TestChip& tc, uint64_t baseSteps, uint64_t steps)
{
    tc.chip.Run((baseSteps + steps) * kOutClocks);
    std::vector<float> out;
    float buf[16384];
    size_t n;
    while ((n = tc.chip.Render(buf, 8192)) > 0)
        out.insert(out.end(), buf, buf + n * 2);
    return out;
}

inline double RmsLeft(const std::vector<float>& v, size_t skipFrames)
{
    double acc = 0.0;
    size_t n = 0;
    for (size_t f = skipFrames; f < v.size() / 2; f++)
    {
        const double x = v[2 * f];
        acc += x * x;
        n++;
    }
    return n ? std::sqrt(acc / n) : 0.0;
}

[[maybe_unused]] inline double RmsRight(const std::vector<float>& v, size_t skipFrames)
{
    double acc = 0.0;
    size_t n = 0;
    for (size_t f = skipFrames; f < v.size() / 2; f++)
    {
        const double x = v[2 * f + 1];
        acc += x * x;
        n++;
    }
    return n ? std::sqrt(acc / n) : 0.0;
}

inline int ZeroCrossLeft(const std::vector<float>& v, size_t skipFrames)
{
    int z = 0;
    for (size_t f = skipFrames + 1; f < v.size() / 2; f++)
        if ((v[2 * f] < 0.0f) != (v[2 * f - 2] < 0.0f))
            z++;
    return z;
}

[[maybe_unused]] inline double MeanLeft(const std::vector<float>& v, size_t skipFrames)
{
    double acc = 0.0;
    size_t n = 0;
    for (size_t f = skipFrames; f < v.size() / 2; f++)
    {
        acc += v[2 * f];
        n++;
    }
    return n ? acc / static_cast<double>(n) : 0.0;
}

inline void MinMaxLeft(const std::vector<float>& v, size_t skipFrames,
                       double* lo, double* hi)
{
    *lo = 0.0;
    *hi = 0.0;
    bool first = true;
    for (size_t f = skipFrames; f < v.size() / 2; f++)
    {
        const double x = v[2 * f];
        if (first || x < *lo)
            *lo = x;
        if (first || x > *hi)
            *hi = x;
        first = false;
    }
}

// RMS per fixed-size window (envelope-shape measurements: attack/decay/
// release timing, plateau flatness).
inline std::vector<double> WindowRms(const std::vector<float>& v, size_t winFrames)
{
    std::vector<double> out;
    const size_t nFrames = v.size() / 2;
    for (size_t start = 0; start + winFrames <= nFrames; start += winFrames)
    {
        double acc = 0.0;
        for (size_t f = start; f < start + winFrames; f++)
        {
            const double x = v[2 * f];
            acc += x * x;
        }
        out.push_back(std::sqrt(acc / static_cast<double>(winFrames)));
    }
    return out;
}

// Upward crossings of an absolute level (unipolar waveform frequency).
inline int LevelUpCrossings(const std::vector<float>& v, size_t skipFrames, double level)
{
    int z = 0;
    bool below = true;
    for (size_t f = skipFrames; f < v.size() / 2; f++)
    {
        const bool nowBelow = v[2 * f] < level;
        if (below && !nowBelow)
            z++;
        below = nowBelow;
    }
    return z;
}

// Near-pure carrier patch on channel `ch` (0..17): modulator TL-maxed
// (phase modulation ~0), carrier TL `tl`, AR15/EGT sustain, routing BOTH,
// NEW armed so OPL3 register semantics apply on both backends.
inline void FmCarrierPatch(Opl4& c, uint64_t t, int ch, int block, int fnum, uint8_t tl)
{
    const int bank = ch / 9;
    const int c9 = ch % 9;
    const auto W = [&](uint8_t reg, uint8_t data) { c.WriteFm(t, bank, reg, data); };
    uint8_t m, r;
    c.WriteFm(t, 1, 0x05, 0x01); // NEW
    FmOpAddr(c9, 0x20, &m, &r);
    W(m, 0x21); // EGT sustaining, mult 1
    W(r, 0x21);
    FmOpAddr(c9, 0x40, &m, &r);
    W(m, 0x3F); // modulator TL max
    W(r, tl);
    FmOpAddr(c9, 0x60, &m, &r);
    W(m, 0xF0); // AR15 DR0
    W(r, 0xF0);
    FmOpAddr(c9, 0x80, &m, &r);
    W(m, 0x00); // SL0 RR0
    W(r, 0x00);
    W(static_cast<uint8_t>(0xA0 + c9), static_cast<uint8_t>(fnum & 0xFF));
    W(static_cast<uint8_t>(0xB0 + c9),
      static_cast<uint8_t>(((fnum >> 8) & 0x03) | (block << 2) | 0x20)); // key on
    W(static_cast<uint8_t>(0xC0 + c9), kRouteBoth);
}

} // namespace

// ---------------------------------------------------------------------------
// FM total level: 6-bit field, 0.75 dB per step (datasheet). Live-writable
// during key-on; measured as steady-state RMS of the left output with the
// FM block mix at unity so the ladder is the only gain in play.
// ---------------------------------------------------------------------------
void FmTlLadderSweep()
{
    std::printf("FmTlLadderSweep\n");
    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00); // FM block mix unity, both sides
    FmCarrierPatch(tc.chip, 0, 0, 4, 582, 0x00);
    (void)CaptureOutput(tc, 4096, 0); // flush attack prefix: windows must be purely post-write

    double rms[64];
    uint64_t t = 4096;
    for (int tl = 0; tl < 64; tl++)
    {
        uint8_t m, r;
        FmOpAddr(0, 0x40, &m, &r);
        tc.chip.WriteFm(t * kOutClocks, 0, r, static_cast<uint8_t>(tl));
        const auto frames = CaptureOutput(tc, t, 1152);
        rms[tl] = RmsLeft(frames, 128);
        t += 1152;
    }

    CHECK(rms[0] > kFmCarrierRmsMin);              // live near rail (unity mix)
    CHECK(rms[63] < rms[0] / 100.0);              // 47.25 dB span silences
    for (int tl = 1; tl < 64; tl++)
    {
        CHECK(rms[tl] <= rms[tl - 1] * 1.002);    // strictly non-increasing
        const double ratio = rms[tl] / rms[tl - 1];
        CHECK(ratio > 0.895 && ratio < 0.940);    // 10^(-0.75/20) = 0.917
    }
    const double span = rms[63] / rms[0];
    const double expect = std::pow(10.0, -63.0 * 0.75 / 20.0);
    CHECK(span > expect * 0.9 && span < expect * 1.1); // cumulative ladder
}

// ---------------------------------------------------------------------------
// FM multiplier: MULT 0..15 mapped through the YMF262 datasheet table
// (0 = x0.5; xN otherwise EXCEPT 11 = x10, 13 = x12, 14 = x15 — the
// non-linear top of the table, a real bug the linear model had). Frequency
// proportional via zero-crossing rate of the left output; fnum 582/block 4
// = A440 at x1.
// ---------------------------------------------------------------------------
void FmMultSweep()
{
    std::printf("FmMultSweep\n");
    // Independent restatement of the datasheet multiplier table.
    constexpr int kMultX[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};
    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00);
    FmCarrierPatch(tc.chip, 0, 0, 4, 582, 0x00);
    (void)CaptureOutput(tc, 4096, 0); // flush attack prefix

    int zc[16];
    uint64_t t = 4096;
    for (int mult = 0; mult < 16; mult++)
    {
        uint8_t m, r;
        FmOpAddr(0, 0x20, &m, &r);
        tc.chip.WriteFm(t * kOutClocks, 0, r,
                        static_cast<uint8_t>(0x20 | mult)); // keep EGT
        const auto frames = CaptureOutput(tc, t, 8192);
        zc[mult] = ZeroCrossLeft(frames, 256);
        t += 8192;
    }
    std::printf("  zc = %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
                zc[0], zc[1], zc[2], zc[3], zc[4], zc[5], zc[6], zc[7],
                zc[8], zc[9], zc[10], zc[11], zc[12], zc[13], zc[14], zc[15]);

    CHECK(zc[1] > 120 && zc[1] < 220); // ~163 at 440 Hz over 8192 frames
    CHECK(std::abs(zc[0] * 2 - zc[1]) <= 8);       // x0.5
    for (int mult = 2; mult < 16; mult++)
    {
        // table-proportional within decimation jitter (2% or 8 crossings)
        const int tol = std::max(8, zc[1] * kMultX[mult] / 50);
        CHECK(std::abs(zc[mult] - kMultX[mult] * zc[1]) <= tol);
    }
}

// ---------------------------------------------------------------------------
// FM key scale level: attenuation that grows with block (and fnum bit 6).
// Map-agnostic invariants on both backends; the folded table (ymfm
// consumption: table numerals in 0.09375 dB index units, shifted by the
// swapped 2-bit slope) gives 3 dB per block above the block-1 knee at the
// reg-01 slope and 0.75 dB for fnum bit 6.
// ---------------------------------------------------------------------------
void FmKslSweep()
{
    std::printf("FmKslSweep\n");
    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00);
    FmCarrierPatch(tc.chip, 0, 0, 4, 512, 0x00);

    uint64_t t = 4096;
    (void)CaptureOutput(tc, t, 0); // flush attack prefix
    // Long-window variant for block 0: fnum 576/block 0 is ~27 Hz, so a
    // 1024-frame window covers under one cycle and RMS is meaningless.
    const auto measure = [&](int block, int fnum, uint8_t kslBits, uint64_t win = 1152) {
        tc.chip.WriteFm(t * kOutClocks, 0, static_cast<uint8_t>(0xA0),
                        static_cast<uint8_t>(fnum & 0xFF));
        tc.chip.WriteFm(t * kOutClocks, 0, static_cast<uint8_t>(0xB0),
                        static_cast<uint8_t>(((fnum >> 8) & 0x03) | (block << 2) | 0x20));
        uint8_t m, r;
        FmOpAddr(0, 0x40, &m, &r);
        tc.chip.WriteFm(t * kOutClocks, 0, r, kslBits); // TL 0 + KSL bits
        const auto frames = CaptureOutput(tc, t, win);
        t += win;
        return RmsLeft(frames, 128); // skip settle, own capture window
    };

    const double base = measure(4, 512, 0x00);
    CHECK(base > kFmCarrierRmsMin);
    const double off7 = measure(7, 512, 0x00); // KSL off: block pitch change only
    CHECK(off7 > base * 0.9 && off7 < base * 1.1);
    const double on7 = measure(7, 512, 0x40);  // KSL on, top block
    CHECK(on7 < off7 * 0.85);                  // clear attenuation

    // Exact folded table (both backends, ymfm consumption semantics): at
    // fnum 512 with reg bits 01 (x4 slope) the attenuation is
    // (48 - 8*(7-block)) << 2 index units = 3*(block-1) dB — zero at block 1,
    // 18 dB at block 7 (3 dB/oct above the block-1 knee).
    for (int block = 1; block <= 7; block++)
    {
        const double on = measure(block, 512, 0x40);
        const double expect = std::pow(10.0, -3.0 * (block - 1) / 20.0);
        CHECK(on > base * expect * 0.92 && on < base * expect * 1.08);
    }
    // fnum bit 6 steps the folded table by 2 units (50 - 48) = 0.75 dB at
    // the x4 slope.
    const double bit6 = measure(3, 576, 0x40);
    const double bit0 = measure(3, 512, 0x40);
    const double expect = std::pow(10.0, -0.75 / 20.0);
    CHECK(bit6 > bit0 * expect * 0.92 && bit6 < bit0 * expect * 1.08);
    // block 0 corner: both fnum 512 and 576 fold below zero (48/50 - 56)
    // and clamp — KSL is fully off at the lowest octave, identical levels.
    // Long window: block-0 tones are ~27-48 Hz.
    const double b0bit6 = measure(0, 576, 0x40, 24576);
    const double b0base = measure(0, 512, 0x40, 24576);
    CHECK(b0bit6 > b0base * 0.92 && b0bit6 < b0base * 1.08);
}

// ---------------------------------------------------------------------------
// FM envelope grid: AR/DR/SL/RR/EGT semantics measured from the output.
// Attack time monotone in AR (rate 0 frozen, 15 near-instant), EGT holds
// at SL, non-EGT decays past it toward silence, release monotone in RR
// with RR 0 frozen. Absolute levels are referenced to the AR-15 plateau
// so windows are comparable across rates.
// ---------------------------------------------------------------------------
struct FmEnvSpec
{
    uint8_t ar = 15, dr = 0, sl = 0, rr = 15;
    bool egt = true;
    int block = 4, fnum = 582;
};

// Patch the ch0 carrier (modulator TL-maxed) with the envelope spec; the
// kon edge is `keyOn` (a kon rewrite after koff retriggers the envelope).
// `t` is an output-frame index — bus timestamps are master clocks.
inline void FmEnvVoice(Opl4& c, uint64_t t, const FmEnvSpec& s, bool keyOn)
{
    const uint64_t busT = t * kOutClocks;
    uint8_t m, r;
    c.WriteFm(busT, 1, 0x05, 0x01); // NEW
    FmOpAddr(0, 0x20, &m, &r);
    c.WriteFm(busT, 0, m, 0x21); // modulator: EGT, mult 1 (silent anyway)
    c.WriteFm(busT, 0, r, static_cast<uint8_t>(0x01 | (s.egt ? 0x20 : 0)));
    FmOpAddr(0, 0x40, &m, &r);
    c.WriteFm(busT, 0, m, 0x3F);
    c.WriteFm(busT, 0, r, 0x00); // carrier TL 0
    FmOpAddr(0, 0x60, &m, &r);
    c.WriteFm(busT, 0, m, static_cast<uint8_t>((s.ar << 4) | s.dr));
    c.WriteFm(busT, 0, r, static_cast<uint8_t>((s.ar << 4) | s.dr));
    FmOpAddr(0, 0x80, &m, &r);
    c.WriteFm(busT, 0, m, static_cast<uint8_t>((s.sl << 4) | s.rr));
    c.WriteFm(busT, 0, r, static_cast<uint8_t>((s.sl << 4) | s.rr));
    c.WriteFm(busT, 0, 0xA0, static_cast<uint8_t>(s.fnum & 0xFF));
    c.WriteFm(busT, 0, 0xB0, static_cast<uint8_t>(((s.fnum >> 8) & 0x03)
                                                   | (s.block << 2)
                                                   | (keyOn ? 0x20 : 0x00)));
    c.WriteFm(busT, 0, 0xC0, kRouteBoth);
}

void FmEnvStageSweep()
{
    std::printf("FmEnvStageSweep\n");
    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00);

    // Full-level reference run (AR 15) first: t50 thresholds must be
    // absolute or slow-AR partial plateaus break monotonicity.
    double peakRef = 0.0;
    double t50ref = 1e9;
    uint64_t t = 0;
    {
        FmEnvSpec s; // ar 15, EGT, SL 0, RR 15
        FmEnvVoice(tc.chip, t, s, true);
        const auto frames = CaptureOutput(tc, t, 4096);
        const auto wins = WindowRms(frames, 64);
        size_t nw = std::min<size_t>(wins.size(), 4);
        for (size_t i = wins.size() - nw; i < wins.size(); i++)
            peakRef += wins[i];
        peakRef /= static_cast<double>(nw);
        for (size_t i = 0; i < wins.size(); i++)
            if (wins[i] >= 0.5 * peakRef)
            {
                t50ref = (i + 0.5) * 64.0;
                break;
            }
        FmEnvVoice(tc.chip, t + 4096, s, false);
        (void)CaptureOutput(tc, t + 4096, 1024);
        t += 4096 + 1024;
    }

    double t50[16];
    t50[15] = t50ref;
    for (int ar = 0; ar < 15; ar++)
    {
        // Shift-ladder cadence halves per AR step: give the slow tail long
        // captures; unreached half-level stays 1e9.
        const uint64_t hold = (ar <= 2) ? 8192 : (ar <= 5) ? 32768 : 4096;
        FmEnvSpec s;
        s.ar = static_cast<uint8_t>(ar);
        FmEnvVoice(tc.chip, t, s, true);
        const auto frames = CaptureOutput(tc, t, hold);
        const auto wins = WindowRms(frames, 64);
        double tHit = 1e9;
        for (size_t i = 0; i < wins.size(); i++)
            if (wins[i] >= 0.5 * peakRef)
            {
                tHit = (i + 0.5) * 64.0;
                break;
            }
        t50[ar] = tHit;
        FmEnvVoice(tc.chip, t + hold, s, false);
        (void)CaptureOutput(tc, t + hold, 1024);
        t += hold + 1024;
    }
    std::printf("  t50 = %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f (ref %.4f)\n",
                t50[0], t50[1], t50[2], t50[3], t50[4], t50[5], t50[6], t50[7],
                t50[8], t50[9], t50[10], t50[11], t50[12], t50[13], t50[14], t50[15],
                peakRef);

    CHECK(peakRef > kFmCarrierRmsMin);
    CHECK(t50[0] > 1e8);                     // AR 0: rate 0 frozen silent
    CHECK(t50[15] <= 128.0);                 // AR 15: near-instant
    for (int ar = 1; ar < 15; ar++)
        if (t50[ar] < 1e8 && t50[ar + 1] < 1e8) // skip unreached sentinels
            CHECK(t50[ar] >= t50[ar + 1] - 96.0); // monotone (1.5-window tol)

    // Sustain level grid (EGT, DR 12 reaches any SL quickly): plateau must
    // be flat and fall with SL; SL 0 sustains at the attack peak.
    double plat[16];
    for (int sl = 0; sl < 16; sl++)
    {
        FmEnvSpec s;
        s.ar = 15;
        s.dr = 12;
        s.sl = static_cast<uint8_t>(sl);
        FmEnvVoice(tc.chip, t, s, true);
        const auto frames = CaptureOutput(tc, t, 4096);
        const auto wins = WindowRms(frames, 128);
        plat[sl] = 0.0;
        for (size_t i = wins.size() - 8; i < wins.size(); i++)
            plat[sl] += wins[i];
        plat[sl] /= 8.0;
        double prev = 0.0;
        for (size_t i = wins.size() - 16; i < wins.size() - 8; i++)
            prev += wins[i];
        prev /= 8.0;
        CHECK(std::abs(plat[sl] - prev) < 0.1 * plat[sl] + 1.0); // flat hold
        FmEnvVoice(tc.chip, t + 4096, s, false);
        (void)CaptureOutput(tc, t + 4096, 1024);
        t += 4096 + 1024;
    }
    std::printf("  plat = %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                plat[0], plat[2], plat[4], plat[6], plat[8], plat[10], plat[12], plat[15]);
    CHECK(plat[0] > 0.8 * peakRef);              // SL 0: full sustain
    for (int sl = 1; sl < 16; sl++)
        CHECK(plat[sl] <= plat[sl - 1] * 1.03);   // ladder non-increasing
    CHECK(plat[8] < 0.45 * plat[0]);             // clearly down the ladder
    CHECK(plat[15] < plat[8]);                   // and keeps falling

    // Non-sustaining (EGT 0): no hold at SL — decays past it to silence.
    {
        FmEnvSpec s;
        s.egt = false;
        s.ar = 15;
        s.dr = 12; // rate 48 (1/tick): SL 8 by ~150 frames, silence by
                   // ~750 — and the patch is otherwise IDENTICAL to the
                   // plat[8] row, so only EGT differs in the comparison
        s.sl = 8;
        FmEnvVoice(tc.chip, t, s, true);
        const auto frames = CaptureOutput(tc, t, 8192);
        const auto wins = WindowRms(frames, 128);
        // Attack sanity: AR 15 must reach near-peak inside the first three
        // windows (before the DR 12 decay pulls the level down).
        double early = 0.0;
        for (size_t i = 0; i < 3 && i < wins.size(); i++)
            early = std::max(early, wins[i]);
        double finalR = 0.0;
        for (size_t i = wins.size() - 4; i < wins.size(); i++)
            finalR += wins[i];
        finalR /= 4.0;
        CHECK(early > 0.4 * peakRef);
        CHECK(finalR < 0.04 * peakRef);   // past SL to silence
        CHECK(finalR < 0.10 * plat[8]);   // no hold at the sustain level
        t += 8192;
    }

    // Release ladder from full sustain: RR 0 frozen, monotone, RR 15 fast.
    const int rrGrid[] = {0, 8, 11, 13, 15};
    double tSil[5];
    for (int gi = 0; gi < 5; gi++)
    {
        FmEnvSpec s;
        s.ar = 15;
        s.rr = static_cast<uint8_t>(rrGrid[gi]);
        FmEnvVoice(tc.chip, t, s, true);
        const auto holdF = CaptureOutput(tc, t, 1536);
        const auto holdW = WindowRms(holdF, 64);
        CHECK_EQ_I(holdW.size(), 1536u / 64u); // full capture (no Run regress)
        const double level0 = holdW.empty() ? 0.0 : holdW.back();
        FmEnvVoice(tc.chip, t + 1536, s, false);
        const auto relF = CaptureOutput(tc, t + 1536, 8192);
        const auto relW = WindowRms(relF, 128);
        double ts = 1e9;
        for (size_t i = 0; i < relW.size(); i++)
            if (relW[i] < 0.03 * level0)
            {
                ts = (i + 0.5) * 128.0;
                break;
            }
        tSil[gi] = ts;
        t += 1536 + 8192;
    }
    std::printf("  tSil = %.0f %.0f %.0f %.0f %.0f\n",
                tSil[0], tSil[1], tSil[2], tSil[3], tSil[4]);
    CHECK(tSil[0] > 1e8);                    // RR 0: frozen at sustain
    CHECK(tSil[4] <= 704.0);                 // RR 15: fast silence
    for (int gi = 1; gi < 4; gi++)
        CHECK(tSil[gi] >= tSil[gi + 1] - 160.0); // monotone (window tol)
}

// ---------------------------------------------------------------------------
// FM feedback: FB 0..7 (C0 bits 3:1) — self-modulation depth of the
// modulator grows ~2x per step; spectral richness and level rise.
// ---------------------------------------------------------------------------
void FmFeedbackSweep()
{
    std::printf("FmFeedbackSweep\n");
    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00);
    // Moderate modulator level so the feedback growth stays in headroom.
    uint8_t m, r;
    tc.chip.WriteFm(0, 1, 0x05, 0x01);
    FmOpAddr(0, 0x20, &m, &r);
    tc.chip.WriteFm(0, 0, m, 0x21);
    tc.chip.WriteFm(0, 0, r, 0x21);
    FmOpAddr(0, 0x40, &m, &r);
    tc.chip.WriteFm(0, 0, m, 0x10); // mod TL 16 (-24 dB)
    tc.chip.WriteFm(0, 0, r, 0x00);
    FmOpAddr(0, 0x60, &m, &r);
    tc.chip.WriteFm(0, 0, m, 0xF0);
    tc.chip.WriteFm(0, 0, r, 0xF0);
    FmOpAddr(0, 0x80, &m, &r);
    tc.chip.WriteFm(0, 0, m, 0x0F);
    tc.chip.WriteFm(0, 0, r, 0x0F);
    tc.chip.WriteFm(0, 0, 0xA0, 0x46);
    tc.chip.WriteFm(0, 0, 0xB0, 0x32); // fnum 582, block 4, key on
    (void)CaptureOutput(tc, 4096, 0);  // flush attack prefix

    double rms[8];
    int zc[8];
    uint64_t t = 4096;
    for (int fb = 0; fb < 8; fb++)
    {
        tc.chip.WriteFm(t * kOutClocks, 0, 0xC0,
                        static_cast<uint8_t>(kRouteBoth | (fb << 1)));
        const auto frames = CaptureOutput(tc, t, 8192);
        rms[fb] = RmsLeft(frames, 256);
        zc[fb] = ZeroCrossLeft(frames, 256);
        t += 8192;
    }
    std::printf("  rms = %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                rms[0], rms[1], rms[2], rms[3], rms[4], rms[5], rms[6], rms[7]);
    std::printf("  zc  = %d %d %d %d %d %d %d %d\n",
                zc[0], zc[1], zc[2], zc[3], zc[4], zc[5], zc[6], zc[7]);
    CHECK(rms[0] > kFmCarrierRmsMin / 4.0);
    // Ladder shape: feedback taps the post-TL modulator output whose 2-op
    // path is already 2x the fb7 depth, so fb<=6 barely moves the spectrum
    // while fb7 (±1/4-cycle self-modulation) dominates it — the density
    // explodes at the top of the ladder. Level stays in-band throughout
    // (phase modulation moves energy between harmonics; the hold-drop
    // reducer discards some of the chaotic high end — no monotone
    // level-growth claim at these settings).
    CHECK(zc[7] > 4.0 * zc[0]);              // fb7: dense spectrum
    CHECK(zc[6] > 0.7 * zc[0] && zc[6] < 1.5 * zc[0]); // ladder top-heavy
    for (int fb = 1; fb < 8; fb++)
    {
        CHECK(rms[fb] > 0.55 * rms[0]);
        CHECK(rms[fb] < 1.25 * rms[0]);
    }
}

// ---------------------------------------------------------------------------
// FM 4-op connection select: 0x104 (bank-1 0x04) pairs (0,3)/(1,4)/(2,5)
// into one 4-operator voice. The pair's two C0 CON bits (master bit 0,
// slave bit 0) select the algorithm. Probed with a one-hot total level —
// only op1 at TL 0, op2/op3/op4 at TL max: algorithms that route op1 into
// the final sum ring loud, cascade-only algorithms stay near-silent. Both
// register maps agree on the loud/quiet signature (ymfm algorithms 8-11
// vs the in-tree two-algorithm model with its always-additive op3), so
// the probe is map-agnostic.
// ---------------------------------------------------------------------------
void Fm4OpConnections()
{
    std::printf("Fm4OpConnections\n");
    const auto patchOp = [](Opl4& c, int c9, uint8_t base, bool car, uint8_t data) {
        uint8_t m, r;
        FmOpAddr(c9, base, &m, &r);
        c.WriteFm(0, 0, car ? r : m, data);
    };

    double rmsA[4];
    for (int combo = 0; combo < 4; combo++)
    {
        const int masterBit = combo & 1; // ch0 C0 bit 0
        const int slaveBit = combo >> 1; // ch3 C0 bit 0
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, 0x00);
        tc.chip.WriteFm(0, 1, 0x05, 0x01); // NEW
        tc.chip.WriteFm(0, 1, 0x04, 0x01); // 0x104 bit 0: pair ch0 + ch3
        for (int i = 0; i < 4; i++) // op1..op4 = ch0 mod/car, ch3 mod/car
        {
            const int c9 = (i < 2) ? 0 : 3;
            const bool car = (i & 1) != 0;
            patchOp(tc.chip, c9, 0x20, car, 0x21); // EGT, mult 1
            patchOp(tc.chip, c9, 0x40, car, i == 0 ? 0x00 : 0x3F);
            patchOp(tc.chip, c9, 0x60, car, 0xF0); // AR 15, DR 0
            patchOp(tc.chip, c9, 0x80, car, 0x0F); // SL 0, RR 15
        }
        tc.chip.WriteFm(0, 0, 0xA0, 0x46);
        tc.chip.WriteFm(0, 0, 0xB0, 0x32); // ch0: fnum 582, block 4, kon
        tc.chip.WriteFm(0, 0, 0xA3, 0x46);
        tc.chip.WriteFm(0, 0, 0xB3, 0x32); // slave kon too (4-op voice)
        tc.chip.WriteFm(0, 0, 0xC0, static_cast<uint8_t>(kRouteBoth | masterBit));
        tc.chip.WriteFm(0, 0, 0xC3, static_cast<uint8_t>(kRouteBoth | slaveBit));
        const auto frames = CaptureOutput(tc, 0, 8192);
        rmsA[combo] = RmsLeft(frames, 1024); // skip attack/settle
    }
    std::printf("  patternA (m0s0 m0s1 m1s0 m1s1) = %.4f %.4f %.4f %.4f\n",
                rmsA[0], rmsA[1], rmsA[2], rmsA[3]);
    // ymfm algorithms 8-11 (YMF262 silicon, both backends): the master C0
    // CON bit routes the TL-0 op1 into the final sum (algs 9/11 add O1);
    // the quiet rows are past TL-max cascade operators.
    CHECK(rmsA[1] > kFmCarrierRmsMin / 2.0); // alg 9: O1 + (O2->O3->O4)
    CHECK(rmsA[3] > kFmCarrierRmsMin / 2.0); // alg 11: O1 + (O2->O3) + O4
    CHECK(rmsA[0] < kFmCarrierRmsMin / 50.0); // alg 8 serial
    CHECK(rmsA[2] < kFmCarrierRmsMin / 50.0); // alg 10 parallel pairs
    CHECK(rmsA[1] > 6.0 * rmsA[0]);

    // Distinctness from 2-op mode: same patch with all TL 0 and an
    // octave-lower slave pitch; enabling 0x104 must change the render (the
    // 4-op cascade replaces two independent 2-op voices).
    const auto runB = [&](uint8_t fourop, int* zc, double* rms) {
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, 0x00);
        tc.chip.WriteFm(0, 1, 0x05, 0x01);
        tc.chip.WriteFm(0, 1, 0x04, fourop);
        for (int i = 0; i < 4; i++)
        {
            const int c9 = (i < 2) ? 0 : 3;
            const bool car = (i & 1) != 0;
            patchOp(tc.chip, c9, 0x20, car, 0x21);
            patchOp(tc.chip, c9, 0x40, car, 0x00);
            patchOp(tc.chip, c9, 0x60, car, 0xF0);
            patchOp(tc.chip, c9, 0x80, car, 0x0F);
        }
        tc.chip.WriteFm(0, 0, 0xA0, 0x46);
        tc.chip.WriteFm(0, 0, 0xB0, 0x32);
        tc.chip.WriteFm(0, 0, 0xA3, 0x23); // fnum 291, one octave down
        tc.chip.WriteFm(0, 0, 0xB3, 0x31);
        tc.chip.WriteFm(0, 0, 0xC0, kRouteBoth);
        tc.chip.WriteFm(0, 0, 0xC3, kRouteBoth);
        const auto frames = CaptureOutput(tc, 0, 8192);
        *zc = ZeroCrossLeft(frames, 1024);
        *rms = RmsLeft(frames, 1024);
    };
    int zcOff = 0, zcOn = 0;
    double rmsOff = 0.0, rmsOn = 0.0;
    runB(0x00, &zcOff, &rmsOff);
    runB(0x01, &zcOn, &rmsOn);
    std::printf("  patternB zc off/on = %d/%d rms off/on = %.4f/%.4f\n",
                zcOff, zcOn, rmsOff, rmsOn);
    CHECK(zcOff > 100 && rmsOff > kFmCarrierRmsMin); // two 2-op voices
    CHECK(std::abs(zcOn - zcOff) > std::max(16, zcOff / 12)
          || std::abs(rmsOn / rmsOff - 1.0) > 0.05);
}

// ---------------------------------------------------------------------------
// FM rhythm mode (0xBD): five percussion voices keyed by their own bit —
// BD 0x10 (ch6 FM pair), SD 0x08 (ch7 carrier), TOM 0x04 (ch8 modulator,
// plain sine), HH 0x01 (ch7 modulator), CY 0x02 (ch8 carrier). Per voice:
// keying via 0xBD must be audible on its channel; clearing the bit
// releases (RR 15). In-tree-only: while rhythm is enabled, B0 key-ons on
// ch7 are claimed by the percussion map and stay silent.
// ---------------------------------------------------------------------------
void FmRhythmSweep()
{
    std::printf("FmRhythmSweep\n");
    struct Voice
    {
        const char* name;
        uint8_t bit;
        int ch;
    };
    const Voice voices[] = {
        {"BD", 0x10, 6}, {"SD", 0x08, 7}, {"TOM", 0x04, 8}, {"HH", 0x01, 7}, {"CY", 0x02, 8},
    };
    for (const Voice& v : voices)
    {
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, 0x00);
        tc.chip.WriteFm(0, 1, 0x05, 0x01); // NEW
        const auto W = [&](uint8_t reg, uint8_t data) { tc.chip.WriteFm(0, 0, reg, data); };
        uint8_t m, r;
        FmOpAddr(v.ch, 0x20, &m, &r);
        W(m, 0x21);
        W(r, 0x21); // EGT, mult 1
        FmOpAddr(v.ch, 0x40, &m, &r);
        W(m, v.ch == 6 ? 0x10 : 0x00); // BD keeps FM headroom in the mod
        W(r, 0x00);
        FmOpAddr(v.ch, 0x60, &m, &r);
        W(m, 0xF0);
        W(r, 0xF0);
        FmOpAddr(v.ch, 0x80, &m, &r);
        W(m, 0x0F);
        W(r, 0x0F);
        W(static_cast<uint8_t>(0xA0 + v.ch), 0x46); // fnum 582, block 4,
        W(static_cast<uint8_t>(0xB0 + v.ch), 0x12); // key off: 0xBD owns it
        W(static_cast<uint8_t>(0xC0 + v.ch), kRouteBoth);
        (void)CaptureOutput(tc, 4096, 0); // flush settle
        tc.chip.WriteFm(4096 * kOutClocks, 0, 0xBD, static_cast<uint8_t>(0x20 | v.bit));
        const auto on = CaptureOutput(tc, 4096, 2048);
        const double rOn = RmsLeft(on, 64);
        tc.chip.WriteFm(6144 * kOutClocks, 0, 0xBD, 0x20); // voice bit clear
        const auto off = CaptureOutput(tc, 6144, 3072);
        const double rOff = RmsLeft(off, 1024); // tail past the RR 15 slope
        std::printf("  %-3s on %8.4f off %8.4f\n", v.name, rOn, rOff);
        CHECK(rOn > kFmCarrierRmsMin / 6.0);
        CHECK(rOff < rOn / 6.0);
    }

#if !defined(OPL4_FM_YMFM)
    // Suppression: rhythm on, ch7 patched loud, B0 kon must be ignored —
    // the voice only sounds once its 0xBD bit (HH, bit 0) keys it. Real
    // divergence (measured 2026-09-15): ymfm keys rhythm-mode channels from
    // B0 kon too (suppressed 0.576 = keyed 0.580 RMS), the in-tree classic
    // engine implements the YMF262 datasheet suppression.
    {
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, 0x00);
        tc.chip.WriteFm(0, 1, 0x05, 0x01);
        tc.chip.WriteFm(0, 0, 0xBD, 0x20); // rhythm enabled, nothing keyed
        uint8_t m, r;
        FmOpAddr(7, 0x20, &m, &r);
        tc.chip.WriteFm(0, 0, m, 0x21);
        tc.chip.WriteFm(0, 0, r, 0x21);
        FmOpAddr(7, 0x40, &m, &r);
        tc.chip.WriteFm(0, 0, m, 0x00);
        tc.chip.WriteFm(0, 0, r, 0x00);
        FmOpAddr(7, 0x60, &m, &r);
        tc.chip.WriteFm(0, 0, m, 0xF0);
        tc.chip.WriteFm(0, 0, r, 0xF0);
        FmOpAddr(7, 0x80, &m, &r);
        tc.chip.WriteFm(0, 0, m, 0x0F);
        tc.chip.WriteFm(0, 0, r, 0x0F);
        tc.chip.WriteFm(0, 0, 0xA7, 0x46);
        tc.chip.WriteFm(0, 0, 0xB7, 0x32); // kon write: must be suppressed
        tc.chip.WriteFm(0, 0, 0xC7, kRouteBoth);
        const auto sup = CaptureOutput(tc, 0, 4096);
        const double rSup = RmsLeft(sup, 128);
        tc.chip.WriteFm(4096 * kOutClocks, 0, 0xBD, 0x21); // HH bit 0
        const auto key = CaptureOutput(tc, 4096, 2048);
        const double rKey = RmsLeft(key, 64);
        std::printf("  suppressed %8.4f keyed %8.4f\n", rSup, rKey);
        CHECK(rSup < kFmCarrierRmsMin / 50.0);
        CHECK(rKey > kFmCarrierRmsMin / 6.0);
    }
#endif
}

// ---------------------------------------------------------------------------
// FM waveform select (0xE0 + operator, ws 0..7, OPL3 extension): shape
// invariants measured on a pure carrier — DC content, polarity, symmetry,
// zero-crossing density relative to the sine. ws 0/1/2 and 4/6/7 agree
// across both register maps; ws 3 and ws 5 genuinely differ between the
// in-tree square-derived set and ymfm's table (documented, per-backend
// checks).
// ---------------------------------------------------------------------------
void FmWaveformSweep()
{
    std::printf("FmWaveformSweep\n");
    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00);
    FmCarrierPatch(tc.chip, 0, 0, 4, 582, 0x00);
    (void)CaptureOutput(tc, 4096, 0); // flush attack prefix

    struct Stat
    {
        double lo = 0.0, hi = 0.0, mean = 0.0, rms = 0.0;
        int zc = 0, up = 0;
    };
    Stat st[8];
    uint8_t m, r;
    FmOpAddr(0, 0xE0, &m, &r);
    uint64_t t = 4096;
    for (int ws = 0; ws < 8; ws++)
    {
        tc.chip.WriteFm(t * kOutClocks, 0, r, static_cast<uint8_t>(ws));
        const auto frames = CaptureOutput(tc, t, 8192);
        MinMaxLeft(frames, 256, &st[ws].lo, &st[ws].hi);
        st[ws].mean = MeanLeft(frames, 256);
        st[ws].rms = RmsLeft(frames, 256);
        st[ws].zc = ZeroCrossLeft(frames, 256);
        st[ws].up = LevelUpCrossings(frames, 256, 0.3 * st[ws].hi);
        t += 8192;
    }
    for (int ws = 0; ws < 8; ws++)
        std::printf("  ws%d: lo %8.4f hi %8.4f mean %8.4f rms %8.4f zc %d up %d\n",
                    ws, st[ws].lo, st[ws].hi, st[ws].mean, st[ws].rms, st[ws].zc, st[ws].up);

    const auto amp = [&st](int w) { return std::max(st[w].hi, -st[w].lo); };
    // ws 0: symmetric bipolar sine.
    CHECK(st[0].rms > kFmCarrierRmsMin);
    CHECK(std::abs(st[0].mean) < 0.06 * st[0].hi);
    CHECK(st[0].lo < -0.85 * st[0].hi && st[0].lo > -1.15 * st[0].hi);
    CHECK(st[0].zc > 120 && st[0].zc < 220);
    // ws 1: positive half sine + zero half — unipolar, DC = 1/pi of peak.
    CHECK(st[1].lo > -0.03 * st[1].hi);
    CHECK(st[1].mean > 0.20 * st[1].hi && st[1].mean < 0.42 * st[1].hi);
    CHECK(st[1].up > 0.35 * st[0].zc && st[1].up < 0.65 * st[0].zc); // 1/period
    // ws 2: full-wave rectified — unipolar, DC = 2/pi.
    CHECK(st[2].lo > -0.03 * st[2].hi);
    CHECK(st[2].mean > 0.52 * st[2].hi && st[2].mean < 0.74 * st[2].hi);
    CHECK(st[2].mean / st[1].mean > 1.7 && st[2].mean / st[1].mean < 2.3);
    // ws 4 and 6: bipolar derived shapes (zc density is shape-specific:
    // checked per backend below).
    CHECK(st[4].lo < -0.05 * st[4].hi);
    CHECK(st[4].rms > kFmCarrierRmsMin / 3.0);
    CHECK(st[6].rms > kFmCarrierRmsMin / 3.0);
    // ws 7: bipolar in both directions; every waveform keeps near-peak RMS.
    CHECK(st[7].hi > 0.05 * amp(7) && st[7].lo < -0.05 * amp(7));
    for (int ws = 1; ws < 8; ws++)
        CHECK(st[ws].rms > kFmCarrierRmsMin / 4.0);
    // Canonical YMF262 shapes (both backends, ymfm_opl.cpp table):
    // ws 3 = |sin| on quarters 1/3, zero on 2/4 — two positive humps,
    // half of ws 2's DC; ws 4 = one full 2x-frequency sine cycle packed
    // into the first half, zero second half (bipolar, half the sine's
    // power, 2 sign flips/period); ws 5 = two positive |sin| humps in
    // the first half, zero second half (ws 1's DC, half duty); ws 6 =
    // +-full-scale 50% square (sign bit only: rms equals amplitude);
    // ws 7 = exponential-decoded pulse pair — narrow bipolar pulses at
    // ~0.29 of the sine's rms.
    CHECK(st[3].lo > -0.03 * st[3].hi);
    CHECK(st[3].mean > 0.20 * st[3].hi && st[3].mean < 0.42 * st[3].hi);
    CHECK(st[3].mean / st[2].mean > 0.4 && st[3].mean / st[2].mean < 0.6);
    CHECK(st[3].up > 1.6 * st[1].up && st[3].up < 2.4 * st[1].up);
    CHECK(st[3].rms / st[0].rms > 0.6 && st[3].rms / st[0].rms < 0.85);
    CHECK(st[4].zc > 0.8 * st[0].zc && st[4].zc < 1.4 * st[0].zc);
    CHECK(std::abs(st[4].mean) < 0.06 * st[4].hi);
    CHECK(st[4].up > 0.8 * st[1].up && st[4].up < 1.4 * st[1].up);
    CHECK(st[4].rms / st[0].rms > 0.6 && st[4].rms / st[0].rms < 0.85);
    CHECK(st[5].lo > -0.03 * st[5].hi);
    CHECK(st[5].mean / st[1].mean > 0.8 && st[5].mean / st[1].mean < 1.2);
    CHECK(st[5].up > 1.6 * st[1].up && st[5].up < 2.4 * st[1].up);
    CHECK(st[5].zc < st[0].zc / 4);      // unipolar: nearly no sign flips
    CHECK(st[6].zc > 0.8 * st[0].zc && st[6].zc < 1.4 * st[0].zc);
    CHECK(st[6].rms / st[6].hi > 0.9 && st[6].rms / st[6].hi < 1.1); // square
    CHECK(st[6].lo < -0.9 * st[6].hi && st[6].lo > -1.1 * st[6].hi);
    CHECK(std::abs(st[6].mean) < 0.06 * st[6].hi);
    CHECK(st[7].zc > 0.8 * st[0].zc && st[7].zc < 1.4 * st[0].zc);
    CHECK(st[7].rms / st[0].rms > 0.2 && st[7].rms / st[0].rms < 0.4);
}

// ---------------------------------------------------------------------------
// FM AM/VIB matrix: per-operator AM (0x20 bit 7) and VIB (bit 6) enables
// against the 0xBD depth bits (bit 7 = AM 4.8/1.2 dB, bit 6 = PM swing
// full/half). AM dips the long-window RMS by the depth-dependent average
// (triangle LFO: ~2.4 dB deep, ~0.6 dB shallow); VIB changes the waveform
// (phase wobble) without moving the level. Map-agnostic.
// ---------------------------------------------------------------------------
void FmAmVibDepthMatrix()
{
    std::printf("FmAmVibDepthMatrix\n");
    const auto run = [](uint8_t amVib, uint8_t bdDepth) {
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, 0x00);
        FmCarrierPatch(tc.chip, 0, 0, 4, 582, 0x00);
        uint8_t m, r;
        FmOpAddr(0, 0x20, &m, &r);
        tc.chip.WriteFm(0, 0, r, static_cast<uint8_t>(0x21 | amVib));
        tc.chip.WriteFm(0, 0, 0xBD, bdDepth);
        return CaptureOutput(tc, 0, 24576);
    };
    const auto differs = [](const std::vector<float>& a, const std::vector<float>& b,
                            size_t fromFrame) {
        if (a.size() != b.size())
            return true;
        for (size_t i = 2 * fromFrame; i < a.size(); i++)
            if (a[i] != b[i])
                return true;
        return false;
    };

    const auto refF = run(0x00, 0x00);
    const double rRef = RmsLeft(refF, 256);
    CHECK(rRef > kFmCarrierRmsMin);
    const auto deepF = run(0x80, 0x80);  // AM on, 4.8 dB swing
    const auto shalF = run(0x80, 0x00);  // AM on, 1.2 dB swing
    const double rDeep = RmsLeft(deepF, 256);
    const double rShal = RmsLeft(shalF, 256);
    std::printf("  am off %.4f deep %.4f shallow %.4f\n", rRef, rDeep, rShal);
    CHECK(rDeep < rRef * 0.83 && rDeep > rRef * 0.65); // ~2.4 dB average
    CHECK(rDeep < rShal * 0.95);                       // depth bit matters
    CHECK(rShal < rRef * 0.98 && rShal > rRef * 0.87); // ~0.6 dB average
    CHECK(rDeep > kFmCarrierRmsMin / 4.0);             // tremolo, not gate

    const auto vibF = run(0x40, 0x00);      // VIB on, half swing
    const auto vibDeepF = run(0x40, 0x40);  // VIB on, full swing
    const double rVib = RmsLeft(vibF, 256);
    CHECK(rVib > rRef * 0.95 && rVib < rRef * 1.05);    // level unchanged
    CHECK(differs(vibF, refF, 64));                     // waveform moves
    CHECK(differs(vibDeepF, vibF, 64));                 // depth bit moves it
    CHECK(differs(vibDeepF, refF, 64));
}

// ---------------------------------------------------------------------------
// FM output routing: C0 bits 4-7 steer the outputs. ymfm models the full
// OPL4 quad: CHA/CHB (bits 4/5) enable pair-A sides, CHC/CHD (bits 6/7)
// the second pair, which the backend sums into the same L/R mix — a side
// is audible when any of its enable bits is set. The PoC in-tree model
// treats bits 4/5 as per-side mutes with a both-sides default and leaves
// CHC/CHD unmodelled — a documented divergence, so only the pattern
// function differs between backends.
// ---------------------------------------------------------------------------
void FmRoutingMatrix()
{
    std::printf("FmRoutingMatrix\n");
    const auto lr = [](uint8_t route) {
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, 0x00);
        FmCarrierPatch(tc.chip, 0, 0, 4, 582, 0x00);
        tc.chip.WriteFm(0, 0, 0xC0, route);
        (void)CaptureOutput(tc, 2048, 0); // flush attack prefix
        const auto frames = CaptureOutput(tc, 2048, 2048);
        return std::make_pair(RmsLeft(frames, 128), RmsRight(frames, 128));
    };
    const double quiet = kFmCarrierRmsMin / 24.0;
    const double level = kFmCarrierRmsMin / 1.2;
    for (int combo = 0; combo < 16; combo++)
    {
        const uint8_t route = static_cast<uint8_t>(combo << 4);
        const bool l = (route & 0x50) != 0; // CHA (bit 4) or CHC (bit 6)
        const bool r = (route & 0xA0) != 0; // CHB (bit 5) or CHD (bit 7)
        const auto [rl, rr] = lr(route);
        std::printf("  route %02X L %.4f R %.4f\n", route, rl, rr);
        CHECK(l ? rl > level : rl < quiet);
        CHECK(r ? rr > level : rr < quiet);
    }
}

// ---------------------------------------------------------------------------
// FM timers (0x02/0x03 load, 0x04 enable/mask/reset): T1 fires every
// (256-load)*4 FM steps, T2 every *16; one FM step = 684 master clocks,
// one output frame = 768, so load 0xF0 puts T1 at ~57 frames and T2 at
// ~228. Flags land in status bits 6/5; masks gate them; 0x80 clears the
// flags only (F8: RST is exclusive, counters keep counting). The timer
// block lives in FmBus — identical for both FM engines.
// ---------------------------------------------------------------------------
void FmTimerSweep()
{
    std::printf("FmTimerSweep\n");
    const auto statusAfter = [](uint8_t tSel, uint64_t frames) {
        TestChip tc;
        tc.chip.WriteFm(0, 0, 0x02, 0xF0);
        tc.chip.WriteFm(0, 0, 0x03, 0xF0);
        tc.chip.WriteFm(0, 0, 0x04, tSel);
        (void)CaptureOutput(tc, 0, frames);
        return tc.chip.ReadStatus(frames * kOutClocks);
    };
    CHECK((statusAfter(0x01, 16) & 0x40) == 0);  // T1 not yet
    CHECK((statusAfter(0x01, 128) & 0x40) != 0); // T1 fired
    CHECK((statusAfter(0x02, 64) & 0x20) == 0);  // T2 not yet
    CHECK((statusAfter(0x02, 512) & 0x20) != 0); // T2 fired
    CHECK((statusAfter(0x41, 128) & 0x40) == 0); // T1 masked: no flag
    CHECK((statusAfter(0x22, 512) & 0x20) == 0); // T2 masked: no flag
    {
        const uint8_t both = statusAfter(0x03, 512);
        CHECK((both & 0x60) == 0x60);            // both flags together
    }
    // Reset (F8): after T1 fires, 0x04 bit 7 clears the flag; the counter
    // keeps counting (never zeroed), so a short follow-up still stays clear
    // (the remaining period must elapse) and the flag re-fires on the next
    // crossing.
    TestChip tc;
    tc.chip.WriteFm(0, 0, 0x02, 0xF0);
    tc.chip.WriteFm(0, 0, 0x04, 0x01);
    (void)CaptureOutput(tc, 0, 128);
    CHECK((tc.chip.ReadStatus(128 * kOutClocks) & 0x40) != 0);
    tc.chip.WriteFm(128 * kOutClocks, 0, 0x04, 0x81); // reset + keep enabled
    (void)CaptureOutput(tc, 128, 16);
    CHECK((tc.chip.ReadStatus(144 * kOutClocks) & 0x40) == 0);
    (void)CaptureOutput(tc, 144, 128);
    CHECK((tc.chip.ReadStatus(272 * kOutClocks) & 0x40) != 0); // re-fires
}

// ---------------------------------------------------------------------------
// FM key-on is edge-triggered on the B0 kon bit: rewriting B0 with kon
// already set must not disturb the envelope (exact stream equality with a
// no-rewrite control), while a koff/kon cycle retriggers through silence.
// Map-agnostic.
// ---------------------------------------------------------------------------
void FmKonMomentary()
{
    std::printf("FmKonMomentary\n");
    const auto konReg = static_cast<uint8_t>(((582 >> 8) & 0x03) | (4 << 2));
    const auto run = [](bool rewrite) {
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, 0x00);
        FmEnvSpec s;
        FmEnvVoice(tc.chip, 0, s, true);
        if (rewrite)
            tc.chip.WriteFm(5000 * kOutClocks, 0, 0xB0,
                            static_cast<uint8_t>(konReg | 0x20)); // kon rewrite
        return CaptureOutput(tc, 0, 8192);
    };
    const auto ctrl = run(false);
    const auto same = run(true);
    CHECK(ctrl.size() == same.size());
    CHECK(std::equal(ctrl.begin(), ctrl.end(), same.begin())); // no retrigger

    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00);
    FmEnvSpec s;
    FmEnvVoice(tc.chip, 0, s, true);
    tc.chip.WriteFm(5000 * kOutClocks, 0, 0xB0, konReg);             // koff
    tc.chip.WriteFm(5400 * kOutClocks, 0, 0xB0,
                    static_cast<uint8_t>(konReg | 0x20));             // kon
    const auto cyc = CaptureOutput(tc, 0, 8192);
    CHECK(cyc.size() == ctrl.size());
    CHECK(!std::equal(ctrl.begin(), ctrl.end(), cyc.begin()));
    const auto wins = WindowRms(cyc, 32);
    CHECK(wins[150] > kFmCarrierRmsMin);              // sustained before koff
    CHECK(wins[165] < wins[150] / 8.0);                // released to silence
    CHECK(wins[172] > wins[150] * 0.9);                // re-attacked after kon
}

// ---------------------------------------------------------------------------
// Envelope rate cadence, map-agnostic cross-backend point: the OPL family
// rate machinery halves the attack interval per rate index (shift ladder +
// sub-table rows), so AR half-level times must step ~2x on BOTH backends.
// ---------------------------------------------------------------------------
void FmEnvelopeRatesVsYmfm()
{
    std::printf("FmEnvelopeRatesVsYmfm\n");
    TestChip tc;
    tc.chip.WriteWave(0, 0xF8, 0x00);

    double peakRef = 0.0;
    uint64_t t = 0;
    {
        FmEnvSpec s;
        FmEnvVoice(tc.chip, t, s, true);
        const auto frames = CaptureOutput(tc, t, 2048);
        const auto wins = WindowRms(frames, 64);
        size_t nw = std::min<size_t>(wins.size(), 4);
        for (size_t i = wins.size() - nw; i < wins.size(); i++)
            peakRef += wins[i];
        peakRef /= static_cast<double>(nw);
        FmEnvVoice(tc.chip, t + 2048, s, false);
        (void)CaptureOutput(tc, t + 2048, 512);
        t += 2048 + 512;
    }

    double t50[10] = {};
    for (int ar = 4; ar <= 9; ar++)
    {
        FmEnvSpec s;
        s.ar = static_cast<uint8_t>(ar);
        FmEnvVoice(tc.chip, t, s, true);
        const auto frames = CaptureOutput(tc, t, 16384);
        const auto wins = WindowRms(frames, 64);
        t50[ar - 4] = 1e9;
        for (size_t i = 0; i < wins.size(); i++)
            if (wins[i] >= 0.5 * peakRef)
            {
                t50[ar - 4] = (i + 0.5) * 64.0;
                break;
            }
        FmEnvVoice(tc.chip, t + 16384, s, false);
        (void)CaptureOutput(tc, t + 16384, 512);
        t += 16384 + 512;
    }
    std::printf("  t50 = %.0f %.0f %.0f %.0f %.0f %.0f (ref %.4f)\n",
                t50[0], t50[1], t50[2], t50[3], t50[4], t50[5], peakRef);
    CHECK(peakRef > kFmCarrierRmsMin);
    for (int k = 0; k < 5; k++)
    {
        CHECK(t50[k] < 1e8 && t50[k + 1] < 1e8); // every rate reached mid-level
        const double ratio = t50[k] / t50[k + 1];
        CHECK(ratio > 1.5 && ratio < 2.7);       // ~2x cadence per step
    }
}

// ---------------------------------------------------------------------------
// PCM family helpers. Tone headers go through the raw ROM/RAM image:
// linear wave<384 headers live in ROM space, which WriteSram cannot reach.
// Header bytes 7..11 (the banks 5..9 rewrite) stay zero — every envelope
// register is written after the fetch, exactly like KeyOnPcmSlot.
// ---------------------------------------------------------------------------
inline void WriteHdrRaw(WaveMemory& mem, uint32_t addr, uint8_t bits,
                        uint32_t start, uint16_t loop, uint16_t end)
{
    uint8_t b[12] = {};
    b[0] = static_cast<uint8_t>((bits << 6) | ((start >> 16) & 0x3F));
    b[1] = static_cast<uint8_t>(start >> 8);
    b[2] = static_cast<uint8_t>(start);
    b[3] = static_cast<uint8_t>(loop >> 8);
    b[4] = static_cast<uint8_t>(loop);
    const uint16_t s = static_cast<uint16_t>(0x10000u - end);
    b[5] = static_cast<uint8_t>(s >> 8);
    b[6] = static_cast<uint8_t>(s);
    std::memcpy(mem.RomData() + addr, b, 12);
}

inline void FillMem(WaveMemory& mem, uint32_t addr, const uint8_t* pat,
                    size_t patLen, size_t total)
{
    uint8_t* dst = mem.RomData() + addr;
    for (size_t i = 0; i < total; i++)
        dst[i] = pat[i % patLen];
}

// Key PCM slot 0: header base `hdr` (0x02 bits 4..2) written before the
// fetch, 9-bit wave number (0..511 — the high register contributes ONE
// bit), OCT/FN from the two nibble-packed bytes, then the envelope
// registers after the fetch. Full level (AR15/TL0/LD), pan centre. fnLo =
// FNUM bits 6..0, octFn bit 3 = PRVB, bits 2..0 = FNUM bits 9..7, bits
// 7..4 = OCT two's complement.
inline void PcmKeyOn(Opl4& c, int wave, int hdr, uint8_t octFn,
                     uint8_t fnLo = 0x7F, uint8_t arD1r = 0xF0,
                     uint8_t dlD2r = 0x00, uint8_t rcRr = 0x00)
{
    c.WriteWave(0, 0x02, static_cast<uint8_t>(hdr << 2));
    c.WriteWave(0, 0x08 + 24, static_cast<uint8_t>(((wave >> 8) & 1) | (fnLo << 1)));
    c.WriteWave(0, 0x08, static_cast<uint8_t>(wave & 0xFF)); // fetch
    c.WriteWave(0, 0x08 + 24 * 2, octFn);
    c.WriteWave(0, 0x08 + 24 * 6, arD1r);
    c.WriteWave(0, 0x08 + 24 * 7, dlD2r);
    c.WriteWave(0, 0x08 + 24 * 8, rcRr);
    c.WriteWave(0, 0x08 + 24 * 3, 0x01); // TL0 LD1
    c.WriteWave(0, 0x08 + 24 * 4, 0x80); // key on, pan 0
}

// ---------------------------------------------------------------------------
// PCM wave number banking: wave < 384 fetches the tone header linearly
// at wave*12 whatever the header base; wave >= 384 banks by 0x02 bits 4..2
// (waveTblHdr) to waveTblHdr*0x80000 + (wave-384)*12, except base 0 stays
// linear. Past the ROM+RAM window the bus floats 0xFF: header width bits
// read as 3 (unspecified) -> silence. Candidates are told apart by which
// one holds the header pointing at +FS data (an absent header reads zero
// bytes: 8-bit width, start 0 inside zeroed ROM -> silence).
// ---------------------------------------------------------------------------
void PcmWaveNumberBoundary()
{
    std::printf("PcmWaveNumberBoundary\n");
    constexpr uint32_t kLoud = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    const auto play = [&](int wave, int hdr, uint32_t loudAt) {
        TestChip tc;
        FillMem(tc.mem, kLoud, dc, 2, 16);
        WriteHdrRaw(tc.mem, loudAt, 2, kLoud, 0, 4);
        PcmKeyOn(tc.chip, wave, hdr, 0x07);
        const auto frames = CaptureOutput(tc, 0, 512);
        return RmsLeft(frames, 64);
    };
    struct Case
    {
        int wave, hdr;
        uint32_t expect, other;
    };
    const Case cases[] = {
        {383, 4, 383 * 12, 0x200000},                         // <384 stays linear
        {384, 4, 0x200000, 384 * 12},                         // base 4: banked SRAM
        {384, 0, 384 * 12, 0x200000},                         // base 0: linear
        {400, 2, 0x100000 + (400 - 384) * 12, 0x200000 + (400 - 384) * 12},
        {400, 5, 0x280000 + (400 - 384) * 12, 0x100000 + (400 - 384) * 12},
        {100, 7, 100 * 12, 0x280000},                         // <384 ignores base 7
    };
    for (const Case& cs : cases)
    {
        const double at = play(cs.wave, cs.hdr, cs.expect);
        const double mis = play(cs.wave, cs.hdr, cs.other);
        std::printf("  wave %d hdr %d: expected %8.4f misplaced %8.4f\n",
                    cs.wave, cs.hdr, at, mis);
        CHECK(at > 0.8 * kPcmRail); // loud: near the ±FS rail
        CHECK(mis < 0.02);
    }
    // Base 6 with wave 384 reads at 0x300000, past the 2 MiB ROM + 1 MiB
    // SRAM window: the header floats to 0xFF (width 3 -> silence) even
    // though a loud linear copy sits at 384*12.
    {
        TestChip tc;
        FillMem(tc.mem, kLoud, dc, 2, 16);
        WriteHdrRaw(tc.mem, 384 * 12, 2, kLoud, 0, 4);
        PcmKeyOn(tc.chip, 384, 6, 0x07);
        const auto frames = CaptureOutput(tc, 0, 512);
        const double r = RmsLeft(frames, 64);
        std::printf("  wave 384 hdr 6 (past window): %8.4f\n", r);
        CHECK(r < 0.02);
    }
}

// ---------------------------------------------------------------------------
// PCM sample width (header byte 0 bits 7..6): 8-bit sign-extended into the
// top byte, 12-bit pairs packed two samples per three bytes (even sample
// takes the middle byte's LOW nibble, odd its HIGH nibble), 16-bit
// big-endian, and the unspecified code 3 decodes as silence. OCT -8 stops
// the step, freezing the position so the pos-0 value is exact; a ~1
// sample/frame position alternates even/odd, whose long-window mean pins
// the odd decode given the frozen even one.
// ---------------------------------------------------------------------------
void PcmWidthDecodeSweep()
{
    std::printf("PcmWidthDecodeSweep\n");
    constexpr uint32_t kData = 0x200100;
    const auto frozenMean = [&](uint8_t bits, const uint8_t* pat, size_t patLen) {
        TestChip tc;
        FillMem(tc.mem, kData, pat, patLen, 96);
        WriteHdrRaw(tc.mem, kHdrBase, bits, kData, 0, 8);
        PcmKeyOn(tc.chip, 384, 4, 0x87); // OCT -8
        const auto frames = CaptureOutput(tc, 0, 256);
        return MeanLeft(frames, 64);
    };
    const auto near = [](double got, double want, double tol) {
        return got > want - tol && got < want + tol;
    };
    const uint8_t pat8a[] = {0x40};
    const uint8_t pat8b[] = {0x7F};
    const uint8_t pat8c[] = {0x80};
    const uint8_t pat12[] = {0x34, 0x21, 0x78};
    const uint8_t pat16a[] = {0x12, 0x34};
    const uint8_t pat16b[] = {0x80, 0x00};

    const double m8a = frozenMean(0, pat8a, 1);   // 0x40 -> 0x4000
    const double m8b = frozenMean(0, pat8b, 1);   // 0x7F -> 0x7F00 (8-bit max)
    const double m8c = frozenMean(0, pat8c, 1);   // 0x80 -> -FS rail
    const double m12e = frozenMean(1, pat12, 3);  // even: 0x3410
    const double m16a = frozenMean(2, pat16a, 2); // 0x1234
    const double m16b = frozenMean(2, pat16b, 2); // 0x8000 -> -FS rail
    std::printf("  frozen: 8b %.4f/%.4f/%.4f 12b-even %.4f 16b %.4f/%.4f\n",
                m8a, m8b, m8c, m12e, m16a, m16b);
    CHECK(near(m8a, PcmNorm(0x4000), 0.01));
    CHECK(near(m8b, PcmNorm(0x7F00), 0.01));  // 8-bit max positive
    CHECK(near(m8c, -kPcmRail, 0.02));       // 8-bit 0x80 -> -FS rail
    CHECK(near(m12e, PcmNorm(0x3410), 0.01)); // low nibble of 0x21
    CHECK(near(m16a, PcmNorm(0x1234), 0.005));
    CHECK(near(m16b, -kPcmRail, 0.02));       // 16-bit -FS rail

    // Unspecified width code 3: silence at full envelope.
    {
        TestChip tc;
        FillMem(tc.mem, kData, pat16b, 2, 96);
        WriteHdrRaw(tc.mem, kHdrBase, 3, kData, 0, 8);
        PcmKeyOn(tc.chip, 384, 4, 0x07);
        const auto frames = CaptureOutput(tc, 0, 256);
        CHECK(RmsLeft(frames, 64) < 0.01);
    }

    // Running ~1 sample/frame over the 12-bit pair: the long-window mean
    // is the even/odd average, so the odd decode (middle byte's HIGH
    // nibble: 0x7820) is implied by the pinned frozen even value.
    {
        TestChip tc;
        FillMem(tc.mem, kData, pat12, 3, 96);
        WriteHdrRaw(tc.mem, kHdrBase, 1, kData, 0, 8);
        PcmKeyOn(tc.chip, 384, 4, 0x07); // OCT 0, FN 1023
        const auto frames = CaptureOutput(tc, 0, 2048);
        const double mean = MeanLeft(frames, 128);
        const double odd = 2.0 * mean - PcmNorm(0x3410);
        std::printf("  12-bit running mean %.4f -> odd %.4f\n", mean, odd);
        CHECK(near(mean, (PcmNorm(0x3410) + PcmNorm(0x7820)) / 2.0, 0.02));
        CHECK(near(odd, PcmNorm(0x7820), 0.02));
    }
}

// ---------------------------------------------------------------------------
// PCM step: CalcStep = (FNUM+1024) << (5+OCT) in 1/65536-sample units per
// output frame (§5.3); OCT -8 freezes (step 0). A 16-sample full-scale
// square loop makes the zero-crossing count over N frames equal
// N*2*step/(65536*16): exact per case, doubling per OCT step, linear in
// FNUM.
// ---------------------------------------------------------------------------
void PcmStepSweep()
{
    std::printf("PcmStepSweep\n");
    constexpr uint32_t kData = 0x200100;
    const auto runStep = [&](int oct, int fn, uint64_t frames) {
        TestChip tc;
        uint8_t sq[32];
        for (int i = 0; i < 16; i++)
        {
            const uint16_t v = i < 8 ? 0x7FFF : 0x8001;
            sq[2 * i] = static_cast<uint8_t>(v >> 8);
            sq[2 * i + 1] = static_cast<uint8_t>(v);
        }
        FillMem(tc.mem, kData, sq, 32, 64);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 16);
        const int nib = oct < 0 ? 16 + oct : oct; // OCT nibble, two's complement
        const uint8_t octFn = static_cast<uint8_t>((nib << 4) | ((fn >> 7) & 7));
        PcmKeyOn(tc.chip, 384, 4, octFn, static_cast<uint8_t>(fn & 0x7F));
        return CaptureOutput(tc, 0, frames);
    };

    // Crossings are counted from frame 256, and the playback rate is
    // step/65536 samples per frame (CalcStep, §5.3).
    const auto expectZc = [](int oct, int fn) {
        const double rate = (fn + 1024.0) * std::ldexp(1.0, 5 + oct) / 65536.0;
        return 7936.0 * 2.0 * rate / 16.0;
    };

    // OCT ladder at FN 1023 (oct -8 handled below).
    int zc[8];
    for (int oct = -7; oct <= 0; oct++)
    {
        const double expect = expectZc(oct, 1023);
        const auto frames = runStep(oct, 1023, 8192);
        zc[oct + 7] = ZeroCrossLeft(frames, 256);
        const double tol = std::max(2.0, expect * 0.03);
        std::printf("  oct %+d zc %d (expect %.1f)\n", oct, zc[oct + 7], expect);
        CHECK(std::abs(zc[oct + 7] - expect) <= tol);
    }
    for (int k = 0; k < 7; k++)
        CHECK(zc[k + 1] > zc[k] * 1.9 && zc[k + 1] < zc[k] * 2.1); // 2x per OCT

    // FNUM linearity at OCT -2: 1024/1280/1536/2047 -> exact 1.0/1.25/1.5/2.0.
    const int fns[] = {0, 256, 512, 1023};
    for (int fn : fns)
    {
        const double expect = expectZc(-2, fn);
        const auto frames = runStep(-2, fn, 8192);
        const int z = ZeroCrossLeft(frames, 256);
        const double tol = std::max(2.0, expect * 0.03);
        std::printf("  oct -2 fn %4d zc %d (expect %.1f)\n", fn, z, expect);
        CHECK(std::abs(z - expect) <= tol);
    }

    // OCT -8: step 0 — position frozen at sample[0] (+FS): DC, no crossings.
    const auto frozen = runStep(-8, 1023, 2048);
    CHECK(ZeroCrossLeft(frozen, 256) <= 1);
    CHECK(RmsLeft(frozen, 256) > 0.2);
}

// ---------------------------------------------------------------------------
// PCM loop edges: the header stores the loop end as its complement S =
// 0x10000 - end, and the wrap test pos + S >= 0x10000 carries the overrun
// into the loop (pos += S + loopAddr) — the real chip's loop glitch (§5.4).
// A 16-sample positive ramp at ~0.125 samples/frame plays one full pass in
// 128 frames, so the pre-loop first pass and the steady loop mean are
// separable. (end = 0x10000, S = 0 — the true one-shot — spans the whole
// 64 Ki position range and is out of capture budget; documented, skipped.)
// ---------------------------------------------------------------------------
void PcmLoopEdgeMatrix()
{
    std::printf("PcmLoopEdgeMatrix\n");
    constexpr uint32_t kData = 0x200100;
    // s[i] = 0x280 * (i + 1): positive 16-bit ramp 0x280..0x2800.
    uint8_t ramp[32];
    for (int i = 0; i < 16; i++)
    {
        const uint16_t v = static_cast<uint16_t>(0x280 * (i + 1));
        ramp[2 * i] = static_cast<uint8_t>(v >> 8);
        ramp[2 * i + 1] = static_cast<uint8_t>(v);
    }
    const auto meanOver = [](const std::vector<float>& v, size_t a, size_t b) {
        double acc = 0.0;
        size_t n = 0;
        for (size_t f = a; f < b && f < v.size() / 2; f++)
        {
            acc += v[2 * f];
            n++;
        }
        return n ? acc / static_cast<double>(n) : 0.0;
    };
    const auto runLoop = [&](uint16_t loop, uint16_t end, int oct, uint64_t frames,
                             const uint8_t* extra = nullptr, size_t extraLen = 0) {
        TestChip tc;
        FillMem(tc.mem, kData, ramp, 32, 32);
        if (extra)
            FillMem(tc.mem, kData + 32, extra, extraLen, extraLen);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, loop, end);
        const int nib = oct < 0 ? 16 + oct : oct;
        PcmKeyOn(tc.chip, 384, 4, static_cast<uint8_t>((nib << 4) | 7));
        return CaptureOutput(tc, 0, frames);
    };
    const double meanAll = PcmNorm(0x280 * 17 / 2);            // mean(s[0..15])
    const double meanLoop4 = PcmNorm(0x280 * 21 / 2);          // mean(s[4..15])
    const double s15 = PcmNorm(0x2800);

    // end 16, loop 0: every pass identical — steady mean = mean(ramp).
    {
        const auto frames = runLoop(0, 16, -3, 1024);
        const double m = meanOver(frames, 512, 1024);
        std::printf("  loop 0/16 steady %.4f (expect %.4f)\n", m, meanAll);
        CHECK(m > meanAll * 0.97 && m < meanAll * 1.03);
    }
    // end 16, loop 4: first pass still covers s[0..3]; steady mean rises.
    {
        const auto frames = runLoop(4, 16, -3, 1024);
        const double pre = meanOver(frames, 8, 120);   // before first wrap
        const double steady = meanOver(frames, 512, 1024);
        std::printf("  loop 4/16 pre %.4f steady %.4f (expect %.4f)\n",
                    pre, steady, meanLoop4);
        CHECK(pre > meanAll * 0.95 && pre < meanAll * 1.05);
        CHECK(steady > meanLoop4 * 0.97 && steady < meanLoop4 * 1.03);
        CHECK(steady > pre * 1.15);
    }
    // end 16, loop 15: 1-sample loop — pure DC at s[15].
    {
        const auto frames = runLoop(15, 16, -3, 1024);
        const double m = meanOver(frames, 512, 1024);
        std::printf("  loop 15/16 steady %.4f (expect %.4f)\n", m, s15);
        CHECK(m > s15 * 0.97 && m < s15 * 1.03);
    }
    // end 32, loop 0, ramp + 16 zero samples: the position really ranges
    // to end-1, so the steady mean halves.
    {
        const uint8_t zeros[32] = {};
        const auto frames = runLoop(0, 32, -3, 1024, zeros, 32);
        const double m = meanOver(frames, 768, 1024);
        std::printf("  loop 0/32 steady %.4f (expect %.4f)\n", m, meanAll / 2);
        CHECK(m > meanAll * 0.48 && m < meanAll * 0.52);
    }
    // Overrun carry at ~2 samples/frame: pos lands past the end by up to
    // one before wrapping; steady coverage stays the loop mean.
    {
        const auto frames = runLoop(4, 16, 1, 4096);
        const double m = meanOver(frames, 512, 4096);
        std::printf("  loop 4/16 @2x steady %.4f (expect %.4f)\n", m, meanLoop4);
        CHECK(m > meanLoop4 * 0.94 && m < meanLoop4 * 1.06);
    }
}

// ---------------------------------------------------------------------------
// PCM total level (7-bit TL, 0.375 dB per step — the envelope's 4x finer
// index domain shifted by 2). TL 0x7F maps to internal 0xFF (D6), past the
// -60 dB silence clip. LD bit 0 applies TL immediately; LD 0 walks tl one
// step per 27 frames down (attenuate) and per 13.5 frames up — exactly 2:1
// asymmetry. Measured on a frozen +FS DC sample so the level is the only
// moving part.
// ---------------------------------------------------------------------------
void PcmTlLadderSweep()
{
    std::printf("PcmTlLadderSweep\n");
    constexpr uint32_t kData = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    const auto tlLevel = [&](uint8_t tlReg, uint64_t at) {
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 16);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 4);
        PcmKeyOn(tc.chip, 384, 4, 0x87); // OCT -8: frozen DC, full envelope
        (void)CaptureOutput(tc, 0, at);
        tc.chip.WriteWave(at * kOutClocks, 0x08 + 24 * 3,
                          static_cast<uint8_t>(tlReg << 1 | 0x01)); // LD 1
        const auto frames = CaptureOutput(tc, at, 128);
        return MeanLeft(frames, 32);
    };

    double lvl[16];
    for (int k = 0; k < 16; k++)
    {
        lvl[k] = tlLevel(static_cast<uint8_t>(k * 8), 256);
        std::printf("  TL %3d level %.5f\n", k * 8, lvl[k]);
    }
    CHECK(lvl[0] > 0.9 * kPcmRail);          // TL 0: near rail
    CHECK(lvl[15] < lvl[0] * 0.02);          // TL 120: -45 dB
    for (int k = 1; k < 16; k++)
    {
        CHECK(lvl[k] <= lvl[k - 1] * 1.02);   // non-increasing
        const double ratio = lvl[k] / lvl[k - 1];
        CHECK(ratio > 0.69 && ratio < 0.73);  // 8 steps = exactly 3 dB
    }

    // 0x7F special vs 0x7E: the -47 dB neighbour is still above the silence
    // clip; 0x7F -> 0xFF internal is hard silence.
    const double nearMax = tlLevel(0x7E, 256);
    const double maxSil = tlLevel(0x7F, 256);
    std::printf("  TL 0x7E %.6f  0x7F %.6f\n", nearMax, maxSil);
    CHECK(nearMax > 0.0005 && nearMax < 0.004);
    CHECK(maxSil < 0.0002);

    // LD cadence: TL 0x20 with LD 0 ramps down 1 step / 27 frames — the
    // -6 dB point (tl 16) is crossed at ~432 frames; back to TL 0 LD 0 the
    // 13.5-frames-per-step recovery crosses at ~216 — a factor 2 faster.
    {
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 16);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 4);
        PcmKeyOn(tc.chip, 384, 4, 0x87);
        (void)CaptureOutput(tc, 0, 256); // settle at TL 0
        tc.chip.WriteWave(256 * kOutClocks, 0x08 + 24 * 3, 0x20 << 1); // LD 0
        const auto down = CaptureOutput(tc, 256, 1024);
        tc.chip.WriteWave(1280 * kOutClocks, 0x08 + 24 * 3, 0x00); // TL 0 LD 0
        const auto up = CaptureOutput(tc, 1280, 768);
        const auto wDown = WindowRms(down, 32);
        const auto wUp = WindowRms(up, 32);
        const auto cross = [](const std::vector<double>& w, double level, bool rising) {
            for (size_t i = 0; i < w.size(); i++)
                if (rising ? w[i] >= level : w[i] <= level)
                    return (i + 0.5) * 32.0;
            return 1e9;
        };
        const double half = 0.5 * kPcmRail;
        const double tDown = cross(wDown, half, false);
        const double tUp = cross(wUp, half, true);
        std::printf("  LD ramp: down %.0f (expect 432) up %.0f (expect 216)\n",
                    tDown, tUp);
        CHECK(std::abs(tDown - 432.0) <= 64.0);
        CHECK(std::abs(tUp - 216.0) <= 48.0);
        CHECK(tDown / tUp > 1.6 && tDown / tUp < 2.4);
        CHECK(wDown.back() > 0.055 && wDown.back() < 0.070); // TL 32 = -12 dB
        CHECK(wUp.back() > 0.2 * kPcmRail);
    }
}

// ---------------------------------------------------------------------------
// PCM envelope rates: rate = val*4 + 2*clamp(OCT+RC,0,15) + FNUM bit 9
// (RC 15 disables the correction); rate 0 freezes, rate 63 (val 15) is
// instant. Four rate units = one shift step, so adjacent AR values are
// exactly 2x apart in time (same kEgInc row) — the OPL family cadence. The
// attack is convex (inc ~ envVol), so t25 < t50 < 2*t25 per run. DL k
// sustains at exactly -3k dB (kDecayLevelTable); DL 15 is 93 dB = past the
// -60 dB clip = silence. Measured on a frozen +FS DC sample.
// ---------------------------------------------------------------------------
void PcmEnvRateMatrix()
{
    std::printf("PcmEnvRateMatrix\n");
    constexpr uint32_t kData = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    // DC region must cover the whole loop: the rate-correction case runs
    // OCT 4 (16 samples/frame) whose step overruns the end-fold — the wrap
    // subtracts `end`, it does not fold mod (end-loop), so a small region
    // would strand the position in unwritten (zero) memory.
    const auto runEnv = [&](uint8_t arD1r, uint8_t dlD2r, uint8_t rcRr,
                            uint64_t frames, int keyoffAt = -1,
                            uint8_t octFn = 0x87) {
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 1024);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 512);
        PcmKeyOn(tc.chip, 384, 4, octFn, 0x7F, arD1r, dlD2r, rcRr);
        if (keyoffAt >= 0)
            tc.chip.WriteWave(static_cast<uint64_t>(keyoffAt) * kOutClocks,
                              0x08 + 24 * 4, 0x00); // key off -> release
        return CaptureOutput(tc, 0, frames);
    };
    const auto tRise = [](const std::vector<double>& w, double level, int win) {
        for (size_t i = 0; i < w.size(); i++)
            if (w[i] >= level)
                return (i + 0.5) * win;
        return 1e9;
    };
    const auto tFallFrom = [](const std::vector<double>& w, double level, int win,
                              size_t fromWin) {
        for (size_t i = fromWin; i < w.size(); i++)
            if (w[i] <= level)
                return (i + 0.5) * win;
        return 1e9;
    };

    // AR 0 never attacks; AR 15 attacks within the first window.
    {
        const auto frozen = runEnv(0x00, 0x00, 0x00, 4096);
        CHECK(RmsLeft(frozen, 64) < 0.02 * kPcmRail);
        const auto inst = runEnv(0xF0, 0x00, 0x00, 256);
        const auto w = WindowRms(inst, 32);
        CHECK(w[0] > 0.8 * kPcmRail);
    }
    // AR ladder (RC 15, no correction): AR 5..8 — each step halves the time.
    // Thresholds are output-domain (50% level = envVol 64), so the convex
    // attack has t25 (envVol 128) < t50.
    {
        const uint64_t caps[4] = {16384, 8192, 4096, 2048};
        double t50[4] = {}, t25a = 0.0, t25b = 0.0;
        for (int k = 0; k < 4; k++)
        {
            const auto w = WindowRms(runEnv(static_cast<uint8_t>((5 + k) << 4),
                                            0x00, 0xF0, caps[k]), 32);
            t50[k] = tRise(w, 0.5 * kPcmRail, 32);
            if (k == 1)
                t25a = tRise(w, 0.25 * kPcmRail, 32);
            if (k == 2)
                t25b = tRise(w, 0.25 * kPcmRail, 32);
        }
        std::printf("  AR5..8 t50 = %.0f %.0f %.0f %.0f (t25 AR6/7 = %.0f/%.0f)\n",
                    t50[0], t50[1], t50[2], t50[3], t25a, t25b);
        for (int k = 0; k < 4; k++)
            CHECK(t50[k] < 1e8);
        for (int k = 0; k < 3; k++)
        {
            const double ratio = t50[k] / t50[k + 1];
            CHECK(ratio > 1.5 && ratio < 2.7);          // 2x per AR step
        }
        CHECK(t25a < t50[1] && t25a > 0.3 * t50[1]);    // convex attack shape
        CHECK(t25a / t25b > 1.5 && t25a / t25b < 2.7);  // t25 keeps the cadence
    }
    // Rate correction: OCT 4 + RC 0 adds 9 rate units to AR 6 (rate 24 ->
    // 33): three shift steps faster (8x) offset by the row-1 inc table
    // (0.625 vs 0.5) — t50 shrinks by ~5x.
    {
        const auto corr = WindowRms(runEnv(0x60, 0x00, 0x00, 2048, -1, 0x47), 32);
        const double tCorr = tRise(corr, 0.5 * kPcmRail, 32);
        const auto base = WindowRms(runEnv(0x60, 0x00, 0xF0, 8192), 32);
        const double tBase = tRise(base, 0.5 * kPcmRail, 32);
        std::printf("  AR6 t50: RC15 %.0f vs OCT4/RC0 %.0f\n", tBase, tCorr);
        CHECK(tBase < 1e8 && tCorr < 1e8);
        CHECK(tBase / tCorr > 3.0 && tBase / tCorr < 7.5);
    }

    // DL ladder: D1R 13 (rate 52: +2/frame, settled well inside 256 frames)
    // into DL k, D2R 0 sustain — exactly -3k dB.
    {
        double sus[10];
        for (int k = 0; k <= 8; k++)
        {
            const auto frames = runEnv(0xFD, static_cast<uint8_t>(k << 4), 0xF0, 1024);
            sus[k] = RmsLeft(frames, 256);
        }
        for (int k = 0; k <= 8; k++)
            std::printf("  DL%d sustain %.5f\n", k, sus[k]);
        CHECK(sus[0] > 0.9 * kPcmRail);
        for (int k = 1; k <= 8; k++)
        {
            CHECK(sus[k] <= sus[k - 1] * 1.03);       // non-increasing
            const double ratio = sus[k] / sus[k - 1];
            CHECK(ratio > 0.685 && ratio < 0.73);     // 3 dB per DL step
        }
        const auto off = runEnv(0xFD, static_cast<uint8_t>(15 << 4), 0xF0, 1024);
        CHECK(RmsLeft(off, 256) < 0.004 * kPcmRail); // DL 15: silence
    }

    // Release: RR 0 holds the sustain; RR 8 vs RR 9 (2x apart) fall to half
    // in ~1.6k/~0.9k frames after the keyoff; RR 15 is fast silence. The
    // crossing times are absolute (keyoff at 512).
    {
        const auto hold = runEnv(0xF0, 0x00, 0x00, 4096, 512);
        CHECK(RmsLeft(hold, 3200) > 0.8 * kPcmRail);
        const auto r8 = WindowRms(runEnv(0xF0, 0x00, 0x08, 4096, 512), 64);
        const auto r9 = WindowRms(runEnv(0xF0, 0x00, 0x09, 2560, 512), 64);
        const double t8 = tFallFrom(r8, 0.5 * kPcmRail, 64, 512 / 64);
        const double t9 = tFallFrom(r9, 0.5 * kPcmRail, 64, 512 / 64);
        std::printf("  RR8 t50 = %.0f  RR9 t50 = %.0f (after 512-frame keyoff)\n", t8, t9);
        CHECK(t8 < 1e8 && t9 < 1e8);
        const double rel8 = t8 - 512.0, rel9 = t9 - 512.0;
        CHECK(rel8 > 1000.0 && rel8 < 3000.0);
        CHECK(rel8 / rel9 > 1.5 && rel8 / rel9 < 2.7);
        const auto fast = runEnv(0xF0, 0x00, 0x0F, 2048, 512);
        CHECK(RmsLeft(fast, 1500) < 0.02 * kPcmRail);
    }

    // Sustain decay: D2R 0 is flat; D2R 6 decays visibly (rate 24: row 0
    // inc, gate 64 — about -6 dB over the window).
    {
        const auto flat = runEnv(0xF0, 0x00, 0x00, 8192);
        const auto slow = runEnv(0xF0, 0x06, 0x00, 8192);
        const double f0 = RmsLeft(flat, 64), f1 = RmsLeft(flat, 7800);
        const double s0 = RmsLeft(slow, 64), s1 = RmsLeft(slow, 7800);
        std::printf("  D2R0 %.4f->%.4f  D2R6 %.4f->%.4f\n", f0, f1, s0, s1);
        CHECK(f1 > 0.95 * f0);
        CHECK(s1 < 0.8 * s0);
        CHECK(s1 > 0.3 * s0);
    }
}

// ---------------------------------------------------------------------------
// PCM DAMP and PRVB. DAMP (reg+4 bit 6) overrides every decay rate on
// release with a fixed two-tier curve: internal rate 48 (+1 index/frame)
// down to -12 dB, then rate 63 (+4/frame) to the silence clip — ignoring RR
// entirely. PRVB (reg+2 bit 3) redirects Dec/Sus/Rel decay to internal rate
// 20 (0.5 index per 128 frames) once the envelope passes -18 dB, replacing
// the tail with a slow pseudo-reverb shelf.
// ---------------------------------------------------------------------------
void PcmDampPrvbMatrix()
{
    std::printf("PcmDampPrvbMatrix\n");
    constexpr uint32_t kData = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    const auto runCase = [&](bool damp, bool prvb, uint8_t rr, int keyoffAt,
                             uint64_t frames) {
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 1024);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 512);
        PcmKeyOn(tc.chip, 384, 4, static_cast<uint8_t>(prvb ? 0x8F : 0x87),
                 0x7F, 0xF0, 0x00, rr);
        // DAMP rides in with the keyoff write: latching it earlier would
        // fade the sustaining voice (damper-pedal semantics).
        tc.chip.WriteWave(static_cast<uint64_t>(keyoffAt) * kOutClocks,
                          0x08 + 24 * 4,
                          static_cast<uint8_t>(damp ? 0x40 : 0x00));
        return CaptureOutput(tc, 0, frames);
    };
    {
        const auto hold = runCase(false, false, 0x00, 512, 2048);
        CHECK(RmsLeft(hold, 768) > 0.8 * kPcmRail); // RR 0: sustain holds
        const auto damped = runCase(true, false, 0x00, 512, 2048);
        const auto w = WindowRms(damped, 32);
        std::printf("  damp: w16 %.3f w18 %.3f w20 %.3f w24 %.4f (hold %.3f)\n",
                    w[16] / kPcmRail, w[18] / kPcmRail, w[20] / kPcmRail,
                    w[24] / kPcmRail, RmsLeft(hold, 768) / kPcmRail);
        CHECK(w[16] > 0.55 * kPcmRail && w[16] < 0.95 * kPcmRail);
        CHECK(w[18] > 0.30 * kPcmRail && w[18] < 0.62 * kPcmRail);
        CHECK(w[20] > 0.12 * kPcmRail && w[20] < 0.38 * kPcmRail); // -12 dB tier
        CHECK(w[24] < 0.01 * kPcmRail);  // silence clip by ~+256
        CHECK(RmsLeft(damped, 1120) < 0.01 * kPcmRail);
    }
    {
        // DAMP during sustain fades even without keyoff (the damper pedal
        // itself, openMSX lineage): same two-tier curve from the write on.
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 1024);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 512);
        PcmKeyOn(tc.chip, 384, 4, 0x87, 0x7F, 0xF0, 0x00, 0x00);
        tc.chip.WriteWave(64 * kOutClocks, 0x08 + 24 * 4, 0xC0);
        const auto f = CaptureOutput(tc, 0, 512);
        std::printf("  sustain damp: first %.3f tail %.4f\n",
                    RmsLeft(f, 0) / kPcmRail, RmsLeft(f, 384) / kPcmRail);
        CHECK(RmsLeft(f, 0) > 0.35 * kPcmRail);
        CHECK(RmsLeft(f, 384) < 0.01 * kPcmRail);
    }
    {
        const auto tail = runCase(false, true, 0x0D, 256, 2560);
        const auto gone = runCase(false, false, 0x0D, 256, 2560);
        const double held = RmsLeft(tail, 1024);
        std::printf("  prvb: shelf %.3f vs plain %.4f\n", held / kPcmRail,
                    RmsLeft(gone, 1024) / kPcmRail);
        CHECK(held > 0.085 * kPcmRail && held < 0.16 * kPcmRail); // ~-18 dB
        CHECK(RmsLeft(gone, 1024) < 0.01 * kPcmRail); // same RR, no shelf
    }
}

// ---------------------------------------------------------------------------
// PCM pan (reg+4 bits 3..0, D8): 16 positions, exactly -3 dB per step from
// centre; 7 = hard right (left off), 8 = both off, 9..15 mirror to the left.
// Bit 4 is the DO1 digital-output pin, unwired on ZXM-MoonSound: any DO1
// setting is modelled as silence.
// ---------------------------------------------------------------------------
void PcmPanSweep()
{
    std::printf("PcmPanSweep\n");
    constexpr uint32_t kData = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    const auto pan = [&](uint8_t reg4) {
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 1024);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 512);
        PcmKeyOn(tc.chip, 384, 4, 0x87, 0x7F, 0xF0, 0x00, 0x00);
        tc.chip.WriteWave(32 * kOutClocks, 0x08 + 24 * 4, reg4);
        (void)CaptureOutput(tc, 0, 64);
        const auto f = CaptureOutput(tc, 64, 192);
        return std::make_pair(RmsLeft(f, 8), RmsRight(f, 8));
    };
    const auto [rl0, rr0] = pan(0x80);
    CHECK(rl0 > 0.9 * kPcmRail && rr0 > 0.9 * kPcmRail);
    const double quiet = 0.01 * kPcmRail;
    // right half: left side loses 3 dB per step, right side stays full
    const double att[7] = {1.0, 0.708, 0.501, 0.355, 0.251, 0.178, 0.126};
    for (int p = 1; p <= 6; p++)
    {
        const auto [rl, rr] = pan(static_cast<uint8_t>(0x80 | p));
        std::printf("  pan %d L %.3f R %.3f\n", p, rl / kPcmRail, rr / kPcmRail);
        CHECK(rl > rl0 * att[p] * 0.88 && rl < rl0 * att[p] * 1.14);
        CHECK(rr > rr0 * 0.88 && rr < rr0 * 1.12);
    }
    {
        const auto [r7l, r7r] = pan(0x87);
        CHECK(r7l < quiet && r7r > rr0 * 0.9);   // hard right
        const auto [r8l, r8r] = pan(0x88);
        CHECK(r8l < quiet && r8r < quiet);       // both off
        const auto [r9l, r9r] = pan(0x89);
        CHECK(r9l > rl0 * 0.9 && r9r < quiet);   // hard left
        const auto [f5l, f5r] = pan(0x8F);
        CHECK(f5l > rl0 * 0.88 && f5l < rl0 * 1.12);
        CHECK(f5r > rr0 * 0.708 * 0.88 && f5r < rr0 * 0.708 * 1.14); // -3 dB
        const auto [dl, dr] = pan(0x90);         // DO1: silence
        CHECK(dl < quiet && dr < quiet);
    }
}

// ---------------------------------------------------------------------------
// PCM LFO (bank 5: bits 5..3 frequency, bits 2..0 vibrato depth; bank 9
// bits 2..0 AM depth; reg+4 bit 5 = per-slot LFO reset/hold). AM adds a
// triangular attenuation of the envelope (depth 7 = 127 index = -11.9 dB
// swing at the 7.07 Hz setting); vibrato wobbles FNUM by +-depth*15/12,
// moving the waveform while leaving level and long-run rate intact.
// ---------------------------------------------------------------------------
void PcmLfoMatrix()
{
    std::printf("PcmLfoMatrix\n");
    constexpr uint32_t kData = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    const auto differsFrom = [](const std::vector<float>& a,
                                const std::vector<float>& b, size_t from) {
        if (a.size() != b.size())
            return true;
        for (size_t i = 2 * from; i < a.size(); i++)
            if (std::fabs(a[i] - b[i]) > 1e-6)
                return true;
        return false;
    };
    const auto minWin = [](const std::vector<float>& f, size_t win) {
        double lo = 1e9;
        for (double x : WindowRms(f, win))
            lo = std::min(lo, x);
        return lo;
    };
    {
        const auto am = [&](int lfo, int depth, bool rst) {
            TestChip tc;
            FillMem(tc.mem, kData, dc, 2, 1024);
            WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 512);
            PcmKeyOn(tc.chip, 384, 4, 0x87, 0x7F, 0xF0, 0x00, 0x00);
            tc.chip.WriteWave(0, 0x08 + 24 * 9, static_cast<uint8_t>(depth));
            tc.chip.WriteWave(0, 0x08 + 24 * 5,
                              static_cast<uint8_t>(lfo << 3));
            if (rst) // key stays on; bit 5 holds the LFO off
                tc.chip.WriteWave(0, 0x08 + 24 * 4, 0xA0);
            return CaptureOutput(tc, 0, 8192);
        };
        const auto ref = am(7, 0, false);
        const double r0 = RmsLeft(ref, 0);
        CHECK(r0 > 0.9 * kPcmRail);
        const auto deep = am(7, 7, false);
        const double lo = minWin(deep, 64);
        double hi = 0.0;
        for (double x : WindowRms(deep, 64))
            hi = std::max(hi, x);
        const double rDeep = RmsLeft(deep, 0);
        std::printf("  am7: rms %.3f min %.3f max %.3f (ref %.3f)\n",
                    rDeep / kPcmRail, lo / kPcmRail, hi / kPcmRail,
                    r0 / kPcmRail);
        CHECK(lo < 0.62 * r0 && lo > 0.20 * r0); // -11.9 dB swing floor
        CHECK(hi > 0.95 * r0);
        CHECK(rDeep > 0.42 * r0 && rDeep < 0.75 * r0);
        const auto shal = am(7, 1, false);
        CHECK(minWin(shal, 64) > 0.75 * r0);      // depth 1 = -1.9 dB swing
        CHECK(RmsLeft(shal, 0) > 0.90 * r0);
        const auto rst = am(7, 7, true);
        CHECK(RmsLeft(rst, 0) > 0.98 * r0);       // bit 5 holds LFO off
        CHECK(differsFrom(am(5, 7, false), deep, 256)); // frequency moves phase
    }
    {
        const auto vib = [&](int depth) {
            TestChip tc;
            uint8_t sq[32]; // 16 samples: 8x +FS then 8x -FS (16-bit BE)
            for (int i = 0; i < 8; i++)
            {
                sq[2 * i] = 0x7F;
                sq[2 * i + 1] = 0xFF;
                sq[16 + 2 * i] = 0x80;
                sq[17 + 2 * i] = 0x00;
            }
            FillMem(tc.mem, kData, sq, 32, 32);
            WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 16);
            PcmKeyOn(tc.chip, 384, 4, 0x00, 0x00, 0xF0, 0x00, 0x00); // 0.5/frame
            tc.chip.WriteWave(0, 0x08 + 24 * 5,
                              static_cast<uint8_t>((7 << 3) | depth));
            return CaptureOutput(tc, 0, 8192);
        };
        const auto refV = vib(0);
        const double r0 = RmsLeft(refV, 256);
        const int z0 = ZeroCrossLeft(refV, 256);
        const auto onV = vib(7);
        const int z7 = ZeroCrossLeft(onV, 256);
        const double r7 = RmsLeft(onV, 256);
        std::printf("  vib7: rms ratio %.3f zc %d vs %d\n", r7 / r0, z7, z0);
        CHECK(differsFrom(onV, refV, 256));
        CHECK(r7 > r0 * 0.93 && r7 < r0 * 1.07);  // level intact
        CHECK(z7 > z0 * 0.97 && z7 < z0 * 1.03);  // long-run rate intact
        CHECK(differsFrom(vib(1), refV, 256));    // depth 1 still audible
    }
}

// ---------------------------------------------------------------------------
// PCM interpolation: the 16-bit fractional step pointer always blends
// GetSample(pos) with GetSample(NextPos(pos,1)) weighted by frac/65536 —
// there is no enable bit, "off" is frac == 0. Exact steppers pin both
// edges: 1 sample/frame (pure staircase, no blend), 0.5 (odd frames sit on
// the half step), 1.5 (a half-step blend between integer advances). Every
// frame is compared against the golden unity chain (VolFactor env -> TL ->
// pan, 2047/2048 per stage, like opl4vectors' GoldenFrame).
// ---------------------------------------------------------------------------
void PcmInterpMatrix()
{
    std::printf("PcmInterpMatrix\n");
    constexpr uint32_t kData = 0x200100;
    const auto run = [&](uint8_t octFn, uint8_t fnLo, uint64_t frames) {
        TestChip tc;
        uint8_t ramp[128]; // 64 16-bit samples: 0x1000 + 0x100*i
        for (int i = 0; i < 64; i++)
        {
            const int s = 0x1000 + 0x100 * i;
            ramp[2 * i] = static_cast<uint8_t>(s >> 8);
            ramp[2 * i + 1] = static_cast<uint8_t>(s);
        }
        FillMem(tc.mem, kData, ramp, 128, 128);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 64);
        PcmKeyOn(tc.chip, 384, 4, octFn, fnLo, 0xF0, 0x00, 0x00);
        return CaptureOutput(tc, 0, frames);
    };
    // Expected frame at a given loop offset in half-sample units: ramp
    // value (with the exact wrap blend at 63.5), then the three unity
    // attenuation stages.
    const auto golden = [&](int halfSteps) {
        const int p = halfSteps >> 1;
        int32_t sm = 0x1000 + 0x100 * p;
        if (halfSteps & 1) // blend partner wraps at the loop end
            sm = (sm + 0x1000 + 0x100 * ((p + 1) % 64)) >> 1;
        const int32_t v = VolFactor(VolFactor(VolFactor(sm, 0), 0), 0);
        return static_cast<double>(v) * kNormScale;
    };
    const auto check = [&](const std::vector<float>& f, int halfPerFrame,
                           const char* tag) {
        int bad = 0;
        double maxErr = 0.0;
        for (size_t k = 256; k < f.size() / 2; k++)
        {
            const int h = static_cast<int>(
                (halfPerFrame * static_cast<long long>(k)) % 128);
            const double err = std::fabs(f[2 * k] - golden(h));
            maxErr = std::max(maxErr, err);
            bad += !(err < 5e-8); // float32 rounding of the exact rational
        }
        std::printf("  %s: %d bad, max err %.2e\n", tag, bad, maxErr);
        CHECK(bad == 0);
    };
    check(run(0x10, 0x00, 512), 2, "step 1.0 (OCT1/FN0)");
    check(run(0x00, 0x00, 512), 1, "step 0.5 (OCT0/FN0)");
    check(run(0x14, 0x00, 512), 3, "step 1.5 (OCT1/FN512)");
}

// ---------------------------------------------------------------------------
// Block mix (0xF8 FM / 0xF9 PCM, D9): L code = bits 5..3, R code = bits
// 2..0, through the silicon table (2042 = 0 dB, 1444 = -3 dB, ..., 0 =
// mute). Reset states 0x1B (FM -9 dB both sides) and 0x00 (PCM unity). Each
// register scales only its own block. Map-agnostic: the mixers sit behind
// both FM backends.
// ---------------------------------------------------------------------------
void MixFieldMatrix()
{
    std::printf("MixFieldMatrix\n");
    constexpr uint32_t kData = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    const auto pcm = [&](uint8_t f8, uint8_t f9) {
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 1024);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 512);
        PcmKeyOn(tc.chip, 384, 4, 0x87, 0x7F, 0xF0, 0x00, 0x00);
        tc.chip.WriteWave(0, 0xF8, f8);
        tc.chip.WriteWave(0, 0xF9, f9);
        (void)CaptureOutput(tc, 0, 64);
        const auto f = CaptureOutput(tc, 64, 192);
        return std::make_pair(RmsLeft(f, 8), RmsRight(f, 8));
    };
    {
        const auto [pl0, pr0] = pcm(0x00, 0x00);
        CHECK(pl0 > 0.9 * kPcmRail && pr0 > 0.9 * kPcmRail);
        const double scale[7] = {1.0,     1444.0 / 2042.0, 1021.0 / 2042.0,
                                 722.0 / 2042.0, 510.0 / 2042.0, 361.0 / 2042.0,
                                 255.0 / 2042.0};
        for (int code = 1; code <= 6; code++)
        {
            const auto [pl, pr] =
                pcm(0x00, static_cast<uint8_t>(code | (code << 3)));
            std::printf("  pcm mix code %d: L %.3f R %.3f\n", code,
                        pl / kPcmRail, pr / kPcmRail);
            CHECK(pl > pl0 * scale[code] * 0.9 && pl < pl0 * scale[code] * 1.1);
            CHECK(pr > pr0 * scale[code] * 0.9 && pr < pr0 * scale[code] * 1.1);
        }
        const auto [rl, rr] = pcm(0x00, 0x07); // R mute, L unity
        CHECK(rl > pl0 * 0.9 && rr < 0.01 * kPcmRail);
        const auto [ql, qr] = pcm(0x00, 0x38); // L mute, R unity
        CHECK(ql < 0.01 * kPcmRail && qr > pr0 * 0.9);
        const auto [ml, mr] = pcm(0x00, 0xFF); // both mute
        CHECK(ml < 0.01 * kPcmRail && mr < 0.01 * kPcmRail);
        // 0xF8 must not touch the PCM block
        const auto [xl, xr] = pcm(0x38, 0x00);
        CHECK(xl > pl0 * 0.95 && xr > pr0 * 0.95);
    }
    const auto fm = [&](uint8_t f8, uint8_t f9) {
        TestChip tc;
        tc.chip.WriteWave(0, 0xF8, f8);
        tc.chip.WriteWave(0, 0xF9, f9);
        FmCarrierPatch(tc.chip, 0, 0, 4, 582, 0x00);
        (void)CaptureOutput(tc, 0, 1024);
        return CaptureOutput(tc, 1024, 1024);
    };
    {
        const auto f0 = fm(0x00, 0x00);
        const double fl0 = RmsLeft(f0, 64), fr0 = RmsRight(f0, 64);
        CHECK(fl0 > kFmCarrierRmsMin);
        const auto fd = fm(0x1B, 0x00); // reset default: -9 dB both sides
        const double ratio = RmsLeft(fd, 64) / fl0;
        std::printf("  fm mix 0x1B ratio %.3f (722/2042 = %.3f)\n", ratio,
                    722.0 / 2042.0);
        CHECK(ratio > 0.31 && ratio < 0.40);
        CHECK(std::abs(RmsRight(fd, 64) / fr0 - ratio) < 0.04);
        const auto fr = fm(0x07, 0x00); // R mute, L unity
        CHECK(RmsLeft(fr, 64) > fl0 * 0.9 && RmsRight(fr, 64) < 0.02 * fl0);
        const auto fll = fm(0x38, 0x00); // L mute, R unity
        CHECK(RmsLeft(fll, 64) < 0.02 * fl0 && RmsRight(fll, 64) > fr0 * 0.9);
        const auto fn = fm(0x00, 0x07); // 0xF9 does not touch FM
        CHECK(RmsLeft(fn, 64) > fl0 * 0.95);
    }
}

// ---------------------------------------------------------------------------
// Wave-memory access port: 0x02 bit 0 = MA gates the data port; 0x03/0x04
// only latch (0x03 upper bits read 0); the full 22-bit address commits on
// the 0x05 write; 0x06 reads/writes with auto-increment, and reads return
// 0xFF when MA = 0.
// ---------------------------------------------------------------------------
void MemoryAccessSweep()
{
    std::printf("MemoryAccessSweep\n");
    TestChip tc;
    Opl4& c = tc.chip;
    c.WriteWave(0, 0x02, 0x01); // MA on
    CHECK((c.ReadWave(10, 0x02) & 0x1F) == 0x01);
    CHECK((c.ReadWave(12, 0x02) & 0x20) != 0); // device-ID bit reads 1
    c.WriteWave(20, 0x03, 0x20);
    c.WriteWave(30, 0x04, 0x00);
    c.WriteWave(40, 0x05, 0x00); // commit 0x200000
    c.WriteWave(50, 0x06, 0x11);
    c.WriteWave(60, 0x06, 0x22);
    c.WriteWave(70, 0x06, 0x33);
    c.WriteWave(80, 0x06, 0x44);
    c.WriteWave(90, 0x03, 0x20);
    c.WriteWave(100, 0x04, 0x01);
    c.WriteWave(110, 0x05, 0x00); // commit 0x200100
    c.WriteWave(120, 0x06, 0xAA);
    // 0x03/0x04 latch only: without a 0x05 write the pointer must not move
    c.WriteWave(130, 0x03, 0x2F);
    c.WriteWave(140, 0x04, 0xFF);
    c.WriteWave(150, 0x06, 0xBB); // still lands at 0x200101
    c.WriteWave(160, 0x05, 0x02); // now commits 0x2FFF02 (still SRAM)
    c.WriteWave(170, 0x06, 0xCC);
    // read-back round trip with read auto-increment
    c.WriteWave(180, 0x03, 0x20);
    c.WriteWave(190, 0x04, 0x00);
    c.WriteWave(200, 0x05, 0x00);
    CHECK_EQ_I(c.ReadWave(210, 0x06), 0x11);
    CHECK_EQ_I(c.ReadWave(220, 0x06), 0x22);
    CHECK_EQ_I(c.ReadWave(230, 0x06), 0x33);
    CHECK_EQ_I(c.ReadWave(240, 0x06), 0x44);
    c.WriteWave(250, 0x03, 0x20);
    c.WriteWave(260, 0x04, 0x01);
    c.WriteWave(270, 0x05, 0x00);
    CHECK_EQ_I(c.ReadWave(280, 0x06), 0xAA);
    CHECK_EQ_I(c.ReadWave(290, 0x06), 0xBB); // proves the latch-only rule
    c.WriteWave(300, 0x03, 0x2F);
    c.WriteWave(310, 0x04, 0xFF);
    c.WriteWave(320, 0x05, 0x02);
    CHECK_EQ_I(c.ReadWave(330, 0x06), 0xCC);
    c.WriteWave(340, 0x03, 0xFF);
    CHECK_EQ_I(c.ReadWave(350, 0x03), 0x3F); // upper bits read 0
    // MA off: reads float 0xFF, writes are ignored
    c.WriteWave(360, 0x02, 0x00);
    CHECK_EQ_I(c.ReadWave(370, 0x06), 0xFF);
    c.WriteWave(380, 0x06, 0x99);
    c.WriteWave(390, 0x02, 0x01);
    c.WriteWave(400, 0x03, 0x20);
    c.WriteWave(410, 0x04, 0x01);
    c.WriteWave(420, 0x05, 0x03);
    CHECK_EQ_I(c.ReadWave(430, 0x06), 0x00); // SRAM initial, not 0x99
}

// ---------------------------------------------------------------------------
// Seeded fuzz tier (§12.6): an LCG drives random register streams across
// both buses with random timestamps (register order and interleaving are
// free-form; tone fetches, memory-port writes and mix rewrites all occur).
// Every seed must render finite, in-range, subnormal-free audio, and a
// second run of the same seed must reproduce the stream bit-exactly. A
// failing seed becomes a permanent regression vector.
// ---------------------------------------------------------------------------
void SeededFuzzTier()
{
    std::printf("SeededFuzzTier\n");
    constexpr uint32_t kData = 0x200100;
    const uint8_t dc[2] = {0x7F, 0xFF};
    const auto runSeed = [&](uint32_t seed) {
        uint32_t st = seed;
        const auto rnd = [&st]() {
            st = st * 1664525u + 1013904223u;
            return st;
        };
        TestChip tc;
        FillMem(tc.mem, kData, dc, 2, 1024);
        WriteHdrRaw(tc.mem, kHdrBase, 2, kData, 0, 512);
        tc.chip.WriteWave(0, 0xF8, 0x00); // FM unity mix
        // Structured opening: one FM carrier and one PCM voice with random
        // valid parameters, so most seeds exercise audible synthesis before
        // the free-form writes land.
        FmCarrierPatch(tc.chip, 0, static_cast<int>(rnd() % 4),
                       static_cast<int>(2 + rnd() % 5),
                       static_cast<int>(64 + rnd() % 1500),
                       static_cast<uint8_t>(rnd() % 48));
        static const uint8_t kOctFns[4] = {0x07, 0x27, 0x47, 0x87};
        PcmKeyOn(tc.chip, 384, 4, kOctFns[rnd() % 4],
                 static_cast<uint8_t>(rnd() % 0x80));
        uint64_t t = 0;
        for (int i = 0; i < 56; i++)
        {
            t += (rnd() % 48 + 1) * kOutClocks;
            const uint32_t r = rnd();
            if (r & 0x100) // wave bus: tone regs, 0x02, memory port, mixes
                tc.chip.WriteWave(t, static_cast<uint8_t>(r >> 8),
                                  static_cast<uint8_t>(r >> 16));
            else // FM register banks
                tc.chip.WriteFm(t, (r >> 8) & 1,
                                static_cast<uint8_t>(r >> 16),
                                static_cast<uint8_t>(r >> 24));
        }
        return CaptureOutput(tc, 0, t / kOutClocks + 256);
    };
    int nonFinite = 0, subnormal = 0, mismatches = 0, loud = 0;
    for (uint32_t seed = 1; seed <= 96; seed++)
    {
        const auto a = runSeed(seed);
        const auto b = runSeed(seed);
        if (a.size() != b.size()
            || !std::equal(a.begin(), a.end(), b.begin()))
            mismatches++;
        double peak = 0.0;
        for (float v : a)
        {
            if (!std::isfinite(v))
                nonFinite++;
            else if (std::fpclassify(v) == FP_SUBNORMAL)
                subnormal++;
            else
                peak = std::max(peak, std::fabs(static_cast<double>(v)));
        }
        CHECK(peak <= 1.0); // normalized wide rail, no wrap
        if (peak > 0.001)
            loud++;
    }
    std::printf("  96 seeds: %d replays mismatched, %d non-finite, "
                "%d subnormal, %d audible\n",
                mismatches, nonFinite, subnormal, loud);
    CHECK(mismatches == 0);
    CHECK(nonFinite == 0);
    CHECK(subnormal == 0);
    CHECK(loud > 8); // the streams genuinely make sound
}

int RunSweepTests()
{
    std::printf("\n--- conformance sweeps (opl4sweep.cpp) ---\n");
    const int before = gFailed;
    FmTlLadderSweep();
    FmMultSweep();
    FmKslSweep();
    FmEnvStageSweep();
    FmFeedbackSweep();
    Fm4OpConnections();
    FmRhythmSweep();
    FmWaveformSweep();
    FmAmVibDepthMatrix();
    FmRoutingMatrix();
    FmTimerSweep();
    FmKonMomentary();
    FmEnvelopeRatesVsYmfm();
    PcmWaveNumberBoundary();
    PcmWidthDecodeSweep();
    PcmStepSweep();
    PcmLoopEdgeMatrix();
    PcmTlLadderSweep();
    PcmEnvRateMatrix();
    PcmDampPrvbMatrix();
    PcmPanSweep();
    PcmLfoMatrix();
    PcmInterpMatrix();
    MixFieldMatrix();
    MemoryAccessSweep();
    SeededFuzzTier();
    return gFailed - before;
}

} // namespace opl4test
