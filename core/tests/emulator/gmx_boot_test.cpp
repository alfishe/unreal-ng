// Scorpion GMX with the shipped gmx644.rom: the shadow monitor's test screen in 640x200 and Edge Grinder from its TRD
// (GMX design section 7 phase 3; research-2026-10-30-software.md 5.1 fixtures 1 and 2).

#include "stdafx.h"
#include "pch.h"

#include <algorithm>
#include <cstring>

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <gtest/gtest.h>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtier.h"
#include "emulator/media/mediamanager.h"
#include "emulator/ports/models/portdecoder_scorpiongmx.h"
#include "emulator/video/gmx/gmxgeometry.h"
#include "emulator/video/screen.h"

class GmxBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        _emulator = _manager->CreateEmulatorWithModelAndRAM("gmx-boot", "GMX", 2048, LoggerLevel::LogError);
        if (!_emulator)
            GTEST_SKIP() << "GMX is not creatable (missing configs/gmx or data/rom/gmx644.rom)";
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            const std::string id = _emulator->GetId();
            _emulator.reset();
            _manager->RemoveEmulator(id);
        }
    }

    uint8_t P7EFD() const { return _context->emulatorState.scorpion.p7EFD; }

    /// Bytes of the shown 640x200 pixel page (#39 / #3B by #7FFD bit 3) that are neither 0 nor #FF
    uint32_t DrawnPixelBytes() const
    {
        const uint8_t* page = _context->pMemory->RAMPageAddress(GmxGeometry::PixelPage(_context->emulatorState.p7FFD));
        uint32_t count = 0;
        for (uint32_t i = 0; i < GmxGeometry::kScreenBytes; ++i)
            count += (page[i] != 0x00 && page[i] != 0xFF) ? 1 : 0;
        return count;
    }
};

/// @brief After the loader and the 128 menu hand-over the v6.44 firmware runs its fast test in the shadow monitor
///        (plane 5) with the 640x200 mode on: the screen switches to the 736x288 frame and the text is drawn
TEST_F(GmxBoot_Test, ShadowMonitorTestScreenIsDrawnIn640x200)
{
    _emulator->EnableTurboMode();
    const int frames = EmulatorTestHelper::RunUntil(
        _emulator.get(), [&] { return (P7EFD() & 0x78) == 0x58 && DrawnPixelBytes() > 200; }, 600, 10);

    EXPECT_LT(frames, 600) << "7EFD=" << std::hex << int(P7EFD());
    EXPECT_EQ(P7EFD() & 0x70, 0x50) << "plane 5: the shadow service monitor";
    ASSERT_EQ(_context->pScreen->GetVideoMode(), M_GMX);
    EXPECT_EQ(_context->pScreen->GetFramebufferDescriptor().width, 736u);
    EXPECT_EQ(_context->pScreen->GetFramebufferDescriptor().height, 288u);
    EXPECT_GT(DrawnPixelBytes(), 200u) << "the monitor's frames and the fast test text";
}

/// @brief Edge Grinder (izzx, 2021): the monitor's IDE probe times out (no drive: the status reads #FF, as in
///        Unreal), the 128 menu's first entry boots TR-DOS, the game's loader and its 640x200 picture follow
///        (#7EFD = #C8). Long tier: about 6000 frames of emulated time
TEST_F(GmxBoot_Test, EdgeGrinderBootsIntoItsExtendedMode)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "UNREAL_LONG_TESTS=1: ~6000 frames through the IDE probe and the game's loader";

    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("machines/gmx/edgegrinder/EGGMX.TRD"), 0, &error))
        << error;
    _emulator->EnableTurboMode();

    const int toMenu = EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return (P7EFD() & 0x7F) == 0x40; }, 5000, 25);
    ASSERT_LT(toMenu, 5000) << "the 128 menu (plane 4; turbo as the NVRAM settings say) never came, 7EFD=" << std::hex << int(P7EFD());
    _emulator->RunNFrames(50, true);

    Keyboard* keyboard = _context->pKeyboard;
    keyboard->PressKey(ZXKEY_ENTER);   // "128 TR-DOS": the autoboot runs `boot`
    _emulator->RunNFrames(6, true);
    keyboard->ReleaseKey(ZXKEY_ENTER);

    // The controls screen waits for a key before the DISC loader: Space every 4 s of emulated time until the game
    // turns its 640x200 mode on
    int toGame = 0;
    while (toGame < 4000 && !(P7EFD() == 0xC8 && DrawnPixelBytes() > 1000))
    {
        keyboard->PressKey(ZXKEY_SPACE);
        _emulator->RunNFrames(6, true);
        keyboard->ReleaseKey(ZXKEY_SPACE);
        toGame += 6 + EmulatorTestHelper::RunUntil(
                          _emulator.get(), [&] { return P7EFD() == 0xC8 && DrawnPixelBytes() > 1000; }, 200, 25);
    }
    EXPECT_LT(toGame, 4000) << "7EFD=" << std::hex << int(P7EFD());
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_GMX);
    EXPECT_GT(DrawnPixelBytes(), 1000u) << "the game's picture in the pixel page";
}

/// @brief The Z-Controller SD is the media manager's slot `sd.zc` (alias `sd` on the planes): their media routes reach it.
///        A folder becomes a FAT32 volume behind an MBR, the layout of the web Unreal's osz.vhd (FAT32 at LBA 63)
TEST_F(GmxBoot_Test, SdCardIsAMediaSlotTakingTheOszFolder)
{
    MediaManager* media = _context->pMediaManager;
    ASSERT_NE(media, nullptr);
    const auto slot = media->Info("sd.zc");
    ASSERT_TRUE(slot.has_value());
    EXPECT_NE(std::find(slot->tags.begin(), slot->tags.end(), "zcontroller"), slot->tags.end());
    EXPECT_FALSE(slot->present);

    MediaSource source;
    source.path = TestPathHelper::GetTestDataPath("machines/gmx/osz/sdcard");
    InsertOptions options;
    options.immediate = true;
    const MediaResult inserted = media->Insert("sd.zc", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    for (const std::string& line : inserted.report)
        EXPECT_EQ(line.find("no MBR support"), std::string::npos) << "the monitor finds the FAT32 partition: " << line;
    _emulator->RunNFrames(1, true);  // the emulation thread attaches it at the frame boundary
    const auto info = media->Info("sd.zc");
    ASSERT_TRUE(info.has_value());
    EXPECT_TRUE(info->present);
    EXPECT_EQ(info->format, "folder-fat32") << "the 6.44 monitor reads FAT32 only (GMX Q20)";
}

/// @brief OSZ (OS ver 2025.10.08, research-2026-10-30-software.md 5.1 fixture 3): OS.TRD in A: loads the kernel's
///        loader, which finds the FAT32 partition on the Z-Controller SD through the monitor (RST 8), reads the system
///        and the None Commander from `sd.zc`, and runs them in 640x200. Long tier: the IDE probe and the SD reads
TEST_F(GmxBoot_Test, OszBootsFromTheSdCard)
{
    if (!TestTier::Long())
        GTEST_SKIP() << "UNREAL_LONG_TESTS=1: ~4000 frames through the IDE probe and the SD reads";

    MediaSource sd;
    sd.path = TestPathHelper::GetTestDataPath("machines/gmx/osz/sdcard");
    InsertOptions options;
    options.immediate = true;
    const MediaResult inserted = _context->pMediaManager->Insert("sd.zc", sd, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    std::string error;
    ASSERT_TRUE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("machines/gmx/osz/OS.TRD"), 0, &error)) << error;
    _emulator->EnableTurboMode();

    const int toMenu = EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return (P7EFD() & 0x7F) == 0x40; }, 5000, 25);
    ASSERT_LT(toMenu, 5000) << "the 128 menu never came, 7EFD=" << std::hex << int(P7EFD());
    _emulator->RunNFrames(50, true);

    Keyboard* keyboard = _context->pKeyboard;
    keyboard->PressKey(ZXKEY_ENTER);   // "128 TR-DOS": the autoboot runs `boot`
    _emulator->RunNFrames(6, true);
    keyboard->ReleaseKey(ZXKEY_ENTER);

    auto* gmx = dynamic_cast<PortDecoder_ScorpionGMX*>(_context->pPortDecoder);
    ASSERT_NE(gmx, nullptr);
    const SdCardSpi& card = gmx->GetSdCard();
    const int toOs = EmulatorTestHelper::RunUntil(
        _emulator.get(), [&] { return card.blocksRead() > 40 && P7EFD() == 0xC8 && DrawnPixelBytes() > 500; }, 2000,
        25);
    EXPECT_LT(toOs, 2000) << "7EFD=" << std::hex << int(P7EFD()) << std::dec << " blocks read " << card.blocksRead();
    EXPECT_TRUE(card.initialized()) << "the monitor's SD driver ran the card's init (CMD0, CMD8, ACMD41)";
    EXPECT_GT(card.blocksRead(), 40u) << "the system and the None Commander come from the SD card";
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_GMX);
}

namespace
{
/// Border stripes timed from an IM 2 interrupt: each frame XOR A, then 512 x (OUT (C),A; INC A; AND 7) = 36 T steps
/// across the top border, the paper black. Identical frames need the same INT, frame, raster and border latch
std::vector<uint8_t> RunBorderStripes(Emulator& emulator)
{
    EmulatorContext* context = emulator.GetContext();
    Memory* memory = context->pMemory;
    const uint8_t program[] = {
        0xF3,                    // 8000 DI
        0x31, 0xF0, 0x7F,        // 8001 LD SP,#7FF0
        0x3E, 0x81,              // 8004 LD A,#81
        0xED, 0x47,              // 8006 LD I,A
        0xED, 0x5E,              // 8008 IM 2
        0xFB,                    // 800A EI
        0x76,                    // 800B HALT
        0xAF,                    // 800C XOR A
        0x0E, 0xFE,              // 800D LD C,#FE
        0x16, 0x02,              // 800F LD D,2
        0x06, 0x00,              // 8011 LD B,0
        0xED, 0x79,              // 8013 OUT (C),A
        0x3C,                    // 8015 INC A
        0xE6, 0x07,              // 8016 AND 7
        0x10, 0xF9,              // 8018 DJNZ #8013
        0x15,                    // 801A DEC D
        0x20, 0xF4,              // 801B JR NZ,#8011
        0xC3, 0x0A, 0x80,        // 801D JP #800A
    };
    for (size_t i = 0; i < sizeof(program); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    for (uint16_t a = 0x8100; a <= 0x8200; a++)
        memory->DirectWriteToZ80Memory(a, 0x82);  // the IM 2 vector #8282
    memory->DirectWriteToZ80Memory(0x8282, 0xC9);  // RET: interrupts stay off until the EI at #800A
    for (uint16_t a = 0x4000; a < 0x5B00; a++)
        memory->DirectWriteToZ80Memory(a, 0x00);

    Z80* z80 = context->pCore->GetZ80();
    z80->iff1 = z80->iff2 = 0;
    z80->halted = 0;
    z80->pc = 0x8000;
    context->pScreen->SetPresentDelayFrames(0);
    emulator.RunNFrames(4, true);

    const auto& fb = context->pScreen->GetFramebufferDescriptor();
    std::vector<uint8_t> rgba(fb.memoryBufferSize);
    EXPECT_TRUE(context->pScreen->CopyPresentedFramebuffer(rgba.data(), rgba.size()));
    return rgba;
}
}  // namespace

/// @brief The loader's Set Up menu (Delete held at power-on) picks the FPGA scheme; "4. Pentagon" writes #00 = #E4 and
///        starts the Pentagon 128 of plane 1: no card ports, the 320-line frame, the Pentagon raster and border. The
///        manual promises "full compatibility with the Pentagon, border and multicolour effects": a border effect
///        timed from the INT draws the same frame as on the PENTAGON model, pixel for pixel
TEST_F(GmxBoot_Test, LoaderSchemeFourIsAPentagon128)
{
    Keyboard* keyboard = _context->pKeyboard;
    keyboard->PressKey(ZXKEY_CAPS_SHIFT);
    keyboard->PressKey(ZXKEY_0);
    _emulator->RunNFrames(100, true);
    keyboard->ReleaseKey(ZXKEY_0);
    keyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);
    _emulator->RunNFrames(10, true);

    keyboard->PressKey(ZXKEY_4);
    _emulator->RunNFrames(6, true);
    keyboard->ReleaseKey(ZXKEY_4);
    const int frames = EmulatorTestHelper::RunUntil(
        _emulator.get(), [&] { return _context->emulatorState.gmx.p00 == 0xE4 && (P7EFD() & 0x70) == 0x10; }, 300, 5);
    ASSERT_LT(frames, 300) << "#00=" << std::hex << int(_context->emulatorState.gmx.p00) << " 7EFD=" << int(P7EFD());

    EXPECT_EQ(_context->config.frame, 71680u);
    EXPECT_EQ(_context->pScreen->GetVideoMode(), M_PENTAGON128K);
    auto* gmx = dynamic_cast<PortDecoder_ScorpionGMX*>(_context->pPortDecoder);
    ASSERT_NE(gmx, nullptr);
    EXPECT_TRUE(gmx->IsPentagonDecode());

    const std::vector<uint8_t> onGmx = RunBorderStripes(*_emulator);
    std::shared_ptr<Emulator> pentagon =
        _manager->CreateEmulatorWithModelAndRAM("gmx-pentagon-ref", "PENTAGON", 128, LoggerLevel::LogError);
    ASSERT_NE(pentagon, nullptr);
    const std::vector<uint8_t> onPentagon = RunBorderStripes(*pentagon);
    const std::string pentagonId = pentagon->GetId();
    pentagon.reset();
    _manager->RemoveEmulator(pentagonId);

    ASSERT_EQ(onGmx.size(), onPentagon.size());
    size_t differing = 0;
    for (size_t i = 0; i < onGmx.size(); i += 4)
        differing += std::memcmp(&onGmx[i], &onPentagon[i], 4) != 0 ? 1 : 0;
    EXPECT_EQ(differing, 0u) << "pixels that differ from the Pentagon's frame";
    const uint32_t* first = reinterpret_cast<const uint32_t*>(onGmx.data());
    const size_t pixels = onGmx.size() / 4;
    EXPECT_GT(std::count_if(first, first + pixels, [&](uint32_t px) { return px != first[0]; }), 1000)
        << "the stripes are drawn";
}

/// @brief The SMUC peripherals the v6.44 monitor's fast test probes, at the ports it uses: the 8259 (page 23 #1F92), the
///        16550 of the ISA slot's modem at COM1 (#7AFE = ISA #3FA, #2021) and the ISA window's DOS-port gate
TEST_F(GmxBoot_Test, SmucPeripheralsAnswerTheFastTestProbes)
{
    PortDecoder* decoder = _context->pPortDecoder;
    EmulatorState& state = _context->emulatorState;

    // 8259: ICW1 #F3, ICW2 #C5, ICW4 #01, the IMR reads 0 or 1; ICW1 #F2, ICW2 #C5, OCW1 #FF, it reads #FF
    decoder->DecodePortOut(0x7EBE, 0xF3, 0x1FA2);
    decoder->DecodePortOut(0x7FBE, 0xC5, 0x1FA2);
    decoder->DecodePortOut(0x7FBE, 0x01, 0x1FA2);
    EXPECT_LE(decoder->DecodePortIn(0x7FBE, 0x1FB4), 1);
    decoder->DecodePortOut(0x7EBE, 0xF2, 0x1FC9);
    decoder->DecodePortOut(0x7FBE, 0xC5, 0x1FC9);
    decoder->DecodePortOut(0x7FBE, 0xFF, 0x1FC9);
    EXPECT_EQ(decoder->DecodePortIn(0x7FBE, 0x1FC9), 0xFF);

    // COM1 through the ISA window: only with the DOS ports on (the window shares its pattern with the keyboard rows)
    const unsigned flags = state.flags;
    state.flags |= CF_DOSPORTS;
    decoder->DecodePortOut(0x7FFE, 0x5A, 0x2024);
    EXPECT_NE(decoder->DecodePortIn(0x7FFE, 0x2024), 0x5A) << "#FFBA D2 / D1 = 0: ISA #27F, no card there";
    decoder->DecodePortOut(0xFFBA, 0xF7, 0x2024);   // the firmware's power-on value: D2 / D1 = 1, ISA #380-#3FF
    EXPECT_NE(decoder->DecodePortIn(0x7AFE, 0x2024), 0xFF) << "the 16550's IIR (#3FA)";
    decoder->DecodePortOut(0x7FFE, 0x5A, 0x2024);   // ISA #3FF: the scratch register
    EXPECT_EQ(decoder->DecodePortIn(0x7FFE, 0x2024), 0x5A);
    decoder->DecodePortOut(0x7BFE, 0x83, 0x2024);   // LCR with DLAB: reads back
    EXPECT_EQ(decoder->DecodePortIn(0x7BFE, 0x2024), 0x83);
    decoder->DecodePortOut(0x7BFE, 0x03, 0x2024);
    state.flags = flags & ~static_cast<unsigned>(CF_DOSPORTS | CF_TRDOS);
    if ((state.p1FFD & 0x02) == 0 && !state.scorpion.dosTrigger)
        EXPECT_NE(decoder->DecodePortIn(0x7FFE, 0x0000), 0x5A) << "outside DOS the row port is the keyboard";

    // The Shadow Monitor (#1FFD bit 1, no TR-DOS) scans the keyboard with #FFBA = #77: the latch does not open the
    // window, #7FFE is the Space - B row and not the modem's scratch register (the monitor's keys were dead)
    const uint8_t p1FFD = state.p1FFD;
    const bool dosTrigger = state.scorpion.dosTrigger;
    state.p1FFD = static_cast<uint8_t>(p1FFD | 0x02);
    state.scorpion.dosTrigger = false;
    decoder->DecodePortOut(0xFFBA, 0x77, 0x04D9);
    EXPECT_EQ(decoder->DecodePortIn(0x7FFE, 0x04BA) & 0x1F, 0x1F) << "no key held: the keyboard answers";
    _context->pKeyboard->PressKey(ZXKEY_SPACE);
    EXPECT_EQ(decoder->DecodePortIn(0x7FFE, 0x04BA) & 0x1F, 0x1E) << "Space reads through the latch";
    _context->pKeyboard->ReleaseKey(ZXKEY_SPACE);
    state.p1FFD = p1FFD;
    state.scorpion.dosTrigger = dosTrigger;
    state.flags = flags;
}

/// @brief A board without a saved NVRAM starts from the config's [SMUC] NvramInitial: the image the 6.44 monitor writes
///        when it initializes blank cells, so the fast test finds the signatures (CMOS cell #0E, EEPROM byte 0 = #65)
///        and valid checksums on the first power-on
TEST_F(GmxBoot_Test, SmucNvramStartsFromTheShippedImage)
{
    auto* gmx = dynamic_cast<PortDecoder_ScorpionGMX*>(_context->pPortDecoder);
    ASSERT_NE(gmx, nullptr);
    _emulator->RunNFrames(1, true);
    EXPECT_EQ(gmx->GetRtc().GetCell(0x0E), 0x65) << "CMOS signature";
    EXPECT_EQ(gmx->GetSMUCNvram().GetEEPROMByte(0x000), 0x65) << "NVRAM signature";
    EXPECT_EQ(gmx->GetSMUCNvram().GetEEPROMByte(0x0FE), 0xED) << "NVRAM checksum low";
    EXPECT_EQ(gmx->GetSMUCNvram().GetEEPROMByte(0x0FF), 0x97) << "NVRAM checksum high";
}
