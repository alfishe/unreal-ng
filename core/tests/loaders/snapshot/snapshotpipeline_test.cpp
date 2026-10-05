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
#include "emulator/emulator.h"
#include "loaders/snapshot/snapshotpipeline.h"

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
