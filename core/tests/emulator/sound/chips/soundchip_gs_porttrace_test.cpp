// GS port/activity tracer tests (triage instrumentation requested to debug
// "is the GS coprocessor alive and doing DAC pushes" without guessing).

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/soundchip_gs.h"

namespace
{
std::string writeRom(const char* leafName, const uint8_t* program, size_t programSize)
{
    std::vector<uint8_t> image(SoundChip_GeneralSound::ROM_SIZE, 0x00);
    memcpy(image.data(), program, programSize);
    std::string path = TestPathHelper::GetUniqueTestScratchPath(leafName);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(image.data()), image.size());
    return path;
}

std::unique_ptr<SoundChip_GeneralSound> makeChip(EmulatorContext& ctx)
{
    ctx.config.sound.gs_vol = 8000;
    ctx.config.frame = 69888;
    ctx.config.frame_duration_us = 19968;
    ctx.emulatorState.current_z80_frequency_multiplier = 1;
    ctx.emulatorState.hw_turbo_shift_applied = 0;
    return std::make_unique<SoundChip_GeneralSound>(&ctx, 512, 44100);
}
} // namespace

TEST(SoundChip_GeneralSound_PortTrace, CountersTrackCpuAndDacActivity)
{
    EmulatorContext ctx(LoggerLevel::LogError);
    auto chip = makeChip(ctx);

    // No EI here on purpose - interrupts start disabled after reset (real Z80
    // semantics), so interruptsAccepted must stay 0 for this program.
    const uint8_t program[] = {
        0x3E, 0x3F,       // LD A,0x3F
        0xD3, 0x06,       // OUT (#06),A   - volume latch write
        0x3E, 0xFF,       // LD A,0xFF
        0x32, 0x00, 0x60, // LD (0x6000),A - seed RAM (window 1 is fixed RAM)
        0x3A, 0x00, 0x60, // LD A,(0x6000) - DAC fetch
        0x76              // HALT
    };
    chip->loadROM(writeRom("gs-trace-counters.rom", program, sizeof(program)));

    EXPECT_EQ(chip->getActivityCounters().cpuSteps, 0u) << "no cycles executed before the first frame";

    chip->handleFrameStart();
    chip->handleFrameEnd(SAMPLES_PER_FRAME);

    const auto& counters = chip->getActivityCounters();
    EXPECT_GT(counters.cpuSteps, 0u) << "the GS Z80 core must have executed instructions";
    EXPECT_EQ(counters.interruptsAccepted, 0u) << "IFF1 is never enabled by this program (no EI)";
    EXPECT_EQ(counters.volumeLatchWrites, 1u);
    EXPECT_EQ(counters.dacFetches, 1u);
    EXPECT_NE(counters.lastDacFetchGsCycle, -1);

    printf("Counters: cpuSteps=%llu interrupts=%llu dacFetches=%llu volWrites=%llu\n",
           (unsigned long long)counters.cpuSteps, (unsigned long long)counters.interruptsAccepted,
           (unsigned long long)counters.dacFetches, (unsigned long long)counters.volumeLatchWrites);
}

TEST(SoundChip_GeneralSound_PortTrace, InterruptAcceptedAfterEI)
{
    // Isolates the interrupt-acceptance mechanism from real-firmware ambiguity:
    // EI, then spin NOPs long enough to cross a 320-cycle quantum boundary.
    EmulatorContext ctx(LoggerLevel::LogError);
    auto chip = makeChip(ctx);

    std::vector<uint8_t> program;
    program.push_back(0xFB); // EI
    for (int i = 0; i < 100; i++)
        program.push_back(0x00); // NOP (100*4 = 400 cycles > 320-cycle quantum)
    program.push_back(0x76);     // HALT

    chip->loadROM(writeRom("gs-trace-ei.rom", program.data(), program.size()));
    chip->handleFrameStart();
    chip->handleFrameEnd(SAMPLES_PER_FRAME);

    const auto& counters = chip->getActivityCounters();
    printf("InterruptAcceptedAfterEI: interrupts=%llu cpuSteps=%llu\n",
           (unsigned long long)counters.interruptsAccepted, (unsigned long long)counters.cpuSteps);
    EXPECT_GT(counters.interruptsAccepted, 0u) << "z80ex_int() acceptance mechanism itself must work when IFF1=1";
}

TEST(SoundChip_GeneralSound_PortTrace, RecordsEventsOnlyWhileCapturing)
{
    EmulatorContext ctx(LoggerLevel::LogError);
    auto chip = makeChip(ctx);

    const uint8_t program[] = {0x3E, 0x3F, 0xD3, 0x06, 0x76};
    chip->loadROM(writeRom("gs-trace-gate.rom", program, sizeof(program)));

    // Not capturing yet: frame runs, no events recorded
    chip->handleFrameStart();
    chip->handleFrameEnd(SAMPLES_PER_FRAME);
    EXPECT_EQ(chip->getPortTraceEventCount(), 0u);
    EXPECT_GT(chip->getActivityCounters().cpuSteps, 0u) << "counters are always-on regardless of trace capture";

    chip->startPortTrace();
    EXPECT_TRUE(chip->isPortTraceCapturing());
    chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_COMMAND, 0x2A);
    chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_DATA, 0x11);

    const auto events = chip->getPortTraceEvents();
    ASSERT_GE(events.size(), 2u);
    bool sawCommand = false, sawData = false;
    for (const auto& e : events)
    {
        if (e.side == GSTraceSide::Host && e.port == SoundChip_GeneralSound::PORT_COMMAND && e.value == 0x2A && e.isOut())
            sawCommand = true;
        if (e.side == GSTraceSide::Host && e.port == SoundChip_GeneralSound::PORT_DATA && e.value == 0x11 && e.isOut())
            sawData = true;
    }
    EXPECT_TRUE(sawCommand);
    EXPECT_TRUE(sawData);

    chip->stopPortTrace();
    chip->portDeviceOutMethod(SoundChip_GeneralSound::PORT_COMMAND, 0x99);
    EXPECT_EQ(chip->getPortTraceEventCount(), events.size()) << "no new events once stopped";
}

TEST(SoundChip_GeneralSound_PortTrace, RealRomProducesDacActivity)
{
    EmulatorContext ctx(LoggerLevel::LogError);
    auto chip = makeChip(ctx);
    chip->loadROM("rom/gs105a.rom");
    ASSERT_TRUE(chip->isROMLoaded());

    chip->startPortTrace();

    for (int frame = 0; frame < 50; frame++)
    {
        chip->handleFrameStart();
        chip->handleFrameEnd(SAMPLES_PER_FRAME);
    }

    const auto& counters = chip->getActivityCounters();
    printf("RealRom(idle,50 frames): cpuSteps=%llu interrupts=%llu dacFetches=%llu volWrites=%llu PC=0x%04x halted=%d\n",
           (unsigned long long)counters.cpuSteps, (unsigned long long)counters.interruptsAccepted,
           (unsigned long long)counters.dacFetches, (unsigned long long)counters.volumeLatchWrites,
           chip->getCPUReg(regPC), (int)chip->isCPUHalted());

    EXPECT_GT(counters.cpuSteps, 0u) << "real firmware must execute";
    EXPECT_FALSE(chip->isCPUHalted()) << "idle firmware polls the mailbox, it does not HALT";
    // NOTE: interruptsAccepted is intentionally NOT asserted here - whether
    // idle/unloaded firmware runs with interrupts enabled is exactly the open
    // question this diagnostic exists to answer empirically (see printf above).
}
