#ifdef UNREALNG_HAVE_OPL4

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/sound/soundmanager.h"

/// Guest-level MoonSound integration tests.
///
/// The device tests (moonsound_device_test.cpp) drive the card from the host
/// through Z80::out()/in(). These tests go one level up: real Z80 machine
/// code - the MoonService v0.3a detection protocol of the card author - runs
/// through the production stack end to end (config load via the emulator
/// manager, SoundManager construction, port decoder, instruction-level port
/// I/O, frame loop). The NEW-gated #7F read claim must survive the author's
/// exact arming sequence executed by the CPU core, not just a host-side call.
///
/// Protocol ground truth: docs/inprogress/2026-09-13-moonsound/
/// opl4-unreal-ng-integration.md sections 2.4 and 12.1.
class MoonServiceGuest_Test : public ::testing::Test
{
protected:
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;

    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
        ASSERT_NE(emulator, nullptr);
        context = emulator->GetContext();
    }

    void TearDown() override
    {
        if (emulator)
        {
            EmulatorTestHelper::CleanupEmulator(emulator);
            emulator = nullptr;
        }
        MessageCenter::DisposeDefaultMessageCenter();
    }

    /// Write guest code into RAM through the current page mapping.
    void LoadGuestCode(uint16_t address, const uint8_t* code, size_t size)
    {
        Memory* memory = context->pMemory;
        ASSERT_NE(memory, nullptr);
        for (size_t i = 0; i < size; i++)
        {
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(address + i), code[i]);
        }
    }

    uint8_t Peek(uint16_t address)
    {
        return context->pMemory->DirectReadFromZ80Memory(address);
    }

    MainLoop_CUT* MainLoop()
    {
        return reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);
    }

    Z80* Cpu()
    {
        return context->pCore->GetZ80();
    }
};

/// The author's init_card sequence as guest code (org $7000, results page at
/// $7F00): FM status presence probe, FM2 register 5 NEW2|NEW arming, FM1
/// register $BD reset, wave register 2 written $10 and read back. The read
/// must return the device-ID bits ($20 = YM278B) with the written low bits
/// intact - through the real instruction-level port funnel.
TEST_F(MoonServiceGuest_Test, GuestCode_RunsAuthorDetectionProtocolAndDetectsYm278b)
{
    // Precondition: the staged-config chain constructed the card (the emulator
    // manager path the WebAPI frontend uses as well)
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    ASSERT_TRUE(soundManager->hasMoonSound());

    static constexpr uint16_t kCodeBase = 0x7000;  // RAM bank 5: stable across paging
    static constexpr uint16_t kResStatus = 0x7F00; // IN #C4 raw
    static constexpr uint16_t kResDevId = 0x7F01;  // IN #7F raw
    static constexpr uint16_t kResDone = 0x7F02;   // completion marker

    static constexpr uint8_t kDetectionStub[] = {
        0xF3,                   // di
        0xDB, 0xC4,             // in a,(#C4)          ; FM status presence probe
        0x32, 0x00, 0x7F,       // ld (kResStatus),a
        0x3E, 0x04,             // ld a,4
        0xD3, 0xC6,             // out (#C6),a         ; FM2 bank 1
        0xAF,                   // xor a
        0xD3, 0xC7,             // out (#C7),a         ; reg 04 <- 00
        0x3E, 0x05,             // ld a,5
        0xD3, 0xC6,             // out (#C6),a
        0x3E, 0x03,             // ld a,3
        0xD3, 0xC7,             // out (#C7),a         ; reg 05 <- NEW2|NEW (arm)
        0x3E, 0xBD,             // ld a,#BD
        0xD3, 0xC4,             // out (#C4),a         ; FM1
        0xAF,                   // xor a
        0xD3, 0xC5,             // out (#C5),a         ; reg BD <- 00
        0x3E, 0x02,             // ld a,2
        0xD3, 0x7E,             // out (#7E),a         ; wave reg select
        0x3E, 0x10,             // ld a,#10
        0xD3, 0x7F,             // out (#7F),a         ; reg 2 <- #10
        0xDB, 0x7F,             // in a,(#7F)          ; read back: claimed by the card
        0x32, 0x01, 0x7F,       // ld (kResDevId),a
        0x3E, 0x5A,             // ld a,#5A
        0x32, 0x02, 0x7F,       // ld (kResDone),a
        0x18, 0xFE              // jr $
    };

    for (uint16_t address = kResStatus; address <= kResDone; address++)
    {
        context->pMemory->DirectWriteToZ80Memory(address, 0);
    }
    LoadGuestCode(kCodeBase, kDetectionStub, sizeof kDetectionStub);

    Cpu()->pc = kCodeBase;
    for (int frame = 0; frame < 2; frame++)
    {
        MainLoop()->RunFrame();
    }

    ASSERT_EQ(Peek(kResDone), 0x5A) << "guest stub did not run to completion";
    EXPECT_NE(Peek(kResStatus), 0xFF) << "FM status port must not read as floating bus";
    EXPECT_EQ(Peek(kResDevId) & 0xE0, 0x20) << "device-ID field must report YM278B";
    EXPECT_EQ(Peek(kResDevId), 0x30) << "register 2 keeps the written bits with the ID bits forced";
}

/// The author's actual binary, end to end: MoonService.bin (10451 bytes,
/// $6000-$88D3, see testdata/sound/moonsound/SOURCES.md) is placed into RAM
/// and executed from its entry point. Its init_card runs against the card and
/// parks the detected device ID at $88D0 - $20 means YM278B detected through
/// the author's own code. The service then continues into its flash/SRAM
/// diagnostics and menu; only the detection result and "still executing the
/// service image" are asserted here (the rest needs the disk/UI harness).
TEST_F(MoonServiceGuest_Test, AuthorBinary_MoonServiceV03a_DetectsYm278b)
{
    SoundManager* soundManager = context->pSoundManager;
    ASSERT_NE(soundManager, nullptr);
    ASSERT_TRUE(soundManager->hasMoonSound());

    // Boot-bound: a real program runs to its first milestone. Turbo keeps the
    // wall time low; nothing here asserts on audio or rendered pixels.
    emulator->EnableTurboMode();

    const std::string path = TestPathHelper::GetTestDataPath("sound/moonsound/moonservice_v03a_service.bin");
    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << "missing fixture: " << path;

    file.seekg(0, std::ios::end);
    const std::streampos size = file.tellg();
    file.seekg(0, std::ios::beg);
    ASSERT_EQ(static_cast<size_t>(size), 10451u) << "unexpected fixture size (see SOURCES.md)";

    std::vector<uint8_t> image(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(image.data()), size);
    ASSERT_EQ(file.gcount(), static_cast<std::streamsize>(size));

    // Deterministic paging for the image tail: the service expects RAM page 0
    // in the #8000 window (its entry re-asserts #7FFD = #10, which keeps it).
    Cpu()->out(0x7FFD, 0x00);

    // Zero the dev_id cell so the completion poll below cannot trip on
    // power-on garbage already sitting at $88D0.
    for (uint16_t address = 0x88D0; address <= 0x88D3; address++)
    {
        context->pMemory->DirectWriteToZ80Memory(address, 0);
    }

    LoadGuestCode(0x6000, image.data(), image.size());

    Cpu()->pc = 0x6000;

    // init_card completes well inside a single frame; the cap is generous.
    // Stop as soon as the service has written its detection verdict.
    constexpr int kMaxFrames = 30;
    for (int frame = 0; frame < kMaxFrames && Peek(0x88D0) == 0; frame++)
    {
        MainLoop()->RunFrame();
    }

    EXPECT_EQ(Peek(0x88D0), 0x20) << "MoonService_dev_id at #88D0: #20 = YM278B";

    const uint16_t pc = Cpu()->pc;
    EXPECT_GE(pc, 0x6000) << "PC left the service image";
    EXPECT_LE(pc, 0x88D3) << "PC left the service image";
}

#endif // UNREALNG_HAVE_OPL4
