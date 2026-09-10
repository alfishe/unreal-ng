#include "stdafx.h"
#include "pch.h"

#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/io/keyboard/keyboard.h"

#include <cstdio>
#include <map>
#include <string>

/// TEMPORARY diagnostic probe - not part of the suite, removed after analysis.
/// Reproduces the "monitor screen renders, then keyboard dead / hang" report:
/// enters the service monitor via the magic NMI in two scenarios (idle 128 menu
/// vs. after the menu timeout when the firmware sits in plane 1), then samples
/// where the monitor loops, whether interrupts fire, and whether a sustained
/// key hold (the page-6 scanner needs >= ~25 frames, profrom-nmi-gaps §3)
/// advances the key ring at #E116 / moves the highlight
static void ProbeMonitorAfterNmi(const char* scenario, int bootFrames)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    std::shared_ptr<Emulator> emulator = manager->CreateEmulatorWithModel("", "PROFSCORP", LoggerLevel::LogError);
    ASSERT_TRUE(emulator) << scenario;

    EmulatorContext* context = emulator->GetContext();
    Memory* memory = context->pMemory;
    EmulatorState& state = context->emulatorState;
    Z80* z80 = context->pCore->GetZ80();

    emulator->RunNFrames(bootFrames);

    printf("=== %s: press NMI at frame %d (pre-PC=%04X plane=%u) ===\n", scenario, bootFrames, z80->pc, state.profrom_bank);
    emulator->RequestMNI();

    // Trajectory: watch 600 frames; does execution ever leave the paged-op loop?
    for (int f = 0; f <= 600; f += 25)
    {
        printf("f%3d: PC=%04X SP=%04X IY=%04X DD6B=%02X%02X E00D=%02X E02D=%02X EAF5=%02X%02X%02X%02X p1FFD=%02X p7FFD=%02X\n", f,
               z80->pc, z80->sp, z80->iy,
               memory->DirectReadFromZ80Memory(0xDD6C), memory->DirectReadFromZ80Memory(0xDD6B),
               memory->DirectReadFromZ80Memory(0xE00D), memory->DirectReadFromZ80Memory(0xE02D),
               memory->DirectReadFromZ80Memory(0xEAF5), memory->DirectReadFromZ80Memory(0xEAF6),
               memory->DirectReadFromZ80Memory(0xEAF7), memory->DirectReadFromZ80Memory(0xEAF8),
               state.p1FFD, state.p7FFD);
        if (f < 600)
            emulator->RunNFrames(25);
    }

    // Fine footprint at the stuck point: watch whether the bank latches cycle
    // (progress through banks) or sit on one value (truly stuck)
    {
        std::map<std::string, int> hist;
        int distinctBanks = 0;
        uint8_t lastBank = 0xFF;
        std::string bankSeq;
        for (int i = 0; i < 3000; i++)
        {
            emulator->RunTStates(279);
            if (state.p1FFD != lastBank)
            {
                if (bankSeq.size() < 120)
                {
                    char b[8];
                    snprintf(b, sizeof b, "%02X ", state.p1FFD);
                    bankSeq += b;
                }
                distinctBanks++;
                lastBank = state.p1FFD;
            }
            if (i < 300)
            {
                char key[96];
                snprintf(key, sizeof key, "PC=%04X HL=%04X DE=%04X BC=%04X p1FFD=%02X iff=%u",
                         z80->pc, z80->hl, z80->de, z80->bc, state.p1FFD, z80->iff1);
                hist[key]++;
            }
        }
        for (auto& kv : hist)
            printf("  stuck-sample x%-4d %s\n", kv.second, kv.first.c_str());
        printf("  #1FFD bank switches in window: %d, seq: %s\n", distinctBanks, bankSeq.c_str());
        // dump the RAM-resident paged-copy engine for offline disassembly
        std::string out = "/Users/dev/Projects/Local GitLab/unreal/scratch/engine-";
        for (const char* p = scenario; *p; p++)
            if (isalnum(static_cast<unsigned char>(*p)))
                out += *p;
        out += ".bin";
        FILE* f = fopen(out.c_str(), "wb");
        if (f)
        {
            for (uint32_t a = 0xE2E0; a < 0xE4E0; a++)
                fputc(memory->DirectReadFromZ80Memory(a), f);
            fclose(f);
            printf("  dumped %s\n", out.c_str());
        }
    }

    // Key ring snapshot, then hold Down (Caps+6) for 40 frames
    auto dumpRing = [&](const char* tag) {
        printf("%s: E116=%02X%02X E118=%02X%02X E11A=%02X E02D=%02X E051=%02X\n", tag,
               memory->DirectReadFromZ80Memory(0xE117), memory->DirectReadFromZ80Memory(0xE116),
               memory->DirectReadFromZ80Memory(0xE119), memory->DirectReadFromZ80Memory(0xE118),
               memory->DirectReadFromZ80Memory(0xE11A), memory->DirectReadFromZ80Memory(0xE02D),
               memory->DirectReadFromZ80Memory(0xE051));
    };
    dumpRing("before hold");

    Keyboard* keyboard = context->pKeyboard;
    keyboard->PressKey(ZXKEY_CAPS_SHIFT);
    keyboard->PressKey(ZXKEY_6);  // Down
    emulator->RunNFrames(40);
    dumpRing("after 40f hold");
    printf("after hold: PC=%04X\n", z80->pc);
    keyboard->ReleaseKey(ZXKEY_6);
    keyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);

    manager->RemoveEmulator(emulator->GetId());
}

TEST(ScorpionTurboProbe_Test, MonitorKeyboardAfterNmi)
{
    ProbeMonitorAfterNmi("A: idle 128 menu", 300);
    ProbeMonitorAfterNmi("B: after menu timeout (plane 1)", 700);
}
