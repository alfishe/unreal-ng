/// @file ttdv1feeder_test.cpp
/// @brief The engine oracle: every recorded v1 session, fed to TimeTravelEngine
/// frame by frame, restores every checkpoint exactly as v1 does
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.2, §6).
///
/// The corpus is testdata/ttd and testdata/machines/<machine>/ttd: real
/// recordings on Pentagon, ZX-Evo, TS-Conf and the other models with fixtures.
/// Each file boots its machine and decodes every checkpoint twice (engine
/// and v1), so the test takes a few seconds - it is the engine's acceptance
/// check, not a unit test.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/gsslot.h"
#include "_helpers/soundcardscope.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdfileinfo.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

namespace
{
namespace fs = std::filesystem;

std::vector<fs::path> CorpusFiles()
{
    std::vector<fs::path> files;
    const fs::path root = TestPathHelper::FindProjectRoot() / "testdata";
    std::vector<fs::path> dirs = {root / "ttd"};
    if (fs::exists(root / "machines"))
        for (const auto& machine : fs::directory_iterator(root / "machines"))
            dirs.push_back(machine.path() / "ttd");
    for (const fs::path& dir : dirs)
        if (fs::exists(dir))
            for (const auto& entry : fs::directory_iterator(dir))
                if (entry.path().extension() == ".ttd")
                    files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    return files;
}
}  // namespace

class TTDV1Feeder_Test : public ::testing::Test
{
protected:
    // The fixtures were recorded on the shipped configs, cards included
    SoundCardScope _soundCards;
    Emulator* _emulator = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;

    void StartMachine(const std::string& model, GSTypeKind generalSound)
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        EmulatorContext* context = _emulator->GetContext();
        _v1 = context->pTimeTravelManager;
        ASSERT_NE(_v1, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        ASSERT_TRUE(FitGeneralSoundCard(context->pSoundManager, generalSound));
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        _emulator = nullptr;
    }

    /// Load @p file into v1 on a machine of its model
    void LoadIntoV1(const fs::path& file)
    {
        ttd::TTDFileInfo info;
        std::string err;
        ASSERT_TRUE(ttd::ReadTTDFileInfo(file.string(), info, err)) << err;
        ASSERT_NO_FATAL_FAILURE(StartMachine(info.machine.model, info.machine.generalSound));
        std::ifstream in(file, std::ios::binary);
        ASSERT_TRUE(_v1->DeserializeSession(in, err)) << err;
        ASSERT_GT(_v1->GetCheckpointCount(), 0u);
    }
};

TEST_F(TTDV1Feeder_Test, EngineRestoresEveryCheckpointAsV1)
{
    const auto files = CorpusFiles();
    ASSERT_GE(files.size(), 5u) << "testdata/ttd/ should hold the recorded corpus";

    for (const fs::path& file : files)
    {
        SCOPED_TRACE(file.filename().string());
        ASSERT_NO_FATAL_FAILURE(LoadIntoV1(file));

        ttd::TimeTravelEngine engine;
        std::string err;
        ttd::bench::FeedStats stats;
        ASSERT_TRUE(ttd::bench::FeedV1Session(*_v1, engine, err, &stats)) << err;
        const size_t count = _v1->GetCheckpointCount();
        ASSERT_EQ(engine.CheckpointCount(), count);
        ASSERT_EQ(stats.checkpoints, count);

        std::vector<uint8_t> v1Ram;
        std::vector<uint8_t> v1Present;
        std::vector<uint8_t> engineRam;
        std::vector<uint8_t> enginePresent;
        for (size_t i = 0; i < count; ++i)
        {
            const ttd::TTDCheckpoint* want = _v1->GetCheckpoint(i);
            const ttd::TTDEngineCheckpoint* got = engine.Checkpoint(i);
            ASSERT_NE(got, nullptr);

            // Position, parent and time
            EXPECT_EQ(got->position.frame, want->time.frame) << "checkpoint " << i;
            EXPECT_EQ(got->parent, i == 0 ? ttd::TTDEngineCheckpoint::kNoParent : static_cast<uint32_t>(i - 1));
            EXPECT_EQ(engine.CheckpointIndexOf(got->position), static_cast<int64_t>(i));

            // CPU, chipset, devices
            EXPECT_EQ(std::memcmp(&got->cpu, &want->cpu, sizeof(want->cpu)), 0) << "CPU at checkpoint " << i;
            EXPECT_EQ(std::memcmp(&got->chipset, &want->chipset, sizeof(want->chipset)), 0)
                << "chipset at checkpoint " << i;
            EXPECT_EQ(got->deviceBlobs, want->peripheralBlobs) << "device state at checkpoint " << i;

            // Memory, byte for byte, and the same set of pieces the session knows
            ASSERT_TRUE(ttd::bench::DecodeV1Ram(*_v1, i, v1Ram, v1Present, err)) << err;
            engineRam.assign(v1Ram.size(), 0);
            const ttd::TTDRestoreResult r = engine.RestoreRegion(i, 0, engineRam.data(), &enginePresent);
            ASSERT_TRUE(r.Ok()) << r.message;
            ASSERT_EQ(enginePresent, v1Present) << "pieces known at checkpoint " << i;
            if (engineRam != v1Ram)
            {
                size_t p = 0;
                while (std::memcmp(engineRam.data() + p * ttd::kTTDPieceSize, v1Ram.data() + p * ttd::kTTDPieceSize,
                                   ttd::kTTDPieceSize) == 0)
                    ++p;
                FAIL() << "RAM differs at checkpoint " << i << ", piece " << p;
            }
        }
    }
}

TEST_F(TTDV1Feeder_Test, FeedsOnlyRealChanges)
{
    // v1 stores every non-zero piece again at each key frame (every 50 frames);
    // the feeder must hand the engine only content that changed
    const fs::path idle = TestPathHelper::FindProjectRoot() / "testdata/ttd/idle_session.ttd";
    ASSERT_TRUE(fs::exists(idle));
    ASSERT_NO_FATAL_FAILURE(LoadIntoV1(idle));

    ttd::TimeTravelEngine engine;
    std::string err;
    ttd::bench::FeedStats stats;
    ASSERT_TRUE(ttd::bench::FeedV1Session(*_v1, engine, err, &stats)) << err;

    // Every piece v1 stored under a new slot id: real changes plus the key
    // frames' re-stores of unchanged content. The feeder must hand over fewer
    // (feeding every new slot would make the two equal)
    size_t newSlots = 0;
    for (size_t i = 0; i < _v1->GetCheckpointCount(); ++i)
    {
        const ttd::TTDCheckpoint* cp = _v1->GetCheckpoint(i);
        const ttd::TTDCheckpoint* prev = i ? _v1->GetCheckpoint(i - 1) : nullptr;
        for (size_t page = 0; page < cp->ramPages.size(); ++page)
            for (uint32_t sub = 0; sub < 4; ++sub)
            {
                const uint32_t slot = cp->ramPages[page].pageSlots[sub];
                if (slot != ttd::TTDPageRef::kNeverTouched &&
                    (!prev || prev->ramPages[page].pageSlots[sub] != slot))
                    ++newSlots;
            }
    }
    ASSERT_GE(_v1->GetCheckpointCount(), 100u) << "the session must span key frames";
    EXPECT_LT(stats.changedPieces, newSlots) << "the key frames' re-stores must not be fed as changes";
}
