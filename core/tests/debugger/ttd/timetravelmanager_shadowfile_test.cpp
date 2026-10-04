/// @file timetravelmanager_shadowfile_test.cpp
/// @brief The shadow session written as it records (Phase 4,
/// TimeTravelManager::SetShadowRecordingRoot): a recording folder with a file
/// per segment, finished at stop, loading back as the session the engine
/// holds; invalidating the session deletes the folder.
///
/// Boots a Pentagon and records 100 frames: slower than the 50 ms guideline,
/// one acceptance check.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdrecordingwriter.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

TEST(TimeTravelManager_ShadowFile_Test, TheSessionIsWrittenAsItRecords)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    // DI; loop: INC A; LD (#C000),A; OUT (#FE),A; JP loop - memory and ports change every frame
    const uint8_t program[] = {0xF3, 0x3C, 0x32, 0x00, 0xC0, 0xD3, 0xFE, 0xC3, 0x01, 0x80};
    for (uint16_t i = 0; i < sizeof(program); ++i)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    emulator->GetZ80State()->pc = 0x8000;

    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    ttd::TimeTravelEngine engine;
    engine.SetHistoryPolicy({ttd::TTDHistoryMode::Growable, 0, 40});
    const std::string root = TestPathHelper::GetUniqueTestScratchPath("ttd-shadow-files");
    FileHelper::DeleteFolder(root);
    ttd->SetShadowEngine(&engine);
    ttd->SetShadowRecordingRoot(root);
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(100, true);
    const std::string folder = ttd->ShadowRecordingFolder();
    ASSERT_FALSE(folder.empty());
    ttd->StopRecording();

    std::vector<std::string> files;
    for (uint32_t n = 0;; ++n)
    {
        char name[32];
        std::snprintf(name, sizeof(name), "segment-%04u.ttd", n);
        const std::string path = FileHelper::PathCombine(folder, name);
        if (!FileHelper::FileExists(path))
            break;
        files.push_back(path);
    }
    EXPECT_EQ(files.size(), engine.Segments().size()) << "a file per segment";
    ASSERT_GE(files.size(), 3u);

    ttd::TimeTravelEngine loaded;
    std::string error;
    ttd::TTDSessionLoadReport report;
    ASSERT_TRUE(ttd::LoadRecording(loaded, files, 0, error, &report)) << error;
    EXPECT_TRUE(report.complete) << report.stoppedAt;
    ASSERT_EQ(loaded.CheckpointCount(), engine.CheckpointCount());
    std::vector<uint8_t> a(size_t(engine.Regions()[0].pieces) * ttd::kTTDPieceSize);
    std::vector<uint8_t> b(a.size());
    for (size_t i = 0; i < engine.CheckpointCount(); i += 7)
    {
        ASSERT_TRUE(engine.RestoreRegion(i, 0, a.data()).Ok());
        ASSERT_TRUE(loaded.RestoreRegion(i, 0, b.data()).Ok());
        ASSERT_TRUE(a == b) << "RAM at checkpoint " << i;
    }
    EXPECT_EQ(loaded.BusWrites().Size(), engine.BusWrites().Size());

    ttd->InvalidateSession("test");
    EXPECT_FALSE(FileHelper::FolderExists(folder)) << "an invalidated session leaves no files";
    ttd->SetShadowEngine(nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
