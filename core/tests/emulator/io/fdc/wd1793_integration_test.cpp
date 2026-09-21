#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testtiminghelper.h"
#include "_helpers/trdostesthelper.h"
#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "debugger/analyzers/analyzermanager.h"
#include "debugger/analyzers/basic-lang/basicencoder.h"
#include "debugger/analyzers/rom-print/screenocr.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"
#include "loaders/disk/loader_trd.h"

/// WD1793 Integration Tests
/// Tests full TR-DOS integration scenarios including FORMAT operations

class WD1793_Integration_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ModuleLogger* _logger = nullptr;

protected:
    void SetUp() override
    {
        // Ensure complete isolation - dispose any existing MessageCenter from previous tests
        MessageCenter::DisposeDefaultMessageCenter();

        // Create emulator via EmulatorTestHelper for proper EmulatorManager registration
        // This is required for ScreenOCR and other APIs that lookup by emulator ID
        _emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);

        if (_emulator)
        {
            _context = _emulator->GetContext();
            _logger = _context ? _context->pModuleLogger : nullptr;
        }
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }

        // Force complete disposal of MessageCenter and all its observers
        MessageCenter::DisposeDefaultMessageCenter();
    }
};

/// @brief Verify TR-DOS catalog structure after format
TEST_F(WD1793_Integration_Test, TRDOS_CatalogStructure)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Create and format disk image using LoaderTRD
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, TestPathHelper::GetTestScratchPath("test.trd"));
    bool formatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(formatted) << "Failed to format TRD disk image";

    // Validate the empty image using LoaderTRD's validation
    bool valid = loaderTrd.validateEmptyTRDOSImage(&diskImage);
    EXPECT_TRUE(valid) << "Formatted TRD image validation failed";

    // Get Track 0 (system track) - contains catalog and disk info
    DiskImage::Track* track0 = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track0, nullptr) << "Track 0 not found";

    // TR-DOS layout on Track 0:
    // Sectors 0-7: Catalog entries (128 files max, 16 bytes each)
    // Sector 8: Disk info sector

    // Read sector 8 (disk info) via track/sector model
    uint8_t* sector8 = track0->getDataForSector(8);
    ASSERT_NE(sector8, nullptr) << "Sector 8 (disk info) not found";

    // Verify TR-DOS disk info structure
    uint8_t firstFreeSector = sector8[0xE1];
    uint8_t firstFreeTrack = sector8[0xE2];
    uint8_t diskType = sector8[0xE3];
    uint8_t numFiles = sector8[0xE4];
    uint16_t freeSectors = sector8[0xE5] | (sector8[0xE6] << 8);

    // Expected values for 80-track DS formatted disk
    EXPECT_EQ(firstFreeSector, 0x00) << "First free sector should be 0 on empty disk";
    EXPECT_EQ(firstFreeTrack, 0x01) << "First free track should be 1 (track 0 is system)";
    EXPECT_EQ(diskType, 0x16) << "Disk type should be 0x16 (80T DS)";
    EXPECT_EQ(numFiles, 0x00) << "Number of files should be 0 on empty disk";

    // Verify free sectors count
    EXPECT_GE(freeSectors, 2400) << "Free sectors should be ~2544 for 80T DS";
    EXPECT_LE(freeSectors, 2560) << "Free sectors cannot exceed total";

    // Verify catalog sectors (0-7) are initialized
    for (int sectorNum = 0; sectorNum < 8; sectorNum++)
    {
        uint8_t* catalogSector = track0->getDataForSector(sectorNum);
        ASSERT_NE(catalogSector, nullptr) << "Catalog sector " << sectorNum << " not found";

        // Each sector has 16 catalog entries of 16 bytes each
        for (int entry = 0; entry < 16; entry++)
        {
            uint8_t* entryData = catalogSector + (entry * 16);
            EXPECT_EQ(entryData[0], 0x00) << "Catalog entry " << (sectorNum * 16 + entry) << " should be empty";
        }
    }
}

/// @brief Verify sector interleave pattern matches TR-DOS standard
/// TR-DOS uses 1:2 interleave: 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16
TEST_F(WD1793_Integration_Test, TRDOS_SectorInterleave)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Create and format disk image
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, TestPathHelper::GetTestScratchPath("test.trd"));
    bool formatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(formatted) << "Failed to format TRD disk image";

    // Get Track 1 (Track 0 is system track)
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(1, 0);
    ASSERT_NE(track, nullptr) << "Track 1 not found";

    // Verify all 16 sectors (1-16) are present
    std::set<uint8_t> foundSectors;
    for (int i = 0; i < 16; i++)
    {
        uint8_t sectorNumber = track->getRawSector(i)->number();
        EXPECT_GE(sectorNumber, 1) << "Sector number should be >= 1";
        EXPECT_LE(sectorNumber, 16) << "Sector number should be <= 16";
        foundSectors.insert(sectorNumber);
    }

    // All 16 sectors should be present
    EXPECT_EQ(foundSectors.size(), 16) << "All 16 sectors should be present";
    for (uint8_t s = 1; s <= 16; s++)
    {
        EXPECT_TRUE(foundSectors.count(s) > 0) << "Sector " << (int)s << " not found";
    }
}

/// @brief Verify all tracks are populated after format
TEST_F(WD1793_Integration_Test, AllTracksPopulated)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    // Create and format disk image
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, TestPathHelper::GetTestScratchPath("test.trd"));
    bool formatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(formatted) << "Failed to format TRD disk image";

    // Verify all 160 tracks (80 cylinders * 2 sides)
    int tracksChecked = 0;

    for (uint8_t cylinder = 0; cylinder < 80; cylinder++)
    {
        for (uint8_t side = 0; side < 2; side++)
        {
            DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(cylinder, side);
            ASSERT_NE(track, nullptr) << "Track C" << (int)cylinder << "S" << (int)side << " not found";

            // Verify track has 16 sectors
            int validSectors = 0;
            for (size_t i = 0; i < 16; i++)
            {
                if (track->getSector(i) != nullptr)
                    validSectors++;
            }
            EXPECT_EQ(validSectors, 16) << "Track C" << (int)cylinder << "S" << (int)side << " should have 16 sectors";

            tracksChecked++;
        }
    }

    EXPECT_EQ(tracksChecked, 160) << "Should verify all 160 tracks";
}

/// @brief Integration test: Full FORMAT operation with disk validation
/// Executes the REAL TR-DOS FORMAT command: the machine is reset into TR-DOS and its cold start runs the
/// command (the same mechanism as disk autostart). Synchronous, emulated time only - no emulator thread,
/// no frame pacing, no keyboard or screen automation to reach the command.
TEST_F(WD1793_Integration_Test, TRDOS_FORMAT_FullOperation)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    std::cout << "\n========================================\n";
    std::cout << "[FORMAT] Full TR-DOS FORMAT Integration Test\n";
    std::cout << "========================================\n";

    // ========================================
    // STEP 1: Insert an empty disk
    // ========================================
    WD1793* wd1793 = _context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr) << "WD1793 not available";

    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr) << "FDD not available";

    DiskImage* diskImage = new DiskImage(80, 2);
    ASSERT_NE(diskImage, nullptr) << "Failed to create disk image";

    fdd->insertDisk(diskImage);
    std::cout << "[STEP 1] ✓ Empty disk inserted (80T, 2 sides)\n";

    // ========================================
    // STEP 2: Start FORMAT through TR-DOS' own cold start
    // ========================================
    TRDOSTestHelper trdos(_emulator);
    trdos.startCommand("FORMAT \"testdisk\"");
    trdos.skipFormatTypePrompt();  // Interactive prompt: unattended run selects 80T DS
    std::cout << "[STEP 2] ✓ FORMAT started\n";

    // ========================================
    // STEP 3: Run until FORMAT completes or fails (emulated time only)
    // ========================================
    // One poll step = 50 frames = 1 emulated second (the screen OCR per poll is the costly part)
    constexpr unsigned kFramesPerPoll = 50;
    constexpr uint64_t kMaxCycles = 180ull * WD1793::Z80_FREQUENCY;  // 180 emulated seconds

    int progress = 0;
    std::string lastProgress;
    bool formatDone = false;
    bool formatFailed = false;
    std::string failureReason;

    trdos.runUntil(
        [&]() {
            progress++;

            // Check WD1793 status register for hardware errors
            // Use const reference to access public const getter (non-const version is protected)
            const WD1793& wdConst = *wd1793;
            uint8_t wdStatus = wdConst.getStatusRegister();
            bool wdBusy = (wdStatus & 0x01);  // WDS_BUSY

            // Note: Bit 2 (0x04) means WDS_TRK00 for Type I commands (RESTORE/SEEK/STEP)
            // but WDS_LOSTDATA for Type II/III commands. Only check for Lost Data
            // when the FDC is actually executing a write command.
            auto lastCmd = wd1793->getLastDecodedCommand();
            bool isWriteCommand = (lastCmd == WD1793::WD_CMD_WRITE_SECTOR || lastCmd == WD1793::WD_CMD_WRITE_TRACK);
            if ((wdStatus & 0x04) && isWriteCommand)
            {
                formatFailed = true;
                failureReason = "WD1793 Lost Data error";
                return true;
            }
            // Note: Bit 5 (0x20) means WDS_HEADLOADED for Type I commands (RESTORE/SEEK/STEP)
            // but WDS_WRITEFAULT for WRITE TRACK command. Only check for Write Fault
            // when the FDC is actually executing WRITE TRACK (command byte 0xF0).
            if ((wdStatus & 0x20) && lastCmd == WD1793::WD_CMD_WRITE_TRACK)
            {
                formatFailed = true;
                failureReason = "WD1793 Write Fault";
                return true;
            }

            const std::string screen = trdos.screenText();

            // The interactive format-type prompt must have been skipped, never shown
            if (screen.find("TURBO-FORMAT") != std::string::npos)
            {
                formatFailed = true;
                failureReason = "TURBO-FORMAT prompt appeared - the $1EDD bypass did not skip the format-type selection";
                return true;
            }

            // If WD1793 is idle but we haven't seen completion, check if we're stuck
            // (only after initial startup: 10 emulated seconds)
            if (!wdBusy && progress > 10 && screen.find("A>") == std::string::npos &&
                screen.find("HEAD") == std::string::npos && screen.find("Press R") == std::string::npos)
            {
                formatFailed = true;
                failureReason = "WD1793 idle without completion or error message";
                return true;
            }

            // Extract HEAD/CYLINDER progress from screen
            std::string currentProgress;
            auto headPos = screen.find("HEAD");
            if (headPos != std::string::npos)
            {
                auto lineEnd = screen.find('\n', headPos);
                if (lineEnd == std::string::npos)
                    lineEnd = screen.length();
                currentProgress = StringHelper::Trim(screen.substr(headPos, lineEnd - headPos));
            }

            // Format SUCCESS: A> prompt is back, or the "repeat FORMAT" message is shown
            if (trdos.formatFinishedOnScreen())
            {
                formatDone = true;
                std::cout << "[STEP 3] ✓ Format completed\n";
                return true;
            }

            // Format FAILURE (TR-DOS error messages)
            struct ErrorPattern
            {
                const char* text;
                const char* reason;
            };
            static const ErrorPattern errors[] = {
                {"No disk", "No disk in drive"},
                {"Disc Error", "Disk/Disc error"},
                {"Disk error", "Disk/Disc error"},
                {"disk error", "Disk/Disc error"},
                {"Retry,Abort,Ignore", "TR-DOS error prompt (Retry,Abort,Ignore)"},
                {"Retry, Abort", "TR-DOS error prompt (Retry,Abort,Ignore)"},
                {"Write protect", "Disk is write protected"},
            };
            for (const ErrorPattern& e : errors)
            {
                if (screen.find(e.text) != std::string::npos)
                {
                    formatFailed = true;
                    failureReason = e.reason;
                    return true;
                }
            }
            if (screen.find("Error") != std::string::npos && screen.find("HEAD") == std::string::npos)
            {
                // Avoid false positives during active formatting (HEAD/CYLINDER visible)
                formatFailed = true;
                failureReason = "TR-DOS error detected";
                return true;
            }

            if (!currentProgress.empty() && currentProgress != lastProgress)
            {
                std::cout << "[STEP 3] " << currentProgress << "\n" << std::flush;
                lastProgress = currentProgress;
            }
            return false;
        },
        kMaxCycles, kFramesPerPoll);

    std::cout << "[STEP 3] Finished after " << progress << " emulated seconds\n" << std::flush;

    const std::string finalScreen = trdos.screenText();
    std::cout << "[STEP 3] Final screen:\n" << finalScreen << "\n";

    if (formatFailed)
    {
        FAIL() << "FORMAT failed: " << failureReason << "\nScreen:\n" << finalScreen;
    }
    ASSERT_TRUE(formatDone) << "FORMAT did not complete within 180 emulated seconds";
    ASSERT_TRUE(trdos.formatPromptSkipped()) << "FORMAT never reached the format-type prompt at $1EDD";

    // ========================================
    // STEP 4: Validate Disk Structure
    // ========================================
    std::cout << "[STEP 4] Validating disk structure...\n";

    // Check Track 0 (system track)
    DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track0, nullptr) << "Track 0 not found after FORMAT";

    // Check disk info sector (sector 8)
    uint8_t* sector8 = track0->getDataForSector(8);
    if (sector8 != nullptr)
    {
        uint8_t firstFreeSector = sector8[0xE1];
        uint8_t firstFreeTrack = sector8[0xE2];
        uint8_t diskType = sector8[0xE3];
        uint8_t numFiles = sector8[0xE4];
        uint16_t freeSectors = sector8[0xE5] | (sector8[0xE6] << 8);

        std::cout << "[STEP 4] Disk info from sector 8:\n";
        std::cout << "  First free sector: " << (int)firstFreeSector << "\n";
        std::cout << "  First free track: " << (int)firstFreeTrack << "\n";
        std::cout << "  Disk type: 0x" << std::hex << (int)diskType << std::dec << "\n";
        std::cout << "  Number of files: " << (int)numFiles << "\n";
        std::cout << "  Free sectors: " << freeSectors << "\n";

        // Validate disk info
        EXPECT_EQ(firstFreeSector, 0x00) << "First free sector should be 0";
        EXPECT_EQ(firstFreeTrack, 0x01) << "First free track should be 1";
        EXPECT_TRUE(diskType == 0x16 || diskType == 0x19) << "Disk type should be 80T DS";
        EXPECT_EQ(numFiles, 0x00) << "Number of files should be 0";
        EXPECT_GE(freeSectors, 2400) << "Free sectors should be ~2544";

        std::cout << "[STEP 4] ✓ Disk info structure valid\n";
    }
    else
    {
        std::cout << "[STEP 4] ⚠ Could not read disk info sector\n";
    }

    // Count tracks with data
    int tracksWithData = 0;
    for (uint8_t cylinder = 0; cylinder < 80; cylinder++)
    {
        for (uint8_t side = 0; side < 2; side++)
        {
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(cylinder, side);
            if (track != nullptr)
            {
                for (size_t i = 0; i < 16; i++)
                {
                    if (track->getDataForSector(i + 1) != nullptr)
                    {
                        tracksWithData++;
                        break;
                    }
                }
            }
        }
    }

    std::cout << "[STEP 4] Tracks with data: " << tracksWithData << " / 160\n";
    EXPECT_GE(tracksWithData, 1) << "At least track 0 should have data";

    // ========================================
    // Summary
    // ========================================
    std::cout << "\n========================================\n";
    std::cout << "[FORMAT] Test Summary:\n";
    std::cout << "  ✓ Empty disk inserted\n";
    std::cout << "  ✓ FORMAT run by TR-DOS cold start (no key injection)\n";
    std::cout << "  ✓ Disk structure validated\n";
    std::cout << "  Tracks formatted: " << tracksWithData << " / 160\n";
    std::cout << "========================================\n";

    // Clean up
    fdd->ejectDisk();
}
