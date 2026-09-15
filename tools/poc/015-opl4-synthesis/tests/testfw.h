// libopl4 test framework — shared across all test translation units.
//
// Tiny CHECK macros with exact-value reporting, the TestChip fixture
// (hostTickRate == kMasterClockHz: 1 host tick == 1 master clock, so every
// timing assertion is exact) and the common register-script helpers.
// The counters live in the TU that hosts main() (opl4tests.cpp).
#ifndef OPL4_TESTFW_H
#define OPL4_TESTFW_H

#include "opl4/opl4.h"
#include "opl4/wavememory.h"
#include "opl4fm.h"
#include "opl4pcm.h"
#include "opl4render.h"
#include "opl4tables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace opl4;

namespace opl4test
{

extern int gChecks;
extern int gFailed;

inline void Fail(const char* what, const char* file, int line)
{
    std::printf("  FAIL %s:%d: %s\n", file, line, what);
    gFailed++;
}

} // namespace opl4test

#define CHECK(cond)                                                        \
    do                                                                     \
    {                                                                      \
        opl4test::gChecks++;                                               \
        if (!(cond))                                                       \
            opl4test::Fail(#cond, __FILE__, __LINE__);                     \
    } while (0)

#define CHECK_EQ_I(a, b)                                                   \
    do                                                                     \
    {                                                                      \
        opl4test::gChecks++;                                               \
        const long long va_ = static_cast<long long>(a);                   \
        const long long vb_ = static_cast<long long>(b);                   \
        if (va_ != vb_)                                                    \
        {                                                                  \
            std::printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n",         \
                        __FILE__, __LINE__, #a, #b, va_, vb_);             \
            opl4test::gFailed++;                                           \
        }                                                                  \
    } while (0)

#define CHECK_EQ_F(a, b)                                                   \
    do                                                                     \
    {                                                                      \
        opl4test::gChecks++;                                               \
        const double va_ = static_cast<double>(a);                         \
        const double vb_ = static_cast<double>(b);                         \
        if (std::fabs(va_ - vb_) > 1e-15)                                  \
        {                                                                  \
            std::printf("  FAIL %s:%d: %s == %s (%.17g != %.17g)\n",       \
                        __FILE__, __LINE__, #a, #b, va_, vb_);             \
            opl4test::gFailed++;                                           \
        }                                                                  \
    } while (0)

namespace opl4test
{

// Render layer normalization: int32 streams use full 16-bit scale (unity ≈ 32767)
// with kRailShift=2 bits of headroom. Float output is normalized by 1/(32768<<2).
// Tests use these constants to convert between old int16-scale expectations and
// the normalized float output.
constexpr int kRailShift = 2;
constexpr float kNormScale = 1.0f / static_cast<float>(32768 << kRailShift);
constexpr float kDenormScale = static_cast<float>(32768 << kRailShift);

// hostTickRate == kMasterClockHz: 1 host tick == 1 master clock.
struct TestChip
{
    WaveMemory mem;
    Opl4 chip;
    Opl4Config cfg;

    explicit TestChip(uint32_t romSize = 2u << 20, uint32_t ramSize = 1u << 20,
                      uint32_t outputRate = 44100)
    {
        cfg.hostTickRate = kMasterClockHz;
        cfg.outputRate = outputRate;
        mem.Configure(romSize, ramSize);
        chip.Configure(cfg, &mem);
        chip.Reset(0);
    }
};

constexpr uint32_t kHdrBase = 0x200000;  // tone headers live in SRAM
constexpr uint32_t kSmpBase = 0x200100;  // sample data
constexpr uint64_t kOutClocks = kOutDivider; // 768 master clocks per output step

// 16-bit tone header with 5 register-rewrite bytes (banks 5..9, D7).
// `end` is the loop end offset from the sample start (sample length);
// the header stores its complement (0x10000 - end), as the chip does.
inline void WriteToneHeader(WaveMemory& mem, uint32_t hdr, uint8_t bits,
                            uint32_t start, uint16_t loop, uint16_t end,
                            const uint8_t* banks579)
{
    uint8_t b[12] = {};
    b[0] = static_cast<uint8_t>((bits << 6) | ((start >> 16) & 0x3F));
    b[1] = static_cast<uint8_t>((start >> 8) & 0xFF);
    b[2] = static_cast<uint8_t>(start & 0xFF);
    b[3] = static_cast<uint8_t>(loop >> 8);
    b[4] = static_cast<uint8_t>(loop & 0xFF);
    const uint16_t endC = static_cast<uint16_t>(0x10000u - end);
    b[5] = static_cast<uint8_t>(endC >> 8);
    b[6] = static_cast<uint8_t>(endC & 0xFF);
    for (int i = 0; i < 5; i++)
        b[7 + i] = banks579 != nullptr ? banks579[i] : 0;
    mem.WriteSram(hdr, b, 12);
    mem.ClearDirty();
}

// Maximum-amplitude 16-bit sample loop (end 4, loop 0).
inline void WriteMaxSampleData(WaveMemory& mem, uint16_t value = 0x7FFF)
{
    uint8_t data[16];
    for (int i = 0; i < 8; i++)
    {
        data[i * 2 + 0] = static_cast<uint8_t>(value >> 8);
        data[i * 2 + 1] = static_cast<uint8_t>(value & 0xFF);
    }
    mem.WriteSram(kSmpBase, data, 16);
    mem.ClearDirty();
}

// Key PCM slot `slot` onto wave 384 (headerBase 4 -> header at 0x200000)
// at full level: instant attack, TL 0, 16-bit max sample loop.
// The wave-low write triggers the tone fetch which rewrites banks 5..9
// from header bytes 7..11 (D7) — envelope registers must come AFTER it.
inline void KeyOnPcmSlot(Opl4& c, int slot, uint64_t t, uint8_t pan = 0)
{
    c.WriteWave(t, 0x02, 0x10); // wave-table header base 4, MA 0
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot + 24 * 1), 0xFF); // wave bit9 + fn lo7
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot), 0x80); // wave lo: 384 -> fetch
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot + 24 * 2), 0x07); // oct0 fn=1023
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot + 24 * 6), 0xF0); // AR15 D1R0
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot + 24 * 7), 0x00); // DL0 D2R0
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot + 24 * 8), 0x00); // RC0 RR0
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot + 24 * 3), 0x01); // TL 0, LD 1
    c.WriteWave(t, static_cast<uint8_t>(0x08 + slot + 24 * 4),
                static_cast<uint8_t>(0x80 | pan)); // key on
}

// FM channel 0 key-on (block 4, fn 771 ~ A4) — steady activity. The
// carrier register addresses are backend-specific: the in-tree engine maps
// the operator families linearly (carrier = 0x21/0x41/0x61/0x81, routing
// 0 = both sides) while YMF262/ymfm use the classic layout (ch0 carrier =
// 0x23/0x43/0x63/0x83, routing CHA|CHB = both sides).
inline void KeyOnFmCh0(Opl4& c, uint64_t t)
{
    c.WriteFm(t, 0, 0x20, 0x01); // modulator: mult 1 (both maps)
#if defined(OPL4_FM_YMFM)
    c.WriteFm(t, 0, 0x23, 0x01); // classic carrier: mult 1
    c.WriteFm(t, 0, 0x40, 0x00); // mod TL 0
    c.WriteFm(t, 0, 0x43, 0x00); // car TL 0
    c.WriteFm(t, 0, 0x60, 0xF0); // mod AR15 DR0
    c.WriteFm(t, 0, 0x63, 0xF0); // car AR15 DR0
    c.WriteFm(t, 0, 0x80, 0x00); // mod SL0 RR0
    c.WriteFm(t, 0, 0x83, 0x00); // car SL0 RR0
#else
    c.WriteFm(t, 0, 0x21, 0x01); // op1 mult 1
    c.WriteFm(t, 0, 0x40, 0x00); // op0 TL 0
    c.WriteFm(t, 0, 0x41, 0x00); // op1 TL 0
    c.WriteFm(t, 0, 0x60, 0xF0); // op0 AR15 DR0
    c.WriteFm(t, 0, 0x61, 0xF0); // op1 AR15 DR0
    c.WriteFm(t, 0, 0x80, 0x00); // op0 SL0 RR0
    c.WriteFm(t, 0, 0x81, 0x00); // op1 SL0 RR0
#endif
    c.WriteFm(t, 0, 0xA0, 0x03); // fn lo
#if defined(OPL4_FM_YMFM)
    c.WriteFm(t, 0, 0xC0, 0x30); // CHA+CHB: both sides (bits include)
#else
    c.WriteFm(t, 0, 0xC0, 0x00); // fb 0, both outputs (bits exclude)
#endif
    c.WriteFm(t, 0, 0xB0, 0x33); // fn bits 9:8 = 3 (fn 0x303), block 4, key on
}

// Aggregate runners defined in the other test TUs; called from main().
int RunVectorTests();
void RunFmBackendCompareTests(); // opl4fmcompare.cpp (in-tree vs ymfm OPL3)
int RunSweepTests();             // opl4sweep.cpp (§12.2.1 conformance sweeps)

} // namespace opl4test

#endif // OPL4_TESTFW_H
