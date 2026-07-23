#include "memoryregionanalyzer_test.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/ianalyzer.h"
#include "debugger/analyzers/memory-region/memoryregionanalyzer.h"
#include "debugger/analyzers/memory-region/types.h"
#include "debugger/analyzers/memory-region/blocksegmenter.h"
#include "debugger/analyzers/memory-region/regionmap.h"
#include "debugger/analyzers/memory-region/tagpipeline.h"
#include "debugger/analyzers/memory-region/itagclassifier.h"
#include "debugger/analyzers/memory-region/analysiscontext.h"
#include "debugger/analyzers/memory-region/blocktagmap.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/porttracker.h"
#include "emulator/monitoring/manifest.h"
#include "pch.h"

/// region <SetUp / TearDown>

void MemoryRegionAnalyzer_Test::SetUp()
{
    _emulator = EmulatorTestHelper::CreateDebugEmulator({"debugmode", "breakpoints"});
    ASSERT_NE(_emulator, nullptr) << "Failed to create debug emulator";

    _context = _emulator->GetContext();
    ASSERT_NE(_context, nullptr);
    ASSERT_NE(_context->pDebugManager, nullptr) << "DebugManager not initialized";

    _manager = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(_manager, nullptr) << "AnalyzerManager not initialized";
}

void MemoryRegionAnalyzer_Test::TearDown()
{
    _manager = nullptr;
    _context = nullptr;

    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
}

/// endregion </SetUp / TearDown>

// =============================================================================
// Registration Tests
// =============================================================================

TEST_F(MemoryRegionAnalyzer_Test, AnalyzerIsRegistered)
{
    // MemoryRegionAnalyzer should be auto-registered by DebugManager
    IAnalyzer* analyzer = _manager->getAnalyzer("memory-region");
    ASSERT_NE(analyzer, nullptr) << "memory-region analyzer not registered";

    EXPECT_EQ(analyzer->getName(), "MemoryRegionAnalyzer");
    EXPECT_FALSE(analyzer->getUUID().empty());
}

TEST_F(MemoryRegionAnalyzer_Test, AnalyzerActivation)
{
    _manager->activate("memory-region");
    EXPECT_TRUE(_manager->isActive("memory-region"));
}

TEST_F(MemoryRegionAnalyzer_Test, AnalyzerDeactivation)
{
    _manager->activate("memory-region");
    _manager->deactivate("memory-region");
    EXPECT_FALSE(_manager->isActive("memory-region"));
}

// =============================================================================
// BlockSegmenter Unit Tests (using static ClassifyAddress)
// =============================================================================

TEST_F(MemoryRegionAnalyzer_Test, BlockSegmenterClassifiesUnknown)
{
    // No access → UNKNOWN (address doesn't matter for this case)
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x0000, 0, 0, 0), BlockType::UNKNOWN);
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x8000, 0, 0, 0), BlockType::UNKNOWN);
}

TEST_F(MemoryRegionAnalyzer_Test, BlockSegmenterClassifiesCode)
{
    // Execute-only → CODE (ROM address)
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x0000, 0, 0, 10), BlockType::CODE);
    // Execute + read → CODE (RAM address, no write)
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x8000, 5, 0, 10), BlockType::CODE);
}

TEST_F(MemoryRegionAnalyzer_Test, BlockSegmenterClassifiesData)
{
    // Read-only → DATA
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x8000, 10, 0, 0), BlockType::DATA);
    // Write-only → DATA (decompressor output)
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x8000, 0, 5, 0), BlockType::DATA);
}

TEST_F(MemoryRegionAnalyzer_Test, BlockSegmenterClassifiesVariable)
{
    // Read + Write (no execute) → VARIABLE
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x8000, 5, 3, 0), BlockType::VARIABLE);
}

TEST_F(MemoryRegionAnalyzer_Test, BlockSegmenterClassifiesROMCodeInRAM)
{
    // Execute + Write in RAM (>= 0x4000) → ROM_CODE_IN_RAM (bridge routines)
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x8000, 0, 5, 10), BlockType::ROM_CODE_IN_RAM);
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x5B00, 3, 5, 10), BlockType::ROM_CODE_IN_RAM);

    // In ROM area (< 0x4000) with write+exec - treated as CODE (shouldn't happen normally)
    EXPECT_EQ(BlockSegmenter::ClassifyAddress(0x1000, 0, 5, 10), BlockType::CODE);
}

// =============================================================================
// BlockTagMap Unit Tests
// =============================================================================

TEST_F(MemoryRegionAnalyzer_Test, BlockTagMapDefaultsToUnknown)
{
    BlockTagMap tagMap;
    tagMap.clear();

    EXPECT_EQ(tagMap.getType(0x0000), BlockType::UNKNOWN);
    EXPECT_EQ(tagMap.getType(0xFFFF), BlockType::UNKNOWN);
    EXPECT_EQ(tagMap.getTags(0x4000), static_cast<MemoryTags>(MemoryTag::None));
}

TEST_F(MemoryRegionAnalyzer_Test, BlockTagMapTypeGetSet)
{
    BlockTagMap tagMap;
    tagMap.clear();

    tagMap.setType(0x8000, BlockType::CODE);
    tagMap.setType(0x4000, BlockType::DATA);

    EXPECT_EQ(tagMap.getType(0x8000), BlockType::CODE);
    EXPECT_EQ(tagMap.getType(0x4000), BlockType::DATA);
    EXPECT_EQ(tagMap.getType(0xC000), BlockType::UNKNOWN);  // Not set
}

TEST_F(MemoryRegionAnalyzer_Test, BlockTagMapTagCombination)
{
    BlockTagMap tagMap;
    tagMap.clear();

    tagMap.addTag(0x8000, MemoryTag::GenericCode);
    tagMap.addTag(0x8000, MemoryTag::GraphicsCode);

    EXPECT_TRUE(tagMap.hasTag(0x8000, MemoryTag::GenericCode));
    EXPECT_TRUE(tagMap.hasTag(0x8000, MemoryTag::GraphicsCode));
    EXPECT_FALSE(tagMap.hasTag(0x8000, MemoryTag::MusicPlayerCode));
}

// =============================================================================
// RegionMap Unit Tests
// =============================================================================

TEST_F(MemoryRegionAnalyzer_Test, RegionMapMergesAdjacentBlocks)
{
    BlockTagMap tagMap;
    tagMap.clear();

    // Set a contiguous region of CODE blocks
    for (uint16_t addr = 0x8000; addr < 0x8010; addr++)
    {
        tagMap.setType(addr, BlockType::CODE);
    }

    RegionMap regionMap;
    regionMap.Build(tagMap);

    auto codeRegions = regionMap.GetRegionsByType(BlockType::CODE);
    EXPECT_GE(codeRegions.size(), 1u);

    // Find the region containing 0x8000
    const RawRegion* region = regionMap.FindRegion(0x8000);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region->type, BlockType::CODE);
    EXPECT_LE(region->startAddress, 0x8000);
    EXPECT_GE(region->endAddress, 0x800F);
}

TEST_F(MemoryRegionAnalyzer_Test, RegionMapSplitsDifferentTypes)
{
    BlockTagMap tagMap;
    tagMap.clear();

    // Two adjacent but different-type regions
    for (uint16_t addr = 0x8000; addr < 0x8008; addr++)
    {
        tagMap.setType(addr, BlockType::CODE);
    }
    for (uint16_t addr = 0x8008; addr < 0x8010; addr++)
    {
        tagMap.setType(addr, BlockType::DATA);
    }

    RegionMap regionMap;
    regionMap.Build(tagMap);

    const RawRegion* codeRegion = regionMap.FindRegion(0x8000);
    const RawRegion* dataRegion = regionMap.FindRegion(0x8008);

    ASSERT_NE(codeRegion, nullptr);
    ASSERT_NE(dataRegion, nullptr);
    EXPECT_EQ(codeRegion->type, BlockType::CODE);
    EXPECT_EQ(dataRegion->type, BlockType::DATA);
    EXPECT_NE(codeRegion, dataRegion);
}

TEST_F(MemoryRegionAnalyzer_Test, RegionMapCombinesTags)
{
    BlockTagMap tagMap;
    tagMap.clear();

    // Set 2 addresses to CODE with same tags - they should merge
    tagMap.setType(0x8000, BlockType::CODE);
    tagMap.setType(0x8001, BlockType::CODE);
    tagMap.addTag(0x8000, MemoryTag::GenericCode);
    tagMap.addTag(0x8000, MemoryTag::GraphicsCode);
    tagMap.addTag(0x8001, MemoryTag::GenericCode);
    tagMap.addTag(0x8001, MemoryTag::GraphicsCode);

    RegionMap regionMap;
    regionMap.Build(tagMap);

    const RawRegion* region = regionMap.FindRegion(0x8000);
    ASSERT_NE(region, nullptr);

    // Both addresses have same tags, so they merge into one region
    uint32_t expectedTags = static_cast<uint32_t>(MemoryTag::GenericCode) |
                            static_cast<uint32_t>(MemoryTag::GraphicsCode);
    EXPECT_EQ(static_cast<uint32_t>(region->tags), expectedTags);

    // Verify they're in the same region
    const RawRegion* region2 = regionMap.FindRegion(0x8001);
    EXPECT_EQ(region, region2);
}

TEST_F(MemoryRegionAnalyzer_Test, RegionMapStatistics)
{
    BlockTagMap tagMap;
    tagMap.clear();

    // Fill with CODE from 0x8000..0x800F and DATA from 0x4000..0x400F
    for (uint16_t i = 0x8000; i < 0x8010; i++)
        tagMap.setType(i, BlockType::CODE);
    for (uint16_t i = 0x4000; i < 0x4010; i++)
        tagMap.setType(i, BlockType::DATA);

    RegionMap regionMap;
    regionMap.Build(tagMap);

    SegmentationStats stats = regionMap.GetStats();
    EXPECT_GE(stats.codeBytes, 16u);
    EXPECT_GE(stats.dataBytes, 16u);
}

// =============================================================================
// TagPipeline Unit Tests
// =============================================================================

// Mock classifier for pipeline tests
class MockTagClassifier : public ITagClassifier
{
public:
    MockTagClassifier(const std::string& name, std::vector<std::string> deps = {})
        : _name(name), _deps(std::move(deps)) {}

    std::string name() const override { return _name; }
    std::vector<std::string> dependencies() const override { return _deps; }

    void classify(const AnalysisContext& ctx, BlockTagMap& tagMap) override
    {
        (void)ctx;
        (void)tagMap;
        classifyCalled = true;
        executionOrder = _globalOrder++;
    }

    bool classifyCalled = false;
    int executionOrder = -1;

    static int _globalOrder;

private:
    std::string _name;
    std::vector<std::string> _deps;
};

int MockTagClassifier::_globalOrder = 0;

TEST_F(MemoryRegionAnalyzer_Test, TagPipelineRegistration)
{
    TagPipeline pipeline;
    pipeline.addClassifier(std::make_unique<MockTagClassifier>("test1"));
    pipeline.addClassifier(std::make_unique<MockTagClassifier>("test2"));

    // No crash, classifiers registered
    SUCCEED();
}

TEST_F(MemoryRegionAnalyzer_Test, TagPipelineDependencyOrder)
{
    MockTagClassifier::_globalOrder = 0;

    TagPipeline pipeline;

    // "derived" depends on "base"
    auto base = std::make_unique<MockTagClassifier>("base");
    auto derived = std::make_unique<MockTagClassifier>("derived", std::vector<std::string>{"base"});

    MockTagClassifier* basePtr = base.get();
    MockTagClassifier* derivedPtr = derived.get();

    // Register in reverse order to test dependency resolution
    pipeline.addClassifier(std::move(derived));
    pipeline.addClassifier(std::move(base));

    // Build empty context for test
    AnalysisContext ctx{};
    BlockTagMap tagMap;
    tagMap.clear();

    pipeline.runAll(ctx, tagMap);

    EXPECT_TRUE(basePtr->classifyCalled);
    EXPECT_TRUE(derivedPtr->classifyCalled);

    // Base should execute before derived
    EXPECT_LT(basePtr->executionOrder, derivedPtr->executionOrder);
}

// =============================================================================
// Feature Toggle Integration Tests
// =============================================================================

TEST_F(MemoryRegionAnalyzer_Test, FeatureTogglesExist)
{
    ASSERT_NE(_context->pFeatureManager, nullptr);

    // All analysis features should be registered and default to OFF
    EXPECT_FALSE(_context->pFeatureManager->isEnabled(Features::kAnalysis));
    EXPECT_FALSE(_context->pFeatureManager->isEnabled(Features::kAnalysisSegmentation));
    EXPECT_FALSE(_context->pFeatureManager->isEnabled(Features::kAnalysisClassifiers));
    EXPECT_FALSE(_context->pFeatureManager->isEnabled(Features::kAnalysisBehaviorDetection));
    EXPECT_FALSE(_context->pFeatureManager->isEnabled(Features::kPortTracking));
}

TEST_F(MemoryRegionAnalyzer_Test, FeatureTogglesCanBeEnabled)
{
    ASSERT_NE(_context->pFeatureManager, nullptr);

    EXPECT_TRUE(_context->pFeatureManager->setFeature(Features::kAnalysis, true));
    EXPECT_TRUE(_context->pFeatureManager->isEnabled(Features::kAnalysis));

    EXPECT_TRUE(_context->pFeatureManager->setFeature(Features::kPortTracking, true));
    EXPECT_TRUE(_context->pFeatureManager->isEnabled(Features::kPortTracking));
}

TEST_F(MemoryRegionAnalyzer_Test, FeatureTogglesResolveByAlias)
{
    ASSERT_NE(_context->pFeatureManager, nullptr);

    // Aliases are short forms: "ana" for analysis, "port" for porttracking
    EXPECT_TRUE(_context->pFeatureManager->setFeature("ana", true));
    EXPECT_TRUE(_context->pFeatureManager->isEnabled(Features::kAnalysis));

    EXPECT_TRUE(_context->pFeatureManager->setFeature("port", true));
    EXPECT_TRUE(_context->pFeatureManager->isEnabled(Features::kPortTracking));
}

// =============================================================================
// PortTracker Integration Tests (lifecycle via Emulator)
// =============================================================================

TEST_F(MemoryRegionAnalyzer_Test, PortTrackerCreatedByEmulator)
{
    // PortTracker should be created during Init()
    EXPECT_NE(_context->pPortTracker, nullptr) << "PortTracker not created by Emulator";
}

TEST_F(MemoryRegionAnalyzer_Test, PortTrackerInitiallyInactive)
{
    ASSERT_NE(_context->pPortTracker, nullptr);
    EXPECT_FALSE(_context->pPortTracker->IsActive());
}

TEST_F(MemoryRegionAnalyzer_Test, PortTrackerSessionLifecycle)
{
    auto* pt = _context->pPortTracker;
    ASSERT_NE(pt, nullptr);

    pt->StartSession();
    EXPECT_TRUE(pt->IsActive());

    pt->TrackWrite(0xFE, 0x07, 0x1234);
    EXPECT_EQ(pt->GetWriteCount(0xFE), 1u);

    pt->StopSession();
    EXPECT_FALSE(pt->IsActive());
}

// =============================================================================
// SHM Manifest Integration Tests
// =============================================================================

TEST_F(MemoryRegionAnalyzer_Test, ManifestSectionTypesExist)
{
    // Verify new section types are defined
    EXPECT_EQ(static_cast<uint16_t>(monitoring::SectionType::SEGMENTATION_MAP), 0x0120);
    EXPECT_EQ(static_cast<uint16_t>(monitoring::SectionType::PORT_ACTIVITY), 0x0121);
}
