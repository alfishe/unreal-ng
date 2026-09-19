// libopl4 — FM bus layer implementation (rearchitecture §4.1).
#include "fm/fmbus.h"

#include <cstring>

namespace opl4
{

void FmBus::Reset()
{
    _regs.fill(0);
    _route.fill(0);
    _status = 0;
    _timer1 = _timer2 = 0;
    _timer1Load = _timer2Load = 0;
    _timer1Enable = _timer2Enable = false;
    _timer1Mask = _timer2Mask = false;
    _newMode = false;
    _new2 = false;
    // _muteMask (render-side) and the engine survive a bus reset; the owner
    // resets the engine alongside.
}

uint8_t FmBus::Read(uint8_t bank, uint8_t reg) const
{
    // Same aliasing rule as Write: one bank until NEW arms, except 0x105.
    if (bank == 1 && !_newMode && reg != 0x05)
        bank = 0;
    return _regs[static_cast<uint16_t>(reg) + (bank ? 256 : 0)];
}

void FmBus::Write(uint8_t bank, uint8_t reg, uint8_t data)
{
    // Bank-1 -> bank-0 aliasing until NEW (0x105), except 0x105 itself
    // (ymfm write_address_hi masking, openMSX-audited).
    if (bank == 1 && !_newMode && reg != 0x05)
        bank = 0;

    const uint16_t r = static_cast<uint16_t>(reg) + (bank ? 256 : 0);
    _regs[r] = data;

    if (bank == 0)
    {
        switch (reg)
        {
        case 0x02:
            _timer1Load = data;
            break;
        case 0x03:
            _timer2Load = data;
            break;
        case 0x04:
            // F8 (openMSX YMF278B::writeIO): RST (bit 7) is exclusive —
            // the same write's enable/mask bits are ignored, flags clear
            // ONLY, and the timer counters are never zeroed.
            if (data & 0x80)
                _status &= ~(kStatusT1 | kStatusT2);
            else
            {
                _timer1Enable = (data & 0x01) != 0;
                _timer2Enable = (data & 0x02) != 0;
                _timer1Mask = (data & 0x40) != 0;
                _timer2Mask = (data & 0x20) != 0;
            }
            break;
        default:
            break;
        }
    }
    else if (reg == 0x05) // 0x105: NEW (bit 0) / NEW2 (bit 1)
    {
        _newMode = (data & 0x01) != 0;
        _new2 = (data & 0x02) != 0;
    }

    // Routing shadow: 0xC0 upper nibble (include enables), both banks.
    if (reg >= 0xC0 && reg <= 0xC8)
        _route[(bank ? 9 : 0) + (reg - 0xC0)] = static_cast<uint8_t>(data & 0xF0);

    // Every write reaches the engine de-aliased (Nuked contract): timers,
    // 0x104/0x105, 0xBD included — the backend keeps whatever internal
    // shadow its synthesis paths read.
    if (_synth != nullptr)
        _synth->WriteReg(r, data);
}

void FmBus::Advance(std::array<int32_t, kChannelCount>& taps, int32_t& outL, int32_t& outR)
{
    FmOutput out;
    if (_synth != nullptr)
        _synth->Advance(out);
    AdvanceTimers();

    if (_synth != nullptr && _synth->Caps().perChannelTaps)
    {
        outL = outR = 0;
        for (int ch = 0; ch < kChannelCount; ch++)
        {
            taps[ch] = out.channel[ch]; // raw: meters see muted channels too
            if ((_muteMask >> ch) & 1u)
                continue;
            const int32_t v = out.channel[ch];
            // Output routing (YMF262 silicon, include semantics): with NEW
            // set, C0 bits 4..7 enable CHA/CHB/CHC/CHD; the card sums pair
            // A/C into L and pair B/D into R; all four clear = channel
            // silent. Without NEW both sides carry the channel (OPL2
            // compatibility).
            const uint8_t route = _newMode ? _route[ch] : 0x30;
            if (route & 0x10)
                outL += v; // CHA -> L
            if (route & 0x20)
                outR += v; // CHB -> R
            if (route & 0x40)
                outL += v; // CHC -> L (second stereo pair)
            if (route & 0x80)
                outR += v; // CHD -> R
        }
    }
    else
    {
        // Pre-mixed backend (ymfm): taps unmetered, per-channel mute
        // inapplicable — the pair passes through unchanged.
        taps.fill(0);
        outL = out.mixL;
        outR = out.mixR;
    }
}

void FmBus::AdvanceTimers()
{
    // OPL-family timers count FM clock/256 steps: T1 every 256 decrements,
    // T2 every 256*4. On the 49516.4 Hz grid this preserves ratios.
    if (_timer1Enable)
    {
        if (++_timer1 >= (0x100 - _timer1Load) * 4)
        {
            _timer1 = 0;
            if (!_timer1Mask)
                _status |= kStatusT1;
        }
    }
    if (_timer2Enable)
    {
        if (++_timer2 >= (0x100 - _timer2Load) * 16)
        {
            _timer2 = 0;
            if (!_timer2Mask)
                _status |= kStatusT2;
        }
    }
}

void FmBus::SetChannelMute(int ch, bool mute)
{
    if (ch < 0 || ch >= kChannelCount)
        return;
    if (mute)
        _muteMask |= (1u << ch);
    else
        _muteMask &= ~(1u << ch);
}

void FmBus::SaveState(uint8_t* dst) const
{
    // Zero the whole image first: [520..527] is reserved and deliberately
    // unwritten below. Without this, destination garbage leaks into the TTD
    // hash — a fresh capture and a restore of it would hash differently
    // (the exact failure the TTD capture/restore self-test catches).
    std::memset(dst, 0, kStateSize);
    std::memcpy(dst, _regs.data(), _regs.size()); // [0..511] shadow
    std::memcpy(dst + 512, &_timer1, 2);          // [512..515] counters
    std::memcpy(dst + 514, &_timer2, 2);
    std::memcpy(dst + 516, &_timer1Load, 2);      // [516..519] loads
    std::memcpy(dst + 518, &_timer2Load, 2);
    dst[528] = _status;
    uint8_t flags = 0;
    flags |= _timer1Enable ? 1 : 0;
    flags |= _timer2Enable ? 2 : 0;
    flags |= _timer1Mask ? 4 : 0;
    flags |= _timer2Mask ? 8 : 0;
    flags |= _newMode ? 16 : 0;
    flags |= _new2 ? 32 : 0;
    dst[529] = flags;
}

void FmBus::LoadState(const uint8_t* src)
{
    std::memcpy(_regs.data(), src, _regs.size());
    std::memcpy(&_timer1, src + 512, 2);
    std::memcpy(&_timer2, src + 514, 2);
    std::memcpy(&_timer1Load, src + 516, 2);
    std::memcpy(&_timer2Load, src + 518, 2);
    _status = src[528];
    const uint8_t flags = src[529];
    _timer1Enable = (flags & 1) != 0;
    _timer2Enable = (flags & 2) != 0;
    _timer1Mask = (flags & 4) != 0;
    _timer2Mask = (flags & 8) != 0;
    _newMode = (flags & 16) != 0;
    _new2 = (flags & 32) != 0;
    // Routes are a pure function of the 0xC0 shadow bytes — re-derived.
    for (int ch = 0; ch < kChannelCount; ch++)
        _route[ch] = _regs[(ch >= 9 ? 256 : 0) + 0xC0 + (ch % 9)] & 0xF0;
}

} // namespace opl4
