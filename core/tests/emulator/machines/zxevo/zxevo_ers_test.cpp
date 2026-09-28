// ZX-Evo (ATM3) tests on the shipped default: the official BaseConf ROM image
// data/rom/zxevo-fe.rom (EVO Reset Service 0.60.05 FE, NEO-DOS in page 29) on
// the current "trdemu" BaseConf FPGA ([EVO] Fpga=trdemu).
//
// These boot a real ROM to a machine state, which is why each one takes longer
// than the usual 50 ms budget (turbo mode, stops as soon as the state is reached).
//
// Expected steady state (measured 2026-09-28): the ERS unpacks its main menu to
// #6000 and idles in a HALT loop at PC #6117 with window 0 on BASIC48 ROM page
// 28, about 60 frames after reset.

#include <base/featuremanager.h>
#include <emulator/cpu/core.h>
#include <emulator/cpu/z80.h>
#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <debugger/analyzers/rom-print/screenocr.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/ports/models/portdecoder_atm3.h>
#include <emulator/ports/portdiagrecorder.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "pch.h"
#include "stdafx.h"

class ZXEvoErs_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    /// Create the shipped ATM3 machine
    void Create()
    {
        _emulator = _manager->CreateEmulatorWithModelAndRAM("zxevo-ers", "ATM3", 4096, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();

        // The shipped config drives this whole file: fail loudly if it drifts
        ASSERT_EQ(std::string(_context->config.atm3_rom_path), "rom/zxevo-fe.rom");
        ASSERT_EQ(_context->config.atm.evo_legacy_fpga, 0);

        // Deterministic RTC (the ERS reads the clock while booting)
        if (auto* decoder = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder))
            decoder->GetCMOS().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

        _emulator->EnableTurboMode();
    }

    /// Capture every access to #13BD (the virtual-drive mask the ERS probes)
    PortDiagnosticRecorder* TraceFddMaskPort()
    {
        FeatureManager* features = _emulator->GetFeatureManager();
        EXPECT_TRUE(features->setFeature(Features::kPortTrace, true));
        PortDiagnosticRecorder* recorder = _context->pPortDecoder->getPortTraceRecorder();
        EXPECT_NE(recorder, nullptr);
        if (recorder)
        {
            PortTraceFilterRule rule;
            rule.rawPort = 0x13BD;
            recorder->addIncludeRule(rule);
            recorder->start();
        }
        return recorder;
    }

    /// Run until the ERS main menu idles (see file header)
    bool RunToMainMenu()
    {
        Memory* memory = _context->pMemory;
        Z80* z80 = _context->pCore->GetZ80();
        EmulatorTestHelper::RunUntil(
            _emulator.get(),
            [&] { return z80->pc == 0x6117 && memory->IsBank0ROM() && memory->GetROMPage() == 28u; },
            300);
        return z80->pc == 0x6117 && memory->IsBank0ROM() && memory->GetROMPage() == 28u;
    }

    /// The ERS FPGA suitability probe (rst8service.a80 VERSION_): IN (#13BD)
    /// to save, OUT (#13BD),%1010, IN (#13BD) and compare. Returns the value
    /// read back right after the %1010 write, or -1 when the probe never ran
    static int FddMaskProbeReadback(const std::vector<PortTraceEvent>& events)
    {
        for (size_t i = 0; i + 1 < events.size(); i++)
        {
            if (events[i].isOut() && events[i].value == 0x0A && !events[i + 1].isOut())
                return events[i + 1].value;
        }
        return -1;
    }
};

/// The shipped pair boots the EVO Reset Service to its main menu
TEST_F(ZXEvoErs_Test, BootsToMainMenu)
{
    Create();
    ASSERT_TRUE(RunToMainMenu()) << "ERS main menu idle loop not reached, pc=" << std::hex
                                 << _context->pCore->GetZ80()->pc;

    // Live idle loop: R keeps moving (HALT woken by INT)
    Z80& z80 = *_context->pCore->GetZ80();
    const uint8_t r1 = z80.r_low;
    _emulator->RunNFrames(1, true);
    const uint8_t r2 = z80.r_low;
    _emulator->RunNFrames(1, true);
    EXPECT_TRUE(r1 != r2 || r2 != z80.r_low);
    EXPECT_EQ(z80.pc, 0x6117);
}

/// ERS "Incorrect FPGA zxevo_fw.bin" check: on the current FPGA the %1010 written
/// to #13BD reads back, so the ERS accepts the machine
TEST_F(ZXEvoErs_Test, FpgaSuitabilityProbePassesOnTrdemu)
{
    Create();
    PortDiagnosticRecorder* recorder = TraceFddMaskPort();
    ASSERT_NE(recorder, nullptr);
    ASSERT_TRUE(RunToMainMenu());

    EXPECT_EQ(FddMaskProbeReadback(recorder->getAll()), 0x0A)
        << "the ERS must read back the %1010 it wrote to #13BD";
}

/// ERS header "Baseconf:" / "AVR Boot:" (mainmenu/src/call_cmos.a80 GET_VERS_EVO):
/// the ERS selects extension type 0, then 1, through the clock data port and
/// reads 16 bytes each. Before the EvoAvr model the cells echoed the type and
/// the ERS printed "NONE" for both
TEST_F(ZXEvoErs_Test, ReadsBaseConfAndBootloaderVersionsFromTheAvr)
{
    Create();
    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_TRUE(features->setFeature(Features::kPortTrace, true));
    PortDiagnosticRecorder* recorder = _context->pPortDecoder->getPortTraceRecorder();
    ASSERT_NE(recorder, nullptr);
    for (uint16_t port : {uint16_t(0xBEF7), uint16_t(0xBFF7)})
    {
        PortTraceFilterRule rule;
        rule.rawPort = port;
        rule.directionOut = false;
        recorder->addIncludeRule(rule);
    }
    recorder->start();
    ASSERT_TRUE(RunToMainMenu());

    std::string readBytes;
    for (const PortTraceEvent& event : recorder->getAll())
        readBytes.push_back(static_cast<char>(event.value));

    EXPECT_NE(readBytes.find("ZXEvo 4M"), std::string::npos) << "BaseConf version never read";
    EXPECT_NE(readBytes.find("ZXEvoAVRBoot"), std::string::npos) << "AVR bootloader version never read";
}

// The legacy-FPGA side of the probe (#13BD does not read back there) is pinned
// at unit level: PortDecoder_ATM3_Test.FddMask13BD_ReadWriteResetAndLegacyAbsent.
// The FE image on the legacy FPGA stops before the probe in this emulator;
// whether real hardware gets that far is not verified, so no test asserts it.

/// Magic button on the ERS main menu (E3): the board NMI enters RAM page #FF,
/// the ERS NMI handler leaves it through #xxBE and lands in its "MAGIC
/// Service" (RST8 service ROM page 23), which waits for a key in an EI : HALT
/// loop at #281B (the byte after the HALT is #281D, where the CPU parks)
TEST_F(ZXEvoErs_Test, MagicButtonEntersNmiPageAndReachesMagicService)
{
    Create();
    ASSERT_TRUE(RunToMainMenu());

    _emulator->RequestMNI();
    _emulator->RunNFrames(1, true);
    EXPECT_TRUE(_context->emulatorState.evoInNmi) << "the board NMI must map RAM #FF";

    Memory* memory = _context->pMemory;
    Z80* z80 = _context->pCore->GetZ80();
    EmulatorTestHelper::RunUntil(
        _emulator.get(),
        [&] { return !_context->emulatorState.evoInNmi && memory->IsBank0ROM() && memory->GetROMPage() == 23u && z80->pc == 0x281D; },
        60, 1);
    EXPECT_FALSE(_context->emulatorState.evoInNmi) << "the ERS handler left the NMI page (#xxBE exit)";
    ASSERT_TRUE(memory->IsBank0ROM());
    EXPECT_EQ(memory->GetROMPage(), 23u) << "MAGIC Service code page";
    EXPECT_EQ(z80->pc, 0x281D) << "key-wait loop";
    _emulator->RunNFrames(5, true);
    EXPECT_EQ(z80->pc, 0x281D) << "stays in its key-wait loop";
}

/// ERS RAM disk through the virtual-TR-DOS trap (E4), end to end on the real
/// ROM. With a blank NVRAM the ERS makes drive A its RAM disk (#13BD = %0001,
/// CMOS #EB bits 1..0 = 0) and NEO-DOS reports "Virtual Drive: A". Every
/// TR-DOS disk access below runs the unmodified NEO-DOS against the ERS WD1793
/// emulator in RAM page #FE (rom/page1/dos_fe/dos_fe.a80): the catalog of the
/// fresh RAM disk, a SAVE, and a LOAD whose bytes must match what was saved.
/// Real-ROM boot plus keyboard-typed TR-DOS commands: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, RamDiskSaveListLoadThroughVirtualTrdos)
{
    Create();
    ASSERT_TRUE(RunToMainMenu());
    Keyboard* keyboard = _context->pKeyboard;
    const std::string id = _emulator->GetId();

    auto tap = [&](ZXKeysEnum key) {
        keyboard->PressKey(key);
        _emulator->RunNFrames(4, true);
        keyboard->ReleaseKey(key);
        _emulator->RunNFrames(4, true);
    };
    auto chord = [&](ZXKeysEnum modifier, ZXKeysEnum key) {
        keyboard->PressKey(modifier);
        keyboard->PressKey(key);
        _emulator->RunNFrames(4, true);
        keyboard->ReleaseKey(key);
        keyboard->ReleaseKey(modifier);
        _emulator->RunNFrames(4, true);
    };
    auto digits = [&](const char* text) {
        for (const char* c = text; *c; c++)
            tap(static_cast<ZXKeysEnum>(ZXKEY_0 + (*c - '0')));
    };
    auto screen = [&]() { return ScreenOCR::ocrScreen(id); };

    // "S. TR-DOS" from the ERS menu
    tap(ZXKEY_S);
    _emulator->RunNFrames(60, true);
    ASSERT_NE(screen().find("Virtual Drive: A"), std::string::npos) << screen();
    EXPECT_EQ(_context->emulatorState.evoFddMask & 0x01, 0x01) << "drive A is emulated by the ERS";

    // LIST: the fresh RAM disk the ERS created
    tap(ZXKEY_K);
    tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    std::string text = screen();
    EXPECT_NE(text.find("Title: RAMDISKO"), std::string::npos) << text;
    EXPECT_NE(text.find("Free Sector 2544"), std::string::npos) << text;

    // SAVE "a" CODE 32768,256 of a known pattern
    for (uint16_t i = 0; i < 256; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), static_cast<uint8_t>(i * 7 + 3));
    tap(ZXKEY_S);                              // SAVE
    chord(ZXKEY_SYM_SHIFT, ZXKEY_P);           // "
    tap(ZXKEY_A);
    chord(ZXKEY_SYM_SHIFT, ZXKEY_P);           // "
    chord(ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT);  // extended mode
    tap(ZXKEY_I);                              // CODE
    digits("32768");
    chord(ZXKEY_SYM_SHIFT, ZXKEY_N);           // ,
    digits("256");
    tap(ZXKEY_ENTER);
    _emulator->RunNFrames(150, true);

    tap(ZXKEY_K);
    tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    text = screen();
    EXPECT_NE(text.find("1 File(s)"), std::string::npos) << text;
    EXPECT_NE(text.find("Free Sector 2543"), std::string::npos) << text;

    // LOAD "a" CODE 40960 and compare
    for (uint16_t i = 0; i < 256; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xA000 + i), 0x00);
    tap(ZXKEY_J);                              // LOAD
    chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    tap(ZXKEY_A);
    chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    chord(ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT);
    tap(ZXKEY_I);                              // CODE
    digits("40960");
    tap(ZXKEY_ENTER);
    _emulator->RunNFrames(150, true);

    int mismatches = 0;
    for (uint16_t i = 0; i < 256; i++)
        if (_context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(0xA000 + i)) != static_cast<uint8_t>(i * 7 + 3))
            mismatches++;
    EXPECT_EQ(mismatches, 0) << "the file read back from the RAM disk must match what was saved";
}

