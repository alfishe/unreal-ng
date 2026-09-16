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
// modulation — full-scale ±2 cycles, the deep FM of the real chip. The
// 19-bit equivalent of op>>1 is op << 5, and the feedback shift base is 3
// (ymfm: (2·op1)>>(10-fb) ≈ op1>>(9-fb) table units = op1>>(3-fb) here:
// fb7 = ±1 cycle, fb1 = ±1/64 cycle).
#include "opl4fm.h"

#include <cstring>

namespace opl4
{

namespace
{

// First-quadrant sine, 256 entries, 13-bit scale (max 4096).
inline constexpr std::array<int16_t, 256> kSineTable = {
    0, 25, 50, 75, 101, 126, 151, 176,
    201, 226, 251, 276, 301, 326, 351, 376,
    401, 426, 451, 476, 501, 526, 551, 576,
    601, 626, 651, 675, 700, 725, 750, 774,
    799, 824, 848, 873, 897, 922, 946, 971,
    995, 1020, 1044, 1068, 1092, 1117, 1141, 1165,
    1189, 1213, 1237, 1261, 1285, 1309, 1332, 1356,
    1380, 1404, 1427, 1451, 1474, 1498, 1521, 1544,
    1567, 1591, 1614, 1637, 1660, 1683, 1706, 1729,
    1751, 1774, 1797, 1819, 1842, 1864, 1886, 1909,
    1931, 1953, 1975, 1997, 2019, 2041, 2062, 2084,
    2106, 2127, 2149, 2170, 2191, 2213, 2234, 2255,
    2276, 2296, 2317, 2338, 2359, 2379, 2399, 2420,
    2440, 2460, 2480, 2500, 2520, 2540, 2559, 2579,
    2598, 2618, 2637, 2656, 2675, 2694, 2713, 2732,
    2751, 2769, 2788, 2806, 2824, 2843, 2861, 2878,
    2896, 2914, 2932, 2949, 2967, 2984, 3001, 3018,
    3035, 3052, 3068, 3085, 3102, 3118, 3134, 3150,
    3166, 3182, 3198, 3214, 3229, 3244, 3260, 3275,
    3290, 3305, 3320, 3334, 3349, 3363, 3378, 3392,
    3406, 3420, 3433, 3447, 3461, 3474, 3487, 3500,
    3513, 3526, 3539, 3551, 3564, 3576, 3588, 3600,
    3612, 3624, 3636, 3647, 3659, 3670, 3681, 3692,
    3703, 3713, 3724, 3734, 3745, 3755, 3765, 3775,
    3784, 3794, 3803, 3812, 3822, 3831, 3839, 3848,
    3857, 3865, 3873, 3881, 3889, 3897, 3905, 3912,
    3920, 3927, 3934, 3941, 3948, 3954, 3961, 3967,
    3973, 3979, 3985, 3991, 3996, 4002, 4007, 4012,
    4017, 4022, 4027, 4031, 4036, 4040, 4044, 4048,
    4052, 4055, 4059, 4062, 4065, 4068, 4071, 4074,
    4076, 4079, 4081, 4083, 4085, 4087, 4088, 4090,
    4091, 4092, 4093, 4094, 4095, 4095, 4096, 4096,
};

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

// KSL: key scale level attenuation index contribution (OPL3 1.5/3/6 dB
// corner per block), 0.09375 dB units.

// PM LFO scale (YMF262): the 8192-step 6.04 Hz counter's top 3 bits index
// this bipolar F-number-fraction multiplier; the 0xBD bit 6 depth control
// halves the swing.
inline constexpr int8_t kPmScale[8] = {8, 4, 0, -4, -8, -4, 0, 4};

// YMF262 multiplier select (datasheet): non-linear at the top — MULT 11
// is x10, 13 is x12, 14 is x15 (11/13/14 duplicate their neighbours);
// MULT 0 is x0.5. Found by the conformance sweep against the ymfm
// reference (FmMultSweep): the linear step*mult model mis-pitches
// exactly these three settings.
inline constexpr uint8_t kMultTable[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};

// KSL attenuation (ymfm opl_key_scale_atten, YMF262 silicon): 0.75 dB
// units indexed by the 4 F-number MSBs, minus 8 per block below 7,
// clamped at 0; the 2-bit KSL field shifts the result (0 = off).
inline constexpr std::array<uint8_t, 16> kKslAtten = {
    0, 24, 32, 37, 40, 43, 45, 47, 48, 50, 51, 52, 53, 54, 55, 56};

// FM rate-index rows 0..3: the shared PCM table parks them on the infinity
// row (zero increments — YMF278 wave semantics where rate 0 freezes the
// stage). The OPL FM side creeps at the slowest cadence instead (ymfm
// k_rate_shift[0]=12 with the 0/1 increment row): AR/RR 0 still attacks /
// releases, just very slowly — the untouched-carrier silence finding.
inline uint8_t FmRateRow(uint8_t rate)
{
    return rate < 4 ? RateRow(0) : kEgRateSelect[rate];
}

} // namespace

void Opl4Fm::Reset()
{
    _regs.fill(0);
    _ops.fill(FmOperator{});
    _ch.fill(FmChannel{});
    _fbHist.fill(0);
    _status = 0;
    _timer1 = _timer2 = 0;
    _timer1Load = _timer2Load = 0;
    _timer1Enable = _timer2Enable = false;
    _timer1Mask = _timer2Mask = false;
    _rhythm = false;
    _newMode = false;
    _new2 = false;
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
    // KSR: 2-bit key-scale rate from block + fnum MSBs; added even at
    // register rate 0 — silicon rates 0..3 creep (FmRateRow), never freeze.
    const uint8_t ksr = (op.block << 1) | ((op.fnum >> 9) & 1);
    int r = regRate * 4 + (op.ksr ? ksr : 0);
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
    op.out = VolFactor(wave << 3, index);
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
        if (rate >= 63)
        {
            op.envVol = kMinAttIndex;
            op.egState = kFmEgDec;
            break;
        }
        const uint8_t shift = kEgRateShift[rate];
        if (!(_egCnt & ((1u << shift) - 1)))
        {
            const uint8_t select = FmRateRow(rate);
            const int32_t inc = (~op.envVol * kEgInc[select + ((_egCnt >> shift) & 7)]) >> 4;
            op.envVol = static_cast<int16_t>(op.envVol + inc);
            if (op.envVol <= kMinAttIndex)
            {
                op.envVol = kMinAttIndex;
                op.egState = kFmEgDec;
            }
        }
        break;
    }
    case kFmEgDec:
    {
        const uint8_t rate = EgRate(op, op.dr);
        const uint8_t shift = kEgRateShift[rate];
        if (!(_egCnt & ((1u << shift) - 1)))
        {
            const uint8_t select = FmRateRow(rate);
            op.envVol = static_cast<int16_t>(op.envVol + kEgInc[select + ((_egCnt >> shift) & 7)]);
            const int16_t sustainLevel = static_cast<int16_t>(op.sl << 4);
            if (op.envVol >= sustainLevel)
                op.egState = op.egt ? kFmEgSus : kFmEgSus;
        }
        break;
    }
    case kFmEgSus:
    {
        if (!op.egt)
        {
            // non-sustaining: decay continues toward silence at DR
            const uint8_t rate = EgRate(op, op.dr);
            const uint8_t shift = kEgRateShift[rate];
            if (!(_egCnt & ((1u << shift) - 1)))
            {
                const uint8_t select = FmRateRow(rate);
                op.envVol = static_cast<int16_t>(op.envVol + kEgInc[select + ((_egCnt >> shift) & 7)]);
                if (op.envVol >= kMaxAttIndex)
                {
                    op.envVol = kMaxAttIndex;
                    op.egState = kFmEgOff;
                }
            }
        }
        break;
    }
    case kFmEgRel:
    {
        const uint8_t rate = EgRate(op, op.rr);
        const uint8_t shift = kEgRateShift[rate];
        if (!(_egCnt & ((1u << shift) - 1)))
        {
            const uint8_t select = FmRateRow(rate);
            op.envVol = static_cast<int16_t>(op.envVol + kEgInc[select + ((_egCnt >> shift) & 7)]);
            if (op.envVol >= kMaxAttIndex)
            {
                op.envVol = kMaxAttIndex;
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
    if (on && !op.keyOn)
    {
        op.keyOn = true;
        op.envVol = kMaxAttIndex;
        op.egState = (EgRate(op, op.ar) >= 63) ? kFmEgDec : kFmEgAtt;
        if (op.egState == kFmEgDec)
            op.envVol = kMinAttIndex;
    }
    else if (!on && op.keyOn)
    {
        op.keyOn = false;
        op.egState = kFmEgRel;
    }
}

void Opl4Fm::UpdateChannelParams(uint8_t ch)
{
    // Refresh channel-level cached parameters from the register file.
    const uint8_t bankBase = (ch >= 9) ? 1 : 0; // register bank selector (x256)
    const uint8_t chReg = static_cast<uint8_t>(ch - (ch >= 9 ? 9 : 0));
    const uint16_t cData = _regs[(bankBase << 8) + 0xC0 + chReg];
    _ch[ch].route = cData & 0xF0; // CHA/CHB/CHC/CHD include enables; FB lives in bits 3:1
    _ch[ch].fbShift = (cData >> 1) & 7;
    _ch[ch].conn = cData & 0x01;
    const uint16_t regBase = static_cast<uint16_t>(bankBase << 8);
    const uint8_t bData = _regs[regBase + 0xB0 + chReg];
    const uint16_t fn = static_cast<uint16_t>(_regs[regBase + 0xA0 + chReg]
                                              | ((bData & 0x03) << 8));
    const uint8_t blk = (bData >> 2) & 7;
    _ops[_ch[ch].op1].fnum = fn;
    _ops[_ch[ch].op2].fnum = fn;
    _ops[_ch[ch].op1].block = blk;
    _ops[_ch[ch].op2].block = blk;
}

void Opl4Fm::WriteReg(uint8_t bank, uint8_t reg, uint8_t data)
{
    // YMF262 layout (§4.2): timers and 0xBD live on bank 0; "0x104"/"0x105"
    // are bank-1 addresses 0x04/0x05. Compatibility quirk modelled after
    // ymfm: until NEW (0x105) is set, bank-1 writes alias back to bank 0,
    // except 0x105 itself.
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
        case 0xBD:
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
        default:
            break;
        }
    }
    else
    {
        switch (reg)
        {
        case 0x04: // 0x104: 4-op connection select
            RebuildConnections();
            return;
        case 0x05: // 0x105: NEW (bit 0, OPL3 mode) / NEW2 (bit 1, OPL4)
            _newMode = (data & 1) != 0;
            _new2 = (data & 2) != 0;
            RebuildConnections();
            return;
        default:
            break;
        }
    }

    // Channel/operator registers: bank 0 covers channels 0..8, bank 1
    // channels 9..17. Operators are slot-linear (_ops[k] = register family
    // offset k, k 0..21; stride 22 per bank, the 0x26/0x27 and 0x2E/0x2F
    // gap slots stored but referenced by no channel).
    const int opBase = bank ? 22 : 0;
    const int chBase = bank ? 9 : 0;

    if (reg >= 0x20 && reg <= 0x35)
    {
        FmOperator& op = _ops[opBase + (reg - 0x20)];
        op.am = (data & 0x80) != 0;
        op.vib = (data & 0x40) != 0;
        op.egt = (data & 0x20) != 0;
        op.ksr = (data & 0x10) != 0;
        op.mult = data & 0x0F;
        return;
    }
    if (reg >= 0x40 && reg <= 0x55)
    {
        FmOperator& op = _ops[opBase + (reg - 0x40)];
        // YMF262 KSL encoding is non-monotonic (ymfm swaps the two bits):
        // reg 01 -> shift 2, reg 10 -> shift 1, reg 11 -> shift 3.
        const uint8_t kslBits = (data >> 6) & 3;
        op.ksl = static_cast<uint8_t>(((kslBits & 1) << 1) | (kslBits >> 1));
        op.tl = data & 0x3F;
        return;
    }
    if (reg >= 0x60 && reg <= 0x75)
    {
        FmOperator& op = _ops[opBase + (reg - 0x60)];
        op.ar = data >> 4;
        op.dr = data & 0x0F;
        return;
    }
    if (reg >= 0x80 && reg <= 0x95)
    {
        FmOperator& op = _ops[opBase + (reg - 0x80)];
        op.sl = data >> 4;
        op.rr = data & 0x0F;
        return;
    }
    if (reg >= 0xE0 && reg <= 0xF5)
    {
        _ops[opBase + (reg - 0xE0)].ws = data & 0x07;
        return;
    }
    if (reg >= 0xA0 && reg <= 0xA8)
    {
        const int ch = chBase + (reg - 0xA0);
        const uint16_t fn = static_cast<uint16_t>((_regs[(bank ? 256 : 0) + 0xB0 + (reg - 0xA0)] & 0x03) << 8
                                                  | data);
        _ops[_ch[ch].op1].fnum = fn;
        _ops[_ch[ch].op2].fnum = fn;
        return;
    }
    if (reg >= 0xB0 && reg <= 0xB8)
    {
        const int ch = chBase + (reg - 0xB0);
        UpdateChannelParams(static_cast<uint8_t>(ch));
        if (!(_rhythm && ch >= 6 && ch <= 8)) // rhythm claims bank-0 ch 6..8
        {
            KeyOn(_ops[_ch[ch].op1], (data & 0x20) != 0);
            KeyOn(_ops[_ch[ch].op2], (data & 0x20) != 0);
        }
        return;
    }
    if (reg >= 0xC0 && reg <= 0xC8)
    {
        const int ch = chBase + (reg - 0xC0);
        _ch[ch].route = data & 0xF0; // CHA/CHB/CHC/CHD include enables
        _ch[ch].fbShift = (data >> 1) & 7;
        _ch[ch].conn = data & 0x01;
        return;
    }
}

uint8_t Opl4Fm::ReadStatus() const
{
    return static_cast<uint8_t>(_status | 0x80); // bit 7 reads as 1
}

void Opl4Fm::AdvanceTimers()
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

void Opl4Fm::Advance(int32_t& outL, int32_t& outR,
                     std::array<int32_t, kChannelCount>& channelTaps)
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
        AdvanceEnvelope(op);
        op.phase = (op.phase + PhaseStep(op)) & 0x7FFFF;
    }

    outL = outR = 0;
    for (int ch = 0; ch < kChannelCount; ch++)
        channelTaps[ch] = 0;

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

            // Stage 1: op1 (feedback on op1), op2 per algorithm.
            int32_t mod = 0;
            if (_ch[ch].fbShift)
                mod = (_ch[ch].fbShift >= 3)
                    ? (_fbHist[ch] << (_ch[ch].fbShift - 3))
                    : (_fbHist[ch] >> (3 - _ch[ch].fbShift));
            const int32_t o1 = OperatorOutput(_ops[opA], mod);
            _fbHist[ch] = o1;
            const int32_t o2 = (alg & 1u)
                ? OperatorOutput(_ops[opB], 0)
                : OperatorOutput(_ops[opB], o1 << 5);
            // Stage 2: op3 modulated by op2 unless it is the free operator
            // (alg 10), op4 modulated by op3 unless free (alg 11).
            const int32_t o3 = (alg == 10u)
                ? OperatorOutput(_ops[opC], 0)
                : OperatorOutput(_ops[opC], o2 << 5);
            const int32_t o4 = (alg == 11u)
                ? OperatorOutput(_ops[opD], 0)
                : OperatorOutput(_ops[opD], o3 << 5);
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
                op.out = VolFactor((_noise & 2) ? 0x1000 : -0x1000, index);
                return op.out;
            };
            if (ch == 7) // HH (mod) + SD (car)
                result = noiseOut(_ops[op1]) + noiseOut(_ops[op2]);
            else // TOM (mod, sine direct) + CY (car)
                result = OperatorOutput(_ops[op1], 0) + noiseOut(_ops[op2]);
            _fbHist[ch] = 0;
        }
        else
        {
            // 2-op: C0 bit 0 (CNT) selects FM (op1 modulates op2) or the
            // additive pair; feedback loop has one-step delay.
            int32_t mod = 0;
            if (_ch[ch].fbShift)
                mod = (_ch[ch].fbShift >= 3)
                    ? (_fbHist[ch] << (_ch[ch].fbShift - 3))
                    : (_fbHist[ch] >> (3 - _ch[ch].fbShift));
            const int32_t o1 = OperatorOutput(_ops[op1], mod);
            _fbHist[ch] = o1;
            result = (_ch[ch].conn != 0)
                ? o1 + OperatorOutput(_ops[op2], 0)
                : OperatorOutput(_ops[op2], o1 << 5);
            _ops[op1].out = o1;
        }

        channelTaps[ch] = result;
        // Output routing (YMF262 silicon, include semantics): with NEW set,
        // C0 bits 4..7 enable CHA/CHB/CHC/CHD; the card sums pair A/C into
        // L and pair B/D into R (the adapter's MAME convention
        // L = out0+out2, R = out1+out3); all four clear = channel silent.
        // Without NEW both sides carry the channel (OPL2 compatibility).
        const uint8_t route = _newMode ? _ch[ch].route : 0x30;
        if (route & 0x10)
            outL += result; // CHA -> L
        if (route & 0x20)
            outR += result; // CHB -> R
        if (route & 0x40)
            outL += result; // CHC -> L (second stereo pair)
        if (route & 0x80)
            outR += result; // CHD -> R
    }

    AdvanceTimers();
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
    std::memcpy(dst + o, _fbHist.data(), _fbHist.size() * 4); o += _fbHist.size() * 4;
    std::memcpy(dst + o, &_status, 1); o += 1;
    std::memcpy(dst + o, &_timer1, 2); o += 2;
    std::memcpy(dst + o, &_timer2, 2); o += 2;
    std::memcpy(dst + o, &_timer1Load, 2); o += 2;
    std::memcpy(dst + o, &_timer2Load, 2); o += 2;
    uint8_t flags = 0;
    flags |= _timer1Enable ? 1 : 0;
    flags |= _timer2Enable ? 2 : 0;
    flags |= _timer1Mask ? 4 : 0;
    flags |= _timer2Mask ? 8 : 0;
    flags |= _rhythm ? 16 : 0;
    flags |= _newMode ? 32 : 0;
    flags |= _new2 ? 64 : 0;
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
    std::memcpy(_fbHist.data(), src + o, _fbHist.size() * 4); o += _fbHist.size() * 4;
    std::memcpy(&_status, src + o, 1); o += 1;
    std::memcpy(&_timer1, src + o, 2); o += 2;
    std::memcpy(&_timer2, src + o, 2); o += 2;
    std::memcpy(&_timer1Load, src + o, 2); o += 2;
    std::memcpy(&_timer2Load, src + o, 2); o += 2;
    uint8_t flags = 0;
    std::memcpy(&flags, src + o, 1); o += 1;
    _timer1Enable = (flags & 1) != 0;
    _timer2Enable = (flags & 2) != 0;
    _timer1Mask = (flags & 4) != 0;
    _timer2Mask = (flags & 8) != 0;
    _rhythm = (flags & 16) != 0;
    _newMode = (flags & 32) != 0;
    _new2 = (flags & 64) != 0;
    std::memcpy(&_egCnt, src + o, 8); o += 8;
    std::memcpy(&_lfoPm, src + o, 4); o += 4;
    std::memcpy(&_lfoAm, src + o, 4); o += 4;
    std::memcpy(&_noise, src + o, 4); o += 4;
}

} // namespace opl4
