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

#include "emulator/io/fdc/diskimage.h"
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

/// RESTORE must behave identically whether the controller is fresh, has been
/// woken from sleep after a motor timeout, or sits in its awake-idle window with
/// the motor off (where handleStep() skips the FSM entirely). The FSM charges
/// _diffTime (time since the previous process()) against pending step delays,
/// so a clock left stale across an idle period would make the first delay
/// elapse instantly and the seek finish far too early. Measured in whole frames:
/// 10 steps x 6 ms = 60 ms must take at least 60 ms and at most two frames more.
TEST_F(WD1793_SleepTimeout_Test, RestoreTimingIdenticalAfterTimeoutSleepAndAwakeIdle)
{
    static constexpr int8_t START_TRACK = 10;
    static constexpr uint8_t CMD_RESTORE_6MS = 0x00;  // RESTORE: no head load, no verify, 6 ms stepping rate
    static constexpr size_t EXPECTED_TSTATES = START_TRACK * 6 * (Z80_FREQUENCY / 1000);
    static constexpr size_t MAX_RUN_TSTATES = 20 * FRAME_TSTATES;

    auto restoreAndMeasure = [this](const char* phase) -> size_t
    {
        _fdc->getDrive()->setTrack(START_TRACK);
        _fdc->portDeviceOutMethod(WD1793::PORT_1F, CMD_RESTORE_6MS);
        EXPECT_FALSE(_fdc->isSleeping()) << phase << ": command must wake the controller";
        EXPECT_TRUE(_fdc->getDrive()->getMotor()) << phase << ": command must start the motor";

        size_t elapsed = 0;
        while (elapsed < MAX_RUN_TSTATES && (_fdc->getStatusRegister() & WD1793::WDS_BUSY))
        {
            elapsed += RunEmulation(FRAME_TSTATES);
        }

        EXPECT_FALSE(_fdc->getStatusRegister() & WD1793::WDS_BUSY) << phase << ": RESTORE did not complete";
        EXPECT_EQ(static_cast<int>(_fdc->getDrive()->getTrack()), 0) << phase << ": head is not at track 0";
        return elapsed;
    };

    // Phase 1: fresh controller
    _z80->t = 1000;
    _context->emulatorState.t_states = 0;
    const size_t fresh = restoreAndMeasure("fresh");
    EXPECT_GE(fresh, EXPECTED_TSTATES) << "fresh: RESTORE finished before its step delays could have elapsed";
    EXPECT_LE(fresh, EXPECTED_TSTATES + 2 * FRAME_TSTATES) << "fresh: RESTORE took too long";

    // Phase 2: 6 s without FDC access - motor times out at ~3 s, controller sleeps
    RunEmulation(6 * Z80_FREQUENCY);
    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Precondition: motor must have timed out";
    ASSERT_TRUE(_fdc->isSleeping()) << "Precondition: controller must be asleep";
    const size_t afterSleep = restoreAndMeasure("woken from sleep");
    EXPECT_EQ(afterSleep, fresh) << "RESTORE timing differs after sleep";

    // Phase 3: awake-idle window - a status poll wakes the controller, then 1 s
    // of idle with the motor off (handleStep skips the FSM here)
    RunEmulation(6 * Z80_FREQUENCY);
    ASSERT_TRUE(_fdc->isSleeping()) << "Precondition: controller must be asleep again";
    _fdc->portDeviceInMethod(WD1793::PORT_1F);
    ASSERT_FALSE(_fdc->isSleeping()) << "Precondition: status poll must wake the controller";
    RunEmulation(1 * Z80_FREQUENCY);
    ASSERT_FALSE(_fdc->isSleeping()) << "Precondition: still inside the 2 s idle window";
    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Precondition: motor must be off";
    const size_t awakeIdle = restoreAndMeasure("awake-idle, motor off");
    EXPECT_EQ(awakeIdle, fresh) << "RESTORE timing differs from the awake-idle window";
}

/// Motor timeout (~3 s) must stop the motor without raising INTRQ.
/// Real WD1793 hardware has no motor control pin and never signals INTRQ on motor stop.
/// Spurious INTRQ on motor stop sets bit 7 of Beta128 status port #FF, breaking guest polling loops.
TEST_F(WD1793_SleepTimeout_Test, MotorTimeoutDoesNotRaiseIntrq)
{
    // Emulate command start: wake controller and start motor
    _z80->t = 1000;
    _context->emulatorState.t_states = 0;
    _fdc->wakeUp();
    _fdc->prolongFDDMotorRotation();
    ASSERT_TRUE(_fdc->getDrive()->getMotor()) << "Precondition: motor must be spinning";

    // Read status port #1F to clear any residual INTRQ
    _fdc->portDeviceInMethod(WD1793::PORT_1F);
    EXPECT_FALSE(_fdc->_intrq_out) << "INTRQ should be clear before timeout";
    EXPECT_FALSE(_fdc->portDeviceInMethod(0x00FF) & WD1793::INTRQ)
        << "Beta128 port #FF bit 7 (INTRQ) should be 0 before timeout";

    // Run emulation past the motor timeout (15 revolutions = ~3 s, run 5 s)
    RunEmulation(5 * Z80_FREQUENCY);

    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Motor must have timed out";
    EXPECT_FALSE(_fdc->_intrq_out) << "Motor stop must NOT raise INTRQ";
    EXPECT_FALSE(_fdc->portDeviceInMethod(0x00FF) & WD1793::INTRQ)
        << "Beta128 port #FF bit 7 (INTRQ) must NOT be set by motor timeout";
}

/// Type 2 (Read/Write Sector) and Type 3 commands must start the motor when issued
/// while the motor is stopped (e.g. after a motor timeout while sleeping or in awake-idle).
/// If the motor is not started before checking isReady(), the FDC deadlocks: isReady()
/// requires a spinning motor, aborts the command immediately, and the FDC remains idle forever.
TEST_F(WD1793_SleepTimeout_Test, ReadSectorStartsMotorAfterTimeoutSleepAndAwakeIdle)
{
    static constexpr uint8_t CMD_READ_SECTOR = 0x80;
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    _fdc->getDrive()->insertDisk(&diskImage);

    // Phase 1: Woken from sleep after motor timeout
    _z80->t = 1000;
    _context->emulatorState.t_states = 0;
    _fdc->wakeUp();
    _fdc->prolongFDDMotorRotation();
    ASSERT_TRUE(_fdc->getDrive()->getMotor());

    // Run 5 s without FDC access -> motor times out and controller sleeps
    RunEmulation(5 * Z80_FREQUENCY);
    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Precondition: motor must have timed out";
    ASSERT_TRUE(_fdc->isSleeping()) << "Precondition: controller must be asleep";

    // Issue Read Sector command via port #1F
    _fdc->portDeviceOutMethod(WD1793::PORT_1F, CMD_READ_SECTOR);

    EXPECT_FALSE(_fdc->isSleeping()) << "Read Sector must wake controller";
    EXPECT_TRUE(_fdc->getDrive()->getMotor()) << "Read Sector must start motor after timeout sleep";
    EXPECT_TRUE(_fdc->getStatusRegister() & WD1793::WDS_BUSY)
        << "Read Sector must be busy (not rejected by isReady deadlock)";
    EXPECT_FALSE(_fdc->getStatusRegister() & WD1793::WDS_NOTRDY)
        << "Read Sector must not set NOT READY when disk is inserted";

    // Phase 2: In awake-idle window with motor off
    // Let motor time out again (5 s)
    RunEmulation(5 * Z80_FREQUENCY);
    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Precondition: motor timed out";
    ASSERT_TRUE(_fdc->isSleeping()) << "Precondition: controller asleep";

    // Wake up via status poll (awake-idle)
    _fdc->portDeviceInMethod(WD1793::PORT_1F);
    ASSERT_FALSE(_fdc->isSleeping()) << "Precondition: status poll wakes controller";
    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Precondition: motor still off in awake-idle";

    // Issue Read Sector command while controller is awake but motor is off
    _fdc->portDeviceOutMethod(WD1793::PORT_1F, CMD_READ_SECTOR);

    EXPECT_TRUE(_fdc->getDrive()->getMotor()) << "Read Sector must start motor in awake-idle";
    EXPECT_TRUE(_fdc->getStatusRegister() & WD1793::WDS_BUSY)
        << "Read Sector must be busy in awake-idle";
    EXPECT_FALSE(_fdc->getStatusRegister() & WD1793::WDS_NOTRDY);

    _fdc->getDrive()->insertDisk(nullptr);
}

/// Type 3 command (Read Address / Read Track) must also start the motor when issued
/// while the motor is stopped (after timeout sleep or in awake-idle).
TEST_F(WD1793_SleepTimeout_Test, ReadAddressStartsMotorAfterTimeoutSleep)
{
    static constexpr uint8_t CMD_READ_ADDRESS = 0xC0;
    DiskImage diskImage(MAX_CYLINDERS, MAX_SIDES);
    _fdc->getDrive()->insertDisk(&diskImage);

    // Let motor time out and controller sleep
    RunEmulation(5 * Z80_FREQUENCY);
    ASSERT_FALSE(_fdc->getDrive()->getMotor()) << "Precondition: motor must have timed out";
    ASSERT_TRUE(_fdc->isSleeping()) << "Precondition: controller must be asleep";

    // Issue Read Address command via port #1F
    _fdc->portDeviceOutMethod(WD1793::PORT_1F, CMD_READ_ADDRESS);

    EXPECT_FALSE(_fdc->isSleeping()) << "Read Address must wake controller";
    EXPECT_TRUE(_fdc->getDrive()->getMotor()) << "Read Address must start motor after timeout sleep";
    EXPECT_TRUE(_fdc->getStatusRegister() & WD1793::WDS_BUSY);
    EXPECT_FALSE(_fdc->getStatusRegister() & WD1793::WDS_NOTRDY);

    _fdc->getDrive()->insertDisk(nullptr);
}

/// When no disk is inserted, Type 2 command must fail with NOTRDY status,
/// transition to end of command, clear BUSY, and raise INTRQ.
TEST_F(WD1793_SleepTimeout_Test, ReadSectorWithoutDiskInsertedFailsGracefully)
{
    _fdc->getDrive()->insertDisk(nullptr);
    _fdc->portDeviceOutMethod(WD1793::PORT_1F, 0x80);

    // Command sets BUSY and NOTRDY initially; BUSY stays visible for a short hold (a real chip raises it
    // before it notices the drive is not ready - the Profi BIOS polls for it) and then S_END_COMMAND runs
    EXPECT_TRUE(_fdc->getStatusRegister() & WD1793::WDS_NOTRDY);
    EXPECT_TRUE(_fdc->getStatusRegister() & WD1793::WDS_BUSY);

    // Still inside the hold: BUSY must remain set
    _z80->t += 20;
    _fdc->handleStep();
    EXPECT_TRUE(_fdc->getStatusRegister() & WD1793::WDS_BUSY) << "BUSY dropped inside the hold window";

    // Past the hold, the next CPU step executes S_END_COMMAND: BUSY is cleared, INTRQ raised
    _z80->t += 100;
    _fdc->handleStep();
    _z80->t += 10;
    _fdc->handleStep();

    EXPECT_TRUE(_fdc->getStatusRegister() & WD1793::WDS_NOTRDY);
    EXPECT_FALSE(_fdc->getStatusRegister() & WD1793::WDS_BUSY);
    EXPECT_TRUE(_fdc->_intrq_out);
    EXPECT_TRUE(_fdc->portDeviceInMethod(0x00FF) & WD1793::INTRQ);
}
