// libopl4 — PCM-domain tables (44100 Hz wavetable half, core TDD §5).
//
// Provenance (core TDD §13.1): envelope rate machinery (eg_inc /
// eg_rate_select / eg_rate_shift, DAMP / PRVB rates, LFO tables, dl_tab,
// calcStep): openMSX YMF278.cc lineage — the most accurate published PCM
// model. The FM half carries its own copies in fm/fmtables.h ("copies,
// not includes": no translation unit ever sees both, and the FM
// re-derivation re-domains its set without touching these).
//
// Shared exp2/pan/block-mix math lives in common/ (exptable.h, mixtables.h)
// and is intentionally NOT re-included here — users include what they use.
#pragma once

#include <array>
#include <cstdint>

namespace opl4
{

// ---------------------------------------------------------------------------
// PCM attenuation domain: 10-bit index, 3/32 dB (0.09375 dB) per step.
// MAX_ATT_INDEX = 0x280 = 640 = -60 dB: the hardware silence clip (D4).
// ---------------------------------------------------------------------------
constexpr int kMaxAttIndex = 0x280;
constexpr int kMinAttIndex = 0;
constexpr int kTlShift = 2; // envelope has 4x the resolution of TL

inline constexpr int16_t SignExtend4(int x)
{
    return static_cast<int16_t>((x ^ 8) - 8);
}

// ---------------------------------------------------------------------------
// Decay level table (3 dB per step); SC(93) = full silence.
// ---------------------------------------------------------------------------
inline constexpr int16_t DecayLevel(int dB)
{
    return static_cast<int16_t>(dB / 3 * 0x20);
}

inline constexpr std::array<int16_t, 16> kDecayLevelTable = {
    DecayLevel(0), DecayLevel(3), DecayLevel(6), DecayLevel(9),
    DecayLevel(12), DecayLevel(15), DecayLevel(18), DecayLevel(21),
    DecayLevel(24), DecayLevel(27), DecayLevel(30), DecayLevel(33),
    DecayLevel(36), DecayLevel(39), DecayLevel(42), DecayLevel(93),
};

// ---------------------------------------------------------------------------
// Envelope rate machinery (openMSX lineage, hardware-verified).
// 6-bit rate index: regRate*4 + rate correction. Rate 63 = infinity
// (attack 15 => zero time). Rates 0..63 select shift + sub-table rows.
// ---------------------------------------------------------------------------
constexpr uint8_t kRateSteps = 8;

inline constexpr std::array<uint8_t, 15 * kRateSteps> kEgInc = {
    // cycle: 0  1   2  3   4  5   6  7
    0, 1,  0, 1,  0, 1,  0, 1, //  0  rates 00..12, k 0 (inc by 0 or 1)
    0, 1,  0, 1,  1, 1,  0, 1, //  1
    0, 1,  1, 1,  0, 1,  1, 1, //  2
    0, 1,  1, 1,  1, 1,  1, 1, //  3
    1, 1,  1, 1,  1, 1,  1, 1, //  4  rate 13 k 0 (inc by 1)
    1, 1,  1, 2,  1, 1,  1, 2, //  5
    1, 2,  1, 2,  1, 2,  1, 2, //  6
    1, 2,  2, 2,  1, 2,  2, 2, //  7
    2, 2,  2, 2,  2, 2,  2, 2, //  8  rate 14 k 0 (inc by 2)
    2, 2,  2, 4,  2, 2,  2, 4, //  9
    2, 4,  2, 4,  2, 4,  2, 4, // 10
    2, 4,  4, 4,  2, 4,  4, 4, // 11
    4, 4,  4, 4,  4, 4,  4, 4, // 12  rate 15 decay (inc by 4)
    8, 8,  8, 8,  8, 8,  8, 8, // 13  rate 15 attack (zero time)
    0, 0,  0, 0,  0, 0,  0, 0, // 14  infinity
};

inline constexpr uint8_t RateRow(int a) { return static_cast<uint8_t>(a * kRateSteps); }

inline constexpr std::array<uint8_t, 64> kEgRateSelect = {
    RateRow(14), RateRow(14), RateRow(14), RateRow(14), // inf rate
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(0), RateRow(1), RateRow(2), RateRow(3),
    RateRow(4), RateRow(5), RateRow(6), RateRow(7),
    RateRow(8), RateRow(9), RateRow(10), RateRow(11),
    RateRow(12), RateRow(12), RateRow(12), RateRow(12), // rates 56..59
    RateRow(12), RateRow(12), RateRow(12), RateRow(12), // rates 60..63
};

// The "rate 15" band (rates 60..63) must select the inc-4 row. A short
// initializer would zero-fill these entries, silently aliasing RateRow(0)
// and turning zero-time release / DAMP tier 2 into a 0.5-per-sample crawl
// — the bug the §12.1 table-integrity vectors catch.
static_assert(kEgRateSelect[60] == RateRow(12) && kEgRateSelect[63] == RateRow(12),
              "kEgRateSelect rates 60..63 must be fully initialised");

// rate  0    1    2    3   4   5   6  7  8  9 10 11 12 13 14 15
// shift 12   11   10   9   8   7   6  5  4  3  2  1  0  0  0  0
inline constexpr std::array<uint8_t, 64> kEgRateShift = {
    12, 12, 12, 12,
    11, 11, 11, 11,
    10, 10, 10, 10,
    9, 9, 9, 9,
    8, 8, 8, 8,
    7, 7, 7, 7,
    6, 6, 6, 6,
    5, 5, 5, 5,
    4, 4, 4, 4,
    3, 3, 3, 3,
    2, 2, 2, 2,
    1, 1, 1, 1,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
    0, 0, 0, 0,
};

static_assert(kEgRateShift[63] == 0, "rate 63 must fire every sample");
static_assert(kEgInc.size() == 15 * kRateSteps, "kEgInc must be complete");

// ---------------------------------------------------------------------------
// LFO (openMSX, hardware-recording-verified formulas).
// 18-bit counter; period table in counter steps per 44100 Hz sample.
// ---------------------------------------------------------------------------
constexpr unsigned kLfoShift = 18;
constexpr unsigned kLfoPeriod = 1u << kLfoShift;

// round(kLfoPeriod * freqHz / 44100)
inline constexpr int LfoStepFor(double freqHz)
{
    return static_cast<int>(kLfoPeriod * freqHz / 44100.0 + 0.5);
}

inline constexpr std::array<int, 8> kLfoSteps = {
    LfoStepFor(0.168), LfoStepFor(2.019), LfoStepFor(3.196), LfoStepFor(4.206),
    LfoStepFor(5.215), LfoStepFor(5.888), LfoStepFor(6.224), LfoStepFor(7.066),
};

// vibrato depth in F-Number cents units (Yamaha doc formula)
inline constexpr std::array<int16_t, 8> kVibDepth = {0, 2, 3, 4, 6, 12, 24, 48};

// AM depth index (Yamaha doc formula; multiplies the 0..0x7F AM counter)
inline constexpr std::array<uint8_t, 8> kAmDepth = {
    0x00, 0x14, 0x20, 0x28, 0x30, 0x40, 0x50, 0x80,
};

// ---------------------------------------------------------------------------
// calcStep (§5.3): step pointer increment per output sample.
// OCT in [-8..+7] signed, FN 10-bit; OCT -8 freezes the sample.
// ---------------------------------------------------------------------------
inline constexpr uint32_t CalcStep(int8_t oct, uint16_t fn, int16_t vib = 0)
{
    if (oct == -8)
        return 0;
    const uint32_t t = (static_cast<uint32_t>(fn) + 1024u
                        + static_cast<uint32_t>(static_cast<int32_t>(vib)))
                       << (8 + oct);
    return t >> 3;
}

} // namespace opl4
