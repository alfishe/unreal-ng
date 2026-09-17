// libopl4 — FM engine (core TDD §4): YMF262-class core on the 49516.4 Hz
// grid (33868800/684). 18 channels, 36 operators, 8 waveforms, 4-op pairing,
// rhythm mode. Differences from YMF262 per §4.2: grid rate, output through
// the rate reducer, block-mix reset −9 dB.
//
// Synthesis-only (rearchitecture §4.3): the FmBus above this engine owns
// every guest-visible bit — register-shadow authority, bank aliasing, NEW/
// NEW2, timers, status, routing, mute. This class receives de-aliased
// register writes (0x000..0x1FF) and emits per-channel taps.
//
// Compact integer model: 19-bit phase accumulator (top 10 bits index a sine
// table), envelope in the shared 10-bit 0.09375 dB attenuation domain with
// the same rate machinery as the PCM engine. All state integer (§9.2 item 4).
#pragma once

#include "opl4/ifmsynth.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace opl4
{

struct FmOperator
{
    // Register-derived
    uint16_t fnum = 0;
    uint8_t block = 0;
    uint8_t mult = 0;      // 0 => x0.5; xN via the YMF262 table (11=>x10, 13=>x12, 14=>x15)
    uint8_t tl = 0;        // 6-bit total level (0.75 dB/step)
    uint8_t ar = 0, dr = 0, sl = 0, rr = 0;
    uint8_t ws = 0;        // waveform 0..7
    bool ksr = false;      // key scale rate enable
    bool am = false;       // LFO AM enable
    bool vib = false;      // LFO PM enable
    bool egt = false;      // envelope type (sustaining)
    uint8_t ksl = 0;       // key scale level shift 0..3 (reg bits 7:6, ymfm bit-swapped)

    // Live state
    uint32_t phase = 0;    // 19-bit accumulator
    int16_t envVol = 0x280; // kFmMaxAttIndex (fm/fmtables.h); literal keeps this header table-free
    uint8_t egState = 0;   // 0 off, 1 rel, 2 sus/dec, 3 attack
    bool keyOn = false;
    int32_t out = 0;       // last output (feedback delay, §4.1)
};

struct FmChannel
{
    uint8_t op1 = 0, op2 = 0; // operator indices (classic YMF262 slot map)
    bool fourOp = false;      // participating in a 4-op connection
    uint8_t conn = 0;         // C0 bit 0 (CNT): additive in 2-op, algorithm bit in 4-op
    int32_t fbShift = 0;      // feedback amount (0 => none)
    // Routing (C0 bits 7:4) is bus-owned — see FmBus::Route.
};

enum FmEgState : uint8_t
{
    kFmEgOff = 0,
    kFmEgRel = 1,
    kFmEgDec = 2,
    kFmEgSus = 3,
    kFmEgAtt = 4,
};

class Opl4Fm final : public IFmSynth
{
public:
    static constexpr int kChannelCount = 18;
    // Register-slot view: 22 slots per bank × 2 banks, addressed exactly as
    // silicon addresses them (bank base 22 + reg - 0x20). The four gap slots
    // per bank (6/7 = regs 0x26/0x27, 14/15 = 0x2E/0x2F) hold no operator —
    // stored-but-unreferenced, mirroring the dead register addresses. 36 are
    // real operators; bank-1 rhythm slots 16..21 (BD/HH/TOM/SD/CY) live at
    // 38..43, beyond a packed 36-entry array — the slot map REQUIRES 44.
    static constexpr int kOperatorCount = 44;

    // Fixed layout (POD pair, TTD §9): the globals pad keeps room for the
    // fields Step 5 will re-derive (the two-tap feedback consumed 72 bytes
    // of it; the kStateSize total is unchanged).
    static constexpr size_t kStateSize =
        2 * 256 /*regs*/ + kOperatorCount * sizeof(FmOperator)
        + kChannelCount * sizeof(FmChannel) + 144 /*fbHist, two taps*/
        + 56 /*globals*/;

    void Reset() override;

    // De-aliased write (bank in bit 8, NEW/NEW2 gating already applied by
    // FmBus). The engine keeps its own shadow of the bytes synthesis reads:
    // 0xA0 partial-fnum pairs, 0xBD rhythm/LFO depth, 0x104 pairing, 0x105
    // NEW (the 4-op gate; the guest-visible flag itself is bus authority).
    void WriteReg(uint16_t reg, uint8_t data) override;

    // One 49516.4 Hz step: fills channel[18] with the raw per-channel
    // values; mixL/mixR stay zero (the bus routes and mutes).
    void Advance(FmOutput& out) override;

    FmCaps Caps() const override
    {
        return {true, true, "opl4"}; // per-channel taps; exact, cheap save
    }

    size_t StateSize() const override { return kStateSize; }
    void SaveState(uint8_t* dst) const override;
    void LoadState(const uint8_t* src) override;
    uint32_t LayoutTag() const override { return 0x354D464F; } // "OFM5"

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

    std::array<uint8_t, 512> _regs{}; // synthesis-side shadow (de-aliased)
    std::array<FmOperator, kOperatorCount> _ops{};
    std::array<FmChannel, kChannelCount> _ch{};
    std::array<std::array<int32_t, 2>, kChannelCount> _fbHist{}; // two-tap feedback delay (ymfm m_feedback)

    bool _rhythm = false;
    bool _newMode = false; // 0x105 NEW (bit 0): 4-op pairing gate
    uint64_t _egCnt = 0;
    uint32_t _lfoPm = 0;   // PM counter (14-bit OPL3-style)
    uint32_t _lfoAm = 0;   // AM counter (13-bit)
    uint32_t _noise = 1;   // 23-bit noise LFSR for rhythm
};

} // namespace opl4
