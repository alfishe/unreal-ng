#include <gtest/gtest.h>

#include "_helpers/testtiminghelper.h"

#include <cmath>
#include <random>
#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/cpu/z80.h"
#include "loaders/disk/loader_trd.h"

/// region <Test types>

static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;
static constexpr size_t const TSTATES_IN_MS = Z80_FREQUENCY / 1000;

class WD1793_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

    TestTimingHelper* _timingHelper = nullptr;

protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);

        // Set-up module logger only for FDC messages
        ModuleLogger& logger = *_context->pModuleLogger;
        logger.TurnOffLoggingForAll();
        logger.TurnOnLoggingForModule(PlatformModulesEnum::MODULE_DISK, PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC);
        // Show more info if needed
        //logger.SetLoggingLevel(LoggerLevel::LogInfo);
        //logger.SetLoggingLevel(LoggerLevel::LogDebug);


        // Mock Core and Z80 to make timings work
        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        // Timing helper
        _timingHelper = new TestTimingHelper(_context);
        _timingHelper->resetClock();    // Reset all t-state counters within system (Z80, emulator state)
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
    static constexpr std::array<RangeCommand, 11> _referenceValues
    {
        RangeCommand { 0x00, 0x0F, WD1793::WD_CMD_RESTORE },
        RangeCommand { 0x10, 0x1F, WD1793::WD_CMD_SEEK },
        RangeCommand { 0x20, 0x3F, WD1793::WD_CMD_STEP },
        RangeCommand { 0x40, 0x5F, WD1793::WD_CMD_STEP_IN },
        RangeCommand { 0x60, 0x7F, WD1793::WD_CMD_STEP_OUT },
        RangeCommand { 0x80, 0x9F, WD1793::WD_CMD_READ_SECTOR },
        RangeCommand { 0xA0, 0xBF, WD1793::WD_CMD_WRITE_SECTOR },
        RangeCommand { 0xC0, 0xDF, WD1793::WD_CMD_READ_ADDRESS },
        RangeCommand { 0xE0, 0xEF, WD1793::WD_CMD_READ_TRACK },
        RangeCommand { 0xF0, 0xFF, WD1793::WD_CMD_WRITE_TRACK },
        RangeCommand { 0xD0, 0xDF, WD1793::WD_CMD_FORCE_INTERRUPT },
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

        //std::string message = StringHelper::Format("0x%02X -> %s", i, VG93CUT::getWD_COMMANDName(result));
        //std::cout << message << std::endl;
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
        int trueCount = (isType1Command ? 1 : 0) + (isType2Command ? 1 : 0) + (isType3Command ? 1 : 0) + (isType4Command ? 1 : 0);

        std::string message = StringHelper::Format("%03d: t1: %d; t2: %d; t3: %d; t4: %d", i, isType1Command, isType2Command, isType3Command, isType4Command);
        EXPECT_EQ(trueCount, 1) << "Only one command type can be active at a time. " << message;

        //std::cout << message << std::endl;
    }
}

/// endregion </WD1793 commands>

/// region <Status bits behavior>

TEST_F(WD1793_Test, Beta128_Status_INTRQ)
{
    FAIL() << "Not Implemented yet";
}

TEST_F(WD1793_Test, Beta128_Status_DRQ)
{
    FAIL() << "Not Implemented yet";
}

/// endregion </Status bits behavior>

/// region <FDD related>

/// Test motor starts and auto-stops after 3 seconds
TEST_F(WD1793_Test, FDD_Motor_StartStop)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 4;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000; // Time increments during simulation

    // Internal logging messages are done on Info level
    _context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    // Reset WDC internal time marks
    fdc.resetTime();

    // Trigger motor start
    fdc.prolongFDDMotorRotation();

    /// region <Perform simulation loop>

    bool motorStarted = false;
    int64_t motorStartTStates = 0;
    int64_t motorStopTStates = 0;

    size_t clk;
    for (clk = 10; clk < TEST_DURATION_TSTATES; clk += TEST_INCREMENT_TSTATES)
    {
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

    size_t estimatedMotorOnTStates = 3 * Z80_FREQUENCY;
    size_t motorWasOnForTSTates = std::abs(motorStopTStates - motorStartTStates);
    EXPECT_IN_RANGE(motorWasOnForTSTates, estimatedMotorOnTStates - TEST_INCREMENT_TSTATES, estimatedMotorOnTStates + TEST_INCREMENT_TSTATES);
    /// endregion </Check results>
}

/// Test if any new operation prolongs timeout
TEST_F(WD1793_Test, FDD_Motor_Prolong)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 10;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000; // Time increments during simulation

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
        if (!prolongActivated &&clk >= 2 * Z80_FREQUENCY)
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
    EXPECT_IN_RANGE(motorWasOnForTSTates, estimatedMotorOnTStates - TEST_INCREMENT_TSTATES, estimatedMotorOnTStates + TEST_INCREMENT_TSTATES);
    /// endregion </Check results>
}

/// Test if index pulses are available during disk rotation
TEST_F(WD1793_Test, FDD_Rotation_Index)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 4;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000; // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

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

        // Start motor after 1 second delay
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
    EXPECT_IN_RANGE(motorWasOnForTSTates, estimatedMotorOnTStates - TEST_INCREMENT_TSTATES, estimatedMotorOnTStates + TEST_INCREMENT_TSTATES);

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
    static constexpr size_t const TEST_INCREMENT_TSTATES = 1000; // Time increments during simulation

    // Internal logging messages are done on Info level
    _context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    // Reset WDC internal time marks
    fdc.resetTime();

    /// region <Perform simulation loop>
    bool motorStarted = false;
    bool motorStopped = false;
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

        // Start motor after 1 second delay
        if (clk > Z80_FREQUENCY && !motorStarted && !motorStopped)  // Block motor re-start by checking motorStopped flag meaning it was done intentionally
        {
            // Trigger motor start
            fdc.prolongFDDMotorRotation();

            motorStartTStates = clk;
            motorStarted = true;
        }

        // Stop motor after 0.5 second after start
        if (!motorStopped && clk >= 1.5 * Z80_FREQUENCY)
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
    size_t estimatedMotorOnTStates = 0.5 * Z80_FREQUENCY;
    size_t motorWasOnForTSTates = std::abs(motorStopTStates - motorStartTStates);
    EXPECT_IN_RANGE(motorWasOnForTSTates, estimatedMotorOnTStates - TEST_INCREMENT_TSTATES, estimatedMotorOnTStates + TEST_INCREMENT_TSTATES);

    size_t estimatedIndexPulses = std::ceil((double)motorWasOnForTSTates * FDD_RPS / (double)Z80_FREQUENCY);
    size_t indexPulses = fdc._indexPulseCounter;
    EXPECT_IN_RANGE(indexPulses, estimatedIndexPulses - 1, estimatedIndexPulses + 1);
    /// endregion </Check results>
}

/// Test index strobe timings and stability
TEST_F(WD1793_Test, FDD_Rotation_Index_Stability)
{
    FAIL() << "Not implemented yet";
}

/// endregion <FDD related>

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
                FAIL() << StringHelper::Format("i: %d; it: %d; %s -> %s; expectedDelay: %d, delayTStates: %d", i, it, srcState.c_str(), dstState.c_str(), expectedDelay, fdc._delayTStates) << std::endl;
            }

            fdc._time += ITERATION_STEP;
            fdc.process();

            EXPECT_EQ(expectedDelay, fdc._delayTStates) << StringHelper::Format("i: %d; it: %d; %s -> %s; expectedDelay: %d, delayTStates: %d", i, it, srcState.c_str(), dstState.c_str(), expectedDelay, fdc._delayTStates) << std::endl;

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
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>

    std::cout << "------------------------------" << std::endl;

    for (size_t i = 0; i < MAX_CYLINDERS; i++)
    {
        fdc._selectedDrive->setTrack(i);

        // Mock parameters
        const uint8_t restoreCommand = 0b0000'0000; // RESTORE on reset is done with all bits zeroed: no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
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

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&    // Controller is not BUSY anymore
                fdc._trackRegister == 0 &&                      // FDC track set to 0
                fdc._selectedDrive->isTrack00() &&              // FDD has the same track 0
                fdc._state == WD1793::S_IDLE)                   // FSM is in idle state
            {
                // RESTORE operation finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&    // Controller is not BUSY anymore
                                       fdc._trackRegister == 0 &&              // FDC track set to 0
                                       fdc._selectedDrive->isTrack00() &&      // FDD has the same track 0
                                       fdc._state == WD1793::S_IDLE &&         // FSM is in idle state
                                       fdc._beta128status & WD1793::INTRQ;     // INTRQ is active

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

        size_t estimatedExecutionTime = i * 6; // Number of positioning steps, 6ms each
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 0.1 * estimatedExecutionTime) << "Abnormal execution time";
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
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    // Remember initial FDD state
    uint8_t initialFDDTrack = fdc._selectedDrive->getTrack();

    // Mock parameters
    const uint8_t restoreCommand = 0b0000'1000; // RESTORE with load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
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

        if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&    // Controller is not BUSY anymore
            fdc._trackRegister == 0 &&              // FDC track set to 0
            fdc._selectedDrive->isTrack00() &&      // FDD has the same track 0
            fdc._state == WD1793::S_IDLE)           // FSM is in idle state
        {
            // RESTORE operation finished
            break;
        }
    }
    /// endregion </Perform simulation loop>

    /// region <Check results>
    bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&    // Controller is not BUSY anymore
                                   fdc._trackRegister == 0 &&              // FDC track set to 0
                                   fdc._selectedDrive->isTrack00() &&      // FDD has the same track 0
                                   fdc._state == WD1793::S_IDLE;           // FSM is in idle state

    EXPECT_EQ(isAccomplishedCorrectly, true) << "RESTORE didn't end up correctly";
    /// endregion </Check results>

    /// region <Get simulation stats>
    size_t elapsedTimeTStates = clk;
    size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

    std::stringstream ss;
    ss << "RESTORE test stats:" << std::endl;
    ss << StringHelper::Format("TStates: %d, time: %d ms", elapsedTimeTStates, elapsedTimeMs) << std::endl;
    ss << StringHelper::Format("From track: %d to track %d", initialFDDTrack, fdc._selectedDrive->getTrack()) << std::endl;

    std::cout << ss.str();
    /// endregion </Get simulation stats>
}

TEST_F(WD1793_Test, FSM_CMD_Restore_Verify)
{
    static constexpr size_t const STEP_DURATION_MS = 6; // HEAD movement duration (from track to track)
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

    // Disable all logging except error messages
    //_context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    /// region <Main test loop>

    std::cout << "------------------------------" << std::endl;

    for (size_t i = 0; i < MAX_CYLINDERS; i++)
    {
        fdc._selectedDrive->setTrack(i);

        // Mock parameters
        const uint8_t restoreCommand = 0b0000'1100; // RESTORE on reset is done with all bits zeroed: no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
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

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&    // Controller is not BUSY anymore
                fdc._trackRegister == 0 &&                      // FDC track set to 0
                fdc._selectedDrive->isTrack00() &&              // FDD has the same track 0
                fdc._state == WD1793::S_IDLE)                   // FSM is in idle state
            {
                // RESTORE operation finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) && // Controller is not BUSY anymore
                                       fdc._trackRegister == 0 &&                   // FDC track set to 0
                                       fdc._selectedDrive->isTrack00() &&           // FDD has the same track 0
                                       fdc._state == WD1793::S_IDLE;                // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "RESTORE didn't end up correctly";

        size_t estimatedExecutionTime = i * STEP_DURATION_MS;       // Number of positioning steps, 6ms each
        estimatedExecutionTime += WD1793CUT::WD93_VERIFY_DELAY_MS;  // Add verification time after the positioning

        size_t timeTolerance = 0.1 * estimatedExecutionTime;
        if (timeTolerance == 0)
            timeTolerance = 3 * STEP_DURATION_MS;
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + timeTolerance) << "Abnormal execution time";
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
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>
    for (int i = 0; i < MAX_CYLINDERS - 1; i++)
    {
        int targetTrack = MAX_CYLINDERS - 1 - i;
        //int targetTrack = 3;

        // Set initial conditions
        fdc._selectedDrive->setTrack(i);
        fdc._trackRegister = i;
        fdc._dataRegister = targetTrack;
        fdc._statusRegister &= ~WD1793::WDS_BUSY;
        fdc._beta128status &= ~(WD1793::INTRQ | WD1793::DRQ);

        // Mock parameters
        const uint8_t stepCommand = 0b0001'0000; // SEEK: no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
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

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&                // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&    // FDD has the same track
                fdc._state == WD1793::S_IDLE)                       // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&             // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack && // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                    // FSM is in idle state
        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK didn't end up correctly";

        bool intrqSet = fdc._beta128status & WD1793::INTRQ;
        EXPECT_EQ(intrqSet, true) << "INTRQ was not set at the end of SEEK";

        size_t estimatedExecutionTime = std::abs(targetTrack - i) * fdc._steppingMotorRate; // We're performing single positioning step 6ms long
        size_t estimationError = std::abs(targetTrack - i) * 0.5; // No more than 0.5ms estimation error per step
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + estimationError) << "Abnormal execution time" << StringHelper::Format(" from trk: %d to trk: %d", i, targetTrack);
        /// endregion </Check results>
    }

    /// endregion </Main test loop>
}

TEST_F(WD1793_Test, FSM_CMD_Seek_All_Rates)
{
    static constexpr double const TEST_DURATION_SEC = 5.0;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    /// region <Main test loop>
    for (uint8_t stepRate = 0; stepRate < 4; stepRate++)
    {
        //for (int i = 0; i < MAX_CYLINDERS - 1; i++)
        for (int i = 0; i < MAX_CYLINDERS; i++)
        {
            int targetTrack = MAX_CYLINDERS - 1 - i;

            // Set initial conditions
            fdc._selectedDrive->setTrack(i);
            fdc._trackRegister = i;
            fdc._dataRegister = targetTrack;

            // Mock parameters
            uint8_t stepCommand = 0b0001'0000;  // SEEK: no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
            stepCommand |= stepRate;            // Apply r1r0 bits for speed rates
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

                if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                    fdc._trackRegister == targetTrack &&                // FDC track set to <next track>
                    fdc._selectedDrive->getTrack() == targetTrack &&    // FDD has the same track
                    fdc._state == WD1793::S_IDLE)                       // FSM is in idle state
                {
                    // STEP_IN command finished
                    break;
                }
            }
            /// endregion </Perform simulation loop>

            /// region <Check results>
            size_t elapsedTimeTStates = clk;
            size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

            bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&      // Controller is not BUSY anymore
                                           fdc._trackRegister == targetTrack &&             // FDC track set to <next track>
                                           fdc._selectedDrive->getTrack() == targetTrack && // FDD has the same track
                                           fdc._state == WD1793::S_IDLE;                    // FSM is in idle state

            EXPECT_EQ(isAccomplishedCorrectly, true)
                    << "SEEK didn't end up correctly"
                    << StringHelper::Format(" stepRate: %d, from trk: %d, to trk: %d", stepRate, i, targetTrack);

            size_t estimatedExecutionTime = std::abs(targetTrack - i) *
                                            fdc._steppingMotorRate; // We're performing single positioning step 6ms long
            size_t estimationError = std::abs(targetTrack - i) * 0.5; // No more than 0.5ms estimation error per step
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
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

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
        fdc._stepDirectionIn = true;

        // Mock parameters
        const uint8_t stepCommand = 0b0010'0000; // STEP: no update, no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepCommand);
        fdc._commandRegister = stepCommand;
        fdc._lastDecodedCmd = decodedCommand;

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

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&                // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&    // FDD has the same track
                fdc._state == WD1793::S_IDLE)                       // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&     // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&             // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack && // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                    // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK increasing direction didn't end up correctly";

        size_t estimatedExecutionTime = 6; // We're performing single positioning step 6ms long
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1) << "Abnormal execution time";
        /// endregion </Check results>
    }

    /// endregion </Main test loop>
}

TEST_F(WD1793_Test, FSM_CMD_Step_Decreasing)
{
    static constexpr double const TEST_DURATION_SEC = 0.3;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

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
        fdc._stepDirectionIn = false;

        // Mock parameters
        const uint8_t stepCommand = 0b0010'0000; // STEP: no update, no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepCommand);
        fdc._commandRegister = stepCommand;
        fdc._lastDecodedCmd = decodedCommand;

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

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&                // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&    // FDD has the same track
                fdc._state == WD1793::S_IDLE)                       // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&     // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&             // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack && // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                    // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK decreasing direction didn't end up correctly";

        size_t estimatedExecutionTime = 6; // We're performing single positioning step 6ms long
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
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

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

        // Mock parameters
        const uint8_t stepInCommand = 0b0100'0000; // StepIn: no update, no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepInCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepInCommand);
        fdc._commandRegister = stepInCommand;
        fdc._lastDecodedCmd = decodedCommand;

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

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&                // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&    // FDD has the same track
                fdc._state == WD1793::S_IDLE)                       // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&     // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&             // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack && // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                    // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK_IN didn't end up correctly";

        size_t estimatedExecutionTime = 6; // We're performing single positioning step 6ms long
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
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation

    // Internal logging messages are done on Info level
    //_context->pModuleLogger->SetLoggingLevel(LogInfo);

    WD1793CUT fdc(_context);

    for (int i = MAX_CYLINDERS - 1; i >= 1; i--)
    {
        uint8_t targetTrack = i - 1;

        // Set initial conditions
        fdc._selectedDrive->setTrack(i);
        fdc._trackRegister = i;

        // Mock parameters
        const uint8_t stepOutCommand = 0b0110'0000; // StepOut: no update, no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
        WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(stepOutCommand);
        uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, stepOutCommand);
        fdc._commandRegister = stepOutCommand;
        fdc._lastDecodedCmd = decodedCommand;

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

            if (!(fdc._statusRegister & WD1793::WDS_BUSY) &&        // Controller is not BUSY anymore
                fdc._trackRegister == targetTrack &&                // FDC track set to <next track>
                fdc._selectedDrive->getTrack() == targetTrack &&    // FDD has the same track
                fdc._state == WD1793::S_IDLE)                       // FSM is in idle state
            {
                // STEP_IN command finished
                break;
            }
        }
        /// endregion </Perform simulation loop>

        /// region <Check results>
        size_t elapsedTimeTStates = clk;
        size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

        bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&     // Controller is not BUSY anymore
                                       fdc._trackRegister == targetTrack &&             // FDC track set to <next track>
                                       fdc._selectedDrive->getTrack() == targetTrack && // FDD has the same track
                                       fdc._state == WD1793::S_IDLE;                    // FSM is in idle state

        EXPECT_EQ(isAccomplishedCorrectly, true) << "SEEK_OUT didn't end up correctly";

        size_t estimatedExecutionTime = 6; // We're performing single positioning step 6ms long
        EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1) << "Abnormal execution time";
        /// endregion </Check results>
    }
}

/// endregion </STEP_OUT>

/// region <READ_SECTOR>
TEST_F(WD1793_Test, FSM_CMD_Read_Sector_Single)
{
    static constexpr size_t const RESTORE_TEST_DURATION_SEC = 1;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * RESTORE_TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 10; // Time increments during simulation

    // Test parameters
    static constexpr const uint8_t TEST_TRACK = 0;
    static constexpr const uint8_t TEST_SECTOR = 9;

    // Internal logging messages are done on Info level
    _context->pModuleLogger->SetLoggingLevel(LogInfo);

    // Sector read buffer
    uint8_t sectorData[SECTORS_SIZE_BYTES] = {};
    size_t sectorDataIndex = 0;

    /// region <Load disk image>
    std::string filepath = "../../../tests/loaders/trd/EyeAche.trd";
    filepath = FileHelper::AbsolutePath(filepath);
    LoaderTRDCUT trdLoader(_context, filepath);
    bool imageLoaded = trdLoader.loadImage();

    EXPECT_EQ(imageLoaded, true) << "Test TRD image was not loaded";

    DiskImage* diskImage = trdLoader.getImage();

    EXPECT_NE(diskImage, nullptr);
    /// endregion </Load disk image>

    WD1793CUT fdc(_context);
    fdc._selectedDrive->insertDisk(diskImage);

    /// region <Create parameters for READ_SECTOR>
    const uint8_t readSectorCommand = 0b0100'0000;
    WD1793CUT::WD_COMMANDS decodedCommand = WD1793CUT::decodeWD93Command(readSectorCommand);
    uint8_t commandValue = WD1793CUT::getWD93CommandValue(decodedCommand, readSectorCommand);
    fdc._commandRegister = readSectorCommand;
    fdc._lastDecodedCmd = decodedCommand;

    fdc._trackRegister = TEST_TRACK;
    fdc._selectedDrive->setTrack(TEST_TRACK);
    fdc._sectorRegister = TEST_SECTOR;
    /// endregion </Create parameters for READ_SECTOR>

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

        // Fetch data bytes with marking Data Register accessed so no DATA LOSS error occurs
        if (fdc._state == WD1793::S_READ_BYTE && !fdc._dataRegisterAccessed)
        {
            uint8_t readValue = fdc.readDataRegister();
            std::cout << StringHelper::Format("Byte '%c' %d (0x%02X) read", readValue, readValue, readValue) << std::endl;

            sectorData[sectorDataIndex++] = readValue;
        }

        // Check if test sequence already finished
        if (fdc._state == WD1793::S_IDLE)
        {
            break;
        }
    }
    /// endregion </Perform simulation loop>

    /// region <Check results>
    size_t elapsedTimeTStates = clk;
    size_t elapsedTimeMs = TestTimingHelper::convertTStatesToMs(clk);

    bool isAccomplishedCorrectly = !(fdc._statusRegister & WD1793::WDS_BUSY) &&     // Controller is not BUSY anymore
                                   fdc._trackRegister == 0 &&                       // FDC track set to <next track>
                                   fdc._selectedDrive->getTrack() == 0 &&           // FDD has the same track
                                   fdc._state == WD1793::S_IDLE;                    // FSM is in idle state

    EXPECT_EQ(isAccomplishedCorrectly, true) << "READ_SECTOR didn't end up correctly";

    size_t estimatedExecutionTime = 256 * WD1793::TSTATES_PER_FDC_BYTE / TSTATES_IN_MS; // We're performing single positioning step 6ms long
    EXPECT_IN_RANGE(elapsedTimeMs, estimatedExecutionTime, estimatedExecutionTime + 1) << "Abnormal execution time";

    EXPECT_EQ(sectorDataIndex, 256) << "Not all sector bytes were read";

    DiskImage::Track* track00 = diskImage->getTrackForCylinderAndSide(TEST_TRACK, 0);
    uint8_t* referenceSector = track00->getDataForSector(TEST_SECTOR);

    EXPECT_ARRAYS_EQ(sectorData, referenceSector, SECTORS_SIZE_BYTES) << "Sector read data is not expected";

    std::cout << "Read sector dump:" << std::endl;
    std::cout << DumpHelper::HexDumpBuffer(sectorData, sizeof(sectorData) / sizeof(sectorData[0])) << std::endl;
    /// endregion </Check results>
}
/// endregion </READ_SECTOR>

/// region <FORCE_INTERRUPT>

TEST_F(WD1793_Test, ForceInterrupt_NotReadyToReady)
{
    FAIL() << "Not implemented yet";
}

TEST_F(WD1793_Test, ForceInterrupt_ReadyToNotReady)
{
    FAIL() << "Not implemented yet";
}

TEST_F(WD1793_Test, ForceInterrupt_IndexPulse)
{
    FAIL() << "Not implemented yet";
}

TEST_F(WD1793_Test, ForceInterrupt_Terminate)
{
    static constexpr size_t const TEST_DURATION_SEC = 1;
    static constexpr size_t const TEST_DURATION_TSTATES = Z80_FREQUENCY * TEST_DURATION_SEC;
    static constexpr size_t const TEST_INCREMENT_TSTATES = 100; // Time increments during simulation
    static constexpr size_t const TEST_TRACKS = 40;

    // Disable all logging except error messages
    _context->pModuleLogger->SetLoggingLevel(LogError);

    WD1793CUT fdc(_context);

    /// region <Interrupt during idle>
    {
        // Mock parameters
        const uint8_t forceInterruptCommand = 0b1101'0000; // FORCE_INTERRUPT with 4 lower bits zeroed - Terminate with no interrupt
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

            const uint8_t restoreCommand = 0b0000'0000; // RESTORE on reset is done with all bits zeroed: no load head, no verify and fastest stepping rate 00 (3ms @ 2MHz, 6ms @ 1MHz)
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

        int64_t estimateRestoreDuration = TEST_TRACKS * 6; // 6ms per step

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

        /// region <Pre-checks>
        EXPECT_EQ(fdc._selectedDrive->getTrack(), TEST_TRACKS / 2); // Ensure we've reached Track 20
        EXPECT_IN_RANGE(fdc._time, positioningDuration - TEST_INCREMENT_TSTATES, positioningDuration +  TEST_INCREMENT_TSTATES);
        /// endregion </Pre-checks>

        /// endregion </Perform simulation loop>


        /// region <Execute FORCE_TERMINATE command>
        {
            const uint8_t forceInterruptCommand = 0b1101'0000; // FORCE_INTERRUPT with 4 lower bits zeroed - Terminate with no interrupt
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
        EXPECT_IN_RANGE(fdc._time, positioningDuration - TEST_INCREMENT_TSTATES, positioningDuration +  TEST_INCREMENT_TSTATES);

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
        EXPECT_EQ(INTRQ, false);    // There should be no interrupt request
        EXPECT_EQ(DRQ, false);      // No data request either
        /// endregion </Check results>
    }
    /// endregion </Interrupt during idle>
}

/// endregion </FORCE_INTERRUPT>

/// endregion </Commands>