#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/memory-region/memoryregionanalyzer.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/porttracker.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "pch.h"

/// @brief Integration tests for analysis pipeline with real emulator execution
class AnalysisIntegration_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        // Create emulator with debug features
        _emulator = EmulatorTestHelper::CreateDebugEmulator({"debugmode", "memorytracking"});
        ASSERT_NE(_emulator, nullptr);

        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
    }

    void TearDown() override
    {
        _context = nullptr;
        EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    void RunFrames(int count)
    {
        for (int i = 0; i < count; i++)
        {
            _emulator->RunFrame();
        }
    }
};

// =============================================================================
// PortTracker Integration
// =============================================================================

TEST_F(AnalysisIntegration_Test, PortTrackerCapturesRealPortAccess)
{
    ASSERT_NE(_context->pPortTracker, nullptr);
    ASSERT_NE(_context->pFeatureManager, nullptr);

    // Enable port tracking
    _context->pFeatureManager->setFeature("porttracking", true);
    _context->pPortTracker->StartSession();

    // Run enough frames for ROM to finish init and start keyboard scanning
    RunFrames(50);

    _context->pPortTracker->StopSession();

    // Port 0xFE is always accessed (keyboard scanning in ROM)
    uint32_t portFEReads = _context->pPortTracker->GetReadCount(0xFE);
    uint32_t portFEWrites = _context->pPortTracker->GetWriteCount(0xFE);

    // ROM scans keyboard every frame - should have many reads
    EXPECT_GT(portFEReads, 0) << "ROM should read port 0xFE for keyboard scanning";

    // ROM also writes to 0xFE for border color
    // Note: may be 0 if we're in early ROM before first border write

    // Get port summaries
    auto summaries = _context->pPortTracker->GetPortSummaries();
    EXPECT_GT(summaries.size(), 0) << "Should have at least one active port";

    // Verify callers are captured
    auto readCallers = _context->pPortTracker->GetReadCallers(0xFE);
    EXPECT_GT(readCallers.size(), 0) << "Should have captured caller PCs for port reads";

    // Callers should be in ROM range (0x0000-0x3FFF)
    for (const auto& [callerPC, count] : readCallers)
    {
        EXPECT_LT(callerPC, 0x4000) << "Keyboard read callers should be in ROM";
    }
}

TEST_F(AnalysisIntegration_Test, PortTrackerSessionStateTransitions)
{
    ASSERT_NE(_context->pPortTracker, nullptr);

    // Initially inactive
    EXPECT_FALSE(_context->pPortTracker->IsActive());

    // Start session
    _context->pPortTracker->StartSession();
    EXPECT_TRUE(_context->pPortTracker->IsActive());

    // Run frames and capture (need enough for ROM to start keyboard scanning)
    RunFrames(50);
    uint32_t afterRun = _context->pPortTracker->GetReadCount(0xFE);

    // Pause session
    _context->pPortTracker->PauseSession();
    EXPECT_FALSE(_context->pPortTracker->IsActive());

    // Run more frames - should NOT capture
    RunFrames(20);
    uint32_t afterPause = _context->pPortTracker->GetReadCount(0xFE);
    EXPECT_EQ(afterRun, afterPause) << "Paused session should not capture";

    // Resume session
    _context->pPortTracker->ResumeSession();
    EXPECT_TRUE(_context->pPortTracker->IsActive());

    // Run more frames - should capture
    RunFrames(20);
    uint32_t afterResume = _context->pPortTracker->GetReadCount(0xFE);
    EXPECT_GT(afterResume, afterPause) << "Resumed session should capture";

    // Stop session
    _context->pPortTracker->StopSession();
    EXPECT_FALSE(_context->pPortTracker->IsActive());
}

// =============================================================================
// MemoryRegionAnalyzer Integration
// =============================================================================

TEST_F(AnalysisIntegration_Test, MemoryRegionAnalyzerCapturesExecution)
{
    ASSERT_NE(_context->pDebugManager, nullptr);

    auto* mgr = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(mgr, nullptr);

    // Enable required features
    _context->pFeatureManager->setFeature("memorytracking", true);
    _context->pFeatureManager->setFeature("analysis", true);

    // Start memory tracking session
    _context->pMemory->GetAccessTracker().StartMemorySession();

    // Activate analyzer
    mgr->activate("memory-region");
    EXPECT_TRUE(mgr->isActive("memory-region"));

    auto* mra = dynamic_cast<MemoryRegionAnalyzer*>(mgr->getAnalyzer("memory-region"));
    ASSERT_NE(mra, nullptr);

    // Run several frames for data collection
    RunFrames(50);

    // Force refresh
    mra->refresh();

    // Check we have regions
    const auto& regions = mra->getRegions();
    EXPECT_GT(regions.size(), 0) << "Should have at least one region after execution";

    // Check stats
    auto stats = mra->getStats();

    // ROM is executed so we should have CODE bytes
    EXPECT_GT(stats.codeBytes, 0) << "ROM execution should create CODE regions";

    // VRAM is written (border color) so we should have DATA
    // Note: depends on how far ROM executed

    // Screen buffer should be tagged
    const RawRegion* screenRegion = mra->getRegionAt(0x4000);
    ASSERT_NE(screenRegion, nullptr);
    EXPECT_TRUE((screenRegion->tags & static_cast<uint32_t>(MemoryTag::ScreenBitmap)) != 0 ||
                (screenRegion->tags & static_cast<uint32_t>(MemoryTag::ScreenBuffer)) != 0)
        << "Address 0x4000 should be tagged as screen";

    mgr->deactivate("memory-region");
}

TEST_F(AnalysisIntegration_Test, MemoryRegionAnalyzerROMIsCode)
{
    ASSERT_NE(_context->pDebugManager, nullptr);
    auto* mgr = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(mgr, nullptr);

    _context->pFeatureManager->setFeature("memorytracking", true);
    _context->pMemory->GetAccessTracker().StartMemorySession();
    mgr->activate("memory-region");

    auto* mra = dynamic_cast<MemoryRegionAnalyzer*>(mgr->getAnalyzer("memory-region"));
    ASSERT_NE(mra, nullptr);

    // Run enough frames for ROM to execute
    RunFrames(50);
    mra->refresh();

    // ROM entry point (0x0000) should be CODE
    BlockType type0000 = mra->getTypeAt(0x0000);
    EXPECT_EQ(type0000, BlockType::CODE) << "ROM entry point should be CODE";

    // RST 38 (interrupt) at 0x0038 should be CODE after interrupts fire
    BlockType type0038 = mra->getTypeAt(0x0038);
    EXPECT_EQ(type0038, BlockType::CODE) << "RST 38 should be CODE (interrupt handler)";

    mgr->deactivate("memory-region");
}

TEST_F(AnalysisIntegration_Test, MemoryRegionAnalyzerSystemVariables)
{
    ASSERT_NE(_context->pDebugManager, nullptr);
    auto* mgr = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(mgr, nullptr);

    _context->pFeatureManager->setFeature("memorytracking", true);
    _context->pMemory->GetAccessTracker().StartMemorySession();
    mgr->activate("memory-region");

    auto* mra = dynamic_cast<MemoryRegionAnalyzer*>(mgr->getAnalyzer("memory-region"));
    ASSERT_NE(mra, nullptr);

    // Run frames - ROM will access system variables
    RunFrames(50);
    mra->refresh();

    // Check system variables region is tagged
    MemoryTags tags5C00 = mra->getTagsAt(0x5C00);
    EXPECT_TRUE((tags5C00 & static_cast<uint32_t>(MemoryTag::SystemVariables)) != 0)
        << "0x5C00 should be tagged as SystemVariables";

    mgr->deactivate("memory-region");
}

// =============================================================================
// Full Pipeline Integration
// =============================================================================

TEST_F(AnalysisIntegration_Test, FullAnalysisPipelineFlow)
{
    // This test validates the complete analysis flow:
    // 1. Enable features
    // 2. Start port tracking
    // 3. Activate analyzer
    // 4. Run emulation
    // 5. Verify data is captured correctly
    // 6. Stop everything

    ASSERT_NE(_context->pFeatureManager, nullptr);
    ASSERT_NE(_context->pPortTracker, nullptr);
    ASSERT_NE(_context->pDebugManager, nullptr);

    auto* mgr = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(mgr, nullptr);

    // Step 1: Enable features
    _context->pFeatureManager->setFeature("memorytracking", true);
    _context->pFeatureManager->setFeature("porttracking", true);
    _context->pFeatureManager->setFeature("analysis", true);

    // Step 2: Start tracking sessions
    _context->pMemory->GetAccessTracker().StartMemorySession();
    _context->pPortTracker->StartSession();
    EXPECT_TRUE(_context->pPortTracker->IsActive());

    // Step 3: Activate analyzer
    mgr->activate("memory-region");
    EXPECT_TRUE(mgr->isActive("memory-region"));

    auto* mra = dynamic_cast<MemoryRegionAnalyzer*>(mgr->getAnalyzer("memory-region"));
    ASSERT_NE(mra, nullptr);

    // Step 4: Run emulation
    RunFrames(50);  // Run 50 frames (~1 second of emulation)

    // Step 5: Verify data
    mra->refresh();

    // Port tracking data
    auto portSummaries = _context->pPortTracker->GetPortSummaries();
    EXPECT_GT(portSummaries.size(), 0) << "Should have port activity";

    uint32_t totalPortOps = 0;
    for (const auto& s : portSummaries)
    {
        totalPortOps += s.readCount + s.writeCount;
    }
    EXPECT_GT(totalPortOps, 10) << "50 frames should generate port operations";

    // Memory segmentation data
    auto stats = mra->getStats();
    EXPECT_GT(stats.codeBytes, 0) << "Should have CODE regions";
    EXPECT_GT(stats.totalRegions, 0) << "Should have regions";

    // Verify ROM execution
    EXPECT_EQ(mra->getTypeAt(0x0000), BlockType::CODE);

    // Verify VRAM tagging
    EXPECT_TRUE((mra->getTagsAt(0x4000) & static_cast<uint32_t>(MemoryTag::ScreenBitmap)) != 0);

    // Step 6: Stop everything
    mgr->deactivate("memory-region");
    _context->pPortTracker->StopSession();

    EXPECT_FALSE(mgr->isActive("memory-region"));
    EXPECT_FALSE(_context->pPortTracker->IsActive());
}
