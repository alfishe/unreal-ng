#include "stdafx.h"
#include "pch.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

/// @brief Real-ROM check of the service monitor's turbo detection
///        (profrom-service-monitor-turbo.md): the monitor strobes IN (#7FFD)
///        at entry and immediately times an INT-bounded count loop; only with
///        the strobe applied mid-frame (Z80::ApplyHardwareTurboNow) can the
///        loop complete at 7 MHz and set the "turbo hardware present +
///        enabled" flags (ProfROM v4.01: #E02D bits 7|6; base v2.9x monitor:
///        #DFF8 = #C0, entry #0211-#0225 with the same #0553 count loop) - the
///        "Computer speed" menu item is refused otherwise.
///        The monitor-entry test is not INT-synced in the firmware, so its
///        result depends on the frame phase of the button press: the test
///        presses at several phases and requires at least one success, and
///        reports the per-phase results in the failure message.
/// @param flagAddr  monitor RAM byte the firmware writes #C0 into on success:
///                  ProfROM v4.01 = #E02D (bits 7|6), base v2.9x = #DFF8
static void RunDetectionProbe(const char* model, uint16_t flagAddr, int& successes, int& attempts, std::string& report)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", model, LoggerLevel::LogError);
    ASSERT_TRUE(emulator) << model << " could not be created";

    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;
    EmulatorState& state = context->emulatorState;

    // Deterministic RTC: the SMUC stub serves live host time by default, and
    // the 128-menu clock then shifts boot-timeline events (config staging,
    // monitor RAM state) with the wall clock - run to run the machine could
    // idle at a different clock speed at the same frame count. Freeze the
    // clock so every phase presses the same booted machine and the sub-frame
    // press offset below stays the only free variable (turbo-flake analysis,
    // 2026-09-10). No-op while the SMUC board is absent by default
    // (PortDecoder_Scorpion256::SetSmucEnabled) and for the base model:
    // nothing ever reads the RTC
    if (PortDecoder_Scorpion256* decoder = static_cast<PortDecoder_Scorpion256*>(context->pPortDecoder))
        decoder->GetSMUCNvram().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

    // Host-side turbo (audio muted + low-quality DSP). Independent of the
    // emulated 7 MHz multiplier under test - config.turbo_mode never reaches
    // current_z80_frequency_multiplier - it only removes per-frame audio work.
    emulator->EnableTurboMode();

    emulator->RunNFrames(300);  // boot to the 128 menu (~6 s)

    for (unsigned phase = 0; phase < 8; phase++)
    {
        // Press at a different point inside the frame each time
        emulator->RunNFrames(3);
        emulator->RunTStates(phase * 8000);

        emulator->RequestMNI();
        emulator->RunNFrames(30);  // entry chain + detection loop + menu draw

        uint8_t flag = memory->DirectReadFromZ80Memory(flagAddr);
        bool detected = (flag & 0xC0) == 0xC0;
        char line[160];
        snprintf(line, sizeof line, "  %s phase %u: #%04X=%02X mult=%u -> %s\n", model, phase, flagAddr, flag,
                 state.current_z80_frequency_multiplier, detected ? "DETECTED" : "no");
        report += line;
        attempts++;
        if (detected)
        {
            successes++;

            // The caller asserts EXPECT_GT(successes, 0): the phases exist to
            // make the probe robust against sub-frame press timing, not to
            // measure a rate. Once one phase has detected, the remaining ones
            // can only cost time - each carries a cold reset plus a 300-frame
            // reboot, ~89% of this test's runtime. A failing run still walks
            // all eight and reports every phase.
            break;
        }

        // Leave the monitor for the next attempt: cold reset back to the menu
        emulator->Reset();
        emulator->RunNFrames(300);
    }

    manager->RemoveEmulator(emulator->GetId());
}

/// Boot-time half of the turbo-detection guard: the boot itself runs the
/// stub/#025E first-entry init (OFF strobe #04D9, ON strobe #04D5, then the
/// un-synced #2C1F count loop) and ORs #C0 into #E02D only if the loop
/// completes at 7 MHz - the exact path a Z80::ApplyHardwareTurboNow
/// regression breaks. The monitor-entry test below cannot catch that: its
/// presses happen with #E02D already C0, and the NMI chain then only re-runs
/// the HALT-synced #2C30 re-check, never the strobes. With the RTC frozen
/// the write lands at frame 45 on every boot (verified 5/5 probe runs,
/// with and without the SMUC board); checking at 60 stays clear of the
/// later config staging (~frame 125 board-absent, ~325 board-present)
TEST(ScorpionTurboDetect_Test, ProfRomBootDetectsSevenMhz)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", "PROFSCORP", LoggerLevel::LogError);
    ASSERT_TRUE(emulator) << "PROFSCORP could not be created";

    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;

    // Same frozen RTC instant as RunDetectionProbe: deterministic boot timeline
    if (PortDecoder_Scorpion256* decoder = static_cast<PortDecoder_Scorpion256*>(context->pPortDecoder))
        decoder->GetSMUCNvram().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

    // Step one frame at a time: RunNFrames(n) derives its t-state budget from
    // the multiplier at entry, and the boot itself flips turbo ON at ~frame 11,
    // so a single 60-frame call only spans ~35 video frames and would stop
    // before the write. Per-frame calls recompute the frame limit each time
    // and land on true video frame 60 (write at 45, staging not until ~321)
    emulator->EnableTurboMode();  // host-side only; see RunDetectionProbe
    for (int frame = 0; frame < 60; frame++)
        emulator->RunNFrames(1);

    const uint8_t flag = memory->DirectReadFromZ80Memory(0xE02D);
    EXPECT_EQ(flag & 0xC0, 0xC0) << "boot-time stub/#025E detection never set #E02D bits 7|6 "
                                    "(strobe/mid-frame clock-switch path broken)";

    manager->RemoveEmulator(emulator->GetId());
}

TEST(ScorpionTurboDetect_Test, ProfRomMonitorDetectsSevenMhz)
{
    int ok = 0, n = 0; std::string report;
    RunDetectionProbe("PROFSCORP", 0xE02D, ok, n, report);
    std::cout << report;
    EXPECT_GT(ok, 0) << "monitor never detected the turbo clock:\n" << report;
}

TEST(ScorpionTurboDetect_Test, BaseMonitorDetectsSevenMhz)
{
    int ok = 0, n = 0; std::string report;
    RunDetectionProbe("SCORPION", 0xDFF8, ok, n, report);
    std::cout << report;
    EXPECT_GT(ok, 0) << "monitor never detected the turbo clock:\n" << report;
}
