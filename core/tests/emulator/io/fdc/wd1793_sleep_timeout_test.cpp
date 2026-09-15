// @brief Regression tests for WD1793 shutdown-by-timeout when no FDC port access is made
// @details Reproduces: "wd1793 doesn't shut down by timeout when no FDC access is made
//          or issues with messagecenter notifications related".
//
//          The existing sleep-mode tests in wd1793_test.cpp run against WD1793CUT,
//          which stubs updateTimeFromEmulatorState() as a no-op and drives _time
//          manually. These tests restore the real T-state synchronization and drive
//          handleStep()/handleFrameEnd() exactly like MainLoop does (per-instruction
//          handleStep, per-frame counter rebase + handleFrameEnd), so the real
//          time-source interactions of the shutdown paths are covered:
//            1. Motor timeout (15 revolutions / ~3 s) stops the motor with no FDC access
//            2. Idle timeout (2 s) puts the controller to sleep with no FDC access
//            3. A machine reset while the motor is spinning must not leave the motor
//               running forever (sleeping controller never processes the motor timeout)
//            4. The motor stop must be observable through MessageCenter
//               (NC_FDD_MOTOR_STOPPED / NC_FDD_STATE_CHANGED) - the UI motor LED
//               depends on these notifications

#define _CODE_UNDER_TEST 1

#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

// ==================== Test constants ====================
static constexpr size_t const Z80_FREQUENCY = 3.5 * 1'000'000;

// ==================== Real-timing CUT ====================
// WD1793CUT stubs updateTimeFromEmulatorState() so unit tests can move _time by
// hand. The shutdown-by-timeout paths depend on the real T-state sync, so undo
// the stub and call the base (production) implementation.
class WD1793RealTimeCUT : public WD1793CUT
{
public:
    WD1793RealTimeCUT(EmulatorContext* context) : WD1793CUT(context) {};

protected:
    void updateTimeFromEmulatorState() override
    {
        WD1793::updateTimeFromEmulatorState();
    }
};

// ==================== Notification capture ====================
// Plain-function observers (MessageCenter is a singleton; capturing lambdas are
// not used because RemoveObserver cannot match them - known library flaw).
namespace
{
    std::mutex g_motorMutex;
    size_t g_motorStartedNotifications = 0;
    size_t g_motorStoppedNotifications = 0;
    bool g_motorObserversRegistered = false;

    void onMotorStarted(int id, Message* message)
    {
        (void)id;
        (void)message;  // SimpleNumberPayload(driveId) - count only
        std::lock_guard<std::mutex> lock(g_motorMutex);
        g_motorStartedNotifications++;
    }

    void onMotorStopped(int id, Message* message)
    {
        (void)id;
        (void)message;
        std::lock_guard<std::mutex> lock(g_motorMutex);
        g_motorStoppedNotifications++;
    }

    void resetMotorCounters()
    {
        std::lock_guard<std::mutex> lock(g_motorMutex);
        g_motorStartedNotifications = 0;
        g_motorStoppedNotifications = 0;
    }

    size_t motorStoppedCount()
    {
        std::lock_guard<std::mutex> lock(g_motorMutex);
        return g_motorStoppedNotifications;
    }

    bool WaitForCondition(const std::function<bool()>& condition, int timeoutMs = 200)
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (condition())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return condition();
    }
}  // namespace

// ==================== Fixture ====================

class WD1793_SleepTimeout_Test : public ::testing::Test
{
protected:
    // Pentagon frame: 69888 T-states @ 3.5 MHz (~20 ms)
    static constexpr size_t FRAME_TSTATES = 69888;
    // Instruction granularity of handleStep() calls inside a frame
    static constexpr size_t STEP_TSTATES = 1000;

    static void SetUpTestSuite()
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter(true);

        if (!g_motorObserversRegistered)
        {
            mc.AddObserver(NC_FDD_MOTOR_STARTED, &onMotorStarted);
            mc.AddObserver(NC_FDD_MOTOR_STOPPED, &onMotorStopped);
            g_motorObserversRegistered = true;
        }
    }

    void SetUp() override
    {
        resetMotorCounters();

        // Full context wiring (mirrors fdc_notification_test.cpp): the FDC reads
        // pCore->GetZ80() and emulatorState.t_states through the real
        // updateTimeFromEmulatorState() restored by WD1793RealTimeCUT
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->pModuleLogger->TurnOffLoggingForAll();
        _context->pMemory = new Memory(_context);
        _context->pMemory->Reset();
        _core = new CoreCUT(_context);
        _core->_memory = _context->pMemory;
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;

        // WD1793 ctor creates 4 FDDs in coreState.diskDrives[] and selects drive A
        _fdc = new WD1793RealTimeCUT(_context);
    }

    void TearDown() override
    {
        // WD1793 does not own the FDDs - delete them explicitly before the context
        delete _fdc;
        if (_context)
        {
            for (size_t i = 0; i < 4; ++i)
            {
                delete _context->coreState.diskDrives[i];
                _context->coreState.diskDrives[i] = nullptr;
            }
            if (_context->pCore)
            {
                _core->_z80 = nullptr;
                delete _z80;
                _context->pCore = nullptr;
                delete _core;
            }
            if (_context->pMemory)
            {
                delete _context->pMemory;
                _context->pMemory = nullptr;
            }
            delete _context;
        }
    }

    /// Advance emulated time the way MainLoop does:
    /// - Z80FrameCycle: instructions until the frame T-state limit, handleStep() after each
    /// - AdjustFrameCounters + OnFrameEnd: rebase z80->t, add the frame to t_states,
    ///   then handleFrameEnd()
    /// @param tStates Total emulated T-states to run
    /// @return Total T-states actually run (multiple of full frames + steps)
    size_t RunEmulation(size_t tStates)
    {
        size_t executed = 0;
        while (executed < tStates)
        {
            // One frame: drive handleStep() at instruction granularity
            size_t frameT = 0;
            _z80->t = 0;
            while (frameT < FRAME_TSTATES)
            {
                size_t chunk = std::min(STEP_TSTATES, FRAME_TSTATES - frameT);
                _z80->t += static_cast<uint32_t>(chunk);
                frameT += chunk;
                _fdc->handleStep();
                if (_fdc->isSleeping())
                {
                    // Controller sleeps - MainLoop would keep calling handleStep(),
                    // but it returns immediately; skip to the frame end for speed
                    _z80->t = static_cast<uint32_t>(FRAME_TSTATES);
                    break;
                }
            }

            // Frame boundary (Core::AdjustFrameCounters + MainLoop::OnFrameEnd)
            _z80->t -= static_cast<uint32_t>(FRAME_TSTATES);
            _context->emulatorState.t_states += FRAME_TSTATES;
            executed += FRAME_TSTATES;
            _fdc->handleFrameEnd();
        }
        return executed;
    }

    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;
    WD1793RealTimeCUT* _fdc = nullptr;
};

// ==================== Tests ====================

/// Motor timeout (15 disk revolutions = ~3 s) must stop the motor even when the
/// software makes no further FDC port access, and the controller must enter sleep
/// mode afterwards. The stop must be observable through MessageCenter.
TEST_F(WD1793_SleepTimeout_Test, MotorTimeoutStopsMotorWithNoFdcAccess)
{
    static constexpr size_t MOTOR_TIMEOUT_TSTATES = 15 * (Z80_FREQUENCY / FDD::DISK_REVOLUTIONS_PER_SECOND);  // 10.5M

    // Emulate a command start: wake the controller and start the motor
    _z80->t = 1000;
    _context->emulatorState.t_states = 0;
    _fdc->wakeUp();
    ASSERT_FALSE(_fdc->isSleeping());
    _fdc->prolongFDDMotorRotation();
    ASSERT_TRUE(_fdc->getDrive()->getMotor()) << "Precondition: motor must be spinning";

    const size_t initialStoppedNotifications = motorStoppedCount();
    size_t motorStopTStates = 0;

    // Run 5 s of emulation with no FDC port access, recording when the motor stops
    static constexpr size_t RUN_TSTATES = 5 * Z80_FREQUENCY;
    size_t executed = 0;
    while (executed < RUN_TSTATES && _fdc->getDrive()->getMotor())
    {
        size_t before = RunEmulation(FRAME_TSTATES);
        executed += before;
        if (_fdc->getDrive()->getMotor() == false && motorStopTStates == 0)
        {
            motorStopTStates = executed;
        }
    }

    // Motor must have stopped roughly at the 3 s mark (tolerance: one frame + step granularity)
    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Motor still spinning after 5 s without FDC access";
    EXPECT_GE(motorStopTStates, MOTOR_TIMEOUT_TSTATES - FRAME_TSTATES) << "Motor stopped too early";
    EXPECT_LE(motorStopTStates, MOTOR_TIMEOUT_TSTATES + FRAME_TSTATES) << "Motor stopped too late";

    // Controller must be asleep by now (idle far beyond the 2 s sleep threshold)
    EXPECT_TRUE(_fdc->isSleeping()) << "FDC did not enter sleep mode after motor timeout";

    // The motor stop must be published: NC_FDD_MOTOR_STOPPED (FDD) consumers
    EXPECT_TRUE(WaitForCondition([initialStoppedNotifications]()
                                  { return motorStoppedCount() > initialStoppedNotifications; }))
        << "NC_FDD_MOTOR_STOPPED was not posted when the motor timed out";
}

/// A woken but completely idle controller (motor never started) must enter sleep
/// mode after SLEEP_AFTER_IDLE_TSTATES (2 s) even if no port is ever touched again.
TEST_F(WD1793_SleepTimeout_Test, IdleControllerSleepsWithNoFdcAccess)
{
    static constexpr size_t SLEEP_TSTATES = 2 * Z80_FREQUENCY;  // WD1793::SLEEP_AFTER_IDLE_TSTATES

    _z80->t = 1000;
    _context->emulatorState.t_states = 0;
    _fdc->wakeUp();
    ASSERT_FALSE(_fdc->isSleeping());

    // 1.5 s - still awake (below the 2 s threshold)
    RunEmulation(SLEEP_TSTATES - FRAME_TSTATES * 10);
    EXPECT_FALSE(_fdc->isSleeping()) << "Controller must stay awake before the idle timeout";

    // Continue past 2 s total - must be asleep now
    RunEmulation(FRAME_TSTATES * 12);
    EXPECT_TRUE(_fdc->isSleeping()) << "Controller did not enter sleep mode after 2 s idle";
}

/// Machine reset (Core::reset -> WD1793::reset -> internalReset) while the motor
/// is spinning must stop the motor. internalReset() puts the controller to sleep,
/// and a sleeping controller never processes the motor timeout - a motor left
/// spinning here runs forever and NC_FDD_MOTOR_STOPPED is never posted.
TEST_F(WD1793_SleepTimeout_Test, MachineResetStopsSpinningMotor)
{
    // Wake the controller and start the motor, as any FDC command would
    _z80->t = 1000;
    _context->emulatorState.t_states = 0;
    _fdc->wakeUp();
    _fdc->prolongFDDMotorRotation();
    ASSERT_TRUE(_fdc->getDrive()->getMotor()) << "Precondition: motor must be spinning";

    const size_t initialStoppedNotifications = motorStoppedCount();

    // Machine reset while the motor is spinning
    _fdc->reset();

    EXPECT_FALSE(_fdc->getDrive()->getMotor()) << "Motor left spinning after machine reset - "
                                                  "a sleeping controller never times it out";

    // Run 10 s of idle emulation - the motor must not resurrect itself
    RunEmulation(10 * Z80_FREQUENCY);
    EXPECT_FALSE(_fdc->getDrive()->getMotor()) << "Motor still spinning 10 s after reset";
    EXPECT_TRUE(_fdc->isSleeping());

    // The reset must publish the motor stop so UI consumers resync
    EXPECT_TRUE(WaitForCondition([initialStoppedNotifications]()
                                  { return motorStoppedCount() > initialStoppedNotifications; }))
        << "NC_FDD_MOTOR_STOPPED was not posted when the reset stopped the motor";
}

/// Beta128 system-controller reset (port #FF reset bit) must stop a spinning motor
/// exactly once - no duplicate NC_FDD_MOTOR_STOPPED notifications.
TEST_F(WD1793_SleepTimeout_Test, Beta128ResetStopsSpinningMotorOnce)
{
    // Select drive A, no reset (bit 2 set = inactive)
    _fdc->portDeviceOutMethod(0x00FF, 0b0000'0101);

    // Start the motor through a command path
    _z80->t = 1000;
    _fdc->wakeUp();
    _fdc->prolongFDDMotorRotation();
    ASSERT_TRUE(_fdc->getDrive()->getMotor());

    const size_t initialStoppedNotifications = motorStoppedCount();

    // Beta128 reset: bit 2 low
    _fdc->portDeviceOutMethod(0x00FF, 0b0000'0001);

    EXPECT_FALSE(_fdc->getDrive()->getMotor()) << "Beta128 reset must stop the motor";

    // Exactly one stop notification for this reset (no duplicates)
    ASSERT_TRUE(WaitForCondition([initialStoppedNotifications]()
                                 { return motorStoppedCount() > initialStoppedNotifications; }));
    EXPECT_EQ(motorStoppedCount() - initialStoppedNotifications, 1u)
        << "Duplicate NC_FDD_MOTOR_STOPPED posted for a single motor stop";
}
