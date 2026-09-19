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
// FM attenuation domain: 10-bit 3/32 dB grid (TL << 3, SL << 5) with the
// OPL3 96 dB ceiling (ymfm 0x3FF; Nuked's 9-bit 0x1FF at 3/16 dB). The PCM
// half keeps its own -60 dB floor: FM carrying that floor attacked from
// -60 dB (~20% early vs ymfm/Nuked) and silenced quiet modulators.
// ---------------------------------------------------------------------------
constexpr int kFmMaxAttIndex = 0x3FF;
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

// First-quadrant sine, 256 entries, 13-bit scale: round(4096 * sin((i + 0.5)
// * pi / 512)) — the silicon's half-step log-sin sampling (ymfm builds its
// table from (2i + 1) * pi / 1024), so the quadrant folds in SineOf mirror
// exactly. Sampling at i * pi / 512 repeated the peak and zero samples at
// every fold and added a -54 dB third harmonic.
inline constexpr std::array<int16_t, 256> kSineTable = {
    13, 38, 63, 88, 113, 138, 163, 188, 214, 239, 264, 289, 314, 339, 364, 389,
    414, 439, 464, 489, 514, 539, 564, 589, 613, 638, 663, 688, 713, 737, 762, 787,
    811, 836, 861, 885, 910, 934, 959, 983, 1007, 1032, 1056, 1080, 1105, 1129, 1153, 1177,
    1201, 1225, 1249, 1273, 1297, 1321, 1344, 1368, 1392, 1415, 1439, 1462, 1486, 1509, 1533, 1556,
    1579, 1602, 1625, 1648, 1671, 1694, 1717, 1740, 1763, 1785, 1808, 1830, 1853, 1875, 1898, 1920,
    1942, 1964, 1986, 2008, 2030, 2052, 2073, 2095, 2117, 2138, 2159, 2181, 2202, 2223, 2244, 2265,
    2286, 2307, 2328, 2348, 2369, 2389, 2410, 2430, 2450, 2470, 2490, 2510, 2530, 2550, 2569, 2589,
    2608, 2628, 2647, 2666, 2685, 2704, 2723, 2741, 2760, 2779, 2797, 2815, 2833, 2852, 2870, 2887,
    2905, 2923, 2940, 2958, 2975, 2992, 3009, 3026, 3043, 3060, 3077, 3093, 3110, 3126, 3142, 3158,
    3174, 3190, 3206, 3221, 3237, 3252, 3267, 3282, 3297, 3312, 3327, 3342, 3356, 3370, 3385, 3399,
    3413, 3426, 3440, 3454, 3467, 3481, 3494, 3507, 3520, 3532, 3545, 3558, 3570, 3582, 3594, 3606,
    3618, 3630, 3642, 3653, 3664, 3675, 3686, 3697, 3708, 3719, 3729, 3739, 3750, 3760, 3770, 3779,
    3789, 3798, 3808, 3817, 3826, 3835, 3844, 3852, 3861, 3869, 3877, 3885, 3893, 3901, 3909, 3916,
    3923, 3930, 3937, 3944, 3951, 3958, 3964, 3970, 3976, 3982, 3988, 3994, 3999, 4005, 4010, 4015,
    4020, 4024, 4029, 4034, 4038, 4042, 4046, 4050, 4053, 4057, 4060, 4064, 4067, 4070, 4072, 4075,
    4077, 4080, 4082, 4084, 4086, 4088, 4089, 4090, 4092, 4093, 4094, 4094, 4095, 4096, 4096, 4096,
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
