// libopl4 — PCM engine (core TDD §5): 24 wavetable slots on the 44100 Hz
// output grid. Envelope rate machinery, TL interpolation (D6), tone header
// fetch side effects (D7), loop overrun (§5.4), MA-mode memory arbitration.
#pragma once

#include "opl4/iwavememory.h"
#include "opl4tables.h"

#include <array>
#include <cstdint>

namespace opl4
{

enum class EgPhase : uint8_t
{
    Off = 0,
    Rel = 1,
    Sus = 2,
    Dec = 3,
    Att = 4,
};

struct PcmSlot
{
    // Register-derived parameters
    uint16_t wave = 0;
    int8_t oct = 0;
    uint16_t fn = 0;
    uint8_t tlDest = 0;  // destination TL level, 8-bit internal (0x7F -> 0xFF)
    uint8_t tl = 0;      // current TL, possibly mid-interpolation
    uint8_t pan = 0;
    uint8_t lfo = 0;     // LFO speed 0..7
    uint8_t vib = 0;     // vibrato depth 0..7
    uint8_t ar = 0;
    uint8_t d1r = 0;
    uint16_t dl = 0;   // decay level as attenuation index (kDecayLevelTable)
    uint8_t d2r = 0;
    uint8_t rc = 0;
    uint8_t rr = 0;
    uint8_t am = 0;      // AM depth 0..7
    bool prvb = false;
    bool damp = false;
    bool keyon = false;
    bool lfoActive = false;

    // Decoded tone header (D7)
    uint8_t bits = 0;            // 0=8-bit, 1=12-bit, 2=16-bit
    uint32_t startAddr = 0;      // 22-bit
    uint16_t loopAddr = 0;
    uint16_t endAddr = 0;

    // Playback state
    uint16_t pos = 0;
    uint32_t stepPtr = 0;        // 16-bit fraction in low bits
    uint32_t step = 0;           // cached CalcStep
    int16_t envVol = kMaxAttIndex;
    EgPhase state = EgPhase::Off;
    uint32_t lfoCnt = 0;
};

class Opl4Pcm
{
public:
    static constexpr int kSlotCount = 24;

    void Reset();
    void SetMemory(IWaveMemory* mem) { _mem = mem; }

    // Register write. Returns true when the write triggered a tone header
    // fetch (bank 0), so the caller asserts the LD deadline (§3.3).
    bool WriteReg(uint8_t reg, uint8_t data);
    uint8_t PeekReg(uint8_t reg) const;
    // Read with side effects: register 6 auto-increments memAdr when MA=1.
    uint8_t ReadReg(uint8_t reg);

    // Advance one 44100 Hz step; produce this slot's contribution to the
    // PCM L/R sums and per-slot mono taps (post-envelope, post-TL, pre-pan).
    void Advance(int32_t& sumLeft, int32_t& sumRight,
                 std::array<int32_t, kSlotCount>& monoTaps);

    bool AnyActive() const;

    // Whole-engine POD save/load (side-effect free save, §9.2).
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    static constexpr size_t kStateSize =
        256 /*regs*/ + kSlotCount * sizeof(PcmSlot) /*slots*/ + 16 /*memAdr+egCnt+rsvd*/;

    std::array<PcmSlot, kSlotCount>& Slots() { return _slots; }
    const std::array<uint8_t, 256>& Regs() const { return _regs; }
    uint32_t MemAdr() const { return _memAdr; }

    // Header fetch support: LD busy duration in master clocks (§3.3).
    static constexpr uint64_t ToneLoadClocks() { return 9600; }

private:
    uint8_t ComputeRate(const PcmSlot& s, int val) const;
    uint8_t ComputeDecayRate(const PcmSlot& s, int val) const;
    int16_t ComputeVib(const PcmSlot& s) const;
    uint16_t ComputeAm(const PcmSlot& s) const;
    void KeyOnHelper(PcmSlot& s);
    int16_t GetSample(const PcmSlot& s, uint16_t pos) const;
    static uint16_t NextPos(const PcmSlot& s, uint16_t pos, uint16_t increment);
    void WriteRegDirect(uint8_t reg, uint8_t data);

    std::array<PcmSlot, kSlotCount> _slots{};
    std::array<uint8_t, 256> _regs{};
    uint32_t _memAdr = 0;
    uint64_t _egCnt = 0;
    IWaveMemory* _mem = nullptr;
};

} // namespace opl4
