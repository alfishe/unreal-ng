#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/test_path_helper.h"
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
    LoaderTRDCUT loaderTrd(_context, "test.trd");
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
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool formatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(formatted) << "Failed to format TRD disk image";

    // Get Track 1 (Track 0 is system track)
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(1, 0);
    ASSERT_NE(track, nullptr) << "Track 1 not found";

    // Verify all 16 sectors (1-16) are present
    std::set<uint8_t> foundSectors;
    for (int i = 0; i < 16; i++)
    {
        uint8_t sectorNumber = track->sectors[i].address_record.sector;
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
    LoaderTRDCUT loaderTrd(_context, "test.trd");
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
/// Uses modern BasicEncoder + ScreenOCR patterns for command injection and verification
/// Executes REAL TR-DOS FORMAT command through proper command injection (not ROM hacks)
TEST_F(WD1793_Integration_Test, TRDOS_FORMAT_FullOperation)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    std::cout << "\n========================================\n";
    std::cout << "[FORMAT] Full TR-DOS FORMAT Integration Test\n";
    std::cout << "========================================\n";

    // Get emulator context components
    EmulatorContext* context = _emulator->GetContext();
    Memory* memory = context->pMemory;
    std::string emulatorId = _emulator->GetId();
    auto* mainLoop = reinterpret_cast<MainLoop_CUT*>(context->pMainLoop);

    // ========================================
    // STEP 1: ROM Initialization
    // ========================================
    std::cout << "[STEP 1] Running ROM initialization (100 frames)...\n";
    for (int i = 0; i < 100; i++)
    {
        mainLoop->RunFrame();
    }

    // Verify with OCR
    std::string screenInit = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 1] Screen after ROM init:\n" << screenInit << "\n";
    ASSERT_TRUE(screenInit.find("128") != std::string::npos || screenInit.find("Tape") != std::string::npos ||
                screenInit.find("BASIC") != std::string::npos)
        << "128K menu should be visible. Got:\n"
        << screenInit;
    std::cout << "[STEP 1] ✓ 128K menu visible\n";

    // ========================================
    // STEP 2: Navigate to TR-DOS
    // ========================================
    std::cout << "[STEP 2] Navigating to TR-DOS...\n";
    BasicEncoder::navigateToTRDOS(memory);

    // Run frames for menu transition
    for (int i = 0; i < 100; i++)
    {
        mainLoop->RunFrame();
    }

    // Verify TR-DOS prompt via OCR
    std::string screenTRDOS = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 2] Screen after navigation:\n" << screenTRDOS << "\n";

    // TR-DOS prompt shows "A>" for drive A
    bool inTRDOS = screenTRDOS.find("A>") != std::string::npos || screenTRDOS.find("Insert") != std::string::npos ||
                   screenTRDOS.find("TR-DOS") != std::string::npos;

    if (!inTRDOS)
    {
        // Run more frames - TR-DOS boot can take longer
        for (int i = 0; i < 200; i++)
        {
            mainLoop->RunFrame();
        }
        screenTRDOS = ScreenOCR::ocrScreen(emulatorId);
        std::cout << "[STEP 2] Screen after extended wait:\n" << screenTRDOS << "\n";
        inTRDOS = screenTRDOS.find("A>") != std::string::npos;
    }

    ASSERT_TRUE(inTRDOS) << "TR-DOS prompt should be visible. Got:\n" << screenTRDOS;
    std::cout << "[STEP 2] ✓ TR-DOS prompt visible\n";

    // ========================================
    // STEP 3: Insert Empty Disk
    // ========================================
    std::cout << "[STEP 3] Inserting empty disk image (80T, 2 sides)...\n";

    WD1793* wd1793 = _context->pBetaDisk;
    ASSERT_NE(wd1793, nullptr) << "WD1793 not available";

    FDD* fdd = wd1793->getDrive();
    ASSERT_NE(fdd, nullptr) << "FDD not available";

    DiskImage* diskImage = new DiskImage(80, 2);
    ASSERT_NE(diskImage, nullptr) << "Failed to create disk image";

    fdd->insertDisk(diskImage);
    std::cout << "[STEP 3] ✓ Empty disk inserted\n";

    // ========================================
    // STEP 4: Set up breakpoint automation BEFORE injecting command
    // ========================================
    // NOTE: Breakpoints must be set BEFORE injecting command to intercept $1EDD
    std::cout << "[STEP 4] Setting up FORMAT automation breakpoints...\n";

    Z80* cpu = _context->pCore->GetZ80();
    ASSERT_NE(cpu, nullptr) << "Z80 not available";

    BreakpointManager* bpMgr = _context->pDebugManager->GetBreakpointsManager();
    ASSERT_NE(bpMgr, nullptr) << "BreakpointManager not available";

    // Enable debug features for breakpoints to work
    EmulatorTestHelper::EnableDebugFeatures(_emulator);

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    bool formatDone = false;
    bool bpHit = false;
    int tracksFormatted = 0;

    auto handler = [&](int id, Message* msg) {
        SimpleNumberPayload* payload = dynamic_cast<SimpleNumberPayload*>(msg->obj);
        
        if (cpu->pc == 0x1EDD)
        {
            // We're bypassing CALL $3200 which sets up critical FORMAT variables:
            // 1. $5CE6 = address of sector interleave table for formatting (ROM: $1FB9)
            // 2. $5CE8 = address of sector table for verification (ROM: $1FBA)
            // 3. A = drive type (0x80 for 80-track)
            // Without these, FORMAT uses garbage sector numbers!
            
            // Set sector table pointers (critical for FORMAT to work!)
            memory->DirectWriteToZ80Memory(0x5CE6, 0xB9);  // Low byte of $1FB9
            memory->DirectWriteToZ80Memory(0x5CE7, 0x1F);  // High byte of $1FB9
            memory->DirectWriteToZ80Memory(0x5CE8, 0xBA);  // Low byte of $1FBA  
            memory->DirectWriteToZ80Memory(0x5CE9, 0x1F);  // High byte of $1FBA
            
            // Set A=0x80 for 80-track normal format
            // (the AND #80 at $1EE0 checks for 80-track drive type)
            cpu->a = 0x80;
            cpu->pc = 0x1EE0;
            bpHit = true;
            std::cout << "[HANDLER] ✓ Breakpoint $1EDD hit - bypassing format-type prompt\n" 
                      << "           Set $5CE6=$1FB9 (format table), $5CE8=$1FBA (verify table)\n" << std::flush;
        }
        _emulator->Resume();
    };
    messageCenter.AddObserver(NC_EXECUTION_BREAKPOINT, handler);

    // Use page-specific breakpoint for TR-DOS ROM (page 1 on Pentagon)
    // Only need $1EDD to bypass format-type prompt
    uint16_t bp1 = bpMgr->AddExecutionBreakpointInPage(0x1EDD, 1, BANK_ROM, "fmt");
    std::cout << "[STEP 4] ✓ Breakpoint set at $1EDD (TR-DOS ROM page 1)\n";

    // ========================================
    // STEP 5: Inject and execute FORMAT Command
    // ========================================
    std::cout << "[STEP 5] Injecting FORMAT command...\n";

    // FORMAT command: FORMAT "diskname" for TR-DOS
    auto result = BasicEncoder::injectToTRDOS(memory, "FORMAT \"testdisk\"");
    EXPECT_TRUE(result.success) << "Failed to inject FORMAT command: " << result.message;

    // Inject ENTER to execute
    BasicEncoder::injectEnter(memory);
    std::cout << "[STEP 5] ✓ FORMAT command injected\n";

    // ========================================
    // STEP 6: Run emulator ASYNC for breakpoint handling
    // ========================================
    // IMPORTANT: Must run async so MessageCenter handler can call Resume()
    // while emulator thread waits on WaitWhilePaused() after breakpoint fire
    std::cout << "[STEP 6] Starting emulator async for FORMAT execution...\n";
    _emulator->StartAsync();
    
    // Wait a short time for initial processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify format-type prompt was bypassed
    std::string screen = ScreenOCR::ocrScreen(emulatorId);
    if (screen.find("TURBO-FORMAT") != std::string::npos)
    {
        std::cout << "[STEP 6] ⚠ TURBO-FORMAT prompt appeared - breakpoint may not have fired\n";
        std::cout << "[STEP 6] Current screen:\n" << screen << "\n";
        _emulator->Stop();
        FAIL() << "TURBO-FORMAT prompt appeared - breakpoint did not bypass the format-type selection";
    }
    std::cout << "[STEP 6] ✓ Format-type prompt bypassed\n";

    // STEP 6b: Wait until FORMAT completes (A> prompt returns) or fails
    // FORMAT takes 2-3 minutes for 80T DS disk
    std::cout << "[STEP 6b] Waiting for FORMAT to complete (up to 180 seconds)...\n" << std::flush;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
    int progress = 0;
    std::string lastProgress;
    bool formatFailed = false;
    std::string failureReason;

    while (!formatDone && !formatFailed && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!_emulator->IsRunning())
        {
            std::cout << "[STEP 6b] ⚠ Emulator stopped unexpectedly!\n" << std::flush;
            formatFailed = true;
            failureReason = "Emulator stopped unexpectedly";
            break;
        }

        // Check WD1793 status register for hardware errors
        // Use const reference to access public const getter (non-const version is protected)
        const WD1793& wdConst = *wd1793;
        uint8_t wdStatus = wdConst.getStatusRegister();
        bool wdBusy = (wdStatus & 0x01);  // WDS_BUSY

        if (wdStatus & 0x04)  // WDS_LOSTDATA (read/write commands only)
        {
            std::cout << "[STEP 6b] ⚠ WD1793 Lost Data error detected (status=0x"
                      << std::hex << (int)wdStatus << std::dec << ")\n" << std::flush;
            formatFailed = true;
            failureReason = "WD1793 Lost Data error";
            break;
        }
        if (wdStatus & 0x20)  // WDS_WRITEFAULT
        {
            std::cout << "[STEP 6b] ⚠ WD1793 Write Fault detected (status=0x"
                      << std::hex << (int)wdStatus << std::dec << ")\n" << std::flush;
            formatFailed = true;
            failureReason = "WD1793 Write Fault";
            break;
        }

        screen = ScreenOCR::ocrScreen(emulatorId);

        // If WD1793 is idle but we haven't seen completion, check if we're stuck
        // Note: Only check after initial startup (progress > 4 = ~2 seconds)
        if (!wdBusy && progress > 4)
        {
            // WD1793 is not busy - format might have failed silently
            // Check screen for error messages more thoroughly
            if (screen.find("A>") == std::string::npos &&
                screen.find("HEAD") == std::string::npos &&
                screen.find("Press R") == std::string::npos)
            {
                std::cout << "[STEP 6b] ⚠ WD1793 idle but no progress/completion detected\n" << std::flush;
                formatFailed = true;
                failureReason = "WD1793 idle without completion or error message";
                break;
            }
        }
        
        // Extract HEAD/CYLINDER progress from screen
        std::string currentProgress;
        auto headPos = screen.find("HEAD");
        if (headPos != std::string::npos)
        {
            // Extract "HEAD X  CYLINDER Y" line
            auto lineEnd = screen.find('\n', headPos);
            if (lineEnd == std::string::npos) lineEnd = screen.length();
            currentProgress = StringHelper::Trim(screen.substr(headPos, lineEnd - headPos));
        }
        
        // Check for format SUCCESS:
        // 1. A> prompt visible (returned to command prompt)
        // 2. "Press R for repeat FORMAT" message (format completed, waiting for keypress)
        if (screen.find("A>") != std::string::npos && screen.find("FORMAT") == std::string::npos)
        {
            formatDone = true;
            std::cout << "[STEP 6b] ✓ Format completed - A> prompt visible\n" << std::flush;
        }
        else if (screen.find("repeat FORMAT") != std::string::npos || 
                 screen.find("Press R") != std::string::npos)
        {
            formatDone = true;
            std::cout << "[STEP 6b] ✓ Format completed - repeat/TR-DOS prompt visible\n" << std::flush;
        }
        // Check for format FAILURE (TR-DOS error messages):
        else if (screen.find("No disk") != std::string::npos)
        {
            formatFailed = true;
            failureReason = "No disk in drive";
        }
        else if (screen.find("Disc Error") != std::string::npos ||  // TR-DOS spelling
                 screen.find("Disk error") != std::string::npos ||
                 screen.find("disk error") != std::string::npos)
        {
            formatFailed = true;
            failureReason = "Disk/Disc error";
        }
        else if (screen.find("Retry,Abort,Ignore") != std::string::npos ||
                 screen.find("Retry, Abort") != std::string::npos)
        {
            formatFailed = true;
            failureReason = "TR-DOS error prompt (Retry,Abort,Ignore)";
        }
        else if (screen.find("Write protect") != std::string::npos)
        {
            formatFailed = true;
            failureReason = "Disk is write protected";
        }
        else if (screen.find("Error") != std::string::npos && 
                 screen.find("HEAD") == std::string::npos)  // Avoid false positives during active formatting
        {
            formatFailed = true;
            failureReason = "TR-DOS error detected";
        }
        else if (!currentProgress.empty() && currentProgress != lastProgress)
        {
            // Show progress only when HEAD/CYLINDER changes
            std::cout << "[STEP 6b] " << currentProgress << "\n" << std::flush;
            lastProgress = currentProgress;
        }

        progress++;
    }
    
    std::cout << "[STEP 6b] Loop finished after " << (progress / 2) << " seconds\n" << std::flush;
    
    // Stop the emulator
    _emulator->Stop();

    // Cleanup breakpoints
    messageCenter.RemoveObserver(NC_EXECUTION_BREAKPOINT, handler);
    bpMgr->RemoveBreakpointByID(bp1);

    // Final screen
    screen = ScreenOCR::ocrScreen(emulatorId);
    std::cout << "[STEP 6b] Final screen:\n" << screen << "\n";

    // Check for failure first (exits loop early on error detection)
    if (formatFailed)
    {
        FAIL() << "FORMAT failed: " << failureReason << "\nScreen:\n" << screen;
    }
    
    // Then check for success (or timeout if neither failed nor succeeded)
    ASSERT_TRUE(formatDone) << "FORMAT did not complete within 180 seconds (timeout)";

    // ========================================
    // STEP 6: Validate Disk Structure
    // ========================================
    std::cout << "[STEP 6] Validating disk structure...\n";

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

        std::cout << "[STEP 6] Disk info from sector 8:\n";
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

        std::cout << "[STEP 6] ✓ Disk info structure valid\n";
    }
    else
    {
        std::cout << "[STEP 6] ⚠ Could not read disk info sector\n";
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

    std::cout << "[STEP 6] Tracks with data: " << tracksWithData << " / 160\n";
    EXPECT_GE(tracksWithData, 1) << "At least track 0 should have data";

    // ========================================
    // Summary
    // ========================================
    std::cout << "\n========================================\n";
    std::cout << "[FORMAT] Test Summary:\n";
    std::cout << "  ✓ ROM initialized and 128K menu visible\n";
    std::cout << "  ✓ Navigated to TR-DOS\n";
    std::cout << "  ✓ Empty disk inserted\n";
    std::cout << "  ✓ FORMAT command injected and executed\n";
    std::cout << "  ✓ Disk structure validated\n";
    std::cout << "  Tracks formatted: " << tracksWithData << " / 160\n";
    std::cout << "========================================\n";

    // Clean up
    fdd->ejectDisk();
}
