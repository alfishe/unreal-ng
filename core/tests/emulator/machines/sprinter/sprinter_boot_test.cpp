// Sprinter Sp2000 BIOS 3.04 cold start on the real data/rom/sprinter/sp2k-3.04.rom
// (Sprinter roadmap S1 acceptance ACC-1a, test-plan R-1).
//
// The machine is created through EmulatorManager with the shipped
// configs/sprinter/unreal.ini, switched to the fast start and reset: the PLD
// starts configured, the BIOS POST writes the port table into RAM page #40 and
// opens the decoder ("DCP opened": the first IN, page 8 #0258), SETUP draws its
// text screen and, with no boot device, stops at its prompt.
//
// The text screen lives in the video RAM mode table: a text square's Mode1 byte
// is the character code (tdd-video §3), so the screen text is read back from
// VRAM without a renderer (phase S2).

#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/memory/memory.h>
#include <emulator/ports/models/portdecoder_sprinter.h>
#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "pch.h"
#include "stdafx.h"
#include "sprinterfixture.h"

class SprinterBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;
    PortDecoder_Sprinter* _decoder = nullptr;

    void SetUp() override
    {
        if (SprinterFixture::Rom304Available() == false)
            GTEST_SKIP() << "data/rom/sprinter/sp2k-3.04.rom not found";

        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        for (const auto& id : _manager->GetEmulatorIds())
            _manager->RemoveEmulator(id);

        _emulator = _manager->CreateEmulatorWithModelAndRAM("sprinter-boot", "SPRINTER", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _decoder = dynamic_cast<PortDecoder_Sprinter*>(_context->pPortDecoder);
        ASSERT_NE(_decoder, nullptr);
        _decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

        // The test default: skip the loader (FastStart=1); the full start is covered by
        // SprinterPldConfig_Test.FastStartEqualsFullStart_Bios304
        _context->config.sprinter.fast_start = 1;
        _emulator->Reset();
        // No assertion looks at pixels
        _emulator->EnableTurboMode();
    }

    void TearDown() override
    {
        _emulator.reset();
        if (_manager)
        {
            for (const auto& id : _manager->GetEmulatorIds())
                _manager->RemoveEmulator(id);
        }
    }

    /// The text of mode-table row b, both column sets (40 squares; each 640-mode
    /// square holds two characters, Line1 then Line2), in both mode pages
    std::string ScreenText()
    {
        const SprinterVideoRam& vram = _decoder->GetVideoRam();
        std::string text;
        for (uint8_t page = 0; page < 2; page++)
        {
            for (uint8_t b = 0; b < 32; b++)
            {
                for (uint8_t a = 0; a < 40; a++)
                {
                    for (uint8_t half = 0; half < 2; half++)
                    {
                        const uint32_t line = (1u + 2u * a + half + 0x80u * page) * 1024u;
                        const uint8_t c = vram.Read(line + 0x301 + 4u * b);
                        text.push_back((c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ');
                    }
                }
                text.push_back('\n');
            }
        }
        return text;
    }

    bool ScreenHas(const std::string& needle) { return ScreenText().find(needle) != std::string::npos; }
};

// ACC-1a: BIOS 3.04 reaches the boot menu.
// Boot-bound (BIOS POST, SETUP depacking, IDE detection): seconds of emulated time, the turbo mode on
TEST_F(SprinterBoot_Test, Bios304_ReachesTheBootMenu)
{
    // POST: the port table, then the first IN opens the decoder at page 8 #0258
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return _decoder->DcpOpenedFrame() >= 0; }, 50, 1);
    ASSERT_GE(_decoder->DcpOpenedFrame(), 0) << "the BIOS never opened the port decoder";
    EXPECT_EQ(_decoder->DcpOpenedPc(), 0x0CD8) << "DcpInit's IN A,(#E2) after the table";

    // The table the BIOS wrote equals the statically extracted 3.04 table (CRC b7f09600)
    const std::vector<uint8_t> rom = SprinterFixture::Rom304();
    const std::vector<uint8_t> expected = SprinterFixture::Table304(rom);
    const uint8_t* table = _context->pMemory->RAMPageAddress(0x40);
    size_t differences = 0;
    std::string report;
    for (size_t i = 0; i < expected.size(); i++)
    {
        if (table[i] == expected[i])
            continue;
        if (differences++ < 16)
            report += StringHelper::Format(" [%04X] %02X!=%02X", static_cast<unsigned>(i), table[i], expected[i]);
    }
    EXPECT_EQ(differences, 0u) << "page #40 right after DcpInit:" << report;

    // SETUP: the boot screen (BIOS id, memory, the CMOS clock; a blank CMOS loads the defaults)
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas("Memory    : 4096K"); }, 300, 5);
    EXPECT_TRUE(ScreenHas("Sprinter BIOS: ver 3.04")) << ScreenText();
    EXPECT_TRUE(ScreenHas("Memory    : 4096K")) << ScreenText();

    // IDE auto-detect: no drive answers (the IDE adapter comes in S3b), so each unit waits ~31 s
    // for BSY to drop. A user presses F4, as the screen says: the AT scan code arrives on
    // the Z84C15 SIO channel A, which SETUP's interrupt handler polls (set 2: F4 = #0C)
    for (const char* unit : {"Primary Master   ... [Press F4", "Primary Slave    ... [Press F4"})
    {
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas(unit); }, 300, 5);
        ASSERT_TRUE(ScreenHas(unit)) << ScreenText();
        for (uint8_t code : {0x0C, 0xF0, 0x0C})
            _decoder->GetZ84().sio.Receive(0, code);
        EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return !ScreenHas(unit); }, 300, 1);
    }

    // No boot device: the floppy and the hard disk fail, the BIOS offers ENTER / ESC
    const char* prompt = "PRESS <ENTER> TO REBOOT, <ESC> TO CANCEL";
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return ScreenHas(prompt); }, 1000, 5);
    EXPECT_TRUE(ScreenHas(prompt))
        << StringHelper::Format("PC=%04X frame=%llu\n", _context->pCore->GetZ80()->pc,
                                static_cast<unsigned long long>(_context->emulatorState.frame_counter))
        << ScreenText();
    EXPECT_TRUE(ScreenHas("fail")) << ScreenText();

    // Page #40 after POST and SETUP: the static table except what the BIOS changes at run time
    std::string after;
    size_t changed = 0;
    for (size_t i = 0; i < expected.size(); i++)
    {
        if (table[i] != expected[i] && changed++ < 32)
            after += StringHelper::Format(" [%04X] %02X!=%02X", static_cast<unsigned>(i), table[i], expected[i]);
    }
    EXPECT_EQ(changed, 0u) << "page #40 at the boot prompt:" << after;
}
