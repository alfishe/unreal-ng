// cosim-ymfm — differential co-simulation of libopl4 against ymfm's
// ymf278b (§12.2/12.3 of the core TDD, pinned ref 81aec25).
//
// Both engines are driven from the SAME WaveMemory image and the same
// register scripts (the YMF278B PCM register map is identical in both
// engines, including the wave-number split 0x08+s / bit 0 of 0x20+s and
// the header table at headerBase*0x80000 + (wave-384)*12).
//
// Scenarios:
//   pcm-position   position traces decoded from the output streams (loop
//                 overrun, E=0 degenerate wrap, one-shot) — the §5.4 wrap
//                 logic must agree step for step
//   pcm-widths     8/12-bit sample-decode traces
//   pcm-envelope   statistical: 6 dB decay time + sustain plateau per
//                 engine, each normalised to its own unity reference
//   fm-tone        statistical: zero-crossing rate + RMS level (engines
//                 use different FM->output resampling; both chip-exact)
//   mix            block-mix register semantics: attenuation ratios for
//                 mix codes 0/3/7 must match (identical silicon table)
//   determinism    libopl4 stream reproduced bit-exactly on a second run
//
// Documented model differences (expected, not bugs; see cosim/README.md):
//   - power-table mantissa: libopl4 unity = triple (x*2047)>>11 chain
//     (envelope, TL, pan each round through the 2047-mantissa table);
//     ymfm PCM unity = (x*8168)>>15 (~1/4 scale — its documented "mixing
//     details need verification" path). Position decoding uses a per-
//     engine empirical scale, which removes both.
//   - ymfm's PCM fetch_sample ignores the fractional position bits (no
//     interpolation). Traces therefore use integer steps (fn 0); the
//     libopl4 interpolator is covered by VecInterpGolden in tests/.
//   - envelope clocking: libopl4 gates rows every 2^shift samples with
//     shift = 12-rate/4 (Valley Bell lineage, per the TDD); ymfm's 5.11
//     fractional counter nets 2^(11-rate/4) — measured exactly 2x faster
//     at mid rates (rate 32: 1024 vs 2048 samples per 6 dB). Both agree
//     at rate 0/15 and on sustain plateaus; compared statistically with a
//     [1.6, 2.4] band around the documented 2x.
//   - FM: libopl4 uses a linear operator map and resamples the 49516 Hz
//     FM grid to 44100 with its reducer; ymfm decimates 171/192 without
//     interpolation. Pitch is compared exactly (zero crossings of a pure
//     sine carrier); the absolute FM headroom differs by design (16-bit
//     chip stream vs ymfm's ~13-bit OPL3 core), so levels are compared per
//     engine via the TL ladder.
//
// Exit code: 0 = all scenarios passed, 1 = at least one failed.
#include "ymfm.h"
#include "ymfm_opl.h"

#include "cosimdrv.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace opl4;
using namespace opl4cosim;

namespace
{

int gScenarios = 0;
int gFailed = 0;
bool gDump = false;
std::string gDumpPrefix;

void ScenarioVerdict(const char* name, bool pass, const std::string& detail)
{
    gScenarios++;
    if (!pass)
        gFailed++;
    std::printf("[%s] %s%s%s\n", pass ? "PASS" : "FAIL", name,
                detail.empty() ? "" : "\n        ", detail.c_str());
}

// ---------------------------------------------------------------------------
// ymfm host: its PCM engine reads wavetable memory through the interface.
// ---------------------------------------------------------------------------
class YmfmHost final : public ymfm::ymfm_interface
{
public:
    WaveMemory* mem = nullptr;

    uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override
    {
        return (type == ymfm::ACCESS_PCM && mem != nullptr) ? mem->Read(address) : 0;
    }
};

// ---------------------------------------------------------------------------
// Drivers
// ---------------------------------------------------------------------------
// ymf278b::generate() produces six outputs per sample:
//   [0],[1] DO0: FM channels 2+3 (secondary FM pair)
//   [2],[3] DO1: wavetable channels 2+3 (PCM voices with the 0x68+s bit 4
//                output-select routed to the secondary pair)
//   [4],[5] DO2: mixed (fmout[0]*fmMix + pcmout[0]*pcmMix) >> 11
// The PCM engine's PRIMARY stereo pair (normal voices, pan applied) is not
// exposed directly: with FM silent and mix code 0, DO2-left is exactly the
// PCM left channel scaled by 2042/2048.
struct Streams
{
    std::vector<int16_t> do0L, do0R;
    std::vector<int16_t> do1L, do1R;
    std::vector<int16_t> mixL, mixR;
};

class RefChip // ymf278b
{
public:
    explicit RefChip(WaveMemory& mem)
    {
        _host.mem = &mem;
        _chip = std::make_unique<ymfm::ymf278b>(_host);
        _chip->reset();
    }

    void Fm(int bank, uint8_t reg, uint8_t data)
    {
        if (bank != 0)
            _chip->write_address_hi(reg);
        else
            _chip->write_address(reg);
        _chip->write_data(data);
    }

    void Pcm(uint8_t reg, uint8_t data)
    {
        _chip->write_address_pcm(reg);
        _chip->write_data_pcm(data);
    }

    Streams Run(int samples)
    {
        std::vector<ymfm::ymf278b::output_data> out(static_cast<size_t>(samples));
        _chip->generate(out.data(), static_cast<uint32_t>(out.size()));
        Streams s;
        s.do0L.reserve(out.size()); s.do0R.reserve(out.size());
        s.do1L.reserve(out.size()); s.do1R.reserve(out.size());
        s.mixL.reserve(out.size()); s.mixR.reserve(out.size());
        for (const auto& o : out)
        {
            s.do0L.push_back(static_cast<int16_t>(o.data[0]));
            s.do0R.push_back(static_cast<int16_t>(o.data[1]));
            s.do1L.push_back(static_cast<int16_t>(o.data[2]));
            s.do1R.push_back(static_cast<int16_t>(o.data[3]));
            s.mixL.push_back(static_cast<int16_t>(o.data[4]));
            s.mixR.push_back(static_cast<int16_t>(o.data[5]));
        }
        return s;
    }

private:
    YmfmHost _host;
    std::unique_ptr<ymfm::ymf278b> _chip;
};

// ---------------------------------------------------------------------------
// Analysis helpers
// ---------------------------------------------------------------------------
double Rms(const std::vector<int16_t>& v)
{
    if (v.empty())
        return 0.0;
    double acc = 0.0;
    for (int16_t x : v)
        acc += static_cast<double>(x) * x;
    return std::sqrt(acc / static_cast<double>(v.size()));
}

void DumpStream(const std::string& tag, const std::vector<int16_t>& v)
{
    if (!gDump)
        return;
    const std::string path = gDumpPrefix + "-" + tag + ".pcm";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f != nullptr)
    {
        std::fwrite(v.data(), sizeof(int16_t), v.size(), f);
        std::fclose(f);
    }
}

// Decode a stream back to sample-table positions. Both engines key on with
// an instant attack (AR 15) and hold full level (D1R/D2R 0, DL 0), so the
// stream is the table scaled by a per-engine constant: a triple (x*2047)>>11
// chain for libopl4 vs (x*8168)>>15 for ymfm. The scale is estimated from
// frame 0 — position 0 is deterministic at key-on in both engines (each
// outputs before advancing) — and every frame is decoded to the nearest
// table entry within a 4% tolerance (the 2047-mantissa chains deviate from
// a perfect scale by well under 1%).
struct Trace
{
    std::vector<int> pos; // table position per frame, -1 = off-table
    double scale = 1.0;
};

Trace DecodeTrace(const std::vector<int16_t>& out, const std::vector<int16_t>& table)
{
    Trace t;
    t.pos.assign(out.size(), -1);
    if (out.empty() || table.empty() || table[0] < 4096)
        return t;
    t.scale = out[0] / static_cast<double>(table[0]);
    if (t.scale <= 0.0)
        return t;
    for (size_t k = 0; k < out.size(); k++)
    {
        int best = -1;
        long bestErr = LONG_MAX;
        for (size_t p = 0; p < table.size(); p++)
        {
            const long want = std::lrint(t.scale * table[p]);
            const long err = std::labs(static_cast<long>(out[k]) - want);
            if (err < bestErr)
            {
                bestErr = err;
                best = static_cast<int>(p);
            }
        }
        const long want = std::lrint(t.scale * table[best]);
        if (bestErr * 100 <= 4 * std::max<long>(std::labs(want), 1))
            t.pos[k] = best;
    }
    return t;
}

// Compare two decoded position sequences: unknown (-1) runs at the ends are
// the lead-in/out (envelope quiet or past-table reads) and are trimmed; the
// remaining runs must match exactly.
bool ComparePositions(const Trace& ours, const Trace& ref, std::string& detail)
{
    const auto trim = [](const std::vector<int>& v)
    {
        size_t lo = 0, hi = v.size();
        while (lo < hi && v[lo] == -1)
            lo++;
        while (hi > lo && v[hi - 1] == -1)
            hi--;
        return std::vector<int>(v.begin() + static_cast<long>(lo),
                                v.begin() + static_cast<long>(hi));
    };
    const std::vector<int> a = trim(ours.pos);
    const std::vector<int> b = trim(ref.pos);
    char buf[160];
    std::snprintf(buf, sizeof buf, "%zu vs %zu frames (scale %.4f/%.4f)",
                  a.size(), b.size(), ours.scale, ref.scale);
    detail += buf;
    if (a.size() != b.size() || a.empty())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        if (a[i] != b[i])
        {
            std::snprintf(buf, sizeof buf, "; first mismatch @%zu: %d vs %d",
                          i, a[i], b[i]);
            detail += buf;
            return false;
        }
    }
    return true;
}

std::vector<int16_t> LeftOf(const std::vector<int16_t>& interleaved)
{
    std::vector<int16_t> l;
    for (size_t i = 0; i + 1 < interleaved.size(); i += 2)
        l.push_back(interleaved[i]);
    return l;
}

// ---------------------------------------------------------------------------
// Scenarios
// ---------------------------------------------------------------------------

// Position traces with distinct samples: both engines must produce the
// identical position sequence through the loop-end wrap semantics (§5.4).
void ScenarioPcmPosition()
{
    // 16-bit table with negatives and near-rails (big first entry so the
    // scale estimate from frame 0 is well conditioned).
    const int raw16[8] = {0x7C01, 0x8F12, 0x1234, static_cast<int>(0xFEDC),
                          0x0012, 0x5555, static_cast<int>(0xAAAA),
                          static_cast<int>(0xFE01)};
    uint8_t raw16be[16];
    for (int i = 0; i < 8; i++)
    {
        const uint16_t u = static_cast<uint16_t>(raw16[i]);
        raw16be[i * 2] = static_cast<uint8_t>(u >> 8);
        raw16be[i * 2 + 1] = static_cast<uint8_t>(u & 0xFF);
    }
    std::vector<int16_t> decoded16(raw16, raw16 + 8);

    // 256-entry distinct table for the wide traces.
    std::vector<int16_t> big(256);
    uint8_t bigbe[512];
    for (int i = 0; i < 256; i++)
    {
        big[i] = static_cast<int16_t>(0x4000 + i * 0x0111);
        const uint16_t u = static_cast<uint16_t>(big[i]);
        bigbe[i * 2] = static_cast<uint8_t>(u >> 8);
        bigbe[i * 2 + 1] = static_cast<uint8_t>(u & 0xFF);
    }

    struct Case
    {
        const char* name;
        int wave;
        uint8_t bits;
        uint32_t start;
        uint16_t loop, end;
        int oct; // step: oct 1 = 1.0, oct 2 = 2.0 (fraction always 0)
        int frames;
        const std::vector<int16_t>* tbl;
        const uint8_t* raw;
        uint32_t rawLen;
    };
    const Case cases[] = {
        // loop 2 / end 8 / step 2: 0, 2, 4, 6 -> wrap to 2 (overrun carried
        // into the loop, the audible glitch of the real chip)
        {"loop-overrun-step2", 384, 2, kSmp16, 2, 8, 2, 48, &decoded16, raw16be, 16},
        // E = 0 (header value 0): the wrap fires every step, so each
        // advance adds loopAddr on top of the increment: 0, 6, 12, ...
        {"end0-degenerate", 385, 2, kSmpBig, 5, 0, 1, 30, &big, bigbe, 512},
        // one-shot: end far past the table, loop 0 — straight count-up.
        {"one-shot", 386, 2, kSmpBig, 0, 0x1000, 1, 40, &big, bigbe, 512},
    };

    std::string detail;
    bool all = true;
    for (const Case& cs : cases)
    {
        OurChip oc;
        RefChip rc(oc.Mem());
        WriteHeader(oc.Mem(), HdrOf(cs.wave), cs.bits, cs.start, cs.loop, cs.end);
        oc.Mem().WriteSram(cs.start, cs.raw, cs.rawLen);
        oc.Mem().ClearDirty();

        PcmVoice v{0, cs.wave, static_cast<uint8_t>(cs.oct << 4), 0xF0, 0, 0xF0,
                   0x01, 0x80};
        BootNew(oc);
        BootNew(rc);
        KeyOnPcm(oc, v);
        KeyOnPcm(rc, v);

        const std::vector<int16_t> mine = LeftOf(oc.Run(cs.frames));
        const Streams rs = rc.Run(cs.frames);
        DumpStream(std::string("pos-") + cs.name + "-ours", mine);
        DumpStream(std::string("pos-") + cs.name + "-ymfm", rs.mixL);
        detail += "\n        ";
        all = ComparePositions(DecodeTrace(mine, *cs.tbl),
                               DecodeTrace(rs.mixL, *cs.tbl), detail)
            && all;
    }
    ScenarioVerdict("pcm-position", all, detail);
}

// 8/12-bit decode paths: the bit-unpacking must be byte-identical.
void ScenarioPcmWidths()
{
    const uint8_t raw8[8] = {0x7F, 0x00, 0x80, 0xFF, 0x55, 0xAA, 0x01, 0xFE};
    const uint8_t raw12[12] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                               0xDE, 0xF0, 0x0F, 0x1A, 0x2B, 0x3C};
    std::vector<int16_t> tbl8, tbl12;
    for (uint8_t b : raw8)
        tbl8.push_back(static_cast<int16_t>(b << 8));
    for (int p = 0; p < 8; p++)
    {
        const int a = (p / 2) * 3;
        tbl12.push_back(p & 1
                            ? static_cast<int16_t>((raw12[a + 2] << 8) | (raw12[a + 1] & 0xF0))
                            : static_cast<int16_t>((raw12[a] << 8) | ((raw12[a + 1] << 4) & 0xF0)));
    }

    struct Case
    {
        const char* name;
        int wave;
        uint8_t bits;
        const uint8_t* raw;
        const std::vector<int16_t>* tbl;
    };
    const Case cases[] = {
        {"width8", 387, 0, raw8, &tbl8},
        {"width12", 388, 1, raw12, &tbl12},
    };

    std::string detail;
    bool all = true;
    for (const Case& cs : cases)
    {
        OurChip oc;
        RefChip rc(oc.Mem());
        const uint32_t start = cs.bits == 0 ? kSmp8 : kSmp12;
        WriteHeader(oc.Mem(), HdrOf(cs.wave), cs.bits, start, 0, 8);
        oc.Mem().WriteSram(start, cs.raw, cs.bits == 0 ? 8 : 12);
        oc.Mem().ClearDirty();
        PcmVoice v{0, cs.wave, 0x10 /*oct 1 -> step 1*/, 0xF0, 0, 0xF0, 0x01, 0x80};
        BootNew(oc);
        BootNew(rc);
        KeyOnPcm(oc, v);
        KeyOnPcm(rc, v);
        const std::vector<int16_t> mine = LeftOf(oc.Run(24));
        const Streams rs = rc.Run(24);
        DumpStream(std::string("width-") + cs.name + "-ours", mine);
        DumpStream(std::string("width-") + cs.name + "-ymfm", rs.mixL);
        detail += "\n        ";
        all = ComparePositions(DecodeTrace(mine, *cs.tbl),
                               DecodeTrace(rs.mixL, *cs.tbl), detail)
            && all;
    }
    ScenarioVerdict("pcm-widths", all, detail);
}

// 6 dB decay time and sustain plateau, each normalised to the engine's own
// unity reference (removes the power-table scale difference). The envelope
// clocking models differ by 2x at mid rates (measured, see the header
// comment); the ratio band is [1.6, 2.4] around it and the plateau must
// agree within 3 dB.
void ScenarioPcmEnvelope()
{
    struct Side
    {
        int t6 = -1;      // samples to fall 6 dB below unity
        double susDb = 0; // sustain plateau relative to unity, dB
    };

    const auto run = [](bool decay, OurChip& oc, RefChip& rc)
    {
        WriteHeader(oc.Mem(), HdrOf(389), 2, kSmpEnv, 0, 64);
        uint8_t raw[128];
        for (int i = 0; i < 64; i++)
        {
            raw[i * 2] = 0x60;
            raw[i * 2 + 1] = 0x00;
        }
        oc.Mem().WriteSram(kSmpEnv, raw, 128);
        oc.Mem().ClearDirty();
        oc.Pcm(0xF8, 0x00);
        oc.Pcm(0xF9, 0x00);
        rc.Pcm(0xF8, 0x00);
        rc.Pcm(0xF9, 0x00);
        // decay run: AR 15 / D1R 8 (rate 32) -> DL 8 plateau; unity run:
        // AR 15 / D1R 0 (hold at full level).
        PcmVoice v{0, 389, 0x10,
                   static_cast<uint8_t>(decay ? 0xF8 : 0xF0), 0x80, 0xF0,
                   0x01, 0x80};
        v.dlD2r = decay ? 0x80 : 0x00;
        BootNew(oc);
        BootNew(rc);
        KeyOnPcm(oc, v);
        KeyOnPcm(rc, v);
        const std::vector<int16_t> mine = LeftOf(oc.Run(12000));
        const Streams rs = rc.Run(12000);
        return std::make_pair(mine, rs.mixL);
    };

    OurChip ocU, ocD;
    RefChip rcU(ocU.Mem()), rcD(ocD.Mem());
    auto [oursU, refU] = run(false, ocU, rcU);
    auto [oursD, refD] = run(true, ocD, rcD);
    DumpStream("env-ours", oursD);
    DumpStream("env-ymfm", refD);

    const auto profile = [](const std::vector<int16_t>& u, const std::vector<int16_t>& d)
    {
        Side s;
        const double unity = Rms(u);
        const int win = 128;
        for (size_t i = 0; i + win <= d.size(); i += win)
        {
            double acc = 0.0;
            for (size_t k = i; k < i + win; k++)
                acc += static_cast<double>(d[k]) * d[k];
            const double db = 10.0 * std::log10(acc / win / (unity * unity + 1e-30) + 1e-30);
            if (s.t6 < 0 && db <= -6.0)
                s.t6 = static_cast<int>(i);
        }
        const size_t tail = d.size() - std::min<size_t>(d.size(), 2000);
        s.susDb = 20.0 * std::log10(Rms(std::vector<int16_t>(d.begin() + static_cast<long>(tail), d.end()))
                                        / (unity + 1e-30) + 1e-30);
        return s;
    };

    const Side so = profile(oursU, oursD);
    const Side sr = profile(refU, refD);

    // Decay clocking diverges by design: the libopl4 envelope counter gates
    // rows every 2^(12-rate/4) samples (Valley Bell lineage, per the TDD)
    // while ymfm's 5.11 counter nets 2^(11-rate/4) — measured exactly 2x at
    // rate 32 (2048 vs 1024 samples per 6 dB). Accept [1.6, 2.4]; the
    // sustain plateau must still agree within 3 dB.
    const double tr = so.t6 / static_cast<double>(sr.t6);
    char buf[220];
    std::snprintf(buf, sizeof buf,
                  "6 dB time: ours=%d ymfm=%d samples (ratio %.2f); sustain: ours=%.1f dB ymfm=%.1f dB",
                  so.t6, sr.t6, tr, so.susDb, sr.susDb);
    const bool ok = so.t6 > 0 && sr.t6 > 0
        && tr >= 1.6 && tr <= 2.4
        && std::fabs(so.susDb - sr.susDb) <= 3.0;
    ScenarioVerdict("pcm-envelope", ok, buf);
}

// FM 2-op tone: same voice data, per-engine addresses (ymfm uses the
// classic OPL3 map: ch0 = ops {0,3}, carrier at 0x23; libopl4 is linear:
// 0x21) and per-engine 0xC0 routing semantics (ymfm needs the CHA/CHB
// output-select bits 0x30; libopl4 0x00 = both sides). Pitch is compared
// exactly via zero crossings; levels via the per-engine TL ladder.
void ScenarioFmTone()
{
    const auto run = [](OurChip& oc, RefChip& rc)
    {
        // unity block mix for both engines (libopl4 resets FM to -9 dB)
        oc.Pcm(0xF8, 0x00);
        oc.Pcm(0xF9, 0x00);
        rc.Pcm(0xF8, 0x00);
        rc.Pcm(0xF9, 0x00);
        BootNew(oc);
        BootNew(rc);
        // Modulator TL 0x3F (-47 dB): the carrier is an unmodulated sine, so
        // the zero-crossing rate IS the pitch. With a full-level modulator the
        // waveform is sin(phi + I*sin(phi)) whose crossing density depends on
        // the engine's internal modulation scaling — not comparable.
        const FmVoice v; // defaults: silent mod, carrier TL 0x10, fn 0x303 b2
        KeyOnFm(oc, v, true);
        KeyOnFm(rc, v, false);
        const std::vector<int16_t> mine = LeftOf(oc.Run(6000));
        const Streams rs = rc.Run(6000); // no PCM keyed: mix = FM tap
        // Carrier TL 0x10 -> 0x20 (-12 dB -> -24 dB): the second window's RMS
        // ratio must be ~0.251 in BOTH engines (TL ladder, 0.75 dB/step). The
        // absolute FM headroom differs by design (libopl4's 16-bit chip
        // stream vs ymfm's ~13-bit OPL3 core scale), so levels are compared
        // per engine, never across.
        oc.Fm(0, 0x41, 0x20);
        rc.Fm(0, 0x43, 0x20);
        const std::vector<int16_t> mine2 = LeftOf(oc.Run(3000));
        const Streams rs2 = rc.Run(3000);
        return std::make_tuple(mine, mine2, rs, rs2);
    };

    OurChip oc;
    RefChip rc(oc.Mem());
    auto [ol, ol2, rs, rs2] = run(oc, rc);
    DumpStream("fm-ours", ol);
    DumpStream("fm-ymfm", rs.mixL);

    const auto xrate = [](const std::vector<int16_t>& v)
    {
        double mean = 0;
        for (int16_t x : v)
            mean += x;
        mean /= static_cast<double>(v.size());
        int zc = 0;
        for (size_t i = 1; i < v.size(); i++)
            if ((v[i - 1] >= mean) != (v[i] >= mean))
                zc++;
        return zc / static_cast<double>(v.size());
    };

    const double zo = xrate(ol), zr = xrate(rs.mixL);
    const double tlO = Rms(ol2) / (Rms(ol) + 1e-30);
    const double tlR = Rms(rs2.mixL) / (Rms(rs.mixL) + 1e-30);
    char buf[240];
    std::snprintf(buf, sizeof buf,
                  "zero-cross/sample: ours=%.5f ymfm=%.5f (ratio %.4f); TL -12 dB step: ours %.3f ymfm %.3f",
                  zo, zr, zo / (zr + 1e-30), tlO, tlR);
    const bool freqOk = std::fabs(zo / (zr + 1e-30) - 1.0) < 0.02;
    const bool tlOk = std::fabs(tlO - 0.2512) <= 0.03
        && std::fabs(tlR - 0.2512) <= 0.03;
    const bool stereoOk = std::fabs(Rms(rs.mixL) - Rms(rs.mixR)) < 1.0;
    ScenarioVerdict("fm-tone", freqOk && tlOk && stereoOk, buf);
}

// Block-mix registers (0xF8 FM / 0xF9 PCM): both engines carry the same
// verified 11-bit silicon table {2042,1444,1021,722,510,361,255,0}. With
// FM+PCM keyed and both registers set to the same code, the attenuation
// RATIOS relative to code 0 must match between the engines (the absolute
// scales differ — ymfm's PCM bus is quarter-scale — but the mix semantics
// must not).
void ScenarioMix()
{
    OurChip oc;
    RefChip rc(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(390), 2, kSmpEnv, 0, 64);
    uint8_t raw[128];
    for (int i = 0; i < 64; i++)
    {
        raw[i * 2] = 0x50;
        raw[i * 2 + 1] = 0x00;
    }
    oc.Mem().WriteSram(kSmpEnv, raw, 128);
    oc.Mem().ClearDirty();
    BootNew(oc);
    BootNew(rc);

    PcmVoice v{0, 390, 0x10, 0xF0, 0, 0xF0, 0x21, 0x80}; // TL -12 dB
    KeyOnPcm(oc, v);
    KeyOnPcm(rc, v);

    // Modulator silent (pure sine carrier at TL -12 dB) and PCM at TL -12 dB:
    // the summed peak stays under the 16-bit rails, so the code-0 reference is
    // unclipped and the attenuation ratios are exact.
    const FmVoice fmv; // defaults: silent mod, carrier TL 0x10, fn 0x303 b2
    KeyOnFm(oc, fmv, true);
    KeyOnFm(rc, fmv, false);

    const int codes[] = {0, 3, 7};
    double ratioOurs[3] = {1, 0, 0}, ratioRef[3] = {1, 0, 0};
    double rmsOurs0 = 1, rmsRef0 = 1;
    std::string detail;
    for (int i = 0; i < 3; i++)
    {
        const uint8_t mix = static_cast<uint8_t>(codes[i] | (codes[i] << 3));
        oc.Pcm(0xF8, mix);
        oc.Pcm(0xF9, mix);
        rc.Pcm(0xF8, mix);
        rc.Pcm(0xF9, mix);
        const std::vector<int16_t> mine = LeftOf(oc.Run(1500));
        const Streams rs = rc.Run(1500);
        const double ro = Rms(mine), rr = Rms(rs.mixL);
        if (i == 0)
        {
            rmsOurs0 = ro;
            rmsRef0 = rr;
        }
        ratioOurs[i] = ro / (rmsOurs0 + 1e-30);
        ratioRef[i] = rr / (rmsRef0 + 1e-30);
        char buf[120];
        std::snprintf(buf, sizeof buf, "\n        code %d: ours %.4f ymfm %.4f",
                      codes[i], ratioOurs[i], ratioRef[i]);
        detail += buf;
        DumpStream(std::string("mix-") + std::to_string(codes[i]) + "-ours", mine);
        DumpStream(std::string("mix-") + std::to_string(codes[i]) + "-ymfm", rs.mixL);
    }

    bool ok = true;
    for (int i = 0; i < 3; i++)
        ok = ok && std::fabs(ratioOurs[i] - ratioRef[i]) <= 0.03;
    ok = ok && ratioOurs[2] < 0.001 && ratioRef[2] < 0.001; // code 7 = mute
    ScenarioVerdict("mix", ok, detail);
}

// §12.6: the libopl4 stream must reproduce bit-exactly on a second run.
void ScenarioDeterminism()
{
    OurChip a, b;
    for (OurChip* oc : {&a, &b})
    {
        WriteHeader(oc->Mem(), HdrOf(391), 2, kSmpBig, 2, 8);
        uint8_t raw[512];
        for (int i = 0; i < 256; i++)
        {
            const uint16_t u = static_cast<uint16_t>(0x4000 + i * 0x0111);
            raw[i * 2] = static_cast<uint8_t>(u >> 8);
            raw[i * 2 + 1] = static_cast<uint8_t>(u & 0xFF);
        }
        oc->Mem().WriteSram(kSmpBig, raw, 512);
        oc->Mem().ClearDirty();
        PcmVoice v{0, 391, 0x20 /*oct 2*/, 0xF5, 0x5A, 0xF3, 0x21, 0x85};
        BootNew(*oc);
        KeyOnPcm(*oc, v);
    }
    const std::vector<int16_t> first = a.Run(2000);
    const std::vector<int16_t> second = b.Run(2000);
    const bool ok = first.size() == second.size() && first.size() >= 4000
        && std::equal(first.begin(), first.end(), second.begin());
    ScenarioVerdict("determinism", ok,
                    ok ? "" : "  streams differ across identical runs");
}

} // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
        {
            gDump = true;
            gDumpPrefix = argv[++i];
        }
    }

    std::printf("cosim-ymfm: libopl4 vs ymfm ymf278b (pinned ref 81aec25)\n");
    ScenarioPcmPosition();
    ScenarioPcmWidths();
    ScenarioPcmEnvelope();
    ScenarioFmTone();
    ScenarioMix();
    ScenarioDeterminism();

    std::printf("cosim-ymfm: %d/%d scenarios passed\n", gScenarios - gFailed, gScenarios);
    return gFailed == 0 ? 0 : 1;
}
