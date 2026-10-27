/// @file snapshotsavegolden_test.cpp
/// @brief Golden save digests (snapshot pipeline P6, PLAN #84, test SP-6): the SNA / Z80 / SZX file every creatable machine
/// writes from one fixed state (RAM filled with a pattern, a fixed CPU, #7FFD open or locked), hashed and compared with
/// testdata/loaders/golden/save-digests.txt.
///
/// The table was first generated on the code BEFORE P6 and records what it wrote, defects included; P6 changes a row only
/// where its commit says so (see the snapshot TODO): the locked 128K machine (the old SNA writer turned the lock bit into a
/// 48K file, the Z80 writer into a 48K model with three pages), machines that now refuse ("refused") instead of writing a
/// file no machine could read back.
///
/// Update after an approved change: UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 core-tests --gtest_filter='SnapshotSaveGoldenRewrite*'

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "loaders/snapshot/snapshotimage.h"

namespace
{
struct Machine
{
    const char* model;
    uint32_t ramKb;   ///< 0 = the model's default
    const char* tag;
};

const Machine kMachines[] = {
    {"48K", 0, "48K"},          {"128k", 0, "128K"},       {"PLUS2", 0, "PLUS2"},
    {"PLUS2A", 0, "PLUS2A"},    {"PLUS3", 0, "PLUS3"},     {"PENTAGON", 0, "PENTAGON128"},
    {"PENTAGON", 512, "PENTAGON512"}, {"PENTAGON", 1024, "PENTAGON1024"}, {"SCORPION", 0, "SCORPION"},
    {"PROFSCORP", 0, "PROFSCORP"}, {"ATM710", 0, "ATM710"}, {"ATM3", 0, "ATM3"},
    {"ATM450", 0, "ATM450"},    {"PROFI", 0, "PROFI"},     {"PROFI3", 0, "PROFI3"},
    {"TSL", 0, "TSL"},          {"SPRINTER", 0, "SPRINTER"},
};

struct Scenario
{
    const char* name;
    uint8_t p7ffd;   ///< written through the port decoder on machines that have #7FFD
};

const Scenario kScenarios[] = {{"open", 0x13}, {"locked", 0x34}};
const char* kFormats[] = {"sna", "z80", "szx"};

const char* kGoldenFile = "loaders/golden/save-digests.txt";

Emulator* Create(const Machine& machine)
{
    SoundCardScope sound(TestSound::TurboSound);
    if (machine.ramKb == 0)
        return EmulatorTestHelper::CreateStandardEmulator(machine.model, LoggerLevel::LogError, RamPowerOn::Zero);
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModelAndRAM(
        "test-emulator", machine.model, machine.ramKb, LoggerLevel::LogError, nullptr,
        Config::RamPowerOnOverride(RamPowerOn::Zero));
    return emulator ? emulator.get() : nullptr;
}

/// One fixed state: a pattern in every RAM page, a CPU with a stack inside RAM, the scenario's #7FFD
void Prepare(Emulator* emulator, const Scenario& scenario)
{
    EmulatorContext* context = emulator->GetContext();
    Memory& memory = *context->pMemory;
    const uint32_t pages = context->config.ramsize ? context->config.ramsize / 16 : 8;
    for (uint32_t page = 0; page < pages; page++)
    {
        uint8_t* bytes = memory.RAMPageAddress(static_cast<uint16_t>(page));
        if (!bytes)
            continue;
        for (uint32_t i = 0; i < 16384; i++)
            bytes[i] = static_cast<uint8_t>((i * 7 + page * 37 + (i >> 8)) & 0xFF);
    }
    if (context->config.mem_model != MM_SPECTRUM48)
        context->pPortDecoder->DecodePortOut(0x7FFD, scenario.p7ffd, 0x8000);
    Z80& z80 = *context->pCore->GetZ80();
    z80.pc = 0x8123;
    z80.sp = 0xBF00;
    z80.af = 0x1234;
    z80.bc = 0x2345;
    z80.de = 0x3456;
    z80.hl = 0x4567;
    z80.ix = 0x5678;
    z80.iy = 0x6789;
    z80.alt.af = 0x789A;
    z80.alt.bc = 0x89AB;
    z80.alt.de = 0x9ABC;
    z80.alt.hl = 0xABCD;
    z80.i = 0x3F;
    z80.r_low = 0x25;
    z80.im = 2;
    z80.iff1 = 1;
    z80.iff2 = 1;
}

/// The SZX creator block ends with the git commit of the build that wrote the file; that would change every row with each
/// commit and with the CI checkout. Cut it (and fix the block size) so the digest covers only what the machine state decides.
void DropBuildFingerprint(std::vector<uint8_t>& bytes)
{
    constexpr size_t kHeader = 8;          // "ZXST", version, machine, flags
    constexpr size_t kCreatorFixed = 36;   // name[32], major, minor
    if (bytes.size() < kHeader + 8 + kCreatorFixed || std::string(bytes.begin() + kHeader, bytes.begin() + kHeader + 4) != "CRTR")
        return;
    const size_t sizeAt = kHeader + 4;
    const size_t size = bytes[sizeAt] | bytes[sizeAt + 1] << 8 | bytes[sizeAt + 2] << 16 | static_cast<size_t>(bytes[sizeAt + 3]) << 24;
    if (size <= kCreatorFixed || kHeader + 8 + size > bytes.size())
        return;
    bytes.erase(bytes.begin() + kHeader + 8 + kCreatorFixed, bytes.begin() + kHeader + 8 + size);
    bytes[sizeAt] = kCreatorFixed;
    bytes[sizeAt + 1] = bytes[sizeAt + 2] = bytes[sizeAt + 3] = 0;
}

/// "refused" or "<size> <hash of the file>"
std::string SaveAndHash(Emulator* emulator, const std::string& format)
{
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("save-golden." + format);
    if (!emulator->SaveSnapshot(path))
        return "refused";
    std::ifstream in(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    DropBuildFingerprint(bytes);
    std::remove(path.c_str());
    return std::to_string(bytes.size()) + " " + snapshot::HashText(bytes);
}

std::string Key(const Scenario& scenario, const Machine& machine, const char* format)
{
    return std::string(scenario.name) + "@" + machine.tag + "." + format;
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

class SnapshotSaveGolden_Test : public ::testing::TestWithParam<Machine>
{
};

TEST_P(SnapshotSaveGolden_Test, EveryMachineWritesTheRecordedFile)
{
    if (std::getenv("UNREAL_SNAPSHOT_GOLDEN_UPDATE"))
        GTEST_SKIP() << "rewriting the table (SnapshotSaveGoldenRewrite_Test)";
    const std::map<std::string, std::string> golden = ReadGolden();
    ASSERT_FALSE(golden.empty()) << kGoldenFile << " is missing";

    const Machine& machine = GetParam();
    for (const Scenario& scenario : kScenarios)
    {
        for (const char* format : kFormats)
        {
            Emulator* emulator = Create(machine);
            ASSERT_NE(emulator, nullptr) << "cannot create " << machine.tag;
            Prepare(emulator, scenario);
            const std::string actual = SaveAndHash(emulator, format);
            EmulatorTestHelper::CleanupEmulator(emulator);

            const std::string key = Key(scenario, machine, format);
            const auto expected = golden.find(key);
            if (expected == golden.end())
                ADD_FAILURE() << "no golden row for " << key << " (" << actual << ")";
            else
                EXPECT_EQ(actual, expected->second) << key;
        }
    }
}

INSTANTIATE_TEST_SUITE_P(Machines, SnapshotSaveGolden_Test, ::testing::ValuesIn(kMachines),
                         [](const ::testing::TestParamInfo<Machine>& info) { return std::string(info.param.tag); });

TEST(SnapshotSaveGoldenRewrite_Test, RewritesTheTableWhenAsked)
{
    if (!std::getenv("UNREAL_SNAPSHOT_GOLDEN_UPDATE"))
        GTEST_SKIP() << "set UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 to rewrite " << kGoldenFile;

    std::ostringstream written;
    written << "# Golden save digests (snapshot pipeline P6, PLAN #84). Rewritten by\n"
            << "# UNREAL_SNAPSHOT_GOLDEN_UPDATE=1 core-tests --gtest_filter='SnapshotSaveGoldenRewrite*'; see\n"
            << "# core/tests/loaders/snapshot/snapshotsavegolden_test.cpp. <scenario>@<machine>.<format><TAB>refused | size hash\n";
    for (const Machine& machine : kMachines)
    {
        for (const Scenario& scenario : kScenarios)
        {
            for (const char* format : kFormats)
            {
                Emulator* emulator = Create(machine);
                ASSERT_NE(emulator, nullptr) << "cannot create " << machine.tag;
                Prepare(emulator, scenario);
                written << Key(scenario, machine, format) << '\t' << SaveAndHash(emulator, format) << '\n';
                EmulatorTestHelper::CleanupEmulator(emulator);
            }
        }
    }
    std::ofstream out(TestPathHelper::GetTestDataPath(kGoldenFile), std::ios::binary);
    out << written.str();
}
