/// @file timetravelmanager_shadow_test.cpp
/// @brief Shadow mode: TimeTravelEngine records the running emulator next to v1
/// (TimeTravelManager::SetShadowEngine) and every frame it records restores
/// exactly as v1's checkpoint of the same frame
/// (docs/inprogress/2026-09-25-ttd-v2-migration/phase-1-memory-regions-tdd.md §4.5).
///
/// Boots a Pentagon and runs a game for a few hundred frames: slower than the
/// 50 ms guideline, it is the live counterpart of the corpus oracle.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class TimeTravelManager_Shadow_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;
    ttd::TimeTravelEngine _engine;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _v1 = _context->pTimeTravelManager;
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        const auto sna = TestPathHelper::FindProjectRoot() / "testdata/loaders/sna/action.sna";
        ASSERT_TRUE(_emulator->LoadSnapshot(sna.string())) << sna;
    }

    void TearDown() override
    {
        if (_v1)
            _v1->SetShadowEngine(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    /// v1's checkpoint index of @p frame, or -1
    int64_t V1IndexOf(uint64_t frame) const
    {
        for (size_t i = 0; i < _v1->GetCheckpointCount(); ++i)
            if (_v1->GetCheckpoint(i)->time.frame == frame)
                return static_cast<int64_t>(i);
        return -1;
    }

    /// Every engine checkpoint equals v1's checkpoint of the same frame
    void ExpectEngineMatchesV1()
    {
        ASSERT_GT(_engine.CheckpointCount(), 0u);
        std::vector<uint8_t> v1Ram, v1Present, engineRam, enginePresent;
        std::string err;
        for (size_t i = 0; i < _engine.CheckpointCount(); ++i)
        {
            const ttd::TTDEngineCheckpoint* got = _engine.Checkpoint(i);
            const int64_t v = V1IndexOf(got->position.frame);
            ASSERT_GE(v, 0) << "v1 has no checkpoint of frame " << got->position.frame;
            const ttd::TTDCheckpoint* want = _v1->GetCheckpoint(static_cast<size_t>(v));
            EXPECT_EQ(std::memcmp(&got->cpu, &want->cpu, sizeof(want->cpu)), 0) << "CPU, frame " << got->position.frame;
            EXPECT_EQ(got->deviceBlobs, want->peripheralBlobs) << "devices, frame " << got->position.frame;
            ASSERT_TRUE(ttd::bench::DecodeV1Ram(*_v1, static_cast<size_t>(v), v1Ram, v1Present, err)) << err;
            engineRam.assign(v1Ram.size(), 0);
            ASSERT_TRUE(_engine.RestoreRegion(i, 0, engineRam.data(), &enginePresent).Ok());
            ASSERT_TRUE(engineRam == v1Ram) << "RAM differs at frame " << got->position.frame;
        }
    }
};

TEST_F(TimeTravelManager_Shadow_Test, EveryRecordedFrameMatchesV1_AndTheLastMatchesLiveMemory)
{
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(300, /*skipBreakpoints=*/true);
    _v1->StopRecording();

    ASSERT_EQ(_engine.CheckpointCount(), _v1->GetCheckpointCount());
    ASSERT_NO_FATAL_FAILURE(ExpectEngineMatchesV1());

    // The delta base invariant, seen from outside: the last checkpoint is the live RAM
    const size_t last = _engine.CheckpointCount() - 1;
    std::vector<uint8_t> ram(_engine.Regions()[0].pieces * size_t(ttd::kTTDPieceSize));
    ASSERT_TRUE(_engine.RestoreRegion(last, 0, ram.data()).Ok());
    for (uint32_t page = 0; page < _engine.Regions()[0].pieces / 4; ++page)
        ASSERT_EQ(std::memcmp(ram.data() + size_t(page) * 4 * ttd::kTTDPieceSize,
                              _context->pMemory->RAMPageAddress(static_cast<uint16_t>(page)), 4 * ttd::kTTDPieceSize),
                  0)
            << "page " << page;

    // The engine stored only real changes: far fewer versions than v1's key frames re-store
    EXPECT_LT(_engine.PieceStore().LiveVersions(), _v1->GetPageStore().GetUsedSlots());
}

TEST_F(TimeTravelManager_Shadow_Test, ResumeFromThePastStartsANewEngineSession)
{
    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(120, /*skipBreakpoints=*/true);
    const uint64_t first = _v1->GetCheckpoint(0)->time.frame;
    const uint64_t resumeAt = first + 40;

    // v1 cuts its history after the resume point; the engine records the trunk
    // only, so it starts over from there (branches come with Phase 5)
    ASSERT_TRUE(_v1->ResumeRecordingFrom({resumeAt, 0}));
    _emulator->RunNFrames(60, /*skipBreakpoints=*/true);
    _v1->StopRecording();

    ASSERT_GT(_engine.CheckpointCount(), 0u);
    EXPECT_GT(_engine.Checkpoint(0)->position.frame, resumeAt - 1) << "the engine's session starts after the resume";
    ASSERT_NO_FATAL_FAILURE(ExpectEngineMatchesV1());
}

TEST_F(TimeTravelManager_Shadow_Test, DetachedEngineRecordsNothing)
{
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(20, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    EXPECT_FALSE(_engine.IsSessionOpen());
    EXPECT_EQ(_engine.CheckpointCount(), 0u);
}
