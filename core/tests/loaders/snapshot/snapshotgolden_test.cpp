/// @file snapshotgolden_test.cpp
/// @brief Golden commit digests (snapshot pipeline P0, PLAN #84, test SP-1): every SNA / Z80 / SZX fixture loaded on
/// every creatable machine, the state it leaves behind digested (RAM, paging latches and bank mapping, CPU, AY 0,
/// border; helper _helpers/snapshotdigest.h) and compared with testdata/loaders/golden/commit-digests.txt.
///
/// The table records TODAY's behavior on purpose, defects included (a 128K file on a 48K machine, #7FFD locks on a
/// Pentagon 1024, the ATM / TS-Conf pagers after a cold reset: proposal section 3). Every later pipeline step must
/// reproduce it; a row may change only in a commit that says so (P4: the 38 SPRINTER rows that loaded now say
/// "refused", a fresh Sprinter is in no Spectrum mode). The defects the proposal suspects are named in the
/// SnapshotDefects_Test cases below: each pins the CURRENT behavior and says what the fix changes.
///
/// Update after an approved change: UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 core-tests --gtest_filter='SnapshotGoldenRewrite*'
/// rewrites the table (review the diff).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/snapshotdigest.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace fs = std::filesystem;

namespace
{
struct Machine
{
    const char* model;
    uint32_t ramKb;   ///< 0 = the model's default
    const char* tag;  ///< the table's name for it
};

const Machine kMachines[] = {
    {"48K", 0, "48K"},          {"128k", 0, "128K"},       {"PLUS2", 0, "PLUS2"},
    {"PLUS2A", 0, "PLUS2A"},    {"PLUS3", 0, "PLUS3"},     {"PENTAGON", 0, "PENTAGON128"},
    {"PENTAGON", 512, "PENTAGON512"}, {"PENTAGON", 1024, "PENTAGON1024"}, {"SCORPION", 0, "SCORPION"},
    {"PROFSCORP", 0, "PROFSCORP"}, {"ATM710", 0, "ATM710"}, {"ATM3", 0, "ATM3"},
    {"ATM450", 0, "ATM450"},    {"PROFI", 0, "PROFI"},     {"PROFI3", 0, "PROFI3"},
    {"TSL", 0, "TSL"},          {"SPRINTER", 0, "SPRINTER"},
};

/// gtest prints a failing parameter; the tag is what a person wants to read
void PrintTo(const Machine& machine, std::ostream* os)
{
    *os << machine.tag;
}

const char* kGoldenFile = "loaders/golden/commit-digests.txt";

/// The fixtures, relative to testdata/loaders, in a fixed order; the "invalid" folders hold files that must fail
std::vector<std::string> Fixtures()
{
    std::vector<std::string> out;
    const fs::path root = TestPathHelper::GetTestDataPath("loaders");
    for (const char* dir : {"sna", "z80", "szx"})
    {
        for (const auto& entry : fs::recursive_directory_iterator(root / dir))
        {
            if (!entry.is_regular_file())
                continue;
            const std::string ext = entry.path().extension().string();
            if (ext != ".sna" && ext != ".z80" && ext != ".szx")
                continue;
            const std::string relative = fs::relative(entry.path(), root).generic_string();
            if (relative.find("/invalid/") != std::string::npos)
                continue;
            out.push_back(relative);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

/// A fresh, zero-RAM machine; nullptr when this build cannot create it
Emulator* Create(const Machine& machine)
{
    if (machine.ramKb == 0)
        return EmulatorTestHelper::CreateStandardEmulator(machine.model, LoggerLevel::LogError, RamPowerOn::Zero);
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM(
        "test-emulator", machine.model, machine.ramKb, LoggerLevel::LogError, nullptr,
        Config::RamPowerOnOverride(RamPowerOn::Zero));
    return emulator ? emulator.get() : nullptr;
}

/// One table row's value: "refused", "threw: ...", or the digest
std::string LoadAndDigest(Emulator* emulator, const std::string& fixture)
{
    try
    {
        if (!emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/" + fixture)))
            return "refused";
    }
    catch (const std::exception& e)
    {
        return std::string("threw: ") + e.what();
    }
    return SnapshotDigest::ToText(SnapshotDigest::Capture(emulator));
}

std::map<std::string, std::string> ReadGolden()
{
    std::map<std::string, std::string> table;
    std::ifstream in(TestPathHelper::GetTestDataPath(kGoldenFile));
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        const size_t tab = line.find('\t');
        if (tab != std::string::npos)
            table[line.substr(0, tab)] = line.substr(tab + 1);
    }
    return table;
}
}  // namespace

/// One test per machine (the 884 loads split into 17 shards-friendly pieces of about 0.3 s)
class SnapshotGolden_Test : public ::testing::TestWithParam<Machine>
{
};

// Booting a fresh machine per fixture is the point: the latches a Reset leaves alone must not leak from one fixture
// into the next, so each row starts from a new machine (about 6 ms each)
TEST_P(SnapshotGolden_Test, EveryFixtureLeavesTheRecordedState)
{
    if (std::getenv("UNREAL_SNAPSHOT_GOLDEN_UPDATE"))
        GTEST_SKIP() << "rewriting the table (SnapshotGoldenRewrite_Test)";

    const Machine& machine = GetParam();
    const std::vector<std::string> fixtures = Fixtures();
    ASSERT_GE(fixtures.size(), 40u) << "the fixture folders are missing";
    const std::map<std::string, std::string> golden = ReadGolden();
    ASSERT_FALSE(golden.empty()) << kGoldenFile << " is missing";

    for (const std::string& fixture : fixtures)
    {
        Emulator* emulator = Create(machine);
        ASSERT_NE(emulator, nullptr) << "cannot create " << machine.tag;
        const std::string key = fixture + "@" + machine.tag;
        const std::string actual = LoadAndDigest(emulator, fixture);
        EmulatorTestHelper::CleanupEmulator(emulator);

        const auto expected = golden.find(key);
        if (expected == golden.end())
            ADD_FAILURE() << "no golden row for " << key << " (" << actual << ")";
        else
            EXPECT_EQ(actual, expected->second) << key;
    }
}

INSTANTIATE_TEST_SUITE_P(Machines, SnapshotGolden_Test, ::testing::ValuesIn(kMachines),
                         [](const ::testing::TestParamInfo<Machine>& info) { return std::string(info.param.tag); });

// Writes the table; does nothing unless UNREAL_SNAPSHOT_GOLDEN_UPDATE is set (about 5 s: every fixture on every machine)
TEST(SnapshotGoldenRewrite_Test, RewritesTheTableWhenAsked)
{
    if (!std::getenv("UNREAL_SNAPSHOT_GOLDEN_UPDATE"))
        GTEST_SKIP() << "set UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 to rewrite " << kGoldenFile;

    std::ostringstream written;
    written << "# Golden commit digests (snapshot pipeline P0, PLAN #84). Rewritten by\n"
            << "# UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 core-tests --gtest_filter='SnapshotGoldenRewrite*'; see\n"
            << "# core/tests/loaders/snapshot/snapshotgolden_test.cpp. <fixture>@<machine><TAB>refused | threw: | digest\n";
    for (const Machine& machine : kMachines)
    {
        for (const std::string& fixture : Fixtures())
        {
            Emulator* emulator = Create(machine);
            ASSERT_NE(emulator, nullptr) << "cannot create " << machine.tag;
            written << fixture << '@' << machine.tag << '\t' << LoadAndDigest(emulator, fixture) << '\n';
            EmulatorTestHelper::CleanupEmulator(emulator);
        }
    }
    std::ofstream out(TestPathHelper::GetTestDataPath(kGoldenFile), std::ios::binary);
    out << written.str();
}

// ---------------------------------------------------------------------------------------------------------------------
// The suspected defects (proposal section 3), pinned as documented current behavior. A step that fixes one flips the
// expectation named in its comment, in the same commit as the golden rows it changes.
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
const char* kAcrossTheEdge = "loaders/sna/across-the-edge-second.sna";   // 128K SNA, #7FFD = #17 (bank 7, ROM 1, unlocked)

/// A copy of a 128K SNA with its #7FFD byte replaced (the snapshot header's byte 49181)
std::string WithPaging(const char* fixture, uint8_t port7ffd, const char* leaf)
{
    std::ifstream in(TestPathHelper::GetTestDataPath(fixture), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bytes[49181] = static_cast<char>(port7ffd);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath(leaf);
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path;
}

struct Loaded
{
    bool ok = false;
    uint8_t p7ffd = 0;
    uint16_t bank3 = 0;   ///< the page at #C000 (0xFFFF = unmapped)
    uint16_t rom = 0;
};

Loaded LoadOn(const Machine& machine, const std::string& path)
{
    Loaded r;
    Emulator* emulator = Create(machine);
    if (!emulator)
        return r;
    r.ok = emulator->LoadSnapshot(path);
    EmulatorContext* context = emulator->GetContext();
    r.p7ffd = context->emulatorState.p7FFD;
    r.bank3 = context->pMemory->GetRAMPageForBank(3);
    r.rom = context->pMemory->GetROMPage();
    EmulatorTestHelper::CleanupEmulator(emulator);
    return r;
}

const Machine& Find(const char* tag)
{
    for (const Machine& m : kMachines)
    {
        if (std::string(m.tag) == tag)
            return m;
    }
    ADD_FAILURE() << "unknown machine " << tag;
    return kMachines[0];
}
}  // namespace

// DEFECT (P5, "pentagon1024-compat"): a 128K file whose #7FFD has the lock bit maps page 32 + n on a Pentagon 1024,
// because the reset leaves #EFF7 = 0 (1 MB paging) and bit 5 then extends the page number. Expected after the fix: 7
TEST(SnapshotDefects_Test, Pentagon1024LockedFileMapsTheWrongBank)
{
    const std::string locked = WithPaging(kAcrossTheEdge, 0x37, "defect-locked.sna");
    const std::string open = TestPathHelper::GetTestDataPath(kAcrossTheEdge);

    EXPECT_EQ(LoadOn(Find("PENTAGON128"), locked).bank3, 7u) << "the 128K Pentagon is right";
    EXPECT_EQ(LoadOn(Find("PENTAGON1024"), open).bank3, 7u) << "unlocked: right";
    const Loaded p1024 = LoadOn(Find("PENTAGON1024"), locked);
    EXPECT_TRUE(p1024.ok);
    EXPECT_EQ(p1024.bank3, 39u) << "locked: 32 + 7 (the defect)";
    std::remove(locked.c_str());
}

// DEFECT (P5, "atm"): on the ATM3 and ATM710 the pager is not in its 128K form after the reset, so a 128K file
// leaves #C000 unmapped. The other clones map bank 7. Expected after the fix: 7 everywhere
TEST(SnapshotDefects_Test, AtmFamilyLeavesTheTopWindowUnmapped)
{
    const std::string path = TestPathHelper::GetTestDataPath(kAcrossTheEdge);
    for (const char* tag : {"ATM3", "ATM710"})
    {
        const Loaded r = LoadOn(Find(tag), path);
        EXPECT_TRUE(r.ok) << tag;
        EXPECT_EQ(r.bank3, 0xFFFFu) << tag << ": unmapped (the defect)";
    }
    for (const char* tag : {"ATM450", "TSL", "PROFI", "PENTAGON128", "SCORPION"})
        EXPECT_EQ(LoadOn(Find(tag), path).bank3, 7u) << tag;
}

// DEFECT (P5, Q1): a 128K file loads on a 48K machine into pages the machine never shows. Expected after the fix:
// refused with a reason (a 128K file locked with bank 0 on top is accepted as the 48K state it is)
TEST(SnapshotDefects_Test, A128kFileIsAcceptedOnA48kMachine)
{
    const Loaded r = LoadOn(Find("48K"), TestPathHelper::GetTestDataPath(kAcrossTheEdge));
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.p7ffd, 0x17) << "a 48K machine now holds a 128K paging byte";
}

// FIXED (P4, the Sprinter's Z5, 2026-10-05): SNA used to write physical pages 0-7 on the Sprinter, wrong even in the BIOS's
// own ZX mode (its cells put Spectrum banks 1 / 3 / 4 / 6 / 7 in pages #ED / #EF / #F0 / #EE / #F1) and fatal at the DSS
// prompt. The machine's own policy ('sprinter-zx', sprinterzxsnapshot.h) now commits through the cell table, and refuses
// outside a Spectrum mode; the cell-table behavior is tested in sprinterzxsnapshot_test.cpp. A fresh Sprinter is not in a
// mode, so the load is refused and the golden rows of SPRINTER are "refused"
TEST(SnapshotDefects_Test, SprinterRefusesASpectrumSnapshotOutsideItsZxMode)
{
    const Loaded r = LoadOn(Find("SPRINTER"), TestPathHelper::GetTestDataPath(kAcrossTheEdge));
    EXPECT_FALSE(r.ok);
}

// DEFECT (P5, Q2): a 48K SNA leaves #7FFD unlocked (#10) while a 48K Z80 locks it (#30); the two formats disagree.
// Expected after the fix: one shared transform that locks like Z80 does
TEST(SnapshotDefects_Test, The48kFormatsDisagreeAboutTheLock)
{
    for (const char* tag : {"128K", "PENTAGON128"})
    {
        EXPECT_EQ(LoadOn(Find(tag), TestPathHelper::GetTestDataPath("loaders/sna/z80full.sna")).p7ffd, 0x10) << tag;
        EXPECT_EQ(LoadOn(Find(tag), TestPathHelper::GetTestDataPath("loaders/z80/newbench.z80")).p7ffd, 0x30) << tag;
    }
}
