// ATM710 CP/M boot, disk-read verification and Prince of Persia launcher
// repro tests.
//
// The v7.10 sys ROM page carries an embedded CP/M 2.2 (XVR BIOS V1.07.13),
// so the BIOS-menu "CP/M" entry boots the OS straight from ROM and then uses
// the floppy in drive A: as the filesystem disk. testdata/machines/atm/cpm/
// prince.trd is a CP/M-filesystem disk on TR-DOS geometry (16x256-byte
// sectors, 80 tracks x 2 sides): system tracks 0-1 empty (all 0xE5), CP/M
// directory on track 2 (BLS=2048, 16-bit allocation pointers), data from
// track 3. The catalog holds 42 user-0 entries plus a hidden user-15
// CONFIG; the "BUG" entry's extension starts with byte 0xFD, which DIR
// renders as a garbage glyph - on-disk content, not a read error.
//
// Verified flow: BIOS boot menu -> CP/M -> "A>" prompt -> DIR (the listing
// must match the on-disk catalog: every user-0 name present, CONFIG absent,
// the BUG entry shown with its mangled glyph) -> PR2 (Prince launcher for
// banked 512K/1024K machines).

#include <emulator/emulator.h>
#include <emulator/emulatorcontext.h>
#include <emulator/emulatormanager.h>
#include <emulator/io/fdc/fdd.h>
#include <emulator/io/fdc/wd1793.h>
#include <emulator/io/keyboard/keyboard.h>
#include <emulator/memory/memory.h>
#include <emulator/platform.h>
#include <gtest/gtest.h>
#include <map>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"
#include "emulator/io/fdc/iwd1793observer.h"
#include "loaders/disk/loader_trd.h"
#include "pch.h"
#include "stdafx.h"

class ATM710CpmBoot_Test : public ::testing::Test
{
protected:
    EmulatorManager* _manager = nullptr;

protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    void TearDown() override
    {
        auto emulatorIds = _manager->GetEmulatorIds();
        for (const auto& id : emulatorIds)
        {
            _manager->RemoveEmulator(id);
        }
    }

    /// One menu "down" press: CAPS SHIFT + 6 held together
    static void PressDown(const std::shared_ptr<Emulator>& emulator, int holdFrames = 8)
    {
        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        keyboard->PressKey(ZXKEY_CAPS_SHIFT);
        keyboard->PressKey(ZXKEY_6);
        emulator->RunNFrames(holdFrames, true);
        keyboard->ReleaseKey(ZXKEY_6);
        keyboard->ReleaseKey(ZXKEY_CAPS_SHIFT);
        emulator->RunNFrames(holdFrames, true);
    }

    static void PressEnter(const std::shared_ptr<Emulator>& emulator)
    {
        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        keyboard->PressKey(ZXKEY_ENTER);
        emulator->RunNFrames(12, true);
        keyboard->ReleaseKey(ZXKEY_ENTER);
        emulator->RunNFrames(12, true);
    }

    /// Type plain text at the CP/M console (letters/digits/space only - the
    /// CP/M command line needs nothing else for DIR / PR2)
    static void TypeText(const std::shared_ptr<Emulator>& emulator, const std::string& text)
    {
        Keyboard* keyboard = emulator->GetContext()->pKeyboard;
        for (char c : text)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = (char)std::toupper((unsigned char)c);  // same key either way
            }
            ZXKeysEnum key;
            if (c == ' ')
            {
                key = ZXKEY_SPACE;
            }
            else if (c == ':')
            {
                // CP/M drive prefixes ("DIR B:") need the colon: SYM SHIFT
                // + Z on the ZX matrix (see keyboard.h special-symbol map)
                Keyboard* keyboard = emulator->GetContext()->pKeyboard;
                keyboard->PressKey(ZXKEY_SYM_SHIFT);
                keyboard->PressKey(ZXKEY_Z);
                emulator->RunNFrames(10, true);
                keyboard->ReleaseKey(ZXKEY_Z);
                keyboard->ReleaseKey(ZXKEY_SYM_SHIFT);
                emulator->RunNFrames(8, true);
                continue;
            }
            else if (c >= 'A' && c <= 'Z')
            {
                // The enum mirrors the ZX matrix: ZXKEY_I = 0x48 and
                // ZXKEY_H = 0x49 are swapped versus ASCII order
                if (c == 'H')
                    key = ZXKEY_H;
                else if (c == 'I')
                    key = ZXKEY_I;
                else
                    key = static_cast<ZXKeysEnum>(0x41 + (c - 'A'));
            }
            else if (c >= '0' && c <= '9')
            {
                key = static_cast<ZXKeysEnum>(0x30 + (c - '0'));
            }
            else
            {
                continue;
            }
            keyboard->PressKey(key);
            emulator->RunNFrames(10, true);
            keyboard->ReleaseKey(key);
            emulator->RunNFrames(8, true);
        }
    }

    /// Type a CP/M command and wait until the console echoed it on the
    /// command line (guards against dropped keys before ENTER)
    static void TypeCommand(const std::shared_ptr<Emulator>& emulator, const std::string& text)
    {
        TypeText(emulator, text);
        EmulatorContext* context = emulator->GetContext();
        EmulatorTestHelper::RunUntil(
            emulator.get(),
            [&] { return StripSpaces(DecodeTextRows(context, 0, 24)).find(text) != std::string::npos; }, 120);
    }

    /// Decode rows of the M_ATMTX 80-column text screen (the ATM BIOS / CP/M
    /// console video mode): chars1 = vp[0x1C0 + 64r + n/2], chars0 =
    /// vp[0x2000 + same]. Non-ASCII codes (e.g. the 0xFD glyph in the BUG
    /// catalog entry) decode as '.' so garbage stays visible in dumps.
    static std::string DecodeTextRows(EmulatorContext* context, uint8_t rowFrom, uint8_t rowTo)
    {
        EmulatorState& state = context->emulatorState;
        Memory* memory = context->pMemory;
        uint8_t videoPage = (state.p7FFD & 0x08) ? 7 : 5;
        const uint8_t* vp = memory->RAMPageAddress(videoPage);
        std::string result;
        for (uint32_t row = rowFrom; row < rowTo; row++)
        {
            for (uint32_t n = 0; n < 80; n++)
            {
                uint32_t byteIdx = 0x1C0 + 64 * row + n / 2;
                bool fromP0 = (n % 2 == 0);
                uint8_t code = fromP0 ? vp[byteIdx] : vp[0x2000 + byteIdx];
                result += (code >= 0x20 && code < 0x7F) ? (char)code : (code == 0 ? ' ' : '.');
            }
            result += '\n';
        }
        return result;
    }

    static std::string StripSpaces(const std::string& text)
    {
        std::string result;
        for (char c : text)
        {
            if (c != ' ')
                result += c;
        }
        return result;
    }

    static std::string Hex(uint32_t value, int width = 2)
    {
        char buf[11];
        snprintf(buf, sizeof(buf), "%0*X", width, value);
        return buf;
    }

    /// FDC command/completion tracer: what the CP/M disk monitor asks the
    /// WD1793 for and what status comes back. verbose=false keeps the command
    /// counter but silences the trace (the game streams hundreds of overlay
    /// reads after entering EGA mode)
    struct FdcTraceObserver : IWD1793Observer
    {
        int commandCount = 0;
        bool verbose = true;
        void onFDCCommand(uint8_t command, const WD1793& fdc) override
        {
            commandCount++;
            if (!verbose)
                return;
            std::cout << "[FDC #" << commandCount - 1 << "] cmd=" << Hex(command)
                      << " trackReg=" << Hex(fdc.getTrackRegister())
                      << " sectorReg=" << Hex(fdc.getSectorRegister()) << "\n";
        }
        void onFDCCommandComplete(uint8_t status, const WD1793& fdc) override
        {
            if (!verbose)
                return;
            std::cout << "[FDC done] status=" << Hex(status)
                      << " trackReg=" << Hex(fdc.getTrackRegister())
                      << " sectorReg=" << Hex(fdc.getSectorRegister()) << "\n";
        }
        void onFDCPortAccess(uint8_t port, uint8_t value, bool isWrite, const WD1793& fdc) override
        {
            if (!verbose)
                return;
            // Track sector reads end to end: data register reads ($5F) during
            // Type II commands and the polling pattern around them
            static int suppress = 0;
            if (isWrite)
            {
                std::cout << "[FDC W " << Hex(port) << "] " << Hex(value)
                          << " (t=" << Hex(fdc.getTrackRegister()) << " s=" << Hex(fdc.getSectorRegister()) << ")\n";
            }
            else
            {
                // Status polls ($3F) come in long bursts - sample one in 16
                if (port == 0x3F)
                {
                    if ((suppress++ & 15) == 0)
                        std::cout << "[FDC R 3F] " << Hex(value) << " (poll)\n";
                }
                else
                {
                    std::cout << "[FDC R " << Hex(port) << "] " << Hex(value) << "\n";
                }
            }
        }
    };

    /// Boot to the ATM BIOS menu, insert prince.trd into drive A:, select the
    /// CP/M menu entry and wait for the CCP "A>" prompt.
    /// @param menuDowns Item index of CP/M in the BIOS menu (0-based, from
    ///                  the first selectable item; CP/M is item 0)
    /// @param fdcTrace Optional FDC tracer attached from emulator creation
    ///                 (the CP/M monitor reads and caches the catalog during
    ///                 boot - before the prompt appears)
    /// @param onCreated Callback run right after emulator creation (install
    ///                 bus trace hooks etc. before any code runs)
    std::shared_ptr<Emulator> BootToCpmPrompt(const std::string& id, uint32_t ramSize, int menuDowns = 0,
                                              int maxFrames = 900, FdcTraceObserver* fdcTrace = nullptr,
                                              std::function<void(Emulator*)> onCreated = nullptr)
    {
        auto emulator = _manager->CreateEmulatorWithModelAndRAM(id, "ATM710", ramSize, LoggerLevel::LogError);
        EXPECT_NE(emulator, nullptr);
        if (!emulator)
            return emulator;
        // Boot-bound test asserting on decoded VRAM only - never on rendered
        // pixels - so turbo is safe and removes the audio/render cost.
        emulator->EnableTurboMode();
        EmulatorContext* context = emulator->GetContext();
        if (fdcTrace)
        {
            context->pBetaDisk->addObserver(fdcTrace);
        }
        if (onCreated)
        {
            onCreated(emulator.get());
        }

        EmulatorTestHelper::RunUntil(
            emulator.get(),
            [&] { return StripSpaces(DecodeTextRows(context, 0, 24)).find("SPECTRUM128") != std::string::npos; }, 300);

        // Insert the CP/M game disk before CP/M selects drive A:
        std::string trdPath = TestPathHelper::GetTestDataPath("machines/atm/cpm/prince.trd");
        EXPECT_TRUE(FileHelper::FileExists(trdPath)) << "Fixture missing: " << trdPath;
        if (!FileHelper::FileExists(trdPath))
            return nullptr;
        LoaderTRD trdLoader(context, trdPath);
        bool loaded = trdLoader.loadImage();
        EXPECT_TRUE(loaded) << "TRD not loaded: " << trdPath;
        if (!loaded)
            return nullptr;
        // Insert into physical drive 0 (not the currently-selected drive, since
        // the FDC drive selection changes dynamically based on Beta128 port writes)
        FDD* fdd = context->coreState.diskDrives[0];
        EXPECT_NE(fdd, nullptr);
        if (!fdd)
            return nullptr;
        fdd->insertDisk(trdLoader.getImage());

        for (int i = 0; i < menuDowns; i++)
        {
            PressDown(emulator, 4);
        }
        PressEnter(emulator);

        // CCP banner + "A>" prompt on the 80-column console
        EmulatorTestHelper::RunUntil(
            emulator.get(),
            [&] {
                std::string screen = StripSpaces(DecodeTextRows(context, 0, 24));
                return screen.find("A>") != std::string::npos &&
                       screen.find("CP/M") != std::string::npos;
            },
            maxFrames);
        return emulator;
    }

    /// Launch PR2 (Prince of Persia) from the CP/M prompt. The game's data
    /// files (PRINCE*.OVL etc.) live on the floppy, which this monitor
    /// exposes as B: - A: is the electronic disk. The transient opens them
    /// with FCB drive=0 = CURRENT drive, so the launch only works with B:
    /// selected as current: type "B:" first, then "PR2" at the "B>" prompt.
    /// ("B:PR2" in one line fails: the CCP loads the transient from B: but
    /// re-selects A: before jumping to it, the loader's first OPEN returns
    /// FF and it RETs straight back to the CCP)
    void LaunchPr2Game(const std::shared_ptr<Emulator>& emulator)
    {
        EmulatorContext* context = emulator->GetContext();
        TypeCommand(emulator, "B:");
        PressEnter(emulator);
        EmulatorTestHelper::RunUntil(
            emulator.get(),
            [&] { return StripSpaces(DecodeTextRows(context, 0, 24)).rfind("B>") != std::string::npos; },
            200);
        TypeCommand(emulator, "PR2");
        PressEnter(emulator);
    }

    /// Full PR2 run verification: boot to CP/M, launch the game from the
    /// floppy, then assert it entered EGA mode, painted a multi-color title
    /// screen and keeps streaming overlays from the floppy instead of
    /// exiting back to the CCP.
    /// @param ramSize RAM in KB - the game runs on both 512 and 1024
    void VerifyPr2GameRuns(uint32_t ramSize)
    {
        FdcTraceObserver fdcTrace;
        fdcTrace.verbose = false;
        auto emulator = BootToCpmPrompt("atm710-cpm-pr2-" + std::to_string(ramSize), ramSize, 0, 900,
                                        &fdcTrace);
        ASSERT_NE(emulator, nullptr);
        if (!emulator)
            return;
        EmulatorContext* context = emulator->GetContext();

        LaunchPr2Game(emulator);

        // The CCP loads the packed PR2.COM, its stub unpacks itself over the
        // TPA and flips pFF77 from text mode (1) to EGA mode 0 (M_ATM16
        // 320x200x16 + 7MHz turbo) - the CP/M text console is gone
        EmulatorTestHelper::RunUntil(
            emulator.get(), [&] { return (context->emulatorState.pFF77 & 7) == 0; }, 1200);
        ASSERT_EQ(context->emulatorState.pFF77 & 7, 0) << "PR2 did not switch to EGA mode";
        const int fdcAtSwitch = fdcTrace.commandCount;
        EXPECT_GT(fdcAtSwitch, 0) << "no floppy reads while loading PR2.COM";

        // The title waits on the game's own line reader: SPACE (buffered as
        // text) then ENTER (CR ends the line). Raw keys - the CP/M console
        // is switched out already
        Keyboard* keyboard = context->pKeyboard;
        keyboard->PressKey(ZXKEY_SPACE);
        emulator->RunNFrames(12, true);
        keyboard->ReleaseKey(ZXKEY_SPACE);
        emulator->RunNFrames(20, true);
        keyboard->PressKey(ZXKEY_ENTER);
        emulator->RunNFrames(12, true);
        keyboard->ReleaseKey(ZXKEY_ENTER);
        emulator->RunNFrames(40, true);

        // Title screen on the displayed page. The game paints it
        // progressively: the mid-draw states already carry >10000 px but only
        // 2-3 colors, so wait for the fully painted title (multi-color AND
        // mostly non-background - the pre-title banner screens are sparse
        // text)
        EmulatorTestHelper::RunUntil(
            emulator.get(),
            [&] {
                EgaStats now = MeasureEgaScreen(context);
                return now.colorsUsed >= 4 && now.nonBackground > 10000;
            },
            900);
        EgaStats stats = MeasureEgaScreen(context);
        DumpEgaScreen(context, "title (displayed)");
        DumpEgaScreen(context, "title bank7", 7);
        EXPECT_GE(stats.colorsUsed, 4) << "title screen should be multi-color";
        EXPECT_GT(stats.nonBackground, 10000) << "title screen should be mostly drawn";

        // Overlays (PRINCE*.OVL) stream from the floppy through the #FF7
        // window after the mode switch; a failed launch would RET to the CCP
        // and put the text console back
        EXPECT_GE(fdcTrace.commandCount - fdcAtSwitch, 50) << "no overlay reads after EGA switch";
        EXPECT_EQ(context->emulatorState.pFF77 & 7, 0) << "game returned to the CCP text console";
    }

    /// Decode the M_ATM16 screen (EGA 320x200x16) into a 320x200 buffer of
    /// 4-bit palette indices plus a 16-entry histogram. Layout per
    /// screenzx.cpp DrawATM16: with 7FFD.3 selecting videoPage 5/7 and
    /// altPage 4 below it, the four bit-planes sit at ap+0, vp+0, ap+0x2000,
    /// vp+0x2000; a line is 40 plane-bytes (200 lines = 8000 per plane) and
    /// every byte packs two 4-bit palette indices: left = {b6,b2,b1,b0},
    /// right = {b7,b5,b4,b3}
    static void DecodeEgaPixels(EmulatorContext* context, int videoPageOverride, uint8_t* pix,
                                long* hist)
    {
        EmulatorState& state = context->emulatorState;
        Memory* memory = context->pMemory;
        const uint8_t videoPage = videoPageOverride ? (uint8_t)videoPageOverride
                                                    : (uint8_t)((state.p7FFD & 0x08) ? 7 : 5);
        const uint8_t* vp = memory->RAMPageAddress(videoPage);
        const uint8_t* ap = memory->RAMPageAddress(videoPage - 4);
        for (uint32_t y = 0; y < 200; y++)
        {
            const uint32_t lineBase = 40 * y;
            for (uint32_t j = 0; j < 40; j++)
            {
                for (uint32_t q = 0; q < 4; q++)
                {
                    const uint8_t* plane = (q & 1) ? vp : ap;
                    const uint8_t bt = plane[((q >> 1) << 13) + lineBase + j];
                    const uint8_t left = (uint8_t)((bt & 0x07) | (bt & 0x40 ? 0x08 : 0x00));
                    const uint8_t right = (uint8_t)(((bt >> 3) & 0x07) | (bt & 0x80 ? 0x08 : 0x00));
                    pix[320 * y + 8 * j + 2 * q] = left;
                    pix[320 * y + 8 * j + 2 * q + 1] = right;
                    hist[left]++;
                    hist[right]++;
                }
            }
        }
    }

    /// Color statistics of the visible EGA screen: how many palette entries
    /// are in use and how many pixels differ from the dominant background
    struct EgaStats
    {
        int colorsUsed = 0;
        uint8_t background = 0;
        long nonBackground = 0;
    };

    static EgaStats MeasureEgaScreen(EmulatorContext* context, int videoPageOverride = 0)
    {
        uint8_t pix[320 * 200];
        long hist[16] = {};
        DecodeEgaPixels(context, videoPageOverride, pix, hist);
        EgaStats stats;
        long bgCount = -1;
        for (int c = 0; c < 16; c++)
        {
            if (hist[c] > 0)
                stats.colorsUsed++;
            if (hist[c] > bgCount)
            {
                bgCount = hist[c];
                stats.background = (uint8_t)c;
            }
        }
        stats.nonBackground = 64000 - bgCount;
        return stats;
    }

    /// Print the visible EGA screen as color stats + an 80x25 density map
    static void DumpEgaScreen(EmulatorContext* context, const std::string& tag, int videoPageOverride = 0)
    {
        std::vector<uint8_t> pix(320 * 200);
        long hist[16] = {};
        DecodeEgaPixels(context, videoPageOverride, pix.data(), hist);
        uint8_t background = 0;
        long bgCount = -1;
        int colorsUsed = 0;
        for (int c = 0; c < 16; c++)
        {
            if (hist[c] > 0)
                colorsUsed++;
            if (hist[c] > bgCount)
            {
                bgCount = hist[c];
                background = (uint8_t)c;
            }
        }
        std::cout << "[EGA " << tag << "] colors used: " << colorsUsed
                  << ", background #" << Hex(background) << " (" << bgCount << " px of 64000)\n";

        // 80x25 map: each cell = 4x8 px block, ramp by non-background density
        static const char ramp[] = " .:*#";
        std::cout << "[EGA " << tag << " map]\n";
        for (uint32_t row = 0; row < 25; row++)
        {
            std::string line;
            for (uint32_t col = 0; col < 80; col++)
            {
                int nonBg = 0;
                for (uint32_t dy = 0; dy < 8; dy++)
                    for (uint32_t dx = 0; dx < 4; dx++)
                        if (pix[320 * (8 * row + dy) + 4 * col + dx] != background)
                            nonBg++;
                line += ramp[(nonBg * 5) / 33];  // density 0..32 -> ramp 0..4
            }
            std::cout << line << "\n";
        }
    }
};

TEST_F(ATM710CpmBoot_Test, DiagnosticBootMenuAndCpmSelect)
{
    // Iteration 1 diagnostic: dump the BIOS menu, calibrate the CP/M item
    // position from the SPECTRUM 128 anchor (2 downs in the verified flow)
    // and dump whatever comes up after ENTER
    auto emulator = _manager->CreateEmulatorWithModelAndRAM("atm710-cpm-diag", "ATM710", 1024, LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->EnableTurboMode();
    EmulatorContext* context = emulator->GetContext();

    EmulatorTestHelper::RunUntil(
        emulator.get(),
        [&] { return StripSpaces(DecodeTextRows(context, 0, 24)).find("SPECTRUM128") != std::string::npos; }, 300);

    std::string menu = DecodeTextRows(context, 0, 24);
    std::cout << "[MENU]\n" << menu;

    // Row calibration: menu items are consecutive 1-row lines; SPECTRUM 128
    // sits at item index 2 (2 downs in the verified Enter128Menu flow)
    int rowSpec = -1, rowCpm = -1;
    for (int row = 0; row < 24; row++)
    {
        std::string line = menu.substr((size_t)row * 81, 80);
        std::string stripped = StripSpaces(line);
        if (stripped.find("SPECTRUM128") != std::string::npos && rowSpec < 0)
            rowSpec = row;
        if (stripped.find("CP/M") != std::string::npos && rowCpm < 0)
            rowCpm = row;
    }
    std::cout << "[CALIB] rowSpec=" << rowSpec << " rowCpm=" << rowCpm << "\n";
    ASSERT_GE(rowSpec, 0) << "SPECTRUM 128 entry not found in menu";
    ASSERT_GE(rowCpm, 0) << "CP/M entry not found in menu";
    // Menu layout (dumped): item 0 = CP/M, item 1 = TR-DOS 48, item 2 =
    // SPECTRUM 128 (the 2-downs anchor from the verified Enter128Menu flow)
    int menuDowns = 2 + (rowCpm - rowSpec);
    ASSERT_GE(menuDowns, 0) << "CP/M row calibrates above the first menu item";

    // Insert the disk, select CP/M, dump the settling screen
    std::string trdPath = TestPathHelper::GetTestDataPath("machines/atm/cpm/prince.trd");
    ASSERT_TRUE(FileHelper::FileExists(trdPath)) << "Fixture missing: " << trdPath;
    LoaderTRD trdLoader(context, trdPath);
    ASSERT_TRUE(trdLoader.loadImage()) << "TRD not loaded: " << trdPath;
    WD1793* wd1793 = context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr);
    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr);
    fdd->insertDisk(trdLoader.getImage());

    for (int i = 0; i < menuDowns; i++)
    {
        PressDown(emulator, 4);
    }
    PressEnter(emulator);
    for (int i = 0; i < 60; i++)
    {
        emulator->RunFrame(true);
    }
    std::cout << "[CPM-60f] pFF77=" << Hex(context->emulatorState.pFF77)
              << " pc=" << Hex(context->pCore->GetZ80()->pc, 4) << "\n";
    std::cout << DecodeTextRows(context, 0, 24);

    EmulatorTestHelper::RunUntil(
        emulator.get(),
        [&] { return StripSpaces(DecodeTextRows(context, 0, 24)).find("A>") != std::string::npos; }, 900);
    std::cout << "[CPM-prompt]\n" << DecodeTextRows(context, 0, 24);
    SUCCEED() << "diagnostic dump";
}

TEST_F(ATM710CpmBoot_Test, CpmDirListsPrinceCatalog)
{
    // CP/M 2.2 DIR must list every user-0 catalog entry of prince.trd exactly
    // as stored on disk (42 entries spanning all 8 catalog sectors of track
    // 2). The hidden user-15 CONFIG must not appear; the BUG entry's 0xFD
    // extension byte renders as a garbage glyph - on-disk content, not a
    // read error. No "BIOS ERROR" / "BDOS ERR" may be on screen.
    // FDC trace attached from emulator creation: the CP/M monitor reads (and
    // caches) the catalog during boot, before the prompt appears
    FdcTraceObserver fdcTrace;
    // Port-level FDC handshake from emulator creation to the prompt: every
    // #1F/#3F/#5F/#7F/#FF read and write with the values seen by the guest -
    // the abort point (the monitor stops after ~4 status polls and never
    // reads #7F) needs the status values to be explained
    std::vector<std::string> fdcIo;
    uint8_t lastInValue[256] = {};
    long totalIns = 0;
    uint16_t lastFdcPc = 0;  // PC of the most recent FDC port access
    Z80* bootCpu = nullptr;   // assigned in onCreated, before any guest code runs
    Emulator* bootEmu = nullptr;  // for stack reads inside the hook
    uint16_t readCmdCaller = 0;   // return address on the stack when READ is issued
    // M1 trace armed at the LAST FDC status read (pc 0x15B1, after the READ
    // completes): records the monitor's post-read decision flow
    std::vector<uint16_t> m1Trace;
    bool m1TraceArmed = false;
    auto captureM1 = [&m1Trace, &m1TraceArmed](uint16_t pc)
    {
        if (m1TraceArmed && m1Trace.size() < 8000)
            m1Trace.push_back(pc);
    };
    auto captureFdcIo = [&fdcIo, &lastInValue, &totalIns, &lastFdcPc, &bootCpu, &bootEmu, &readCmdCaller, &m1TraceArmed](char type, uint16_t port, uint8_t value)
    {
        // 'I'/'O' are port I/O; 'R'/'W' are memory accesses (their "port" is
        // an address, which collides with the FDC port numbers in low RAM).
        // The ATM710 decoder matches the FDC on the LOW byte only (any
        // #NN1F/#NN3F/#NN5F/#NN7F/#NNFF reaches the WD1793). The monitor's
        // DRQ/INTRQ poll loop produces tens of thousands of identical reads,
        // so record all OUTs but only IN value TRANSITIONS (and a total)
        //
        // Also watched: writes into the monitor channel-descriptor table
        // (#FA00-#FB60 and its banked-copy alias #7A00-#7B60) - the fallback
        // mount installs per-channel device descriptors there (ch0 = floppy,
        // ch1 = HDD), and the writer PC reveals the device-assignment code
        if (type == 'W' && ((port >= 0xFA00 && port <= 0xFB60) || (port >= 0x7A00 && port <= 0x7B60)) &&
            fdcIo.size() < 1500)
        {
            char buf[80];
            snprintf(buf, sizeof(buf), "W %04X = %02X pc=%04X", port, value, bootCpu ? bootCpu->pc : 0);
            fdcIo.push_back(buf);
        }
        if (type == 'I' || type == 'O')
        {
            uint16_t low = port & 0x00FF;
            if (low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0xFF)
            {
                uint16_t pc = bootCpu ? bootCpu->pc : 0;
                lastFdcPc = pc;
                if (type == 'O')
                {
                    char buf[80];
                    snprintf(buf, sizeof(buf), "O %04X = %02X pc=%04X", port, value, pc);
                    fdcIo.push_back(buf);
                    // When the READ command byte hits the FDC, the return address
                    // into the monitor's caller is on top of the guest stack
                    if (low == 0x1F && value == 0x80 && bootEmu && !readCmdCaller)
                    {
                        uint16_t sp = bootCpu->sp;
                        Memory* mem = bootEmu->GetContext()->pMemory;
                        readCmdCaller = mem->DirectReadFromZ80Memory(static_cast<uint16_t>(sp)) |
                                        (mem->DirectReadFromZ80Memory(static_cast<uint16_t>(sp + 1)) << 8);
                    }
                }
                else
                {
                    totalIns++;
                    // The clean status read that ends the READ command arms
                    // the M1 trace (captures the monitor's decision flow)
                    if (low == 0x1F && readCmdCaller && value == 0x00 && !m1TraceArmed)
                    {
                        m1TraceArmed = true;
                    }
                    if (value != lastInValue[low] || fdcIo.size() < 8)
                    {
                        char buf[80];
                        snprintf(buf, sizeof(buf), "I %04X = %02X pc=%04X (total %ld)", port, value, pc, totalIns);
                        fdcIo.push_back(buf);
                        lastInValue[low] = value;
                    }
                }
            }
        }
    };
    auto emulator = BootToCpmPrompt("atm710-cpm-dir", 1024, 0, 900, &fdcTrace,
                                    [&](Emulator* e)
                                    {
                                        bootEmu = e;
                                        bootCpu = e->GetContext()->pCore->GetZ80();
                                        bootCpu->busTraceHook = captureFdcIo;
                                        bootCpu->m1TraceHook = captureM1;
                                    });
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();

    std::cout << "[BOOT FDC I/O] " << fdcIo.size() << " recorded, " << totalIns << " total INs, lastPc=" << Hex(lastFdcPc)
              << ", readCmdCaller=" << Hex(readCmdCaller) << "\n";
    for (size_t i = 0; i < fdcIo.size() && i < 1100; i++)
    {
        std::cout << "  [" << i << "] " << fdcIo[i] << "\n";
    }
    // Guest code context around the last FDC access - the monitor's decision
    // point after the successful track-0 read (what makes it stop probing)
    if (lastFdcPc)
    {
        Memory* mem = context->pMemory;
        uint16_t base = lastFdcPc > 96 ? lastFdcPc - 96 : 0;
        std::cout << "[MONITOR CODE @" << Hex(lastFdcPc) << "] ";
        for (uint32_t a = base; a < base + 192 && a < 0x10000; a++)
            std::cout << Hex(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
        std::cout << "\n";
        // Wider window for offline disassembly: the whole FDC driver + callers
        std::cout << "[ROM 1000-1900] ";
        for (uint32_t a = 0x1000; a < 0x1900; a++)
            std::cout << Hex(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
        std::cout << "\n";
        std::cout << "[ROM 1900-2100] ";
        for (uint32_t a = 0x1900; a < 0x2100; a++)
            std::cout << Hex(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
        std::cout << "\n";
    }
    Z80* cpu = context->pCore->GetZ80();
    cpu->busTraceHook = nullptr;  // boot capture done
    cpu->m1TraceHook = nullptr;
    // Post-read decision flow: raw M1 address list (analyzed offline against
    // the ROM dump) - armed at the FORCE INTERRUPT that precedes the read
    std::cout << "[M1 TRACE after READ] " << m1Trace.size() << " instrs\n";
    for (uint16_t pc : m1Trace)
        std::cout << Hex(pc) << " ";
    std::cout << "\n";

    // Image-side ground truth: the catalog sectors as loaded from prince.trd
    // (loader correctness check - the emulation path is what varies)
    {
        WD1793* wd1793 = context->pBetaDisk;
        DiskImage* img = wd1793->getDrive() ? wd1793->getDrive()->getDiskImage() : nullptr;
        ASSERT_NE(img, nullptr);
        for (int s = 0; s < 2; s++)
        {
            auto* sector = img->getTrack(2)->getSector(s);
            std::string hex;
            for (int i = 0; i < 32; i++)
                hex += Hex(sector->data[i]);
            std::cout << "[IMG t2 s" << (s + 1) << "] " << hex << "\n";
        }
    }

    // System-table forensics: BIOS/BDOS placement and the monitor's RAM
    // interface at #F800 (RQDIO #F824 / RQSET #F82A / RQCHK #F82D per the XVR
    // BIOS reference) - the layer between the CP/M BIOS and the device
    // drivers that the fallback mount talks to
    {
        Memory* mem = context->pMemory;
        auto word = [mem](uint16_t a)
        {
            return (uint16_t)(mem->DirectReadFromZ80Memory(a) | (mem->DirectReadFromZ80Memory(a + 1) << 8));
        };
        std::cout << "[SYS] word@0001=" << Hex(word(1), 4) << " word@0006=" << Hex(word(6), 4) << "\n";
        std::cout << "[MON RAM F800-F840] ";
        for (uint32_t a = 0xF800; a < 0xF840; a++)
            std::cout << Hex(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
        std::cout << "\n";
        // RAM-side RQDIO dispatcher (#F88F entry prologue seen in the traces)
        // and the low-RAM window/copy helpers behind RST 8 / RST 0x18
        std::cout << "[MON RAM F880-F900] ";
        for (uint32_t a = 0xF880; a < 0xF900; a++)
            std::cout << Hex(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
        std::cout << "\n";
        std::cout << "[LOWRAM 0040-0080] ";
        for (uint32_t a = 0x0040; a < 0x0080; a++)
            std::cout << Hex(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
        std::cout << "\n";
        // Monitor data area behind the interface: the channel-descriptor
        // table the RQDIO driver dispatches on lives here (first descriptor
        // found at #FA01) - dumped raw for offline decode of all 8 channels
        std::cout << "[MON RAM F900-FC00] ";
        for (uint32_t a = 0xF900; a < 0xFC00; a++)
            std::cout << Hex(mem->DirectReadFromZ80Memory(static_cast<uint16_t>(a)));
        std::cout << "\n";
    }

    // Whole-RAM scans: (a) was the on-disk catalog EVER read into guest RAM
    // (TITLE1 pattern from track 2), (b) which disk-monitor channel
    // descriptors the fallback installed (doc 9.3 layout; DTYP 2 =
    // electronic/RAM disk, 3 = floppy, 4 = HDD), (c) which DPB the CP/M BIOS
    // ended up with (doc 7.2 layout)
    {
        Memory* mem = context->pMemory;
        uint8_t ramMask = mem->GetRamMask();
        auto matches = [](const uint8_t* p, const char* pat, int len)
        {
            for (int i = 0; i < len; i++)
                if (p[i] != (uint8_t)pat[i])
                    return false;
            return true;
        };
        int titleHits = 0, princeHits = 0, descHits = 0, dpbHits = 0;
        for (uint16_t page = 0; page <= ramMask; page++)
        {
            uint8_t* base = mem->RAMPageAddress(page);
            if (!base)
                continue;
            for (uint32_t i = 0; i + 30 <= 0x4000; i++)
            {
                if (titleHits < 8 && matches(base + i, "TITLE1", 6))
                {
                    std::cout << "[RAM] TITLE1 @page " << page << " +" << Hex(i, 4) << "\n";
                    titleHits++;
                }
                if (princeHits < 100 && matches(base + i, "PRINCE", 6))
                {
                    princeHits++;  // summary count only (screen text can hit too)
                }
                if (descHits < 16)
                {
                    uint8_t dtyp = base[i + 1];
                    uint16_t dbytes = (uint16_t)(base[i + 9] | (base[i + 10] << 8));
                    if (dtyp >= 1 && dtyp <= 7 && base[i + 2] <= 7 &&
                        base[i + 8] >= 1 && base[i + 8] <= 64 &&
                        (dbytes == 128 || dbytes == 256 || dbytes == 512 || dbytes == 1024 ||
                         dbytes == 2048 || dbytes == 4096))
                    {
                        std::cout << "[DESC @page " << page << " +" << Hex(i, 4) << "] ";
                        for (int k = 0; k < 30; k++)
                            std::cout << Hex(base[i + k]);
                        std::cout << " DTYP=" << (int)dtyp << " DUS=" << (int)base[i + 2]
                                  << " DCYLN=" << (base[i + 6] | (base[i + 7] << 8))
                                  << " DSECTT=" << (int)base[i + 8] << " DBYTES=" << dbytes
                                  << " DALTCYL=" << (int)base[i + 11]
                                  << " DBLDR=" << (base[i + 14] | (base[i + 15] << 8))
                                  << " DBLTR=" << (base[i + 16] | (base[i + 17] << 8))
                                  << " DTRACK=" << (base[i + 18] | (base[i + 19] << 8))
                                  << " DDIRENT=" << (base[i + 21] | (base[i + 22] << 8)) << "\n";
                        descHits++;
                    }
                }
                if (dpbHits < 12)
                {
                    uint16_t spt = (uint16_t)(base[i] | (base[i + 1] << 8));
                    uint8_t bsh = base[i + 2];
                    uint16_t drm = (uint16_t)(base[i + 7] | (base[i + 8] << 8));
                    uint16_t off = (uint16_t)(base[i + 13] | (base[i + 14] << 8));
                    if (spt >= 8 && spt <= 256 && bsh >= 3 && bsh <= 7 &&
                        base[i + 3] == (uint8_t)((1u << bsh) - 1) && base[i + 4] <= 7 &&
                        drm >= 15 && drm <= 1023 && off <= 8)
                    {
                        std::cout << "[DPB @page " << page << " +" << Hex(i, 4) << "] ";
                        for (int k = 0; k < 15; k++)
                            std::cout << Hex(base[i + k]);
                        std::cout << " SPT=" << spt << " BSH=" << (int)bsh << " DSM="
                                  << (base[i + 5] | (base[i + 6] << 8)) << " DRM=" << drm
                                  << " CKS=" << (base[i + 11] | (base[i + 12] << 8)) << " OFF=" << off << "\n";
                        dpbHits++;
                    }
                }
            }
        }
        std::cout << "[RAM SCAN] TITLE1 hits=" << titleHits << " PRINCE hits=" << princeHits
                  << " DESC hits=" << descHits << " DPB hits=" << dpbHits << "\n";
    }

    // Disk-monitor call log across the DIR phase: every entry into the
    // monitor RAM interface (#F824 RQDIO / #F827 RQRES / #F82A RQSET /
    // #F82D RQCHK) with live registers and the first bytes at DE (the RQDIO
    // request block: com/bln/track/block/badr), plus disk-related BDOS calls
    // (13 reset, 14 select, 17/18 search, 26 set-DMA) - this shows exactly
    // where the CCP DIR command stops on its way to the FDC
    struct DmCall
    {
        uint16_t entry;
        uint8_t a, c;
        uint16_t de, hl;
        const char* phase;
        uint8_t req[10];
    };
    std::vector<DmCall> dmCalls;
    std::map<uint16_t, long> dmCounts;
    const char* dmPhase = "postboot";
    // Instruction trace from the FIRST DIR-phase RQDIO entry: the ROM disk
    // monitor processed all 32 directory reads with zero FDC port I/O -
    // this trace catches the early-exit branch inside the driver
    std::vector<uint16_t> dmM1;
    bool dmM1On = false;
    Memory* dmMem = context->pMemory;
    uint16_t bdosEntry = (uint16_t)(dmMem->DirectReadFromZ80Memory(6) | (dmMem->DirectReadFromZ80Memory(7) << 8));
    cpu->m1TraceHook = [&dmCalls, &dmCounts, &dmPhase, &dmM1, &dmM1On, cpu, dmMem, bdosEntry](uint16_t pc)
    {
        if (dmM1On && dmM1.size() < 4000)
            dmM1.push_back(pc);
        bool isDm = pc >= 0xF824 && pc <= 0xF82D && ((pc - 0xF824) % 3) == 0;
        bool isBdos = pc == bdosEntry && (cpu->c == 13 || cpu->c == 14 || cpu->c == 17 || cpu->c == 18 || cpu->c == 26);
        if (!isDm && !isBdos)
            return;
        dmCounts[isBdos ? 6 : pc]++;
        if (pc == 0xF824 && dmPhase[0] == 'd' && !dmM1On)
            dmM1On = true;  // record from the first DIR-phase RQDIO on
        if (dmCalls.size() >= 400)
            return;
        DmCall call = {};
        call.entry = pc;
        call.a = cpu->a;
        call.c = cpu->c;
        call.de = cpu->de;
        call.hl = cpu->hl;
        call.phase = dmPhase;
        for (int i = 0; i < 10; i++)
            call.req[i] = dmMem->DirectReadFromZ80Memory(static_cast<uint16_t>(cpu->de + i));
        dmCalls.push_back(call);
    };

    // Guest-side port trace for the DIR phase, armed BEFORE typing: DIR
    // without real disk I/O finishes within a few frames of ENTER (the
    // previous after-ENTER trace started too late to see anything)
    struct BusEvent
    {
        uint16_t port;
        uint8_t value;
        uint16_t pc;
    };
    std::vector<BusEvent> busEvents;
    std::map<uint16_t, long> outByPort;
    std::map<uint16_t, long> inByPort;
    std::vector<std::string> dirFdcIo;
    uint8_t lastDirIn[256] = {};
    cpu->busTraceHook = [&busEvents, &outByPort, &inByPort, &dirFdcIo, &lastDirIn, cpu](char type, uint16_t port, uint8_t value)
    {
        if (type == 'O')
        {
            outByPort[port]++;
            if (busEvents.size() < 400)
                busEvents.push_back({port, value, cpu->pc});
        }
        else if (type == 'I')
        {
            inByPort[port]++;
            uint16_t low = port & 0xFF;
            if ((low == 0x1F || low == 0x3F || low == 0x5F || low == 0x7F || low == 0xFF) &&
                value != lastDirIn[low] && dirFdcIo.size() < 200)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "I %04X=%02X pc=%04X", port, value, cpu->pc);
                dirFdcIo.push_back(buf);
                lastDirIn[low] = value;
            }
        }
    };

    dmPhase = "dir";
    // The monitor bound A: to its electronic (RAM) disk channel: the floppy
    // without a CP/M system-descriptor sector (55 AA magic at track 0
    // sector 1) is NOT the system disk and mounts as B: - DIR B: must list
    // the on-disk catalog of prince.trd
    TypeCommand(emulator, "DIR B:");
    PressEnter(emulator);

    // The tail of the catalog (READ.ME is the last user-0 entry) settles
    // only after all 8 catalog sectors came back from the FDC
    EmulatorTestHelper::RunUntil(
        emulator.get(),
        [&] { return StripSpaces(DecodeTextRows(context, 0, 24)).find("README") != std::string::npos; }, 900);

    std::string screen = DecodeTextRows(context, 0, 24);
    std::string stripped = StripSpaces(screen);
    std::cout << "[DIR]\n" << screen;

    // Guest-side port summary for the DIR phase: which ports the monitor
    // actually talked to (aggregated) plus the first raw events
    cpu->busTraceHook = nullptr;
    cpu->m1TraceHook = nullptr;
    std::cout << "[DM entry counts]\n";
    for (auto& kv : dmCounts)
    {
        std::cout << "  #" << (kv.first == 6 ? "BDOS" : Hex(kv.first, 4)) << " x" << kv.second << "\n";
    }
    for (size_t i = 0; i < dmCalls.size() && i < 120; i++)
    {
        const DmCall& call = dmCalls[i];
        std::cout << "[DM " << call.phase << " #" << Hex(call.entry, 4) << "] A=" << Hex(call.a)
                  << " C=" << Hex(call.c) << " DE=" << Hex(call.de, 4) << " HL=" << Hex(call.hl, 4) << " req=";
        for (int k = 0; k < 10; k++)
            std::cout << Hex(call.req[k]);
        std::cout << "\n";
    }
    // Raw instruction flow from the first DIR-phase RQDIO (offline disasm
    // against the ROM dumps): where the driver bails before the WD1793
    std::cout << "[DM M1 from first RQDIO] " << dmM1.size() << " instrs\n";
    for (size_t i = 0; i < dmM1.size(); i++)
    {
        std::cout << Hex(dmM1[i]) << ((i & 31) == 31 ? "\n" : " ");
    }
    if (dmM1.size() % 32)
        std::cout << "\n";
    std::cout << "[DIR FDC I/O] " << dirFdcIo.size() << " value transitions\n";
    for (size_t i = 0; i < dirFdcIo.size(); i++)
    {
        std::cout << "  [" << i << "] " << dirFdcIo[i] << "\n";
    }
    std::cout << "[BUS out ports]\n";
    for (auto& kv : outByPort)
    {
        std::cout << "  #" << Hex(kv.first, 4) << " x" << kv.second << "\n";
    }
    std::cout << "[BUS in ports]\n";
    for (auto& kv : inByPort)
    {
        std::cout << "  #" << Hex(kv.first, 4) << " x" << kv.second << "\n";
    }
    for (size_t i = 0; i < busEvents.size() && i < 60; i++)
    {
        std::cout << "[BUS " << i << "] W #" << Hex(busEvents[i].port, 4) << " = " << Hex(busEvents[i].value) << "\n";
    }

    // Every user-0 catalog entry, name+ext concatenated (as DIR renders them)
    static const char* kCatalog[] = {
        "TITLE1DAT", "TITLE2DAT", "PRCOM", "PRINCE1OVL", "PRINCE2OVL", "PRINCEHOF",
        "PRINCE0FNT", "PRINCE1FNT", "PVDAT", "PV1DAT", "DATASEGDAT", "PRINCE2FNT",
        "PRINCE3FNT", "PRINCE4FNT", "PRINCE5FNT", "PRINCE6FNT", "KIDDAT", "SKELDAT",
        "VIZIERDAT", "PALACEDAT", "DUNGEONDAT", "GUARDDAT", "GUARD1DAT", "GUARD2DAT",
        "LEVELSDAT", "FATDAT", "SHADOWDAT", "PRINCE7FNT", "PRINCEDAT", "PRINCE8FNT",
        "PRINCE9FNT", "PRINCEAFNT", "PRINCEBFNT", "INFODAT", "SOUNDDAT", "PRINCE3OVL",
        "PR2COM", "PRINCE4OVL", "PRINCEDOC", "PRINCENFO", "README",
    };
    for (const char* name : kCatalog)
    {
        EXPECT_NE(stripped.find(name), std::string::npos) << "catalog entry missing from DIR: " << name;
    }

    // The BUG entry: name renders clean, the 0xFD extension byte decodes as
    // '.' in the text screen - the exact "garbage but no errors" the disk
    // itself contains
    EXPECT_NE(stripped.find("BUG"), std::string::npos) << "BUG entry missing";

    // Hidden user-15 file must not be listed
    EXPECT_EQ(stripped.find("CONFIG"), std::string::npos) << "user-15 CONFIG must stay hidden";

    // The prompt returned and no disk subsystem error is displayed
    EXPECT_NE(stripped.rfind("A>"), std::string::npos);
    EXPECT_EQ(stripped.find("BIOSERROR"), std::string::npos) << "disk monitor error";
    EXPECT_EQ(stripped.find("BDOSERR"), std::string::npos) << "BDOS error";
}

TEST_F(ATM710CpmBoot_Test, Pr2GameRunsOn1024)
{
    // Boot-bound test: it boots a real ROM, a real game disk and a real
    // transient program - far over the 50 ms budget, turbo keeps it fast
    VerifyPr2GameRuns(1024);
}

TEST_F(ATM710CpmBoot_Test, Pr2GameRunsOn512)
{
    // Same launch on the minimum RAM the game needs: the electronic disk
    // shrinks but the floppy B: path is unchanged
    VerifyPr2GameRuns(512);
}


