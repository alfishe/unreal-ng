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
