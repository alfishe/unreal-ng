// libopl4 — shared block-mix and pan tables (chip output stage, D8/D9).
//
// Provenance (core TDD §13.1): kMixScale is the verified 11-bit Yamaha
// silicon block-mix table (ymfm s_mix_scale), 2042 = 0 dB unity; pan
// positions and dl_tab lineage: openMSX YMF278.cc — the most accurate
// published PCM model. Shared by the FM and PCM halves of the chip (both
// route through 0xF8/0xF9 block mix and the D8 pan domain).
#pragma once

#include <array>
#include <cstdint>

namespace opl4
{

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

} // namespace opl4
