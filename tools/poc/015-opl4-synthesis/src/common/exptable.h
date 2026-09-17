// libopl4 — shared exponent/attenuation math (FM and PCM output paths).
//
// Provenance (core TDD §13.1): s_power_table is the Yamaha die-derived
// 256-entry logarithmic power table round(2047 * 2^(-i/256)) — the D5
// ground truth. The early openMSX 0.75 linear guess is explicitly rejected
// (+0.51 dB/stage).
//
// kExpClipIndex is the curve's own clip point, deliberately NOT named
// kMaxAttIndex: each engine owns its attenuation ceiling as a domain
// constant (pcm/pcmtables.h, fm/fmtables.h). Today both equal 0x280; the
// FM re-derivation re-domains the FM ceiling to the OPL3 96 dB clip, and
// only the engine constants move — this curve does not.
#pragma once

#include <array>
#include <cstdint>

namespace opl4
{

// Exp2 curve clip: the last index the mantissa table resolves before the
// octave shift alone drives the product to zero. 0x280 in the 3/32 dB
// index domain (-60.0 dB). VolFactor's silence floor.
constexpr int kExpClipIndex = 0x280;

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
    if (index >= static_cast<uint32_t>(kExpClipIndex))
        return 0; // hardware clips to silence below -60 dB
    const uint32_t shift = index >> 6;
    const uint32_t step = (index & 0x3F) << 2; // 64 steps -> 256-entry table
    return (sample * kPowerTable[step]) >> (11 + shift);
}

} // namespace opl4
