// libopl4 — PCM engine implementation (core TDD §5).
#include "pcm/pcmsynthopl4.h"

#include "common/exptable.h"
#include "common/mixtables.h"

#include <algorithm>
#include <cstring>

namespace opl4
{

void Opl4Pcm::Reset()
{
    // Whole-object zero, padding included: `PcmSlot{}` value-init only
    // stores members (the non-zero envVol NSDMI defeats the whole-object
    // zero-fill), and the trivially-copyable fill() assignment then copies
    // the temporary's stack-garbage padding into every slot — garbage
    // SaveState would serialize into the TTD hash blob. envVol is the one
    // non-zero default to re-apply afterwards.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif
    std::memset(_slots.data(), 0, sizeof(_slots));
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    for (auto& s : _slots)
        s.envVol = static_cast<int16_t>(kMaxAttIndex);
    _regs.fill(0);
    _regs[0xF8] = 0x1B; // FM block mix reset (kept in the wave reg file; D9)
    _memAdr = 0;
    _egCnt = 0;
}

bool Opl4Pcm::AnyActive() const
{
    for (const auto& s : _slots)
        if (s.state != EgPhase::Off)
            return true;
    return false;
}

uint8_t Opl4Pcm::ComputeRate(const PcmSlot& s, int val) const
{
    if (val == 0)
        return 0;
    if (val == 15)
        return 63;
    int res = val * 4;
    if (s.rc != 15)
    {
        // clamping verified with HW tests -Valley Bell
        const int octRc = std::clamp(static_cast<int>(s.oct) + static_cast<int>(s.rc), 0, 15);
        res += 2 * octRc;
        res += (s.fn & 0x200) ? 1 : 0;
    }
    return static_cast<uint8_t>(std::clamp(res, 0, 63));
}

uint8_t Opl4Pcm::ComputeDecayRate(const PcmSlot& s, int val) const
{
    if (s.damp)
    {
        // Manual: -12dB at sample 256, -96dB at sample 480; verified to
        // ignore rate correction.
        if (s.envVol < kDecayLevelTable[4])
            return 48; //   0 dB .. -12 dB
        return 63;     // -12 dB .. -96 dB
    }
    if (s.prvb)
    {
        // Activated at -18 dB; overrides D1R/D2R/RR with internal rate 4*5.
        if (s.envVol >= kDecayLevelTable[6])
            return 20;
    }
    return ComputeRate(s, val);
}

int16_t Opl4Pcm::ComputeVib(const PcmSlot& s) const
{
    int lfoFm = static_cast<int>(s.lfoCnt / (kLfoPeriod / 0x40));
    // +0x00..+0x0F, +0x0F..+0x00, -0x00..-0x0F, -0x0F..-0x00
    if (lfoFm & 0x10)
        lfoFm ^= 0x1F;
    if (lfoFm & 0x20)
        lfoFm = -(lfoFm & 0x0F);
    return static_cast<int16_t>((lfoFm * kVibDepth[s.vib]) / 12);
}

uint16_t Opl4Pcm::ComputeAm(const PcmSlot& s) const
{
    int lfoAm = static_cast<int>(s.lfoCnt / (kLfoPeriod / 0x100));
    // 0x00..0x7F, 0x7F..0x00
    if (lfoAm >= 0x80)
        lfoAm ^= 0xFF;
    return static_cast<uint16_t>((lfoAm * kAmDepth[s.am]) >> 7);
}

void Opl4Pcm::KeyOnHelper(PcmSlot& s)
{
    // Unlike FM, the envelope level is reset (the sample restarts).
    s.envVol = kMaxAttIndex;
    if (ComputeRate(s, s.ar) < 63)
    {
        s.state = EgPhase::Att;
    }
    else
    {
        // AR = 15 is zero-time attack.
        s.envVol = kMinAttIndex;
        s.state = s.dl ? EgPhase::Dec : EgPhase::Sus;
    }
    s.stepPtr = 0;
    s.pos = 0;
}

int16_t Opl4Pcm::GetSample(const PcmSlot& s, uint16_t pos) const
{
    const uint32_t addr = s.startAddr + pos;
    switch (s.bits)
    {
    case 0: // 8 bit
        return static_cast<int16_t>(_mem->Read(addr) << 8);
    case 1:
    { // 12 bit, 2 samples in 3 bytes
        const uint32_t a = s.startAddr + ((pos / 2) * 3);
        if (pos & 1)
            return static_cast<int16_t>((_mem->Read(a + 2) << 8)
                                        | (_mem->Read(a + 1) & 0xF0));
        return static_cast<int16_t>((_mem->Read(a + 0) << 8)
                                    | ((_mem->Read(a + 1) << 4) & 0xF0));
    }
    case 2: // 16 bit
    {
        const uint32_t a = s.startAddr + (pos * 2);
        return static_cast<int16_t>((_mem->Read(a + 0) << 8) | _mem->Read(a + 1));
    }
    default:
        return 0; // unspecified width
    }
}

uint16_t Opl4Pcm::NextPos(const PcmSlot& s, uint16_t pos, uint16_t increment)
{
    // Loop overrun (§5.4, openMSX-exact "this is how the actual chip does
    // it"): endAddr is stored as its complement S (= 0x10000 - true end), so
    // the chip tests pos + S >= 0x10000 and wraps by pos += S + loopAddr,
    // carrying the overrun into the loop — the audible loop glitch of the
    // real chip and MAME's historical pitch-fluctuation fix. S == 0 (a full
    // 64 KiB sample) never trips the test: linear one-shot play with the
    // natural 16-bit wrap.
    pos = static_cast<uint16_t>(pos + increment);
    if (static_cast<uint32_t>(pos) + s.endAddr >= 0x10000u)
        pos = static_cast<uint16_t>(pos + s.endAddr + s.loopAddr);
    return pos;
}

bool Opl4Pcm::WriteReg(uint8_t reg, uint8_t data)
{
    bool toneLoad = false;
    WriteRegDirect(reg, data);
    if (reg >= 0x08 && reg < 0x20) // bank 0: wave number low 8 bits (D7)
        toneLoad = true;
    // Reg 0x03 latches through the 6-bit mask inside WriteRegDirect; the
    // raw re-store here would unmask it and leak phantom bits into reads.
    if (reg != 0x03)
        _regs[reg] = data;
    return toneLoad;
}

void Opl4Pcm::WriteRegDirect(uint8_t reg, uint8_t data)
{
    if (reg >= 0x08 && reg <= 0xF7)
    {
        const int slotNum = (reg - 8) % 24;
        PcmSlot& s = _slots[slotNum];
        switch ((reg - 8) / 24)
        {
        case 0:
        {
            // Tone header fetch (D7). The 12 fetched bytes decode width /
            // start / loop / end, and bytes 7..11 rewrite banks 5..9
            // observably through the normal register path.
            s.wave = static_cast<uint16_t>((s.wave & 0x100) | data);
            const int waveTblHdr = (_regs[2] >> 2) & 0x7;
            const uint32_t base = (s.wave < 384 || waveTblHdr == 0)
                                      ? s.wave * 12
                                      : waveTblHdr * 0x80000u + (s.wave - 384) * 12;
            uint8_t buf[12];
            for (int i = 0; i < 12; i++)
                buf[i] = _mem->Read(base + i);
            s.bits = (buf[0] & 0xC0) >> 6;
            s.startAddr = buf[2] | (buf[1] << 8) | ((buf[0] & 0x3F) << 16);
            s.loopAddr = static_cast<uint16_t>(buf[4] | (buf[3] << 8));
            // Bytes 5..6 hold the end point as its complement S (stored
            // negative; openMSX keeps it raw and wraps chip-style in
            // NextPos). S == 0 means a full 64 KiB one-shot.
            s.endAddr = static_cast<uint16_t>(buf[6] | (buf[5] << 8));
            for (int i = 7; i < 12; i++)
            {
                // Bytes 7..11 rewrite banks 5..9 *observably*: route through
                // the normal register path AND store the raw byte so register
                // reads return it (D7).
                const uint8_t rewriteReg = static_cast<uint8_t>(8 + slotNum + (i - 2) * 24);
                WriteRegDirect(rewriteReg, buf[i]);
                _regs[rewriteReg] = buf[i];
            }
            if (s.keyon)
            {
                KeyOnHelper(s);
            }
            else
            {
                s.stepPtr = 0;
                s.pos = 0;
            }
            break;
        }
        case 1:
            s.wave = static_cast<uint16_t>((s.wave & 0xFF) | ((data & 0x1) << 8));
            s.fn = static_cast<uint16_t>((s.fn & 0x380) | (data >> 1));
            s.step = CalcStep(s.oct, s.fn);
            break;
        case 2:
            s.fn = static_cast<uint16_t>((s.fn & 0x07F) | ((data & 0x07) << 7));
            s.prvb = (data & 0x08) != 0;
            s.oct = SignExtend4((data & 0xF0) >> 4);
            s.step = CalcStep(s.oct, s.fn);
            break;
        case 3:
        {
            const uint8_t t = data >> 1;
            // TL register 0x7F maps to internal level 0xFF (D6, HW-verified).
            s.tlDest = (t != 0x7F) ? t : 0xFF;
            if (data & 1)
                s.tl = s.tlDest; // LD set: immediate
            break;
        }
        case 4:
            // DO1 pin is unwired on ZXM-MoonSound: modelled as silence.
            s.pan = (data & 0x10) ? 8 : (data & 0x0F);
            if (data & 0x20)
            {
                s.lfoActive = false;
                s.lfoCnt = 0;
            }
            else
            {
                s.lfoActive = true;
            }
            s.damp = (data & 0x40) != 0;
            if (data & 0x80)
            {
                if (!s.keyon)
                {
                    s.keyon = true;
                    KeyOnHelper(s);
                }
            }
            else if (s.keyon)
            {
                s.keyon = false;
                s.state = EgPhase::Rel;
            }
            break;
        case 5:
            s.lfo = (data >> 3) & 0x7;
            s.vib = data & 0x7;
            break;
        case 6:
            s.ar = data >> 4;
            s.d1r = data & 0xF;
            break;
        case 7:
            s.dl = static_cast<uint16_t>(kDecayLevelTable[data >> 4]);
            s.d2r = data & 0xF;
            break;
        case 8:
            s.rc = data >> 4;
            s.rr = data & 0xF;
            break;
        case 9:
            s.am = data & 0x7;
            break;
        }
        return;
    }

    switch (reg)
    {
    case 0x00:
    case 0x01:
        break; // test
    case 0x02:
        // Memory mode / wave-table header base: stored; the engine reads it
        // lazily at tone fetch. Bits 4..2 = WTB header base, bit 1 = the
        // /MCS chip-select mode (openMSX setupMemoryPointers; both modes map
        // identically on a 2 MiB ROM + 1 MiB SRAM board, so unmodelled),
        // bit 0 = MA (memory access).
        break;
    case 0x03:
        // Verified on real YMF278: regs 3/4 only latch; upper 2 bits read 0.
        data &= 0x3F;
        break;
    case 0x04:
        break;
    case 0x05:
        // Only writes to reg 5 commit the full 22-bit address counter.
        _memAdr = ((_regs[3] << 16) | (_regs[4] << 8) | data) & 0x3FFFFF;
        break;
    case 0x06:
        // Memory data port: active only when MA = 1 (reg 0x02 bit 0).
        if (_regs[2] & 1)
        {
            _mem->Write(_memAdr, data);
            _memAdr = (_memAdr + 1) & 0x3FFFFF;
        }
        break;
    default:
        break; // 0xF8/0xF9 handled by the top level (block mix, D9)
    }
    _regs[reg] = data;
}

uint8_t Opl4Pcm::PeekReg(uint8_t reg) const
{
    switch (reg)
    {
    case 2: // 3 upper bits are device ID (openMSX: | 0x20)
        return (_regs[2] & 0x1F) | 0x20;
    case 6:
        if (_regs[2] & 1)
            return _mem->Read(_memAdr);
        return 0xFF; // verified on real YMF278
    default:
        return _regs[reg];
    }
}

uint8_t Opl4Pcm::ReadReg(uint8_t reg)
{
    const uint8_t result = PeekReg(reg);
    if (reg == 6 && (_regs[2] & 1))
        _memAdr = (_memAdr + 1) & 0x3FFFFF; // only when MA = 1 (verified)
    return result;
}

void Opl4Pcm::Advance(int32_t& sumLeft, int32_t& sumRight,
                      std::array<int32_t, kSlotCount>& monoTaps)
{
    for (int i = 0; i < kSlotCount; i++)
    {
        PcmSlot& s = _slots[i];
        if (s.state == EgPhase::Off)
        {
            monoTaps[i] = 0;
            continue;
        }

        // 1. Fetch and interpolate with the 16-bit fractional step pointer.
        // int64 domain: the unsigned form wrapped negative samples to
        // large positives (-32768 * 0x10000 >> 16 = +32768) — §12.2 vectors.
        const uint32_t frac = s.stepPtr & 0xFFFF;
        const int32_t sample = static_cast<int32_t>(
            (static_cast<int64_t>(GetSample(s, s.pos)) * (0x10000 - frac)
             + static_cast<int64_t>(GetSample(s, NextPos(s, s.pos, 1))) * frac)
            >> 16);

        // 2/3. Envelope and TL applied as two separately clipped stages (D4).
        uint32_t envVol = static_cast<uint32_t>(s.envVol);
        if (s.lfoActive && s.am)
            envVol += ComputeAm(s);
        envVol = std::min<uint32_t>(envVol, kMaxAttIndex);
        const int32_t smplOut = VolFactor(VolFactor(sample, envVol),
                                          static_cast<uint32_t>(s.tl) << kTlShift);
        monoTaps[i] = smplOut; // post-envelope, post-TL, pre-pan tap (§8.5)

        // 4. Pan (D8) via the 16-entry table, independently clipped.
        const PanPair pp = kPanTable[s.pan & 0x0F];
        sumLeft += VolFactor(smplOut, pp.left);
        sumRight += VolFactor(smplOut, pp.right);

        // 6. Advance position (vibrato recomputes the step when active).
        const uint32_t step = (s.lfoActive && s.vib)
                                  ? CalcStep(s.oct, s.fn, ComputeVib(s))
                                  : s.step;
        s.stepPtr += step;
        if (s.stepPtr >= 0x10000)
        {
            s.pos = NextPos(s, s.pos, static_cast<uint16_t>(s.stepPtr >> 16));
            s.stepPtr &= 0xFFFF;
        }
    }

    // Envelope + TL interpolation + LFO counters advance once per sample.
    _egCnt++;

    // TL interpolation (D6): derived from egCnt — deterministic modulo
    // counters, one step down per 27 samples, up per 13.5 samples.
    const uint64_t tlIntCnt = _egCnt % 9;      // 0..8
    const uint64_t tlIntStep = (_egCnt / 9) % 3; // 0..2
    for (auto& s : _slots)
    {
        if (tlIntCnt == 0)
        {
            if (tlIntStep == 0)
            {
                if (s.tl < s.tlDest) ++s.tl; // attenuate: every 27 samples
            }
            else
            {
                if (s.tl > s.tlDest) --s.tl; // brighten: every 13.5 samples
            }
        }
        if (s.lfoActive)
            s.lfoCnt = (s.lfoCnt + kLfoSteps[s.lfo]) & (kLfoPeriod - 1);
    }

    for (auto& s : _slots)
    {
        switch (s.state)
        {
        case EgPhase::Att:
        {
            const uint8_t rate = ComputeRate(s, s.ar);
            if (rate >= 63)
                break; // AR 15 set mid-attack freezes the envelope
            const uint8_t shift = kEgRateShift[rate];
            if (!(_egCnt & ((1u << shift) - 1)))
            {
                const uint8_t select = kEgRateSelect[rate];
                // >>4 shapes the attack to match the actual chip -Valley Bell
                const int32_t inc = ((~s.envVol * kEgInc[select + ((_egCnt >> shift) & 7)]) >> 4);
                s.envVol = static_cast<int16_t>(s.envVol + inc);
                if (s.envVol <= kMinAttIndex)
                {
                    s.envVol = kMinAttIndex;
                    s.state = s.dl ? EgPhase::Dec : EgPhase::Sus;
                }
            }
            break;
        }
        case EgPhase::Dec:
        {
            const uint8_t rate = ComputeDecayRate(s, s.d1r);
            const uint8_t shift = kEgRateShift[rate];
            if (!(_egCnt & ((1u << shift) - 1)))
            {
                const uint8_t select = kEgRateSelect[rate];
                s.envVol = static_cast<int16_t>(
                    s.envVol + kEgInc[select + ((_egCnt >> shift) & 7)]);
                if (s.envVol >= static_cast<int16_t>(s.dl))
                    s.state = (s.envVol < kMaxAttIndex) ? EgPhase::Sus : EgPhase::Off;
            }
            break;
        }
        case EgPhase::Sus:
        {
            const uint8_t rate = ComputeDecayRate(s, s.d2r);
            const uint8_t shift = kEgRateShift[rate];
            if (!(_egCnt & ((1u << shift) - 1)))
            {
                const uint8_t select = kEgRateSelect[rate];
                s.envVol = static_cast<int16_t>(
                    s.envVol + kEgInc[select + ((_egCnt >> shift) & 7)]);
                if (s.envVol >= kMaxAttIndex)
                {
                    s.envVol = kMaxAttIndex;
                    s.state = EgPhase::Off;
                }
            }
            break;
        }
        case EgPhase::Rel:
        {
            const uint8_t rate = ComputeDecayRate(s, s.rr);
            const uint8_t shift = kEgRateShift[rate];
            if (!(_egCnt & ((1u << shift) - 1)))
            {
                const uint8_t select = kEgRateSelect[rate];
                s.envVol = static_cast<int16_t>(
                    s.envVol + kEgInc[select + ((_egCnt >> shift) & 7)]);
                if (s.envVol >= kMaxAttIndex)
                {
                    s.envVol = kMaxAttIndex;
                    s.state = EgPhase::Off;
                }
            }
            break;
        }
        case EgPhase::Off:
            break;
        }
    }
}

void Opl4Pcm::SaveState(uint8_t* dst) const
{
    // Packed POD layout with explicit sizes — no pointers, no padding
    // ambiguity, no floats (§9.2 item 4). Copy field groups manually.
    size_t o = 0;
    std::memcpy(dst + o, _regs.data(), 256); o += 256;
    for (const auto& s : _slots)
    {
        std::memcpy(dst + o, &s, sizeof(PcmSlot));
        o += sizeof(PcmSlot);
    }
    std::memcpy(dst + o, &_memAdr, 4); o += 4;
    std::memcpy(dst + o, &_egCnt, 8); o += 8;
    // trailing 4 bytes: reserved, zeroed
    std::memset(dst + o, 0, 4);
}

void Opl4Pcm::LoadState(const uint8_t* src)
{
    size_t o = 0;
    std::memcpy(_regs.data(), src + o, 256); o += 256;
    for (auto& s : _slots)
    {
        std::memcpy(&s, src + o, sizeof(PcmSlot));
        o += sizeof(PcmSlot);
    }
    std::memcpy(&_memAdr, src + o, 4); o += 4;
    std::memcpy(&_egCnt, src + o, 8); o += 8;
}

} // namespace opl4
