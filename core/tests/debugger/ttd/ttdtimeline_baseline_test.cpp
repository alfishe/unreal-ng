#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

/// The timeline at a segment's start (TD-5 blobs): a baseline checkpoint stores every piece anew, which is no change
/// of the machine. RAM pieces, device regions and device state blobs count there only what differs from the
/// checkpoint before.
///
/// Runtime justification: 20 recorded frames of a small loop on a Pentagon.
TEST(TTDTimelineBaseline_Test, ASegmentStartIsNoChange)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();

    // Writes one byte a pass at #6000 (one RAM piece a frame), nothing else
    static const uint8_t program[] = {
        0xF3,              // 8000 DI
        0x21, 0x00, 0x60,  // 8001 LD HL,#6000
        0x34,              // 8004 loop: INC (HL)
        0x18, 0xFD,        // 8005 JR loop
    };
    emulator->RunNFrames(5);
    Z80* z80 = context->pCore->GetZ80();
    for (size_t i = 0; i < sizeof(program); i++)
        z80->DirectWrite(static_cast<uint16_t>(0x8000 + i), program[i]);
    z80->pc = 0x8000;

    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(10);
    ttd->GetEngine().RequestBaseline(ttd::kTTDTrunk);
    emulator->RunNFrames(10);
    ttd->StopRecording();

    const ttd::TimeTravelEngine& engine = ttd->GetEngine();
    size_t baseline = 0;
    for (size_t i = engine.FirstCheckpoint() + 1; i < engine.CheckpointCount(); ++i)
        if (engine.Checkpoint(i)->baseline)
            baseline = i;
    ASSERT_GT(baseline, 0u) << "no segment started inside the recording";
    uint32_t ramRegion = 0;
    for (uint32_t r = 0; r < engine.Regions().size(); ++r)
        if (engine.Regions()[r].name == "ram")
            ramRegion = r;
    ASSERT_GT(engine.ChangeCount(baseline, ramRegion), 8u) << "a baseline stores every RAM piece";

    ttd::TTDTimelineOptions o;
    o.buckets = true;
    o.bucketSize = 1;
    o.limit = 500;
    const ttd::TTDTimelineResult r = ttd->QueryTimeline(o);
    ASSERT_TRUE(r.error.empty()) << r.error;
    const uint64_t frame = engine.Checkpoint(baseline - 1)->position.frame;   // the frame the baseline ends
    bool found = false;
    for (const ttd::TTDTimelineBucket& b : r.buckets)
    {
        // Every frame increments #6000 and moves the stack nowhere: one or two pieces, the baseline's frame too
        EXPECT_LE(b.ramPieces, 2u) << "frame " << b.frameStart;
        EXPECT_GE(b.ramPieces, 1u) << "frame " << b.frameStart;
        EXPECT_EQ(b.screenFrames, 0u) << "frame " << b.frameStart;
        if (b.frameStart != frame)
            continue;
        found = true;
        for (size_t k = 0; k < b.stateFrames.size(); ++k)
            if (!r.stateEveryFrame[k])
                EXPECT_EQ(b.stateFrames[k], 0u) << r.states[k].name;
    }
    EXPECT_TRUE(found);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
