// cosimdrv.h — shared driver for the co-simulation harnesses: one libopl4
// chip wired to a WaveMemory SRAM image, plus the register scripts both
// harnesses (cosim-ymfm, cosim-oracle) drive chips with. Header-only and
// free of any ymfm dependency on purpose: the oracle builds without refs/.
//
// The SRAM layout is shared vocabulary between the harnesses:
//   0x200000  header table (headerBase 4, waves 384..511, 12 bytes each)
//   0x200100  16-bit sample table (8 entries)
//   0x200200  8-bit sample table (8 entries)
//   0x200300  12-bit sample table (8 entries)
//   0x200400  256-entry 16-bit table (wide traces)
//   0x201400  constant-envelope table (level measurements)
#pragma once

#include "opl4/opl4.h"
#include "opl4/opl4config.h"
#include "opl4/wavememory.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace opl4cosim
{

using namespace opl4;

constexpr uint32_t kHdr = 0x200000;    // header table (headerBase 4, wave 384)
constexpr uint32_t kSmp16 = 0x200100;  // 16-bit sample table
constexpr uint32_t kSmp8 = 0x200200;   // 8-bit sample table
constexpr uint32_t kSmp12 = 0x200300;  // 12-bit sample table
constexpr uint32_t kSmpBig = 0x200400; // 256-entry 16-bit table
constexpr uint32_t kSmpEnv = 0x201400; // constant envelope table

// libopl4 chip driver. run() feeds the ABSOLUTE master-clock timestamp the
// core expects (Opl4::Run is SyncTo-based), so harnesses never touch clocks.
class OurChip
{
public:
    OurChip()
    {
        _cfg.hostTickRate = kMasterClockHz;
        _cfg.outputRate = 44100;
        _mem.Configure(2u << 20, 1u << 20);
        _chip.Configure(_cfg, &_mem);
        _chip.Reset(0);
    }

    WaveMemory& Mem() { return _mem; }
    Opl4& Chip() { return _chip; }
    uint64_t Clock() const { return _clock; } // absolute master-clock ticks

    void Fm(int bank, uint8_t reg, uint8_t data) { _chip.WriteFm(0, bank, reg, data); }
    void Pcm(uint8_t reg, uint8_t data) { _chip.WriteWave(0, reg, data); }

    std::vector<int16_t> Run(int samples)
    {
        _clock += static_cast<uint64_t>(samples) * kOutDivider;
        _chip.Run(_clock);
        std::vector<int16_t> out;
        float buf[8192];
        size_t n;
        while ((n = _chip.Render(buf, 4096)) > 0)
            for (size_t i = 0; i < n * 2; i++)
                // Render emits normalized floats (full scale +-1.0); the
                // harness streams are int16, so de-normalize first. Plain
                // lrint(buf[i]) silently truncated every sample to 0 after
                // the render layer switched scales.
                out.push_back(static_cast<int16_t>(
                    std::lrint(buf[i] * 32768.0f)));
        return out; // interleaved L,R
    }

private:
    uint64_t _clock = 0;
    Opl4Config _cfg;
    WaveMemory _mem;
    Opl4 _chip;
};

// Header address for an SRAM wave number (384..511), headerBase 4.
constexpr uint32_t HdrOf(int wave)
{
    return kHdr + static_cast<uint32_t>(wave - 384) * 12;
}

void WriteHeader(WaveMemory& mem, uint32_t hdr, uint8_t bits, uint32_t start,
                 uint16_t loop, uint16_t end)
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
    mem.WriteSram(hdr, b, 12);
}

// One PCM voice on an SRAM wave (384..511): identical register bytes for
// libopl4 and ymfm. The wave-low write (0x08+s) triggers the tone fetch whose
// header bytes 7..11 rewrite banks 5..9, so envelope registers must come
// after it.
struct PcmVoice
{
    int slot;
    int wave;        // 384..511; header at HdrOf(wave)
    uint8_t octFnHi; // (oct << 4) | prvb | fn hi
    uint8_t arD1r, dlD2r, rcRr, tlLd, panKey;
};

template <typename Chip>
void KeyOnPcm(Chip& c, const PcmVoice& v)
{
    const uint8_t s = static_cast<uint8_t>(v.slot);
    c.Pcm(0x02, 0x10); // wave-table header base 4 -> headers at 0x200000
    c.Pcm(static_cast<uint8_t>(0x20 + s), 0x01); // wave bit 8, fn lo 0
    c.Pcm(static_cast<uint8_t>(0x08 + s), static_cast<uint8_t>(v.wave & 0xFF));
    c.Pcm(static_cast<uint8_t>(0x38 + s), v.octFnHi);
    c.Pcm(static_cast<uint8_t>(0x98 + s), v.arD1r);
    c.Pcm(static_cast<uint8_t>(0xB0 + s), v.dlD2r);
    c.Pcm(static_cast<uint8_t>(0xC8 + s), v.rcRr);
    c.Pcm(static_cast<uint8_t>(0x50 + s), v.tlLd);
    c.Pcm(static_cast<uint8_t>(0x68 + s), v.panKey); // key on
}

// ymfm ignores write_data_pcm until NEW2 (0x105 bit 1); libopl4 wants NEW
// (bit 0) for its bank-1 register file. 0x03 satisfies both.
template <typename Chip>
void BootNew(Chip& c)
{
    c.Fm(1, 0x05, 0x03);
}

// Classic OPL3 2-op voice on FM channel 0. Both engines share the
// canonical YMF262 operator map since the classic-map adoption
// (kStateVersion 3): ch0 = slots {0,3}, carrier at 0x23, and 0xC0 carries
// include-semantics output selects (0x30 = CHA+CHB, both sides).
struct FmVoice
{
    uint8_t mult = 0x01;
    uint8_t tlMod = 0x3F; // silent modulator: pure-sine carrier
    uint8_t tlCar = 0x10;
    uint8_t ardr = 0xF0; // AR 15, DR 0 (instant attack, hold)
    uint8_t slrr = 0x00;
    uint8_t fnLo = 0x03; // F-number 0x303, block 2 (B0 0x2B)
    uint8_t b0 = 0x2B;
};

template <typename Chip>
void KeyOnFm(Chip& c, const FmVoice& v, bool /*ours*/)
{
    c.Fm(0, 0x20, v.mult);
    c.Fm(0, 0x23, v.mult);
    c.Fm(0, 0x40, v.tlMod);
    c.Fm(0, 0x43, v.tlCar);
    c.Fm(0, 0x60, v.ardr);
    c.Fm(0, 0x63, v.ardr);
    c.Fm(0, 0x80, v.slrr);
    c.Fm(0, 0x83, v.slrr);
    c.Fm(0, 0xA0, v.fnLo);
    c.Fm(0, 0xB0, v.b0);
    c.Fm(0, 0xC0, 0x30);
}

} // namespace opl4cosim
