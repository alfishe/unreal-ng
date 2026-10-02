/// @file timetravelmanager_shadowmodels_test.cpp
/// @brief Shadow mode on every machine with large memory: the engine records
/// each model next to v1 and every frame it records restores exactly as v1's
/// checkpoint of the same frame (machine RAM, CPU, chipset, device state).
/// Machine RAM is the engine's region 0 on every model; this checks that the
/// one mechanism holds on 512 KB-4 MB machines and their paging schemes.
///
/// Each case boots its model from ROM and records 150 frames: slower than the
/// 50 ms guideline, one acceptance check per model.

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/bench/ttdv1feeder.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

class TimeTravelManager_ShadowModels_Test : public ::testing::TestWithParam<const char*>
{
protected:
    Emulator* _emulator = nullptr;
    ttd::TimeTravelManager* _v1 = nullptr;
    ttd::TimeTravelEngine _engine;

    void TearDown() override
    {
        if (_v1)
            _v1->SetShadowEngine(nullptr);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
};

TEST_P(TimeTravelManager_ShadowModels_Test, EveryFrameRestoresAsV1)
{
    _emulator = EmulatorTestHelper::CreateStandardEmulator(GetParam(), LoggerLevel::LogError);
    ASSERT_NE(_emulator, nullptr) << GetParam();
    EmulatorContext* context = _emulator->GetContext();
    _v1 = context->pTimeTravelManager;
    _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();

    _v1->SetShadowEngine(&_engine);
    ASSERT_TRUE(_v1->StartRecording());
    _emulator->RunNFrames(150, /*skipBreakpoints=*/true);
    _v1->StopRecording();
    _v1->SetShadowEngine(nullptr);

    ASSERT_EQ(_engine.CheckpointCount(), _v1->GetCheckpointCount());
    ASSERT_FALSE(_engine.Regions().empty());
    EXPECT_EQ(_engine.Regions()[0].pieces, _v1->GetCheckpoint(0)->ramPages.size() * 4)
        << "region 0 covers the model's whole RAM";

    std::vector<uint8_t> v1Ram, v1Present, engineRam, enginePresent;
    std::string err;
    for (size_t i = 0; i < _engine.CheckpointCount(); ++i)
    {
        const ttd::TTDCheckpoint* want = _v1->GetCheckpoint(i);
        const ttd::TTDEngineCheckpoint* got = _engine.Checkpoint(i);
        ASSERT_EQ(got->position.frame, want->time.frame);
        EXPECT_EQ(std::memcmp(&got->cpu, &want->cpu, sizeof(want->cpu)), 0) << "CPU, checkpoint " << i;
        EXPECT_EQ(std::memcmp(&got->chipset, &want->chipset, sizeof(want->chipset)), 0) << "chipset, checkpoint " << i;
        EXPECT_EQ(got->deviceBlobs, want->peripheralBlobs) << "devices, checkpoint " << i;
        ASSERT_TRUE(ttd::bench::DecodeV1Ram(*_v1, i, v1Ram, v1Present, err)) << err;
        engineRam.assign(v1Ram.size(), 0);
        ASSERT_TRUE(_engine.RestoreRegion(i, 0, engineRam.data(), &enginePresent).Ok());
        ASSERT_EQ(enginePresent, v1Present) << "pieces known, checkpoint " << i;
        ASSERT_TRUE(engineRam == v1Ram) << "RAM differs at checkpoint " << i;
    }
}

INSTANTIATE_TEST_SUITE_P(LargeMemoryModels, TimeTravelManager_ShadowModels_Test,
                         ::testing::Values("PENTAGON", "SCORPION", "PROFSCORP", "PROFI", "ATM710", "ATM450", "ATM3",
                                           "TSL", "SPRINTER"),
                         [](const ::testing::TestParamInfo<const char*>& info) { return std::string(info.param); });
