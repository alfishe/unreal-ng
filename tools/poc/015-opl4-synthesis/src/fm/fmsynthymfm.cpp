// libopl4 — FM engine, ymfm verification backend (implementation).
#include "fm/fmsynthymfm.h"

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
    // choice).
    _engine.reset();
    _engine.ResetAddress();
}

void Opl4FmYmfm::WriteReg(uint16_t reg, uint8_t data)
{
    // Already de-aliased by FmBus: the bank bit selects the address latch
    // and the byte lands in ymfm's own register file for its synthesis
    // paths (ymfm decodes 0x105 NEW internally, as before).
    if ((reg & 0x100) != 0)
        _engine.write_address_hi(static_cast<uint8_t>(reg & 0xFF));
    else
        _engine.write_address(static_cast<uint8_t>(reg));
    _engine.write_data(data);
}

void Opl4FmYmfm::Advance(FmOutput& out)
{
    // One FM sample per 684-clock boundary: ymf262::generate() is exactly
    // the clock + output + clamp16 sequence ymf278b::generate() runs per FM
    // tick on the same engine family. Outputs 0/1 = primary stereo pair,
    // 2/3 = secondary; summing both pairs is the full YMF262 stereo mix.
    //
    // Domain: ymfm outputs 13-bit samples (peak ~8192), while the in-tree FM
    // and PCM engines output 16-bit samples (peak ~32768). Scale by 4× so a
    // unity carrier equals a unity slot on both backends.
    ymfm::ymf262::output_data o;
    _engine.generate(&o, 1);
    out.mixL = (o.data[0] + o.data[2]) << 2;
    out.mixR = (o.data[1] + o.data[3]) << 2;
    // channel[] stays zero: no per-channel taps (temporary backend).
}

void Opl4FmYmfm::SaveState(uint8_t* dst) const
{
    // Layout 2 (synthesis-only): [0] version, [1..2] payload length LE,
    // [3..15] reserved zero, [16..] ymfm payload zero-padded to the cap.
    std::memset(dst, 0, kStateSize);
    dst[0] = 2;

    _io.clear();
    ymfm::ymfm_saved_state st(_io, true);
    _engine.SaveTo(st); // pure serialization (local TTD patch)
    const size_t len = std::min(_io.size(), kYmfmStateCap);

    dst[1] = static_cast<uint8_t>(len & 0xFF);
    dst[2] = static_cast<uint8_t>(len >> 8);
    if (len != 0)
        std::memcpy(dst + 16, _io.data(), len);
}

void Opl4FmYmfm::LoadState(const uint8_t* src)
{
    if (src[0] != 2)
        return; // refuse foreign layouts

    const size_t len = static_cast<size_t>(src[1]) | (static_cast<size_t>(src[2]) << 8);
    if (len > kYmfmStateCap)
        return;

    _io.assign(src + 16, src + 16 + len);
    ymfm::ymfm_saved_state st(_io, false);
    _engine.save_restore(st); // exact continuation (local TTD patch)
}

} // namespace opl4
