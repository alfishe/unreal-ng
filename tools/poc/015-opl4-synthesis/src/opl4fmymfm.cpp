// libopl4 — FM engine, ymfm verification backend (implementation).
#include "opl4fmymfm.h"

#include <algorithm>
#include <cstring>

namespace opl4
{

Opl4FmYmfm::Opl4FmYmfm()
    : _intf(), _engine(_intf)
{
    Reset();
}

void Opl4FmYmfm::Reset()
{
    // ymfm resets registers, operators, envelopes and LFO/noise state. The
    // address latch is ours to clear (ymf262::reset() leaves it, upstream
    // choice); the timer/status block mirrors Opl4Fm::Reset exactly.
    _engine.reset();
    _engine.ResetAddress();

    _status = 0;
    _timer1 = _timer2 = 0;
    _timer1Load = _timer2Load = 0;
    _timer1Enable = _timer2Enable = false;
    _timer1Mask = _timer2Mask = false;
    _newMode = false;
    _new2 = false;
}

void Opl4FmYmfm::WriteReg(uint8_t bank, uint8_t reg, uint8_t data)
{
    // Bank-1 aliasing until NEW (0x105), except 0x105 itself — identical to
    // Opl4Fm and to ymfm's own write_address_hi masking. Resolved here so
    // the latched address is already the effective one; 0x105 still reaches
    // bank 1 through write_address_hi.
    if (bank == 1 && !_newMode && reg != 0x05)
        bank = 0;

    if (bank != 0)
        _engine.write_address_hi(reg);
    else
        _engine.write_address(reg);
    _engine.write_data(data);

    // Audited bus-visible semantics in parallel with ymfm's copy of the
    // byte (which its synthesis paths consume; we never read its timers).
    if (bank == 0)
    {
        switch (reg)
        {
        case 0x02:
            _timer1Load = data;
            return;
        case 0x03:
            _timer2Load = data;
            return;
        case 0x04:
            _timer1Enable = (data & 0x01) != 0;
            _timer2Enable = (data & 0x02) != 0;
            _timer1Mask = (data & 0x40) != 0;
            _timer2Mask = (data & 0x20) != 0;
            if (data & 0x80) // reset timer flags
            {
                _status &= ~(kStatusT1 | kStatusT2);
                _timer1 = 0;
                _timer2 = 0;
            }
            return;
        default:
            break;
        }
        return;
    }

    switch (reg)
    {
    case 0x05: // 0x105: NEW (bit 0) / NEW2 (bit 1)
        _newMode = (data & 1) != 0;
        _new2 = (data & 2) != 0;
        return;
    default:
        break;
    }
}

uint8_t Opl4FmYmfm::ReadStatus() const
{
    return static_cast<uint8_t>(_status | 0x80); // bit 7 reads as 1
}

void Opl4FmYmfm::Advance(int32_t& outL, int32_t& outR,
                          std::array<int32_t, kChannelCount>& channelTaps)
{
    // One FM sample per 684-clock boundary: ymf262::generate() is exactly
    // the clock + output + clamp16 sequence ymf278b::generate() runs per FM
    // tick on the same engine family. Outputs 0/1 = primary stereo pair,
    // 2/3 = secondary; summing both pairs is the full YMF262 stereo mix.
    //
    // Domain: ymfm outputs 13-bit samples (peak ~8192), while the in-tree FM
    // and PCM engines output 16-bit samples (peak ~32768). Scale by 4× so a
    // unity carrier equals a unity slot on both backends.
    ymfm::ymf262::output_data out;
    _engine.generate(&out, 1);
    outL = (out.data[0] + out.data[2]) << 2;
    outR = (out.data[1] + out.data[3]) << 2;
    channelTaps.fill(0); // per-channel taps not modelled (temporary backend)
    AdvanceTimers(); // Opl4Fm::Advance self-ticks: chip calls only Advance()
}

void Opl4FmYmfm::AdvanceTimers()
{
    // Field-for-field copy of Opl4Fm::AdvanceTimers: T1 every (0x100-load)*4
    // FM steps, T2 every *16, status flags gated by the mask bits.
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

void Opl4FmYmfm::SaveState(uint8_t* dst) const
{
    // Layout: [0] version, [1..2] payload length LE, [3] flag bits,
    // [4..5] timer loads, [6..7] timer counters, [8] status, [9..15]
    // reserved zero, [16..] ymfm payload zero-padded to the cap.
    std::memset(dst, 0, kStateSize);
    dst[0] = 1;

    _io.clear();
    ymfm::ymfm_saved_state st(_io, true);
    _engine.SaveTo(st); // pure serialization (local TTD patch)
    const size_t len = std::min(_io.size(), kYmfmStateCap);

    dst[1] = static_cast<uint8_t>(len & 0xFF);
    dst[2] = static_cast<uint8_t>(len >> 8);
    uint8_t flags = 0;
    if (_newMode) flags |= 1;
    if (_new2) flags |= 2;
    if (_timer1Enable) flags |= 4;
    if (_timer2Enable) flags |= 8;
    if (_timer1Mask) flags |= 16;
    if (_timer2Mask) flags |= 32;
    dst[3] = flags;
    dst[4] = static_cast<uint8_t>(_timer1Load);
    dst[5] = static_cast<uint8_t>(_timer2Load);
    dst[6] = static_cast<uint8_t>(_timer1);
    dst[7] = static_cast<uint8_t>(_timer2);
    dst[8] = _status;
    if (len != 0)
        std::memcpy(dst + 16, _io.data(), len);
}

void Opl4FmYmfm::LoadState(const uint8_t* src)
{
    if (src[0] != 1)
        return; // refuse foreign layouts

    const size_t len = static_cast<size_t>(src[1]) | (static_cast<size_t>(src[2]) << 8);
    if (len > kYmfmStateCap)
        return;

    const uint8_t flags = src[3];
    _newMode = (flags & 1) != 0;
    _new2 = (flags & 2) != 0;
    _timer1Enable = (flags & 4) != 0;
    _timer2Enable = (flags & 8) != 0;
    _timer1Mask = (flags & 16) != 0;
    _timer2Mask = (flags & 32) != 0;
    _timer1Load = src[4];
    _timer2Load = src[5];
    _timer1 = src[6];
    _timer2 = src[7];
    _status = src[8];

    _io.assign(src + 16, src + 16 + len);
    ymfm::ymfm_saved_state st(_io, false);
    _engine.save_restore(st); // exact continuation (local TTD patch)
}

} // namespace opl4
