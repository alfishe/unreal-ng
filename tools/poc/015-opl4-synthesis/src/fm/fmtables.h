// libopl4 — FM-domain tables (YMF262-class half, 49516.4 Hz grid, §4).
//
// The attenuation constants and rate machinery below derive from the PCM
// set in pcm/pcmtables.h, but are RENAMED (kFm prefix) — not shared and
// not same-named: the two engines link into one binary, and same-named
// inline constexpr tables in one namespace are an ODR violation whose
// winner is linker-arbitrary (the CRYOGENT investigation: the FM envelope
// silently ran on the PCM half-cadence shift table because the linker
// kept the PCM copy for every runtime-indexed read). The FM re-derivation
// re-domains this copy to the OPL3 envelope grid (0.1875 dB steps, 96 dB
// ceiling, x2 increment ladder) without touching the silicon-verified
// PCM tables.
//
// The wave-shaping tables (sine, PM scale, MULT, KSL) and FmRateRow are
// FM-only and were never shared.
#pragma once

#include <array>
#include <cstdint>

namespace opl4
{

// ---------------------------------------------------------------------------
// FM attenuation domain (copy, pre-re-domain): same 10-bit 3/32 dB grid as
// PCM today; step 5 moves this to the OPL3 domain. See the file comment.
// ---------------------------------------------------------------------------
constexpr int kFmMaxAttIndex = 0x280;
constexpr int kFmMinAttIndex = 0;

// ---------------------------------------------------------------------------
// Envelope rate machinery (copy of the PCM set, openMSX lineage).
// ---------------------------------------------------------------------------
constexpr uint8_t kFmRateSteps = 8;

inline constexpr std::array<uint8_t, 15 * kFmRateSteps> kFmEgInc = {
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

inline constexpr uint8_t FmRateOffset(int a) { return static_cast<uint8_t>(a * kFmRateSteps); }

inline constexpr std::array<uint8_t, 64> kFmEgRateSelect = {
    FmRateOffset(14), FmRateOffset(14), FmRateOffset(14), FmRateOffset(14), // inf rate
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(0), FmRateOffset(1), FmRateOffset(2), FmRateOffset(3),
    FmRateOffset(4), FmRateOffset(5), FmRateOffset(6), FmRateOffset(7),
    FmRateOffset(8), FmRateOffset(9), FmRateOffset(10), FmRateOffset(11),
    FmRateOffset(12), FmRateOffset(12), FmRateOffset(12), FmRateOffset(12), // rates 56..59
    FmRateOffset(12), FmRateOffset(12), FmRateOffset(12), FmRateOffset(12), // rates 60..63
};

// Same integrity anchor as the PCM copy: the "rate 15" band must select
// the inc-4 row (zero-time release / DAMP tier 2).
static_assert(kFmEgRateSelect[60] == FmRateOffset(12) && kFmEgRateSelect[63] == FmRateOffset(12),
              "kFmEgRateSelect rates 60..63 must be fully initialised");

// OPL3 envelope cadence (re-domained from the PCM copy, which stays at its
// own half-cadence): silicon clocks a 5.11 envelope counter — ymfm
// ymfm_fm.ipp clock_envelope shifts env_counter by rate>>2 and fires when
// the 11-bit fraction clears, i.e. every 2^(11-(rate>>2)) samples (rates
// 40-43 every 2 samples, 44+ every sample). The PCM lineage shifts below
// are one higher; keeping them halved every FM decay/release rate — the
// CRYOGENT (mfm_sample_2 melody 7) drum ring-out the retrigger
// differential measured at exactly x2.
// rate  0    1    2    3   4   5   6   7   8   9  10  11  12  13  14  15
// shift 11   11   11   11  10  10  10  10   9   9   8   8   7   7   6   6
inline constexpr std::array<uint8_t, 64> kFmEgRateShift = {
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
    0, 0, 0, 0,
};

static_assert(kFmEgRateShift[63] == 0, "rate 63 must fire every sample");
static_assert(kFmEgRateShift[40] == 1 && kFmEgRateShift[44] == 0,
              "OPL3 cadence: rates 40-43 fire every 2 samples (ymfm 5.11");
static_assert(kFmEgInc.size() == 15 * kFmRateSteps, "kFmEgInc must be complete");

// ---------------------------------------------------------------------------
// FM-only wave shaping.
// ---------------------------------------------------------------------------

// First-quadrant sine, 256 entries, 13-bit scale (max 4096).
inline constexpr std::array<int16_t, 256> kSineTable = {
    0, 25, 50, 75, 101, 126, 151, 176,
    201, 226, 251, 276, 301, 326, 351, 376,
    401, 426, 451, 476, 501, 526, 551, 576,
    601, 626, 651, 675, 700, 725, 750, 774,
    799, 824, 848, 873, 897, 922, 946, 971,
    995, 1020, 1044, 1068, 1092, 1117, 1141, 1165,
    1189, 1213, 1237, 1261, 1285, 1309, 1332, 1356,
    1380, 1404, 1427, 1451, 1474, 1498, 1521, 1544,
    1567, 1591, 1614, 1637, 1660, 1683, 1706, 1729,
    1751, 1774, 1797, 1819, 1842, 1864, 1886, 1909,
    1931, 1953, 1975, 1997, 2019, 2041, 2062, 2084,
    2106, 2127, 2149, 2170, 2191, 2213, 2234, 2255,
    2276, 2296, 2317, 2338, 2359, 2379, 2399, 2420,
    2440, 2460, 2480, 2500, 2520, 2540, 2559, 2579,
    2598, 2618, 2637, 2656, 2675, 2694, 2713, 2732,
    2751, 2769, 2788, 2806, 2824, 2843, 2861, 2878,
    2896, 2914, 2932, 2949, 2967, 2984, 3001, 3018,
    3035, 3052, 3068, 3085, 3102, 3118, 3134, 3150,
    3166, 3182, 3198, 3214, 3229, 3244, 3260, 3275,
    3290, 3305, 3320, 3334, 3349, 3363, 3378, 3392,
    3406, 3420, 3433, 3447, 3461, 3474, 3487, 3500,
    3513, 3526, 3539, 3551, 3564, 3576, 3588, 3600,
    3612, 3624, 3636, 3647, 3659, 3670, 3681, 3692,
    3703, 3713, 3724, 3734, 3745, 3755, 3765, 3775,
    3784, 3794, 3803, 3812, 3822, 3831, 3839, 3848,
    3857, 3865, 3873, 3881, 3889, 3897, 3905, 3912,
    3920, 3927, 3934, 3941, 3948, 3954, 3961, 3967,
    3973, 3979, 3985, 3991, 3996, 4002, 4007, 4012,
    4017, 4022, 4027, 4031, 4036, 4040, 4044, 4048,
    4052, 4055, 4059, 4062, 4065, 4068, 4071, 4074,
    4076, 4079, 4081, 4083, 4085, 4087, 4088, 4090,
    4091, 4092, 4093, 4094, 4095, 4095, 4096, 4096,
};

// PM LFO scale (YMF262): the 8192-step 6.04 Hz counter's top 3 bits index
// this bipolar F-number-fraction multiplier; the 0xBD bit 6 depth control
// halves the swing.
inline constexpr int8_t kPmScale[8] = {8, 4, 0, -4, -8, -4, 0, 4};

// YMF262 multiplier select (datasheet): non-linear at the top — MULT 11
// is x10, 13 is x12, 14 is x15 (11/13/14 duplicate their neighbours);
// MULT 0 is x0.5. Found by the conformance sweep against the ymfm
// reference (FmMultSweep): the linear step*mult model mis-pitches
// exactly these three settings.
inline constexpr uint8_t kMultTable[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};

// KSL attenuation (ymfm opl_key_scale_atten, YMF262 silicon): 0.75 dB
// units indexed by the 4 F-number MSBs, minus 8 per block below 7,
// clamped at 0; the 2-bit KSL field shifts the result (0 = off).
inline constexpr std::array<uint8_t, 16> kKslAtten = {
    0, 24, 32, 37, 40, 43, 45, 47, 48, 50, 51, 52, 53, 54, 55, 56};

// FM rate-index rows 0..3: EgRate yields only 0 (register rate 0 — a
// hard freeze taken in AdvanceEnvelope per ymfm effective_rate /
// Nuked's reg_rate gate: key scale never rescues a zero register rate) or
// 4..63, so rows 1..3 are unreachable from the FM side; rate 0 parks on
// the slowest 0/1 increment row as a guard rather than misselecting the
// mid table.
inline uint8_t FmRateRow(uint8_t rate)
{
    return rate < 4 ? FmRateOffset(0) : kFmEgRateSelect[rate];
}

} // namespace opl4
