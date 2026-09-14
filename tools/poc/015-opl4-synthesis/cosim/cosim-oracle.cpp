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

// E = 0 degenerate wrap: every advance adds loopAddr on top of the step.
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
    oc.Fm(0, 0x41, 0x20); // carrier TL -12 dB -> -24 dB
    Append(out, oc.Run(3000));
    return out;
}

// Vibrato: carrier VIB bit + deep depth (0xBD bit 6), ~3 PM LFO periods.
std::vector<int16_t> CaseFmVibrato()
{
    OurChip oc;
    BootNew(oc);
    KeyOnFm(oc, FmVoice{}, true);
    oc.Fm(0, 0x21, 0x41); // carrier: VIB on, mult 1
    oc.Fm(0, 0xBD, 0x40); // deep vibrato depth
    return oc.Run(20000);
}

// Tremolo: carrier AM bit + deep depth (0xBD bit 7), ~2 AM LFO periods.
std::vector<int16_t> CaseFmTremolo()
{
    OurChip oc;
    BootNew(oc);
    KeyOnFm(oc, FmVoice{}, true);
    oc.Fm(0, 0x21, 0x81); // carrier: AM on, mult 1
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
