// cosim-oracle — golden self-oracle for libopl4 (§12.2 tier 2).
//
// While cosim-ymfm compares libopl4 against ymfm numerically, this harness
// pins OUR OWN exact bit stream: every deterministic script's full chip
// output is reduced to an FNV-1a-64 digest and compared against the
// committed golden/oracle.txt. Any change that alters the stream — however
// innocent it looks in review — fails here and must be re-baselined
// consciously with --generate (and, if the behaviour was supposed to stay
// hardware-exact, re-validated against ymfm first).
//
// The committed golden file doubles as a cross-machine determinism check:
// cosim-ymfm's determinism scenario only proves two chips agree within one
// process; the golden digests must also reproduce on every platform/CI run.
//
// Usage (from cosim/):
//   cosim-oracle              compare against golden/oracle.txt
//   cosim-oracle --generate   (re)write golden/oracle.txt
//   cosim-oracle --verbose    print per-case digests while comparing
//
// Exit code: 0 = all cases match, 1 = any mismatch/missing/stale entry.
#include "cosimdrv.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace opl4cosim;

namespace
{

constexpr const char* kGoldenPath = "golden/oracle.txt";

// ---------------------------------------------------------------------------
// Digest
// ---------------------------------------------------------------------------
uint64_t Fnv1a(uint64_t h, const void* data, size_t n)
{
    const uint8_t* b = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++)
    {
        h ^= b[i];
        h *= 0x100000001B3ULL;
    }
    return h;
}

// Digest of a case stream: the interleaved int16 bytes plus the frame count
// (so a truncation to an all-zero prefix still flips the hash).
uint64_t Digest(const std::vector<int16_t>& stream)
{
    uint64_t h = 0xCBF29CE484222325ULL;
    h = Fnv1a(h, stream.data(), stream.size() * sizeof(int16_t));
    const uint32_t frames = static_cast<uint32_t>(stream.size() / 2);
    return Fnv1a(h, &frames, sizeof frames);
}

void Append(std::vector<int16_t>& dst, const std::vector<int16_t>& src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

// ---------------------------------------------------------------------------
// Shared SRAM tables (same layout the differential harness uses)
// ---------------------------------------------------------------------------
void WriteRaw16(WaveMemory& mem)
{
    const int raw16[8] = {0x7C01, 0x8F12, 0x1234, static_cast<int>(0xFEDC),
                          0x0012, 0x5555, static_cast<int>(0xAAAA),
                          static_cast<int>(0xFE01)};
    uint8_t be[16];
    for (int i = 0; i < 8; i++)
    {
        const uint16_t u = static_cast<uint16_t>(raw16[i]);
        be[i * 2] = static_cast<uint8_t>(u >> 8);
        be[i * 2 + 1] = static_cast<uint8_t>(u & 0xFF);
    }
    mem.WriteSram(kSmp16, be, 16);
}

void WriteBig(WaveMemory& mem)
{
    uint8_t be[512];
    for (int i = 0; i < 256; i++)
    {
        const uint16_t u = static_cast<uint16_t>(0x4000 + i * 0x0111);
        be[i * 2] = static_cast<uint8_t>(u >> 8);
        be[i * 2 + 1] = static_cast<uint8_t>(u & 0xFF);
    }
    mem.WriteSram(kSmpBig, be, 512);
}

// Constant-value envelope table (level measurements): 64 entries of `hi:00`.
void WriteEnv(WaveMemory& mem, uint8_t hi)
{
    uint8_t raw[128];
    for (int i = 0; i < 64; i++)
    {
        raw[i * 2] = hi;
        raw[i * 2 + 1] = 0x00;
    }
    mem.WriteSram(kSmpEnv, raw, 128);
}

// ---------------------------------------------------------------------------
// Cases — each returns the full interleaved chip stream
// ---------------------------------------------------------------------------

// §5.4 wrap semantics: loop 2 / end 8 / step 2 overruns into the loop.
std::vector<int16_t> CasePcmLoopOverrun()
{
    OurChip oc;
    WriteRaw16(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(384), 2, kSmp16, 2, 8);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 384, 0x20, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(48);
}

// E = 0 (stored complement 0): a full 64 KiB sample. openMSX-exact
// comparator (5426b4b1): pos + S never reaches 0x10000, so the slot
// counts up linearly — 0, 1, 2, ... (ymfm wraps every step instead; the
// corner is a documented model difference, see the cosim-ymfm.cpp
// header and cosim/README.md).
std::vector<int16_t> CasePcmEnd0Wrap()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(385), 2, kSmpBig, 5, 0);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 385, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(30);
}

std::vector<int16_t> CasePcmOneShot()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(386), 2, kSmpBig, 0, 0x1000);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 386, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(40);
}

std::vector<int16_t> CasePcmWidth8()
{
    OurChip oc;
    const uint8_t raw8[8] = {0x7F, 0x00, 0x80, 0xFF, 0x55, 0xAA, 0x01, 0xFE};
    oc.Mem().WriteSram(kSmp8, raw8, 8);
    WriteHeader(oc.Mem(), HdrOf(387), 0, kSmp8, 0, 8);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 387, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(24);
}

std::vector<int16_t> CasePcmWidth12()
{
    OurChip oc;
    const uint8_t raw12[12] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC,
                               0xDE, 0xF0, 0x0F, 0x1A, 0x2B, 0x3C};
    oc.Mem().WriteSram(kSmp12, raw12, 12);
    WriteHeader(oc.Mem(), HdrOf(388), 1, kSmp12, 0, 8);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 388, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(24);
}

// AR 15 / D1R 8 (rate 32) / DL 8: decay to the sustain plateau.
std::vector<int16_t> CasePcmDecaySustain()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(389), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    oc.Pcm(0xF8, 0x00);
    oc.Pcm(0xF9, 0x00);
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 389, 0x10, 0xF8, 0x80, 0xF0, 0x01, 0x80});
    return oc.Run(12000);
}

// Hold at full level, key off, release at RR 5.
std::vector<int16_t> CasePcmRelease()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(389), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    oc.Pcm(0xF8, 0x00);
    oc.Pcm(0xF9, 0x00);
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 389, 0x10, 0xF0, 0x00, 0x05, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(3000);
    oc.Pcm(0x68, 0x80); // key off (pan preserved, KEY bit cleared)
    Append(out, oc.Run(6000));
    return out;
}

// Pure-sine carrier (silent modulator) + a TL step mid-run, mirroring the
// differential fm-tone scenario.
std::vector<int16_t> CaseFmTone()
{
    OurChip oc;
    oc.Pcm(0xF8, 0x00);
    oc.Pcm(0xF9, 0x00);
    BootNew(oc);
    KeyOnFm(oc, FmVoice{}, true);
    std::vector<int16_t> out = oc.Run(6000);
    oc.Fm(0, 0x43, 0x20); // carrier TL -12 dB -> -24 dB
    Append(out, oc.Run(3000));
    return out;
}

// Vibrato: carrier VIB bit + deep depth (0xBD bit 6), ~3 PM LFO periods.
std::vector<int16_t> CaseFmVibrato()
{
    OurChip oc;
    BootNew(oc);
    KeyOnFm(oc, FmVoice{}, true);
    oc.Fm(0, 0x23, 0x41); // carrier: VIB on, mult 1
    oc.Fm(0, 0xBD, 0x40); // deep vibrato depth
    return oc.Run(20000);
}

// Tremolo: carrier AM bit + deep depth (0xBD bit 7), ~2 AM LFO periods.
std::vector<int16_t> CaseFmTremolo()
{
    OurChip oc;
    BootNew(oc);
    KeyOnFm(oc, FmVoice{}, true);
    oc.Fm(0, 0x23, 0x81); // carrier: AM on, mult 1
    oc.Fm(0, 0xBD, 0x80); // deep AM depth
    return oc.Run(20000);
}

// FM key off: B0 bit 5 cleared, release at RR 5.
std::vector<int16_t> CaseFmRelease()
{
    OurChip oc;
    BootNew(oc);
    FmVoice v;
    v.slrr = 0x05; // SL 0, RR 5
    KeyOnFm(oc, v, true);
    std::vector<int16_t> out = oc.Run(3000);
    oc.Fm(0, 0xB0, 0x0B); // key off (fn hi + block preserved)
    Append(out, oc.Run(6000));
    return out;
}

// Full block-mix sweep: codes 0..7 on both 0xF8 (FM) and 0xF9 (PCM) with
// FM+PCM keyed at the anti-clipping levels of the differential mix scenario.
std::vector<int16_t> CaseMixSweep()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x50);
    WriteHeader(oc.Mem(), HdrOf(390), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 390, 0x10, 0xF0, 0, 0xF0, 0x21, 0x80});
    KeyOnFm(oc, FmVoice{}, true);
    std::vector<int16_t> out;
    for (int code = 0; code < 8; code++)
    {
        const uint8_t mix = static_cast<uint8_t>(code | (code << 3));
        oc.Pcm(0xF8, mix);
        oc.Pcm(0xF9, mix);
        Append(out, oc.Run(600));
    }
    return out;
}

// Four PCM voices (distinct rate/TL/pan) + FM: the 24-voice mix bus.
std::vector<int16_t> CaseMultiVoice()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(392), 2, kSmpBig, 2, 8);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 392, 0x10, 0xF0, 0x00, 0xF0, 0x01, 0x20});
    KeyOnPcm(oc, PcmVoice{1, 392, 0x20, 0xF0, 0x00, 0xF0, 0x05, 0x50});
    KeyOnPcm(oc, PcmVoice{2, 392, 0x30, 0xF0, 0x00, 0xF0, 0x09, 0x80});
    KeyOnPcm(oc, PcmVoice{3, 392, 0x10, 0xF0, 0x00, 0xF0, 0x0D, 0xB0});
    KeyOnFm(oc, FmVoice{}, true);
    return oc.Run(3000);
}

// Save/restore replay: the stream produced after LoadState must equal the
// original continuation, sample for sample. The digest pins both halves;
// HalvesEqual additionally fails the case outright if they ever diverge.
std::vector<int16_t> CaseSaveRestore()
{
    const auto setup = [](OurChip& oc)
    {
        WriteBig(oc.Mem());
        WriteHeader(oc.Mem(), HdrOf(391), 2, kSmpBig, 2, 8);
        oc.Mem().ClearDirty();
        BootNew(oc);
        KeyOnPcm(oc, PcmVoice{0, 391, 0x20, 0xF5, 0x5A, 0xF3, 0x21, 0x85});
    };
    OurChip a, b;
    setup(a);
    setup(b);
    (void)a.Run(2000);
    std::vector<uint8_t> state(a.Chip().StateSize());
    a.Chip().SaveState(state.data());
    // The snapshot carries absolute chip time (masterPos); the restoring
    // host must keep feeding timestamps from that point. Burn the same
    // lead-in on b so its driver clock matches the restored masterPos
    // (LoadState also clears b's delivery streams).
    (void)b.Run(2000);
    b.Chip().LoadState(state.data());
    const std::vector<int16_t> tailA = a.Run(2000);
    const std::vector<int16_t> tailB = b.Run(2000);
    std::vector<int16_t> out = tailA;
    Append(out, tailB);
    return out;
}

bool HalvesEqual(const std::vector<int16_t>& out)
{
    return out.size() % 2 == 0
        && std::equal(out.begin(), out.begin() + static_cast<long>(out.size() / 2),
                      out.begin() + static_cast<long>(out.size() / 2));
}

// ---------------------------------------------------------------------------
// Tier-2 expansion (conformance plan §12.2): one digest per opl4sweep
// family over a representative sub-sweep. Same register addressing the
// sweeps use: the canonical YMF262 map (ch0 mod 0x20 / car 0x23), with
// include-semantics 0xC0 routing (0x30 = CHA+CHB).
// ---------------------------------------------------------------------------

// Extended FM voice: adds the bits FmVoice carries implicitly as constants
// (wave select, AM/VIB enables, feedback/routing) so the sweeps' families
// each pin one stream.
struct FmPatch
{
    uint8_t multMod = 0x01;
    uint8_t multCar = 0x01;
    uint8_t tlMod = 0x3F; // silent modulator by default: pure carrier
    uint8_t tlCar = 0x10;
    uint8_t ardrMod = 0xF0;
    uint8_t ardrCar = 0xF0;
    uint8_t slrrMod = 0x00;
    uint8_t slrrCar = 0x00;
    uint8_t flagsCar = 0x00; // AM(7) VIB(6) EGT(5) KSR(4) + MULT(3..0)
    uint8_t wsCar = 0x00;
    uint8_t fbRoute = 0x00;  // FB(3..1) + CON(0)
    uint8_t fnLo = 0x03;
    uint8_t b0 = 0x2B; // F-num hi + block 2 + key on
};

void KeyOnFmPatch(OurChip& oc, const FmPatch& p)
{
    oc.Fm(0, 0x20, p.multMod);
    oc.Fm(0, 0x23, static_cast<uint8_t>(p.multCar | p.flagsCar));
    oc.Fm(0, 0x40, p.tlMod);
    oc.Fm(0, 0x43, p.tlCar);
    oc.Fm(0, 0x60, p.ardrMod);
    oc.Fm(0, 0x63, p.ardrCar);
    oc.Fm(0, 0x80, p.slrrMod);
    oc.Fm(0, 0x83, p.slrrCar);
    oc.Fm(0, 0xE3, p.wsCar);
    oc.Fm(0, 0xA0, p.fnLo);
    oc.Fm(0, 0xC0, static_cast<uint8_t>(0x30 | p.fbRoute)); // CHA+CHB + FB/CON
    oc.Fm(0, 0xB0, p.b0);
}

// FmTlLadderSweep: carrier TL 0..120 in -6 dB steps, mid-run rewrites.
std::vector<int16_t> CaseFmTlLadder()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.flagsCar = 0x20; // EGT: hold at full attack
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out = oc.Run(300);
    for (int tl = 0; tl <= 120; tl += 8)
    {
        oc.Fm(0, 0x43, static_cast<uint8_t>(tl));
        Append(out, oc.Run(200));
    }
    return out;
}

// FmTlLadderSweep modulator half: FM index ladder with feedback off.
std::vector<int16_t> CaseFmModTl()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.tlMod = 0x00;
    p.flagsCar = 0x20;
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out = oc.Run(300);
    for (int tl = 0; tl <= 48; tl += 6)
    {
        oc.Fm(0, 0x40, static_cast<uint8_t>(tl));
        Append(out, oc.Run(250));
    }
    return out;
}

// FmMultSweep: carrier MULT 0..15 (0 = 0.5x), 200 frames each.
std::vector<int16_t> CaseFmMult()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.flagsCar = 0x20;
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out = oc.Run(300);
    for (int m = 0; m <= 15; m++)
    {
        oc.Fm(0, 0x23, static_cast<uint8_t>(0x20 | m));
        Append(out, oc.Run(200));
    }
    return out;
}

// FmKslSweep: KSL 0..3 x block 2..5 stepping (carrier 0x43 bits 7..6).
std::vector<int16_t> CaseFmKsl()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.tlCar = 0x18;
    p.flagsCar = 0x20;
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out = oc.Run(300);
    for (int ksl = 0; ksl <= 3; ksl++)
        for (int block = 2; block <= 5; block++)
        {
            oc.Fm(0, 0x43, static_cast<uint8_t>((ksl << 6) | 0x18));
            oc.Fm(0, 0xB0, static_cast<uint8_t>(0x0B | (block << 2)));
            Append(out, oc.Run(200));
        }
    return out;
}

// FmEnvStageSweep attack half: AR 6..13 from silence (fresh key-ons).
std::vector<int16_t> CaseFmEnvAttack()
{
    std::vector<int16_t> out;
    for (int ar = 6; ar <= 13; ar++)
    {
        OurChip oc;
        BootNew(oc);
        FmPatch p;
        p.flagsCar = 0x20;
        p.ardrCar = static_cast<uint8_t>((ar << 4) | 2);
        KeyOnFmPatch(oc, p);
        Append(out, oc.Run(700));
    }
    return out;
}

// FmEnvStageSweep decay half: DR 2..9 into SL 4, EGT 0 (decay enabled).
std::vector<int16_t> CaseFmEnvDecay()
{
    std::vector<int16_t> out;
    for (int dr = 2; dr <= 9; dr++)
    {
        OurChip oc;
        BootNew(oc);
        FmPatch p;
        p.flagsCar = 0x00;
        p.ardrCar = static_cast<uint8_t>(0xF0 | dr);
        p.slrrCar = 0x40; // SL 4, RR 0
        KeyOnFmPatch(oc, p);
        Append(out, oc.Run(900));
    }
    return out;
}

// FmEnvStageSweep release half: hold, key off, RR 4..12.
std::vector<int16_t> CaseFmEnvRelease()
{
    std::vector<int16_t> out;
    for (int rr = 4; rr <= 12; rr += 2)
    {
        OurChip oc;
        BootNew(oc);
        FmPatch p;
        p.flagsCar = 0x20;
        p.slrrCar = static_cast<uint8_t>(rr);
        KeyOnFmPatch(oc, p);
        Append(out, oc.Run(400));
        oc.Fm(0, 0xB0, 0x0B); // key off (block/fn preserved)
        Append(out, oc.Run(900));
    }
    return out;
}

// FmFeedbackSweep: FB 1..7 stepped on the modulator feedback path.
std::vector<int16_t> CaseFmFeedback()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.tlMod = 0x3F; // feedback only: modulator stays out of the mix
    p.flagsCar = 0x20;
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out = oc.Run(300);
    for (int fb = 1; fb <= 7; fb++)
    {
        oc.Fm(0, 0xC0, static_cast<uint8_t>(0x30 | (fb << 1)));
        Append(out, oc.Run(300));
    }
    return out;
}

// Fm4OpConnections: ch0+ch3 pair, the four master/slave CON combos.
std::vector<int16_t> CaseFm4Op()
{
    std::vector<int16_t> out;
    for (int combo = 0; combo < 4; combo++)
    {
        OurChip oc;
        BootNew(oc);
        oc.Fm(1, 0x04, 0x01); // pair ch0 + ch3
        // op1..op4 = ch0 mod/car (0x20/0x23), ch3 mod/car (0x28/0x2B) —
        // the classic map's four cascade slots.
        const int tl[4] = {0x00, 0x10, 0x20, 0x30};
        for (int i = 0; i < 4; i++)
        {
            const uint8_t base = static_cast<uint8_t>(0x20 + 3 * (i & 1) + 8 * (i >> 1));
            oc.Fm(0, base, 0x21); // EGT hold, mult 1
            oc.Fm(0, static_cast<uint8_t>(base + 0x20), static_cast<uint8_t>(tl[i]));
            oc.Fm(0, static_cast<uint8_t>(base + 0x40), 0xF0); // AR 15
            oc.Fm(0, static_cast<uint8_t>(base + 0x60), 0x0F); // RR 15
        }
        oc.Fm(0, 0xA0, 0x46);
        oc.Fm(0, 0xB0, 0x32);
        oc.Fm(0, 0xA3, 0x23); // slave one octave down
        oc.Fm(0, 0xB3, 0x31);
        oc.Fm(0, 0xC0, static_cast<uint8_t>(0x30 | (combo & 1)));
        oc.Fm(0, 0xC3, static_cast<uint8_t>(0x30 | (combo >> 1)));
        Append(out, oc.Run(1200));
    }
    return out;
}

// FmRhythmSweep: the five 0xBD voices, on then off.
std::vector<int16_t> CaseFmRhythm()
{
    OurChip oc;
    BootNew(oc);
    const int chs[5] = {6, 7, 8, 7, 8};
    const uint8_t bits[5] = {0x10, 0x08, 0x04, 0x01, 0x02};
    for (int v = 0; v < 5; v++)
    {
        // Classic rhythm slots: ch6 pair 0x30/0x33 (BD), ch7 0x31/0x34
        // (HH/SD), ch8 0x32/0x35 (TOM/CY) — mod at 0x2A + ch, car +3.
        const uint8_t base = static_cast<uint8_t>(0x2A + chs[v]);
        oc.Fm(0, base, 0x21);
        oc.Fm(0, static_cast<uint8_t>(base + 3), 0x21);
        oc.Fm(0, static_cast<uint8_t>(base + 0x20), 0x00);
        oc.Fm(0, static_cast<uint8_t>(base + 0x23), 0x00);
        oc.Fm(0, static_cast<uint8_t>(base + 0x40), 0xF0);
        oc.Fm(0, static_cast<uint8_t>(base + 0x43), 0xF0);
        oc.Fm(0, static_cast<uint8_t>(base + 0x60), 0x0F);
        oc.Fm(0, static_cast<uint8_t>(base + 0x63), 0x0F);
        oc.Fm(0, static_cast<uint8_t>(0xA0 + chs[v]), 0x46);
        oc.Fm(0, static_cast<uint8_t>(0xB0 + chs[v]), 0x12);
        oc.Fm(0, static_cast<uint8_t>(0xC0 + chs[v]), 0x30);
    }
    std::vector<int16_t> out;
    for (int v = 0; v < 5; v++)
    {
        oc.Fm(0, 0xBD, static_cast<uint8_t>(0x20 | bits[v]));
        Append(out, oc.Run(1000));
        oc.Fm(0, 0xBD, 0x20);
        Append(out, oc.Run(1000));
    }
    return out;
}

// FmWaveformSweep: carrier WS 0..7 stepped.
std::vector<int16_t> CaseFmWaveform()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.flagsCar = 0x20;
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out = oc.Run(300);
    for (int ws = 0; ws <= 7; ws++)
    {
        oc.Fm(0, 0xE3, static_cast<uint8_t>(ws));
        Append(out, oc.Run(250));
    }
    return out;
}

// FmAmVibDepthMatrix: AM/VIB enables x 0xBD depth bits, 4 combos.
std::vector<int16_t> CaseFmAmVibMatrix()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.flagsCar = 0x20;
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out;
    const uint8_t combos[4][2] = {
        {0xC0, 0x00}, {0xC0, 0xC0}, {0x80, 0x80}, {0x40, 0x40}};
    for (const auto& c : combos)
    {
        oc.Fm(0, 0x23, static_cast<uint8_t>(0x20 | c[0]));
        oc.Fm(0, 0xBD, c[1]);
        Append(out, oc.Run(2400)); // ~2 AM LFO periods at deep rate
    }
    return out;
}

// FmRoutingMatrix: 0xC0 CHA/CHB/CHC/CHD 16 combos (include semantics:
// bits 4..7 enable, all clear = silent).
std::vector<int16_t> CaseFmRouting()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.flagsCar = 0x20;
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out = oc.Run(300);
    for (int route = 0; route < 16; route++)
    {
        oc.Fm(0, 0xC0, static_cast<uint8_t>(route << 4));
        Append(out, oc.Run(150));
    }
    return out;
}

// FmTimerSweep: T1/T2 fire, mask, reset — status bytes appended to the
// stream (audio is silent; the digest pins flag timing).
std::vector<int16_t> CaseFmTimers()
{
    OurChip oc;
    BootNew(oc);
    Opl4& chip = oc.Chip();
    std::vector<int16_t> out;
    const auto arm = [&](uint8_t tSel) {
        oc.Fm(0, 0x02, 0xF0);
        oc.Fm(0, 0x03, 0xF0);
        oc.Fm(0, 0x04, tSel);
    };
    arm(0x01);
    Append(out, oc.Run(128));
    out.push_back(static_cast<int16_t>(chip.ReadStatus(oc.Clock())));
    arm(0x41); // masked re-arm after reset
    Append(out, oc.Run(128));
    out.push_back(static_cast<int16_t>(chip.ReadStatus(oc.Clock())));
    arm(0x02);
    Append(out, oc.Run(512));
    out.push_back(static_cast<int16_t>(chip.ReadStatus(oc.Clock())));
    arm(0x22); // T2 masked
    Append(out, oc.Run(512));
    out.push_back(static_cast<int16_t>(chip.ReadStatus(oc.Clock())));
    arm(0x03);
    Append(out, oc.Run(512));
    out.push_back(static_cast<int16_t>(chip.ReadStatus(oc.Clock())));
    oc.Fm(0, 0x04, 0x83); // reset both + keep enabled
    Append(out, oc.Run(16));
    out.push_back(static_cast<int16_t>(chip.ReadStatus(oc.Clock())));
    return out;
}

// FmKonMomentary: B0 kon bit toggled every 64 frames x16 (re-attacks).
std::vector<int16_t> CaseFmKonEdge()
{
    OurChip oc;
    BootNew(oc);
    FmPatch p;
    p.flagsCar = 0x20;
    p.ardrCar = 0xFA; // fast-ish attack for visible edges
    KeyOnFmPatch(oc, p);
    std::vector<int16_t> out;
    for (int i = 0; i < 16; i++)
    {
        oc.Fm(0, 0xB0, i & 1 ? 0x2B : 0x0B);
        Append(out, oc.Run(64));
    }
    return out;
}

// EGT hold vs decay and KSR rate scaling, four flag combinations.
std::vector<int16_t> CaseFmEgtKsr()
{
    std::vector<int16_t> out;
    const uint8_t flags[4] = {0x00, 0x20, 0x10, 0x30};
    for (uint8_t f : flags)
    {
        OurChip oc;
        BootNew(oc);
        FmPatch p;
        p.flagsCar = f;
        p.ardrCar = 0x54; // AR 5 DR 4: visible without rate scaling
        p.fnLo = 0x03;
        p.b0 = 0x34;      // kon, block 5: KSR doubles the rates when enabled
        KeyOnFmPatch(oc, p);
        Append(out, oc.Run(1500));
    }
    return out;
}

// Bus behaviour: register readback and status at increasing absolute times
// (pins LD busy timing and the readable register file).
std::vector<int16_t> CaseBusStatus()
{
    OurChip oc;
    WriteRaw16(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(384), 2, kSmp16, 2, 8);
    oc.Mem().ClearDirty();
    oc.Pcm(0xF8, 0x24);
    oc.Pcm(0xF9, 0x49);
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 384, 0x20, 0xF0, 0x00, 0xF0, 0x01, 0x80});
    KeyOnFm(oc, FmVoice{}, true);
    std::vector<int16_t> out = oc.Run(1500);
    Opl4& chip = oc.Chip();
    const uint64_t t0 = oc.Clock();
    const uint8_t rd[] = {
        chip.ReadStatus(t0 + 1),
        chip.ReadWave(t0 + 768, 0x02),  // header-base reg written at key-on
        chip.ReadWave(t0 + 768, 0x08),  // wave number low
        chip.ReadWave(t0 + 1536, 0x38), // oct/f-number
        chip.ReadWave(t0 + 1536, 0x50), // TL
        chip.ReadWave(t0 + 1536, 0x68), // pan/key
        chip.ReadWave(t0 + 2304, 0xF8), // FM mix
        chip.ReadWave(t0 + 2304, 0xF9), // PCM mix
        chip.ReadStatus(t0 + 5000),
        chip.ReadStatus(t0 + 500000),
    };
    for (uint8_t x : rd)
        out.push_back(static_cast<int16_t>(x));
    return out;
}

// PcmWaveNumberBoundary: linear vs banked headers, base switching.
std::vector<int16_t> CasePcmWaveBank()
{
    std::vector<int16_t> out;
    struct W
    {
        int wave;
        uint8_t hdrSel; // 0x02 value: (header base) << 2
        uint32_t hdrAt;
    };
    const W ws[] = {
        {383, 0x10, 383 * 12},                    // < 384 stays linear
        {384, 0x10, 0x200000},                    // base 4: banked
        {384, 0x00, 384 * 12},                    // base 0: linear
        {400, 0x08, 0x100000 + (400 - 384) * 12}, // base 2
        {400, 0x14, 0x280000 + (400 - 384) * 12}, // base 5
        {100, 0x1C, 100 * 12},                    // < 384 ignores base
    };
    for (const W& w : ws)
    {
        OurChip oc;
        const uint8_t dc[2] = {0x7F, 0xFF};
        oc.Mem().WriteSram(0x200100, dc, 2);
        WriteHeader(oc.Mem(), w.hdrAt, 2, 0x200100, 0, 4);
        oc.Mem().ClearDirty();
        BootNew(oc);
        oc.Pcm(0x02, w.hdrSel);
        oc.Pcm(0x20, 0x01);
        oc.Pcm(0x08, static_cast<uint8_t>(w.wave & 0xFF));
        oc.Pcm(0x38, 0x07);
        oc.Pcm(0x98, 0xF0);
        oc.Pcm(0xB0, 0x00);
        oc.Pcm(0xC8, 0x00);
        oc.Pcm(0x50, 0x01);
        oc.Pcm(0x68, 0x80);
        Append(out, oc.Run(256));
    }
    return out;
}

// PcmStepSweep: OCT -4..+3 at FNUM 1023 on the 256-entry ramp.
std::vector<int16_t> CasePcmOctFnum()
{
    std::vector<int16_t> out;
    for (int oct = -4; oct <= 3; oct++)
    {
        OurChip oc;
        WriteBig(oc.Mem());
        WriteHeader(oc.Mem(), HdrOf(393), 2, kSmpBig, 0, 0x1000);
        oc.Mem().ClearDirty();
        BootNew(oc);
        KeyOnPcm(oc, PcmVoice{0, 393,
                              static_cast<uint8_t>(((oct & 0xF) << 4) | 7),
                              0xF0, 0, 0xF0, 0x01, 0x80});
        Append(out, oc.Run(48));
    }
    return out;
}

// PcmStepSweep fnum grid: OCT 0 x FNUM 0/256/512/1023 (custom fn bytes
// need direct writes — KeyOnPcm hardcodes fnLo 0).
std::vector<int16_t> CasePcmFnumGrid()
{
    std::vector<int16_t> out;
    const int fns[4] = {0, 256, 512, 1023};
    for (int fn : fns)
    {
        OurChip oc;
        WriteBig(oc.Mem());
        WriteHeader(oc.Mem(), HdrOf(393), 2, kSmpBig, 0, 0x1000);
        oc.Mem().ClearDirty();
        BootNew(oc);
        oc.Pcm(0x02, 0x10);
        oc.Pcm(0x20, static_cast<uint8_t>(1 | ((fn & 0x7F) << 1)));
        oc.Pcm(0x08, 0x89); // wave 393
        oc.Pcm(0x38, static_cast<uint8_t>((fn >> 7) & 7));
        oc.Pcm(0x98, 0xF0);
        oc.Pcm(0xB0, 0x00);
        oc.Pcm(0xC8, 0x00);
        oc.Pcm(0x50, 0x01);
        oc.Pcm(0x68, 0x80);
        Append(out, oc.Run(96));
    }
    return out;
}

// PcmLoopEdgeMatrix: single-sample loop (loop 7 / end 8).
std::vector<int16_t> CasePcmLoop1Sample()
{
    OurChip oc;
    WriteRaw16(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(394), 2, kSmp16, 7, 8);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 394, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(48);
}

// PcmLoopEdgeMatrix overrun carry: loop 4 / end 16 at step 2.
std::vector<int16_t> CasePcmLoopOverrun2x()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(395), 2, kSmpBig, 4, 16);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 395, 0x20, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(96);
}

// PcmTlLadderSweep: TL 0..120 step 16 plus the 0x7F mute special.
std::vector<int16_t> CasePcmTlLadder()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(396), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 396, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(200);
    for (int tl = 0; tl <= 120; tl += 16)
    {
        oc.Pcm(0x50, static_cast<uint8_t>((tl << 1) | 0x01));
        Append(out, oc.Run(200));
    }
    oc.Pcm(0x50, 0xFF); // TL 0x7F special: immediate full mute
    Append(out, oc.Run(200));
    return out;
}

// PcmTlLadderSweep LD half: 0x00 -> 0x40 with LD 0 (interpolated ramp).
std::vector<int16_t> CasePcmTlLd()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(396), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 396, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(200);
    oc.Pcm(0x50, 0x80); // TL 0x40, LD 0: ramp at the interpolated cadence
    Append(out, oc.Run(600));
    oc.Pcm(0x50, 0x01); // back to TL 0 (still interpolated)
    Append(out, oc.Run(600));
    return out;
}

// PcmEnvRateMatrix attack half: AR 5..8 (D1R 0 hold).
std::vector<int16_t> CasePcmArRows()
{
    std::vector<int16_t> out;
    for (int ar = 5; ar <= 8; ar++)
    {
        OurChip oc;
        WriteEnv(oc.Mem(), 0x60);
        WriteHeader(oc.Mem(), HdrOf(397), 2, kSmpEnv, 0, 64);
        oc.Mem().ClearDirty();
        BootNew(oc);
        KeyOnPcm(oc, PcmVoice{0, 397, 0x10, static_cast<uint8_t>(ar << 4),
                              0, 0xF0, 0x01, 0x80});
        Append(out, oc.Run(1500));
    }
    return out;
}

// PcmEnvRateMatrix sustain half: DL 2..9 with D2R 4.
std::vector<int16_t> CasePcmDlSustain()
{
    std::vector<int16_t> out;
    for (int dl = 2; dl <= 9; dl++)
    {
        OurChip oc;
        WriteEnv(oc.Mem(), 0x60);
        WriteHeader(oc.Mem(), HdrOf(397), 2, kSmpEnv, 0, 64);
        oc.Mem().ClearDirty();
        BootNew(oc);
        KeyOnPcm(oc, PcmVoice{0, 397, 0x10, 0xF0,
                              static_cast<uint8_t>((dl << 4) | 4), 0xF0,
                              0x01, 0x80});
        Append(out, oc.Run(2000));
    }
    return out;
}

// PcmEnvRateMatrix release half: hold, key off, RR 8..11.
std::vector<int16_t> CasePcmRrRelease()
{
    std::vector<int16_t> out;
    for (int rr = 8; rr <= 11; rr++)
    {
        OurChip oc;
        WriteEnv(oc.Mem(), 0x60);
        WriteHeader(oc.Mem(), HdrOf(397), 2, kSmpEnv, 0, 64);
        oc.Mem().ClearDirty();
        BootNew(oc);
        KeyOnPcm(oc, PcmVoice{0, 397, 0x10, 0xF0, 0,
                              static_cast<uint8_t>(rr), 0x01, 0x80});
        Append(out, oc.Run(400));
        oc.Pcm(0x68, 0x00); // key off (pan 0, bits clear)
        Append(out, oc.Run(1400));
    }
    return out;
}

// PcmEnvRateMatrix rate correction: AR 7 at OCT 0 vs OCT 3 vs OCT 3 + RC 15.
std::vector<int16_t> CasePcmRateScale()
{
    std::vector<int16_t> out;
    const uint8_t octFns[3] = {0x07, 0x37, 0x37};
    const uint8_t rcRrs[3] = {0x00, 0x00, 0xF0};
    for (int i = 0; i < 3; i++)
    {
        OurChip oc;
        WriteEnv(oc.Mem(), 0x60);
        WriteHeader(oc.Mem(), HdrOf(397), 2, kSmpEnv, 0, 64);
        oc.Mem().ClearDirty();
        BootNew(oc);
        KeyOnPcm(oc, PcmVoice{0, 397, octFns[i], 0x70, 0, rcRrs[i], 0x01,
                              0x80});
        Append(out, oc.Run(1500));
    }
    return out;
}

// PcmDampPrvbMatrix: DAMP rides with key off, then a mid-sustain damp.
std::vector<int16_t> CasePcmDamp()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(398), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 398, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(400);
    oc.Pcm(0x68, 0x40); // key off + DAMP: damper fade ignores RR
    Append(out, oc.Run(1600));

    OurChip ob;
    WriteEnv(ob.Mem(), 0x60);
    WriteHeader(ob.Mem(), HdrOf(398), 2, kSmpEnv, 0, 64);
    ob.Mem().ClearDirty();
    BootNew(ob);
    KeyOnPcm(ob, PcmVoice{0, 398, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    Append(out, ob.Run(400));
    ob.Pcm(0x68, 0xC0); // DAMP while still keyed: fade from sustain
    Append(out, ob.Run(1600));
    return out;
}

// PcmDampPrvbMatrix pseudo-reverb: PRVB redirects the decay to rate 20.
std::vector<int16_t> CasePcmPrvb()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(399), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 399, 0x18, 0xF5, 0x40, 0xF3, 0x01, 0x80});
    return oc.Run(3000); // oct 1, PRVB on, D1R 5 into DL 4 / D2R 0
}

// PcmPanSweep: pan 0..7 stepped mid-voice (left-attenuation ladder).
std::vector<int16_t> CasePcmPanRow()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(399), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 399, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(200);
    for (int pan = 0; pan <= 7; pan++)
    {
        oc.Pcm(0x68, static_cast<uint8_t>(0x80 | pan));
        Append(out, oc.Run(200));
    }
    return out;
}

// PcmPanSweep mirror half: pan 9..15, pan 8 (both off), DO1 bit.
std::vector<int16_t> CasePcmPanMirror()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(399), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 399, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(200);
    for (int pan = 9; pan <= 15; pan++)
    {
        oc.Pcm(0x68, static_cast<uint8_t>(0x80 | pan));
        Append(out, oc.Run(200));
    }
    oc.Pcm(0x68, 0x88); // pan 8: both outputs off
    Append(out, oc.Run(200));
    oc.Pcm(0x68, 0x90); // DO1 bit: output disable
    Append(out, oc.Run(200));
    return out;
}

// PcmLfoMatrix AM half: LFO on, AM depth 7 then 1.
std::vector<int16_t> CasePcmLfoAm()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x60);
    WriteHeader(oc.Mem(), HdrOf(399), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 399, 0x10, 0xF0, 0, 0xF0, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(200);
    oc.Pcm(0x80, 0x08); // LFO freq 1, VIB depth 0
    oc.Pcm(0xD0, 0x07); // AM depth 7
    Append(out, oc.Run(4000));
    oc.Pcm(0xD0, 0x01); // AM depth 1
    Append(out, oc.Run(4000));
    return out;
}

// PcmLfoMatrix VIB half: LFO on, VIB depth 7 then 1 on the ramp table.
std::vector<int16_t> CasePcmLfoVib()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(395), 2, kSmpBig, 0, 0x1000);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 395, 0x00, 0xF0, 0, 0xF0, 0x01, 0x80});
    std::vector<int16_t> out = oc.Run(200);
    oc.Pcm(0x80, 0x0F); // LFO freq 1, VIB depth 7
    Append(out, oc.Run(4000));
    oc.Pcm(0x80, 0x09); // VIB depth 1
    Append(out, oc.Run(4000));
    return out;
}

// PcmInterpMatrix: OCT 0 / FNUM 0 — step 0.5, every other frame blended.
std::vector<int16_t> CasePcmInterpHalf()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(395), 2, kSmpBig, 0, 0x1000);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 395, 0x00, 0xF0, 0, 0xF0, 0x01, 0x80});
    return oc.Run(192);
}

// PcmInterpMatrix: OCT 1 / FNUM 512 — step 1.5, every frame blended.
std::vector<int16_t> CasePcmInterp3Halves()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(395), 2, kSmpBig, 0, 0x1000);
    oc.Mem().ClearDirty();
    BootNew(oc);
    oc.Pcm(0x02, 0x10);
    oc.Pcm(0x20, 0x01);
    oc.Pcm(0x08, 0x8B); // wave 395
    oc.Pcm(0x38, 0x16); // oct 1, fn hi 6 (FNUM 512)
    oc.Pcm(0x98, 0xF0);
    oc.Pcm(0xB0, 0x00);
    oc.Pcm(0xC8, 0x00);
    oc.Pcm(0x50, 0x01);
    oc.Pcm(0x68, 0x80);
    return oc.Run(128);
}

// MixFieldMatrix per-side half: independent L != R codes on 0xF8/0xF9.
std::vector<int16_t> CaseMixPerSide()
{
    OurChip oc;
    WriteEnv(oc.Mem(), 0x50);
    WriteHeader(oc.Mem(), HdrOf(390), 2, kSmpEnv, 0, 64);
    oc.Mem().ClearDirty();
    BootNew(oc);
    KeyOnPcm(oc, PcmVoice{0, 390, 0x10, 0xF0, 0, 0xF0, 0x21, 0x80});
    KeyOnFm(oc, FmVoice{}, true);
    std::vector<int16_t> out;
    const uint8_t mixes[4] = {0x2D, 0x07, 0x38, 0x5A}; // (L<<3)|R pairs
    for (uint8_t mix : mixes)
    {
        oc.Pcm(0xF8, mix);
        oc.Pcm(0xF9, static_cast<uint8_t>(mix ^ 0x1F));
        Append(out, oc.Run(600));
    }
    return out;
}

// Multi-voice PCM half of the mix bus: 12 slots, TL/pan spread.
std::vector<int16_t> CasePcm12Voices()
{
    OurChip oc;
    WriteBig(oc.Mem());
    WriteHeader(oc.Mem(), HdrOf(392), 2, kSmpBig, 2, 8);
    oc.Mem().ClearDirty();
    BootNew(oc);
    for (int s = 0; s < 12; s++)
        KeyOnPcm(oc, PcmVoice{s, 392, static_cast<uint8_t>(0x10 + (s & 3)),
                              0xF0, 0, 0xF0,
                              static_cast<uint8_t>(((s * 8) << 1) | 0x01),
                              static_cast<uint8_t>(0x80 | (s & 0xF))});
    return oc.Run(2000);
}

// FM chord: channels 0/1/2, block and TL spread, EGT hold.
std::vector<int16_t> CaseFmChord()
{
    OurChip oc;
    BootNew(oc);
    const uint8_t tls[3] = {0x08, 0x18, 0x28};
    const uint8_t b0s[3] = {0x2B, 0x33, 0x3B}; // blocks 2/3/4
    for (int ch = 0; ch < 3; ch++)
    {
        // Classic map: channel ch mod = 0x20 + ch, car = 0x23 + ch.
        oc.Fm(0, static_cast<uint8_t>(0x20 + ch), 0x01);
        oc.Fm(0, static_cast<uint8_t>(0x23 + ch), 0x21);
        oc.Fm(0, static_cast<uint8_t>(0x40 + ch), 0x3F);
        oc.Fm(0, static_cast<uint8_t>(0x43 + ch), tls[ch]);
        oc.Fm(0, static_cast<uint8_t>(0x60 + ch), 0xF0);
        oc.Fm(0, static_cast<uint8_t>(0x63 + ch), 0xF0);
        oc.Fm(0, static_cast<uint8_t>(0x80 + ch), 0x00);
        oc.Fm(0, static_cast<uint8_t>(0x83 + ch), 0x00);
        oc.Fm(0, static_cast<uint8_t>(0xA0 + ch), 0x03);
        oc.Fm(0, static_cast<uint8_t>(0xC0 + ch), 0x30);
        oc.Fm(0, static_cast<uint8_t>(0xB0 + ch), b0s[ch]);
    }
    return oc.Run(2000);
}

// SeededFuzzTier: three LCG register streams (56 writes, both buses).
// Same generator as opl4sweep's fuzz family — pinned here as digests so
// any future stream-altering change trips the oracle even if the sweep's
// invariant-only checks stay green.
std::vector<int16_t> CaseFuzzSeeds()
{
    std::vector<int16_t> out;
    for (uint32_t seed : {7u, 19u, 42u})
    {
        uint32_t st = seed;
        const auto rnd = [&st]() {
            st = st * 1664525u + 1013904223u;
            return st;
        };
        OurChip oc;
        const uint8_t dc[2] = {0x7F, 0xFF};
        for (int i = 0; i < 128; i++)
            oc.Mem().WriteSram(0x200100 + static_cast<uint32_t>(i) * 8,
                                dc, 2);
        WriteHeader(oc.Mem(), HdrOf(384), 2, 0x200100, 0, 512);
        oc.Mem().ClearDirty();
        oc.Pcm(0xF8, 0x00); // FM unity
        // Structured opening (same shape as the sweep): one FM carrier +
        // one PCM voice with seed-derived valid parameters.
        FmPatch p;
        p.tlMod = 0x3F;
        p.tlCar = static_cast<uint8_t>(rnd() % 48);
        p.flagsCar = 0x20;
        p.b0 = static_cast<uint8_t>(0x2B + (rnd() % 3));
        KeyOnFmPatch(oc, p);
        static const uint8_t kOctFns[4] = {0x07, 0x27, 0x47, 0x87};
        KeyOnPcm(oc, PcmVoice{static_cast<int>(rnd() % 24), 384,
                              kOctFns[rnd() % 4], 0xF0, 0, 0xF0,
                              static_cast<uint8_t>(rnd() % 0x80), 0x80});
        for (int i = 0; i < 56; i++)
        {
            const uint32_t r = rnd();
            if (r & 0x100) // wave bus: tone regs, 0x02, memory port, mixes
                oc.Pcm(static_cast<uint8_t>(r >> 8),
                       static_cast<uint8_t>(r >> 16));
            else // FM register banks
                oc.Fm((r >> 8) & 1, static_cast<uint8_t>(r >> 16),
                      static_cast<uint8_t>(r >> 24));
        }
        Append(out, oc.Run(600));
    }
    return out;
}

// MemoryAccessSweep: memory-port latch/commit/auto-increment and the MA
// gate; read bytes appended to the stream.
std::vector<int16_t> CaseMemoryPort()
{
    OurChip oc;
    const uint8_t pat[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    oc.Mem().WriteSram(0x200180, pat, 8);
    oc.Mem().ClearDirty();
    BootNew(oc);
    Opl4& chip = oc.Chip();
    std::vector<int16_t> out = oc.Run(16);
    // MA off: 0x06 reads float 0xFF, writes are ignored
    oc.Pcm(0x02, 0x00);
    oc.Pcm(0x03, 0x20);
    oc.Pcm(0x04, 0x01);
    oc.Pcm(0x05, 0x80);
    oc.Pcm(0x06, 0x99);
    Append(out, oc.Run(8));
    out.push_back(static_cast<int16_t>(chip.ReadWave(oc.Clock(), 0x06)));
    // MA on: reads auto-increment through the pattern
    oc.Pcm(0x02, 0x01);
    oc.Pcm(0x03, 0x20);
    oc.Pcm(0x04, 0x01);
    oc.Pcm(0x05, 0x80);
    for (int i = 0; i < 4; i++)
    {
        Append(out, oc.Run(8));
        out.push_back(static_cast<int16_t>(chip.ReadWave(oc.Clock(), 0x06)));
    }
    // writes with auto-increment, then register/data readback
    oc.Pcm(0x05, 0x84);
    oc.Pcm(0x06, 0xAA);
    oc.Pcm(0x06, 0xBB);
    Append(out, oc.Run(8));
    out.push_back(static_cast<int16_t>(chip.ReadWave(oc.Clock(), 0x03)));
    out.push_back(static_cast<int16_t>(chip.ReadWave(oc.Clock(), 0x04)));
    out.push_back(static_cast<int16_t>(chip.ReadWave(oc.Clock(), 0x06)));
    return out;
}

struct Case
{
    const char* name;
    std::vector<int16_t> (*run)();
    bool (*check)(const std::vector<int16_t>&) = nullptr; // optional semantic check
};

const Case kCases[] = {
    {"pcm-loop-overrun", CasePcmLoopOverrun},
    {"pcm-end0-wrap", CasePcmEnd0Wrap},
    {"pcm-one-shot", CasePcmOneShot},
    {"pcm-width8", CasePcmWidth8},
    {"pcm-width12", CasePcmWidth12},
    {"pcm-decay-sustain", CasePcmDecaySustain},
    {"pcm-release", CasePcmRelease},
    {"fm-tone", CaseFmTone},
    {"fm-vibrato", CaseFmVibrato},
    {"fm-tremolo", CaseFmTremolo},
    {"fm-release", CaseFmRelease},
    {"mix-sweep", CaseMixSweep},
    {"multi-voice", CaseMultiVoice},
    {"save-restore-replay", CaseSaveRestore, HalvesEqual},
    {"bus-status", CaseBusStatus},
    {"fm-tl-ladder", CaseFmTlLadder},
    {"fm-mod-tl", CaseFmModTl},
    {"fm-mult", CaseFmMult},
    {"fm-ksl", CaseFmKsl},
    {"fm-env-attack", CaseFmEnvAttack},
    {"fm-env-decay", CaseFmEnvDecay},
    {"fm-env-release", CaseFmEnvRelease},
    {"fm-feedback", CaseFmFeedback},
    {"fm-4op", CaseFm4Op},
    {"fm-rhythm", CaseFmRhythm},
    {"fm-waveform", CaseFmWaveform},
    {"fm-amvib-matrix", CaseFmAmVibMatrix},
    {"fm-routing", CaseFmRouting},
    {"fm-timers", CaseFmTimers},
    {"fm-kon-edge", CaseFmKonEdge},
    {"fm-egt-ksr", CaseFmEgtKsr},
    {"fm-chord", CaseFmChord},
    {"pcm-wave-bank", CasePcmWaveBank},
    {"pcm-oct-fnum", CasePcmOctFnum},
    {"pcm-fnum-grid", CasePcmFnumGrid},
    {"pcm-loop-1sample", CasePcmLoop1Sample},
    {"pcm-loop-overrun-2x", CasePcmLoopOverrun2x},
    {"pcm-tl-ladder", CasePcmTlLadder},
    {"pcm-tl-ld", CasePcmTlLd},
    {"pcm-ar-rows", CasePcmArRows},
    {"pcm-dl-sustain", CasePcmDlSustain},
    {"pcm-rr-release", CasePcmRrRelease},
    {"pcm-ratescale", CasePcmRateScale},
    {"pcm-damp", CasePcmDamp},
    {"pcm-prvb", CasePcmPrvb},
    {"pcm-pan-row", CasePcmPanRow},
    {"pcm-pan-mirror", CasePcmPanMirror},
    {"pcm-lfo-am", CasePcmLfoAm},
    {"pcm-lfo-vib", CasePcmLfoVib},
    {"pcm-interp-half", CasePcmInterpHalf},
    {"pcm-interp-3halves", CasePcmInterp3Halves},
    {"pcm-12voices", CasePcm12Voices},
    {"mix-per-side", CaseMixPerSide},
    {"memory-port", CaseMemoryPort},
    {"fuzz-seeds", CaseFuzzSeeds},
};

// ---------------------------------------------------------------------------
// Golden file
// ---------------------------------------------------------------------------
struct GoldenEntry
{
    uint32_t frames = 0;
    uint64_t hash = 0;
};

bool LoadGolden(const std::string& path, std::unordered_map<std::string, GoldenEntry>& out)
{
    std::ifstream f(path);
    if (!f)
        return false;
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream is(line);
        std::string name;
        unsigned long long frames, hash;
        if (!(is >> name >> frames >> std::hex >> hash))
        {
            std::printf("cosim-oracle: malformed golden line: %s\n", line.c_str());
            return false;
        }
        out[name] = GoldenEntry{static_cast<uint32_t>(frames), hash};
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    bool generate = false;
    bool verbose = false;
    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--generate") == 0)
            generate = true;
        else if (std::strcmp(argv[i], "--verbose") == 0)
            verbose = true;
        else
        {
            std::printf("usage: cosim-oracle [--generate] [--verbose]\n");
            return 2;
        }
    }

    if (generate)
    {
        std::filesystem::create_directories("golden");
        std::ofstream f(kGoldenPath);
        if (!f)
        {
            std::printf("cosim-oracle: cannot write %s\n", kGoldenPath);
            return 1;
        }
        f << "# cosim-oracle golden — libopl4 chip-stream digests (FNV-1a 64).\n"
          << "# Regenerated by `cosim-oracle --generate` after an INTENTIONAL\n"
          << "# behaviour change; re-run cosim-ymfm before committing a baseline.\n";
        for (const Case& c : kCases)
        {
            const std::vector<int16_t> out = c.run();
            std::printf("generated %-22s %6zu frames  hash %016llx\n", c.name,
                        out.size() / 2,
                        static_cast<unsigned long long>(Digest(out)));
            f << c.name << ' ' << out.size() / 2 << ' '
              << std::hex << std::setw(16) << std::setfill('0') << Digest(out)
              << std::dec << '\n';
        }
        std::printf("cosim-oracle: wrote %s — review the diff, then commit\n",
                    kGoldenPath);
        return 0;
    }

    std::unordered_map<std::string, GoldenEntry> golden;
    if (!LoadGolden(kGoldenPath, golden))
    {
        std::printf("cosim-oracle: %s missing (run with --generate first)\n",
                    kGoldenPath);
        return 1;
    }

    int total = 0;
    int failed = 0;
    for (const Case& c : kCases)
    {
        total++;
        const auto it = golden.find(c.name);
        if (it == golden.end())
        {
            failed++;
            std::printf("[FAIL] %s: no golden entry\n", c.name);
            continue;
        }
        const std::vector<int16_t> out = c.run();
        const uint64_t h = Digest(out);
        const bool hashOk = h == it->second.hash;
        const bool framesOk = out.size() / 2 == it->second.frames;
        const bool checkOk = c.check == nullptr || c.check(out);
        const bool ok = hashOk && framesOk && checkOk;
        if (!ok)
            failed++;
        if (!ok || verbose)
            std::printf("[%s] %s: %zu frames  hash %016llx (golden %016llx/%u)\n",
                        ok ? "PASS" : "FAIL", c.name, out.size() / 2,
                        static_cast<unsigned long long>(h),
                        static_cast<unsigned long long>(it->second.hash),
                        it->second.frames);
        if (!checkOk)
            std::printf("       semantic check failed (save/restore halves differ)\n");
        golden.erase(it); // matched — what remains at the end is stale
    }
    for (const auto& stale : golden)
    {
        failed++;
        std::printf("[FAIL] stale golden entry: %s\n", stale.first.c_str());
    }

    std::printf("cosim-oracle: %d/%d cases match\n", total - failed, total);
    return failed == 0 ? 0 : 1;
}
