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
#include <debugger/debugmanager.h>
#include <debugger/keyboard/debugkeyboardmanager.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <emulator/ports/models/portdecoder_atm3.h>
#include <emulator/ports/portdiagrecorder.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/cdtestdisc.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/fatimagebuilder.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/ide/ata/atapicdrom.h"
#include "emulator/io/ide/idecontroller.h"
#include "emulator/io/storage/cd/cdimage.h"
#include "emulator/io/storage/chd/chdwriter.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/media/mediamanager.h"
#include "emulator/sound/soundmanager.h"
#include "emulator/state/devicestate.h"
#include "pch.h"
#include "stdafx.h"

#include <set>

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
            decoder->GetRtc().SetFixedTime(1767268830);  // 2026-01-01 12:00:30 UTC

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

    /// Press a key for 4 frames, then give the ROM 4 frames to see it released
    void Tap(ZXKeysEnum key)
    {
        _context->pKeyboard->PressKey(key);
        _emulator->RunNFrames(4, true);
        _context->pKeyboard->ReleaseKey(key);
        _emulator->RunNFrames(4, true);
    }

    void Chord(ZXKeysEnum modifier, ZXKeysEnum key)
    {
        _context->pKeyboard->PressKey(modifier);
        _context->pKeyboard->PressKey(key);
        _emulator->RunNFrames(4, true);
        _context->pKeyboard->ReleaseKey(key);
        _context->pKeyboard->ReleaseKey(modifier);
        _emulator->RunNFrames(4, true);
    }

    void Digits(const char* text)
    {
        for (const char* c = text; *c; c++)
            Tap(static_cast<ZXKeysEnum>(ZXKEY_0 + (*c - '0')));
    }

    /// The screen as text (ROM font: NEO-DOS and BASIC, not the ERS menus)
    std::string Screen() { return ScreenOCR::ocrScreen(_emulator->GetId()); }

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
/// PLAN #8: a port trace of the real boot attributes every mainboard access -
/// the ZX-Evo decoder used to hand the trace nothing, so all of them read as
/// undecoded with no device
TEST_F(ZXEvoErs_Test, BootTraceAttributesEveryMainboardPort)
{
    Create();
    FeatureManager* features = _emulator->GetFeatureManager();
    ASSERT_TRUE(features->setFeature(Features::kPortTrace, true));
    PortDiagnosticRecorder* recorder = _context->pPortDecoder->getPortTraceRecorder();
    ASSERT_NE(recorder, nullptr);
    recorder->start();
    ASSERT_TRUE(RunToMainMenu());

    // Low bytes every BaseConf decode owns outright (ClassifyPort)
    const std::set<uint8_t> mainboard = {0xFE, 0xF6, 0xFC, 0xFD, 0xF7, 0x77, 0x57, 0xBF, 0xBE, 0xBD};
    std::set<PortDeviceId> seen;
    size_t mainboardEvents = 0;
    for (const PortTraceEvent& event : recorder->getAll())
    {
        seen.insert(event.deviceId);
        if (!mainboard.count(static_cast<uint8_t>(event.rawPort & 0x00FF)))
            continue;
        mainboardEvents++;
        ASSERT_NE(event.deviceId, PortDeviceId::None)
            << "#" << std::hex << event.rawPort << (event.isOut() ? " OUT" : " IN") << " has no device";
        ASSERT_NE(event.decodedPort, 0x0000) << "#" << std::hex << event.rawPort;
    }
    EXPECT_GT(mainboardEvents, 0u);
    EXPECT_TRUE(seen.count(PortDeviceId::ATM_FF77)) << "the boot programs the ATM system port";
    EXPECT_TRUE(seen.count(PortDeviceId::Memory_Windows)) << "the boot maps the memory windows";
    EXPECT_TRUE(seen.count(PortDeviceId::Evo_Config)) << "the boot reads and writes #BF";
}

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

    // "S. TR-DOS" from the ERS menu
    Tap(ZXKEY_S);
    _emulator->RunNFrames(60, true);
    ASSERT_NE(Screen().find("Virtual Drive: A"), std::string::npos) << Screen();
    EXPECT_EQ(_context->emulatorState.evoFddMask & 0x01, 0x01) << "drive A is emulated by the ERS";

    // LIST: the fresh RAM disk the ERS created
    Tap(ZXKEY_K);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    std::string text = Screen();
    EXPECT_NE(text.find("Title: RAMDISKO"), std::string::npos) << text;
    EXPECT_NE(text.find("Free Sector 2544"), std::string::npos) << text;

    // SAVE "a" CODE 32768,256 of a known pattern
    for (uint16_t i = 0; i < 256; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), static_cast<uint8_t>(i * 7 + 3));
    Tap(ZXKEY_S);                              // SAVE
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);           // "
    Tap(ZXKEY_A);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);           // "
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT);  // extended mode
    Tap(ZXKEY_I);                              // CODE
    Digits("32768");
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_N);           // ,
    Digits("256");
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(150, true);

    Tap(ZXKEY_K);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    text = Screen();
    EXPECT_NE(text.find("1 File(s)"), std::string::npos) << text;
    EXPECT_NE(text.find("Free Sector 2543"), std::string::npos) << text;

    // LOAD "a" CODE 40960 and compare
    for (uint16_t i = 0; i < 256; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0xA000 + i), 0x00);
    Tap(ZXKEY_J);                              // LOAD
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_A);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT);
    Tap(ZXKEY_I);                              // CODE
    Digits("40960");
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(150, true);

    int mismatches = 0;
    for (uint16_t i = 0; i < 256; i++)
        if (_context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(0xA000 + i)) != static_cast<uint8_t>(i * 7 + 3))
            mismatches++;
    EXPECT_EQ(mismatches, 0) << "the file read back from the RAM disk must match what was saved";
}

namespace
{
    /// A Hobeta "$C" file: 17-byte header (name, type, start, length, sector
    /// count, checksum) and the code padded to whole 256-byte sectors
    std::vector<uint8_t> MakeHobeta(const char* name8, uint16_t start, const std::vector<uint8_t>& code)
    {
        const size_t sectors = (code.size() + 255) / 256;
        std::vector<uint8_t> file(17 + sectors * 256, 0x00);
        for (int i = 0; i < 8; i++)
            file[i] = static_cast<uint8_t>(name8[i]);
        file[8] = 'C';
        file[9] = static_cast<uint8_t>(start);
        file[10] = static_cast<uint8_t>(start >> 8);
        file[11] = static_cast<uint8_t>(code.size());
        file[12] = static_cast<uint8_t>(code.size() >> 8);
        file[13] = 0x00;
        file[14] = static_cast<uint8_t>(sectors);
        unsigned checksum = 0;
        for (unsigned i = 0; i < 15; i++)
            checksum += file[i] * 257u + i;
        file[15] = static_cast<uint8_t>(checksum);
        file[16] = static_cast<uint8_t>(checksum >> 8);
        std::copy(code.begin(), code.end(), file.begin() + 17);
        return file;
    }
}  // namespace

/// ERS-SD-1: "5. SDcard boot" on the real ROM. The card is a FAT16 volume
/// (MBR, partition at LBA 2048) holding SD_BOOT.$C; the ERS finds the card
/// through its Z-Controller driver (CMD0/CMD8/ACMD41 over #77/#57), reads the
/// partition table and the FAT, loads the Hobeta body at its start address
/// and jumps there (rom/mainmenu/src/sdcardboot.a80, fat_boot/micro_boot_fat.a80).
/// The program marks memory and parks in a loop.
/// Real-ROM boot plus a FAT file load: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, SdCardBootRunsSdBootFromAFatVolume)
{
    // #8000: DI : LD A,4 : OUT (#FE),A : LD HL,#C0DE : LD (#9000),HL : JR $
    const std::vector<uint8_t> code = {0xF3, 0x3E, 0x04, 0xD3, 0xFE, 0x21, 0xDE, 0xC0, 0x22, 0x00, 0x90, 0x18, 0xFE};
    FatImageSpec spec;
    spec.label = "ZXEVO TEST";
    spec.files = {{"SD_BOOT.$C", MakeHobeta("sd_boot ", 0x8000, code)}};
    ScratchFatImage image("zxevo-sdboot.img", spec);
    ASSERT_TRUE(image.ok()) << image.error();

    Create();
    auto* decoder = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
    ASSERT_TRUE(decoder->InsertSdCard(image.path(), SdCardSpi::WriteMode::Session));
    ASSERT_TRUE(RunToMainMenu());

    Tap(ZXKEY_5);  // "5. SDcard boot"

    Z80* z80 = _context->pCore->GetZ80();
    Memory* memory = _context->pMemory;
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return z80->pc == 0x800B; }, 300);
    ASSERT_EQ(z80->pc, 0x800B) << "SD_BOOT.$C not reached";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x9000), 0xDE);
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x9001), 0xC0);
    EXPECT_GT(decoder->GetSdCard().blocksRead(), 2u) << "MBR, boot sector, FAT, directory and the file came from the card";
    EXPECT_EQ(decoder->GetSdCard().blocksWritten(), 0u);
}

/// ERS-MNT-1: a TRD file on the SD card mounted as TR-DOS drive B and used by
/// the unmodified TR-DOS (NEO-DOS) - the user-facing "mount TR-DOS from SD".
/// ERS "F. File browse" lists the card's FAT root, ENTER on EYEACHE.TRD offers
/// "TRD to:", "6. Mount B:" records the file's clusters and sets #13BD bit 1.
/// Every later TR-DOS access to B traps (E4) into the ERS WD1793 emulator,
/// which reads and writes the TRD sectors on the card through the Z-Controller
/// (rom/page5/source/fat/mounter.a80). The card writes through to the image
/// file (Persist), so the TR-DOS SAVE must appear inside the TRD in the file.
/// Real-ROM boot plus menus and typed TR-DOS commands: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, SdCardTrdMountedAsDriveBReadAndWrittenByTrdos)
{
    std::ifstream in(TestPathHelper::FindProjectRoot() / "testdata/loaders/trd/EyeAche.trd", std::ios::binary);
    const std::vector<uint8_t> trd((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_EQ(trd.size(), 655360u);
    FatImageSpec spec;
    spec.files = {{"EYEACHE.TRD", trd}};
    ScratchFatImage image("zxevo-mount.img", spec);
    ASSERT_TRUE(image.ok()) << image.error();

    Create();
    auto* decoder = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
    ASSERT_TRUE(decoder->InsertSdCard(image.path(), SdCardSpi::WriteMode::Persist));
    ASSERT_TRUE(RunToMainMenu());

    // F. File browse -> ENTER on EYEACHE.TRD (the only file) -> 6. Mount B:
    for (ZXKeysEnum key : {ZXKEY_F, ZXKEY_ENTER, ZXKEY_6})
    {
        Tap(key);
        _emulator->RunNFrames(40, true);
    }
    EXPECT_EQ(_context->emulatorState.evoFddMask, 0x02) << "drive B is now served by the ERS";

    // S. TR-DOS; the first key after NEO-DOS starts is swallowed, so an empty
    // line first; then *"b" makes B the current drive
    Tap(ZXKEY_S);
    _emulator->RunNFrames(60, true);
    ASSERT_NE(Screen().find("Virtual Drive: B"), std::string::npos) << Screen();
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(20, true);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_B);  // *
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);  // "
    Tap(ZXKEY_B);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(20, true);

    // LIST: the TRD's own catalog, read from the card
    const uint64_t readsBeforeList = decoder->GetSdCard().blocksRead();
    Tap(ZXKEY_K);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    std::string text = Screen();
    EXPECT_NE(text.find("Disk Drive: B"), std::string::npos) << text;
    EXPECT_NE(text.find("2 File(s)"), std::string::npos) << text;
    EXPECT_NE(text.find("Free Sector 2392"), std::string::npos) << text;
    EXPECT_NE(text.find("EYEACHE-<B>141"), std::string::npos) << text;
    EXPECT_NE(text.find("boot    <B> 11"), std::string::npos) << text;
    EXPECT_GT(decoder->GetSdCard().blocksRead(), readsBeforeList) << "the catalog came from the card";

    // SAVE "t" CODE 32768,256 of a known pattern onto B
    for (uint16_t i = 0; i < 256; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), static_cast<uint8_t>(i ^ 0xA5));
    Tap(ZXKEY_S);                              // SAVE
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_T);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT);  // extended mode
    Tap(ZXKEY_I);                              // CODE
    Digits("32768");
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_N);           // ,
    Digits("256");
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(150, true);
    EXPECT_GT(decoder->GetSdCard().blocksWritten(), 0u);

    Tap(ZXKEY_K);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    text = Screen();
    EXPECT_NE(text.find("3 File(s)"), std::string::npos) << text;
    EXPECT_NE(text.find("Free Sector 2391"), std::string::npos) << text;

    // The image file: find the TRD inside the FAT volume, then the new
    // catalog entry and its data sector
    std::ifstream file(image.path(), std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto trdAt = std::search(bytes.begin(), bytes.end(), trd.begin(), trd.begin() + 32);
    ASSERT_NE(trdAt, bytes.end()) << "the TRD's first catalog entries in the image";
    const size_t base = static_cast<size_t>(trdAt - bytes.begin());
    const uint8_t* entry = &bytes[base + 2 * 16];
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(entry), 9), "t       C");
    EXPECT_EQ(entry[9] | (entry[10] << 8), 32768);
    EXPECT_EQ(entry[11] | (entry[12] << 8), 256);
    const size_t data = base + (static_cast<size_t>(entry[15]) * 16 + entry[14]) * 256;
    ASSERT_LT(data + 256, bytes.size());
    int mismatches = 0;
    for (int i = 0; i < 256; i++)
        if (bytes[data + static_cast<size_t>(i)] != static_cast<uint8_t>(i ^ 0xA5))
            mismatches++;
    EXPECT_EQ(mismatches, 0) << "the saved bytes are in the TRD on the card";
}

namespace
{
    std::string Utf8(const std::filesystem::path& path)
    {
        const auto u8 = path.u8string();
        return std::string(u8.begin(), u8.end());
    }

    std::vector<uint8_t> ReadHostFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
}  // namespace

/// ACC-1: "5. SDcard boot" from a PC folder. The folder holds SD_BOOT.$C;
/// the media manager turns it into a FAT16 volume (HostFolderFat) and the
/// unmodified ERS finds, loads and runs the program from it
/// Real-ROM boot plus a FAT file load: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, SdCardBootFromAHostFolder)
{
    const std::vector<uint8_t> code = {0xF3, 0x3E, 0x04, 0xD3, 0xFE, 0x21, 0xDE, 0xC0, 0x22, 0x00, 0x90, 0x18, 0xFE};
    const std::vector<uint8_t> hobeta = MakeHobeta("sd_boot ", 0x8000, code);
    ScratchFolder folder("zxevo-sd-folder");
    folder.File("SD_BOOT.$C", std::string(hobeta.begin(), hobeta.end()));
    folder.File(".DS_Store", "host junk the guest never sees");

    Create();
    MediaManager& manager = *_context->pMediaManager;
    MediaSource source;
    source.path = Utf8(folder.Path());
    const MediaResult inserted = manager.Insert("sd.zc", source);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    EXPECT_EQ(manager.Info("sd.zc")->format, "folder-fat16");
    ASSERT_TRUE(RunToMainMenu());

    Tap(ZXKEY_5);  // "5. SDcard boot"
    Z80* z80 = _context->pCore->GetZ80();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return z80->pc == 0x800B; }, 300);
    ASSERT_EQ(z80->pc, 0x800B) << "SD_BOOT.$C from the folder not reached";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9000), 0xDE);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9001), 0xC0);
}

/// The same SD card as a MAME CHD (docs/inprogress/2026-10-02-media-chd/): MAME keeps every disk and card as a
/// CHD, and the format is shared - the card that boots from a raw image boots from its CHD through the same SD slot,
/// compressed with chdman's default codecs
/// Real-ROM boot plus a FAT file load: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, SdCardBootFromAChd)
{
    const std::vector<uint8_t> code = {0xF3, 0x3E, 0x04, 0xD3, 0xFE, 0x21, 0xDE, 0xC0, 0x22, 0x00, 0x90, 0x18, 0xFE};
    FatImageSpec spec;
    spec.label = "ZXEVO CHD";
    spec.files = {{"SD_BOOT.$C", MakeHobeta("sd_boot ", 0x8000, code)}};
    ScratchFatImage image("zxevo-sdboot-chd.img", spec);
    ASSERT_TRUE(image.ok()) << image.error();
    ScratchFolder folder("zxevo-sd-chd");
    const auto u8 = (folder.Path() / "card.chd").u8string();
    const std::string chdPath(u8.begin(), u8.end());
    {
        auto raw = RawImage::Open(image.path(), RawImage::Access::ReadOnly);
        ASSERT_NE(raw, nullptr);
        chd::WriteOptions options;
        options.codecs = chd::kDefaultHardDiskCodecs;
        std::string error;
        ASSERT_TRUE(chd::WriteChd(chdPath, *raw, options, &error)) << error;
    }

    Create();
    MediaManager& manager = *_context->pMediaManager;
    MediaSource source;
    source.path = chdPath;
    const MediaResult inserted = manager.Insert("sd.zc", source);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    EXPECT_EQ(manager.Info("sd.zc")->format, "chd");
    ASSERT_TRUE(RunToMainMenu());

    Tap(ZXKEY_5);  // "5. SDcard boot"
    Z80* z80 = _context->pCore->GetZ80();
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return z80->pc == 0x800B; }, 300);
    ASSERT_EQ(z80->pc, 0x800B) << "SD_BOOT.$C from the CHD not reached";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9000), 0xDE);
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9001), 0xC0);
}

/// ACC-2: a TRD in a PC folder mounted as TR-DOS drive B; TR-DOS lists it and
/// SAVEs to it. The folder never changes; the export (the guest's view as an
/// image) carries the save inside the TRD, read back by an independent FAT reader
/// Real-ROM boot plus menus and typed TR-DOS commands: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, SdCardFolderTrdMountedReadWrittenAndExported)
{
    const std::filesystem::path original = TestPathHelper::FindProjectRoot() / "testdata/loaders/trd/EyeAche.trd";
    const std::vector<uint8_t> trd = ReadHostFile(original);
    ASSERT_EQ(trd.size(), 655360u);
    ScratchFolder folder("zxevo-mount-folder");
    const std::filesystem::path hostTrd = folder.File("EYEACHE.TRD", std::string(trd.begin(), trd.end()));

    Create();
    MediaManager& manager = *_context->pMediaManager;
    MediaSource source;
    source.path = Utf8(folder.Path());
    InsertOptions options;
    options.freeBytes = 4 * 1024 * 1024;  // the export below writes the whole volume
    ASSERT_TRUE(manager.Insert("sd.zc", source, options).Ok());
    ASSERT_TRUE(RunToMainMenu());

    for (ZXKeysEnum key : {ZXKEY_F, ZXKEY_ENTER, ZXKEY_6})  // File browse -> the TRD -> 6. Mount B:
    {
        Tap(key);
        _emulator->RunNFrames(40, true);
    }
    ASSERT_EQ(_context->emulatorState.evoFddMask, 0x02);

    Tap(ZXKEY_S);
    _emulator->RunNFrames(60, true);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(20, true);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_B);  // *"b"
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_B);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(20, true);
    Tap(ZXKEY_K);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    std::string text = Screen();
    EXPECT_NE(text.find("2 File(s)"), std::string::npos) << text;
    EXPECT_NE(text.find("EYEACHE-<B>141"), std::string::npos) << text;

    for (uint16_t i = 0; i < 256; i++)
        _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), static_cast<uint8_t>(i ^ 0x5A));
    Tap(ZXKEY_S);                              // SAVE "t" CODE 32768,256
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_T);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Chord(ZXKEY_CAPS_SHIFT, ZXKEY_SYM_SHIFT);
    Tap(ZXKEY_I);
    Digits("32768");
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_N);
    Digits("256");
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(150, true);
    Tap(ZXKEY_K);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    text = Screen();
    EXPECT_NE(text.find("3 File(s)"), std::string::npos) << text;

    // The folder is untouched: the write went into the session layer
    EXPECT_EQ(ReadHostFile(hostTrd), trd) << "the host TRD never changes";
    EXPECT_TRUE(manager.Info("sd.zc")->dirty);

    // Export and read the TRD back out of the FAT volume
    const std::string image = TestPathHelper::GetUniqueTestScratchPath("zxevo-folder-export.img");
    ASSERT_TRUE(manager.Export("sd.zc", image).Ok());
    auto exported = RawImage::Open(image, RawImage::Access::ReadOnly);
    ASSERT_NE(exported, nullptr);
    FatVolumeReader reader;
    std::string error;
    ASSERT_TRUE(reader.Open(*exported, CodePage::Cp866, &error)) << error;
    std::vector<uint8_t> saved;
    ASSERT_TRUE(reader.ReadFile("/EYEACHE.TRD", saved, &error)) << error;
    ASSERT_EQ(saved.size(), trd.size());
    const uint8_t* entry = &saved[2 * 16];
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(entry), 9), "t       C");
    const size_t data = (static_cast<size_t>(entry[15]) * 16 + entry[14]) * 256;
    int mismatches = 0;
    for (int i = 0; i < 256; i++)
        if (saved[data + static_cast<size_t>(i)] != static_cast<uint8_t>(i ^ 0x5A))
            mismatches++;
    EXPECT_EQ(mismatches, 0) << "the export carries the TR-DOS save";

    exported.reset();
    std::error_code ec;
    std::filesystem::remove(FileHelper::ToFsPath(image), ec);
}

/// ACC-4: IMAGE.MNT automount from a PC folder. The ERS reads IMAGE.MNT from
/// the root of the last FAT partition at start when CMOS #EC bit 5 is set
/// (rom/page5/source/fat/mounter.a80 FIND_MOUNTED); a line "E:/EYEACHE.TRD B:"
/// mounts the TRD as drive B without any menu. The bit is set the way a user
/// does, with "N" in the main menu, so the ERS keeps its NVRAM CRC valid
/// Two real-ROM boots: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, ImageMntAutomountFromAHostFolder)
{
    const std::vector<uint8_t> trd = ReadHostFile(TestPathHelper::FindProjectRoot() / "testdata/loaders/trd/EyeAche.trd");
    ScratchFolder folder("zxevo-automount");
    folder.File("EYEACHE.TRD", std::string(trd.begin(), trd.end()));
    folder.File("IMAGE.MNT", "E:/EYEACHE.TRD B:\r\n");

    Create();
    MediaSource source;
    source.path = Utf8(folder.Path());
    ASSERT_TRUE(_context->pMediaManager->Insert("sd.zc", source).Ok());
    ASSERT_TRUE(RunToMainMenu());
    // A blank NVRAM makes drive A the ERS RAM disk (E4): bit 0 only
    EXPECT_EQ(_context->emulatorState.evoFddMask, 0x01) << "nothing mounted from the card while automount is off";

    Tap(ZXKEY_N);  // main menu: automount on (CMOS #EC bit 5)
    _emulator->RunNFrames(20, true);
    auto* decoder = static_cast<PortDecoder_ATM3*>(_context->pPortDecoder);
    decoder->GetEvoAvr().WriteAddress(0xEC);
    ASSERT_EQ(decoder->GetEvoAvr().ReadData() & 0x20, 0x20) << "the ERS stored the automount bit";

    _emulator->Reset();
    ASSERT_TRUE(RunToMainMenu());
    EXPECT_EQ(_context->emulatorState.evoFddMask, 0x03) << "IMAGE.MNT mounted EYEACHE.TRD as drive B at start (A stays the RAM disk)";
    EXPECT_TRUE(_context->pMediaManager->Info("sd.zc")->present) << "the card survived the reset";

    // TR-DOS on drive B lists the TRD's catalog
    Tap(ZXKEY_S);
    _emulator->RunNFrames(60, true);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(20, true);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_B);  // *"b"
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_B);
    Chord(ZXKEY_SYM_SHIFT, ZXKEY_P);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(20, true);
    Tap(ZXKEY_K);
    Tap(ZXKEY_ENTER);
    _emulator->RunNFrames(60, true);
    const std::string text = Screen();
    EXPECT_NE(text.find("Disk Drive: B"), std::string::npos) << text;
    EXPECT_NE(text.find("EYEACHE-<B>141"), std::string::npos) << text;
}

/// ACC-3: NedoOS boots from a PC folder to its shell. The folder
/// (testdata/machines/zxevo/nedoos/sdcard) holds the ZX-Evo SD boot loader,
/// term.com, cmd.com and an autoexec.bat that echoes a marker. The kernel
/// reads them through its own FatFs; the shell composes the prompt "M:/bin>"
/// at run time, so the prompt plus the marker in RAM mean the shell ran the
/// batch file. Typing into NedoOS needs its PS/2 keyboard (E2b), not needed here
/// Real-ROM boot plus a whole OS boot (~300 frames): slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, NedoOsBootsFromAHostFolder)
{
    const std::filesystem::path card = TestPathHelper::FindProjectRoot() / "testdata/machines/zxevo/nedoos/sdcard";
    ASSERT_TRUE(std::filesystem::is_directory(card)) << Utf8(card);

    Create();
    MediaSource source;
    source.path = Utf8(card);
    InsertOptions options;
    options.freeBytes = 16 * 1024 * 1024;  // NedoOS needs no room; keep the volume small
    const MediaResult inserted = _context->pMediaManager->Insert("sd.zc", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    ASSERT_TRUE(RunToMainMenu());

    auto inRam = [this](const std::string& needle) {
        for (uint16_t page = 0; page < 256; page++)
        {
            const uint8_t* bytes = _context->pMemory->RAMPageAddress(page);
            if (bytes && std::search(bytes, bytes + PAGE_SIZE, needle.begin(), needle.end()) != bytes + PAGE_SIZE)
                return true;
        }
        return false;
    };

    Tap(ZXKEY_5);  // "5. SDcard boot" -> SD_BOOT.$C -> the NedoOS kernel
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return inRam("M:/bin>"); }, 800, 20);
    EXPECT_TRUE(inRam("M:/bin>")) << "the NedoOS shell prompt";
    EXPECT_TRUE(inRam("UNREALNGSDBOOT")) << "the shell read autoexec.bat from the folder";
    EXPECT_FALSE(_context->pMediaManager->Info("sd.zc")->dirty) << "booting writes nothing to the card";
}

/// PLAN #83, real software: NedoOS's audio CD player (cdplay.com, the shipped release) on the CD drive
/// (the IDE slave).
/// 1. An Enhanced CD (cdtestdisc.h default: session 1 audio tracks 1-2, session 2 data track 3): it
///    reads the TOC, lists the tracks, '1' plays track 1 with PLAY AUDIO MSF (the digit keys pick the
///    track by number: the ZX-Evo PS/2 path delivers '1'), shows the position it reads with READ
///    SUB-CHANNEL, pauses and resumes (Space) and stops (S); the drive's audio comes out of its mixer
///    row. '2' plays track 2 "to the next track's start" (in session 2): the drive plays to session 1's
///    lead-out. '3' (the data track): the drive refuses the PLAY (ILLEGAL MODE FOR THIS TRACK) and keeps
///    playing track 2; cdplay does not check the error and follows the drive back to track 2 (what the
///    owner saw on the old layout: '1' on the data track "played the track under the cursor").
/// 2. A folder of WAV / MP3 files as an audio CD (AudioFolderDisc): swapped in, 'T' rereads the TOC,
///    the tracks are the files in natural order, '2' plays the second file.
/// Slow (~4 s): boots the ERS and NedoOS
TEST_F(ZXEvoErs_Test, NedoOsCdplayPlaysAudioTracks)
{
    const std::filesystem::path root = TestPathHelper::FindProjectRoot() / "testdata/machines/zxevo/nedoos";
    ASSERT_TRUE(std::filesystem::exists(root / "cdplay/cdplay.com"));
    ScratchFolder card("nedoos-cdplay");
    std::filesystem::create_directories(card.Path() / "bin");
    std::filesystem::copy_file(root / "sdcard/SD_BOOT.$C", card.Path() / "SD_BOOT.$C");
    for (const char* name : {"term.com", "cmd.com"})
        std::filesystem::copy_file(root / "sdcard/bin" / name, card.Path() / "bin" / name);
    std::filesystem::copy_file(root / "cdplay/cdplay.com", card.Path() / "bin/cdplay.com");
    card.File("bin/autoexec.bat", "cdplay\r\n");
    ScratchFolder disc("nedoos-cdplay-disc");
    // Track 1 LBA 0-449, track 2 pregap 450, INDEX 01 at 600 (00:10:00) to 1049; data track 3 at 12450
    const std::string cue = cdtest::WriteMusicDisc(disc.Path(), 2, 6, 300);
    const cdtest::MusicDiscLayout layout = cdtest::MusicLayoutOf(2, 6, 300, cdtest::MusicLayout::Enhanced);

    Create();
    _context->pSoundManager->setCoreRatePin(44100);
    InsertOptions options;
    options.freeBytes = 16 * 1024 * 1024;
    MediaSource source;
    source.path = Utf8(card.Path());
    ASSERT_TRUE(_context->pMediaManager->Insert("sd.zc", source, options).Ok());
    source.path = cue;
    const MediaResult inserted = _context->pMediaManager->Insert("ide0.slave", source, options);
    ASSERT_TRUE(inserted.Ok()) << inserted.message;
    ASSERT_TRUE(RunToMainMenu());

    auto screen = [this] {
        std::string text;
        const StateNode lines = DeviceState::VideoText(_context);
        if (const StateNode* list = lines.find("lines"))
        {
            for (const StateNode& line : list->items)
                text += line.find("text")->s + "\n";
        }
        return text;
    };
    auto shows = [&](const std::string& needle) { return screen().find(needle) != std::string::npos; };

    Tap(ZXKEY_5);  // "5. SDcard boot" -> NedoOS -> autoexec.bat -> cdplay
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return shows("Track 03 ["); }, 1500, 20);
    ASSERT_TRUE(shows("Audio CD Player")) << screen();
    ASSERT_TRUE(shows("Track 01 [AUDIO]  Start 00:02:00  Dur 00:08")) << "the TOC as the player shows it\n" << screen();
    ASSERT_TRUE(shows("Track 02 [AUDIO]  Start 00:10:00  Dur 02:38")) << "its length counts the session gap\n" << screen();

    AtapiCdrom* cd = static_cast<AtapiCdrom*>(_context->pIdeController->Channel().Unit(1));
    DebugKeyboardManager* keys = _emulator->GetDebugManager()->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);
    keys->TypeText("1");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Playing; }, 200, 5);
    ASSERT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing) << screen();
    EXPECT_EQ(cd->Audio().State().playStartLba, 0u) << "'1' is track 1: PLAY AUDIO MSF from its TOC start";
    EXPECT_EQ(cd->Audio().State().endLba, layout.audioStart[1]) << "... to the next track's start";

    // The position the player reads with READ SUB-CHANNEL: one emulated second later it shows 00:01
    _emulator->RunNFrames(60, true);
    EXPECT_TRUE(shows("[PLAYING] Track: 01 / 03")) << screen();
    EXPECT_TRUE(shows("Time:   00:01 / 00:08")) << screen();

    // Audio out of the drive's mixer row (turbo renders nothing: one frame at the normal speed)
    _emulator->DisableTurboMode();
    _emulator->RunNFrames(2, true);
    const int16_t* out = _context->pSoundManager->deviceBuffer(AudioSourceType::CdAudio1);
    ASSERT_NE(out, nullptr) << "the CD drive's row has this frame's audio";
    EXPECT_TRUE(cd->Audio().HadSoundLastFrame()) << "a 330 Hz tone, not silence";
    _emulator->EnableTurboMode();

    // Space pauses: the head stays; Space again resumes
    keys->TypeText(" ");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Paused; }, 200, 5);
    ASSERT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Paused) << screen();
    const uint64_t paused = cd->Audio().PeekHeadSample();
    _emulator->RunNFrames(50, true);
    EXPECT_EQ(cd->Audio().PeekHeadSample(), paused);
    EXPECT_TRUE(shows("[PAUSE]")) << screen();
    keys->TypeText(" ");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Playing; }, 200, 5);
    ASSERT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing) << screen();
    _emulator->RunNFrames(20, true);
    EXPECT_GT(cd->Audio().PeekHeadSample(), paused);

    // S stops
    keys->TypeText("s");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Idle; }, 200, 5);
    EXPECT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Idle) << screen();
    _emulator->RunNFrames(20, true);
    EXPECT_TRUE(shows("[STOPPED]")) << screen();

    // '2': track 2 "to track 3's start" (the data session): the drive plays to session 1's lead-out
    keys->TypeText("2");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Playing; }, 200, 5);
    ASSERT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing) << screen();
    EXPECT_EQ(cd->Audio().State().playStartLba, layout.audioStart[1]);
    EXPECT_EQ(cd->Audio().State().endLba, layout.audioLeadOut);

    // '3' (the data track): refused (ILLEGAL MODE FOR THIS TRACK), track 2 plays on; cdplay ignores the
    // error, reads track 2 from the drive and plays it again from its start
    _emulator->RunNFrames(100, true);
    const uint64_t before = cd->Audio().PeekHeadSample();
    keys->TypeText("3");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->State().asc == AtapiCdrom::kAscIllegalModeForTrack; }, 100, 1);
    EXPECT_EQ(cd->State().senseKey, AtapiCdrom::kSenseIllegalRequest);
    EXPECT_EQ(cd->State().asc, AtapiCdrom::kAscIllegalModeForTrack) << "the PLAY of track 3 was refused";
    EXPECT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing) << "the refused PLAY did not stop track 2";
    EXPECT_GE(cd->Audio().PeekHeadSample(), before);
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return shows("Track: 02 / 03"); }, 200, 5);
    EXPECT_TRUE(shows("[PLAYING] Track: 02 / 03")) << "cdplay follows the drive back to track 2\n" << screen();
    keys->TypeText("s");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Idle; }, 200, 5);

    // 2. A folder of audio files as an audio CD: "1 low.wav" (5 s), "2 high.wav" (5 s), "10 short.mp3"
    //    (0.5 s, padded to 4 s): tracks at LBA 0, 525, 1050; lead-out 1350
    ScratchFolder music("nedoos-cdplay-folder");
    cdtest::WriteFile(music.Path() / "2 high.wav", cdtest::Wave(cdtest::TonePcm(375, 880.0)));
    cdtest::WriteFile(music.Path() / "1 low.wav", cdtest::Wave(cdtest::TonePcm(375, 440.0)));
    std::filesystem::copy_file(TestPathHelper::GetTestDataPath("media/audio/tone-440-660-44k-stereo.mp3"), music.Path() / "10 short.mp3");
    InsertOptions now;
    now.immediate = true;
    source.path = Utf8(music.Path());
    const MediaResult folder = _context->pMediaManager->Insert("ide0.slave", source, now);
    ASSERT_TRUE(folder.Ok()) << folder.message;
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] {
        keys->TypeText("t");  // reread the TOC (the first read after the swap reports the disc change)
        _emulator->RunNFrames(20, true);
        return shows("Track 01 [AUDIO]  Start 00:02:00  Dur 00:07");
    }, 10, 1);
    ASSERT_TRUE(shows("Track 02 [AUDIO]  Start 00:09:00  Dur 00:07")) << screen();
    ASSERT_TRUE(shows("Track 03 [AUDIO]  Start 00:16:00  Dur 00:04")) << screen();
    keys->TypeText("2");
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return cd->Audio().PeekStatus() == CdAudioStatus::Playing; }, 200, 5);
    ASSERT_EQ(cd->Audio().PeekStatus(), CdAudioStatus::Playing) << screen();
    EXPECT_EQ(cd->Audio().State().playStartLba, 525u) << "the second file in natural order";
    EXPECT_EQ(cd->Disc()->TrackTitle(1), "2 high.wav");
    _emulator->RunNFrames(60, true);
    EXPECT_TRUE(shows("[PLAYING] Track: 02 / 03")) << screen();
    EXPECT_TRUE(shows("Time:   00:01 / 00:07")) << screen();
}

namespace
{
    void PutBothEndian32(std::string& out, size_t at, uint32_t value)
    {
        for (int i = 0; i < 4; i++)
        {
            out[at + i] = static_cast<char>(value >> (8 * i));       // little endian
            out[at + 7 - i] = static_cast<char>(value >> (8 * i));   // big endian
        }
    }

    /// An ISO 9660 directory record for `name` at `extent` (2048-byte blocks), `size` bytes
    std::string DirectoryRecord(const std::string& name, uint32_t extent, uint32_t size, bool directory)
    {
        std::string record(33 + name.size() + ((33 + name.size()) % 2), '\0');
        record[0] = static_cast<char>(record.size());
        PutBothEndian32(record, 2, extent);
        PutBothEndian32(record, 10, size);
        record[25] = directory ? 0x02 : 0x00;
        record[28] = 1;  // volume sequence number 1 (both-endian, 16-bit)
        record[31] = 1;
        record[32] = static_cast<char>(name.size());
        std::memcpy(record.data() + 33, name.data(), name.size());
        return record;
    }

    /// A minimal ISO 9660 image: the primary volume descriptor at block 16,
    /// the terminator at 17, the root directory at 18 and AUTORUN.ZX at 20
    std::string MakeIso(const std::vector<uint8_t>& autorun)
    {
        const uint32_t blocks = 24;
        std::string iso(blocks * 2048, '\0');
        char* pvd = iso.data() + 16 * 2048;
        pvd[0] = 1;
        std::memcpy(pvd + 1, "CD001", 5);
        pvd[6] = 1;
        std::memset(pvd + 8, ' ', 64);
        std::memcpy(pvd + 40, "UNREALNG", 8);
        PutBothEndian32(iso, 16 * 2048 + 80, blocks);  // volume space size
        const std::string root = DirectoryRecord(std::string(1, '\0'), 18, 2048, true);
        std::memcpy(pvd + 156, root.data(), root.size());
        char* terminator = iso.data() + 17 * 2048;
        terminator[0] = static_cast<char>(0xFF);
        std::memcpy(terminator + 1, "CD001", 5);
        terminator[6] = 1;

        std::string directory = DirectoryRecord(std::string(1, '\0'), 18, 2048, true) +
                                DirectoryRecord(std::string(1, '\1'), 18, 2048, true) +
                                DirectoryRecord("AUTORUN.ZX;1", 20, static_cast<uint32_t>(autorun.size()), false);
        std::memcpy(iso.data() + 18 * 2048, directory.data(), directory.size());
        std::memcpy(iso.data() + 20 * 2048, autorun.data(), autorun.size());
        return iso;
    }
}  // namespace

/// ERS-HDD-1: "B. HDD boot" on the real ROM (rom/mainmenu/src/hdd_cd_boot.a80
/// HDDBOOT): the ERS resets the NemoIDE master through #C8, recalibrates,
/// checks DRDY + DSC, reads 48 sectors from C0/H0/S3 (LBA 2) to #6000 and
/// jumps there, with the data word read as IN #10 / IN #11 (Nemo order).
/// Real-ROM boot plus an IDE load: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, HddBootRunsTheBootBlockFromLba2)
{
    // #6000: DI : LD A,2 : OUT (#FE),A : LD HL,#C0DE : LD (#9000),HL : JR $
    const std::vector<uint8_t> code = {0xF3, 0x3E, 0x02, 0xD3, 0xFE, 0x21, 0xDE, 0xC0, 0x22, 0x00, 0x90, 0x18, 0xFE};
    std::string disk(64 * 512, '\0');
    std::memcpy(disk.data() + 2 * 512, code.data(), code.size());
    ScratchFolder folder("zxevo-hddboot");
    const std::string image = Utf8(folder.File("hdd.img", disk));

    Create();
    ASSERT_EQ(_context->pIdeController->Scheme(), IDE_NEMO_DIVIDE) << "NemoIDE is built into ZX-Evo";
    MediaSource source;
    source.path = image;
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.master", source, options).Ok());
    ASSERT_TRUE(RunToMainMenu());

    Tap(ZXKEY_B);  // "B. HDD boot"

    Z80* z80 = _context->pCore->GetZ80();
    Memory* memory = _context->pMemory;
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return z80->pc == 0x600B; }, 300);
    ASSERT_EQ(z80->pc, 0x600B) << "the boot block at #6000 did not run";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x9000), 0xDE);
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x9001), 0xC0);
    EXPECT_FALSE(_context->pMediaManager->Info("ide0.master")->dirty) << "booting writes nothing";
}

/// ERS-CD-1: "D. CD boot" on the real ROM (hdd_cd_boot.a80 CDBOOTGO): the
/// slave is an ATAPI drive; the ERS resets it (#08), tells it from a disk by
/// the aborted IDENTIFY and the #EB14 signature, reads the TOC, loads the
/// start of the session (the volume descriptor lands at #E000), the root
/// directory, finds AUTORUN.ZX, loads it to #6000 and enters it with A = #B0.
/// Real-ROM boot plus a CD load: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, CdBootRunsAutorunFromAnIso)
{
    // AUTORUN.ZX at #6000: DI : LD (#9000),A : LD HL,#C0DE : LD (#9001),HL : JR $
    const std::vector<uint8_t> code = {0xF3, 0x32, 0x00, 0x90, 0x21, 0xDE, 0xC0, 0x22, 0x01, 0x90, 0x18, 0xFE};
    ScratchFolder folder("zxevo-cdboot");
    const std::string iso = Utf8(folder.File("boot.iso", MakeIso(code)));

    Create();
    ASSERT_EQ(_context->config.ide[1].cd, 1) << "the shipped config fits the slave as a CD drive (CD1=1)";
    MediaSource source;
    source.path = iso;
    InsertOptions options;
    options.immediate = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, options).Ok());
    ASSERT_TRUE(RunToMainMenu());

    Tap(ZXKEY_D);  // "D. CD boot"

    Z80* z80 = _context->pCore->GetZ80();
    Memory* memory = _context->pMemory;
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return z80->pc == 0x600A; }, 600);
    ASSERT_EQ(z80->pc, 0x600A) << "AUTORUN.ZX did not run";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x9000), 0xB0) << "entered with A = #B0 (slave)";
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x9001), 0xDE);
    EXPECT_EQ(memory->DirectReadFromZ80Memory(0x9002), 0xC0);
}

/// ERS-CD-2: the shipped ZX-Evo has its CD drive (the slave), and the ERS
/// sees the disc go and come back. With the disc ejected, "D. CD boot" gets
/// NOT READY / medium not present (sense 2/#3A) and keeps retrying READ (10);
/// a disc inserted as a user would (with the swap delay) raises UNIT ATTENTION
/// / medium changed (6/#28), which the ERS clears, then it boots AUTORUN.ZX.
/// Real-ROM boot plus a CD load: slower than 50 ms by nature
TEST_F(ZXEvoErs_Test, CdBootSeesTheDiscEjectedAndInsertedAgain)
{
    const std::vector<uint8_t> code = {0xF3, 0x32, 0x00, 0x90, 0x21, 0xDE, 0xC0, 0x22, 0x01, 0x90, 0x18, 0xFE};
    ScratchFolder folder("zxevo-cdeject");
    MediaSource source;
    source.path = Utf8(folder.File("boot.iso", MakeIso(code)));

    Create();
    ASSERT_EQ(_context->config.ide[1].cd, 1) << "the shipped config fits the slave as a CD drive";
    InsertOptions now;
    now.immediate = true;
    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source, now).Ok());
    ASSERT_TRUE(RunToMainMenu());
    ASSERT_TRUE(_context->pMediaManager->Eject("ide0.slave").Ok());
    EXPECT_FALSE(_context->pMediaManager->Info("ide0.slave")->present);

    auto atapi = [&] {
        const StateNode report = DeviceState::Ide(_context);
        return *report.find("units")->items[1].find("atapi");
    };

    Tap(ZXKEY_D);  // "D. CD boot" with the drive empty
    Z80* z80 = _context->pCore->GetZ80();
    _emulator->RunNFrames(100, true);
    ASSERT_NE(z80->pc, 0x600A) << "booted without a disc";
    StateNode drive = atapi();
    EXPECT_FALSE(drive.find("disc")->b);
    EXPECT_EQ(drive.find("sense_key")->i, AtapiCdrom::kSenseNotReady);
    EXPECT_EQ(drive.find("asc")->i, AtapiCdrom::kAscMediumNotPresent);
    EXPECT_EQ(drive.find("last_packet")->s.substr(0, 2), "28") << "the ERS keeps retrying READ (10)";

    ASSERT_TRUE(_context->pMediaManager->Insert("ide0.slave", source).Ok());  // with the swap delay
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return z80->pc == 0x600A; }, 600);
    ASSERT_EQ(z80->pc, 0x600A) << "AUTORUN.ZX did not run after the disc came back";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x9000), 0xB0) << "entered with A = #B0 (slave)";
    drive = atapi();
    EXPECT_TRUE(drive.find("disc")->b);
    EXPECT_FALSE(drive.find("unit_attention")->b) << "the swap was reported (UNIT ATTENTION) and the ERS took it";
    EXPECT_EQ(drive.find("sense_key")->i, 0) << "the reads after it succeeded: no sense left (SPC: the next command discards it)";
}

/// NOS-KBD-1: NedoOS on ZX-Evo reads its keyboard only from the AVR's PS/2 log
/// (PS2KBD=1 kernel): a command typed through automation runs in the shell.
/// Slow (~1.5 s): boots the ERS and NedoOS from the SD card, then types
TEST_F(ZXEvoErs_Test, NedoOsShellRunsATypedCommand)
{
    const std::filesystem::path card = TestPathHelper::FindProjectRoot() / "testdata/machines/zxevo/nedoos/sdcard";
    ASSERT_TRUE(std::filesystem::is_directory(card)) << Utf8(card);

    Create();
    MediaSource source;
    source.path = Utf8(card);
    InsertOptions options;
    options.freeBytes = 16 * 1024 * 1024;
    ASSERT_TRUE(_context->pMediaManager->Insert("sd.zc", source, options).Ok());
    ASSERT_TRUE(RunToMainMenu());

    // The NedoOS terminal draws in the ATM 80x25 text mode (screenatm.cpp M_ATMTX): row r's even columns at
    // #01C0 + 64 * r of the screen page (5, or 7 with #7FFD bit 3), its odd columns #2000 further on. What the
    // shell printed is read there: text in RAM is no evidence, the terminal's receive buffer takes the output
    // in whatever pieces the pipe hands over and overwrites them (with the ZX-Evo's 14 MHz memory waits the
    // "free" output arrives in two pieces)
    auto screenHas = [this](const std::string& needle) {
        const uint16_t page = (_context->emulatorState.p7FFD & 0x08) ? 7 : 5;
        const uint8_t* bytes = _context->pMemory->RAMPageAddress(page);
        for (int row = 0; row < 25; row++)
        {
            std::string line;
            for (int i = 0; i < 40; i++)
            {
                line += static_cast<char>(bytes[0x01C0 + 64 * row + i]);
                line += static_cast<char>(bytes[0x21C0 + 64 * row + i]);
            }
            if (line.find(needle) != std::string::npos)
                return true;
        }
        return false;
    };

    Tap(ZXKEY_5);  // "5. SDcard boot"
    EmulatorTestHelper::RunUntil(_emulator.get(), [&] { return screenHas("M:/bin>"); }, 800, 20);
    ASSERT_TRUE(screenHas("M:/bin>")) << "the NedoOS shell prompt";
    _emulator->RunNFrames(50);
    ASSERT_FALSE(screenHas("free pages=")) << "the command's output before the command";

    DebugKeyboardManager* keys = _emulator->GetDebugManager()->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);
    keys->TypeText("free\n");
    _emulator->RunNFrames(200);

    EXPECT_TRUE(screenHas("M:/bin>free")) << "the typed command echoed";
    EXPECT_TRUE(screenHas("free pages=")) << "the command ran and printed";
}
