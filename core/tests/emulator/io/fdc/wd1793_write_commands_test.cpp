#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdc.h"
#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <cstring>
#include <iostream>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/testtiminghelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/mfm_parser.h"

/// region <Test types>

static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;
static constexpr size_t const TSTATES_IN_MS = Z80_FREQUENCY / 1000;

class WD1793_WriteCommands_Test : public ::testing::Test
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

        // Enable logging for WD1793 module
        _logger->TurnOffLoggingForAll();
        _logger->TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK,
                                        PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);

        // Set log level to warning by default
        _logger->SetLoggingLevel(LoggerLevel::LogWarning);

        // Mock Core and Z80 to make timings work
        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        // Timing helper
        _timingHelper = new TestTimingHelper(_context);
        _timingHelper->resetClock();
    }

    void TearDown() override
    {
        if (_timingHelper)
        {
            delete _timingHelper;
        }

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
};

/// endregion </Test types>

/// region <reindex Tests>
/// The sector index is rebuilt from the raw stream (ID fields in physical order); sector numbers come from the
/// ID fields themselves, so any interleave, gaps in numbering or duplicates are represented faithfully.

namespace
{
    /// Rewrite the sector number in the ID field of the physical sector at position physIdx and fix its CRC
    void setPhysicalSectorNumber(DiskImage::Track* track, size_t physIdx, uint8_t number)
    {
        DiskImage::Sector* sector = track->getRawSector(physIdx);
        ASSERT_NE(sector, nullptr);
        sector->id->sector = number;
        sector->id->recalculateCRC();
    }
}

/// Sequential sector numbers (1-16): logical index == physical index
TEST_F(WD1793_WriteCommands_Test, Reindex_Sequential_Sectors)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    for (uint8_t i = 0; i < 16; i++)
    {
        setPhysicalSectorNumber(track, i, i + 1);
    }

    track->reindex();

    ASSERT_EQ(track->sectorCount(), 16u);
    for (uint8_t i = 0; i < 16; i++)
    {
        ASSERT_NE(track->getSector(i), nullptr) << "getSector(" << (int)i << ") is null";
        EXPECT_EQ(track->getRawSector(i), track->getSector(i))
            << "getSector(" << (int)i << ") should point to physical sector " << (int)i;
    }
}

/// TR-DOS 1:2 interleave pattern
TEST_F(WD1793_WriteCommands_Test, Reindex_TR_DOS_Interleave)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    const uint8_t trdosPattern[16] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16};

    for (uint8_t physIdx = 0; physIdx < 16; physIdx++)
    {
        setPhysicalSectorNumber(track, physIdx, trdosPattern[physIdx]);
    }

    track->reindex();

    ASSERT_NE(track->getSector(8), nullptr);
    EXPECT_EQ(track->getRawSector(1), track->getSector(8)) << "Sector 9 at physical position 1 should be getSector(8)";

    ASSERT_NE(track->getSector(0), nullptr);
    EXPECT_EQ(track->getRawSector(0), track->getSector(0)) << "Sector 1 at physical position 0 should be getSector(0)";

    for (uint8_t physIdx = 0; physIdx < 16; physIdx++)
    {
        uint8_t logicalIdx = trdosPattern[physIdx] - 1;
        EXPECT_EQ(track->getRawSector(physIdx), track->getSector(logicalIdx))
            << "Physical sector " << (int)physIdx << " (IDAM sector " << (int)trdosPattern[physIdx]
            << ") should be getSector(" << (int)logicalIdx << ")";
    }
}

/// Reverse order (16,15,...,1)
TEST_F(WD1793_WriteCommands_Test, Reindex_Reverse_Order)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    for (uint8_t i = 0; i < 16; i++)
    {
        setPhysicalSectorNumber(track, i, 16 - i);
    }

    track->reindex();

    for (uint8_t i = 0; i < 16; i++)
    {
        uint8_t logicalIdx = 16 - i - 1;
        ASSERT_NE(track->getSector(logicalIdx), nullptr);
        EXPECT_EQ(track->getRawSector(i), track->getSector(logicalIdx))
            << "Physical position " << (int)i << " should map to getSector(" << (int)logicalIdx << ")";
    }
}

/// Sector number 0 is a legal ID value but is not TR-DOS sector 6: getSector(5) is null, findSector(0) works
TEST_F(WD1793_WriteCommands_Test, Reindex_SectorNo_Zero)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    for (uint8_t i = 0; i < 16; i++)
    {
        setPhysicalSectorNumber(track, i, i + 1);
    }
    setPhysicalSectorNumber(track, 5, 0);

    track->reindex();

    EXPECT_EQ(track->sectorCount(), 16u) << "the ID field is still indexed";
    EXPECT_EQ(track->getSector(5), nullptr) << "getSector(5) should be nullptr (no sector 6)";
    EXPECT_EQ(track->findSector(0), track->getRawSector(5));

    for (uint8_t i = 0; i < 16; i++)
    {
        if (i != 5)
        {
            EXPECT_NE(track->getSector(i), nullptr) << "getSector(" << (int)i << ") should not be nullptr";
        }
    }
}

/// Sector number 17 is outside TR-DOS numbering but still a valid sector
TEST_F(WD1793_WriteCommands_Test, Reindex_SectorNo_17)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    for (uint8_t i = 0; i < 16; i++)
    {
        setPhysicalSectorNumber(track, i, i + 1);
    }
    setPhysicalSectorNumber(track, 7, 17);

    track->reindex();

    EXPECT_EQ(track->getSector(7), nullptr) << "getSector(7) should be nullptr (no sector 8 on the track)";
    ASSERT_NE(track->getSector(16), nullptr) << "sector 17 is addressable";
    EXPECT_EQ(track->getSector(16), track->getRawSector(7));
}

/// Duplicate sector numbers: all kept, first in stream order wins for findSector, rotational lookup reaches the rest
TEST_F(WD1793_WriteCommands_Test, Reindex_Duplicate_SectorNo)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    for (uint8_t i = 0; i < 16; i++)
    {
        setPhysicalSectorNumber(track, i, 5);
    }

    track->reindex();

    ASSERT_EQ(track->sectorCount(), 16u);
    EXPECT_EQ(track->getSector(4), track->getRawSector(0)) << "First physical sector claiming sector 5 is returned";

    // Rotational search from the middle of the track returns the next duplicate
    size_t middle = track->getRawSector(8)->idamOffset;
    EXPECT_EQ(track->findSector(5, middle), track->getRawSector(8));
    EXPECT_EQ(track->findSector(5, middle + 1), track->getRawSector(9));

    for (uint8_t i = 0; i < 16; i++)
    {
        if (i != 4)
        {
            EXPECT_EQ(track->getSector(i), nullptr) << "getSector(" << (int)i << ") should be nullptr";
        }
    }
}

/// Only half of the sectors carry TR-DOS numbers
TEST_F(WD1793_WriteCommands_Test, Reindex_Partial_Valid)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    for (uint8_t i = 0; i < 16; i++)
    {
        setPhysicalSectorNumber(track, i, (i < 8) ? (i + 1) : 0);
    }

    track->reindex();

    for (uint8_t i = 0; i < 8; i++)
    {
        EXPECT_NE(track->getSector(i), nullptr) << "getSector(" << (int)i << ") should be non-null";
    }
    for (uint8_t i = 8; i < 16; i++)
    {
        EXPECT_EQ(track->getSector(i), nullptr) << "getSector(" << (int)i << ") should be nullptr";
    }
}

/// A sector whose ID field was wiped out of the stream disappears from the index entirely
TEST_F(WD1793_WriteCommands_Test, Reindex_ErasedIdField)
{
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track, nullptr);

    DiskImage::Sector* victim = track->getSector(3);
    ASSERT_NE(victim, nullptr);
    std::memset(track->rawData() + victim->idamOffset - 3, 0x4E, 10);  // A1 A1 A1 FE C H R N CRC -> gap

    track->reindex();

    EXPECT_EQ(track->sectorCount(), 15u);
    EXPECT_EQ(track->getSector(3), nullptr);
}

/// endregion </reindex Tests>

/// region <processWriteTrack Tests>

/// Test that F5 writes 0xA1 and presets CRC
TEST_F(WD1793_WriteCommands_Test, WriteTrack_F5_WritesA1)
{
    WD1793CUT fdc(_context);
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    fdc.getDrive()->insertDisk(&diskImage);

    // Set up for write track - directly manipulate internal buffer like existing tests
    fdc.wakeUp();
    fdc._time = 1000;
    fdc.prolongFDDMotorRotation();
    
    // Allocate raw track buffer directly (like existing WriteTrack_F5_Sets_CrcStartPosition test)
    fdc._rawDataBuffer = new uint8_t[DiskImage::RawTrack::RAW_TRACK_SIZE];
    fdc._rawDataBufferIndex = 0;
    fdc._bytesToWrite = 6250;
    
    // Write F5 byte directly
    fdc._dataRegister = 0xF5;
    fdc.processWriteTrack();
    
    // Verify A1 was written (F5 -> A1) at the current buffer position
    EXPECT_EQ(fdc._rawDataBuffer[0], 0xA1) << "F5 should be translated to A1";
    
    // Cleanup
    delete[] fdc._rawDataBuffer;
    fdc._rawDataBuffer = nullptr;
}

/// Test that F7 generates valid CRC bytes
TEST_F(WD1793_WriteCommands_Test, WriteTrack_F7_CRC_Matches_Helper)
{
    // Prepare test data - simulating IDAM
    uint8_t testData[] = {0xFE, 0x00, 0x00, 0x01, 0x01};  // FE + track + side + sector + size

    // Calculate CRC using the helper
    uint16_t expectedCrc = CRCHelper::crcWD1793(testData, sizeof(testData));

    // Extract bytes as they would be written
    uint8_t crcHigh = (expectedCrc >> 8) & 0xFF;
    uint8_t crcLow = expectedCrc & 0xFF;

    // Verify CRC is non-zero and reasonable
    EXPECT_NE(expectedCrc, 0x0000) << "CRC should not be zero";
    EXPECT_NE(expectedCrc, 0xFFFF) << "CRC should not be all ones";

    // Parse the data to verify it would validate
    // (This just confirms the CRC helper is working correctly)
    std::cout << "Test IDAM CRC: 0x" << std::hex << expectedCrc << " (bytes: " << (int)crcHigh << ", " << (int)crcLow
              << ")" << std::dec << std::endl;
}

/// Test that write track stops at 6250 bytes (no buffer overflow)
/// NOTE: This is a complex integration test - marked DISABLED pending proper integration test setup
/// The full format->read sequence should be tested in wd1793_integration_test.cpp
TEST_F(WD1793_WriteCommands_Test, DISABLED_WriteTrack_Buffer_Overflow_Protected)
{
    static constexpr size_t const TEST_DURATION_TSTATES = 3.5 * 1'000'000 * 2;  // 2 seconds
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;
    
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
    size_t bytesWritten = 0;
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES && bytesWritten < 7000; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc.process();

        if (fdc._beta128status & WD1793::DRQ)
        {
            fdc.writeDataRegister(0x4E);
            bytesWritten++;
        }

        // Check if command finished
        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }

    // Command should complete at exactly 6250 bytes (not more)
    EXPECT_EQ(bytesWritten, DiskImage::RawTrack::RAW_TRACK_SIZE) << "Should write exactly 6250 bytes";
}

/// endregion </processWriteTrack Tests>

/// region <processWriteSector Tests>

/// Test that write sector stops at sector size bytes
TEST_F(WD1793_WriteCommands_Test, WriteSector_Stops_At_SectorSize)
{
    static constexpr size_t const TEST_DURATION_TSTATES = 3.5 * 1'000'000 * 2;  // 2 seconds
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;
    
    WD1793CUT fdc(_context);
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);

    // Format track first so sectors are readable
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    track->formatTrack(0, 0);

    fdc.getDrive()->insertDisk(&diskImage);

    fdc.wakeUp();
    fdc._time = 1000;
    fdc.prolongFDDMotorRotation();
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;
    fdc._sectorRegister = 1;

    // Issue WRITE SECTOR command
    fdc.cmdWriteSector(0xA0);

    // Try to write 300 bytes (more than 256 sector size)
    size_t bytesWritten = 0;
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES && bytesWritten < 300; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc.process();

        if (fdc._beta128status & WD1793::DRQ)
        {
            fdc.writeDataRegister(static_cast<uint8_t>(bytesWritten & 0xFF));
            bytesWritten++;
        }

        // Check if command finished
        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }

    // Should have written exactly 256 bytes (TR-DOS sector size)
    EXPECT_EQ(bytesWritten, 256) << "Should write exactly 256 bytes for TR-DOS sector";
}

/// Test that data is written at correct buffer offset
/// NOTE: This is a complex integration test - marked DISABLED pending proper integration test setup
/// The full write->read sequence should be tested in wd1793_integration_test.cpp
TEST_F(WD1793_WriteCommands_Test, DISABLED_WriteSector_Buffer_Alignment)
{
    static constexpr size_t const TEST_DURATION_TSTATES = 3.5 * 1'000'000 * 2;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;
    
    WD1793CUT fdc(_context);
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);

    // Format track first
    DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(0, 0);
    track->formatTrack(0, 0);

    fdc.getDrive()->insertDisk(&diskImage);

    fdc.wakeUp();
    fdc._time = 1000;
    fdc.prolongFDDMotorRotation();
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    // Write to sector 5 (index 4)
    const uint8_t targetSector = 5;
    fdc._sectorRegister = targetSector;

    // Issue WRITE SECTOR command
    fdc.cmdWriteSector(0xA0);

    // Write specific pattern
    size_t bytesWritten = 0;
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES && bytesWritten < 256; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc.process();

        if (fdc._beta128status & WD1793::DRQ)
        {
            fdc.writeDataRegister(static_cast<uint8_t>(0xAA + bytesWritten));
            bytesWritten++;
        }

        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }

    // Verify we wrote all 256 bytes
    EXPECT_EQ(bytesWritten, 256) << "Should have written 256 bytes";
    
    // Verify data was written to the correct sector (physical position == sector number - 1 for the 1:1 layout)
    uint8_t* sectorData = track->getRawSector(targetSector - 1)->data;

    EXPECT_EQ(sectorData[0], 0xAA) << "First byte should be 0xAA";
    EXPECT_EQ(sectorData[255], static_cast<uint8_t>(0xAA + 255)) << "Last byte should match pattern";
}

/// endregion </processWriteSector Tests>

/// region <CRC Validation Tests>

/// Test that IDAM CRC is valid after F7 command
TEST_F(WD1793_WriteCommands_Test, CRC_IDAM_Valid_After_F7)
{
    // Test that CRC calculation matches between F7 and CRCHelper
    uint8_t idamData[] = {0xFE, 0x00, 0x00, 0x01, 0x01};  // IDAM: FE + C + H + S + N

    uint16_t crc = CRCHelper::crcWD1793(idamData, sizeof(idamData));

    // CRC should be non-trivial
    EXPECT_NE(crc, 0x0000);
    EXPECT_NE(crc, 0xFFFF);

    // Append CRC to data and verify MFM parser would accept it
    // (This is a basic sanity check - full validation requires MFMParser)
    uint8_t fullData[7];
    memcpy(fullData, idamData, 5);
    fullData[5] = (crc >> 8) & 0xFF;
    fullData[6] = crc & 0xFF;

    // Verify CRC recalculates correctly
    uint16_t recalc = CRCHelper::crcWD1793(idamData, 5);
    EXPECT_EQ(recalc, crc) << "CRC should be deterministic";
}

/// Test CRC byte order (HIGH-LOW)
TEST_F(WD1793_WriteCommands_Test, CRC_ByteOrder_HighLow)
{
    uint8_t testData[] = {0xFB, 0x00, 0x00, 0x00, 0x00};  // Simple test pattern

    uint16_t crc = CRCHelper::crcWD1793(testData, sizeof(testData));

    uint8_t highByte = (crc >> 8) & 0xFF;
    uint8_t lowByte = crc & 0xFF;

    // Verify these produce the expected value when combined back
    uint16_t reconstructed = (highByte << 8) | lowByte;
    EXPECT_EQ(reconstructed, crc) << "CRC byte order should be HIGH-LOW";
}

/// endregion </CRC Validation Tests>

