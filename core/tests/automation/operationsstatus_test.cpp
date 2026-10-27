/// @file operationsstatus_test.cpp
/// @brief The operation log report every automation surface returns (OperationsStatus: WebAPI /operations, CLI
/// `ops`, Lua / Python operations(), MCP emulator_manage operations): its fields, since / limit, the text form.
///
/// Boots a machine: slower than the 50 ms guideline, one acceptance check of the shared report.

#include <gtest/gtest.h>

#include <string>

#include "../../automation/operationsstatus.h"
#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"

class OperationsStatus_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    static const StateNode* Member(const StateNode& node, const std::string& key)
    {
        for (const auto& [name, value] : node.members)
            if (name == key)
                return &value;
        return nullptr;
    }
};

TEST_F(OperationsStatus_Test, TheReportCarriesEveryEntryFieldOldestFirst)
{
    const uint64_t before = _emulator->GetOperationLog().LastSeq();
    _emulator->Reset();
    _emulator->Reset(/*hardReset=*/true);

    const StateNode report = OperationsStatus::Report(_emulator, before);
    ASSERT_NE(Member(report, "mode"), nullptr);
    EXPECT_EQ(Member(report, "mode")->s, "live");
    EXPECT_EQ(Member(report, "last_seq")->i, static_cast<int64_t>(before + 2));
    EXPECT_EQ(Member(report, "capacity")->i, 1024);
    const StateNode* entries = Member(report, "entries");
    ASSERT_NE(entries, nullptr);
    ASSERT_EQ(entries->items.size(), 2u);
    const StateNode& soft = entries->items[0];
    for (const char* field : {"seq", "frame", "t_in_frame", "wall_ms", "kind", "source", "mode", "status", "effect",
                              "summary", "detail"})
        EXPECT_NE(Member(soft, field), nullptr) << field;
    EXPECT_EQ(Member(soft, "kind")->s, "reset");
    EXPECT_EQ(Member(soft, "summary")->s, "soft");
    EXPECT_EQ(Member(soft, "status")->s, "done");
    EXPECT_EQ(Member(entries->items[1], "summary")->s, "hard");
    EXPECT_GT(Member(entries->items[1], "seq")->i, Member(soft, "seq")->i);
}

TEST_F(OperationsStatus_Test, LimitKeepsTheNewestAndSinceTheNew)
{
    const uint64_t before = _emulator->GetOperationLog().LastSeq();
    for (int i = 0; i < 3; ++i)
        _emulator->Reset();

    const StateNode newest = OperationsStatus::Report(_emulator, before, 1);
    ASSERT_EQ(Member(newest, "entries")->items.size(), 1u);
    EXPECT_EQ(Member(Member(newest, "entries")->items[0], "seq")->i, static_cast<int64_t>(before + 3));

    const StateNode none = OperationsStatus::Report(_emulator, before + 3);
    EXPECT_TRUE(Member(none, "entries")->items.empty());
    EXPECT_TRUE(Member(none, "entries")->isArray()) << "an empty list, not a missing one";
}

TEST_F(OperationsStatus_Test, TheTextHasOneLinePerOperation)
{
    const uint64_t before = _emulator->GetOperationLog().LastSeq();
    _emulator->Reset(/*hardReset=*/true);
    const std::string text = OperationsStatus::Text(_emulator, before);
    EXPECT_NE(text.find("Operations (mode live, last #" + std::to_string(before + 1) + ", 1 shown"), std::string::npos)
        << text;
    EXPECT_NE(text.find("reset hard  done  [live, unknown]  ttd: end-keep-history"), std::string::npos) << text;
    EXPECT_NE(OperationsStatus::Text(_emulator, before + 1).find("(none)"), std::string::npos);
}
