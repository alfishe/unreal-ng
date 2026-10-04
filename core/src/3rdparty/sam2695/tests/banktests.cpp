// libsam2695 tests - SF2 loader and the bank identity.
#include "sf2builder.h"
#include "synthhelper.h"
#include "testfw.h"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace sam2695;
using namespace sam2695test;

namespace
{

Sf2Builder TwoPresetBuilder()
{
    Sf2Builder b;
    const int s = b.AddSample(SineSample("sine"));
    b.presets.push_back(SimplePreset("Lead", 0, 80, s));
    b.presets.push_back(SimplePreset("Piano", 0, 0, s, {{G(Gen::InitialAttenuation), 60}}));
    return b;
}

Sf2Bank::LoadResult LoadBytes(const std::vector<uint8_t>& f)
{
    return Sf2Bank::LoadMemory(f.data(), f.size());
}

bool Contains(const std::string& s, const char* what)
{
    return s.find(what) != std::string::npos;
}

} // namespace

TEST(Sha256, Vectors)
{
    CHECK(DigestHex(Sha256(nullptr, 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const char* abc = "abc";
    CHECK(DigestHex(Sha256(reinterpret_cast<const uint8_t*>(abc), 3)) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; // 56 bytes: two-block padding
    CHECK(DigestHex(Sha256(reinterpret_cast<const uint8_t*>(two), std::strlen(two))) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Bank, LoadsSynthetic)
{
    const std::vector<uint8_t> f = TwoPresetBuilder().Build();
    Sf2Bank::LoadResult r = LoadBytes(f);
    CHECK(r.bank != nullptr);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::None));
    if (!r.bank)
        return;
    const BankModel& m = r.bank->Model();
    CHECK_EQ_I(m.presets.size(), 2);
    CHECK(m.presets[0].name == "Piano"); // sorted by bank / program
    CHECK(m.presets[1].name == "Lead");
    CHECK_EQ_I(m.instruments.size(), 2);
    CHECK_EQ_I(m.samples.size(), 1);
    CHECK_EQ_I(m.samples[0].loopEnd - m.samples[0].loopStart, 800);
    CHECK_EQ_I(m.versionMajor, 2);
    CHECK_EQ_I(m.versionMinor, 4);
    CHECK(m.name == "libsam2695 test bank");
    CHECK(m.warnings.empty());
    CHECK(r.bank->FindPreset(0, 80) != nullptr);
    CHECK(r.bank->FindPreset(0, 1) == nullptr);
    CHECK(r.bank->Digest() == Sha256(f.data(), f.size()));
    const Zone& z = m.zones[m.instruments[0].zoneFirst];
    CHECK_EQ_I(z.link, 0);
}

TEST(Bank, RefusesNotRiff)
{
    std::vector<uint8_t> f = TwoPresetBuilder().Build();
    f[0] = 'X';
    Sf2Bank::LoadResult r = LoadBytes(f);
    CHECK(r.bank == nullptr);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::NotRiff));
    f = TwoPresetBuilder().Build();
    std::memcpy(&f[8], "WAVE", 4);
    r = LoadBytes(f);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::NotSfbk));
    CHECK(Contains(r.reason, "WAVE"));
}

TEST(Bank, RefusesTruncated)
{
    std::vector<uint8_t> f = TwoPresetBuilder().Build();
    const size_t shdr = Sf2Builder::FindChunk(f, "shdr");
    f.resize(shdr + 20); // cut inside shdr: its declared size runs past the pdta list
    // fix the RIFF size so only the inner chunk is inconsistent
    Sf2Bank::LoadResult r = LoadBytes(f);
    CHECK(r.bank == nullptr);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::Truncated));
    CHECK(Contains(r.reason, "past its container"));
}

TEST(Bank, RefusesMissingChunk)
{
    std::vector<uint8_t> f = TwoPresetBuilder().Build();
    std::memcpy(&f[Sf2Builder::FindChunk(f, "shdr")], "xhdr", 4);
    Sf2Bank::LoadResult r = LoadBytes(f);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::MissingChunk));
    CHECK(Contains(r.reason, "pdta/shdr"));
    f = TwoPresetBuilder().Build();
    std::memcpy(&f[Sf2Builder::FindChunk(f, "ifil")], "xfil", 4);
    r = LoadBytes(f);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::MissingChunk));
    CHECK(Contains(r.reason, "ifil"));
    f = TwoPresetBuilder().Build();
    std::memcpy(&f[Sf2Builder::FindChunk(f, "smpl")], "xmpl", 4);
    r = LoadBytes(f);
    CHECK(Contains(r.reason, "smpl"));
}

TEST(Bank, RefusesBadRecordSize)
{
    // igen grows by 2 bytes (sizes of igen, LIST pdta and RIFF patched): not a whole 4-byte record
    std::vector<uint8_t> f = TwoPresetBuilder().Build();
    const size_t igen = Sf2Builder::FindChunk(f, "igen");
    const uint32_t size = static_cast<uint32_t>(f[igen + 4] | (f[igen + 5] << 8));
    f.insert(f.begin() + static_cast<long>(igen + 8 + size), {0, 0});
    auto grow = [&f](size_t at) {
        uint32_t v;
        std::memcpy(&v, &f[at], 4);
        v += 2;
        std::memcpy(&f[at], &v, 4);
    };
    grow(igen + 4);
    grow(4);
    grow(Sf2Builder::FindChunk(f, "pdta") - 4);
    Sf2Bank::LoadResult r = LoadBytes(f);
    CHECK(r.bank == nullptr);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::BadRecordSize));
    CHECK(Contains(r.reason, "pdta/igen"));
}

TEST(Bank, RefusesBadIndex)
{
    Sf2Builder b = TwoPresetBuilder();
    b.presets[0].zones[0].sample = 7; // no such sample
    Sf2Bank::LoadResult r = LoadBytes(b.Build());
    CHECK(r.bank == nullptr);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::BadIndex));
    CHECK(Contains(r.reason, "sample index 7"));
}

TEST(Bank, RefusesBadSample)
{
    std::vector<uint8_t> f = TwoPresetBuilder().Build();
    const size_t shdr = Sf2Builder::FindChunk(f, "shdr") + 8;
    // start beyond the sample data
    const uint32_t bad = 1u << 20;
    std::memcpy(&f[shdr + 20], &bad, 4);
    const uint32_t badEnd = (1u << 20) + 10;
    std::memcpy(&f[shdr + 24], &badEnd, 4);
    Sf2Bank::LoadResult r = LoadBytes(f);
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::BadSample));
    CHECK(Contains(r.reason, "outside"));
}

TEST(Bank, RefusesSf3AndOtherVersions)
{
    Sf2Builder b = TwoPresetBuilder();
    b.versionMajor = 3;
    Sf2Bank::LoadResult r = LoadBytes(b.Build());
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::Compressed));
    CHECK(Contains(r.reason, "SF3"));
    b.versionMajor = 1;
    r = LoadBytes(b.Build());
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::BadVersion));
}

TEST(Bank, RefusesNoPresets)
{
    Sf2Builder b = TwoPresetBuilder();
    b.presets.clear();
    Sf2Bank::LoadResult r = LoadBytes(b.Build());
    CHECK_EQ_I(static_cast<int>(r.error), static_cast<int>(BankError::NoPresets));
}

TEST(Bank, Sm24)
{
    Sf2Builder b;
    BSample s = DcSample("dc24", 400, 0);
    for (int32_t& v : s.frames)
        v = (1000 << 8) | 0x80; // 24-bit value 256128
    b.AddSample(s);
    b.presets.push_back(SimplePreset("Dc", 0, 0, 0, {{G(Gen::SampleModes), 1}, {G(Gen::AttackVolEnv), -32768}}));
    b.use24 = true;
    Sf2Bank::LoadResult r = LoadBytes(b.Build());
    CHECK(r.bank != nullptr);
    if (!r.bank)
        return;
    CHECK_EQ_I(r.bank->Model().data24.size(), r.bank->Model().data16.size());
    CHECK_EQ_I(r.bank->Model().data24[0], 0x80);
    CHECK_EQ_I(r.bank->Model().data16[0], 1000);
    // played back: the 24-bit level reaches the output (velocity 127, CC7 127, center pan)
    TestSynth ts(r.bank);
    ts.Send(0, {0xB0, 7, 127, 0x90, 69, 127});
    ts.RunTo(640);
    const double expected = 256128.0 / 8388608.0 * std::cos(3.14159265358979323846 / 4);
    const double velocityCb = 960.0 * (-(20.0 / 96.0) * std::log10(std::pow(127.0 / 128.0, 2)));
    // CC 7 = 127 and CC 11 = 127 (power-up) go through the same curve
    CHECK_NEAR(ts.L(320), expected * std::pow(10.0, -3.0 * velocityCb / 200.0), 1e-5);

    // sm24 in a 2.01 bank is ignored (warned)
    b.versionMinor = 1;
    r = LoadBytes(b.Build());
    CHECK(r.bank != nullptr);
    if (r.bank)
    {
        CHECK(r.bank->Model().data24.empty());
        CHECK(!r.bank->Model().warnings.empty());
    }
}

TEST(Bank, ToleratesFixable)
{
    Sf2Builder b = TwoPresetBuilder();
    b.samples[0].loopStart = 700;
    b.samples[0].loopEnd = 5000; // past the sample end
    std::vector<uint8_t> f = b.Build();
    Sf2Bank::LoadResult r = LoadBytes(f);
    CHECK(r.bank != nullptr);
    if (!r.bank)
        return;
    const SampleInfo& s = r.bank->Model().samples[0];
    CHECK_EQ_I(s.loopEnd, s.end);
    CHECK_EQ_I(s.loopStart, s.start + 700);
    CHECK(!r.bank->Model().warnings.empty());
}

TEST(Bank, GlobalZonesAndRanges)
{
    Sf2Builder b;
    const int s = b.AddSample(SineSample("sine"));
    BPreset p = SimplePreset("Split", 0, 0, s);
    p.hasInstGlobal = true;
    p.instGlobal.gens = {{G(Gen::SampleModes), 1}, {G(Gen::InitialAttenuation), 100}};
    p.instGlobal.keyLo = 0;
    p.instGlobal.keyHi = 59; // inherited by the first zone, which sets no range
    BZone high;
    high.sample = s;
    high.keyLo = 60;
    high.keyHi = 127;
    p.zones.push_back(high);
    b.presets.push_back(p);
    std::shared_ptr<const Sf2Bank> bank = b.Load();
    CHECK(bank != nullptr);
    if (!bank)
        return;
    const BankModel& m = bank->Model();
    const Instrument& in = m.instruments[0];
    CHECK(in.globalZone >= 0);
    CHECK_EQ_I(in.zoneCount, 2);
    CHECK_EQ_I(m.zones[in.globalZone].gens[G(Gen::InitialAttenuation)], 100);
    // key 40 and key 80 each sound exactly one zone
    TestSynth ts(bank);
    ts.Send(0, {0x90, 40, 100, 0x91, 80, 100});
    ts.RunTo(64);
    SynthReport rep;
    ts.synth.Describe(rep);
    CHECK_EQ_I(rep.channels[0].activeVoices, 1);
    CHECK_EQ_I(rep.channels[1].activeVoices, 1);
}

TEST(Bank, SharedReadOnly)
{
    std::shared_ptr<const ISoundBank> bank = BasicBank();
    TestSynth a(bank), b(bank);
    for (TestSynth* t : {&a, &b})
    {
        t->Send(10, {0x90, 69, 100});
        t->RunTo(2000);
    }
    CHECK(a.out == b.out);
    CHECK_EQ_I(bank.use_count(), 3);
}

TEST(Bank, LoadFileMatchesMemory)
{
    // the streaming file loader (hash pass, lists in memory, samples read in place) builds the same
    // model and identity as the memory loader, and refuses the same way
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() /
                          ("sam2695tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                           ".sf2");
    Sf2Builder b = TwoPresetBuilder();
    BSample s = DcSample("dc24", 300, 0);
    for (int32_t& v : s.frames)
        v = (-2000 << 8) | 0x11;
    b.AddSample(s);
    b.use24 = true;
    for (int variant = 0; variant < 3; variant++)
    {
        std::vector<uint8_t> bytes = b.Build();
        if (variant == 1) // the file ends inside shdr
            bytes.resize(Sf2Builder::FindChunk(bytes, "shdr") + 20);
        if (variant == 2) // shdr claims one record more than its list holds
            bytes[Sf2Builder::FindChunk(bytes, "shdr") + 4] += 46;
        {
            std::ofstream f(path, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        Sf2Bank::LoadResult m = LoadBytes(bytes);
        Sf2Bank::LoadResult f = Sf2Bank::LoadFile(path.string());
        CHECK_EQ_I(static_cast<int>(f.error), static_cast<int>(m.error));
        CHECK(f.reason == m.reason);
        if (variant == 2)
            CHECK(Contains(m.reason, "'shdr' in pdta at offset"));
        if (variant == 0 && f.bank && m.bank)
        {
            CHECK(f.bank->Digest() == m.bank->Digest());
            CHECK(f.bank->Model().data16 == m.bank->Model().data16);
            CHECK(f.bank->Model().data24 == m.bank->Model().data24);
            CHECK_EQ_I(f.bank->Model().data24.size(), f.bank->Model().data16.size());
            CHECK_EQ_I(f.bank->Model().presets.size(), m.bank->Model().presets.size());
            CHECK_EQ_I(f.bank->Model().zones.size(), m.bank->Model().zones.size());
        }
    }
    std::error_code ec;
    fs::remove(path, ec);
    Sf2Bank::LoadResult missing = Sf2Bank::LoadFile(path.string());
    CHECK_EQ_I(static_cast<int>(missing.error), static_cast<int>(BankError::Io));
}
