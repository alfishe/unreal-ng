// libopl4 — silicon-derived tables and pure attenuation math.
//
// Provenance (core TDD §13.1):
//  - s_power_table: Yamaha die-derived 256-entry logarithmic power table
//    2^(-i/256), round(2047 * 2^(-i/256)) — the D5 ground truth. The early
//    openMSX 0.75 linear guess is explicitly rejected (+0.51 dB/stage).
//  - kMixScale: verified 11-bit Yamaha silicon block-mix table (ymfm
//    s_mix_scale), 2042 = 0 dB unity.
//  - envelope rate machinery (eg_inc / eg_rate_select / eg_rate_shift, DAMP /
//    PRVB rates, LFO tables, pan positions, dl_tab, calcStep): openMSX
//    YMF278.cc lineage — the most accurate published PCM model.
#pragma once

#include <array>
#include <cstdint>

namespace opl4
{

// ---------------------------------------------------------------------------
// Attenuation domain: 10-bit index, 3/32 dB (0.09375 dB) per step.
// MAX_ATT_INDEX = 0x280 = 640 = -60 dB: the hardware silence clip (D4).
// ---------------------------------------------------------------------------
constexpr int kMaxAttIndex = 0x280;
constexpr int kMinAttIndex = 0;
constexpr int kTlShift = 2; // envelope has 4x the resolution of TL

inline constexpr int16_t SignExtend4(int x)
{
    return static_cast<int16_t>((x ^ 8) - 8);
}

// 256-entry logarithmic power table: round(2047 * 2^(-i/256)).
// step 0 = 2047 (unity), step 128 = 1444 (-3.01 dB ≈ 1/sqrt(2)).
inline constexpr std::array<uint16_t, 256> kPowerTable = {
    2047, 2041, 2036, 2030, 2025, 2019, 2014, 2009, 2003, 1998, 1992, 1987, 1982, 1976, 1971, 1966,
    1960, 1955, 1950, 1944, 1939, 1934, 1929, 1923, 1918, 1913, 1908, 1903, 1898, 1892, 1887, 1882,
    1877, 1872, 1867, 1862, 1857, 1852, 1847, 1842, 1837, 1832, 1827, 1822, 1817, 1812, 1807, 1802,
    1798, 1793, 1788, 1783, 1778, 1773, 1769, 1764, 1759, 1754, 1750, 1745, 1740, 1735, 1731, 1726,
    1721, 1717, 1712, 1707, 1703, 1698, 1694, 1689, 1684, 1680, 1675, 1671, 1666, 1662, 1657, 1653,
    1648, 1644, 1639, 1635, 1631, 1626, 1622, 1617, 1613, 1609, 1604, 1600, 1596, 1591, 1587, 1583,
    1578, 1574, 1570, 1566, 1561, 1557, 1553, 1549, 1545, 1540, 1536, 1532, 1528, 1524, 1520, 1516,
    1512, 1507, 1503, 1499, 1495, 1491, 1487, 1483, 1479, 1475, 1471, 1467, 1463, 1459, 1455, 1451,
    1447, 1444, 1440, 1436, 1432, 1428, 1424, 1420, 1416, 1413, 1409, 1405, 1401, 1397, 1394, 1390,
    1386, 1382, 1379, 1375, 1371, 1367, 1364, 1360, 1356, 1353, 1349, 1345, 1342, 1338, 1335, 1331,
    1327, 1324, 1320, 1317, 1313, 1309, 1306, 1302, 1299, 1295, 1292, 1288, 1285, 1281, 1278, 1274,
    1271, 1268, 1264, 1261, 1257, 1254, 1251, 1247, 1244, 1240, 1237, 1234, 1230, 1227, 1224, 1220,
    1217, 1214, 1211, 1207, 1204, 1201, 1198, 1194, 1191, 1188, 1185, 1181, 1178, 1175, 1172, 1169,
    1166, 1162, 1159, 1156, 1153, 1150, 1147, 1144, 1141, 1137, 1134, 1131, 1128, 1125, 1122, 1119,
    1116, 1113, 1110, 1107, 1104, 1101, 1098, 1095, 1092, 1089, 1086, 1083, 1080, 1078, 1075, 1072,
    1069, 1066, 1063, 1060, 1057, 1054, 1052, 1049, 1046, 1043, 1040, 1037, 1035, 1032, 1029, 1026,
};

// D5 attenuation: 6 dB octaves as shifts + 256-entry power-table mantissa.
// Every 64 index steps = exactly 6 dB; intermediate steps map to the table.
inline constexpr int32_t VolFactor(int32_t sample, uint32_t index)
{
    if (index >= static_cast<uint32_t>(kMaxAttIndex))
        return 0; // hardware clips to silence below -60 dB
    const uint32_t shift = index >> 6;
    const uint32_t step = (index & 0x3F) << 2; // 64 steps -> 256-entry table
    return (sample * kPowerTable[step]) >> (11 + shift);
}

// ---------------------------------------------------------------------------
// Block mix (D9): verified 11-bit Yamaha silicon table. out = x * t / 2042.
// Reset states: 0xF8 = 0x1B (FM L/R both -9 dB), 0xF9 = 0x00 (PCM 0 dB).
// ---------------------------------------------------------------------------
inline constexpr std::array<int32_t, 8> kMixScale = {
    2042, // code 0:  0 dB, unity
    1444, // code 1: -3 dB, 1444/2042 = 0.70715
    1021, // code 2: -6 dB
    722,  // code 3: -9 dB
    510,  // code 4: -12 dB
    361,  // code 5: -15 dB
    255,  // code 6: -18 dB
    0     // code 7: -inf dB, mute
};

inline constexpr int32_t ApplyMixScale(int32_t sample, uint32_t code)
{
    return (sample * kMixScale[code & 7]) / 2042;
}

// ---------------------------------------------------------------------------
// Pan (D8): 16-entry table in 3 dB units (one 3 dB step = 32 index units).
// 0 = centre; 1..7 = right positions (left side attenuated, 7 = hard right
// with left off); 8 = both off; 9..15 = left positions (9 = hard left with
// right off). Attenuations are 10-bit index domain; 1020 maps to silence.
// ---------------------------------------------------------------------------
struct PanPair
{
    uint16_t left;
    uint16_t right;
};

inline constexpr std::array<PanPair, 16> kPanTable = {{
    {0, 0},       // 0: centre
    {32, 0},      // 1: right, left -3 dB
    {64, 0},      // 2: right, left -6 dB
    {96, 0},      // 3: right, left -9 dB
    {128, 0},     // 4: right, left -12 dB
    {160, 0},     // 5: right, left -15 dB
    {192, 0},     // 6: right, left -18 dB
    {1020, 0},    // 7: hard right, left off
    {1020, 1020}, // 8: both off
    {0, 1020},    // 9: hard left, right off
    {0, 192},     // 10: left, right -18 dB
    {0, 160},     // 11: left, right -15 dB
    {0, 128},     // 12: left, right -12 dB
    {0, 96},      // 13: left, right -9 dB
    {0, 64},      // 14: left, right -6 dB
    {0, 32},      // 15: left, right -3 dB
}};

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
