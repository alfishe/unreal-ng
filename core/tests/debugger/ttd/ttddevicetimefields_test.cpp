// Device states with clocks that advance every frame at an idle machine store them as time fields (Phase 2, §5.2.3;
// measured field by field 2026-10-09, docs/inprogress/2026-09-25-ttd-v2-migration/e6-comparison.md): the engine keeps
// their residual from a straight line, so an idle frame costs a few bytes, not the clocks' changing bytes.
// Bytes per recorded frame (150 frames after 10 that settle the lines), with the time fields / without them:
//   VDAC2 card 0.0 / 13.2, Sprinter PLD 0.3 / 8.6, ZX-Evo AVR volatile bytes 3.8 / 6.5, WD1793 on the Sprinter 13.6 /
//   19.7, Profi XT KBC 35.5 / 44.5 (the rest: real state - the controller's fraction, the MCU's registers). Each bound
//   lies between the two.
// The 16550s a ZiFi peer clocks (the ZiFi line and the #xxEF port) are not time fields: their clock is the time of
// the last Advance, a few T-states off the frame's end, so a residual would change sign from frame to frame. The
// UART saves its clocks as 0 while no character is on a line instead: 0.0 / 6.8 each.
//
// Runtime: boot-bound (each machine boots 300 frames in turbo, then 160 recorded frames), 1-2 s per machine.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "emulator/emulator.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/memory/memory.h"

namespace
{
struct Case
{
    const char* model;
    const char* device;   ///< the engine's region "device.<instance>"
    double maxBytesPerFrame;
    const char* zifi = nullptr;   ///< a ZiFi peer ([NETWORK] ZiFi): the ZiFi line and the #xxEF port run clocked
};

void PrintTo(const Case& c, std::ostream* os) { *os << c.model << "/" << c.device; }
}  // namespace

class TTDDeviceTimeFields_Test : public ::testing::TestWithParam<Case>
{
};

TEST_P(TTDDeviceTimeFields_Test, AnIdleFrameCostsTheClocksNothing)
{
    const Case c = GetParam();
    SoundCardScope cards;
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator(c.model, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->EnableTurboMode();
    emulator->RunNFrames(300, true);
    emulator->DisableTurboMode();
    EmulatorContext* context = emulator->GetContext();
    if (c.zifi)
    {
        NetworkManager::Change change;
        std::string error;
        ASSERT_TRUE(NetworkManager::ParseChange({{"zifi", c.zifi}}, change, error)) << error;
        ASSERT_TRUE(context->pCore->GetNetworkManager()->RequestChange(change, error)) << error;
        emulator->RunNFrames(2, true);   // the change lands at a frame boundary
    }
    context->pFeatureManager->setFeature(Features::kDebugMode, true);
    context->pFeatureManager->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(10, true);   // the lines through the clocks settle

    const ttd::TimeTravelEngine& engine = ttd->GetEngine();
    uint32_t region = UINT32_MAX;
    for (uint32_t r = 0; r < engine.Regions().size(); ++r)
        if (engine.Regions()[r].name == std::string("device.") + c.device)
            region = r;
    ASSERT_NE(region, UINT32_MAX) << c.model << " records no " << c.device;
    const uint64_t before = engine.RegionPayloadBytes(region);
    constexpr int kFrames = 150;
    emulator->RunNFrames(kFrames, true);
    const double perFrame = static_cast<double>(engine.RegionPayloadBytes(region) - before) / kFrames;
    ttd->StopRecording();
    EXPECT_LE(perFrame, c.maxBytesPerFrame) << c.model << " " << c.device << ": bytes per idle frame";
    EmulatorTestHelper::CleanupEmulator(emulator);
}

INSTANTIATE_TEST_SUITE_P(Devices, TTDDeviceTimeFields_Test,
                         ::testing::Values(Case{"TSL-VDAC2", "vdac2", 4.0}, Case{"SPRINTER", "sprinterpld", 3.0},
                                           Case{"TSL", "evoavrvolatile", 5.0}, Case{"SPRINTER", "betadisk", 16.5},
                                           Case{"PROFI", "profixtkbc", 40.0},
                                           Case{"TSL", "zifi.uart", 0.5, "ZIFI-NATIVE,S3"},
                                           Case{"TSL", "uart", 0.5, "ZIFI-NATIVE,S3"}),
                         [](const auto& info) {
                             std::string name = std::string(info.param.model) + "_" + info.param.device;
                             for (char& ch : name)
                                 if (!isalnum(static_cast<unsigned char>(ch)))
                                     ch = '_';
                             return name;
                         });
