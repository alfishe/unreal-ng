// libopl4 — FM bus layer (rearchitecture §4.1): everything guest-visible
// above the synthesis engine.
//
// Owns: the 512-byte register shadow, bank-1 → bank-0 aliasing before NEW
// (0x105), NEW/NEW2 decode (the NEW2 wave-port gate), timers T1/T2 with
// their status bits, the status byte, per-channel routing decode from the
// 0xC0 shadow (include semantics, CHA/CHB/CHC/CHD quad) and the channel
// mute state acting on the tap-sum path (R7: mute is never serialised).
//
// The engine below (IFmSynth) is synthesis-only: it receives de-aliased
// writes and emits per-channel taps (or a pre-mixed pair when it cannot).
// Nothing guest-visible depends on which engine is plugged in.
#pragma once

#include "opl4/ifmsynth.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace opl4
{

class FmBus
{
public:
    static constexpr int kChannelCount = 18;
    static constexpr uint8_t kStatusBusy = 0x01;
    static constexpr uint8_t kStatusT1 = 0x40;
    static constexpr uint8_t kStatusT2 = 0x20;

    // [0..511] register shadow, [512..515] timer counters, [516..519] timer
    // loads, [520..527] reserved (saved as zero), [528] status, [529] flags.
    // Routes are re-derived from the shadow on load (pure function of the
    // 0xC0 bytes).
    static constexpr size_t kStateSize = 530;

    // The engine is not owned; the owner resets it alongside the bus.
    void SetSynth(IFmSynth* synth) { _synth = synth; }

    void Reset(); // bus state only (register shadow, timers, status, NEW)
    void Write(uint8_t bank, uint8_t reg, uint8_t data);

    // One 684-clock FM step: engine Advance + timer tick + routing + mute.
    // taps always receive the raw per-channel values (peaks read them);
    // outL/outR carry the routed, mute-applied mix when the engine provides
    // taps, otherwise the engine's pre-mixed pair through unchanged.
    void Advance(std::array<int32_t, kChannelCount>& taps, int32_t& outL, int32_t& outR);

    uint8_t Status() const { return _status; }
    uint8_t ReadStatus() const { return static_cast<uint8_t>(_status | 0x80); } // bit 7 reads as 1
    bool NewMode() const { return _newMode; }
    bool New2() const { return _new2; }
    // Effective include route for a channel: with NEW set, the 0xC0 upper
    // nibble; without it, both stereo sides (OPL2 compatibility).
    uint8_t Route(int ch) const { return _newMode ? _route[ch] : 0x30; }
    const std::array<uint8_t, 512>& Regs() const { return _regs; }

    void SetChannelMute(int ch, bool mute);

    // POD pair (TTD §9); mute is deliberately not saved (R7).
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);

private:
    void AdvanceTimers();

    std::array<uint8_t, 512> _regs{};
    std::array<uint8_t, kChannelCount> _route{}; // 0xC0 upper-nibble shadow
    uint8_t _status = 0;
    uint16_t _timer1 = 0, _timer2 = 0;
    uint16_t _timer1Load = 0, _timer2Load = 0;
    bool _timer1Enable = false, _timer2Enable = false;
    bool _timer1Mask = false, _timer2Mask = false;
    bool _newMode = false; // 0x105 NEW (bit 0, OPL3 mode) — bus authority
    bool _new2 = false;    // 0x105 NEW2 (bit 1, OPL4 wave gate) — bus authority
    uint32_t _muteMask = 0; // bit ch (render-side only, never serialised)
    IFmSynth* _synth = nullptr;
};

} // namespace opl4
