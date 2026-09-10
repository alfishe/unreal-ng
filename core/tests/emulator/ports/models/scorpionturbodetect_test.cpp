#include "stdafx.h"
#include "pch.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"

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
            successes++;

        // Leave the monitor for the next attempt: cold reset back to the menu
        emulator->Reset();
        emulator->RunNFrames(300);
    }

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
