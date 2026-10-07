/// @file snapshotpipeline_test.cpp
/// @brief The pipeline's report through the Emulator (snapshot pipeline P1, PLAN #84): every load path leaves a
/// SnapshotReport that names the commit, the verdicts and the format's outcomes; a refusal says why.

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/portdecoder.h"
#include "loaders/snapshot/loader_sna.h"
#include "loaders/snapshot/snapshotlauncher.h"
#include "loaders/snapshot/snapshotpipeline.h"
#include "loaders/snapshot/snapshotpolicy.h"

namespace
{
std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
}  // namespace

class SnapshotPipeline_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

    Emulator* _emulator = nullptr;
};

TEST_F(SnapshotPipeline_Test, ASnaLoadReportsTheLegacyCommit)
{
    EXPECT_TRUE(_emulator->LastSnapshotReport().format.empty()) << "nothing loaded yet";
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_EQ(report.format, "sna");
    EXPECT_EQ(report.machineHint, "128k-family");
    EXPECT_EQ(report.commit, "legacy");
    EXPECT_FALSE(report.refused);
    EXPECT_EQ(report.verdicts.size(), 1u);
    EXPECT_NE(report.ToText().find("loaded by the legacy commit"), std::string::npos);
}

TEST_F(SnapshotPipeline_Test, AZ80LoadReportsTheMachineTheFileWasMadeOn)
{
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/z80/dizzyx.z80")));
    EXPECT_EQ(_emulator->LastSnapshotReport().format, "z80");
    EXPECT_EQ(_emulator->LastSnapshotReport().machineHint, "pentagon128");
}

TEST_F(SnapshotPipeline_Test, AnSzxLoadCarriesTheBlockOutcomes)
{
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-pentagon.szx")));
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_EQ(report.format, "szx");
    EXPECT_EQ(report.machineHint, "pentagon128");
    EXPECT_FALSE(report.items.empty());
    bool z80r = false;
    for (const snapshot::ReportItem& item : report.items)
        z80r = z80r || (item.item == "Z80R" && item.outcome == snapshot::Outcome::Applied);
    EXPECT_TRUE(z80r) << report.ToText();
}

TEST_F(SnapshotPipeline_Test, ARefusedSzxSaysWhyInTheReport)
{
    // A 48K SZX on a Pentagon: the format's model check refuses
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-48.szx")));
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_EQ(report.format, "szx");
    EXPECT_TRUE(report.refused);
    EXPECT_FALSE(report.reason.empty());
    EXPECT_NE(report.ToText().find("refused"), std::string::npos);
}

TEST_F(SnapshotPipeline_Test, ALoadFromMemoryLeavesTheSameReport)
{
    const std::vector<uint8_t> sna = ReadFile(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"));
    ASSERT_TRUE(_emulator->LoadSnapshotData(sna, "sna", "upload.sna"));
    EXPECT_EQ(_emulator->LastSnapshotReport().format, "sna");
    EXPECT_EQ(_emulator->LastSnapshotReport().commit, "legacy");

    const std::vector<uint8_t> szx = ReadFile(TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-pentagon.szx"));
    ASSERT_TRUE(_emulator->LoadSnapshotData(szx, "szx", "upload.szx"));
    EXPECT_EQ(_emulator->LastSnapshotReport().format, "szx");
    EXPECT_FALSE(_emulator->LastSnapshotReport().items.empty());
}

TEST_F(SnapshotPipeline_Test, TheReportIsAStateNodeEverySurfaceCanReturn)
{
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));
    const StateNode node = _emulator->LastSnapshotReport().ToStateNode();
    ASSERT_NE(node.find("commit"), nullptr);
    EXPECT_EQ(node.find("commit")->s, "legacy");
    ASSERT_NE(node.find("refused"), nullptr);
    EXPECT_FALSE(node.find("refused")->b);
    ASSERT_NE(node.find("verdicts"), nullptr);
    EXPECT_EQ(node.find("verdicts")->items.size(), 1u);
}

// ---------------------------------------------------------------------------------------------------------------------
// The plan's order and the registry (SP-4, with fake policies)
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/// A policy that answers what it is told and counts what it is asked
class FakePolicy : public snapshot::ISnapshotCommitPolicy
{
public:
    FakePolicy(std::string name, snapshot::Verdict verdict, bool commitResult = true)
        : _name(std::move(name)), _verdict(std::move(verdict)), _commitResult(commitResult)
    {
    }
    std::string Name() const override { return _name; }
    snapshot::Verdict Examine(const snapshot::Image& image, EmulatorContext&) const override
    {
        ++examined;
        lastFormat = image.format;
        return _verdict;
    }
    bool Commit(const snapshot::Image&, EmulatorContext&, snapshot::Report& report) override
    {
        ++committed;
        report.Add("fake", snapshot::Outcome::Applied, "written by " + _name);
        return _commitResult;
    }

    mutable int examined = 0;
    int committed = 0;
    mutable std::string lastFormat;

private:
    std::string _name;
    snapshot::Verdict _verdict;
    bool _commitResult;
};

const char* kSna = "loaders/sna/action.sna";

uint16_t PcOfAction()
{
    const std::vector<uint8_t> f = ReadFile(TestPathHelper::GetTestDataPath(kSna));
    return static_cast<uint16_t>(f[49179] | f[49180] << 8);
}
}  // namespace

class SnapshotPlan_Test : public SnapshotPipeline_Test
{
protected:
    void TearDown() override
    {
        _emulator->GetContext()->pPortDecoder->SetSnapshotPolicy(nullptr);
        snapshot::SnapshotPolicies::Unregister("fake-caller");
        snapshot::SnapshotPolicies::Unregister("fake-other");
        SnapshotPipeline_Test::TearDown();
    }

    uint16_t Pc() { return _emulator->GetContext()->pCore->GetZ80()->pc; }
    void SetMachinePolicy(FakePolicy& policy) { _emulator->GetContext()->pPortDecoder->SetSnapshotPolicy(&policy); }
};

TEST_F(SnapshotPlan_Test, NoPolicyIsTheLegacyCommit)
{
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kSna)));
    EXPECT_EQ(Pc(), PcOfAction());
    EXPECT_EQ(_emulator->LastSnapshotReport().commit, "legacy");
}

TEST_F(SnapshotPlan_Test, AMachinePolicyThatTakesItCommitsInsteadOfTheLegacyCode)
{
    FakePolicy machine("fake-machine", snapshot::Verdict::Take("mine"));
    SetMachinePolicy(machine);
    const uint16_t before = Pc();
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kSna)));
    EXPECT_EQ(machine.examined, 1);
    EXPECT_EQ(machine.committed, 1);
    EXPECT_EQ(machine.lastFormat, "sna");
    EXPECT_NE(Pc(), PcOfAction()) << "the legacy commit did not run";
    EXPECT_EQ(Pc(), before);
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_EQ(report.commit, "fake-machine");
    ASSERT_EQ(report.verdicts.size(), 1u);
    EXPECT_NE(report.verdicts[0].find("takes it: mine"), std::string::npos);
    ASSERT_FALSE(report.items.empty());
    EXPECT_EQ(report.items[0].note, "written by fake-machine");
}

TEST_F(SnapshotPlan_Test, AMachinePolicyThatDeclinesFallsThroughToLegacy)
{
    FakePolicy machine("fake-machine", snapshot::Verdict::Decline());
    SetMachinePolicy(machine);
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kSna)));
    EXPECT_EQ(machine.examined, 1);
    EXPECT_EQ(machine.committed, 0);
    EXPECT_EQ(Pc(), PcOfAction());
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_EQ(report.commit, "legacy");
    ASSERT_EQ(report.verdicts.size(), 2u) << "who was asked, then the default";
    EXPECT_NE(report.verdicts[0].find("declined"), std::string::npos);
}

TEST_F(SnapshotPlan_Test, AMachineRefusalWritesNothingAndSaysWhy)
{
    FakePolicy machine("fake-machine", snapshot::Verdict::Refuse("not in the right mode", "zx_mode"));
    SetMachinePolicy(machine);
    const uint16_t before = Pc();
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kSna)));
    EXPECT_EQ(machine.committed, 0);
    EXPECT_EQ(Pc(), before) << "the machine is as it was";
    const snapshot::Report& report = _emulator->LastSnapshotReport();
    EXPECT_TRUE(report.refused);
    EXPECT_EQ(report.reason, "not in the right mode");
    EXPECT_EQ(report.needs, "zx_mode");
}

TEST_F(SnapshotPlan_Test, TheCallersNameBeatsTheMachinePolicy)
{
    FakePolicy machine("fake-machine", snapshot::Verdict::Take());
    SetMachinePolicy(machine);
    auto caller = std::make_shared<FakePolicy>("fake-caller", snapshot::Verdict::Take());
    snapshot::SnapshotPolicies::Register(caller);

    LoaderSNA loader(_emulator->GetContext(), TestPathHelper::GetTestDataPath(kSna));
    snapshot::Options options;
    options.commit = "fake-caller";
    loader.SetOptions(options);
    ASSERT_TRUE(loader.load());
    EXPECT_EQ(caller->committed, 1);
    EXPECT_EQ(machine.examined, 0) << "the machine is not even asked";
    EXPECT_EQ(loader.GetSnapshotReport().commit, "fake-caller");
}

TEST_F(SnapshotPlan_Test, LegacyForcesTodaysCommitOverAMachinePolicy)
{
    FakePolicy machine("fake-machine", snapshot::Verdict::Take());
    SetMachinePolicy(machine);

    LoaderSNA loader(_emulator->GetContext(), TestPathHelper::GetTestDataPath(kSna));
    snapshot::Options options;
    options.commit = "legacy";
    loader.SetOptions(options);
    ASSERT_TRUE(loader.load());
    EXPECT_EQ(machine.examined, 0);
    EXPECT_EQ(Pc(), PcOfAction());
    EXPECT_EQ(loader.GetSnapshotReport().commit, "legacy");
}

TEST_F(SnapshotPlan_Test, AnUnknownNameIsRefusedWithTheKnownOnes)
{
    snapshot::SnapshotPolicies::Register(std::make_shared<FakePolicy>("fake-other", snapshot::Verdict::Take()));
    const uint16_t before = Pc();
    LoaderSNA loader(_emulator->GetContext(), TestPathHelper::GetTestDataPath(kSna));
    snapshot::Options options;
    options.commit = "nonesuch";
    loader.SetOptions(options);
    EXPECT_FALSE(loader.load());
    EXPECT_EQ(Pc(), before);
    const snapshot::Report& report = loader.GetSnapshotReport();
    EXPECT_TRUE(report.refused);
    EXPECT_NE(report.reason.find("'nonesuch'"), std::string::npos);
    EXPECT_NE(report.reason.find("known: fake-other, legacy"), std::string::npos) << report.reason;
}

TEST_F(SnapshotPlan_Test, ANamedPolicyThatDeclinesIsAnErrorNotAFallback)
{
    snapshot::SnapshotPolicies::Register(std::make_shared<FakePolicy>("fake-caller", snapshot::Verdict::Decline()));
    LoaderSNA loader(_emulator->GetContext(), TestPathHelper::GetTestDataPath(kSna));
    snapshot::Options options;
    options.commit = "fake-caller";
    loader.SetOptions(options);
    EXPECT_FALSE(loader.load());
    EXPECT_TRUE(loader.GetSnapshotReport().refused);
    EXPECT_NE(loader.GetSnapshotReport().reason.find("does not apply"), std::string::npos);
}

TEST_F(SnapshotPlan_Test, AFailedPolicyCommitIsARefusal)
{
    FakePolicy machine("fake-machine", snapshot::Verdict::Take(), /*commitResult=*/false);
    SetMachinePolicy(machine);
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(kSna)));
    EXPECT_EQ(machine.committed, 1);
    EXPECT_TRUE(_emulator->LastSnapshotReport().refused);
    EXPECT_NE(_emulator->LastSnapshotReport().reason.find("fake-machine"), std::string::npos);
}

TEST_F(SnapshotPlan_Test, EveryFormatAsksThePolicy)
{
    FakePolicy machine("fake-machine", snapshot::Verdict::Take());
    SetMachinePolicy(machine);
    for (const char* file : {"loaders/sna/action.sna", "loaders/z80/dizzyx.z80",
                             "loaders/szx/libspectrum/synth-pentagon.szx"})
    {
        ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(file))) << file;
        EXPECT_EQ(_emulator->LastSnapshotReport().commit, "fake-machine") << file;
    }
    EXPECT_EQ(machine.examined, 3);
    EXPECT_EQ(machine.committed, 3);
    // And from memory
    const std::vector<uint8_t> szx = ReadFile(TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-pentagon.szx"));
    ASSERT_TRUE(_emulator->LoadSnapshotData(szx, "szx", "memory.szx"));
    EXPECT_EQ(machine.committed, 4);
}

TEST(SnapshotPolicies_Test, TheRegistryKeepsNamesSortedAndReplacesByName)
{
    // The machines' own policies are known from the start ('sprinter-zx'), 'legacy' is the plan's default
    const std::vector<std::string> builtin = snapshot::SnapshotPolicies::Names();
    EXPECT_EQ(builtin, (std::vector<std::string>{"legacy", "sprinter-zx"}));
    EXPECT_EQ(snapshot::SnapshotPolicies::Find("fake-a"), nullptr);

    auto a = std::make_shared<FakePolicy>("fake-a", snapshot::Verdict::Take());
    auto b = std::make_shared<FakePolicy>("fake-b", snapshot::Verdict::Decline());
    snapshot::SnapshotPolicies::Register(b);
    snapshot::SnapshotPolicies::Register(a);
    EXPECT_EQ(snapshot::SnapshotPolicies::Names(), (std::vector<std::string>{"fake-a", "fake-b", "legacy", "sprinter-zx"}));
    EXPECT_EQ(snapshot::SnapshotPolicies::Find("fake-a"), a.get());

    auto replacement = std::make_shared<FakePolicy>("fake-a", snapshot::Verdict::Refuse("x"));
    snapshot::SnapshotPolicies::Register(replacement);
    EXPECT_EQ(snapshot::SnapshotPolicies::Find("fake-a"), replacement.get());

    snapshot::SnapshotPolicies::Unregister("fake-a");
    snapshot::SnapshotPolicies::Unregister("fake-b");
    snapshot::SnapshotPolicies::Unregister("never-there");
    EXPECT_EQ(snapshot::SnapshotPolicies::Names(), builtin);
}

// ---------------------------------------------------------------------------------------------------------------------
// The shared fit check (P5, owner Q1): the memory a snapshot carries must exist on the machine
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
uint64_t RamHash(Memory& memory, unsigned pages)
{
    uint64_t h = 14695981038346656037ull;
    for (unsigned page = 0; page < pages; ++page)
    {
        const uint8_t* bytes = memory.RAMPageAddress(static_cast<uint16_t>(page));
        for (size_t i = 0; i < PAGE_SIZE; ++i)
        {
            h ^= bytes[i];
            h *= 1099511628211ull;
        }
    }
    return h;
}
}  // namespace

TEST(SnapshotFit_Test, A128kSnapshotOnA48kMachineIsRefusedWithTheReason)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    Memory& memory = *emulator->GetContext()->pMemory;
    const uint64_t before = RamHash(memory, 8);

    for (const char* file : {"loaders/sna/action.sna", "loaders/z80/dizzyx.z80"})
    {
        EXPECT_FALSE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(file))) << file;
        const snapshot::Report& report = emulator->LastSnapshotReport();
        EXPECT_TRUE(report.refused) << file;
        EXPECT_EQ(report.needs, "model:128K") << file;
        EXPECT_NE(report.reason.find("128K snapshot"), std::string::npos) << report.reason;
        EXPECT_NE(report.reason.find("48K"), std::string::npos) << report.reason;
        EXPECT_NE(report.reason.find("Pentagon"), std::string::npos) << "it names what would work";
    }
    EXPECT_EQ(RamHash(memory, 8), before) << "a refusal writes nothing";

    // inspect says so before a load is tried
    StateNode inspected;
    std::string error;
    ASSERT_TRUE(emulator->InspectSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"), {}, inspected, error)) << error;
    EXPECT_FALSE(inspected.find("would_load")->b);
    EXPECT_EQ(inspected.find("plan")->find("needs")->s, "model:128K");

    // A 48K file is all a 48K holds
    EXPECT_TRUE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/z80full.sna")));
    EXPECT_FALSE(emulator->LastSnapshotReport().refused);
    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(SnapshotFit_Test, BanksBeyondTheMachinesRamAreRefused)
{
    // The Scorpion's 256K snapshot holds banks 8-15: a 128K machine has banks 0-7
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("128k", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(emulator, nullptr);
    const std::string scorpion = TestPathHelper::GetTestDataPath("loaders/z80/libspectrum/synth-scorpion.z80");
    EXPECT_FALSE(emulator->LoadSnapshot(scorpion));
    const snapshot::Report& report = emulator->LastSnapshotReport();
    EXPECT_TRUE(report.refused);
    EXPECT_EQ(report.needs, "ram:256K");
    EXPECT_NE(report.reason.find("bank 15"), std::string::npos) << report.reason;
    EXPECT_NE(report.reason.find("at least 256 KB"), std::string::npos) << report.reason;
    // A 128K file still loads there
    EXPECT_TRUE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));
    EmulatorTestHelper::CleanupEmulator(emulator);

    // A machine that has the banks takes it (the Scorpion's own, and a Pentagon with 512 KB)
    Emulator* scorp = EmulatorTestHelper::CreateStandardEmulator("SCORPION", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(scorp, nullptr);
    EXPECT_TRUE(scorp->LoadSnapshot(scorpion));
    EmulatorTestHelper::CleanupEmulator(scorp);
}

TEST_F(SnapshotPlan_Test, AMachinePolicyIsAskedBeforeTheFitCheck)
{
    // The fit check is the default: a machine that owns a policy decides for itself (the Sprinter's own rules)
    FakePolicy machine("fake-machine", snapshot::Verdict::Take());
    SetMachinePolicy(machine);
    ASSERT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/z80/libspectrum/synth-scorpion.z80")))
        << "the policy took it; the 128 KB Pentagon's fit check never ran";
    EXPECT_EQ(machine.committed, 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// The ATM family (P5): a snapshot lands in RAM and the picture is the Pentagon's
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
/// FNV-1a over the frame the machine shows
uint64_t PictureHash(Emulator* emulator)
{
    uint32_t* frame = nullptr;
    size_t size = 0;
    emulator->GetContext()->pScreen->GetFramebufferData(&frame, &size);
    uint64_t h = 14695981038346656037ull;
    for (size_t i = 0; i < size / sizeof(uint32_t); ++i)
    {
        h ^= frame[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t PictureAfterLoad(const char* model, const std::string& file)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
    EXPECT_NE(emulator, nullptr) << model;
    if (!emulator)
        return 0;
    EXPECT_TRUE(emulator->LoadSnapshot(file)) << model << " " << file;
    emulator->RunNFrames(3);
    const uint64_t hash = PictureHash(emulator);
    EmulatorTestHelper::CleanupEmulator(emulator);
    return hash;
}
}  // namespace

// The reset of an ATM leaves the pager off (or the system ROM on): the snapshot used to land in a machine whose RAM was not in
// the address space. The pictures of static programs are compared with the Pentagon's on all three clones
TEST(SnapshotAtm_Test, TheSamePictureAsThePentagonOnEveryClone)
{
    for (const char* file : {"loaders/z80/dizzyx.z80", "loaders/sna/z80full.sna", "loaders/sna/Dizzy Y.sna"})
    {
        const std::string path = TestPathHelper::GetTestDataPath(file);
        const uint64_t pentagon = PictureAfterLoad("PENTAGON", path);
        for (const char* clone : {"ATM710", "ATM3", "ATM450"})
            EXPECT_EQ(PictureAfterLoad(clone, path), pentagon) << clone << " " << file;
    }
}

TEST(SnapshotAtm_Test, TheMemoryManagerIsOnAndLaidOutLikeA128k)
{
    for (const char* model : {"ATM710", "ATM3"})
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(emulator, nullptr) << model;
        ASSERT_TRUE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/across-the-edge-second.sna"))) << model;
        const EmulatorState& state = emulator->GetContext()->emulatorState;
        Memory& memory = *emulator->GetContext()->pMemory;
        EXPECT_NE(state.aFF77 & 0x100, 0) << model << ": the manager (PEN) is on";
        EXPECT_NE(state.aFF77 & 0x200, 0) << model << ": ~CPM set, TR-DOS is not forced";
        EXPECT_EQ(state.flags & CF_TRDOS, 0) << model;
        EXPECT_EQ(memory.GetRAMPageForBank(1), 5u) << model;
        EXPECT_EQ(memory.GetRAMPageForBank(2), 2u) << model;
        EXPECT_EQ(memory.GetRAMPageForBank(3), 7u) << model << ": window 3 follows #7FFD";
        EXPECT_TRUE(memory.IsBank0ROM()) << model << ": window 0 is ROM";
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
    // The ATM450: ROM at #0000, not the system ROM
    Emulator* atm450 = EmulatorTestHelper::CreateStandardEmulator("ATM450", LoggerLevel::LogError, RamPowerOn::Zero);
    ASSERT_NE(atm450, nullptr);
    ASSERT_TRUE(atm450->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/across-the-edge-second.sna")));
    EXPECT_NE(atm450->GetContext()->emulatorState.aFE & 0x80, 0) << "ROM at #0000";
    EXPECT_EQ(atm450->GetContext()->emulatorState.aFB & 0x80, 0) << "not the system ROM (CPSYS off)";
    EmulatorTestHelper::CleanupEmulator(atm450);
}

// ---------------------------------------------------------------------------------------------------------------------
// TS-Conf (P5): window 0 shows the BASIC the snapshot selected
// ---------------------------------------------------------------------------------------------------------------------

// The reset leaves MEM_CONFIG in the normal mode: window 0 is ROM page 0 (the TS-BIOS image, an "unknown ROM") whatever #7FFD
// says, so a snapshot's program called the TS-BIOS where it expected BASIC. The commit now puts MEM_CONFIG in the mapped mode
// (window 0 = the {service, TR-DOS, 128, 48} group by ROM128 = #7FFD bit 4) with the 128K decode first
TEST(SnapshotTsConf_Test, WindowZeroShowsTheBasicTheSnapshotSelected)
{
    struct Case
    {
        const char* file;
        bool basic48;
        const char* what;
    };
    const Case cases[] = {
        {"loaders/sna/Dizzy Y 2.sna", false, "128K SNA, #7FFD = #00: BASIC-128"},
        {"loaders/sna/aytest_0.2.sna", true, "128K SNA, #7FFD = #10: BASIC-48"},
        {"loaders/sna/z80full.sna", true, "48K SNA"},
        {"loaders/z80/newbench.z80", true, "48K Z80 (#7FFD = #30)"},
        {"loaders/z80/BBG128.z80", false, "128K Z80, #7FFD = #30 -> bit 4 set: BASIC-48"},
    };
    for (const Case& c : cases)
    {
        Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("TSL", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(emulator, nullptr);
        Memory& memory = *emulator->GetContext()->pMemory;
        ASSERT_TRUE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(c.file))) << c.what;
        const uint8_t p7ffd = emulator->GetContext()->emulatorState.p7FFD;
        const bool basic48 = (p7ffd & 0x10) != 0;
        EXPECT_TRUE(memory.IsBank0ROM()) << c.what;
        EXPECT_EQ(memory.GetROMPage(), memory.GetROMPageFromAddress(basic48 ? memory.base_sos_rom : memory.base_128_rom))
            << c.what << " (#7FFD " << std::hex << int(p7ffd) << "): the ROM that #7FFD bit 4 names";
        if (std::string(c.file).find("BBG128") == std::string::npos)
            EXPECT_EQ(basic48, c.basic48) << c.what;
        EmulatorTestHelper::CleanupEmulator(emulator);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// The ROM latch agrees with the ROM shown (P5): a bank recompute keeps the BASIC the snapshot selected
// ---------------------------------------------------------------------------------------------------------------------

// The shipped configs say RESET=128, so after the reset the latches select BASIC-128. A 48K snapshot showed the 48K ROM (a bank
// pointer) while the latch still said BASIC-128: the first recompute of the banks (any #7FFD write, a TR-DOS page-in) swapped the
// ROM under the program, on every machine. The other tests reset to RM_SOS, where the latch already agrees and the bug hides
TEST(SnapshotRomLatch_Test, TheRomTheSnapshotSelectedSurvivesABankRecompute)
{
    struct Case
    {
        const char* file;
        bool is48k;   // a 48K snapshot: the ROM must be the 48K BASIC. A 128K one (#7FFD = #10 here) keeps what it selected
    };
    const Case cases[] = {
        {"loaders/sna/z80full.sna", true},      // 48K SNA
        {"loaders/z80/newbench.z80", true},     // 48K Z80
        {"loaders/sna/aytest_0.2.sna", false},  // 128K SNA, #7FFD = #10 (on a +2A / +3 that is ROM 1: no #1FFD in an SNA)
    };
    for (const char* model : {"PENTAGON", "128k", "PLUS2", "PLUS2A", "PLUS3", "TSL", "ATM710", "ATM3", "ATM450", "PROFI",
                              "SCORPION", "PROFSCORP"})
    {
        for (const Case& c : cases)
        {
            Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError, RamPowerOn::Zero);
            ASSERT_NE(emulator, nullptr) << model;
            emulator->GetContext()->config.reset_rom = RM_128;
            emulator->Reset();
            Memory& memory = *emulator->GetContext()->pMemory;
            ASSERT_TRUE(emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(c.file))) << model << " " << c.file;
            const uint16_t shown = memory.GetROMPage();
            if (c.is48k)
                EXPECT_EQ(shown, memory.GetROMPageFromAddress(memory.base_sos_rom)) << model << " " << c.file << ": the 48K BASIC";
            memory.UpdateZ80Banks();
            EXPECT_EQ(memory.GetROMPage(), shown) << model << " " << c.file << ": the latch agrees with the ROM shown";
            EmulatorTestHelper::CleanupEmulator(emulator);
        }
    }
}

// The plan announces a commit exactly once, and only when the snapshot WILL be committed: the emulator ends a TTD recording
// session from it, so a refused load must never fire it
class SnapshotBeforeCommit_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _options.beforeCommit = [this] { _announced++; };
    }
    void TearDown() override { EmulatorTestHelper::CleanupEmulator(_emulator); }

    Emulator* _emulator = nullptr;
    snapshot::Options _options;
    int _announced = 0;
};

TEST_F(SnapshotBeforeCommit_Test, AGoodLoadAnnouncesTheCommitOnce)
{
    for (const char* file : {"loaders/sna/action.sna", "loaders/z80/dizzyx.z80", "loaders/szx/libspectrum/synth-pentagon.szx"})
    {
        _announced = 0;
        EXPECT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(file), {}, _options)) << file;
        EXPECT_EQ(_announced, 1) << file;
    }
}

TEST_F(SnapshotBeforeCommit_Test, ARefusedLoadNeverAnnouncesIt)
{
    // an SZX of another model (the loader's own rule, now judged by the plan), an SPG on a machine that is not a TS-Conf
    for (const char* file : {"loaders/szx/libspectrum/synth-48.szx", "loaders/szx/libspectrum/synth-128.szx",
                             "machines/tsconf/spg/empty.spg"})
    {
        EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath(file), {}, _options)) << file;
        EXPECT_EQ(_announced, 0) << file << ": the load was refused, nothing was announced";
        EXPECT_TRUE(_emulator->LastSnapshotReport().refused) << file;
        EXPECT_FALSE(_emulator->LastSnapshotReport().reason.empty()) << file;
    }
    // the refusals carry what would work
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("machines/tsconf/spg/empty.spg"), {}, _options));
    EXPECT_EQ(_emulator->LastSnapshotReport().needs, "model:TSL");
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-128.szx"), {}, _options));
    EXPECT_EQ(_emulator->LastSnapshotReport().needs, "model:128k");
    EXPECT_EQ(_announced, 0);

    // a file that is no snapshot at all fails before the plan
    const std::string garbage = TestPathHelper::GetUniqueTestScratchPath("garbage.sna");
    {
        std::ofstream out(garbage, std::ios::binary);
        out << "not a snapshot";
    }
    EXPECT_FALSE(_emulator->LoadSnapshot(garbage, {}, _options));
    EXPECT_EQ(_announced, 0);
    std::remove(garbage.c_str());
}

TEST_F(SnapshotBeforeCommit_Test, TheCallersChoiceOfLegacyIsJudgedByThePlanToo)
{
    _options.commit = "legacy";
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("machines/tsconf/spg/empty.spg"), {}, _options));
    EXPECT_EQ(_announced, 0);
    EXPECT_FALSE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/szx/libspectrum/synth-128.szx"), {}, _options));
    EXPECT_EQ(_announced, 0);
    EXPECT_TRUE(_emulator->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"), {}, _options));
    EXPECT_EQ(_announced, 1);
}

// inspect (the dry plan) never announces
TEST_F(SnapshotBeforeCommit_Test, InspectNeverAnnouncesIt)
{
    StateNode inspected;
    std::string error;
    ASSERT_TRUE(_emulator->InspectSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"), _options, inspected, error)) << error;
    EXPECT_EQ(_announced, 0);
}

// A ZX-Poly takes a .zxp and nothing else (owner rule 2026-10-07): the four modules run in lockstep, so a snapshot of one machine
// means nothing there. Refused on load with the reason, nothing written on save, a module never replaced by another model
class SnapshotZXPoly_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::string error;
        _master = EmulatorManager::GetInstance()->CreateZXPolyMachine("zxpoly-snapshot", "128K", "", &error);
        ASSERT_TRUE(_master) << error;
        _plain = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_plain, nullptr);
    }
    void TearDown() override
    {
        for (const auto& id : EmulatorManager::GetInstance()->GetEmulatorIds())
            EmulatorManager::GetInstance()->RemoveEmulator(id);
    }

    std::shared_ptr<Emulator> _master;
    Emulator* _plain = nullptr;
};

TEST_F(SnapshotZXPoly_Test, EverySingleMachineSnapshotIsRefusedWithTheReason)
{
    for (const char* file : {"loaders/sna/action.sna", "loaders/z80/dizzyx.z80", "loaders/szx/libspectrum/synth-128.szx",
                             "machines/tsconf/spg/empty.spg"})
    {
        EXPECT_FALSE(_master->LoadSnapshot(TestPathHelper::GetTestDataPath(file))) << file;
        const snapshot::Report& report = _master->LastSnapshotReport();
        EXPECT_TRUE(report.refused) << file;
        EXPECT_EQ(report.needs, "format:zxp") << file;
        EXPECT_NE(report.reason.find("ZX-Poly"), std::string::npos) << report.reason;
        EXPECT_NE(report.reason.find(".zxp"), std::string::npos) << "it says what is taken";
    }
    // the same file loads on a single machine
    EXPECT_TRUE(_plain->LoadSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna")));

    // inspect says so before a load is tried
    StateNode inspected;
    std::string error;
    ASSERT_TRUE(_master->InspectSnapshot(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"), {}, inspected, error)) << error;
    EXPECT_FALSE(inspected.find("would_load")->b);
}

TEST_F(SnapshotZXPoly_Test, TheLauncherNeverReplacesAModule)
{
    SnapshotLoadRequest request;
    request.emulatorId = _master->GetId();
    request.path = TestPathHelper::GetTestDataPath("machines/tsconf/spg/empty.spg");   // would switch a single machine to TS-Conf
    request.switchModel = true;
    const SnapshotLoadResult result = SnapshotLauncher::Load(request);
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.modelSwitched);
    EXPECT_NE(result.message.find("ZX-Poly"), std::string::npos) << result.message;
    EXPECT_NE(EmulatorManager::GetInstance()->GetEmulator(_master->GetId()), nullptr) << "the module is still there";
}

TEST_F(SnapshotZXPoly_Test, NoSnapshotIsSavedFromAModule)
{
    const snapshot::SaveFormats formats = _master->SnapshotSaveFormats();
    EXPECT_FALSE(formats.viewAvailable);
    for (const snapshot::FormatStatus& status : formats.formats)
    {
        EXPECT_FALSE(status.available) << snapshot::ToText(status.format);
        EXPECT_EQ(status.needs, "zxpoly");
        EXPECT_NE(status.reason.find("ZX-Poly"), std::string::npos) << status.reason;
    }
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("zxpoly-save.sna");
    EXPECT_FALSE(_master->SaveSnapshot(path));
    EXPECT_FALSE(std::ifstream(path).good());
}
