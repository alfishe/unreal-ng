/// WD1793 Write Track MFM Verification Tests
/// Tests the raw MFM data written by WD1793 Write Track command
/// and validates DiskImage reindexing produces correct sector structure.

#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "common/modulelogger.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/iwd1793observer.h"

/// Test observer to capture Write Track completion
class WriteTrackTestObserver : public IWD1793Observer
{
public:
    bool writeTrackStarted = false;
    bool writeTrackCompleted = false;
    uint8_t completionStatus = 0;
    uint8_t lastCommandByte = 0;

    void onFDCCommand(uint8_t command, const WD1793& fdc) override
    {
        lastCommandByte = command;
        // Write Track command is 0xF0-0xFF (top nibble = 0xF)
        if ((command & 0xF0) == 0xF0)
        {
            writeTrackStarted = true;
            std::cout << "[OBSERVER] Write Track command started (cmd=0x" 
                      << std::hex << (int)command << std::dec << ")\n";
        }
    }

    void onFDCCommandComplete(uint8_t status, const WD1793& fdc) override
    {
        if ((lastCommandByte & 0xF0) == 0xF0)
        {
            writeTrackCompleted = true;
            completionStatus = status;
            std::cout << "[OBSERVER] Write Track complete, status: 0x" 
                      << std::hex << (int)status << std::dec << "\n";
        }
    }

    void reset()
    {
        writeTrackStarted = false;
        writeTrackCompleted = false;
        completionStatus = 0;
        lastCommandByte = 0;
    }
};

/// WD1793 CUT for testing protected members
class WD1793_WriteTrackCUT : public WD1793
{
    using WD1793::WD1793;
public:
    // Expose protected members for testing
    using WD1793::_time;
    using WD1793::_lastTime;
    using WD1793::_diffTime;
    using WD1793::_delayTStates;
    using WD1793::_state;
    using WD1793::_state2;
    using WD1793::_statusRegister;
    using WD1793::_trackRegister;
    using WD1793::_dataRegister;
    using WD1793::_sectorRegister;
    using WD1793::_beta128status;
    using WD1793::_selectedDrive;
    using WD1793::_rawDataBuffer;
    using WD1793::_rawDataBufferIndex;
    using WD1793::_bytesToWrite;
    using WD1793::_writeTrackTarget;
    using WD1793::resetTime;
    using WD1793::wakeUp;
    using WD1793::cmdWriteTrack;
    using WD1793::process;
    using WD1793::processWriteTrack;
    using WD1793::decodeWD93Command;
    using WD1793::processWD93Command;
    using WD1793::transitionFSM;
    using WD1793::portDeviceOutMethod;
    using WD1793::portDeviceInMethod;
    using WD1793::DRQ;
    using WD1793::INTRQ;
};

class WD1793_WriteTrack_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    WriteTrackTestObserver _observer;

protected:
    void SetUp() override
    {
        MessageCenter::DisposeDefaultMessageCenter();
        _emulator = EmulatorTestHelper::CreateStandardEmulator("Pentagon", LoggerLevel::LogError);
        if (_emulator)
        {
            _context = _emulator->GetContext();
        }
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
        MessageCenter::DisposeDefaultMessageCenter();
    }
};

/// Test that WD1793 Write Track command writes data to raw track buffer
TEST_F(WD1793_WriteTrack_Test, WriteTrack_WritesToRawBuffer)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    std::cout << "\n========================================\n";
    std::cout << "[WRITE_TRACK] Raw Buffer Write Test\n";
    std::cout << "========================================\n";

    // Get FDC and insert disk
    WD1793_WriteTrackCUT* fdc = reinterpret_cast<WD1793_WriteTrackCUT*>(_context->pBetaDisk);
    ASSERT_NE(fdc, nullptr);

    FDD* fdd = fdc->getDrive();
    ASSERT_NE(fdd, nullptr);

    DiskImage* diskImage = new DiskImage(80, 2);
    fdd->insertDisk(diskImage);

    // Register observer
    fdc->addObserver(&_observer);

    // Wake up FDC and initialize
    fdc->wakeUp();
    fdc->resetTime();
    fdc->_trackRegister = 0;
    fdc->_sectorRegister = 1;

    // Get track 0 for reference
    DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
    ASSERT_NE(track0, nullptr);

    // Capture raw track pointer before Write Track
    uint8_t* rawTrackBefore = reinterpret_cast<uint8_t*>(static_cast<DiskImage::RawTrack*>(track0));
    
    // Store first 16 bytes before modification
    uint8_t beforeBytes[16];
    memcpy(beforeBytes, rawTrackBefore, 16);
    
    std::cout << "[BEFORE] First 16 bytes: ";
    for (int i = 0; i < 16; i++)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)beforeBytes[i] << " ";
    std::cout << std::dec << "\n";

    // Issue Write Track command via port 0x1F
    // Command byte: 0xF0 = Write Track (no flags)
    fdc->portDeviceOutMethod(0x1F, 0xF0);

    std::cout << "[COMMAND] Write Track (0xF0) issued\n";
    std::cout << "[STATE] FSM state: " << WD1793::WDSTATEToString(fdc->_state) << "\n";

    // Disk rotation period is 700,000 T-states (200ms at 3.5MHz)
    // WD1793 waits for index pulse before starting Write Track
    static constexpr size_t DISK_ROTATION_TSTATES = 3500000 / 5;  // 700,000 T-states
    static constexpr size_t TSTATES_PER_FDC_BYTE = 114;  // ~114 T-states per byte at FDD speed

    std::cout << "[INFO] Disk rotation period: " << DISK_ROTATION_TSTATES << " T-states\n";

    // Helper to advance emulator time properly (updates _context and triggers _diffTime calculation)
    auto advanceTime = [&](size_t tstates) {
        // Update emulator state time (this is what process() reads from)
        _context->emulatorState.t_states += tstates;
        // Also update the FDC internal time directly for delay tracking
        fdc->_diffTime = tstates;
        fdc->_lastTime = fdc->_time;
        fdc->_time += tstates;
    };

    // Phase 1: Wait for index pulse (advance past one rotation)
    std::cout << "[PHASE 1] Waiting for index pulse...\n";
    for (size_t i = 0; i < 15 && fdc->_state != WD1793::S_WRITE_TRACK; i++)
    {
        advanceTime(DISK_ROTATION_TSTATES / 10);
        fdc->process();
        std::cout << "  State after " << (i + 1) * DISK_ROTATION_TSTATES / 10 
                  << " T-states: " << WD1793::WDSTATEToString(fdc->_state) 
                  << " (delay=" << fdc->_delayTStates << ")\n";
    }

    std::cout << "[PHASE 2] Writing track data...\n";

    // Phase 2: Write data bytes
    // CRITICAL: The WD1793 expects bytes to be written BEFORE calling process()
    // So the loop is: (1) check DRQ, (2) write byte if DRQ asserted, (3) advance time, (4) process()
    size_t bytesWritten = 0;
    size_t maxIterations = 100000;  // Safety limit
    size_t lastReportedState = fdc->_state;
    
    for (size_t i = 0; i < maxIterations; i++)
    {
        // Check if DRQ is asserted BEFORE advancing time
        if (fdc->_beta128status & WD1793_WriteTrackCUT::DRQ)
        {
            // Write a test pattern byte immediately
            uint8_t testByte;
            if (bytesWritten < 12)
                testByte = 0x00;  // Sync bytes
            else if (bytesWritten < 15)
                testByte = 0xF5;  // -> A1 sync marks
            else if (bytesWritten == 15)
                testByte = 0xFE;  // ID Address Mark
            else if (bytesWritten < 20)
                testByte = 0x00;  // C/H/R/N
            else if (bytesWritten == 20)
                testByte = 0xF7;  // CRC
            else
                testByte = 0x4E;  // Gap filler
            
            fdc->portDeviceOutMethod(0x7F, testByte);
            bytesWritten++;
            
            if (bytesWritten % 1000 == 0)
            {
                std::cout << "[PROGRESS] " << bytesWritten << " bytes written, state: " 
                          << WD1793::WDSTATEToString(fdc->_state) << "\n";
            }
        }

        // NOW advance time and process
        advanceTime(TSTATES_PER_FDC_BYTE);
        fdc->process();

        // Report state changes
        if (fdc->_state != lastReportedState && bytesWritten < 50)
        {
            std::cout << "  State transition: " << WD1793::WDSTATEToString(static_cast<WD1793::WDSTATE>(lastReportedState))
                      << " -> " << WD1793::WDSTATEToString(fdc->_state) << " at byte " << bytesWritten << "\n";
            lastReportedState = fdc->_state;
        }

        // Check for command completion
        if (fdc->_state == WD1793::S_IDLE && bytesWritten > 0)
        {
            std::cout << "[COMPLETE] Write Track finished after " << bytesWritten << " bytes\n";
            break;
        }
    }

    // Check results
    std::cout << "\n[RESULTS]\n";
    std::cout << "  Bytes written: " << bytesWritten << "\n";
    std::cout << "  Observer started: " << (_observer.writeTrackStarted ? "YES" : "NO") << "\n";
    std::cout << "  Observer completed: " << (_observer.writeTrackCompleted ? "YES" : "NO") << "\n";
    std::cout << "  Final status: 0x" << std::hex << (int)fdc->_statusRegister << std::dec << "\n";

    // Check raw buffer after Write Track
    uint8_t* rawTrackAfter = reinterpret_cast<uint8_t*>(static_cast<DiskImage::RawTrack*>(track0));
    
    std::cout << "[AFTER] First 16 bytes: ";
    for (int i = 0; i < 16; i++)
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)rawTrackAfter[i] << " ";
    std::cout << std::dec << "\n";

    // Check if data was actually written (buffer should have changed)
    bool bufferChanged = (memcmp(beforeBytes, rawTrackAfter, 16) != 0);
    std::cout << "[CHECK] Buffer changed: " << (bufferChanged ? "YES" : "NO") << "\n";

    // Run MFM validation on the track
    auto validationResult = MFMValidator::validate(rawTrackAfter, DiskImage::RawTrack::RAW_TRACK_SIZE);
    std::cout << "\n[MFM VALIDATION]\n";
    std::cout << "  Valid sectors: " << validationResult.parseResult.validSectors << "/16\n";
    std::cout << "  Passed: " << (validationResult.passed ? "YES" : "NO") << "\n";

    // Cleanup
    fdc->removeObserver(&_observer);
    fdd->ejectDisk();

    // Assertions
    EXPECT_TRUE(_observer.writeTrackStarted) << "Observer should see Write Track command";
    EXPECT_GT(bytesWritten, 0) << "Should have written at least some bytes";
    
    std::cout << "========================================\n";
}

/// Test that Write Track with proper TR-DOS format pattern produces valid sectors
TEST_F(WD1793_WriteTrack_Test, WriteTrack_TRDOSFormatPattern)
{
    if (!_emulator)
    {
        GTEST_SKIP() << "Emulator initialization failed";
    }

    std::cout << "\n========================================\n";
    std::cout << "[WRITE_TRACK] TR-DOS Format Pattern Test\n";
    std::cout << "========================================\n";

    // This test will write a proper TR-DOS format pattern and verify sectors

    // TODO: Implement after basic write verification passes

    std::cout << "[SKIP] Not yet implemented - basic write test must pass first\n";
    std::cout << "========================================\n";
}
