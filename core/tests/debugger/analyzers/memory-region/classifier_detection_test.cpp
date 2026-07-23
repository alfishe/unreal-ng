#include <gtest/gtest.h>
#include <iostream>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/memory-region/memoryregionanalyzer.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/porttracker.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/memoryaccesstracker.h"
#include "pch.h"

class ClassifierDetection_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
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

TEST_F(ClassifierDetection_Test, RealROMClassification)
{
    auto* mgr = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(mgr, nullptr);

    // Enable features and start sessions
    _context->pFeatureManager->setFeature("memorytracking", true);
    _context->pFeatureManager->setFeature("porttracking", true);
    _context->pMemory->GetAccessTracker().StartMemorySession();
    _context->pPortTracker->StartSession();

    mgr->activate("memory-region");
    auto* mra = dynamic_cast<MemoryRegionAnalyzer*>(mgr->getAnalyzer("memory-region"));
    ASSERT_NE(mra, nullptr);

    // Run 100 frames - enough for ROM init, keyboard scanning, AY init
    RunFrames(100);
    mra->refresh();

    // Get stats
    auto stats = mra->getStats();
    std::cerr << "=== Classification Results After 100 Frames ===" << std::endl;
    std::cerr << "Total regions: " << stats.totalRegions << std::endl;
    std::cerr << "CODE bytes: " << stats.codeBytes << std::endl;
    std::cerr << "DATA bytes: " << stats.dataBytes << std::endl;
    std::cerr << "VARIABLE bytes: " << stats.variableBytes << std::endl;
    std::cerr << "SMC bytes: " << stats.smcBytes << std::endl;
    std::cerr << "UNKNOWN bytes: " << stats.unknownBytes << std::endl;

    // Get port activity
    auto portSummaries = _context->pPortTracker->GetPortSummaries();
    std::cerr << "\n=== Port Activity ===" << std::endl;
    for (const auto& s : portSummaries)
    {
        std::cerr << "Port 0x" << std::hex << s.port << std::dec
                  << ": reads=" << s.readCount << " writes=" << s.writeCount << std::endl;
    }

    // Check specific classifier results
    std::cerr << "\n=== Tag Verification ===" << std::endl;

    // ROM entry should be CODE
    BlockType type0000 = mra->getTypeAt(0x0000);
    std::cerr << "0x0000 type: " << static_cast<int>(type0000) << " (expected CODE=1)" << std::endl;
    EXPECT_EQ(type0000, BlockType::CODE) << "ROM entry should be CODE";

    // Screen bitmap should be tagged
    MemoryTags tags4000 = mra->getTagsAt(0x4000);
    std::cerr << "0x4000 tags: 0x" << std::hex << tags4000 << std::dec << std::endl;
    bool hasScreenTag = (tags4000 & static_cast<uint32_t>(MemoryTag::ScreenBitmap)) != 0 ||
                        (tags4000 & static_cast<uint32_t>(MemoryTag::ScreenBuffer)) != 0;
    EXPECT_TRUE(hasScreenTag) << "Screen memory should be tagged";

    // System variables should be tagged
    MemoryTags tags5C00 = mra->getTagsAt(0x5C00);
    std::cerr << "0x5C00 tags: 0x" << std::hex << tags5C00 << std::dec << std::endl;
    EXPECT_TRUE((tags5C00 & static_cast<uint32_t>(MemoryTag::SystemVariables)) != 0)
        << "System variables should be tagged";

    // Check if MusicClassifier detected AY activity
    // ROM plays startup beep on AY, so if we ran enough frames...
    auto ayReadCallers = _context->pPortTracker->GetReadCallers(0xFFFD);
    auto ayWriteCallers = _context->pPortTracker->GetWriteCallers(0xBFFD);
    std::cerr << "\nAY port 0xFFFD read callers: " << ayReadCallers.size() << std::endl;
    std::cerr << "AY port 0xBFFD write callers: " << ayWriteCallers.size() << std::endl;

    // If there are AY callers, check if MusicClassifier tagged them
    bool foundMusicCode = false;
    for (const auto& [callerPC, count] : ayWriteCallers)
    {
        MemoryTags callerTags = mra->getTagsAt(callerPC);
        if (callerTags & static_cast<uint32_t>(MemoryTag::MusicPlayerCode))
        {
            foundMusicCode = true;
            std::cerr << "Found MusicPlayerCode at 0x" << std::hex << callerPC << std::dec << std::endl;
        }
    }

    // Verify basic sanity
    EXPECT_GT(stats.codeBytes, 0) << "Should have CODE regions from ROM execution";
    EXPECT_GT(stats.totalRegions, 1) << "Should have multiple regions";

    // Port tracking should have captured keyboard and paging
    EXPECT_GT(_context->pPortTracker->GetReadCount(0x00FE), 0) << "Should have keyboard reads";
    EXPECT_GT(_context->pPortTracker->GetWriteCount(0x7FFD), 0) << "Should have memory paging writes";

    _context->pPortTracker->StopSession();
    mgr->deactivate("memory-region");
}

TEST_F(ClassifierDetection_Test, VerifyAllRegions)
{
    auto* mgr = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(mgr, nullptr);

    _context->pFeatureManager->setFeature("memorytracking", true);
    _context->pMemory->GetAccessTracker().StartMemorySession();

    mgr->activate("memory-region");
    auto* mra = dynamic_cast<MemoryRegionAnalyzer*>(mgr->getAnalyzer("memory-region"));
    ASSERT_NE(mra, nullptr);

    RunFrames(100);
    mra->refresh();

    // === Code Flow Analysis Results ===
    const auto& codeFlowResult = mra->getCodeFlowResult();
    std::cerr << "\n=== Code Flow Analysis ===" << std::endl;
    std::cerr << "  Branch targets found: " << codeFlowResult.branchTargetsFound << std::endl;
    std::cerr << "  Unreached CODE bytes identified: " << codeFlowResult.unreachedCodeBytes << std::endl;

    // === Raw Regions (exact boundaries) ===
    const auto& rawRegions = mra->getRegions();
    std::cerr << "\n=== Raw Regions (" << rawRegions.size() << ") ===" << std::endl;

    int codeRegions = 0, dataRegions = 0, varRegions = 0, romInRamRegions = 0, unknownRegions = 0;
    for (const auto& region : rawRegions)
    {
        switch (region.type)
        {
            case BlockType::CODE: codeRegions++; break;
            case BlockType::DATA: dataRegions++; break;
            case BlockType::VARIABLE: varRegions++; break;
            case BlockType::ROM_CODE_IN_RAM: romInRamRegions++; break;
            case BlockType::UNKNOWN: unknownRegions++; break;
            default: break;
        }
    }

    std::cerr << "  CODE: " << codeRegions << std::endl;
    std::cerr << "  DATA: " << dataRegions << std::endl;
    std::cerr << "  VARIABLE: " << varRegions << std::endl;
    std::cerr << "  ROM_CODE_IN_RAM: " << romInRamRegions << std::endl;
    std::cerr << "  UNKNOWN: " << unknownRegions << std::endl;

    // === Aggregated Regions (merged same-type) ===
    const auto& aggRegions = mra->getAggregatedRegions();
    std::cerr << "\n=== Aggregated Regions (" << aggRegions.size() << ") ===" << std::endl;

    int aggCode = 0, aggData = 0, aggVar = 0, aggRomInRam = 0, aggUnknown = 0;
    for (const auto& region : aggRegions)
    {
        switch (region.type)
        {
            case BlockType::CODE: aggCode++; break;
            case BlockType::DATA: aggData++; break;
            case BlockType::VARIABLE: aggVar++; break;
            case BlockType::ROM_CODE_IN_RAM: aggRomInRam++; break;
            case BlockType::UNKNOWN: aggUnknown++; break;
            default: break;
        }
    }

    std::cerr << "  CODE: " << aggCode << std::endl;
    std::cerr << "  DATA: " << aggData << std::endl;
    std::cerr << "  VARIABLE: " << aggVar << std::endl;
    std::cerr << "  ROM_CODE_IN_RAM: " << aggRomInRam << std::endl;
    std::cerr << "  UNKNOWN: " << aggUnknown << std::endl;

    // Show first 10 aggregated regions with their raw region counts
    std::cerr << "\nFirst 10 aggregated regions:" << std::endl;
    int shown = 0;
    for (const auto& region : aggRegions)
    {
        if (shown >= 10) break;
        std::cerr << "  0x" << std::hex << region.startAddress << "-0x" << region.endAddress
                  << " type=" << static_cast<int>(region.type)
                  << " tags=0x" << region.unionTags
                  << " (" << std::dec << region.rawRegionCount() << " raw)" << std::endl;
        shown++;
    }

    EXPECT_GT(codeRegions, 0) << "Should have CODE regions";
    EXPECT_GT(rawRegions.size(), 5) << "Should have multiple distinct raw regions";
    EXPECT_LT(aggRegions.size(), rawRegions.size()) << "Aggregated should be fewer than raw";

    // Verify UNKNOWN = never accessed
    auto& tracker = _context->pMemory->GetAccessTracker();
    const uint32_t* reads = tracker.GetZ80ReadCountersPtr();
    const uint32_t* writes = tracker.GetZ80WriteCountersPtr();
    const uint32_t* execs = tracker.GetZ80ExecuteCountersPtr();

    int unknownWithAccess = 0;
    for (const auto& region : rawRegions)
    {
        if (region.type == BlockType::UNKNOWN)
        {
            for (uint32_t addr = region.startAddress; addr <= region.endAddress; addr++)
            {
                if (reads[addr] > 0 || writes[addr] > 0 || execs[addr] > 0)
                {
                    unknownWithAccess++;
                    if (unknownWithAccess <= 3)
                    {
                        std::cerr << "WARNING: UNKNOWN region has access at 0x"
                                  << std::hex << addr << std::dec
                                  << " r=" << reads[addr] << " w=" << writes[addr]
                                  << " x=" << execs[addr] << std::endl;
                    }
                }
            }
        }
    }
    EXPECT_EQ(unknownWithAccess, 0) << "UNKNOWN regions should have no memory access";

    mgr->deactivate("memory-region");
}
