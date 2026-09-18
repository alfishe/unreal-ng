/// @file coverageanalyzer_test.cpp
/// @brief Unit tests for the CoverageAnalyzer executed-address map (M7d / M9).
///
/// Drives the analyzer through a standalone AnalyzerManager: activation
/// subscribes the CPU-step hot path (which also flips the manager's master
/// dispatch toggle on), dispatchCPUStep is invoked with a null CPU (the
/// analyzer only consumes pc), and the query API is verified: bit-map
/// recording, instruction counting, consecutive-range merging, gap inversion,
/// truncation limits, and clear() semantics.

#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/coverage/coverageanalyzer.h"
#include "emulator/emulatorcontext.h"

class CoverageAnalyzer_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _manager = new AnalyzerManager(_context);

        auto analyzer = std::make_unique<CoverageAnalyzer>();
        _coverage = analyzer.get();
        _manager->registerAnalyzer("coverage", std::move(analyzer));

        // First activation enables dispatching manager-wide
        ASSERT_TRUE(_manager->activate("coverage"));
        ASSERT_TRUE(_coverage->isRecording());
    }

    void TearDown() override
    {
        delete _manager; // destructor deactivates all analyzers and frees them
        delete _context;
    }

    void Step(uint16_t pc)
    {
        // onCPUStep ignores the cpu argument — a null CPU is valid here
        _manager->dispatchCPUStep(nullptr, pc);
    }

    EmulatorContext* _context = nullptr;
    AnalyzerManager* _manager = nullptr;
    CoverageAnalyzer* _coverage = nullptr;
};

TEST_F(CoverageAnalyzer_Test, RecordsExecutedAddresses)
{
    Step(0x8000);
    Step(0x8001);
    Step(0x8003);

    EXPECT_EQ(_coverage->getInstructionCount(), 3u);
    EXPECT_TRUE(_coverage->isExecuted(0x8000));
    EXPECT_TRUE(_coverage->isExecuted(0x8001));
    EXPECT_TRUE(_coverage->isExecuted(0x8003));
    EXPECT_FALSE(_coverage->isExecuted(0x8002)); // address was skipped
    EXPECT_EQ(_coverage->getExecutedCount(), 3u);
    EXPECT_EQ(_coverage->getExecutedCountInRange(0x8001, 0x8005), 2u); // 0x8001, 0x8003
}

TEST_F(CoverageAnalyzer_Test, RepeatedExecution_CountsInstructionsButNotAddresses)
{
    for (int i = 0; i < 5; i++)
    {
        Step(0x8000);
    }

    EXPECT_EQ(_coverage->getInstructionCount(), 5u);
    EXPECT_EQ(_coverage->getExecutedCount(), 1u); // the map is monotonic bits, not counters
}

TEST_F(CoverageAnalyzer_Test, DeactivationStopsRecordingAndDispatch)
{
    Step(0x8000);
    ASSERT_TRUE(_manager->deactivate("coverage"));

    EXPECT_FALSE(_coverage->isRecording());
    Step(0x9000); // last analyzer gone → dispatch disabled
    EXPECT_EQ(_coverage->getInstructionCount(), 1u);
    EXPECT_FALSE(_coverage->isExecuted(0x9000));
}

TEST_F(CoverageAnalyzer_Test, ExecutedRangesMergeConsecutiveAddresses)
{
    for (uint16_t pc = 0x8000; pc <= 0x8002; pc++)
    {
        Step(pc);
    }
    Step(0x8005);
    Step(0x8006);

    std::vector<std::pair<uint16_t, uint16_t>> ranges = _coverage->getExecutedRanges();
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].first, 0x8000);
    EXPECT_EQ(ranges[0].second, 0x8002);
    EXPECT_EQ(ranges[1].first, 0x8005);
    EXPECT_EQ(ranges[1].second, 0x8006);
}

TEST_F(CoverageAnalyzer_Test, ExecutedRanges_TruncatesToMaxRanges)
{
    Step(0x8000);
    Step(0x8005);

    std::vector<std::pair<uint16_t, uint16_t>> ranges = _coverage->getExecutedRanges(1);
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].first, 0x8000);
}

TEST_F(CoverageAnalyzer_Test, GapsInvertExecutedWithinWindow)
{
    for (uint16_t pc = 0x8000; pc <= 0x8002; pc++)
    {
        Step(pc);
    }
    Step(0x8005);
    Step(0x8006);

    // Executed 0x8000-0x8002 and 0x8005-0x8006 → single hole at 0x8003-0x8004
    std::vector<std::pair<uint16_t, uint16_t>> gaps = _coverage->getGaps(0x8000, 0x8006);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].first, 0x8003);
    EXPECT_EQ(gaps[0].second, 0x8004);
}

TEST_F(CoverageAnalyzer_Test, Gaps_TruncatesToMaxGaps)
{
    Step(0x8002);
    Step(0x8005);

    // Holes before 0x8002 and between 0x8002 and 0x8005 → only the first is kept
    std::vector<std::pair<uint16_t, uint16_t>> gaps = _coverage->getGaps(0x8000, 0x8006, 1);
    ASSERT_EQ(gaps.size(), 1u);
    EXPECT_EQ(gaps[0].first, 0x8000);
    EXPECT_EQ(gaps[0].second, 0x8001);
}

TEST_F(CoverageAnalyzer_Test, ClearResetsMapButKeepsRecording)
{
    Step(0x8000);
    Step(0x8001);
    ASSERT_EQ(_coverage->getInstructionCount(), 2u);

    _coverage->clear();

    EXPECT_EQ(_coverage->getInstructionCount(), 0u);
    EXPECT_FALSE(_coverage->isExecuted(0x8000));
    EXPECT_TRUE(_coverage->isRecording()); // clear is not stop
    Step(0x8000);
    EXPECT_EQ(_coverage->getInstructionCount(), 1u);
}
