// libopl4 — FM engine implementation (core TDD §4).
//
// Phase math: datasheet pitch on the 49516.4 Hz grid — f = fnum * 2^block *
// fmClock / 2^20 (F-number 582 / block 4 = A440, D1/§4.2; YMF262's 49716 Hz
// grid is the §4.2 deviation we do NOT take). With the 19-bit accumulator
// (2^-19-cycle units, top 10 bits index the sine) the step is
// (fnum << block) >> 1; vibrato is carried in a 12-bit fnumber fraction so
// the PM swing stays proportional to F-number exactly like YMF262.
//
// Modulator domain: YMF262 adds operator outputs to the NEXT PHASE in
// sine-table units (low 10 bits = one full cycle), op1 >> 1 for inter-op
// modulation — full-scale ±4 cycles, the deep FM of the real chip (ymfm
// operator values are 14-bit signed, peak 8192; this rail is peak 32768,
// so the 19-bit equivalent of op>>1 is op << 6). Feedback is the ymfm
// two-tap loop: (fb0 + fb1) >> (10 - fb) in table units, the average of
// the two previous op1 outputs — (fb0 + fb1) << (fb - 3) here: fb7 = ±1
// cycle, fb1 = ±1/64 cycle, fb0 = none.
#include "fm/fmsynthopl4.h"

#include "common/exptable.h"
#include "fm/fmtables.h"

#include <cstring>

namespace opl4
{

namespace
{

// Operator output on the FM grid: the total attenuation (envelope + TL + KSL
// + AM) saturates at the 96 dB ceiling, where the output is zero. The PCM
// VolFactor clips at -60 dB instead, which silenced quiet FM modulators.
inline int32_t FmVolFactor(int32_t sample, uint32_t index)
{
    if (index >= static_cast<uint32_t>(kFmMaxAttIndex))
        return 0;
    const uint32_t shift = index >> 6;
    const uint32_t step = (index & 0x3F) << 2;
    return (sample * static_cast<int32_t>(kPowerTable[step])) >> (11 + shift);
}

inline int16_t SineOf(uint16_t index10)
{
    // Full 10-bit sine via quadrant folding.
    uint16_t idx = index10 & 0x3FF;
    int16_t v;
    if (idx & 0x100)
        v = kSineTable[0xFF - (idx & 0xFF)];
    else
        v = kSineTable[idx & 0xFF];
    return (idx & 0x200) ? static_cast<int16_t>(-v) : v;
}

} // namespace

void Opl4Fm::Reset()
{
    _regs.fill(0);
    // Whole-object zero, padding included: `FmOperator{}` value-init only
    // stores members (the non-zero envVol NSDMI defeats the whole-object
    // zero-fill), and the trivially-copyable fill() assignment then copies
    // the temporary's stack-garbage padding into every operator — garbage
    // SaveState would serialize into the TTD hash blob. envVol is the one
    // non-zero default to re-apply afterwards.
    std::memset(_ops.data(), 0, sizeof(_ops));
    for (auto& op : _ops)
        op.envVol = kFmMaxAttIndex;
    std::memset(_ch.data(), 0, sizeof(_ch)); // FmChannel has no non-zero defaults
    _fbHist.fill({});
    _rhythm = false;
    _newMode = false;
    _egCnt = 0;
    _lfoPm = 0;
    _lfoAm = 0;
    _noise = 1;
    // Channel -> operator map (classic YMF262 layout): per bank, channel n
    // (0..8) owns register offsets (n%3) + 8*(n/3) and +3 — the canonical
    // slots with the 0x26/0x27 and 0x2E/0x2F gaps; bank-1 slots are +22
    // (indices 22..43 — 22 slots per bank, the gaps included). _ops[k]
    // decodes register base+k exactly as silicon decodes that slot, so the
    // gap operators (6/7, 14/15 per bank) are stored but referenced by no
    // channel, like the unused registers they are. The bank stride MUST be
    // 22 (slots), not 18 (operators): with 18, bank-1 channel 0 would alias
    // bank-0 channel 8's rhythm operators.
    for (int i = 0; i < kChannelCount; i++)
    {
        const int n = i % 9;
        _ch[i].op1 = static_cast<uint8_t>(22 * (i / 9) + (n % 3) + 8 * (n / 3));
        _ch[i].op2 = static_cast<uint8_t>(_ch[i].op1 + 3);
    }
    RebuildConnections();
}

void Opl4Fm::RebuildConnections()
{
    const uint8_t conn4 = _regs[0x104]; // bank-1 address 0x04
    for (int i = 0; i < kChannelCount; i++)
        _ch[i].fourOp = false;
    if (_newMode)
    {
        // 4-op pairs: master 0,1,2,9,10,11 with slaves 3,4,5,12,13,14;
        // register 0x104 bit i selects pair i (bits 0..5).
        for (int i = 0; i < 6; i++)
        {
            const int master = (i < 3) ? i : i + 6;
            const bool on = (conn4 >> i) & 1;
            _ch[master].fourOp = on;
            _ch[master + 3].fourOp = on;
        }
    }
    // Pair ownership may have flipped: re-cache every channel so operators
    // follow their owning channel's registers immediately (silicon reads
    // the owning channel live; this engine caches on writes).
    for (int i = 0; i < kChannelCount; i++)
        UpdateChannelParams(static_cast<uint8_t>(i));
}

uint32_t Opl4Fm::PhaseStep(const FmOperator& op)
{
    // Datasheet pitch (D1/§4.2): f = fnum * 2^block * 49516.4 / 2^20 —
    // F-number 582 / block 4 = A440. Carried in a 12-bit fnumber fraction
    // (fnum << 2) so vibrato adds F-number-proportional cents like YMF262;
    // (fnum12 << block) >> 3 == (fnum << block) >> 1 in 2^-19-cycle units.
    uint32_t fnum12 = static_cast<uint32_t>(op.fnum) << 2;
    if (op.vib)
    {
        // PM: scale the top 3 fnumber bits by the bipolar LFO value;
        // 0xBD bit 6 = deep (full swing), clear = shallow (half).
        const int pm = kPmScale[(_lfoPm >> 10) & 7]
            >> ((_regs[0xBD] & 0x40) ? 0 : 1);
        fnum12 = (fnum12 + static_cast<uint32_t>(
                     (pm * static_cast<int>(op.fnum >> 7)) >> 1)) & 0xFFF;
    }
    const uint32_t step = (fnum12 << op.block) >> 3;
    return op.mult ? step * kMultTable[op.mult] : step >> 1; // mult 0 = x0.5
}

uint32_t Opl4Fm::KslIndex(const FmOperator& op)
{
    // OPL3 KSL (ymfm opl_key_scale_atten, consumed ymfm_opl.cpp:327):
    // grows with block and the F-number MSBs, folded per block (−8 units
    // per block below 7, clamped at 0). The table numerals are consumed
    // directly in the 0.09375 dB index domain — TL is pre-scaled <<3 on its
    // own path, so the shift here carries only the 2-bit KSL slope: reg 01
    // -> x4 = 3 dB/oct, 10 -> x2 = 1.5 dB/oct, 11 -> x8 = 6 dB/oct.
    if (op.ksl == 0)
        return 0;
    const int32_t atten = kKslAtten[op.fnum >> 6] - 8 * (op.block ^ 7);
    return static_cast<uint32_t>(atten > 0 ? atten : 0) << op.ksl;
}

uint8_t Opl4Fm::EgRate(const FmOperator& op, uint8_t regRate) const
{
    // Register rate 0 is a hard freeze — the key-scale adder never applies
    // (ymfm effective_rate: rawrate == 0 -> 0 before ksrval; Nuked gates
    // every increment on reg_rate != 0). A DR0 modulator therefore holds
    // its attack peak forever: the JAMMED2 pads (AR15/DR0/EGT1, FB7
    // additive) keep the full feedback loop gain and self-oscillate as
    // broadband noise; adding the keycode at rate 0 decayed them to SL and
    // collapsed the wash into a quiet sine.
    if (regRate == 0)
        return 0;
    // KSR (ymfm ymfm_opl.cpp:298): 4-bit keycode = block<<1 | the fnum bit
    // NTS picks (bank-0 register 0x08 bit 6, ymfm note_select / Nuked
    // chip->nts: bit 9 clear, bit 8 set — reversed from the manual); the
    // rate adds the FULL keycode when the KSR bit is set and keycode>>2
    // when clear. Nonzero register rates only: rate 4..63.
    const bool nts = (_regs[0x008] & 0x40) != 0;
    const uint8_t keycode = (op.block << 1) | ((op.fnum >> (nts ? 8 : 9)) & 1);
    int r = regRate * 4 + (op.ksr ? keycode : (keycode >> 2));
    if (r > 63)
        r = 63;
    return static_cast<uint8_t>(r);
}

int32_t Opl4Fm::WaveSample(const FmOperator& op, uint16_t ph) const
{
    // Canonical YMF262 set, ported from ymfm's attenuation-domain tables
    // (ymfm_opl.cpp constructor): wf0 sine, wf1 half-sine, wf2 |sine|,
    // wf3 |sine| quarters 1/3, wf4 sine(2x) first half, wf5 |sin(2x)|
    // first half, wf6 sign-only square, wf7 exponential pulse pair. All
    // shapes share the sine's peak scale (attenuation 0 at the crest).
    const int16_t s = SineOf(ph);
    switch (op.ws)
    {
    case 0:
        return s;
    case 1: // positive half only
        return (ph & 0x200) ? 0 : s;
    case 2: // full rectified
        return (ph & 0x200) ? -s : s;
    case 3: // |sin| on quarters 1/3, silence on 2/4 (ymfm wf3)
        return ((ph >> 8) & 1) ? 0 : ((ph & 0x200) ? -s : s);
    case 4: // one full sine(2x) cycle packed into the first half,
        // silence second half (ymfm wf4: wf0[index*2])
        return (ph & 0x200) ? 0 : SineOf(static_cast<uint16_t>(ph << 1));
    case 5: // |sin(2x)| first half, silence second half (ymfm wf5)
    {
        if (ph & 0x200)
            return 0;
        const int16_t v = SineOf(static_cast<uint16_t>(ph << 1));
        return v < 0 ? -static_cast<int32_t>(v) : static_cast<int32_t>(v);
    }
    case 6: // sign-only square at the sine's peak (ymfm wf6)
        return (ph & 0x200) ? -4096 : 4096;
    default: // ws 7: exponential pulse pair (ymfm wf7)
    {
        // ymfm: attenuation (bit9 ? (index^0x13ff) : index) << 3 decoded
        // through the die-derived power table = amplitude 2^(-x/32) of the
        // peak, x = index in the first half / 1023-index in the second:
        // a positive pulse decaying over the first half, a negative pulse
        // growing to full across the second.
        const uint16_t x = (ph & 0x200) ? static_cast<uint16_t>(1023 - ph) : ph;
        const uint32_t step = static_cast<uint32_t>(x) << 3; // 1/256-octave units
        const int32_t amp = (4096 * kPowerTable[step & 0xFF]) >> (11 + (step >> 8));
        return (ph & 0x200) ? -amp : amp;
    }
    }
}

int32_t Opl4Fm::OperatorOutput(FmOperator& op, int32_t modInput) const
{
    // Carrier/modulator: wave(phase + mod) scaled by envelope + TL + KSL.
    // The modulation input shifts the wave lookup phase itself (the FM in
    // FM): wave is evaluated at the modulated angle, never at the raw
    // operator phase.
    const uint32_t p = (op.phase + static_cast<uint32_t>(modInput)) & 0x7FFFF;
    const int32_t wave = WaveSample(op, static_cast<uint16_t>(p >> 9));

    uint32_t index = static_cast<uint32_t>(op.envVol);
    if (op.am)
    {
        // AM (YMF262): triangle over the 210*64-step counter (3.69 Hz),
        // peak 105*64; 0xBD bit 7 depth: 4.8 dB (>> 7) vs 1.2 dB (>> 9),
        // in 0.09375 dB index units.
        uint32_t amv = _lfoAm;
        if (amv >= 105u * 64u)
            amv = 210u * 64u + 63u - amv;
        amv >>= (_regs[0xBD] & 0x80) ? 7 : 9;
        index += amv;
    }
    index += static_cast<uint32_t>(op.tl) << 3; // TL: 0.75 dB -> 8 index units
    index += KslIndex(op);
    // Unity operator at full 16-bit scale: the 13-bit sine (peak 4096) shifts
    // up by 3. The chip rail is widened to accommodate multiple voices.
    op.out = FmVolFactor(wave << 3, index);
    return op.out;
}

void Opl4Fm::AdvanceEnvelope(FmOperator& op)
{
    // Same rate machinery as the PCM engine (OPL family shares it).
    switch (op.egState)
    {
    case kFmEgAtt:
    {
        const uint8_t rate = EgRate(op, op.ar);
        if (rate == 0)
            break; // AR0: frozen at max attenuation (ymfm/Nuked rate-0 gate)
        if (rate >= 63)
        {
            op.envVol = kFmMinAttIndex;
            op.egState = kFmEgDec;
            break;
        }
        const uint8_t shift = kFmEgRateShift[rate];
        if (!(_egCnt & ((1u << shift) - 1)))
        {
            const uint8_t select = FmRateRow(rate);
            const int32_t inc = (~op.envVol * kFmEgInc[select + ((_egCnt >> shift) & 7)]) >> 4;
            op.envVol = static_cast<int16_t>(op.envVol + inc);
            if (op.envVol <= kFmMinAttIndex)
            {
                op.envVol = kFmMinAttIndex;
                op.egState = kFmEgDec;
            }
        }
        break;
    }
    case kFmEgDec:
    {
        const uint8_t rate = EgRate(op, op.dr);
        if (rate == 0)
            break; // DR0: holds the attack peak — the JAMMED2 pad wash lives here
        const uint8_t shift = kFmEgRateShift[rate];
        if (!(_egCnt & ((1u << shift) - 1)))
        {
            const uint8_t select = FmRateRow(rate);
            op.envVol = static_cast<int16_t>(op.envVol + kFmEgInc[select + ((_egCnt >> shift) & 7)]);
            // OPL3 sustain level (ymfm ymfm_opl.cpp:329): SL is 3 dB steps
            // (<<5 in the 0.09375 dB index domain) with SL 15 doubling to
            // "no sustain sound" ((SL|(SL+1)&0x10)<<5); capped at this
            // 96 dB ceiling. The PCM copy's <<4 halved every sustain
            // level.
            int sustainLevel = (op.sl | ((op.sl + 1) & 0x10)) << 5;
            if (sustainLevel > kFmMaxAttIndex)
                sustainLevel = kFmMaxAttIndex;
            if (op.envVol >= sustainLevel)
                op.egState = op.egt ? kFmEgSus : kFmEgSus;
        }
        break;
    }
    case kFmEgSus:
    {
        if (!op.egt)
        {
            // non-sustaining (EGT 0): the post-SL segment decays at the
            // RELEASE rate, not DR (ymfm opl prepare: eg_rate[EG_SUSTAIN] =
            // egt ? 0 : effective_rate(RR*4, ksrval); YMF262 datasheet "the
            // sound decays even in the sustain segment" at RR). One-shot
            // drum envelopes (CRYOGENT, mfm_sample_2) depend on it: a DR1/
            // RR10 op4 must fade in tens of ms, not freeze at SL.
            const uint8_t rate = EgRate(op, op.rr);
            if (rate == 0)
                break; // RR0 with EGT0: holds at SL forever (rate-0 freeze)
            const uint8_t shift = kFmEgRateShift[rate];
            if (!(_egCnt & ((1u << shift) - 1)))
            {
                const uint8_t select = FmRateRow(rate);
                op.envVol = static_cast<int16_t>(op.envVol + kFmEgInc[select + ((_egCnt >> shift) & 7)]);
                if (op.envVol >= kFmMaxAttIndex)
                {
                    op.envVol = kFmMaxAttIndex;
                    op.egState = kFmEgOff;
                }
            }
        }
        break;
    }
    case kFmEgRel:
    {
        const uint8_t rate = EgRate(op, op.rr);
        if (rate == 0)
            break; // RR0 key-off never fades (classic OPL eternal sustain)
        const uint8_t shift = kFmEgRateShift[rate];
        if (!(_egCnt & ((1u << shift) - 1)))
        {
            const uint8_t select = FmRateRow(rate);
            op.envVol = static_cast<int16_t>(op.envVol + kFmEgInc[select + ((_egCnt >> shift) & 7)]);
            if (op.envVol >= kFmMaxAttIndex)
            {
                op.envVol = kFmMaxAttIndex;
                op.egState = kFmEgOff;
            }
        }
        break;
    }
    case kFmEgOff:
    default:
        break;
    }
}

void Opl4Fm::KeyOn(FmOperator& op, bool on)
{
    // Latched only: like the silicon (ymfm clock_keystate, Nuked's per-sample
    // key edge) the envelope samples the key once per clock, so a key-off
    // and key-on written between two clocks are no transition at all.
    op.keyReq = on;
}

void Opl4Fm::ClockKeyState(FmOperator& op) const
{
    if (op.keyReq == op.keyOn)
        return;
    op.keyOn = op.keyReq;
    if (op.keyOn)
    {
        // Unlike the PCM engine, FM attack starts from the current envelope
        // level (ymfm start_attack, Nuked-OPL3): re-keying a sounding note
        // must not drop it to silence first. The phase restarts.
        op.phase = 0;
        op.egState = (EgRate(op, op.ar) >= 63) ? kFmEgDec : kFmEgAtt;
        if (op.egState == kFmEgDec)
            op.envVol = kFmMinAttIndex;
    }
    else
    {
        op.egState = kFmEgRel;
    }
}

void Opl4Fm::UpdateChannelParams(uint8_t ch)
{
    // Refresh channel-level cached parameters from the register file.
    const uint8_t bankBase = (ch >= 9) ? 1 : 0; // register bank selector (x256)
    const uint8_t chReg = static_cast<uint8_t>(ch - (ch >= 9 ? 9 : 0));
    const uint16_t cData = _regs[(bankBase << 8) + 0xC0 + chReg];
    _ch[ch].fbShift = (cData >> 1) & 7; // C0 bits 3:1; routing nibble is bus-owned
    _ch[ch].conn = cData & 0x01;
    const uint16_t regBase = static_cast<uint16_t>(bankBase << 8);
    const uint8_t bData = _regs[regBase + 0xB0 + chReg];
    const uint16_t fn = static_cast<uint16_t>(_regs[regBase + 0xA0 + chReg]
                                              | ((bData & 0x03) << 8));
    const uint8_t blk = (bData >> 2) & 7;

    // 4-op frequency ownership (YMF278B silicon / ymfm fm_channel::assign:
    // a master channel re-points all four operators' choffs to itself, so
    // phase AND key-scale reads come from the MASTER's 0xA0/0xB0; the
    // slave's own 0xA0/0xB0 feed nothing). CRYOGENT (mfm_sample_2 melody 7)
    // programs/keys masters only, so under a per-channel decode the slave
    // carriers never sounded and the voices collapsed to the op1 feedback
    // loop (white noise).
    if (_ch[ch].fourOp && _newMode)
    {
        if (!(ch < 3 || (ch >= 9 && ch < 12)))
            return; // slave: only its own C0 (algorithm bit) is live
        const int s = ch + 3;
        FmOperator* const quad[4] = { &_ops[_ch[ch].op1], &_ops[_ch[ch].op2],
                                      &_ops[_ch[s].op1], &_ops[_ch[s].op2] };
        for (FmOperator* op : quad)
        {
            op->fnum = fn;
            op->block = blk;
        }
        return;
    }
    _ops[_ch[ch].op1].fnum = fn;
    _ops[_ch[ch].op2].fnum = fn;
    _ops[_ch[ch].op1].block = blk;
    _ops[_ch[ch].op2].block = blk;
}

void Opl4Fm::WriteReg(uint16_t reg, uint8_t data)
{
    // De-aliased 0x000..0x1FF (bank in bit 8): FmBus resolved aliasing and
    // NEW/NEW2 gating; timers/status live there too. This shadow feeds only
    // synthesis reads (0xA0 partial fnum, 0xBD, 0x104, 0x105).
    _regs[reg] = data;
    const int bank = (reg >> 8) & 1;
    const uint8_t r = static_cast<uint8_t>(reg & 0xFF);

    if (bank != 0)
    {
        switch (r)
        {
        case 0x04: // 0x104: 4-op connection select
            RebuildConnections();
            return;
        case 0x05: // 0x105: NEW gates 4-op pairing (guest flag is bus-owned)
            _newMode = (data & 1) != 0;
            RebuildConnections();
            return;
        default:
            break;
        }
    }
    else if (r == 0xBD)
    {
        _rhythm = (data & 0x20) != 0;
        // Rhythm key-ons, datasheet 0xBD bits: BD 0x10, SD 0x08, TOM
        // 0x04, CY 0x02, HH 0x01 (ymfm/Nuked-verified order). Classic
        // voice set (regs 0x30-0x35): BD = ch6 pair; HH = ch7
        // modulator, SD = ch7 carrier; TOM = ch8 modulator, CY = ch8
        // carrier — addressed through the channel map so the decode
        // always follows it.
        KeyOn(_ops[_ch[6].op1], (data & 0x10) != 0); // BD mod
        KeyOn(_ops[_ch[6].op2], (data & 0x10) != 0); // BD car
        KeyOn(_ops[_ch[7].op1], (data & 0x01) != 0); // HH envelope (bit 0)
        KeyOn(_ops[_ch[7].op2], (data & 0x08) != 0); // SD envelope (bit 3)
        KeyOn(_ops[_ch[8].op1], (data & 0x04) != 0); // TOM (bit 2)
        KeyOn(_ops[_ch[8].op2], (data & 0x02) != 0); // CY envelope (bit 1)
        return;
    }

    // Channel/operator registers: bank 0 covers channels 0..8, bank 1
    // channels 9..17. Operators are slot-linear (_ops[k] = register family
    // offset k, k 0..21; stride 22 per bank, the 0x26/0x27 and 0x2E/0x2F
    // gap slots stored but referenced by no channel).
    const int opBase = bank ? 22 : 0;
    const int chBase = bank ? 9 : 0;

    if (r >= 0x20 && r <= 0x35)
    {
        FmOperator& op = _ops[opBase + (r - 0x20)];
        op.am = (data & 0x80) != 0;
        op.vib = (data & 0x40) != 0;
        op.egt = (data & 0x20) != 0;
        op.ksr = (data & 0x10) != 0;
        op.mult = data & 0x0F;
        return;
    }
    if (r >= 0x40 && r <= 0x55)
    {
        FmOperator& op = _ops[opBase + (r - 0x40)];
        // YMF262 KSL encoding is non-monotonic (ymfm swaps the two bits):
        // reg 01 -> shift 2, reg 10 -> shift 1, reg 11 -> shift 3.
        const uint8_t kslBits = (data >> 6) & 3;
        op.ksl = static_cast<uint8_t>(((kslBits & 1) << 1) | (kslBits >> 1));
        op.tl = data & 0x3F;
        return;
    }
    if (r >= 0x60 && r <= 0x75)
    {
        FmOperator& op = _ops[opBase + (r - 0x60)];
        op.ar = data >> 4;
        op.dr = data & 0x0F;
        return;
    }
    if (r >= 0x80 && r <= 0x95)
    {
        FmOperator& op = _ops[opBase + (r - 0x80)];
        op.sl = data >> 4;
        op.rr = data & 0x0F;
        return;
    }
    if (r >= 0xE0 && r <= 0xF5)
    {
        _ops[opBase + (r - 0xE0)].ws = data & 0x07;
        return;
    }
    if (r >= 0xA0 && r <= 0xA8)
    {
        const int ch = chBase + (r - 0xA0);
        const uint16_t fn = static_cast<uint16_t>((_regs[(bank ? 256 : 0) + 0xB0 + (r - 0xA0)] & 0x03) << 8
                                                  | data);
        if (_ch[ch].fourOp && _newMode)
        {
            // 4-op: the master's A0 tunes all four operators; the slave's
            // own A0 is dead (ownership as in UpdateChannelParams).
            if (ch < 3 || (ch >= 9 && ch < 12))
            {
                const int s = ch + 3;
                _ops[_ch[ch].op1].fnum = fn;
                _ops[_ch[ch].op2].fnum = fn;
                _ops[_ch[s].op1].fnum = fn;
                _ops[_ch[s].op2].fnum = fn;
            }
            return;
        }
        _ops[_ch[ch].op1].fnum = fn;
        _ops[_ch[ch].op2].fnum = fn;
        return;
    }
    if (r >= 0xB0 && r <= 0xB8)
    {
        const int ch = chBase + (r - 0xB0);
        UpdateChannelParams(static_cast<uint8_t>(ch));
        if (!(_rhythm && ch >= 6 && ch <= 8)) // rhythm claims bank-0 ch 6..8
        {
            const bool on = (data & 0x20) != 0;
            if (_ch[ch].fourOp && _newMode)
            {
                // 4-op keying (ymfm keyon_opmask 15): only the master's B0
                // keys, and it keys ALL FOUR operators at once; the slave's
                // B0 is dead. Per-channel keying left master-keyed MFM
                // tunes with permanently silent slave carriers.
                if (ch < 3 || (ch >= 9 && ch < 12))
                {
                    const int s = ch + 3;
                    KeyOn(_ops[_ch[ch].op1], on);
                    KeyOn(_ops[_ch[ch].op2], on);
                    KeyOn(_ops[_ch[s].op1], on);
                    KeyOn(_ops[_ch[s].op2], on);
                }
            }
            else
            {
                KeyOn(_ops[_ch[ch].op1], on);
                KeyOn(_ops[_ch[ch].op2], on);
            }
        }
        return;
    }
    if (r >= 0xC0 && r <= 0xC8)
    {
        const int ch = chBase + (r - 0xC0);
        _ch[ch].fbShift = (data >> 1) & 7; // include nibble decoded by the bus
        _ch[ch].conn = data & 0x01;
        return;
    }
}

void Opl4Fm::Advance(FmOutput& out)
{
    _egCnt++;
    _lfoPm = (_lfoPm + 1) & 0x1FFF; // PM LFO: 8192 steps = 6.04 Hz
    if (++_lfoAm >= 13440)          // AM LFO: 210*64 steps = 3.69 Hz
        _lfoAm = 0;
    // 23-bit noise LFSR (OPL3-style tap set).
    const uint32_t nbit = ((_noise >> 0) ^ (_noise >> 2) ^ (_noise >> 9)) & 1;
    _noise = (_noise << 1) | nbit;

    // Advance envelopes and phases.
    for (auto& op : _ops)
    {
        ClockKeyState(op);
        AdvanceEnvelope(op);
        op.phase = (op.phase + PhaseStep(op)) & 0x7FFFF;
    }

    for (int ch = 0; ch < kChannelCount; ch++)
    {
        const int op1 = _ch[ch].op1;
        const int op2 = _ch[ch].op2;
        int32_t result = 0;

        if (_ch[ch].fourOp && _newMode)
        {
            // Only masters emit; slaves feed the cascade below.
            const bool isMaster = (ch < 3 || (ch >= 9 && ch < 12));
            if (!isMaster)
                continue;

            const int s = ch + 3; // slave channel
            const int opA = _ch[ch].op1, opB = _ch[ch].op2;
            const int opC = _ch[s].op1, opD = _ch[s].op2;
            // Algorithm select (ymfm ch_algorithm, YMF262 silicon):
            // 8 + master CNT (bit 0) + slave CNT<<1 — the four connections:
            //   8: O1→O2→O3→O4          out = O4
            //   9: O2→O3→O4, O1 free    out = O1 + O4
            //  10: O1→O2, O3→O4         out = O2 + O4
            //  11: O2→O3, O1/O4 free    out = O1 + O3 + O4
            const unsigned alg = 8u
                | (_ch[ch].conn != 0 ? 1u : 0u)
                | (_ch[s].conn != 0 ? 2u : 0u);

            // Stage 1: op1 (feedback on op1), op2 per algorithm. Two-tap
            // loop (ymfm channel::output): modulator phase offset from the
            // two previous op1 outputs — (fb0 + fb1) >> (10 - fb) in ymfm's
            // 10-bit phase domain, (fb0 + fb1) << (fbShift - 3) in this
            // 19-bit/×4-rail domain.
            int32_t mod = 0;
            if (_ch[ch].fbShift)
            {
                const int32_t taps = _fbHist[ch][0] + _fbHist[ch][1];
                mod = (_ch[ch].fbShift >= 3)
                    ? (taps << (_ch[ch].fbShift - 3))
                    : (taps >> (3 - _ch[ch].fbShift));
            }
            const int32_t o1 = OperatorOutput(_ops[opA], mod);
            _fbHist[ch][0] = _fbHist[ch][1];
            _fbHist[ch][1] = o1;
            const int32_t o2 = (alg & 1u)
                ? OperatorOutput(_ops[opB], 0)
                : OperatorOutput(_ops[opB], o1 << 6);
            // Stage 2: op3 modulated by op2 unless it is the free operator
            // (alg 10), op4 modulated by op3 unless free (alg 11).
            const int32_t o3 = (alg == 10u)
                ? OperatorOutput(_ops[opC], 0)
                : OperatorOutput(_ops[opC], o2 << 6);
            const int32_t o4 = (alg == 11u)
                ? OperatorOutput(_ops[opD], 0)
                : OperatorOutput(_ops[opD], o3 << 6);
            switch (alg)
            {
            case 9u:
                result = o1 + o4;
                break;
            case 10u:
                result = o2 + o4;
                break;
            case 11u:
                result = o1 + o3 + o4;
                break;
            default:
                result = o4;
                break;
            }
        }
        else if (_rhythm && (ch == 7 || ch == 8))
        {
            // Rhythm percussion ch7 (HH = op1 / SD = op2), ch8 (TOM = op1
            // sine direct / CY = op2): envelopes from the channel's two
            // operators (classic regs 0x31/0x32/0x34/0x35); HH/SD/CY are
            // noise-bit squares through the envelope (documented PoC
            // simplification).
            const auto noiseOut = [this](FmOperator& op) {
                const uint32_t index = static_cast<uint32_t>(op.envVol)
                    + (static_cast<uint32_t>(op.tl) << 3) + KslIndex(op);
                op.out = FmVolFactor((_noise & 2) ? 0x1000 : -0x1000, index);
                return op.out;
            };
            if (ch == 7) // HH (mod) + SD (car)
                result = noiseOut(_ops[op1]) + noiseOut(_ops[op2]);
            else // TOM (mod, sine direct) + CY (car)
                result = OperatorOutput(_ops[op1], 0) + noiseOut(_ops[op2]);
            _fbHist[ch] = {};
        }
        else
        {
            // 2-op: C0 bit 0 (CNT) selects FM (op1 modulates op2) or the
            // additive pair; two-tap feedback as in the 4-op stage 1 above.
            int32_t mod = 0;
            if (_ch[ch].fbShift)
            {
                const int32_t taps = _fbHist[ch][0] + _fbHist[ch][1];
                mod = (_ch[ch].fbShift >= 3)
                    ? (taps << (_ch[ch].fbShift - 3))
                    : (taps >> (3 - _ch[ch].fbShift));
            }
            const int32_t o1 = OperatorOutput(_ops[op1], mod);
            _fbHist[ch][0] = _fbHist[ch][1];
            _fbHist[ch][1] = o1;
            result = (_ch[ch].conn != 0)
                ? o1 + OperatorOutput(_ops[op2], 0)
                : OperatorOutput(_ops[op2], o1 << 6);
            _ops[op1].out = o1;
        }

        out.channel[ch] = result; // raw per-channel tap; FmBus routes and mutes
    }
}

void Opl4Fm::SaveState(uint8_t* dst) const
{
    size_t o = 0;
    std::memcpy(dst + o, _regs.data(), _regs.size()); o += _regs.size();
    for (const auto& op : _ops)
    {
        std::memcpy(dst + o, &op, sizeof(FmOperator));
        o += sizeof(FmOperator);
    }
    for (const auto& c : _ch)
    {
        std::memcpy(dst + o, &c, sizeof(FmChannel));
        o += sizeof(FmChannel);
    }
    std::memcpy(dst + o, _fbHist.data(), sizeof(_fbHist)); o += sizeof(_fbHist);
    uint8_t flags = 0;
    flags |= _rhythm ? 1 : 0;
    flags |= _newMode ? 2 : 0;
    std::memcpy(dst + o, &flags, 1); o += 1;
    std::memcpy(dst + o, &_egCnt, 8); o += 8;
    std::memcpy(dst + o, &_lfoPm, 4); o += 4;
    std::memcpy(dst + o, &_lfoAm, 4); o += 4;
    std::memcpy(dst + o, &_noise, 4); o += 4;
    std::memset(dst + o, 0, kStateSize - o);
}

void Opl4Fm::LoadState(const uint8_t* src)
{
    size_t o = 0;
    std::memcpy(_regs.data(), src + o, _regs.size()); o += _regs.size();
    for (auto& op : _ops)
    {
        std::memcpy(&op, src + o, sizeof(FmOperator));
        o += sizeof(FmOperator);
    }
    for (auto& c : _ch)
    {
        std::memcpy(&c, src + o, sizeof(FmChannel));
        o += sizeof(FmChannel);
    }
    std::memcpy(_fbHist.data(), src + o, sizeof(_fbHist)); o += sizeof(_fbHist);
    uint8_t flags = 0;
    std::memcpy(&flags, src + o, 1); o += 1;
    _rhythm = (flags & 1) != 0;
    _newMode = (flags & 2) != 0;
    std::memcpy(&_egCnt, src + o, 8); o += 8;
    std::memcpy(&_lfoPm, src + o, 4); o += 4;
    std::memcpy(&_lfoAm, src + o, 4); o += 4;
    std::memcpy(&_noise, src + o, 4); o += 4;
}

} // namespace opl4
