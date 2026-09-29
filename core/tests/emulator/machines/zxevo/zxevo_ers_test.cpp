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

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fatimagebuilder.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/media/mediamanager.h"
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
    decoder->GetEvoAvr().SetCMOSAddress(0xEC);
    ASSERT_EQ(decoder->GetEvoAvr().ReadCMOS() & 0x20, 0x20) << "the ERS stored the automount bit";

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
