/// @file timetravelmanager_hostwrites_test.cpp
/// @brief FR-20: while time travel replays history, nothing the machine does
/// writes a host file. v1's replay holds the media manager's host writes
/// (MediaManager::HoldHostWrites) for exactly as long as it runs.
///
/// A guest loop writes to a test port; the port's handler notes whether the
/// media manager held host writes at that moment. Live: never; inside the
/// replay a mid-frame seek runs: always.

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

namespace
{
constexpr uint16_t kProbePort = 0x1235;   // a full-decode observer: sees every OUT to it

/// Notes, at each OUT, whether host writes were held
struct HoldProbe : PortDevice
{
    MediaManager* manager = nullptr;
    uint64_t live = 0, held = 0;
    uint8_t portDeviceInMethod(uint16_t) override { return 0xFF; }
    void portDeviceOutMethod(uint16_t, uint8_t) override { (manager->HoldingHostWrites() ? held : live)++; }
};
}  // namespace

TEST(TimeTravelManager_HostWrites_Test, AReplayHoldsHostWritesForExactlyItsDuration)
{
    Emulator emulator(LoggerLevel::LogError);
    ASSERT_TRUE(emulator.Init());
    EmulatorContext* context = emulator.GetContext();
    ttd::TimeTravelManager* ttd = context->pTimeTravelManager;
    ASSERT_NE(context->pMediaManager, nullptr);
    FeatureManager* fm = emulator.GetFeatureManager();
    fm->setFeature(Features::kDebugMode, true);
    fm->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();

    HoldProbe probe;
    probe.manager = context->pMediaManager;
    ASSERT_TRUE(context->pPortDecoder->RegisterFullDecodePort(kProbePort, &probe));
    // DI; LD BC,kProbePort; loop: OUT (C),A; JR loop
    const uint8_t program[] = {0xF3, 0x01, kProbePort & 0xFF, kProbePort >> 8, 0xED, 0x79, 0x18, 0xFC};
    for (size_t i = 0; i < sizeof(program); ++i)
        context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    Z80* z80 = context->pCore->GetZ80();
    z80->pc = 0x8000;
    z80->sp = 0xBFF0;

    ASSERT_TRUE(ttd->StartRecording());
    emulator.RunNFrames(6, /*skipBreakpoints=*/true);
    ttd->StopRecording();
    ASSERT_GT(probe.live, 1000u) << "the probe port is the guest's";
    EXPECT_EQ(probe.held, 0u) << "live: host writes are never held";

    const uint64_t frame = ttd->GetCheckpoint(2)->time.frame;
    ASSERT_TRUE(ttd->SeekTo({frame, 30000}));   // inside a frame: a replay from its checkpoint
    EXPECT_GT(probe.held, 1000u) << "the replay ran with host writes held";
    EXPECT_FALSE(context->pMediaManager->HoldingHostWrites()) << "released when the replay ended";

    context->pPortDecoder->UnregisterFullDecodePort(kProbePort, &probe);
}
