// libopl4 — FM engine (core TDD §4): YMF262-class core on the 49516.4 Hz
// grid (33868800/684). 18 channels, 36 operators, 8 waveforms, 4-op pairing,
// rhythm mode, timers 1/2, status semantics. Differences from YMF262 per
// §4.2: grid rate, output through the rate reducer, block-mix reset −9 dB.
//
// Compact integer model: 19-bit phase accumulator (top 10 bits index a sine
// table), envelope in the shared 10-bit 0.09375 dB attenuation domain with
// the same rate machinery as the PCM engine. All state integer (§9.2 item 4).
#pragma once

#include "opl4tables.h"

#include <array>
#include <cstdint>

namespace opl4
{

struct FmOperator
{
    // Register-derived
    uint16_t fnum = 0;
    uint8_t block = 0;
    uint8_t mult = 0;      // 0 => x0.5, 1 => x1 ... 15 => x15
    uint8_t tl = 0;        // 6-bit total level (0.75 dB/step)
    uint8_t ar = 0, dr = 0, sl = 0, rr = 0;
    uint8_t ws = 0;        // waveform 0..7
    bool ksr = false;      // key scale rate enable
    bool am = false;       // LFO AM enable
    bool vib = false;      // LFO PM enable
    bool egt = false;      // envelope type (sustaining)
    bool ksl = false;      // key scale level enable

    // Live state
    uint32_t phase = 0;    // 19-bit accumulator
    int16_t envVol = kMaxAttIndex;
    uint8_t egState = 0;   // 0 off, 1 rel, 2 sus/dec, 3 attack
    bool keyOn = false;
    int32_t out = 0;       // last output (feedback delay, §4.1)
};

struct FmChannel
{
    uint8_t op1 = 0, op2 = 0;      // operator indices
    bool fourOp = false;           // participating in a 4-op connection
    uint8_t conn = 0;              // algorithm when 4-op group master
    int32_t fbShift = 0;           // feedback amount (0 => none)
    uint8_t cha = 0, chb = 0;      // output routing (0 = L, 1 = R... toms)
};

enum FmEgState : uint8_t
{
    kFmEgOff = 0,
    kFmEgRel = 1,
    kFmEgDec = 2,
    kFmEgSus = 3,
    kFmEgAtt = 4,
};

class Opl4Fm
{
public:
    static constexpr int kChannelCount = 18;
    static constexpr int kOperatorCount = 36;

    void Reset();

    // Bank 0/1 register write. 0x105 NEW triggers channel re-pairing.
    void WriteReg(uint8_t bank, uint8_t reg, uint8_t data);
    uint8_t ReadStatus() const;

    // Advance one 49516.4 Hz step; emit 16-bit L/R into outL/outR and
    // per-channel mono taps (18).
    void Advance(int32_t& outL, int32_t& outR,
                 std::array<int32_t, kChannelCount>& channelTaps);

    // Timers (guest-visible): call once per FM step.
    void AdvanceTimers();

    // POD save/load, side-effect free save.
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    static constexpr size_t kStateSize =
        2 * 256 /*regs*/ + kOperatorCount * sizeof(FmOperator)
        + kChannelCount * sizeof(FmChannel) + 72 /*fbHist*/ + 128 /*globals*/;

    uint8_t Status() const { return _status; }
    const std::array<FmOperator, kOperatorCount>& Operators() const { return _ops; }
    const std::array<FmChannel, kChannelCount>& Channels() const { return _ch; }
    const std::array<uint8_t, 512>& Regs() const { return _regs; }

    static constexpr uint8_t kStatusBusy = 0x01;
    static constexpr uint8_t kStatusT1 = 0x40;
    static constexpr uint8_t kStatusT2 = 0x20;

private:
    void UpdateChannelParams(uint8_t ch);
    void KeyOn(FmOperator& op, bool on);
    uint32_t PhaseStep(const FmOperator& op); // reads LFO/regs for vibrato
    static uint32_t KslIndex(const FmOperator& op);
    uint8_t EgRate(const FmOperator& op, uint8_t regRate) const;
    int32_t WaveSample(const FmOperator& op, uint16_t ph) const;
    int32_t OperatorOutput(FmOperator& op, int32_t modInput) const;
    void AdvanceEnvelope(FmOperator& op);
    void RebuildConnections();

    std::array<uint8_t, 512> _regs{};
    std::array<FmOperator, kOperatorCount> _ops{};
    std::array<FmChannel, kChannelCount> _ch{};
    std::array<int32_t, kChannelCount> _fbHist{}; // feedback delay taps

    uint8_t _status = 0;
    uint16_t _timer1 = 0, _timer2 = 0;
    uint16_t _timer1Load = 0, _timer2Load = 0;
    bool _timer1Enable = false, _timer2Enable = false;
    bool _timer1Mask = false, _timer2Mask = false;
    bool _rhythm = false;
    bool _newMode = false; // 0x105 NEW
    uint64_t _egCnt = 0;
    uint32_t _lfoPm = 0;   // PM counter (14-bit OPL3-style)
    uint32_t _lfoAm = 0;   // AM counter (13-bit)
    uint32_t _noise = 1;   // 23-bit noise LFSR for rhythm
};

} // namespace opl4
