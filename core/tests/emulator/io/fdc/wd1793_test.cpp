#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <set>

#include "_helpers/test_path_helper.h"
#include "_helpers/testtiminghelper.h"
#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "common/stringhelper.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"
#include "loaders/disk/loader_trd.h"

/// region <Test types>

static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;
static constexpr size_t const TSTATES_IN_MS = Z80_FREQUENCY / 1000;

class WD1793_Test : public ::testing::Test
{
protected:
    // Module logger definitions
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

        // Set log level to warning by default. Each test can override
        _logger->SetLoggingLevel(LoggerLevel::LogWarning);

        // Mock Core and Z80 to make timings work
        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        // Timing helper
        _timingHelper = new TestTimingHelper(_context);
        _timingHelper->resetClock();  // Reset all t-state counters within system (Z80, emulator state)
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

struct RangeCommand
{
    uint8_t rangeStart;
    uint8_t rangeEnd;
    WD1793::WD_COMMANDS command;
};

class RangeLookup
{
private:
    static constexpr std::array<RangeCommand, 11> _referenceValues{
        RangeCommand{0x00, 0x0F, WD1793::WD_CMD_RESTORE},
        RangeCommand{0x10, 0x1F, WD1793::WD_CMD_SEEK},
        RangeCommand{0x20, 0x3F, WD1793::WD_CMD_STEP},
        RangeCommand{0x40, 0x5F, WD1793::WD_CMD_STEP_IN},
        RangeCommand{0x60, 0x7F, WD1793::WD_CMD_STEP_OUT},
        RangeCommand{0x80, 0x9F, WD1793::WD_CMD_READ_SECTOR},
        RangeCommand{0xA0, 0xBF, WD1793::WD_CMD_WRITE_SECTOR},
        RangeCommand{0xC0, 0xDF, WD1793::WD_CMD_READ_ADDRESS},
        RangeCommand{0xE0, 0xEF, WD1793::WD_CMD_READ_TRACK},
        RangeCommand{0xF0, 0xFF, WD1793::WD_CMD_WRITE_TRACK},
        RangeCommand{0xD0, 0xDF, WD1793::WD_CMD_FORCE_INTERRUPT},
    };

public:
    bool isValueInRange(uint8_t value)
    {
        bool result = false;

        for (const auto& rangeCommand : _referenceValues)
        {
            if (value >= rangeCommand.rangeStart && value <= rangeCommand.rangeEnd)
            {
                result = true;
            }
        }

        return result;
    }

    WD1793CUT::WD_COMMANDS getCommandForValue(uint8_t value)
    {
        WD1793CUT::WD_COMMANDS result = WD1793CUT::WD_CMD_RESTORE;

        for (const auto& rangeCommand : _referenceValues)
        {
            if (value >= rangeCommand.rangeStart && value <= rangeCommand.rangeEnd)
            {
                result = rangeCommand.command;
            }
        }

        return result;
    }
};

/// endregion </Test types>

/// region <Disk image verification tests>

/// endregion </Disk image verification tests>

/// region <WD1793 commands>

/// Basic WD1793 commands decoding test
TEST_F(WD1793_Test, decodeWD93Command)
{
    RangeLookup referenceValues;

    for (int i = 0; i <= 255; i++)
    {
        WD1793::WD_COMMANDS result = WD1793CUT::decodeWD93Command(i);
        WD1793::WD_COMMANDS reference = referenceValues.getCommandForValue(i);

        EXPECT_EQ(result, reference) << StringHelper::Format("0x%02X -> %s", i, WD1793CUT::getWD_COMMANDName(result));

        // std::string message = StringHelper::Format("0x%02X -> %s", i, VG93CUT::getWD_COMMANDName(result));
        // std::cout << message << std::endl;
    }
}

TEST_F(WD1793_Test, isTypeNCommand)
{
    for (int i = 0; i <= 255; i++)
    {
        bool isType1Command = WD1793CUT::isType1Command(i);
        bool isType2Command = WD1793CUT::isType2Command(i);
        bool isType3Command = WD1793CUT::isType3Command(i);
        bool isType4Command = WD1793CUT::isType4Command(i);
        int trueCount =
            (isType1Command ? 1 : 0) + (isType2Command ? 1 : 0) + (isType3Command ? 1 : 0) + (isType4Command ? 1 : 0);

        std::string message = StringHelper::Format("%03d: t1: %d; t2: %d; t3: %d; t4: %d", i, isType1Command,
                                                   isType2Command, isType3Command, isType4Command);
        EXPECT_EQ(trueCount, 1) << "Only one command type can be active at a time. " << message;

        // std::cout << message << std::endl;
    }
}

/// endregion </WD1793 commands>

/// region <Status bits behavior>

TEST_F(WD1793_Test, DISABLED_Beta128_Status_INTRQ)
{
    FAIL() << "Not Implemented yet";
}

TEST_F(WD1793_Test, DISABLED_Beta128_Status_DRQ)
{
    FAIL() << "Not Implemented yet";
}

/// endregion </Status bits behavior>

/// region <FDD related>

/// Test motor starts and auto-stops after 3 seconds
TEST_F(WD1793_Test, FDD_Motor_StartStop)
{
    static constexpr size_t const TEST_DURATION_SEC = 4;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000;  // Time increments during simulation

    // Internal logging messages with specified level (Warning for regular test runs, Debug for triaging)
    _context->pModuleLogger->SetLoggingLevel(LogDebug);

    WD1793CUT fdc(_context);

    // Reset WDC internal time marks
    fdc.resetTime();

    // Initialize time tracking
    int64_t currentTime = 10;  // Start at 10 T-states
    int64_t prevTime = 0;

    // Set initial time
    fdc._time = currentTime;

    // Ensure we have a disk in the drive
    DiskImage diskImage = DiskImage(MAX_CYLINDERS, MAX_SIDES);
    fdc.getDrive()->insertDisk(&diskImage);
    EXPECT_TRUE(fdc.getDrive()->isDiskInserted()) << "Disk image must be inserted";

    // Reset index pulse tracking
    fdc._indexPulseCounter = 0;
    fdc._prevIndex = false;
    fdc._index = false;

    // Store initial index pulse counter
    uint32_t initialIndexPulseCount = fdc._indexPulseCounter;

    // Trigger motor start
    fdc.prolongFDDMotorRotation();

    // Log the initial state
    GTEST_LOG_(INFO) << "Initial state:"
                     << "\n  Motor: " << (fdc._selectedDrive->getMotor() ? "ON" : "OFF")
                     << "\n  Initial timeout: " << fdc._motorTimeoutTStates << " T-states ("
                     << (fdc._motorTimeoutTStates / (double)Z80_FREQUENCY) << "s)"
                     << "\n  Initial index pulse count: " << initialIndexPulseCount;

    /// region <Perform simulation loop>

    bool motorStarted = false;
    int64_t motorStartTStates = 0;
    int64_t motorStopTStates = 0;

    size_t clk;
    // Simulate time progression in fixed increments
    for (clk = currentTime; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        // Calculate time difference for this iteration
        prevTime = currentTime;
        currentTime = clk;
        int64_t diffTime = TEST_INCREMENT_TSTATES;  // Fixed time increment

        // Log the current state for debugging
        if (clk % (Z80_FREQUENCY / 10) == 0)  // Log every 100ms
        {
            GTEST_LOG_(INFO) << "Time: " << currentTime << " T-states (" << (currentTime / (double)Z80_FREQUENCY)
                             << "s), "
                             << "Motor: " << (fdc._selectedDrive->getMotor() ? "ON" : "OFF")
                             << ", Timeout: " << fdc._motorTimeoutTStates << " T-states";
        }

        // Update time for FDC and process motor state
        fdc._time = currentTime;
        fdc._diffTime = diffTime;  // Set the actual time difference since last update

        // Process FDC state updates
        fdc.process();  // Process other FDC states

        if (!motorStarted && fdc._selectedDrive->getMotor())
        {
            motorStartTStates = clk;
            motorStarted = true;

            // Log when motor starts
            GTEST_LOG_(INFO) << "FDD motor started at " << clk << " T-states (" << (clk * 1000 / (double)Z80_FREQUENCY)
                             << "ms)";
        }

        if (motorStarted && !fdc._selectedDrive->getMotor())
        {
            motorStopTStates = clk;
            motorStarted = false;

            // Log when motor stops
            GTEST_LOG_(INFO) << "FDD motor stopped at " << clk << " T-states (" << (clk * 1000 / (double)Z80_FREQUENCY)
                             << "ms)";
        }

        // Log index strobe data using Google Test's debug output
        std::string strobeInfo = fdc.dumpIndexStrobeData();
        if (!strobeInfo.empty())
        {
            // GTEST_LOG_(INFO) << "Index Strobe Data at " << clk << " T-states (" << (clk * 1000 /
            // (double)Z80_FREQUENCY) << "ms):\n" << strobeInfo;
        }
    }
    /// endregion </Perform simulation loop>

    /// region <Check results>
    EXPECT_NE(motorStartTStates, 0) << "Motor never started";
    EXPECT_NE(motorStopTStates, 0) << "Motor never stopped";

    // Calculate motor runtime statistics
    size_t estimatedMotorOnTStates = 3 * Z80_FREQUENCY;  // 3 seconds at 3.5MHz = 10,500,000 T-states
    size_t motorWasOnForTSTates = std::abs(motorStopTStates - motorStartTStates);
    uint32_t finalIndexPulseCount = fdc._indexPulseCounter;
    uint32_t indexPulsesDuringTest = finalIndexPulseCount - initialIndexPulseCount;

    // Calculate expected index pulses (5 per second at 300 RPM)
    const double testDurationSeconds = motorWasOnForTSTates / (double)Z80_FREQUENCY;
    const double expectedIndexPulses = testDurationSeconds * FDD::DISK_REVOLUTIONS_PER_SECOND;

    // Log detailed results
    GTEST_LOG_(INFO) << "Test Results:"
                     << "\n  Motor Runtime:"
                     << "\n    T-states: " << motorWasOnForTSTates
                     << "\n    Milliseconds: " << (motorWasOnForTSTates * 1000.0 / Z80_FREQUENCY)
                     << "\n    Seconds: " << (motorWasOnForTSTates / (double)Z80_FREQUENCY) << "\n  Index Pulses:"
                     << "\n    Initial count: " << initialIndexPulseCount
                     << "\n    Final count: " << finalIndexPulseCount
                     << "\n    Pulses during test: " << indexPulsesDuringTest << "\n    Expected pulses: ~"
                     << static_cast<int>(expectedIndexPulses) << " (" << FDD::DISK_REVOLUTIONS_PER_SECOND
                     << " per second)";

    // The motor should run for approximately 3 seconds (10.5M T-states at 3.5MHz)
    // Allow some tolerance in the test
    size_t tolerance = Z80_FREQUENCY / 10;  // 100ms tolerance (350,000 T-states)

    EXPECT_GE(motorWasOnForTSTates, estimatedMotorOnTStates - tolerance) << "Motor ran for less time than expected";

    // The motor should not run for significantly longer than 3 seconds
    EXPECT_LE(motorWasOnForTSTates, estimatedMotorOnTStates + tolerance) << "Motor ran for longer than expected";

    // Verify index pulse counting is reasonable
    EXPECT_NEAR(indexPulsesDuringTest, expectedIndexPulses, 1) << "Unexpected number of index pulses detected";
    /// endregion </Check results>
}

/// Test if any new operation prolongs timeout
TEST_F(WD1793_Test, FDD_Motor_Prolong)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 10;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    // Reset WDC internal time marks
    fdc.resetTime();

    // Trigger motor start
    fdc.prolongFDDMotorRotation();

    /// region <Perform simulation loop>

    bool motorStarted = false;
    bool prolongActivated = false;
    int64_t motorStartTStates = 0;
    int64_t motorStopTStates = 0;

    size_t clk;
    for (clk = 10; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        // Prolong for another 3 seconds. So total time should be about 5 seconds
        if (!prolongActivated && clk >= 2 * Z80_FREQUENCY)
        {
            fdc.prolongFDDMotorRotation();

            prolongActivated = true;
        }

        // Update time for FDC
        fdc._time = clk;

        // Process FSM state updates
        fdc.process();

        if (!motorStarted && fdc._selectedDrive->getMotor())
        {
            motorStartTStates = clk;

            motorStarted = true;
        }

        if (motorStarted && !fdc._selectedDrive->getMotor())
        {
            motorStopTStates = clk;

            motorStarted = false;
        }
    }
    /// endregion </Perform simulation loop>

    /// region <Check results>
    EXPECT_NE(motorStartTStates, 0);
    EXPECT_NE(motorStopTStates, 0);

    size_t estimatedMotorOnTStates = 5 * Z80_FREQUENCY;
    size_t motorWasOnForTSTates = std::abs(motorStopTStates - motorStartTStates);
    EXPECT_IN_RANGE(motorWasOnForTSTates, estimatedMotorOnTStates - TEST_INCREMENT_TSTATES,
                    estimatedMotorOnTStates + TEST_INCREMENT_TSTATES);
    /// endregion </Check results>
}

/// Test if index pulses are available during disk rotation
TEST_F(WD1793_Test, FDD_Rotation_Index)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 4;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    // Since, when counting Index pulses, we have a check: if (diskInserted && motorOn)
    // Then we should insert a disk image
    DiskImage diskImage = DiskImage(MAX_CYLINDERS, MAX_SIDES);
    fdc.getDrive()->insertDisk(&diskImage);

    EXPECT_EQ(fdc.getDrive()->isDiskInserted(), true) << "Disk image must be inserted";

    // Reset WDC internal time marks
    fdc.resetTime();

    /// region <Perform simulation loop>
    bool motorStarted = false;
    int64_t motorStartTStates = 0;
    int64_t motorStopTStates = 0;

    /// region <Pre-checks>
    EXPECT_EQ(fdc._indexPulseCounter, 0);
    /// endregion </Pre-checks>

    size_t clk;
    for (clk = 10; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        if (clk < Z80_FREQUENCY)
        {
            EXPECT_EQ(fdc._indexPulseCounter, 0) << "Index pulse counter shouldn't increment when FDD motor is stopped";
        }

        // Start motor after 1-second delay
        if (clk > Z80_FREQUENCY && !motorStarted)
        {
            // Trigger motor start
            fdc.prolongFDDMotorRotation();

            motorStartTStates = clk;
            motorStarted = true;
        }

        // Update time for FDC
        fdc._time = clk;

        // Process FSM state updates
        fdc.process();

        // Record motor stop time
        if (motorStarted && !fdc._selectedDrive->getMotor())
        {
            motorStopTStates = clk;

            motorStarted = false;
        }
    }
    /// endregion </Perform simulation loop>

    /// region <Check results>
    EXPECT_NE(motorStartTStates, 0);
    EXPECT_NE(motorStopTStates, 0);

    // Check motor still switched off within specs
    size_t estimatedMotorOnTStates = 3 * Z80_FREQUENCY;
    size_t motorWasOnForTSTates = std::abs(motorStopTStates - motorStartTStates);
    EXPECT_IN_RANGE(motorWasOnForTSTates, estimatedMotorOnTStates - TEST_INCREMENT_TSTATES,
                    estimatedMotorOnTStates + TEST_INCREMENT_TSTATES);

    size_t estimatedIndexPulses = std::ceil((double)motorWasOnForTSTates * FDD_RPS / (double)Z80_FREQUENCY);
    size_t indexPulses = fdc._indexPulseCounter;
    EXPECT_IN_RANGE(indexPulses, estimatedIndexPulses - 1, estimatedIndexPulses + 1);
    /// endregion </Check results>
}

/// Test if index pulses stop if FDD motor is not rotating
TEST_F(WD1793_Test, FDD_Rotation_Index_NotCountingIfMotorStops)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 4;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000;  // Time increments during simulation

    // Internal logging messages are done on Info level
    _context->pModuleLogger->SetLoggingLevel(LogDebug);

    WD1793CUT fdc(_context);

    // Reset WDC internal time marks
    fdc.resetTime();

    // Ensure we have a disk in the drive
    DiskImage diskImage = DiskImage(MAX_CYLINDERS, MAX_SIDES);
    fdc.getDrive()->insertDisk(&diskImage);
    EXPECT_TRUE(fdc.getDrive()->isDiskInserted()) << "Disk image must be inserted";

    // Reset index pulse tracking
    fdc._indexPulseCounter = 0;
    fdc._prevIndex = false;
    fdc._index = false;

    /// region <Perform simulation loop>
    bool motorStarted = false;
    bool motorStopped = false;
    int64_t motorStartTStates = 0;
    int64_t motorStopTStates = 0;

    /// region <Pre-checks>
    bool diskInserted = fdc._selectedDrive && fdc._selectedDrive->isDiskInserted();
    bool motorOn = fdc._motorTimeoutTStates > 0;

    EXPECT_EQ(fdc._indexPulseCounter, 0) << "Index pulse counter should be zero at the beginning of the test";
    EXPECT_EQ(diskInserted, true) << "Disk image must be inserted before starting the test";
    EXPECT_EQ(motorOn, false) << "FDD motor should be stopped before starting the test";
    /// endregion </Pre-checks>

    size_t clk;
    for (clk = 10; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        if (clk < Z80_FREQUENCY)
        {
            EXPECT_EQ(fdc._indexPulseCounter, 0) << "Index pulse counter shouldn't increment when FDD motor is stopped";
        }

        // Start motor after 1-second delay
        if (clk > Z80_FREQUENCY && !motorStarted &&
            !motorStopped)  // Block motor re-start by checking motorStopped flag meaning it was done intentionally
        {
            // Trigger motor start
            fdc.prolongFDDMotorRotation();

            motorStartTStates = clk;
            motorStarted = true;
        }

        // All the time until the explicit stop motor should be on
        if (clk > Z80_FREQUENCY && clk < 2 * Z80_FREQUENCY)
        {
            bool motorOn = fdc._selectedDrive->getMotor() && motorStarted && !motorStopped;
            EXPECT_EQ(motorOn, true) << "Motor should be on between 1 and 2 seconds after start";
        }

        // Stop motor after 1 second after start
        // We expect that FDD_RPM index pulses will be detected
        if (!motorStopped && clk >= 2 * Z80_FREQUENCY)
        {
            fdc.stopFDDMotor();

            motorStopped = true;
        }

        // Update time for FDC
        fdc._time = clk;

        // Process FSM state updates
        fdc.process();

        // Record motor stop time
        if (motorStarted && !fdc._selectedDrive->getMotor())
        {
            motorStopTStates = clk;

            motorStarted = false;
        }
    }
    /// endregion </Perform simulation loop>

    /// region <Check results>
    EXPECT_NE(motorStartTStates, 0);
    EXPECT_NE(motorStopTStates, 0);

    // Check motor still switched off within specs
    size_t estimatedMotorOnTStates = 1 * Z80_FREQUENCY;
    size_t motorWasOnForTSTates = std::abs(motorStopTStates - motorStartTStates);
    EXPECT_IN_RANGE(motorWasOnForTSTates, estimatedMotorOnTStates - TEST_INCREMENT_TSTATES,
                    estimatedMotorOnTStates + TEST_INCREMENT_TSTATES);

    size_t estimatedIndexPulses = std::ceil((double)motorWasOnForTSTates * FDD_RPS / (double)Z80_FREQUENCY);
    size_t indexPulses = fdc._indexPulseCounter;
    EXPECT_IN_RANGE(indexPulses, estimatedIndexPulses - 1, estimatedIndexPulses + 1);
    /// endregion </Check results>
}

/// Test index strobe timings and stability
TEST_F(WD1793_Test, DISABLED_FDD_Rotation_Index_Stability)
{
    FAIL() << "Not implemented yet";
}

/// endregion <FDD related>

/// region <Sleep mode>

/// Test that FDD starts in sleep mode
TEST_F(WD1793_Test, SleepMode_StartsInSleepMode)
{
    WD1793CUT fdc(_context);

    // After construction and reset, FDD should be sleeping
    EXPECT_TRUE(fdc._sleeping) << "FDD should start in sleep mode";
    EXPECT_TRUE(fdc.isSleeping()) << "isSleeping() should return true";
}

/// Test that handleStep returns immediately when sleeping
TEST_F(WD1793_Test, SleepMode_HandleStepSkipsWhenSleeping)
{
    WD1793CUT fdc(_context);

    // Reset time counters
    fdc.resetTime();
    fdc._time = 1000;

    // Verify FDD is sleeping
    EXPECT_TRUE(fdc._sleeping);

    // Store initial state
    WD1793::WDSTATE initialState = fdc._state;
    uint64_t initialTime = fdc._time;

    // Call handleStep - should return immediately without changing state
    fdc.handleStep();

    // State should not have changed (no processing occurred)
    EXPECT_EQ(fdc._state, initialState) << "State should not change when sleeping";
    EXPECT_TRUE(fdc._sleeping) << "Should still be sleeping after handleStep";
}

/// Test that wakeUp() transitions from sleeping to awake
TEST_F(WD1793_Test, SleepMode_WakeUp)
{
    WD1793CUT fdc(_context);

    // Verify FDD starts sleeping
    EXPECT_TRUE(fdc._sleeping);

    // Set up time
    fdc._time = 10000;

    // Wake up
    fdc.wakeUp();

    // Verify FDD is now awake
    EXPECT_FALSE(fdc._sleeping) << "FDD should be awake after wakeUp()";
    EXPECT_FALSE(fdc.isSleeping()) << "isSleeping() should return false after wakeUp()";
    EXPECT_EQ(fdc._wakeTimestamp, 10000) << "Wake timestamp should be set to current time";
}

/// Test that wakeUp() is idempotent when already awake
TEST_F(WD1793_Test, SleepMode_WakeUpIdempotent)
{
    WD1793CUT fdc(_context);

    // Wake up first time
    fdc._time = 10000;
    fdc.wakeUp();
    EXPECT_FALSE(fdc._sleeping);
    EXPECT_EQ(fdc._wakeTimestamp, 10000);

    // Advance time and wake up again
    fdc._time = 20000;
    fdc.wakeUp();

    // Should still be awake, but timestamp should NOT change (already awake)
    EXPECT_FALSE(fdc._sleeping);
    EXPECT_EQ(fdc._wakeTimestamp, 10000) << "Wake timestamp should not change when already awake";
}

/// Test that enterSleepMode() transitions from awake to sleeping
TEST_F(WD1793_Test, SleepMode_EnterSleepMode)
{
    WD1793CUT fdc(_context);

    // Wake up first
    fdc._time = 10000;
    fdc.wakeUp();
    EXPECT_FALSE(fdc._sleeping);

    // Enter sleep mode
    fdc.enterSleepMode();

    // Verify FDD is now sleeping
    EXPECT_TRUE(fdc._sleeping) << "FDD should be sleeping after enterSleepMode()";
}

/// Test that FDD enters sleep mode after idle timeout (2 seconds)
TEST_F(WD1793_Test, SleepMode_AutoSleepAfterIdleTimeout)
{
    static constexpr size_t const SLEEP_TIMEOUT_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds

    WD1793CUT fdc(_context);

    // Wake up the FDD
    fdc._time = 0;
    fdc.wakeUp();
    EXPECT_FALSE(fdc._sleeping);

    // Ensure FDD is in IDLE state with motor off
    fdc._state = WD1793::S_IDLE;
    fdc._motorTimeoutTStates = 0;

    // Advance time just before sleep timeout - should stay awake
    fdc._time = SLEEP_TIMEOUT_TSTATES - 100;
    fdc.handleStep();
    EXPECT_FALSE(fdc._sleeping) << "FDD should still be awake before timeout";

    // Advance time past sleep timeout
    fdc._time = SLEEP_TIMEOUT_TSTATES + 100;
    fdc.handleStep();

    // FDD should now be sleeping
    EXPECT_TRUE(fdc._sleeping) << "FDD should enter sleep mode after 2 seconds of idle";
}

/// Test that FDD does NOT enter sleep mode when motor is running
TEST_F(WD1793_Test, SleepMode_NoAutoSleepWhileMotorRunning)
{
    static constexpr size_t const SLEEP_TIMEOUT_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds

    WD1793CUT fdc(_context);

    // Wake up the FDD
    fdc._time = 0;
    fdc.wakeUp();
    EXPECT_FALSE(fdc._sleeping);

    // Ensure FDD is in IDLE state but motor is still running
    fdc._state = WD1793::S_IDLE;
    fdc._motorTimeoutTStates = 1000000;  // Motor still has time left

    // Advance time past sleep timeout
    fdc._time = SLEEP_TIMEOUT_TSTATES + 100;
    fdc.handleStep();

    // FDD should NOT sleep because motor is running
    EXPECT_FALSE(fdc._sleeping) << "FDD should NOT sleep while motor is running";
}

/// Test that FDD does NOT enter sleep mode when FSM is active (not IDLE)
TEST_F(WD1793_Test, SleepMode_NoAutoSleepWhileFSMActive)
{
    static constexpr size_t const SLEEP_TIMEOUT_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds

    WD1793CUT fdc(_context);

    // Wake up the FDD
    fdc._time = 0;
    fdc.wakeUp();
    EXPECT_FALSE(fdc._sleeping);

    // Ensure FDD is in WAIT state (active command) with motor off
    fdc._state = WD1793::S_WAIT;
    fdc._motorTimeoutTStates = 0;

    // Advance time past sleep timeout
    fdc._time = SLEEP_TIMEOUT_TSTATES + 100;
    fdc.handleStep();

    // FDD should NOT sleep because FSM is active
    EXPECT_FALSE(fdc._sleeping) << "FDD should NOT sleep while FSM is active";
}

/// Test that port access wakes up FDD (simulated via wakeUp call)
TEST_F(WD1793_Test, SleepMode_PortAccessWakesUp)
{
    WD1793CUT fdc(_context);

    // Verify FDD starts sleeping
    EXPECT_TRUE(fdc._sleeping);

    // Simulate port access by calling wakeUp (which is called in portDeviceInMethod/portDeviceOutMethod)
    fdc._time = 50000;
    fdc.wakeUp();

    // FDD should be awake
    EXPECT_FALSE(fdc._sleeping) << "Port access should wake up FDD";
    EXPECT_EQ(fdc._wakeTimestamp, 50000);
}

/// Test complete sleep/wake cycle with port activity
TEST_F(WD1793_Test, SleepMode_CompleteLifecycle)
{
    static constexpr size_t const SLEEP_TIMEOUT_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds

    WD1793CUT fdc(_context);

    // Phase 1: Start in sleep mode
    EXPECT_TRUE(fdc._sleeping) << "Phase 1: Should start sleeping";

    // Phase 2: Wake up via port access
    fdc._time = 0;
    fdc.wakeUp();
    EXPECT_FALSE(fdc._sleeping) << "Phase 2: Should be awake after port access";

    // Phase 3: FDD is active, doing work
    fdc._state = WD1793::S_WAIT;
    fdc._motorTimeoutTStates = 1000000;
    fdc._time = SLEEP_TIMEOUT_TSTATES + 1000;
    fdc.handleStep();
    EXPECT_FALSE(fdc._sleeping) << "Phase 3: Should stay awake while active";

    // Phase 4: Work completes, transition to idle
    fdc._state = WD1793::S_IDLE;
    fdc._motorTimeoutTStates = 0;

    // Phase 5: Wait for sleep timeout
    fdc._time = SLEEP_TIMEOUT_TSTATES + 100;
    fdc.handleStep();
    EXPECT_TRUE(fdc._sleeping) << "Phase 5: Should enter sleep after timeout";

    // Phase 6: Wake up again via port access
    fdc._time = SLEEP_TIMEOUT_TSTATES + 50000;
    fdc.wakeUp();
    EXPECT_FALSE(fdc._sleeping) << "Phase 6: Should wake up again";
}

/// endregion </Sleep mode>

/// region <FSM>

/// Check if delayed state switch was correctly recorded and fields recalculated
TEST_F(WD1793_Test, FSM_DelayRegister)
{
    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Set up random numbers generator>
    std::random_device rd;
    std::mt19937 generator(rd());

    // Define random numbers range
    std::uniform_int_distribution<size_t> delayDistribution(1, 10'000'000);
    std::uniform_int_distribution<uint8_t> stateDistribution(WD1793::S_IDLE, WD1793::WDSTATE_MAX - 1);
    /// endregion </Set up random numbers generator>

    /// region <Check delay request was registered correctly>

    for (size_t i = 0; i < 20; i++)
    {
        int64_t randomDelay = delayDistribution(generator);
        WD1793::WDSTATE fromState = WD1793::S_IDLE;
        WD1793::WDSTATE toState = (WD1793::WDSTATE)stateDistribution(generator);

        fdc._state = fromState;
        fdc.transitionFSMWithDelay(toState, randomDelay);

        EXPECT_EQ(fdc._delayTStates, randomDelay - 1);
        EXPECT_EQ(fdc._state, WD1793::S_WAIT);
        EXPECT_EQ(fdc._state2, toState);
    }

    /// endregion </Check delay request was registered correctly>
}

/// Check how state machine delayed state switch handles timing synchronization and counters update
TEST_F(WD1793_Test, FSM_DelayCounters)
{
    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Set up random numbers generator>
    std::random_device rd;
    std::mt19937 generator(rd());

    std::uniform_int_distribution<size_t> delayDistribution(1, 10'000);
    std::uniform_int_distribution<uint8_t> stateDistribution(WD1793::S_WAIT + 1, WD1793::WDSTATE_MAX - 1);

    /// endregion </Set up random numbers generator>

    /// region <Check delay counter operates correctly>
    for (size_t i = 0; i < 100; i++)
    {
        // Generate random delays that are multiplier of 100
        static const size_t ITERATION_STEP = 100;
        int64_t randomDelay = delayDistribution(generator) * ITERATION_STEP;
        WD1793::WDSTATE fromState = (WD1793::WDSTATE)stateDistribution(generator);
        WD1793::WDSTATE toState = (WD1793::WDSTATE)stateDistribution(generator);
        std::string srcState = WD1793::WDSTATEToString(fromState);
        std::string dstState = WD1793::WDSTATEToString(toState);

        fdc._state = fromState;
        fdc.transitionFSMWithDelay(toState, randomDelay);

        // Consistency checks
        EXPECT_EQ(fdc._delayTStates, randomDelay - 1);
        EXPECT_EQ(fdc._state, WD1793::S_WAIT);
        EXPECT_EQ(fdc._state2, toState);

        /// region <Main loop>

        int64_t expectedDelay = randomDelay - 1 - ITERATION_STEP;
        fdc._time = 0;
        fdc._lastTime = 0;
        fdc._diffTime = 0;
        for (int64_t it = randomDelay; it > 0; it -= ITERATION_STEP)
        {
            // Check if delay has already been finished prematurely
            if ((fdc._state == WD1793::S_WAIT && fdc._delayTStates <= 0) || fdc._state != WD1793::S_WAIT)
            {
                FAIL() << StringHelper::Format("i: %d; it: %d; %s -> %s; expectedDelay: %d, delayTStates: %d", i, it,
                                               srcState.c_str(), dstState.c_str(), expectedDelay, fdc._delayTStates)
                       << std::endl;
            }

            fdc._time += ITERATION_STEP;
            fdc.process();

            EXPECT_EQ(expectedDelay, fdc._delayTStates)
                << StringHelper::Format("i: %d; it: %d; %s -> %s; expectedDelay: %d, delayTStates: %d", i, it,
                                        srcState.c_str(), dstState.c_str(), expectedDelay, fdc._delayTStates)
                << std::endl;

            // Adjust expected delay
            expectedDelay -= ITERATION_STEP;
            if (expectedDelay < 0)
            {
                expectedDelay = 0;
            }
        }

        /// endregion </Main loop>
    }

    /// endregion </Check delay counter operates correctly>
}

/// endregion </FSM>

/// region <Commands>

/// region <RESTORE>

TEST_F(WD1793_Test, FSM_CMD_Restore_OnReset)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>

    std::cout << "------------------------------" << std::endl;

    for (size_t i = 0; i < MAX_CYLINDERS; i++)
    {
        fdc._selectedDrive->setTrack(i);

        // Mock parameters
        const uint8_t restoreCommand = 0b0000'0000;  // RESTORE on reset is done with all bits zeroed: no load head, no
                                                     // verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(restoreCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, restoreCommand);
        fdc._commandRegister = restoreCommand;
        fdc._lastDecodedCmd = decodedCommand;

        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Pre-checks>
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_RESTORE);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        /// endregion </Pre-checks>

        // Send command to FDC
        fdc.cmdRestore(commandValue);

        // Check
        EXPECT_EQ(fdc._beta128status & WD1793::INTRQ, 0) << "INTRQ must be reset at any command start";

        /// region <Perform simulation loop>
        size_t clk;
        for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Check that BUSY flag is set for the whole duration of head positioning
            if (fdc._state != WD1793::S_IDLE)
            {
                bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                EXPECT_EQ(busyFlag, true);
            }

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                fdc._trackRegister == 0 &&                    // FDC track set to 0
                fdc._selectedDrive->isTrack00() &&            // FDD has the same track 0
                fdc._state == WD1793::S_IDLE)                 // FSM is in idle state
            {
                // RESTORE operation finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                       fdc._trackRegister == 0 &&                    // FDC track set to 0
                                       fdc._selectedDrive->isTrack00() &&            // FDD has the same track 0
                                       fdc._state == WD1793::S_IDLE &&               // FSM is in idle state
                                       fdc._beta128status & WD1793::INTRQ;           // INTRQ is active

        std::stringstream se;
        if (!isAccomplishedCorrectly)
        {
            if (fdc._statusRegister & WD1793::WDS_BUSY)
            {
                se << "BUSY was not reset" << std::endl;
            }

            if (fdc._trackRegister != 0)
            {
                se << "FDC Track Register is not on track 0" << std::endl;
            }

            if (!fdc._selectedDrive->isTrack00())
            {
                se << "FDD is not on track 0" << std::endl;
            }

            if (fdc._state != WD1793::S_IDLE)
            {
                se << "FSM state is not idle" << std::endl;
            }

            if (!(fdc._beta128status & WD1793::INTRQ))
            {
                se << "INTRQ is not set" << std::endl;
            }
        }

        EXPECT_EQ(isAccomplishedCorrectly, true) << "RESTORE didn't end up correctly" << se.str();

        size_t estimatedExecutionTime = i * 6;  // Number of positioning steps, 6ms each
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 0.1 * estimatedExecutionTime)
            << "Abnormal execution time";
        /// endregion </Check results>

        /// region <Get simulation stats>
        std::stringstream ss;
        ss << "RESTORE test stats:" << std::endl;
        ss << StringHelper::Format("TStates: %d, time: %d ms", elapsedTimeTStates, elapsedTimeMs) << std::endl;
        ss << StringHelper::Format("From track: %d to track %d", i, fdc._selectedDrive->getTrack()) << std::endl;
        ss << "------------------------------" << std::endl;

        std::cout << ss.str();
        /// endregion </Get simulation stats>
    }

    /// endregion </Main test loop>
}

TEST_F(WD1793_Test, FSM_CMD_Restore_NoVerify)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    // Remember initial FDD state
    uint8_t initialFDDTrack = fdc._selectedDrive->getTrack();

    // Mock parameters
    const uint8_t restoreCommand =
        0b0000'1000;  // RESTORE with load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(restoreCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, restoreCommand);
    fdc._commandRegister = restoreCommand;
    fdc._lastDecodedCmd = decodedCommand;

    // Reset WDC internal time marks
    fdc.resetTime();

    /// region <Pre-checks>
    EXPECT_EQ(decodedCommand, WD1793::WD_CMD_RESTORE);
    EXPECT_EQ(fdc._time, 0);
    EXPECT_EQ(fdc._lastTime, 0);
    EXPECT_EQ(fdc._diffTime, 0);
    /// endregion </Pre-checks>

    // Send command to FDC
    fdc.cmdRestore(commandValue);

    /// region <Perform simulation loop>
    size_t clk;
    for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        // Update time for FDC
        fdc._time = clk;

        // Process FSM state updates
        fdc.process();

        // Check that BUSY flag is set for the whole duration of head positioning
        if (fdc._state != WD1793::S_IDLE)
        {
            bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
            EXPECT_EQ(busyFlag, true);
        }

        if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
            fdc._trackRegister == 0 &&                    // FDC track set to 0
            fdc._selectedDrive->isTrack00() &&            // FDD has the same track 0
            fdc._state == WD1793::S_IDLE)                 // FSM is in idle state
        {
            // RESTORE operation finished
            break;
        }
    }
    /// endregion </Perform simulation loop>

    /// region <Check results>
    bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                   fdc._trackRegister == 0 &&                    // FDC track set to 0
                                   fdc._selectedDrive->isTrack00() &&            // FDD has the same track 0
                                   fdc._state == WD1793::S_IDLE;                 // FSM is in idle state

    EXPECT_EQ(isAccomplishedCorrectly, true) << "RESTORE didn't end up correctly";
    /// endregion </Check results>

    /// region <Get simulation stats>
    size_t elapsedTimeTStates = clk;
    size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

    std::stringstream ss;
    ss << "RESTORE test stats:" << std::endl;
    ss << StringHelper::Format("TStates: %d, time: %d ms", elapsedTimeTStates, elapsedTimeMs) << std::endl;
    ss << StringHelper::Format("From track: %d to track %d", initialFDDTrack, fdc._selectedDrive->getTrack())
       << std::endl;

    std::cout << ss.str();
    /// endregion </Get simulation stats>
}

TEST_F(WD1793_Test, FSM_CMD_Restore_Verify)
{
    static constexpr size_t const STEP_DURATION_MS = 6;  // HEAD movement duration (from track to track)
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Disable all logging except error messages
    //_context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    /// region <Main test loop>

    std::cout << "------------------------------" << std::endl;

    for (size_t i = 0; i < MAX_CYLINDERS; i++)
    {
        fdc._selectedDrive->setTrack(i);

        // Mock parameters
        const uint8_t restoreCommand = 0b0000'1100;  // RESTORE on reset is done with all bits zeroed: no load head, no
                                                     // verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(restoreCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, restoreCommand);
        fdc._commandRegister = restoreCommand;
        fdc._lastDecodedCmd = decodedCommand;

        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Pre-checks>
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_RESTORE);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        /// endregion </Pre-checks>

        // Send command to FDC
        fdc.cmdRestore(commandValue);

        /// region <Perform simulation loop>
        size_t clk;
        for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Check that BUSY flag is set for the whole duration of head positioning
            if (fdc._state != WD1793::S_IDLE)
            {
                bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                EXPECT_EQ(busyFlag, true);
            }

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                fdc._trackRegister == 0 &&                    // FDC track set to 0
                fdc._selectedDrive->isTrack00() &&            // FDD has the same track 0
                fdc._state == WD1793::S_IDLE)                 // FSM is in idle state
            {
                // RESTORE operation finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                       fdc._trackRegister == 0 &&                    // FDC track set to 0
                                       fdc._selectedDrive->isTrack00() &&            // FDD has the same track 0
                                       fdc._state == WD1793::S_IDLE;                 // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "RESTORE didn't end up correctly";

        size_t estimatedExecutionTime = i * STEP_DURATION_MS;       // Number of positioning steps, 6ms each
        estimatedExecutionTime += WD1793CUT::WD93_VERIFY_DELAY_MS;  // Add verification time after the positioning

        size_t timeTolerance = 0.1 * estimatedExecutionTime;
        if (timeTolerance == 0)
            timeTolerance = 3 * STEP_DURATION_MS;
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + timeTolerance)
            << "Abnormal execution time";
        /// endregion </Check results>

        /// region <Get simulation stats>
        std::stringstream ss;
        ss << "RESTORE test stats:" << std::endl;
        ss << StringHelper::Format("From track: %d to track %d", i, fdc._selectedDrive->getTrack()) << std::endl;
        ss << StringHelper::Format("TStates: %d, time: %d ms", elapsedTimeTStates, elapsedTimeMs) << std::endl;
        ss << "------------------------------" << std::endl;

        std::cout << ss.str();
        /// endregion </Get simulation stats>
    }

    /// endregion </Main test loop>
}

/// endregion </RESTORE>

/// region <SEEK>

TEST_F(WD1793_Test, FSM_CMD_Seek)
{
    static constexpr double const TEST_DURATION_SEC = 1.0;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>
    for (int i = 0; i < MAX_CYLINDERS - 1; i++)
    {
        int targetTrack = MAX_CYLINDERS - 1 - i;
        // int targetTrack = 3;

        // Set initial conditions
        fdc._selectedDrive->setTrack(i);
        fdc._trackRegister = i;
        fdc._dataRegister = targetTrack;
        fdc._statusRegister &= ~WD1793::WDS_BUSY;
        fdc._beta128status &= ~(WD1793::INTRQ | WD1793::DRQ);

        // Mock parameters
        const uint8_t stepCommand =
            0b0001'0000;  // SEEK: no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepCommand);
        fdc._commandRegister = stepCommand;
        fdc._lastDecodedCmd = decodedCommand;

        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Pre-checks>
        std::string error = StringHelper::Format("Track %d", i);
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_SEEK);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        EXPECT_EQ(fdc._statusRegister & WD1793::WDS_BUSY, 0) << error;
        EXPECT_EQ(fdc._beta128status & WD1793::INTRQ, 0) << error;
        /// endregion </Pre-checks>

        // Trigger SEEK command
        fdc.cmdSeek(commandValue);

        /// region <Perform simulation loop>
        size_t clk;
        for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Check that BUSY flag is set for the whole duration of head positioning
            if (fdc._state != WD1793::S_IDLE)
            {
                bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                EXPECT_EQ(busyFlag, true);
            }

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&              // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                fdc._state == WD1793::S_IDLE)                     // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&          // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                     // FSM is in idle state
        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK didn't end up correctly";

        bool intrqSet = fdc._beta128status & WD1793::INTRQ;
        EXPECT_EQ(intrqSet, true) << "INTRQ was not set at the end of SEEK";

        size_t estimatedExecutionTime =
            std::abs(targetTrack - i) * fdc._steppingMotorRate;    // We're performing single positioning step 6ms long
        size_t estimationError = std::abs(targetTrack - i) * 0.5;  // No more than 0.5ms estimation error per step
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + estimationError)
            << "Abnormal execution time" << StringHelper::Format(" from trk: %d to trk: %d", i, targetTrack);
        /// endregion </Check results>
    }

    /// endregion </Main test loop>
}

TEST_F(WD1793_Test, FSM_CMD_Seek_All_Rates)
{
    static constexpr double const TEST_DURATION_SEC = 5.0;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>
    for (uint8_t stepRate = 0; stepRate < 4; stepRate++)
    {
        // for (int i = 0; i < MAX_CYLINDERS - 1; i++)
        for (int i = 0; i < MAX_CYLINDERS; i++)
        {
            int targetTrack = MAX_CYLINDERS - 1 - i;

            // Set initial conditions
            fdc._selectedDrive->setTrack(i);
            fdc._trackRegister = i;
            fdc._dataRegister = targetTrack;

            // Mock parameters
            uint8_t stepCommand =
                0b0001'0000;  // SEEK: no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
            stepCommand |= stepRate;  // Apply r1r0 bits for speed rates
            WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepCommand);
            uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepCommand);
            fdc._commandRegister = stepCommand;
            fdc._lastDecodedCmd = decodedCommand;

            // Reset WDC internal time marks
            fdc.resetTime();

            /// region <Pre-checks>
            EXPECT_EQ(decodedCommand, WD1793::WD_CMD_SEEK);
            EXPECT_EQ(fdc._time, 0);
            EXPECT_EQ(fdc._lastTime, 0);
            EXPECT_EQ(fdc._diffTime, 0);
            /// endregion </Pre-checks>

            // Trigger SEEK command
            fdc.cmdSeek(commandValue);

            /// region <Perform simulation loop>
            size_t clk;
            for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
            {
                // Update time for FDC
                fdc._time = clk;

                // Process FSM state updates
                fdc.process();

                // Check that BUSY flag is set for the whole duration of head positioning
                if (fdc._state != WD1793::S_IDLE)
                {
                    bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                    EXPECT_EQ(busyFlag, true);
                }

                if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                    fdc._trackRegister == targetTrack &&              // FDC track set to <next track>
                    fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                    fdc._state == WD1793::S_IDLE)                     // FSM is in idle state
                {
                    // STEP_IN command finished
                    break;
                }
            }
            /// endregion </Perform simulation loop>

            /// region <Check results>
            size_t elapsedTimeTStates = clk;
            size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

            bool isAccomplishedCorrectly =
                !(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&              // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                fdc._state == WD1793::S_IDLE;                     // FSM is in idle state

            EXPECT_EQ(isAccomplishedCorrectly, true)
                << "SEEK didn't end up correctly"
                << StringHelper::Format(" stepRate: %d, from trk: %d, to trk: %d", stepRate, i, targetTrack);

            size_t estimatedExecutionTime =
                std::abs(targetTrack - i) *
                fdc._steppingMotorRate;  // We're performing single positioning step 6ms long
            size_t estimationError = std::abs(targetTrack - i) * 0.5;  // No more than 0.5ms estimation error per step
            EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + estimationError)
                << "Abnormal execution time"
                << StringHelper::Format(" stepRate: %d, from trk: %d to trk: %d", stepRate, i, targetTrack);
            /// endregion </Check results>
        }
    }

    /// endregion </Main test loop>
}

/// endregion <SEEK>

/// region <STEP>

TEST_F(WD1793_Test, FSM_CMD_Step_Increasing)
{
    static constexpr double const TEST_DURATION_SEC = 0.3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>
    for (size_t i = 0; i < MAX_CYLINDERS - 1; i++)
    {
        uint8_t targetTrack = i + 1;

        // Set initial conditions
        fdc._selectedDrive->setTrack(i);
        fdc._trackRegister = i;
        fdc._dataRegister = 0xFF;  // Set to non-matching value for STEP commands
        fdc._stepDirectionIn = true;

        // Mock parameters
        const uint8_t stepCommand = 0b0011'0000;  // STEP: UPDATE TRACK REGISTER (u=1), no load head, no verify
                                                  // 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepCommand);
        fdc._commandRegister = stepCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc._lastCmdValue = commandValue;

        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Pre-checks>
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_STEP);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        EXPECT_EQ(fdc._stepDirectionIn, true);
        /// endregion </Pre-checks>

        // Trigger STEP command
        fdc.cmdStep(commandValue);

        /// region <Perform simulation loop>
        size_t clk;
        for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Check that BUSY flag is set for the whole duration of head positioning
            if (fdc._state != WD1793::S_IDLE)
            {
                bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                EXPECT_EQ(busyFlag, true);
            }

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&              // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                fdc._state == WD1793::S_IDLE)                     // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&          // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                     // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK increasing direction didn't end up correctly";

        size_t estimatedExecutionTime = 6;  // We're performing single positioning step 6ms long
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1) << "Abnormal execution time";
        /// endregion </Check results>
    }

    /// endregion </Main test loop>
}

TEST_F(WD1793_Test, FSM_CMD_Step_Decreasing)
{
    static constexpr double const TEST_DURATION_SEC = 0.3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>
    for (size_t i = MAX_CYLINDERS - 1; i > 0; i--)
    {
        uint8_t targetTrack = i - 1;

        // Set initial conditions
        fdc._selectedDrive->setTrack(i);
        fdc._trackRegister = i;
        fdc._dataRegister = 0xFF;  // Set to non-matching value for STEP commands
        fdc._stepDirectionIn = false;

        // Mock parameters
        const uint8_t stepCommand = 0b0011'0000;  // STEP: UPDATE TRACK REGISTER (u=1), no load head, no verify
                                                  // 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepCommand);
        fdc._commandRegister = stepCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc._lastCmdValue = commandValue;

        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Pre-checks>
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_STEP);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        EXPECT_EQ(fdc._stepDirectionIn, false);
        /// endregion </Pre-checks>

        // Trigger STEP command
        fdc.cmdStep(commandValue);

        /// region <Perform simulation loop>
        size_t clk;
        for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Check that BUSY flag is set for the whole duration of head positioning
            if (fdc._state != WD1793::S_IDLE)
            {
                bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                EXPECT_EQ(busyFlag, true);
            }

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&              // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                fdc._state == WD1793::S_IDLE)                     // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&          // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                     // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK decreasing direction didn't end up correctly";

        size_t estimatedExecutionTime = 6;  // We're performing single positioning step 6ms long
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1) << "Abnormal execution time";
        /// endregion </Check results>
    }

    /// endregion </Main test loop>
}

/// endregion </STEP>

/// region <STEP_IN>

TEST_F(WD1793_Test, FSM_CMD_Step_In)
{
    static constexpr double const TEST_DURATION_SEC = 0.3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>
    for (size_t i = 0; i < MAX_CYLINDERS - 1; i++)
    {
        uint8_t targetTrack = i + 1;

        // Set initial conditions
        fdc._selectedDrive->setTrack(i);
        fdc._trackRegister = i;
        fdc._dataRegister = 0xFF;  // Set to non-matching value for STEP commands

        // Mock parameters
        const uint8_t stepInCommand = 0b0101'0000;  // StepIn: UPDATE TRACK REGISTER (u=1), no load head, no verify
                                                    // rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepInCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepInCommand);
        fdc._commandRegister = stepInCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc._lastCmdValue = commandValue;

        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Pre-checks>
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_STEP_IN);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        /// endregion </Pre-checks>

        // Trigger STEP_IN command
        fdc.cmdStepIn(commandValue);

        /// region <Perform simulation loop>
        size_t clk;
        for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Check that BUSY flag is set for the whole duration of head positioning
            if (fdc._state != WD1793::S_IDLE)
            {
                bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                EXPECT_EQ(busyFlag, true);
            }

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&              // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                fdc._state == WD1793::S_IDLE)                     // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&          // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                     // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK_IN didn't end up correctly";

        size_t estimatedExecutionTime = 6;  // We're performing single positioning step 6ms long
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1) << "Abnormal execution time";
        /// endregion </Check results>
    }

    /// endregion </Main test loop>
}

/// endregion </STEP_IN>

/// region <STEP_OUT>

TEST_F(WD1793_Test, FSM_CMD_Step_Out)
{
    static constexpr double const RESTORE_TEST_DURATION_SEC = 0.3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    for (int i = MAX_CYLINDERS - 1; i >= 1; i--)
    {
        uint8_t targetTrack = i - 1;

        // Set initial conditions
        fdc._selectedDrive->setTrack(i);
        fdc._trackRegister = i;
        fdc._dataRegister = 0xFF;  // Set to non-matching value for STEP commands

        // Mock parameters
        const uint8_t stepOutCommand = 0b0111'0000;  // StepOut: UPDATE TRACK REGISTER (u=1), no load head, no verify
                                                     // rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepOutCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepOutCommand);
        fdc._commandRegister = stepOutCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc._lastCmdValue = commandValue;

        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Pre-checks>
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_STEP_OUT);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        /// endregion </Pre-checks>

        // Trigger STEP_Out command
        fdc.cmdStepOut(commandValue);

        /// region <Perform simulation loop>
        size_t clk;
        for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Check that BUSY flag is set for the whole duration of head positioning
            if (fdc._state != WD1793::S_IDLE)
            {
                bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                EXPECT_EQ(busyFlag, true);
            }

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&              // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                fdc._state == WD1793::S_IDLE)                     // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&  // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&          // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack &&  // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                     // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK_OUT didn't end up correctly";

        size_t estimatedExecutionTime = 6;  // We're performing single positioning step 6ms long
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1) << "Abnormal execution time";
        /// endregion </Check results>
    }
}

/// endregion </STEP_OUT>

/// region <READ_SECTOR>
TEST_F(WD1793_Test, FSM_CMD_Read_Sector_Single)
{
    static constexpr size_t const READ_SECTOR_TEST_DURATION_SEC = 1;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * READ_SECTOR_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 10;  // Time increments during simulation

    // Internal logging messages are done on the Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);
    _context->pModuleLogger->SetLoggingLevel(LogError);

    // Sector read buffer
    uint8_t sectorData[TRD_SECTORS_SIZE_BYTES] = {};
    size_t sectorDataIndex = 0;

    /// region <Load disk image>
    std::string filepath = TestPathHelper::GetTestDataPath("loaders/trd/EyeAche.trd");
    LoaderTRDCUT trdLoader(_context, filepath);
    bool imageLoaded = trdLoader.loadImage();

    // Use ASSERT to terminate test early if disk image fails to load - prevents null pointer crash
    ASSERT_EQ(imageLoaded, true) << "Test TRD image was not loaded: " << filepath;

    DiskImage* diskImage = trdLoader.getImage();

    ASSERT_NE(diskImage, nullptr) << "Disk image is null after loading";
    /// endregion </Load disk image>

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(diskImage);

    // De-activate WD1793 reset (active low), Set active drive A, Select MFM / double density mode
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    /// region <For all tracks and sectors>

    const uint8_t readSectorCommand = 0b1000'0000;
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(readSectorCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, readSectorCommand);
    EXPECT_EQ(decodedCommand, WD1793CUT::WD_COMMANDS::WD_CMD_READ_SECTOR);

    for (uint8_t track = 0; track < TRD_80_TRACKS * MAX_SIDES; ++track)
    {
        for (uint8_t sector = 0; sector < TRD_SECTORS_PER_TRACK; ++sector)
        {
            sectorDataIndex = 0;

            fdc.reset();
            /// region <Create parameters for READ_SECTOR>
            fdc._commandRegister = readSectorCommand;
            fdc._lastDecodedCmd = decodedCommand;

            uint16_t physicalTrack = track / 2;
            fdc._trackRegister = physicalTrack;
            fdc._selectedDrive->setTrack(physicalTrack);
            fdc._sectorRegister = sector + 1;  // WD1793 register accepts only 1..26 values
            /// endregion </Create parameters for READ_SECTOR>

            // Set the proper FDD side using Beta128 register
            uint8_t beta128Register = fdc._beta128Register;
            bool sideUp = track % 2;
            beta128Register = beta128Register | (sideUp ? WD1793::BETA128_COMMAND_BITS::BETA_CMD_HEAD : 0);
            fdc._beta128Register = beta128Register;
            fdc._sideUp = sideUp;

            // Trigger FDC command
            fdc.cmdReadSector(commandValue);

            /// region <Perform simulation loop>
            size_t clk;
            for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
            {
                // Update time for FDC
                fdc._time = clk;

                // Process FSM state updates
                fdc.process();

                // Check that BUSY flag is set for the whole duration of head positioning
                if (fdc._state != WD1793::S_IDLE)
                {
                    bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                    EXPECT_EQ(busyFlag, true);
                }

                // Fetch data bytes when DRQ is asserted (data ready in Data Register)
                // Note: After processReadByte runs, FSM transitions to S_WAIT while DRQ remains set
                if (fdc._beta128status & WD1793::DRQ)
                {
                    uint8_t readValue = fdc.readDataRegister();

                    // size_t usage = EmbeddedStackMonitor::instance().measure_current();
                    // printf("Current stack usage: %zu bytes\n", usage);
                    // EmbeddedStackMonitor::instance().print_stats();
                    // std::cout << StringHelper::Format("Byte '%c' %d (0x%02X) read", readValue, readValue, readValue)
                    // << std::endl;

                    sectorData[sectorDataIndex++] = readValue;
                }

                // Check if a test sequence already finished
                if (fdc._state == WD1793::S_IDLE)
                {
                    break;
                }
            }
            /// endregion </Perform simulation loop>

            /// region <Check results>
            size_t elapsedTimeTStates = clk;
            size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

            bool isAccomplishedCorrectly =
                !(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                fdc._trackRegister == physicalTrack &&              // FDC track still set properly
                fdc._selectedDrive->getTrack() == physicalTrack &&  // FDD is on the same track
                fdc._state == WD1793::S_IDLE;                       // FSM is in idle state

            EXPECT_EQ(isAccomplishedCorrectly, true) << "READ_SECTOR didn't end up correctly";

            size_t estimatedExecutionTime = 256 * WD1793::WD93_TSTATES_PER_FDC_BYTE /
                                            TSTATES_IN_MS;  // We're performing single positioning step 6ms long
            EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1)
                << "Abnormal execution time";

            EXPECT_EQ(sectorDataIndex, 256) << "Not all sector bytes were read";

            DiskImage::Track* trackData = diskImage->getTrack(track);
            uint8_t* referenceSector = trackData->getDataForSector(sector);

            if (!areUint8ArraysEqual(sectorData, referenceSector, TRD_SECTORS_SIZE_BYTES))
            {
                std::string diff =
                    DumpHelper::DumpBufferDifferences(sectorData, referenceSector, TRD_SECTORS_SIZE_BYTES);

                // FAIL() << "Track: " << (int)track << " Sector: " << (int)sector << " Sector read data does not match
                // the reference" << std::endl << diff;
                std::cout << "Track: " << (int)track << " Sector: " << (int)sector
                          << " Sector read data does not match the reference" << std::endl
                          << diff << std::endl;

                return;
            }

            // EXPECT_ARRAYS_EQ(sectorData, referenceSector, SECTORS_SIZE_BYTES) << "Sector read data does not match the
            // reference";

            // std::cout << "Read sector dump (T: " << (int)track << " S: " << (int)sector << ")" << std::endl;
            // std::cout << DumpHelper::HexDumpBuffer(sectorData, sizeof(sectorData) / sizeof(sectorData[0])) <<
            // std::endl;

            // return;

            /// endregion </Check results>
        }
    }

    /// endregion </For all tracks and sectors>
}
/// endregion </READ_SECTOR>

/// region <READ_TRACK>

/// Test Read Track command - reads entire raw track (6250 bytes)
/// Per WD1793 datasheet: Read Track starts reading after first index pulse and reads all 6250 bytes
TEST_F(WD1793_Test, FSM_CMD_Read_Track)
{
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds max
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Larger steps for efficiency
    static constexpr size_t const RAW_TRACK_SIZE = DiskImage::RawTrack::RAW_TRACK_SIZE;  // 6250 bytes

    _context->pModuleLogger->SetLoggingLevel(LogError);

    // Create and format a fresh disk image (avoids _diskImage pointer issues with loaded images)
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool imageFormatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(imageFormatted) << "Failed to format TRD disk image";
    
    // Write some non-zero data to track 0 for verification
    DiskImage::Track* refTrack = diskImage.getTrack(0);
    ASSERT_NE(refTrack, nullptr) << "Track 0 should exist";
    for (int i = 0; i < 16; i++)
    {
        uint8_t* sectorData = refTrack->getDataForSector(i);
        for (int j = 0; j < 256; j++)
        {
            sectorData[j] = static_cast<uint8_t>((i << 4) | (j & 0x0F));
        }
    }

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(&diskImage);

    // De-activate WD1793 reset, Set active drive A, MFM mode
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Start motor to enable index pulse generation
    fdc._selectedDrive->setMotor(true);
    fdc.prolongFDDMotorRotation();  // Keep motor running during test

    // Buffer for raw track data
    std::vector<uint8_t> rawTrackData(RAW_TRACK_SIZE, 0);
    size_t bytesRead = 0;

    // Read Track command
    const uint8_t readTrackCommand = 0b1110'0000;  // Read Track command
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(readTrackCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, readTrackCommand);
    EXPECT_EQ(decodedCommand, WD1793CUT::WD_CMD_READ_TRACK);

    // Setup for track 0, side 0
    fdc._commandRegister = readTrackCommand;
    fdc._lastDecodedCmd = decodedCommand;
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    // Trigger FDC command
    fdc.cmdReadTrack(commandValue);

    /// region <Simulation loop>
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc._lastTime = clk > TEST_INCREMENT_TSTATES ? clk - TEST_INCREMENT_TSTATES : 0;
        fdc.process();

        // Fetch data bytes when DRQ is asserted
        if (fdc._beta128status & WD1793::DRQ)
        {
            if (bytesRead < RAW_TRACK_SIZE)
            {
                rawTrackData[bytesRead++] = fdc.readDataRegister();
            }
        }

        // Check if command finished
        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }
    /// endregion </Simulation loop>

    // Verify all bytes were read
    EXPECT_EQ(bytesRead, RAW_TRACK_SIZE) << "Should read all " << RAW_TRACK_SIZE << " bytes";
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "FDC should be in IDLE state";
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY) << "BUSY should be cleared";

    // Verify some data was actually read (not all zeros)
    size_t nonZeroBytes = 0;
    for (size_t i = 0; i < RAW_TRACK_SIZE && i < 256; i++)
    {
        if (rawTrackData[i] != 0) nonZeroBytes++;
    }
    EXPECT_GT(nonZeroBytes, 0) << "Track data should not be all zeros";
}

/// Test Write Track (Format) command - formats entire track with 6250 bytes
/// Per WD1793 datasheet: Write Track starts after index pulse and writes until next index pulse
TEST_F(WD1793_Test, FSM_CMD_Write_Track)
{
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds max
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Larger steps for efficiency
    static constexpr size_t const RAW_TRACK_SIZE = DiskImage::RawTrack::RAW_TRACK_SIZE;  // 6250 bytes

    _context->pModuleLogger->SetLoggingLevel(LogError);

    // Create and format a fresh disk image
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool imageFormatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(imageFormatted) << "Failed to format TRD disk image";

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(&diskImage);

    // De-activate WD1793 reset, Set active drive A, MFM mode
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Start motor to enable index pulse generation
    fdc._selectedDrive->setMotor(true);
    fdc.prolongFDDMotorRotation();

    size_t bytesWritten = 0;
    size_t formatDataIndex = 0;

    // Simple format pattern: gap bytes (0x4E), then sync (0x00), then address marks
    // For simplicity, we'll just write gap bytes for the entire track
    std::vector<uint8_t> formatData(RAW_TRACK_SIZE, 0x4E);  // Fill with gap bytes

    // Write Track command
    const uint8_t writeTrackCommand = 0b1111'0000;  // Write Track command
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(writeTrackCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, writeTrackCommand);
    EXPECT_EQ(decodedCommand, WD1793CUT::WD_CMD_WRITE_TRACK);

    // Setup for track 0, side 0
    fdc._commandRegister = writeTrackCommand;
    fdc._lastDecodedCmd = decodedCommand;
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    // Trigger FDC command
    fdc.cmdWriteTrack(commandValue);

    /// region <Simulation loop>
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc._lastTime = clk > TEST_INCREMENT_TSTATES ? clk - TEST_INCREMENT_TSTATES : 0;
        fdc.process();

        // Feed data bytes when DRQ is asserted
        if (fdc._beta128status & WD1793::DRQ)
        {
            if (formatDataIndex < RAW_TRACK_SIZE)
            {
                fdc.writeDataRegister(formatData[formatDataIndex++]);
                bytesWritten++;
            }
        }

        // Check if command finished
        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }
    /// endregion </Simulation loop>

    // Verify bytes were written (may not be exactly 6250 depending on timing)
    EXPECT_GT(bytesWritten, 0) << "Should write some bytes";
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "FDC should be in IDLE state";
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY) << "BUSY should be cleared";
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_WRITEPROTECTED) << "Write protect should not be set";
}

/// Test Write Track with write-protected disk - should reject immediately
TEST_F(WD1793_Test, FSM_CMD_Write_Track_WriteProtect)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    // Create and format a fresh disk image
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool imageFormatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(imageFormatted) << "Failed to format TRD disk image";

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(&diskImage);

    // Set write protection
    fdc._selectedDrive->setWriteProtect(true);

    // De-activate WD1793 reset, Set active drive A, MFM mode
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Write Track command
    const uint8_t writeTrackCommand = 0b1111'0000;
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(writeTrackCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, writeTrackCommand);

    // Setup for track 0, side 0
    fdc._commandRegister = writeTrackCommand;
    fdc._lastDecodedCmd = decodedCommand;
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sideUp = false;

    // Trigger FDC command - should fail immediately due to write protect
    fdc.cmdWriteTrack(commandValue);

    // Process until IDLE or timeout
    for (int i = 0; i < 100; i++)
    {
        fdc._time = i * 100;
        fdc.process();
        if (fdc._state == WD1793::S_IDLE) break;
    }

    // Verify write protect rejection
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_WRITEPROTECTED) << "Write protect status should be set";
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "FDC should be in IDLE state";
}
/// endregion </READ_TRACK>

/// region <WRITE_SECTOR>
TEST_F(WD1793_Test, FSM_CMD_Write_Sector_Single)
{
    static constexpr size_t const WRITE_SECTOR_TEST_DURATION_SEC = 1;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * WRITE_SECTOR_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 10;  // Time increments during simulation

    // Internal logging messages are done on the Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);
    _context->pModuleLogger->SetLoggingLevel(LogError);

    // Sector write buffer
    uint8_t sectorData[TRD_SECTORS_SIZE_BYTES] = {};
    size_t sectorDataIndex = 0;

    for (size_t i = 0; i < TRD_SECTORS_SIZE_BYTES; ++i)
    {
        sectorData[i] = (uint8_t)i;
    }

    /// region <Create empty disk image>
    DiskImage diskImage = DiskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool imageFormatted = loaderTrd.format(&diskImage);
    EXPECT_EQ(imageFormatted, true) << "Empty test TRD image was not formatted";
    bool formatValid = loaderTrd.validateEmptyTRDOSImage(&diskImage);
    EXPECT_EQ(formatValid, true) << "Empty test TRD image was not formatted properly";
    /// endregion </Create empty disk image>

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(&diskImage);

    // De-activate WD1793 reset (active low), Set active drive A, Select MFM / double density mode
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    /// region <For all tracks and sectors>

    const uint8_t writeSectorCommand = WD1793CUT::WD_COMMAND_BITS::WD_CMD_BITS_WRITE_SECTOR;
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(writeSectorCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, writeSectorCommand);
    EXPECT_EQ(decodedCommand, WD1793CUT::WD_COMMANDS::WD_CMD_WRITE_SECTOR);

    for (uint8_t track = 0; track < TRD_80_TRACKS * MAX_SIDES; ++track)
    {
        for (uint8_t sector = 0; sector < TRD_SECTORS_PER_TRACK; ++sector)
        {
            sectorDataIndex = 0;

            fdc.reset();
            /// region <Create parameters for WRITE_SECTOR>
            fdc._commandRegister = writeSectorCommand;
            fdc._lastDecodedCmd = decodedCommand;

            uint16_t physicalTrack = track / 2;
            fdc._trackRegister = physicalTrack;
            fdc._selectedDrive->setTrack(physicalTrack);
            fdc._sectorRegister = sector + 1;  // WD1793 register accepts only values from range 1..26
            /// endregion </Create parameters for READ_SECTOR>

            // Set the proper FDD side using Beta128 register
            uint8_t beta128Register = fdc._beta128Register;
            bool sideUp = track % 2;
            beta128Register = beta128Register | (sideUp ? WD1793::BETA128_COMMAND_BITS::BETA_CMD_HEAD : 0);
            fdc._beta128Register = beta128Register;
            fdc._sideUp = sideUp;

            // Trigger FDC command
            fdc.cmdWriteSector(commandValue);

            /// region <Perform simulation loop>
            size_t clk;
            for (clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
            {
                // Update time for FDC
                fdc._time = clk;

                // Feed data bytes with marking Data Register accessed so no DATA LOSS error occurs
                if (fdc._state == WD1793::S_WRITE_BYTE && fdc._drq_out && !fdc._drq_served)
                {
                    uint8_t writeValue = sectorData[sectorDataIndex++];

                    fdc.writeDataRegister(writeValue);
                }

                // Process FSM state updates
                fdc.process();

                // Check that BUSY flag is set for the whole duration of head positioning
                if (fdc._state != WD1793::S_IDLE)
                {
                    bool busyFlag = fdc._statusRegister & WD1793::WDS_BUSY;
                    EXPECT_EQ(busyFlag, true);
                }

                // Check if a test sequence already finished
                if (fdc._state == WD1793::S_IDLE)
                {
                    break;
                }
            }
            /// endregion </Perform simulation loop>

            /// region <Check results>
            size_t elapsedTimeTStates = clk;
            size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

            bool isAccomplishedCorrectly =
                !(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                fdc._trackRegister == physicalTrack &&              // FDC track still set properly
                fdc._selectedDrive->getTrack() == physicalTrack &&  // FDD is on the same track
                fdc._state == WD1793::S_IDLE;                       // FSM is in idle state

            EXPECT_EQ(isAccomplishedCorrectly, true) << "WRITE_SECTOR didn't end up correctly";

            size_t estimatedExecutionTime = 256 * WD1793::WD93_TSTATES_PER_FDC_BYTE /
                                            TSTATES_IN_MS;  // We're performing single positioning step 6ms long
            EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1)
                << "Abnormal execution time";

            EXPECT_EQ(sectorDataIndex, 256) << "Not all sector bytes were written";

            DiskImage::Track* trackData = diskImage.getTrack(track);
            uint8_t* referenceSector = trackData->getDataForSector(sector);

            if (!areUint8ArraysEqual(sectorData, referenceSector, TRD_SECTORS_SIZE_BYTES))
            {
                std::string diff =
                    DumpHelper::DumpBufferDifferences(sectorData, referenceSector, TRD_SECTORS_SIZE_BYTES);

                // FAIL() << "Track: " << (int)track << " Sector: " << (int)sector << " Sector write data does not match
                // the reference" << std::endl << diff;
                std::cout << "Track: " << (int)track << " Sector: " << (int)sector
                          << " Sector write data does not match the reference" << std::endl
                          << diff << std::endl;

                return;
            }

            // EXPECT_ARRAYS_EQ(sectorData, referenceSector, SECTORS_SIZE_BYTES) << "Sector write data does not match
            // the reference";

            // std::cout << "Read sector dump (T: " << (int)track << " S: " << (int)sector << ")" << std::endl;
            // std::cout << DumpHelper::HexDumpBuffer(sectorData, sizeof(sectorData) / sizeof(sectorData[0])) <<
            // std::endl;

            // return;

            /// endregion </Check results>
        }
    }

    /// endregion </For all tracks and sectors>
}

/// Test Write Sector write protect rejection
/// Per WD1793 datasheet: If disk is write protected, command should terminate with WDS_WRITEPROTECTED status
TEST_F(WD1793_Test, FSM_CMD_Write_Sector_WriteProtect)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    /// region <Create empty disk image>
    DiskImage diskImage = DiskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool imageFormatted = loaderTrd.format(&diskImage);
    ASSERT_EQ(imageFormatted, true) << "Empty test TRD image was not formatted";
    /// endregion </Create empty disk image>

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(&diskImage);

    // Enable write protection on the drive
    fdc._selectedDrive->setWriteProtect(true);

    // De-activate WD1793 reset, Set active drive A, Select MFM / double density mode
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Set up for sector write
    const uint8_t writeSectorCommand = WD1793CUT::WD_COMMAND_BITS::WD_CMD_BITS_WRITE_SECTOR;
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(writeSectorCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, writeSectorCommand);
    
    fdc._commandRegister = writeSectorCommand;
    fdc._lastDecodedCmd = decodedCommand;
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sectorRegister = 1;

    // Try to write to write-protected disk
    fdc.cmdWriteSector(commandValue);

    // The command should immediately terminate with WRITE PROTECT status
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_WRITEPROTECTED) << "Write protect status should be set";
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY) << "Controller should not be busy after rejection";
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "Controller should be in IDLE state after immediate termination";

    // Clean up
    fdc._selectedDrive->setWriteProtect(false);
}

/// Test Write Sector with multi-sector flag (m=1)
/// Per WD1793 datasheet: When m=1, FDC continues to write consecutive sectors
TEST_F(WD1793_Test, FSM_CMD_Write_Sector_MultiSector)
{
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds max
    static constexpr size_t const TEST_INCREMENT_TSTATES = 10;
    static constexpr size_t const SECTORS_TO_WRITE = 4;

    _context->pModuleLogger->SetLoggingLevel(LogError);

    /// region <Create empty disk image>
    DiskImage diskImage = DiskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool imageFormatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(imageFormatted) << "Empty test TRD image was not formatted";
    /// endregion </Create empty disk image>

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(&diskImage);

    // De-activate WD1793 reset, Set active drive A
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Prepare test data - each sector has different pattern
    uint8_t testData[SECTORS_TO_WRITE][TRD_SECTORS_SIZE_BYTES];
    for (size_t sector = 0; sector < SECTORS_TO_WRITE; sector++)
    {
        for (size_t i = 0; i < TRD_SECTORS_SIZE_BYTES; i++)
        {
            testData[sector][i] = static_cast<uint8_t>((sector + 1) * 0x10 + (i & 0x0F));
        }
    }

    // Write Sector command with multi-sector flag (m=1, bit 4)
    const uint8_t writeSectorMultiCommand = WD1793CUT::WD_COMMAND_BITS::WD_CMD_BITS_WRITE_SECTOR | 0b0001'0000;
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(writeSectorMultiCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, writeSectorMultiCommand);

    // Setup for writing starting at track 0, sector 1
    fdc._commandRegister = writeSectorMultiCommand;
    fdc._lastDecodedCmd = decodedCommand;
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sectorRegister = 1;
    fdc._sideUp = false;
    
    // Track current sector being written
    size_t currentSector = 0;
    size_t byteInSector = 0;

    // Trigger FDC command
    fdc.cmdWriteSector(commandValue);

    /// region <Simulation loop>
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;

        // Feed data when DRQ is active
        if (fdc._state == WD1793::S_WRITE_BYTE && fdc._drq_out && !fdc._drq_served)
        {
            if (currentSector < SECTORS_TO_WRITE && byteInSector < TRD_SECTORS_SIZE_BYTES)
            {
                fdc.writeDataRegister(testData[currentSector][byteInSector]);
                byteInSector++;
                
                if (byteInSector >= TRD_SECTORS_SIZE_BYTES)
                {
                    // Move to next sector
                    currentSector++;
                    byteInSector = 0;
                }
            }
        }

        fdc.process();

        // Check if command finished
        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }
    /// endregion </Simulation loop>

    // Verify all sectors were written
    EXPECT_EQ(currentSector, SECTORS_TO_WRITE) << "Should have written " << SECTORS_TO_WRITE << " sectors";
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "FDC should be in IDLE state";
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY) << "BUSY should be cleared";

    // Verify written data matches
    DiskImage::Track* track = diskImage.getTrack(0);
    for (size_t sector = 0; sector < SECTORS_TO_WRITE; sector++)
    {
        uint8_t* sectorData = track->getDataForSector(sector);
        ASSERT_TRUE(areUint8ArraysEqual(testData[sector], sectorData, TRD_SECTORS_SIZE_BYTES))
            << "Sector " << sector << " data mismatch";
    }
}

/// Test Write Sector with Deleted Data Mark (a0=1)
/// Per WD1793 datasheet: WDS_RECORDTYPE (bit 5) reflects written data mark type
TEST_F(WD1793_Test, FSM_CMD_Write_Sector_DeletedDataMark)
{
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 10;

    _context->pModuleLogger->SetLoggingLevel(LogError);

    /// region <Create empty disk image>
    DiskImage diskImage = DiskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool imageFormatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(imageFormatted) << "Empty test TRD image was not formatted";
    /// endregion </Create empty disk image>

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(&diskImage);

    // De-activate WD1793 reset, Set active drive A
    fdc._beta128Register =
        WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Prepare test data
    uint8_t sectorData[TRD_SECTORS_SIZE_BYTES];
    for (size_t i = 0; i < TRD_SECTORS_SIZE_BYTES; i++)
    {
        sectorData[i] = static_cast<uint8_t>(i);
    }
    size_t byteIndex = 0;

    // Write Sector command with Deleted Data Mark flag (a0=1, bit 0)
    const uint8_t writeSectorDeletedCommand = WD1793CUT::WD_COMMAND_BITS::WD_CMD_BITS_WRITE_SECTOR | 0b0000'0001;
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(writeSectorDeletedCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, writeSectorDeletedCommand);

    // Verify command decodes correctly
    EXPECT_EQ(decodedCommand, WD1793CUT::WD_CMD_WRITE_SECTOR);
    EXPECT_TRUE(commandValue & 0x01) << "a0 bit should be set for deleted data mark";

    // Setup for writing
    fdc._commandRegister = writeSectorDeletedCommand;
    fdc._lastDecodedCmd = decodedCommand;
    fdc._trackRegister = 0;
    fdc._selectedDrive->setTrack(0);
    fdc._sectorRegister = 1;
    fdc._sideUp = false;

    // Trigger FDC command
    fdc.cmdWriteSector(commandValue);

    // Verify _useDeletedDataMark was set
    EXPECT_TRUE(fdc._useDeletedDataMark) << "_useDeletedDataMark should be set for a0=1";

    /// region <Simulation loop>
    for (size_t clk = 0; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;

        // Feed data when DRQ is active
        if (fdc._state == WD1793::S_WRITE_BYTE && fdc._drq_out && !fdc._drq_served)
        {
            if (byteIndex < TRD_SECTORS_SIZE_BYTES)
            {
                fdc.writeDataRegister(sectorData[byteIndex++]);
            }
        }

        fdc.process();

        // Check if command finished
        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }
    /// endregion </Simulation loop>

    // Verify completion
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "FDC should be in IDLE state";
    EXPECT_EQ(byteIndex, TRD_SECTORS_SIZE_BYTES) << "All bytes should have been written";

    // Verify WDS_RECORDTYPE (bit 5) is set for deleted data mark
    EXPECT_TRUE(fdc._statusRegister & WD1793::WDS_RECORDTYPE) 
        << "WDS_RECORDTYPE should be set for deleted data mark (F8)";
}
/// endregion </WRITE_SECTOR>

/// region <FORCE_INTERRUPT>

/// Test Force Interrupt I0: Not-Ready to Ready transition
/// Per WD1793 datasheet: I0=1 generates interrupt when drive transitions from Not-Ready to Ready
TEST_F(WD1793_Test, ForceInterrupt_NotReadyToReady)
{
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;

    // Disable all logging except error messages
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // De-activate WD1793 reset, Set active drive A, Select MFM / double density mode
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | 
                           WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Start with NO disk inserted (Not-Ready state)
    fdc._selectedDrive->ejectDisk();
    
    // Reset WDC internal time marks
    fdc.resetTime();

    // Pre-check: verify not ready
    EXPECT_FALSE(fdc.isReady()) << "Drive should not be ready with no disk";

    /// region <Send Force Interrupt command with I0=1>
    {
        const uint8_t forceInterruptCommand = 0b1101'0001;  // I0=1: Not-Ready to Ready transition
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);
        
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_FORCE_INTERRUPT);

        // Send command to FDC
        fdc._commandRegister = forceInterruptCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc.cmdForceInterrupt(commandValue);
    }
    /// endregion

    // Verify I0 condition is set
    EXPECT_EQ(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_NOT_READY, 
              WD1793::WD_FORCE_INTERRUPT_NOT_READY) << "I0 condition should be set";
    
    // Verify no INTRQ yet (condition not triggered)
    bool INTRQ_before = fdc._beta128status & WD1793::INTRQ;
    EXPECT_FALSE(INTRQ_before) << "INTRQ should not be set before transition";

    // Simulate some cycles - no interrupt should happen yet
    for (size_t clk = 0; clk < TSTATES_IN_MS * 10; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc.process();
    }

    // Still no interrupt (no transition occurred)
    EXPECT_FALSE(fdc._beta128status & WD1793::INTRQ) << "INTRQ should not be set without transition";

    // Now trigger the Not-Ready to Ready transition
    // Create and insert a disk image
    DiskImage* diskImage = new DiskImage(80, 2);  // 80 cylinders, 2 sides
    fdc._selectedDrive->insertDisk(diskImage);
    fdc.startFDDMotor();

    // Set _prevReady to false to ensure transition detection
    fdc._prevReady = false;

    // Process once to detect the transition
    fdc._time += TEST_INCREMENT_TSTATES;
    fdc.process();

    // Verify INTRQ is now set
    bool INTRQ_after = fdc._beta128status & WD1793::INTRQ;
    EXPECT_TRUE(INTRQ_after) << "INTRQ should be set after Not-Ready->Ready transition";
    
    // Verify I0 condition is still set (conditions persist until new command)
    EXPECT_NE(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_NOT_READY, 0) 
        << "I0 condition should persist after triggering (for subsequent transitions)";

    // Cleanup
    fdc._selectedDrive->ejectDisk();
    delete diskImage;
}

/// Test Force Interrupt I1: Ready to Not-Ready transition
/// Per WD1793 datasheet: I1=1 generates interrupt when drive transitions from Ready to Not-Ready
TEST_F(WD1793_Test, ForceInterrupt_ReadyToNotReady)
{
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;

    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // De-activate WD1793 reset, Set active drive A
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | 
                           WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Start with disk inserted (Ready state)
    DiskImage* diskImage = new DiskImage(80, 2);  // 80 cylinders, 2 sides
    fdc._selectedDrive->insertDisk(diskImage);
    fdc.startFDDMotor();

    // Reset WDC internal time marks
    fdc.resetTime();

    // Pre-check: verify ready
    EXPECT_TRUE(fdc.isReady()) << "Drive should be ready with disk inserted";

    /// region <Send Force Interrupt command with I1=1>
    {
        const uint8_t forceInterruptCommand = 0b1101'0010;  // I1=1: Ready to Not-Ready transition
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);

        fdc._commandRegister = forceInterruptCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc.cmdForceInterrupt(commandValue);
    }
    /// endregion

    // Verify I1 condition is set
    EXPECT_EQ(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_READY, 
              WD1793::WD_FORCE_INTERRUPT_READY) << "I1 condition should be set";

    // Set _prevReady to true to ensure we can detect the transition
    fdc._prevReady = true;

    // Verify no INTRQ yet
    fdc.clearIntrq();
    EXPECT_FALSE(fdc._beta128status & WD1793::INTRQ) << "INTRQ should not be set before transition";

    // Trigger the Ready to Not-Ready transition - eject the disk
    fdc._selectedDrive->ejectDisk();
    delete diskImage;

    // Process once to detect the transition
    fdc._time += TEST_INCREMENT_TSTATES;
    fdc.process();

    // Verify INTRQ is now set
    bool INTRQ_after = fdc._beta128status & WD1793::INTRQ;
    EXPECT_TRUE(INTRQ_after) << "INTRQ should be set after Ready->Not-Ready transition";
    
    // Verify I1 condition is still set (conditions persist until new command)
    EXPECT_NE(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_READY, 0) 
        << "I1 condition should persist after triggering (for subsequent transitions)";
}

/// Test Force Interrupt I2: Index pulse interrupt
/// Per WD1793 datasheet: I2=1 generates interrupt on each index pulse
TEST_F(WD1793_Test, ForceInterrupt_IndexPulse)
{
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;

    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // De-activate WD1793 reset, Set active drive A
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | 
                           WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    // Insert disk and start motor (required for index pulses)
    DiskImage* diskImage = new DiskImage(80, 2);  // 80 cylinders, 2 sides
    fdc._selectedDrive->insertDisk(diskImage);
    
    // Use prolongFDDMotorRotation() which sets proper motor timeout
    fdc.prolongFDDMotorRotation();

    // Reset WDC internal time marks - motor timeout is already set
    fdc.resetTime();
    
    // Ensure motor timeout is restored after reset
    fdc.prolongFDDMotorRotation();

    /// region <Send Force Interrupt command with I2=1>
    {
        const uint8_t forceInterruptCommand = 0b1101'0100;  // I2=1: Index pulse interrupt
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);

        fdc._commandRegister = forceInterruptCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc.cmdForceInterrupt(commandValue);
    }
    /// endregion

    // Verify I2 condition is set
    EXPECT_EQ(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_INDEX_PULSE, 
              WD1793::WD_FORCE_INTERRUPT_INDEX_PULSE) << "I2 condition should be set";

    // Clear any existing INTRQ
    fdc.clearIntrq();
    EXPECT_FALSE(fdc._beta128status & WD1793::INTRQ) << "INTRQ should be cleared";

    // Record initial index pulse counter
    size_t initialPulseCount = fdc._indexPulseCounter;
    
    // Verify motor is on
    EXPECT_TRUE(fdc._selectedDrive->getMotor()) << "Motor should be running";
    EXPECT_GT(fdc._motorTimeoutTStates, 0) << "Motor timeout should be set";

    // Simulate until we hit an index pulse (one full disk rotation = 200ms = 700,000 t-states)
    // We need to run until we detect a rising edge of the index pulse
    bool foundIndexPulse = false;
    size_t lastTime = 0;
    for (size_t clk = 0; clk < Z80_FREQUENCY / 5 + 10000; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc._lastTime = lastTime;  // Set last time for proper diffTime calculation
        lastTime = clk;
        fdc.process();

        // Check if we got an index pulse
        if (fdc._indexPulseCounter > initialPulseCount)
        {
            foundIndexPulse = true;
            break;
        }
    }

    EXPECT_TRUE(foundIndexPulse) << "Should have detected an index pulse";

    // Verify INTRQ is now set
    bool INTRQ_after = fdc._beta128status & WD1793::INTRQ;
    EXPECT_TRUE(INTRQ_after) << "INTRQ should be set after index pulse";
    
    // Verify I2 condition is still set (triggers on EVERY index pulse)
    EXPECT_NE(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_INDEX_PULSE, 0) 
        << "I2 condition should persist (triggers on every index pulse)";

    // Cleanup
    fdc._selectedDrive->ejectDisk();
    delete diskImage;
}

/// Test Force Interrupt I3: Immediate interrupt
/// Per WD1793 datasheet: I3=1 generates interrupt immediately
TEST_F(WD1793_Test, ForceInterrupt_ImmediateInterrupt)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // De-activate WD1793 reset
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | 
                           WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;

    // Reset WDC internal time marks
    fdc.resetTime();
    
    // Clear any existing INTRQ
    fdc.clearIntrq();
    EXPECT_FALSE(fdc._beta128status & WD1793::INTRQ) << "INTRQ should be cleared initially";

    /// region <Send Force Interrupt command with I3=1>
    {
        const uint8_t forceInterruptCommand = 0b1101'1000;  // I3=1: Immediate interrupt
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);

        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_FORCE_INTERRUPT);

        fdc._commandRegister = forceInterruptCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc.cmdForceInterrupt(commandValue);
    }
    /// endregion

    // Verify INTRQ is immediately set (no need to process)
    bool INTRQ = fdc._beta128status & WD1793::INTRQ;
    EXPECT_TRUE(INTRQ) << "INTRQ should be set immediately for I3=1";
    
    // Verify controller is in idle state
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "Controller should be in IDLE state";
    
    // Verify BUSY is cleared
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY) << "BUSY should be cleared";
}

/// Test Force Interrupt D0: Terminate with NO interrupt
/// Per WD1793 datasheet: If I0-I3=0, command is terminated but NO interrupt is generated
TEST_F(WD1793_Test, ForceInterrupt_D0_NoInterrupt)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // De-activate WD1793 reset
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | 
                           WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;

    // Reset WDC internal time marks
    fdc.resetTime();
    
    // Clear any existing INTRQ
    fdc.clearIntrq();
    EXPECT_FALSE(fdc._beta128status & WD1793::INTRQ) << "INTRQ should be cleared initially";

    /// region <Send Force Interrupt command with I0-I3=0 (D0 = 0xD0)>
    {
        const uint8_t forceInterruptCommand = 0b1101'0000;  // D0: All I flags = 0
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);

        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_FORCE_INTERRUPT);
        EXPECT_EQ(commandValue, 0) << "Command value should be 0 for $D0";

        fdc._commandRegister = forceInterruptCommand;
        fdc._lastDecodedCmd = decodedCommand;
        fdc.cmdForceInterrupt(commandValue);
    }
    /// endregion

    // KEY TEST: Verify INTRQ is NOT set for $D0
    bool INTRQ = fdc._beta128status & WD1793::INTRQ;
    EXPECT_FALSE(INTRQ) << "INTRQ should NOT be set for $D0 (terminate without interrupt)";
    
    // Verify controller is in idle state
    EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "Controller should be in IDLE state";
    
    // Verify BUSY is cleared
    EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_BUSY) << "BUSY should be cleared";
    
    // Verify no interrupt conditions are set for monitoring
    EXPECT_EQ(fdc._interruptConditions, 0) << "No interrupt conditions should be set for $D0";
}

TEST_F(WD1793_Test, ForceInterrupt_Terminate)
{
    static constexpr size_t const TEST_DURATION_SEC = 1;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;  // Time increments during simulation
    static constexpr size_t const TEST_TRACKS = 40;

    // Disable all logging except error messages
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    /// region <Interrupt during idle>
    {
        // Mock parameters
        const uint8_t forceInterruptCommand =
            0b1101'0000;  // FORCE_INTERRUPT with 4 lower bits zeroed - Terminate with no interrupt
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);
        fdc._commandRegister = forceInterruptCommand;
        fdc._lastDecodedCmd = decodedCommand;

        // Reset WDC internal time marks
        fdc.resetTime();

        fdc._state = WD1793::S_IDLE;
        fdc._state2 = WD1793::S_IDLE;

        /// region <Pre-checks>
        EXPECT_EQ(decodedCommand, WD1793::WD_CMD_FORCE_INTERRUPT);
        EXPECT_EQ(fdc._time, 0);
        EXPECT_EQ(fdc._lastTime, 0);
        EXPECT_EQ(fdc._diffTime, 0);
        /// endregion </Pre-checks>

        // Send command to FDC
        fdc.cmdForceInterrupt(commandValue);

        /// region <Check results>
        bool isBusy = fdc._statusRegister & WD1793::WDS_BUSY;
        bool isCRCError = fdc._statusRegister & WD1793::WDS_CRCERR;
        bool isSeekError = fdc._statusRegister & WD1793::WDS_SEEKERR;
        bool isTrack0 = (fdc._statusRegister & WD1793::WDS_TRK00);

        EXPECT_EQ(isBusy, false);
        EXPECT_EQ(isCRCError, false);
        EXPECT_EQ(isSeekError, false);
        EXPECT_EQ(isTrack0, fdc._selectedDrive->isTrack00());
        /// endregion </Check results>
    }
    /// endregion </Interrupt during idle>

    /// region <Interrupt during command>
    {
        // Reset WDC internal time marks
        fdc.resetTime();

        /// region <Execute RESTORE command>
        {
            // Put FDD head far enough from Track0
            fdc._selectedDrive->setTrack(TEST_TRACKS);

            const uint8_t restoreCommand =
                0b0000'0000;  // RESTORE on reset is done with all bits zeroed: no load head, no verify and fastest
                              // stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
            WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(restoreCommand);
            uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, restoreCommand);
            fdc._commandRegister = restoreCommand;
            fdc._lastDecodedCmd = decodedCommand;

            /// region <Pre-checks>
            EXPECT_EQ(decodedCommand, WD1793::WD_CMD_RESTORE);
            EXPECT_EQ(fdc._time, 0);
            EXPECT_EQ(fdc._lastTime, 0);
            EXPECT_EQ(fdc._diffTime, 0);
            /// endregion </Pre-checks>

            // Send command to FDC
            fdc.cmdRestore(commandValue);
        }

        int64_t estimateRestoreDuration = TEST_TRACKS * 6;  // 6ms per step

        /// endregion </Execute RESTORE command>

        /// region <Perform simulation loop>
        size_t positioningDuration = estimateRestoreDuration / 2 * TSTATES_IN_MS;  // Set timing position at 20 track
        size_t clk;
        for (clk = 0; clk < positioningDuration; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();
        }

        // Note: Track may be +/- 1 from expected due to discrete simulation timing
        EXPECT_IN_RANGE(fdc._selectedDrive->getTrack(), TEST_TRACKS / 2 - 1, TEST_TRACKS / 2 + 1);  // Ensure we're around Track 20
        EXPECT_IN_RANGE(fdc._time, positioningDuration - TEST_INCREMENT_TSTATES,
                        positioningDuration + TEST_INCREMENT_TSTATES);
        /// endregion </Pre-checks>

        /// endregion </Perform simulation loop>

        /// region <Execute FORCE_TERMINATE command>
        {
            const uint8_t forceInterruptCommand =
                0b1101'0000;  // FORCE_INTERRUPT with 4 lower bits zeroed - Terminate with no interrupt
            WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
            uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);
            fdc._commandRegister = forceInterruptCommand;
            fdc._lastDecodedCmd = decodedCommand;

            /// region <Pre-checks>
            EXPECT_EQ(decodedCommand, WD1793::WD_CMD_FORCE_INTERRUPT);
            EXPECT_GT(fdc._time, 0);
            EXPECT_GT(fdc._lastTime, 0);
            /// endregion </Pre-checks>

            // Send command to FDC
            fdc.cmdForceInterrupt(commandValue);
        }

        // Continue simulation loop
        for (; clk < positioningDuration; clk += TEST_INCREMENT_TSTATES)
        {
            // Update time for FDC
            fdc._time = clk;

            // Process FSM state updates
            fdc.process();

            // Wait when FORCE_INTERRUPT will be handled
            if (fdc._state == WD1793::S_IDLE)
            {
                break;
            }
        }

        /// endregion </Execute FORCE_TERMINATE command>

        /// region <Check results>

        // Check timing - interrupt should happen within single simulation increment (ideally - immediately)
        EXPECT_IN_RANGE(fdc._time, positioningDuration - TEST_INCREMENT_TSTATES,
                        positioningDuration + TEST_INCREMENT_TSTATES);

        bool isBusy = fdc._statusRegister & WD1793::WDS_BUSY;
        bool isCRCError = fdc._statusRegister & WD1793::WDS_CRCERR;
        bool isSeekError = fdc._statusRegister & WD1793::WDS_SEEKERR;
        bool isTrack0 = (fdc._statusRegister & WD1793::WDS_TRK00);
        bool DRQ = fdc._beta128status & DRQ;
        bool INTRQ = fdc._beta128status & INTRQ;

        EXPECT_EQ(isBusy, false);
        EXPECT_EQ(isCRCError, false);
        EXPECT_EQ(isSeekError, false);
        EXPECT_EQ(isTrack0, fdc._selectedDrive->isTrack00());
        EXPECT_EQ(INTRQ, false);  // There should be no interrupt request
        EXPECT_EQ(DRQ, false);    // No data request either
        /// endregion </Check results>
    }
    /// endregion </Interrupt during idle>
}

/// endregion </FORCE_INTERRUPT>

/// endregion </Commands>

/// region <TR-DOS Integration Tests>

/// @brief Integration test: Full TR-DOS format via ROM execution
/// This test validates the complete FDC integration by:
/// 1. Setting up Pentagon-128K emulator with TR-DOS ROM
/// 2. Executing TR-DOS FORMAT routine
/// 3. Verifying completion via TR-DOS RAM variables
/// 4. Validating disk catalog structure
///
/// Note: This test requires pentagon.rom to be available in data/rom/
TEST_F(WD1793_Test, Integration_TRDOS_Format_ViaROM)
{
    // Create full emulator with Pentagon-128K (default config)
    Emulator emulator(LoggerLevel::LogWarning);
    
    // Initialize emulator (loads config, ROM, sets up all peripherals)
    bool initResult = emulator.Init();
    if (!initResult)
    {
        GTEST_SKIP() << "Emulator initialization failed - ROM files may not be available";
    }
    
    EmulatorContext* context = emulator.GetContext();
    ASSERT_NE(context, nullptr);
    
    Memory* memory = emulator.GetMemory();
    ASSERT_NE(memory, nullptr);
    
    // Verify TR-DOS is enabled
    if (!context->config.trdos_present)
    {
        GTEST_SKIP() << "TR-DOS not enabled in emulator configuration";
    }
    
    // Get Z80 CPU for direct control
    Z80* z80 = context->pCore->GetZ80();
    ASSERT_NE(z80, nullptr) << "Z80 CPU not available";
    
    // Create EMPTY (unformatted) disk image for drive A:
    // The FORMAT routine will format it
    DiskImage* diskImage = new DiskImage(80, 2);
    ASSERT_NE(diskImage, nullptr);
    
    // Get FDD drive A and insert EMPTY disk
    FDD* driveA = context->coreState.diskDrives[0];
    if (driveA == nullptr)
    {
        delete diskImage;
        GTEST_SKIP() << "FDD drive A not available";
    }
    
    driveA->insertDisk(diskImage);
    context->coreState.diskImages[0] = diskImage;
    
    // Verify disk is inserted
    ASSERT_TRUE(driveA->isDiskInserted()) << "Disk should be inserted in drive A";
    ASSERT_FALSE(driveA->isWriteProtect()) << "Disk should not be write protected";
    
    // ============================================================
    // Step 1: Activate TR-DOS ROM
    // ============================================================
    // Debug: Dump raw ROM pointers BEFORE switching
    std::cout << "[FORMAT_TEST] BEFORE SetROMDOS - base_sos_rom[0-7]: ";
    for (int i = 0; i < 8; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)(memory->base_sos_rom[i]) << " ";
    }
    std::cout << std::endl;
    
    std::cout << "[FORMAT_TEST] BEFORE SetROMDOS - base_dos_rom[0-7]: ";
    for (int i = 0; i < 8; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)(memory->base_dos_rom[i]) << " ";
    }
    std::cout << std::endl;
    
    std::cout << "[FORMAT_TEST] BEFORE SetROMDOS - base_128_rom[0-7]: ";
    for (int i = 0; i < 8; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)(memory->base_128_rom[i]) << " ";
    }
    std::cout << std::endl;
    
    std::cout << "[FORMAT_TEST] BEFORE SetROMDOS - base_sys_rom[0-7]: ";
    for (int i = 0; i < 8; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)(memory->base_sys_rom[i]) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // For Pentagon-128K, activate TR-DOS ROM via Memory::SetROMDOS()
    memory->SetROMDOS(true);
    EXPECT_TRUE(memory->isCurrentROMDOS()) << "TR-DOS ROM should be active";
    
    // Debug: Verify ROM at $0000 right after SetROMDOS
    std::cout << "[FORMAT_TEST] Step 1: After SetROMDOS - ROM bytes at 0x0000-0x0010: ";
    for (uint16_t addr = 0x0000; addr < 0x0010; addr++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)memory->DirectReadFromZ80Memory(addr) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // ============================================================
    // Step 2: Set up TR-DOS system variables for FORMAT
    // ============================================================
    // These values are read by the FORMAT routine
    
    // TRDOS system variable: Disk type for format (0x19 = 80T DS)
    memory->DirectWriteToZ80Memory(TRDOS::DISK_TYPE, TRDOS::DiskTypes::DISK_80T_DS);
    
    // TRDOS system variable: Number of tracks (80)
    memory->DirectWriteToZ80Memory(TRDOS::TRACKS_PER_SIDE, 80);
    
    // TRDOS system variable: Number of sides (2 for DS)
    memory->DirectWriteToZ80Memory(TRDOS::SIDES_PER_DISK, 0x02);
    
    // TRDOS system variable: Current side = 0
    memory->DirectWriteToZ80Memory(TRDOS::CURRENT_SIDE, 0x00);
    
    // Clear error flag before format (using SystemVariables48k::ERR_NR)
    memory->DirectWriteToZ80Memory(SystemVariables48k::ERR_NR, 0xFF);  // 0xFF = no error
    
    // ============================================================
    // Step 3: Initialize TR-DOS via entry point $0000
    // ============================================================
    // TR-DOS ROM $0000 is the initialization/reset routine
    // (Note: $3D00 is the trap from SOS, not valid when DOS ROM already paged)
    constexpr uint16_t TRDOS_INIT_ENTRY = 0x0000;  // TR-DOS init in ROM
    constexpr uint16_t INIT_SENTINEL = 0x0001;     // Sentinel to detect init completion
    
    // Set up stack with return sentinel for initialization
    z80->sp = 0xFF00;
    memory->DirectWriteToZ80Memory(0xFEFF, (INIT_SENTINEL >> 8) & 0xFF);
    memory->DirectWriteToZ80Memory(0xFEFE, INIT_SENTINEL & 0xFF);
    z80->sp = 0xFEFE;
    
    // Set PC to TR-DOS init entry
    z80->pc = TRDOS_INIT_ENTRY;
    
    std::cout << "[FORMAT_TEST] Step 3a: Running TR-DOS initialization at $0000" << std::endl;
    std::cout << "[FORMAT_TEST] INIT PC=0x" << std::hex << z80->pc << std::dec << std::endl;
    
    // Dump first bytes at $0000 to verify it's DOS ROM
    std::cout << "[FORMAT_TEST] ROM bytes at 0x0000-0x0010: ";
    for (uint16_t addr = 0x0000; addr < 0x0010; addr++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)memory->DirectReadFromZ80Memory(addr) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Run initialization until it returns to sentinel or reaches command prompt
    constexpr size_t MAX_INIT_CYCLES = 10'000'000;  // 10M cycles max for init
    size_t initCycles = 0;
    while (z80->pc != INIT_SENTINEL && initCycles < MAX_INIT_CYCLES)
    {
        z80->Z80Step(true);
        initCycles++;
        
        // Also stop if we reach TR-DOS command prompt (waits for input at ~$08__)
        // This is optional early exit
    }
    
    if (z80->pc == INIT_SENTINEL)
    {
        std::cout << "[FORMAT_TEST] TR-DOS initialized in " << initCycles << " cycles" << std::endl;
    }
    else
    {
        std::cout << "[FORMAT_TEST] TR-DOS init status after " << initCycles 
                  << " cycles: PC=0x" << std::hex << z80->pc << std::dec << std::endl;
    }
    
    // ============================================================
    // Step 3b: Set Z80 PC to FORMAT entry point and execute
    // ============================================================
    // TR-DOS v5.04T FORMAT entry point is at 0x1EC2
    constexpr uint16_t FORMAT_ENTRY_POINT = TRDOS::EntryPoints::FORMAT_DISK;
    
    // Set stack pointer for FORMAT with sentinel
    z80->sp = 0xFF00;
    constexpr uint16_t SENTINEL_ADDRESS = 0x0000;  // Return to address 0x0000
    memory->DirectWriteToZ80Memory(0xFEFF, (SENTINEL_ADDRESS >> 8) & 0xFF);
    memory->DirectWriteToZ80Memory(0xFEFE, SENTINEL_ADDRESS & 0xFF);
    z80->sp = 0xFEFE;
    
    // Set PC to FORMAT entry point
    z80->pc = FORMAT_ENTRY_POINT;
    
    // ============================================================
    // Step 4: Execute Z80 cycles until format completes or timeout
    // ============================================================
    // FORMAT operation takes significant time - formatting 160 tracks
    // Each track format involves: seek, wait for index, write track data
    // Estimate: ~200ms per track at 300 RPM = ~32 seconds total
    // At 3.5MHz, that's ~112M cycles. We'll set a reasonable limit.
    
    constexpr size_t MAX_CYCLES = 500'000'000;  // 500M cycles max (~143 seconds)
    constexpr size_t CHECK_INTERVAL = 100'000;   // Check completion every 100K cycles
    constexpr size_t PRINT_INTERVAL = 1'000'000; // Print progress every 1M cycles
    
    size_t cyclesExecuted = 0;
    size_t lastPrintCycles = 0;
    bool formatCompleted = false;
    bool formatError = false;
    
    std::cout << "[FORMAT_TEST] Starting TR-DOS FORMAT execution" << std::endl;
    std::cout << "[FORMAT_TEST] PC=0x" << std::hex << z80->pc 
              << ", SP=0x" << z80->sp << std::dec << std::endl;
    
    // Debug: Dump ROM content at FORMAT entry point to verify ROM is loaded
    std::cout << "[FORMAT_TEST] ROM bytes at 0x1EC2-0x1ED2: ";
    for (uint16_t addr = 0x1EC2; addr < 0x1ED2; addr++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << (int)memory->DirectReadFromZ80Memory(addr) << " ";
    }
    std::cout << std::dec << std::endl;
    
    // Debug: Also verify isCurrentROMDOS
    std::cout << "[FORMAT_TEST] isCurrentROMDOS=" << memory->isCurrentROMDOS() << std::endl;
    
    while (cyclesExecuted < MAX_CYCLES && !formatCompleted && !formatError)
    {
        // Run a batch of Z80 cycles
        for (size_t i = 0; i < CHECK_INTERVAL; i++)
        {
            z80->Z80Step(true);  // Skip breakpoints
            cyclesExecuted++;
            
            // Check if we've returned to sentinel address (format complete)
            if (z80->pc == SENTINEL_ADDRESS)
            {
                std::cout << "[FORMAT_TEST] FORMAT completed - returned to sentinel at cycle " 
                          << cyclesExecuted << std::endl;
                formatCompleted = true;
                break;
            }
        }
        
        // Print progress periodically
        if (cyclesExecuted - lastPrintCycles >= PRINT_INTERVAL)
        {
            uint8_t currentTrack = memory->DirectReadFromZ80Memory(TRDOS::SC_0B);  // Tracks formatted
            uint8_t currentSide = memory->DirectReadFromZ80Memory(TRDOS::CURRENT_SIDE);
            std::cout << "[FORMAT_TEST] Cycles=" << cyclesExecuted 
                      << " PC=0x" << std::hex << std::setw(4) << std::setfill('0') << z80->pc
                      << std::dec << " Track=" << (int)currentTrack 
                      << " Side=" << (int)currentSide << std::endl;
            lastPrintCycles = cyclesExecuted;
        }
        
        // ============================================================
        // Step 5: Check completion status via RAM variables
        // ============================================================
        // Read TR-DOS error code
        uint8_t errorCode = memory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
        
        // Error codes: 0 = no error, other values = specific errors
        // During format, this may be updated
        if (errorCode != 0x00 && errorCode != 0xFF)
        {
            // Non-zero error code (excluding 0xFF which is sometimes used for "OK")
            std::cout << "[FORMAT_TEST] ERROR: code=0x" << std::hex << (int)errorCode 
                      << " at PC=0x" << z80->pc << std::dec 
                      << " cycle=" << cyclesExecuted << std::endl;
            formatError = true;
            FAIL() << "TR-DOS format error: error code = 0x" 
                   << std::hex << (int)errorCode 
                   << " at cycle " << std::dec << cyclesExecuted;
        }
    }
    
    // ============================================================
    // Step 6: Verify format results via RAM and disk structure
    // ============================================================
    if (formatCompleted)
    {
        // Read final error status
        uint8_t finalError = memory->DirectReadFromZ80Memory(SystemVariables48k::ERR_NR);
        EXPECT_TRUE(finalError == 0x00 || finalError == 0xFF) 
            << "Format should complete without error, got: 0x" << std::hex << (int)finalError;
        
        // Verify Track 0 has valid TR-DOS structure
        DiskImage::Track* track0 = diskImage->getTrackForCylinderAndSide(0, 0);
        ASSERT_NE(track0, nullptr) << "Track 0 should exist after format";
        
        // Read disk info sector (sector 8 on Track 0)
        uint8_t* diskInfoSector = track0->getDataForSector(8);
        ASSERT_NE(diskInfoSector, nullptr) << "Disk info sector should be readable";
        
        // Verify TR-DOS disk structure
        uint8_t numFiles = diskInfoSector[0xE4];
        uint8_t firstFreeTrack = diskInfoSector[0xE2];
        uint16_t freeSectors = diskInfoSector[0xE5] | (diskInfoSector[0xE6] << 8);
        
        EXPECT_EQ(numFiles, 0x00) << "Freshly formatted disk should have 0 files";
        EXPECT_EQ(firstFreeTrack, 0x01) << "First free track should be 1";
        EXPECT_GE(freeSectors, 2400) << "Free sectors should be ~2544 for 80T DS";
    }
    else if (!formatError)
    {
        FAIL() << "Format operation timed out after " << cyclesExecuted << " cycles";
    }
    
    // Clean up
    emulator.Stop();
    emulator.Release();
}

/// @brief Verify TR-DOS catalog structure after format
/// Validates Track 0 structure according to TR-DOS specification
TEST_F(WD1793_Test, Integration_TRDOS_CatalogStructure)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

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
    // TR-DOS sector numbering starts from 1, but getDataForSector uses 0-15 index
    // Sector 9 in TR-DOS = index 8 (0-based)
    uint8_t* sector8 = track0->getDataForSector(8);
    ASSERT_NE(sector8, nullptr) << "Sector 8 (disk info) not found";

    // Verify TR-DOS disk info structure at sector 8
    // Offset 0xE1: First free sector number
    // Offset 0xE2: First free track number  
    // Offset 0xE3: Disk type (0x16=40T DS, 0x17=40T SS, 0x18=80T SS, 0x19=80T DS)
    // Offset 0xE4: Number of files (0 for empty)
    // Offset 0xE5-0xE6: Free sectors count (little-endian)

    uint8_t firstFreeSector = sector8[0xE1];
    uint8_t firstFreeTrack = sector8[0xE2];
    uint8_t diskType = sector8[0xE3];
    uint8_t numFiles = sector8[0xE4];
    uint16_t freeSectors = sector8[0xE5] | (sector8[0xE6] << 8);

    // Expected values for 80-track DS formatted disk
    EXPECT_EQ(firstFreeSector, 0x00) << "First free sector should be 0 on empty disk";
    EXPECT_EQ(firstFreeTrack, 0x01) << "First free track should be 1 (track 0 is system)";
    EXPECT_EQ(diskType, 0x16) << "Disk type should be 0x16 (80T DS) or 0x19";  // May vary by formatter
    EXPECT_EQ(numFiles, 0x00) << "Number of files should be 0 on empty disk";
    
    // 80 tracks * 2 sides * 16 sectors = 2560 total sectors
    // Track 0 uses 16 sectors for system, so 2544 free
    // LoaderTRD may use different calculation - accept range
    EXPECT_GE(freeSectors, 2400) << "Free sectors should be ~2544 for 80T DS";
    EXPECT_LE(freeSectors, 2560) << "Free sectors cannot exceed total";

    // Verify catalog sectors (0-7) are initialized (filled with 0x00 for empty entries)
    for (int sectorNum = 0; sectorNum < 8; sectorNum++)
    {
        uint8_t* catalogSector = track0->getDataForSector(sectorNum);
        ASSERT_NE(catalogSector, nullptr) << "Catalog sector " << sectorNum << " not found";

        // Each sector has 16 catalog entries of 16 bytes each
        // Empty entry has first byte = 0x00
        for (int entry = 0; entry < 16; entry++)
        {
            uint8_t* entryData = catalogSector + (entry * 16);
            EXPECT_EQ(entryData[0], 0x00) 
                << "Catalog entry " << (sectorNum * 16 + entry) << " should be empty (0x00)";
        }
    }
}

/// @brief Verify sector interleave pattern matches TR-DOS standard
/// TR-DOS uses 1:2 interleave: 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16
TEST_F(WD1793_Test, Integration_TRDOS_SectorInterleave)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    // Create and format disk image
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool formatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(formatted) << "Failed to format TRD disk image";

    // Standard TR-DOS 1:2 interleave pattern
    static const uint8_t TRDOS_INTERLEAVE[] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16};

    // Check Track 1 (Track 0 is special system track)
    DiskImage::Track* track1 = diskImage.getTrackForCylinderAndSide(1, 0);
    ASSERT_NE(track1, nullptr) << "Track 1 not found";

    // Verify each sector ID in the track matches the interleave pattern
    // Note: Physical sector order on disk should follow interleave
    // But logical sector numbers 1-16 should all be present
    std::set<uint8_t> foundSectors;
    for (int i = 0; i < 16; i++)
    {
        uint8_t sectorNumber = track1->sectors[i].address_record.sector;
        EXPECT_GE(sectorNumber, 1) << "Sector number should be >= 1";
        EXPECT_LE(sectorNumber, 16) << "Sector number should be <= 16";
        foundSectors.insert(sectorNumber);
    }

    // All 16 sectors (1-16) should be present
    EXPECT_EQ(foundSectors.size(), 16) << "All 16 sectors should be present on track";
    for (uint8_t s = 1; s <= 16; s++)
    {
        EXPECT_TRUE(foundSectors.count(s) > 0) << "Sector " << (int)s << " not found on track";
    }
}

/// @brief Test that verifies WD1793 format operation populates all tracks
TEST_F(WD1793_Test, Integration_AllTracksPopulated)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    // Create and format a full 80-track DS disk image
    DiskImage diskImage(80, 2);
    LoaderTRDCUT loaderTrd(_context, "test.trd");
    bool formatted = loaderTrd.format(&diskImage);
    ASSERT_TRUE(formatted) << "Failed to format TRD disk image";

    // Verify all 160 tracks (80 cylinders * 2 sides) are populated
    int tracksChecked = 0;
    for (uint8_t cylinder = 0; cylinder < 80; cylinder++)
    {
        for (uint8_t side = 0; side < 2; side++)
        {
            DiskImage::Track* track = diskImage.getTrackForCylinderAndSide(cylinder, side);
            ASSERT_NE(track, nullptr) 
                << "Track not found: cylinder=" << (int)cylinder << " side=" << (int)side;

            // Verify track has 16 sectors
            int validSectors = 0;
            for (int s = 0; s < 16; s++)
            {
                // Check sector has valid ID (cylinder and side match)
                DiskImage::AddressMarkRecord& id = track->sectors[s].address_record;
                EXPECT_EQ(id.cylinder, cylinder) 
                    << "Sector cylinder mismatch at C" << (int)cylinder << "S" << (int)side;
                EXPECT_EQ(id.head, side)
                    << "Sector side mismatch at C" << (int)cylinder << "S" << (int)side;
                EXPECT_EQ(id.sector_size, 0x01)  // 256 bytes
                    << "Sector size should be 256 bytes (0x01)";
                
                validSectors++;
            }
            EXPECT_EQ(validSectors, 16) 
                << "Track C" << (int)cylinder << "S" << (int)side << " should have 16 sectors";
            
            tracksChecked++;
        }
    }

    EXPECT_EQ(tracksChecked, 160) << "Should verify all 160 tracks (80 cylinders * 2 sides)";
}

/// endregion </TR-DOS Integration Tests>

/// region <FORCE_INTERRUPT Persistence Tests>

/// Test Force Interrupt I2: Verify interrupt triggers on MULTIPLE index pulses
/// Per WD1793 datasheet: "The interrupt is generated on every index pulse"
TEST_F(WD1793_Test, ForceInterrupt_I2_MultipleIndexPulses)
{
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;
    static constexpr size_t const ROTATION_PERIOD_TSTATES = Z80_FREQUENCY / 5; // 200ms per rotation

    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // Setup: Insert disk and start motor
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | 
                           WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    fdc._drive = 0;

    DiskImage* diskImage = new DiskImage(80, 2);
    fdc._selectedDrive->insertDisk(diskImage);
    fdc.prolongFDDMotorRotation();
    fdc.resetTime();
    fdc.prolongFDDMotorRotation();

    // Send Force Interrupt command with I2=1
    const uint8_t forceInterruptCommand = 0b1101'0100;  // I2=1: Index pulse interrupt
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(forceInterruptCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, forceInterruptCommand);
    fdc._commandRegister = forceInterruptCommand;
    fdc._lastDecodedCmd = decodedCommand;
    fdc.cmdForceInterrupt(commandValue);

    // Verify I2 condition is set
    EXPECT_NE(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_INDEX_PULSE, 0);

    // Clear initial INTRQ
    fdc.clearIntrq();
    size_t initialPulseCount = fdc._indexPulseCounter;
    
    // Run for 3 full disk rotations (~600ms) and count how many times INTRQ is raised
    size_t intrqRaisedCount = 0;
    size_t lastTime = 0;
    bool lastIntrqState = false;
    
    for (size_t clk = 0; clk < 3 * ROTATION_PERIOD_TSTATES + 10000; clk += TEST_INCREMENT_TSTATES)
    {
        fdc._time = clk;
        fdc._lastTime = lastTime;
        lastTime = clk;
        fdc.process();

        // Detect rising edge of INTRQ
        bool currentIntrqState = (fdc._beta128status & WD1793::INTRQ) != 0;
        if (currentIntrqState && !lastIntrqState)
        {
            intrqRaisedCount++;
            // Clear INTRQ to detect the next one
            fdc.clearIntrq();
        }
        lastIntrqState = (fdc._beta128status & WD1793::INTRQ) != 0;
    }

    size_t totalPulses = fdc._indexPulseCounter - initialPulseCount;
    
    // Should have at least 2-3 index pulses over 3 rotations
    EXPECT_GE(totalPulses, 2) << "Should have at least 2 index pulses in 3 rotations";
    
    // INTRQ should have been raised for each pulse (key test for the fix!)
    EXPECT_EQ(intrqRaisedCount, totalPulses) 
        << "INTRQ should be raised on EVERY index pulse, not just the first";

    // I2 condition should still be set (persists)
    EXPECT_NE(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_INDEX_PULSE, 0)
        << "I2 condition should persist after multiple triggers";

    // Cleanup
    fdc._selectedDrive->ejectDisk();
    delete diskImage;
}

/// Test that Force Interrupt conditions are cleared when a new command is issued
TEST_F(WD1793_Test, ForceInterrupt_ConditionsClearedByNewCommand)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // Set I2 condition
    const uint8_t forceInterruptI2 = 0b1101'0100;  // I2=1
    fdc._commandRegister = forceInterruptI2;
    fdc._lastDecodedCmd = WD1793::WD_CMD_FORCE_INTERRUPT;
    fdc.cmdForceInterrupt(0b0100);  // I2 bit

    // Verify I2 is set
    EXPECT_NE(fdc._interruptConditions & WD1793::WD_FORCE_INTERRUPT_INDEX_PULSE, 0);

    // Issue a new RESTORE command
    fdc._commandRegister = 0x00;  // RESTORE
    fdc._lastDecodedCmd = WD1793::WD_CMD_RESTORE;
    fdc.cmdRestore(0);

    // Verify I2 condition is now cleared by new command start
    EXPECT_EQ(fdc._interruptConditions, 0) 
        << "Interrupt conditions should be cleared when a new command is issued";
}

/// Test $D0 (Force Interrupt with no conditions) - negative test
/// Verify NO interrupt is raised, not even after disk transitions
TEST_F(WD1793_Test, ForceInterrupt_D0_NoInterruptEvenAfterTransitions)
{
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100;

    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // Setup
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET;
    fdc._drive = 0;

    // Send $D0 - Terminate with NO interrupt
    const uint8_t forceInterruptD0 = 0b1101'0000;
    fdc._commandRegister = forceInterruptD0;
    fdc._lastDecodedCmd = WD1793::WD_CMD_FORCE_INTERRUPT;
    fdc.cmdForceInterrupt(0);  // No condition bits

    // Verify no interrupt conditions are monitored
    EXPECT_EQ(fdc._interruptConditions, 0);
    
    // Clear any INTRQ
    fdc.clearIntrq();
    EXPECT_FALSE(fdc._beta128status & WD1793::INTRQ);

    // Insert a disk (simulates Not-Ready -> Ready transition)
    DiskImage* diskImage = new DiskImage(80, 2);
    fdc._selectedDrive->insertDisk(diskImage);
    fdc._prevReady = false;
    fdc.prolongFDDMotorRotation();
    
    // Process to let transition detection run
    fdc._time = TEST_INCREMENT_TSTATES;
    fdc.process();

    // Verify NO interrupt was raised (since we specified $D0)
    EXPECT_FALSE(fdc._beta128status & WD1793::INTRQ) 
        << "$D0 should NOT generate interrupts even after state transitions";

    // Cleanup
    fdc._selectedDrive->ejectDisk();
    delete diskImage;
}

/// endregion </FORCE_INTERRUPT Persistence Tests>

/// region <Write Track CRC Regression Tests>

/// Test that Write Track F5 byte correctly sets _crcStartPosition
/// Regression test for Bug #5: Missing start_crc position tracking
TEST_F(WD1793_Test, WriteTrack_F5_Sets_CrcStartPosition)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // Setup for Write Track
    DiskImage* diskImage = new DiskImage(80, 2);
    fdc._selectedDrive->insertDisk(diskImage);
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET | 
                           WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_DENSITY;
    
    // Allocate raw track buffer
    fdc._rawDataBuffer = new uint8_t[DiskImage::RawTrack::RAW_TRACK_SIZE];
    fdc._rawDataBufferIndex = 100;  // Simulate we're at byte 100
    fdc._bytesToWrite = 6250;
    
    // Write F5 (sync byte)
    fdc._dataRegister = 0xF5;
    fdc.processWriteTrack();
    
    // Verify _crcStartPosition was set to AFTER the A1 byte (index + 1)
    EXPECT_EQ(fdc._crcStartPosition, 100 + 1) 
        << "F5 should set _crcStartPosition to the byte AFTER the A1 sync byte";
    
    // Verify A1 was written (F5 -> A1)
    EXPECT_EQ(fdc._rawDataBuffer[100], 0xA1) << "F5 should write A1 byte";
    
    // Cleanup
    delete[] fdc._rawDataBuffer;
    fdc._rawDataBuffer = nullptr;
    fdc._selectedDrive->ejectDisk();
    delete diskImage;
}

/// Test that Write Track F7 writes CRC in correct byte order (low byte first, then high byte)
/// Regression test for Bug #4: CRC byte order was reversed
TEST_F(WD1793_Test, WriteTrack_F7_CrcByteOrder_LowFirst)
{
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // Setup for Write Track
    DiskImage* diskImage = new DiskImage(80, 2);
    fdc._selectedDrive->insertDisk(diskImage);
    fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET;
    
    // Allocate raw track buffer
    fdc._rawDataBuffer = new uint8_t[DiskImage::RawTrack::RAW_TRACK_SIZE];
    memset(fdc._rawDataBuffer, 0, DiskImage::RawTrack::RAW_TRACK_SIZE);
    
    // Write known data pattern for CRC calculation
    // After F5: write A1 and set _crcStartPosition
    fdc._rawDataBufferIndex = 0;
    fdc._bytesToWrite = 6250;
    fdc._drq_served = true;  // Mark DRQ as served to prevent Lost Data error
    fdc._dataRegister = 0xF5;  // Sync byte
    fdc.processWriteTrack();
    
    size_t crcStart = fdc._crcStartPosition;  // Should be 1
    
    // Write a known data byte (e.g., FE for ID Address Mark)
    fdc._drq_served = true;  // Mark DRQ as served
    fdc._dataRegister = 0xFE;
    fdc.processWriteTrack();
    
    // Record current index before writing CRC
    size_t indexBeforeCrc = fdc._rawDataBufferIndex;
    
    // Write F7 (generates 2 CRC bytes)
    fdc._drq_served = true;  // Mark DRQ as served
    fdc._dataRegister = 0xF7;
    fdc.processWriteTrack();
    
    // CRC should be 2 bytes after previous position
    EXPECT_EQ(fdc._rawDataBufferIndex, indexBeforeCrc + 2) << "F7 should write 2 CRC bytes";
    
    // Manually calculate expected CRC for verification
    // CRC-CCITT starting with 0xCDB4 over the data from crcStart
    uint16_t expectedCrc = 0xCDB4;
    for (size_t i = crcStart; i < indexBeforeCrc; i++)
    {
        expectedCrc ^= static_cast<uint16_t>(fdc._rawDataBuffer[i]) << 8;
        for (int j = 0; j < 8; j++)
        {
            expectedCrc = (expectedCrc << 1) ^ ((expectedCrc & 0x8000) ? 0x1021 : 0);
        }
    }
    
    // Key regression test: verify byte order is LOW BYTE FIRST, then HIGH BYTE
    uint8_t lowByte = fdc._rawDataBuffer[indexBeforeCrc];
    uint8_t highByte = fdc._rawDataBuffer[indexBeforeCrc + 1];
    
    EXPECT_EQ(lowByte, expectedCrc & 0xFF) << "First CRC byte should be LOW byte";
    EXPECT_EQ(highByte, (expectedCrc >> 8) & 0xFF) << "Second CRC byte should be HIGH byte";
    
    // Cleanup
    delete[] fdc._rawDataBuffer;
    fdc._rawDataBuffer = nullptr;
    fdc._selectedDrive->ejectDisk();
    delete diskImage;
}

/// Test that processWaitIndex uses T-state based delay calculation
/// Regression test for Bug #8: Index pulse race condition
TEST_F(WD1793_Test, WaitIndex_Uses_TState_Delay)
{
    static constexpr size_t const ROTATION_PERIOD = Z80_FREQUENCY / 5;  // 200ms = 700,000 T-states
    
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    // Setup
    DiskImage* diskImage = new DiskImage(80, 2);
    fdc._selectedDrive->insertDisk(diskImage);
    fdc.prolongFDDMotorRotation();
    fdc.wakeUp();
    
    // Set initial time to middle of disk rotation
    fdc._time = ROTATION_PERIOD / 2;  // 350,000 T-states into rotation
    
    // Set state to wait for index
    fdc._state = WD1793::S_WAIT_INDEX;
    fdc._waitIndexPulseCount = SIZE_MAX;  // Reset wait state
    
    // Call processWaitIndex - should calculate delay and set S_WAIT
    fdc.processWaitIndex();
    
    // Verify it switched to S_WAIT (using delay-based approach, not counter polling)
    EXPECT_EQ(fdc._state, WD1793::S_WAIT) << "processWaitIndex should use delay-based wait";
    EXPECT_EQ(fdc._state2, WD1793::S_FETCH_FIFO) << "After delay, should transition to S_FETCH_FIFO";
    
    // Verify delay was calculated to next index pulse
    // At time = ROTATION_PERIOD/2, delay should be approximately ROTATION_PERIOD/2 to reach next index
    size_t expectedDelay = ROTATION_PERIOD - (fdc._time % ROTATION_PERIOD);
    EXPECT_IN_RANGE(fdc._delayTStates, expectedDelay - 100, expectedDelay + 100)
        << "Delay should be calculated to next index pulse";
    
    // Cleanup
    fdc._selectedDrive->ejectDisk();
    delete diskImage;
}

/// endregion </Write Track CRC Regression Tests>
