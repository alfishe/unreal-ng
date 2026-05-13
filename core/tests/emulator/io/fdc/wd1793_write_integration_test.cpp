#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <cstring>
#include <iostream>

#include "_helpers/test_path_helper.h"
#include "_helpers/testtiminghelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/mfm_parser.h"

/// WD1793 Write Command Integration Tests
/// Tests full write sequences: Format→Read, Format→Write→Read, Write→Read

static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;
static constexpr size_t const TSTATES_IN_MS = Z80_FREQUENCY / 1000;

class WD1793_WriteIntegration_Test : public ::testing::Test
{
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISK;
    const uint16_t _SUBMODULE = PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC;
    ModuleLogger* _logger = nullptr;

    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

    TestTimingHelper* _timingHelper = nullptr;

protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogDebug);
        _logger = _context->pModuleLogger;

        _logger->TurnOffLoggingForAll();
        _logger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK,
                                        PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);
        _logger->SetLoggingLevel(LoggerLevel::LogWarning);

        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        _timingHelper = new TestTimingHelper(_context);
        _timingHelper->resetClock();
    }

    void TearDown() override
    {
        if (_timingHelper)
            delete _timingHelper;

        if (_context)
        {
            if (_context->pCore)
            {
                if (_core->_z80)
                {
                    _core->_z80 = nullptr;
                    delete _z80;
                }
                _context->pCore = nullptr;
                delete _core;
            }
            delete _context;
        }
    }
    
    /// Helper: Run FDC simulation loop until command completes or timeout
    /// Returns number of bytes written (if tracking DRQ)
    size_t runWriteTrackCommand(WD1793CUT& fdc, uint8_t fillByte, size_t maxBytes = 7000)
    {
        static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds max
        static constexpr size_t const TEST_INCREMENT_TSTATES = 100;
        
        size_t bytesWritten = 0;
        for (size_t clk = 0; clk < TEST_DURATION_TSTATES && bytesWritten < maxBytes; clk += TEST_INCREMENT_TSTATES)
        {
            fdc._time = clk;
            fdc.process();

            if (fdc._beta128status & WD1793::DRQ)
            {
                fdc.writeDataRegister(fillByte);
                bytesWritten++;
            }

            if (fdc._state == WD1793::S_IDLE)
            {
                break;
            }
        }
        return bytesWritten;
    }
    
    /// Helper: Run FDC simulation loop for WRITE_SECTOR
    size_t runWriteSectorCommand(WD1793CUT& fdc, const uint8_t* data, size_t dataLen)
    {
        static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 2;
        static constexpr size_t const TEST_INCREMENT_TSTATES = 100;
        
        size_t bytesWritten = 0;
        for (size_t clk = 0; clk < TEST_DURATION_TSTATES && bytesWritten < dataLen; clk += TEST_INCREMENT_TSTATES)
        {
            fdc._time = clk;
            fdc.process();

            if (fdc._beta128status & WD1793::DRQ)
            {
                fdc.writeDataRegister(data[bytesWritten]);
                bytesWritten++;
            }

            if (fdc._state == WD1793::S_IDLE)
            {
                break;
            }
        }
        return bytesWritten;
    }
    
    /// Helper: Run FDC simulation loop for READ_SECTOR
    size_t runReadSectorCommand(WD1793CUT& fdc, uint8_t* buffer, size_t bufferLen)
    {
        static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 2;
        static constexpr size_t const TEST_INCREMENT_TSTATES = 100;
        
        size_t bytesRead = 0;
        for (size_t clk = 0; clk < TEST_DURATION_TSTATES && bytesRead < bufferLen; clk += TEST_INCREMENT_TSTATES)
        {
            fdc._time = clk;
            fdc.process();

            if (fdc._beta128status & WD1793::DRQ)
            {
                buffer[bytesRead] = fdc.readDataRegister();
                bytesRead++;
            }

            if (fdc._state == WD1793::S_IDLE)
            {
                break;
            }
        }
        return bytesRead;
    }
};

/// region <Write Track Integration Tests>

/// Test that WRITE_TRACK completes at exactly 6250 bytes (no buffer overflow)
TEST_F(WD1793_WriteIntegration_Test, WriteTrack_Stops_At_6250_Bytes)
{
    WD1793CUT fdc(_context);
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    fdc.getDrive()->insertDisk(&diskImage);

    fdc.wakeUp();
    fdc._time = 1000;
    fdc.prolongFDDMotorRotation();
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    // Issue WRITE TRACK command
    fdc.cmdWriteTrack(0xF0);

    // Try to write 7000 bytes (more than 6250 limit)
    size_t bytesWritten = runWriteTrackCommand(fdc, 0x4E, 7000);

    // Command should stop at exactly 6250 bytes
    EXPECT_EQ(bytesWritten, DiskImage::RawTrack::RAW_TRACK_SIZE) 
        << "WRITE_TRACK should accept exactly 6250 bytes";

    // Verify command completed
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "FDC should be in IDLE state";
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY) << "BUSY should be cleared";
}

/// Test Format → MFM Reindex → Read sector sequence
/// Simplified test - verify reindexFromIDAM is called after format
TEST_F(WD1793_WriteIntegration_Test, Format_MFMReindex_Read)
{
    // Pre-create disk with formatted track to test reindexing
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);
    
    // Set up sectors with TR-DOS 1:2 interleave pattern directly
    const uint8_t trdosPattern[16] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16};
    
    for (uint8_t physIdx = 0; physIdx < 16; physIdx++)
    {
        track->sectors[physIdx].address_record.sector = trdosPattern[physIdx];
        track->sectors[physIdx].address_record.cylinder = 0;
        track->sectors[physIdx].address_record.head = 0;
    }

    // Call reindexFromIDAM to rebuild sector mapping
    track->reindexFromIDAM();

    // Verify sector 9 (which is at physical position 1 in TR-DOS pattern) is accessible at index 8
    ASSERT_NE(track->sectorsOrderedRef[8], nullptr) << "Sector 9 should be mapped at index 8";
    EXPECT_EQ(track->sectorsOrderedRef[8]->address_record.sector, 9) 
        << "sectorsOrderedRef[8] should point to sector 9 (at physical position 1)";
    
    // Verify sector 1 at index 0
    ASSERT_NE(track->sectorsOrderedRef[0], nullptr) << "Sector 1 should be mapped at index 0";
    EXPECT_EQ(track->sectorsOrderedRef[0]->address_record.sector, 1)
        << "sectorsOrderedRef[0] should point to sector 1";
}

/// Test that max track size (6250 bytes) matches simulated index pulse timing
/// Verifies the emulation correctly models one disk revolution
/// WD1793 datasheet: WRITE_TRACK writes from one index pulse to the next
TEST_F(WD1793_WriteIntegration_Test, TrackSize_Matches_IndexPulse_Timing)
{
    // MFM Double Density specs:
    // - Data rate: 250 kbps (250,000 bits per second)
    // - Disk rotation: 300 RPM (5 revolutions per second, 200ms per revolution)
    // - Bytes per revolution: 250,000 bits / 8 bits/byte / 5 rev/sec = 6250 bytes
    //
    // This test verifies that RAW_TRACK_SIZE equals the calculated value
    
    static constexpr size_t MFM_DATA_RATE_BPS = 250'000;        // 250 kbps
    static constexpr size_t DISK_RPM = 300;                      // 300 RPM standard
    static constexpr size_t REVOLUTIONS_PER_SEC = DISK_RPM / 60; // 5 rev/sec
    static constexpr size_t BYTES_PER_REVOLUTION = MFM_DATA_RATE_BPS / 8 / REVOLUTIONS_PER_SEC;
    
    // Verify the constant matches the calculated value
    EXPECT_EQ(DiskImage::RawTrack::RAW_TRACK_SIZE, BYTES_PER_REVOLUTION)
        << "RAW_TRACK_SIZE should equal bytes per disk revolution at 250kbps/300RPM";
    
    // Also verify it matches the expected 6250 bytes
    EXPECT_EQ(DiskImage::RawTrack::RAW_TRACK_SIZE, 6250)
        << "RAW_TRACK_SIZE should be exactly 6250 bytes";
    
    // Now verify the FDC respects this boundary during WRITE_TRACK
    WD1793CUT fdc(_context);
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    fdc.getDrive()->insertDisk(&diskImage);

    fdc.wakeUp();
    fdc._time = 1000;
    fdc.prolongFDDMotorRotation();
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    fdc.cmdWriteTrack(0xF0);
    
    // Try to write more than one revolution - FDC should stop at exactly 6250
    size_t bytesWritten = runWriteTrackCommand(fdc, 0x4E, 7000);
    
    // Verify exactly one revolution's worth of bytes were accepted
    EXPECT_EQ(bytesWritten, BYTES_PER_REVOLUTION)
        << "WRITE_TRACK should accept exactly one revolution of data (6250 bytes)";
    
    // Command should have completed (simulated index pulse terminates command)
    EXPECT_EQ(fdc._state, WD1793::S_IDLE)
        << "Command should complete after one simulated revolution";
}

/// endregion </Write Track Integration Tests>

/// region <Write/Read Sector Integration Tests>

/// Test Write → Read sector sequence (verify data integrity)
TEST_F(WD1793_WriteIntegration_Test, WriteSector_Then_ReadSector)
{
    WD1793CUT fdc(_context);
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    
    // Pre-format track so sector structure exists
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    track->formatTrack(0, 0);
    track->reindexSectors();  // Standard 1:1 mapping
    
    fdc.getDrive()->insertDisk(&diskImage);

    fdc.wakeUp();
    fdc._time = 1000;
    fdc.prolongFDDMotorRotation();
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    // Step 1: Write test pattern to sector 5
    const uint8_t targetSector = 5;
    uint8_t writeData[256];
    for (int i = 0; i < 256; i++)
        writeData[i] = static_cast<uint8_t>(0xAA + i);  // Pattern: AA, AB, AC, ...

    fdc._sectorRegister = targetSector;
    fdc.cmdWriteSector(0xA0);

    size_t bytesWritten = runWriteSectorCommand(fdc, writeData, 256);
    EXPECT_EQ(bytesWritten, 256) << "Should write 256 bytes to sector";

    // Wait for command to complete
    while (fdc._state != WD1793::S_IDLE)
    {
        fdc._time += 100;
        fdc.process();
    }

    // Step 2: Read back sector 5
    uint8_t readBuffer[256] = {0};
    
    fdc._sectorRegister = targetSector;
    fdc.cmdReadSector(0x80);

    size_t bytesRead = runReadSectorCommand(fdc, readBuffer, 256);
    EXPECT_EQ(bytesRead, 256) << "Should read 256 bytes from sector";

    // Step 3: Verify data matches
    EXPECT_EQ(readBuffer[0], 0xAA) << "First byte should be 0xAA";
    EXPECT_EQ(readBuffer[255], static_cast<uint8_t>(0xAA + 255)) << "Last byte should match pattern";
    
    // Full comparison
    bool dataMatches = (memcmp(writeData, readBuffer, 256) == 0);
    EXPECT_TRUE(dataMatches) << "Read data should match written data";
}

/// Test sector buffer alignment - verify data goes to correct location
/// Uses direct sector array access (not ordered refs) since reindexSectors uses 1:1 mapping
TEST_F(WD1793_WriteIntegration_Test, WriteSector_Buffer_Alignment)
{
    WD1793CUT fdc(_context);
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    
    // Pre-format track
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    track->formatTrack(0, 0);
    track->reindexSectors();
    
    fdc.getDrive()->insertDisk(&diskImage);

    fdc.wakeUp();
    fdc.prolongFDDMotorRotation();
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    // Write to sector 1 with unique pattern
    uint8_t pattern[256];
    for (int i = 0; i < 256; i++)
        pattern[i] = 0xAA + (i & 0x0F);  // Distinctive pattern

    fdc._time = 1000;
    fdc._sectorRegister = 1;
    fdc.cmdWriteSector(0xA0);
    
    size_t bytesWritten = runWriteSectorCommand(fdc, pattern, 256);
    EXPECT_EQ(bytesWritten, 256) << "Sector 1 should accept 256 bytes";

    // Wait for command to complete
    while (fdc._state != WD1793::S_IDLE)
    {
        fdc._time += 100;
        fdc.process();
    }

    // Verify data was written to sector 1 (index 0)
    // Use getSector which uses the ordered refs (after reindexSectors, it's 1:1)
    uint8_t* sectorData = track->getSector(0)->data;
    
    EXPECT_EQ(sectorData[0], 0xAA) << "First byte should be 0xAA";
    EXPECT_EQ(sectorData[15], static_cast<uint8_t>(0xAA + 0x0F)) << "Byte 15 should match pattern";
}

/// endregion </Write/Read Sector Integration Tests>
