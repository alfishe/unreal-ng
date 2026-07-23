#include "classifier_test.h"

#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "debugger/debugmanager.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/memory-region/memoryregionanalyzer.h"
#include "debugger/analyzers/memory-region/classifiers/generictagclassifier.h"
#include "debugger/analyzers/memory-region/classifiers/screenclassifier.h"
#include "debugger/analyzers/memory-region/classifiers/musicclassifier.h"
#include "debugger/analyzers/memory-region/classifiers/loaderclassifier.h"
#include "debugger/analyzers/memory-region/analysiscontext.h"
#include "debugger/analyzers/memory-region/blocktagmap.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/porttracker.h"
#include "pch.h"

/// region <SetUp / TearDown>

void Classifier_Test::SetUp()
{
    _emulator = EmulatorTestHelper::CreateDebugEmulator({"debugmode", "breakpoints"});
    ASSERT_NE(_emulator, nullptr) << "Failed to create debug emulator";

    _context = _emulator->GetContext();
    ASSERT_NE(_context, nullptr);
    ASSERT_NE(_context->pDebugManager, nullptr) << "DebugManager not initialized";

    _manager = _context->pDebugManager->GetAnalyzerManager();
    ASSERT_NE(_manager, nullptr) << "AnalyzerManager not initialized";
}

void Classifier_Test::TearDown()
{
    _manager = nullptr;
    _context = nullptr;

    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
}

/// endregion </SetUp / TearDown>

// =============================================================================
// GenericTagClassifier Tests
// =============================================================================

TEST_F(Classifier_Test, GenericClassifierTagsScreenBitmap)
{
    BlockTagMap tagMap;
    tagMap.clear();

    GenericTagClassifier classifier;
    AnalysisContext ctx{};

    classifier.classify(ctx, tagMap);

    // Check screen bitmap region (0x4000-0x57FF)
    EXPECT_TRUE(tagMap.hasTag(0x4000, MemoryTag::ScreenBitmap));
    EXPECT_TRUE(tagMap.hasTag(0x57FF, MemoryTag::ScreenBitmap));
    EXPECT_FALSE(tagMap.hasTag(0x5800, MemoryTag::ScreenBitmap));
}

TEST_F(Classifier_Test, GenericClassifierTagsScreenAttributes)
{
    BlockTagMap tagMap;
    tagMap.clear();

    GenericTagClassifier classifier;
    AnalysisContext ctx{};

    classifier.classify(ctx, tagMap);

    // Check screen attributes region (0x5800-0x5AFF)
    EXPECT_TRUE(tagMap.hasTag(0x5800, MemoryTag::ScreenAttributes));
    EXPECT_TRUE(tagMap.hasTag(0x5AFF, MemoryTag::ScreenAttributes));
    EXPECT_FALSE(tagMap.hasTag(0x57FF, MemoryTag::ScreenAttributes));
}

TEST_F(Classifier_Test, GenericClassifierTagsSystemVariables)
{
    BlockTagMap tagMap;
    tagMap.clear();

    GenericTagClassifier classifier;
    AnalysisContext ctx{};

    classifier.classify(ctx, tagMap);

    // Check system variables region (0x5B00-0x5CBF)
    EXPECT_TRUE(tagMap.hasTag(0x5B00, MemoryTag::SystemVariables));
    EXPECT_TRUE(tagMap.hasTag(0x5CBF, MemoryTag::SystemVariables));
    EXPECT_FALSE(tagMap.hasTag(0x5CC0, MemoryTag::SystemVariables));
}

TEST_F(Classifier_Test, GenericClassifierAssignsFallbackTags)
{
    BlockTagMap tagMap;
    tagMap.clear();

    // Set some addresses as CODE without any tags
    tagMap.setType(0x8000, BlockType::CODE);
    tagMap.setType(0x8001, BlockType::DATA);
    tagMap.setType(0x8002, BlockType::VARIABLE);

    GenericTagClassifier classifier;
    AnalysisContext ctx{};

    classifier.classify(ctx, tagMap);

    // Should get fallback generic tags
    EXPECT_TRUE(tagMap.hasTag(0x8000, MemoryTag::GenericCode));
    EXPECT_TRUE(tagMap.hasTag(0x8001, MemoryTag::GenericData));
    EXPECT_TRUE(tagMap.hasTag(0x8002, MemoryTag::Variables));
}

// =============================================================================
// MusicClassifier Tests
// =============================================================================

TEST_F(Classifier_Test, MusicClassifierRequiresPortTracker)
{
    BlockTagMap tagMap;
    tagMap.clear();
    tagMap.setType(0x8000, BlockType::CODE);

    MusicClassifier classifier;
    AnalysisContext ctx{};
    ctx.portTracker = nullptr;

    // Should not crash with null portTracker
    classifier.classify(ctx, tagMap);

    // No tags should be added without port data
    EXPECT_FALSE(tagMap.hasTag(0x8000, MemoryTag::MusicPlayerCode));
}

TEST_F(Classifier_Test, MusicClassifierTagsAYWriters)
{
    ASSERT_NE(_context->pPortTracker, nullptr);

    BlockTagMap tagMap;
    tagMap.clear();
    tagMap.setType(0x8000, BlockType::CODE);

    // Simulate AY register write from code at 0x8000
    _context->pPortTracker->StartSession();
    _context->pPortTracker->TrackWrite(0xFFFD, 0x07, 0x8000);  // AY register select
    _context->pPortTracker->TrackWrite(0xBFFD, 0x38, 0x8000);  // AY data write

    MusicClassifier classifier;
    AnalysisContext ctx{};
    ctx.portTracker = _context->pPortTracker;

    classifier.classify(ctx, tagMap);

    EXPECT_TRUE(tagMap.hasTag(0x8000, MemoryTag::MusicPlayerCode));

    _context->pPortTracker->StopSession();
}

// =============================================================================
// LoaderClassifier Tests
// =============================================================================

TEST_F(Classifier_Test, LoaderClassifierTagsFDCAccess)
{
    ASSERT_NE(_context->pPortTracker, nullptr);

    BlockTagMap tagMap;
    tagMap.clear();
    tagMap.setType(0x8100, BlockType::CODE);

    // Simulate FDC port access from code at 0x8100
    _context->pPortTracker->StartSession();
    _context->pPortTracker->TrackRead(0x1F, 0x00, 0x8100);   // FDC status read
    _context->pPortTracker->TrackWrite(0x1F, 0x80, 0x8100);  // FDC command write

    LoaderClassifier classifier;
    AnalysisContext ctx{};
    ctx.portTracker = _context->pPortTracker;

    classifier.classify(ctx, tagMap);

    EXPECT_TRUE(tagMap.hasTag(0x8100, MemoryTag::DiskLoaderCode));

    _context->pPortTracker->StopSession();
}

TEST_F(Classifier_Test, LoaderClassifierTagsTapeReads)
{
    ASSERT_NE(_context->pPortTracker, nullptr);

    BlockTagMap tagMap;
    tagMap.clear();
    tagMap.setType(0x8200, BlockType::CODE);

    // Simulate frequent tape port reads (>100 threshold)
    _context->pPortTracker->StartSession();
    for (int i = 0; i < 150; i++)
    {
        _context->pPortTracker->TrackRead(0xFE, static_cast<uint8_t>(i & 0x40), 0x8200);
    }

    LoaderClassifier classifier;
    AnalysisContext ctx{};
    ctx.portTracker = _context->pPortTracker;

    classifier.classify(ctx, tagMap);

    EXPECT_TRUE(tagMap.hasTag(0x8200, MemoryTag::TapeLoaderCode));

    _context->pPortTracker->StopSession();
}

// =============================================================================
// ScreenClassifier Tests
// =============================================================================

TEST_F(Classifier_Test, ScreenClassifierTagsShadowScreen)
{
    ASSERT_NE(_context->pPortTracker, nullptr);

    BlockTagMap tagMap;
    tagMap.clear();

    // Simulate shadow screen selection via port 0x7FFD bit 3
    _context->pPortTracker->StartSession();
    _context->pPortTracker->TrackWrite(0x7FFD, 0x08, 0x8000);  // Bit 3 = shadow screen

    ScreenClassifier classifier;
    AnalysisContext ctx{};
    ctx.portTracker = _context->pPortTracker;
    ctx.emulatorContext = _context;

    classifier.classify(ctx, tagMap);

    // Shadow VRAM should be tagged as ScreenBuffer
    EXPECT_TRUE(tagMap.hasTag(0xC000, MemoryTag::ScreenBuffer));
    EXPECT_TRUE(tagMap.hasTag(0xDAFF, MemoryTag::ScreenBuffer));

    _context->pPortTracker->StopSession();
}

TEST_F(Classifier_Test, ScreenClassifierNoShadowWithoutPortWrite)
{
    ASSERT_NE(_context->pPortTracker, nullptr);

    BlockTagMap tagMap;
    tagMap.clear();

    // Port tracker active but no shadow screen selection
    _context->pPortTracker->StartSession();

    ScreenClassifier classifier;
    AnalysisContext ctx{};
    ctx.portTracker = _context->pPortTracker;
    ctx.emulatorContext = _context;

    classifier.classify(ctx, tagMap);

    // Shadow VRAM should NOT be tagged
    EXPECT_FALSE(tagMap.hasTag(0xC000, MemoryTag::ScreenBuffer));

    _context->pPortTracker->StopSession();
}
