// Golden digests of Saa1099 over the co-simulation corpus
// (tools/verification/saa1099/corpus/*.saa).
//
// The corpus streams were run through our model and the references (SAASound, MAME,
// the MiSTer RTL) by tools/verification/saa1099/run-cosim.py until every generator
// agreed or differed only where the consensus table says why. These digests freeze
// that agreed behavior: FNV-1a over the generator state sampled every 61 chip clocks
// and at every write (tone levels, latched numbers and counters, LFSRs and dividers,
// envelope state, summed output), the band-limited audio at 44.1 kHz in 10 ms frames, and the final
// TTD blob. A digest change means the chip behaves differently: rerun the
// co-simulation before refreshing a digest here.
//
// Runtime: each stream is about 50 ms of chip time, run event-driven (well under the
// 50 ms test budget in Release; the sampling dominates).

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/sound/chips/saa1099/saa1099.h"

namespace
{
struct Fnv
{
    uint64_t h = 14695981039346656037ull;
    void Byte(uint8_t b)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    template <typename T>
    void Value(T v)
    {
        const auto* p = reinterpret_cast<const uint8_t*>(&v);
        for (size_t i = 0; i < sizeof(v); i++)
            Byte(p[i]);
    }
};

struct Write
{
    uint64_t clock;
    bool address;
    uint8_t value;
};

// Corpus format: "<clock> A|D <value>", "<clock> END", ';' comments (drivers/cosim.h)
bool LoadStream(const std::string& name, std::vector<Write>& writes, uint64_t& end)
{
    const auto path = TestPathHelper::FindProjectRoot() / "tools/verification/saa1099/corpus" / (name + ".saa");
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    end = 0;
    while (std::getline(in, line))
    {
        const auto comment = line.find(';');
        if (comment != std::string::npos)
            line.resize(comment);
        std::istringstream ls(line);
        std::string clock, kind, value;
        if (!(ls >> clock >> kind))
            continue;
        const uint64_t c = std::stoull(clock);
        if (kind == "END")
        {
            end = c;
            continue;
        }
        if (!(ls >> value))
            return false;
        const unsigned v = value[0] == '#' ? std::stoul(value.substr(1), nullptr, 16) : std::stoul(value, nullptr, 0);
        writes.push_back({c, kind == "A", static_cast<uint8_t>(v)});
    }
    return end > 0;
}

void HashState(Fnv& f, const Saa1099Report& r)
{
    for (const auto& t : r.tones)
    {
        f.Value(t.level);
        f.Value(t.toneLatched);
        f.Value(t.octaveLatched);
        f.Value(t.clocksToTransition);
    }
    for (const auto& n : r.noise)
    {
        f.Value(n.lfsr);
        f.Value(n.clocksToShift);
    }
    for (const auto& e : r.envelopes)
    {
        f.Value(e.enabled);
        f.Value(e.resolution3Bit);
        f.Value(e.shape);
        f.Value(e.invertRight);
        f.Value(e.externalClock);
        f.Value(e.phase);
        f.Value(e.position);
        f.Value(e.ended);
        f.Value(e.pending);
        f.Value(e.levelLeft);
        f.Value(e.levelRight);
    }
    f.Value(r.outputLeft);
    f.Value(r.outputRight);
    f.Value(r.chipClocks);
}

struct Digests
{
    uint64_t state;
    uint64_t audio;
    uint64_t blob;
};

Digests Play(const std::vector<Write>& writes, uint64_t end, Saa1099RenderMode mode)
{
    constexpr uint64_t kSampleStep = 61;
    constexpr uint64_t kFrameClocks = 80000; // 10 ms at 8 MHz
    constexpr size_t kFrameSamples = 441;    // 10 ms at 44.1 kHz

    Saa1099 chip;
    Saa1099Config cfg;
    cfg.hostTickRate = 8000000; // host axis = chip clock: the corpus timing exactly
    cfg.chipClockHz = 8000000;
    cfg.outputRate = 44100;
    cfg.renderMode = mode;
    chip.Configure(cfg);

    Fnv state, audio;
    std::vector<int16_t> frame(kFrameSamples * 2);
    uint64_t t = 0;
    uint64_t nextFrame = kFrameClocks;
    size_t w = 0;
    Saa1099Report r;
    auto advanceTo = [&](uint64_t target) {
        while (t < target)
        {
            const uint64_t step = std::min({kSampleStep, target - t, nextFrame - t});
            t += step;
            chip.Run(t);
            if (t == nextFrame)
            {
                chip.EndFrame(t, frame.data(), kFrameSamples);
                for (int16_t s : frame)
                    audio.Value(s);
                nextFrame += kFrameClocks;
            }
            chip.Describe(r);
            HashState(state, r);
        }
    };
    while (w < writes.size())
    {
        advanceTo(writes[w].clock);
        if (writes[w].address)
            chip.WriteAddress(t, writes[w].value);
        else
            chip.WriteData(t, writes[w].value);
        chip.Describe(r);
        HashState(state, r);
        w++;
    }
    advanceTo(end);
    // The partial last frame
    const uint64_t tail = t - (nextFrame - kFrameClocks);
    if (tail > 0)
    {
        const size_t samples = static_cast<size_t>(tail * kFrameSamples / kFrameClocks);
        chip.EndFrame(t, frame.data(), samples);
        for (size_t i = 0; i < samples * 2; i++)
            audio.Value(frame[i]);
    }

    std::vector<uint8_t> blob(chip.TTDStateSize());
    chip.TTDSaveState(blob.data());
    Fnv b;
    for (uint8_t byte : blob)
        b.Byte(byte);
    return {state.h, audio.h, b.h};
}

} // namespace

// Outside the anonymous namespace: gtest's parameter factory holds it (GCC
// -Wsubobject-linkage)
struct Saa1099GoldenCase
{
    const char* stream;
    Saa1099RenderMode mode;
    uint64_t state;
    uint64_t audio;
    uint64_t blob;
};

void PrintTo(const Saa1099GoldenCase& g, std::ostream* os)
{
    *os << g.stream << (g.mode == Saa1099RenderMode::Authentic ? " (Authentic)" : "");
}

namespace
{
constexpr Saa1099RenderMode HiFi = Saa1099RenderMode::HiFi;
constexpr Saa1099RenderMode Authentic = Saa1099RenderMode::Authentic;

const Saa1099GoldenCase kGolden[] = {
    // clang-format off
    {"all-voices",               HiFi,      0x6BDBC193AC8CB16Dull, 0x9DD1F02EB020FFEBull, 0xC66DD74A15B28A89ull},
    {"amplitude-zero",           HiFi,      0x4C84ABD64FD8E70Full, 0x70E445B7A25A8AB4ull, 0x09D5246CC7D7D2DDull},
    {"env-buffered",             HiFi,      0x7832E9968C4E6C8Dull, 0x8E6223D7E8303201ull, 0xD11E142846969FA3ull},
    {"env-external",             HiFi,      0xEB5C04D525B2E593ull, 0x22345163827ED2EDull, 0xE9229282EF7103B3ull},
    {"env-resolution-switch",    HiFi,      0xFBEA57E7AF2616FAull, 0xCF7662A0CBB45BA9ull, 0xF31C1988FD773D7Aull},
    {"env-shapes-3bit",          HiFi,      0x1701626DAC0ADA3Full, 0x76A08EAC46865AE9ull, 0xBCFD695E96C773ECull},
    {"env-shapes-3bit-inverted", HiFi,      0x9BCEA184588EEBA3ull, 0x212DC773578CA9B6ull, 0xAC91BF56BD5F6F2Eull},
    {"env-shapes-4bit",          HiFi,      0x7493192D6B567310ull, 0x50EDC8CCB9C1A0EDull, 0xF535D4CF77D023F1ull},
    {"env-shapes-4bit-inverted", HiFi,      0x258295BDEDDCA3E8ull, 0x05B8E1CA621C65C6ull, 0x1F7C6A5AEC6CBF57ull},
    {"mixer",                    HiFi,      0x9F970939A615B09Bull, 0xBF0BCA1E22970F28ull, 0xBDA16B1BA8A316EDull},
    {"noise-from-tone",          HiFi,      0xB140609188E12550ull, 0x003A2F847E36B791ull, 0xBE5506BCAD9086BEull},
    {"octave-latch",             HiFi,      0xC10A2FF5361C88A7ull, 0xD97CD2CA9B76EA91ull, 0xF4D9912495EE57CAull},
    {"random-1",                 HiFi,      0x0A92639ECE600846ull, 0xED5127A72A245F2Eull, 0x8B82818B02BD9044ull},
    {"random-2",                 HiFi,      0x89C6F1018B2DA6BAull, 0x7BFD5951D9063D4Full, 0x671306B7C710C137ull},
    {"random-3",                 HiFi,      0x4F86BE900AA04C21ull, 0xA8AD371D302C3D0Eull, 0x234221E4F254EC71ull},
    {"random-4",                 HiFi,      0xB764A952580B3935ull, 0xDF3896A4B47A7B51ull, 0x52CE5263998135F5ull},
    {"random-5",                 HiFi,      0xCA216070910DD408ull, 0x2737EC203E8218AFull, 0xA000EACC1A4D0482ull},
    {"random-6",                 HiFi,      0xE6AB9145BD1DAA47ull, 0x027DA43CB46C3D19ull, 0x9302646D4C242ACCull},
    {"random-quiet-9",           HiFi,      0x4D07AFDC93294177ull, 0x424A16B08BEB19ABull, 0x8DA437809C2A4F60ull},
    {"random-quiet-10",          HiFi,      0xB80E2861FF90E688ull, 0xAE4649DF287AA555ull, 0x26D8ED3563279AFBull},
    {"random-raw-7",             HiFi,      0x1CD6BE47605B5003ull, 0xA82B7ABFDC2BBE62ull, 0xEC5A30550BFA8A71ull},
    {"random-raw-8",             HiFi,      0x942EF6D3E38338B4ull, 0x7C0A7F5DFFD72820ull, 0x3879F16C14766159ull},
    {"sync-reset",               HiFi,      0x4BD3FD78F58DF5ECull, 0xE276E99785508D4Dull, 0xF4A6AD818841D699ull},
    {"tone-periods",             HiFi,      0x2330066EBBA40772ull, 0x3B7864D13ACCE23Dull, 0x6C670764461B437Dull},
    {"all-voices",               Authentic, 0xE4917ADFD5439B0Eull, 0x1F61CCE2F3687E19ull, 0xC66DD74A15B28A89ull},
    {"env-shapes-4bit-inverted", Authentic, 0x0A24761C9682EA5Full, 0xDF21714AC619BAE4ull, 0x1F7C6A5AEC6CBF57ull},
    // clang-format on
};

} // namespace

class Saa1099Golden_Test : public ::testing::TestWithParam<Saa1099GoldenCase>
{
};

TEST_P(Saa1099Golden_Test, CorpusDigest)
{
    const Saa1099GoldenCase& g = GetParam();
    std::vector<Write> writes;
    uint64_t end = 0;
    ASSERT_TRUE(LoadStream(g.stream, writes, end)) << "corpus stream " << g.stream << " missing";
    const Digests d = Play(writes, end, g.mode);
    EXPECT_EQ(d.state, g.state) << std::hex << "state 0x" << d.state;
    EXPECT_EQ(d.audio, g.audio) << std::hex << "audio 0x" << d.audio;
    EXPECT_EQ(d.blob, g.blob) << std::hex << "blob 0x" << d.blob;
    if (d.state != g.state || d.audio != g.audio || d.blob != g.blob)
        printf("    {\"%s\", %s, 0x%016llXull, 0x%016llXull, 0x%016llXull},\n", g.stream,
               g.mode == Saa1099RenderMode::HiFi ? "HiFi" : "Authentic", static_cast<unsigned long long>(d.state),
               static_cast<unsigned long long>(d.audio), static_cast<unsigned long long>(d.blob));
}

INSTANTIATE_TEST_SUITE_P(Corpus, Saa1099Golden_Test, ::testing::ValuesIn(kGolden),
                         [](const ::testing::TestParamInfo<Saa1099GoldenCase>& info) {
                             // "env-shapes-3bit" -> "EnvShapes3bit"
                             std::string name;
                             bool upper = true;
                             for (const char* c = info.param.stream; *c; c++)
                             {
                                 if (*c == '-')
                                 {
                                     upper = true;
                                     continue;
                                 }
                                 name += upper ? static_cast<char>(toupper(static_cast<unsigned char>(*c))) : *c;
                                 upper = false;
                             }
                             return name + (info.param.mode == Saa1099RenderMode::Authentic ? "Authentic" : "");
                         });
