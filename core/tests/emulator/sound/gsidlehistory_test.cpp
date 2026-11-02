// The GS card's histories over its firmware's idle poll loop: the loop is slept through with them on as off, and each
// sleep is one record (the event trace's `idle`, the execution history's idle_iterations), not its port reads or
// instructions one by one

#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "debugger/history/exechistory.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/gsporttrace.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/state/statenodejson.h"

namespace
{
// gs105a: COMINT_, the idle poll loop after POST (IN A,(4) / RRCA / JR C ...), interrupts off
constexpr uint16_t kIdleLoop = 0x026E;

struct Card
{
    EmulatorContext ctx{ LoggerLevel::LogError };
    std::unique_ptr<SoundChip_GeneralSound> chip;
    GSHistory events{ "gs", "GS port and activity events", 4096 };
    history::DeviceHistory<ExecRecord> exec{ "gs.cpu", "the card's instructions", 4096 };

    Card()
    {
        ctx.config.sound.gs_vol = 8000;
        ctx.config.frame = 69888;
        ctx.config.frame_duration_us = 19968;
        ctx.emulatorState.current_z80_frequency_multiplier = 1;
        ctx.emulatorState.hw_turbo_ratio_applied = 1;
        chip = std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
        chip->loadROM("rom/gs105a.rom");
        chip->attachPortTrace(&events);
        chip->attachExecHistory(&exec, &ctx);
    }
    ~Card()
    {
        chip->attachExecHistory(nullptr, nullptr);
        chip->attachPortTrace(nullptr);
    }

    void frame()
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }

    /// POST, then the host takes the reply byte: the firmware is in its poll loop
    void boot()
    {
        for (int i = 0; i < 200 && chip->getActivityCounters().volumeLatchWrites < 4; i++)
            frame();
        frame();
        if (chip->readStatus() & 0x80)
            (void)chip->portDeviceInMethod(GeneralSoundCard::PORT_DATA);
        frame();
    }

    template <typename Record>
    static void On(history::DeviceHistory<Record>& h)
    {
        std::string error;
        ASSERT_TRUE(h.Configure({ .mode = history::Mode::Ring, .limit = 4096 }, error)) << error;
    }
};
}  // namespace

/// The event trace on: the poll loop still sleeps, a frame of it is a few records with an `idle` one at its head
TEST(GSIdleHistory_Test, EventTraceSleepsAndRecordsTheSleep)
{
    Card card;
    card.boot();
    Card::On(card.events);
    const uint64_t slept = card.chip->stepsSlept();
    card.frame();
    EXPECT_GT(card.chip->stepsSlept(), slept) << "the trace does not keep the firmware awake";

    const auto records = card.events.Snapshot();
    EXPECT_LT(records.size(), 200u) << "a frame of polling is not thousands of FLAGS reads";
    bool idle = false;
    for (const GSTraceEvent& e : records)
        if (e.side == GSTraceSide::Idle)
        {
            idle = true;
            EXPECT_EQ(e.pc, kIdleLoop);
            EXPECT_GT(e.iterations, 0u);
        }
    EXPECT_TRUE(idle);
    const std::string json = StateNodeToJsonText(card.events.Records(0));
    EXPECT_NE(json.find("\"side\":\"idle\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"iterations\""), std::string::npos) << json;
}

/// The execution history on: the record loop sleeps too, the sleep is one record with its iterations
TEST(GSIdleHistory_Test, ExecutionHistorySleepsAndRecordsTheSleep)
{
    Card card;
    card.boot();
    Card::On(card.exec);
    const uint64_t slept = card.chip->stepsSlept();
    card.frame();
    EXPECT_GT(card.chip->stepsSlept(), slept) << "the history does not keep the firmware awake";

    const auto records = card.exec.Snapshot();
    ASSERT_FALSE(records.empty());
    EXPECT_LT(records.size(), 1000u) << "a frame of polling is not its instructions one by one";
    bool idle = false;
    for (const ExecRecord& r : records)
        if (r.idle)
        {
            idle = true;
            EXPECT_EQ(r.pc, kIdleLoop);
        }
    EXPECT_TRUE(idle);
    const std::string json = StateNodeToJsonText(card.exec.Records(0));
    EXPECT_NE(json.find("\"idle_iterations\""), std::string::npos) << json;
}
