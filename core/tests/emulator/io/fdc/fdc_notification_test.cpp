// @brief Unit tests for WD1793 FDC state notifications (NC_FDC_STATE_CHANGED)
// @details Verifies diff-gated posting on track/sector register writes, Beta128
//          drive/side selection, command start (BUSY), payload field integrity
//          and the new drive/side accessors.

#define _CODE_UNDER_TEST 1

#include "emulator/io/fdc/wd1793.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

// ==================== Notification capture ====================
// Static capture vector + plain-function observer (MessageCenter is a singleton).
// Suite-level registration mirrors fdd_notification_test.cpp — capturing lambdas
// are never used because RemoveObserver cannot match them (known library flaw).

namespace
{
    struct CapturedFdcState
    {
        std::string emulatorId;
        uint8_t driveId = 0;
        uint8_t side = 0;
        uint8_t trackRegister = 0;
        uint8_t sectorRegister = 0;
        uint8_t physicalTrack = 0;
        uint8_t command = 0;
        uint8_t status = 0;
        bool busy = false;
        bool drq = false;
        bool motorOn = false;
        bool diskInserted = false;
    };

    // All accesses to g_capturedFdcStates happen under this mutex: the
    // MessageCenter worker thread push_backs from Dispatch() while test
    // threads read/clear from WaitForCondition polls and SetUp. An
    // unsynchronized std::vector is UB - a push_back reallocating during a
    // concurrent back()/empty()/size() read yields torn state (observed as an
    // empty emulatorId string in test-parallel runs).
    std::mutex g_captureMutex;
    std::vector<CapturedFdcState> g_capturedFdcStates;
    bool g_observersRegistered = false;

    void onFdcStateChanged(int id, Message* message)
    {
        (void)id;
        if (message && message->obj)
        {
            FDCStatePayload* payload = static_cast<FDCStatePayload*>(message->obj);
            CapturedFdcState captured;
            captured.emulatorId = payload->_emulatorId.toString();
            captured.driveId = payload->_driveId;
            captured.side = payload->_side;
            captured.trackRegister = payload->_trackRegister;
            captured.sectorRegister = payload->_sectorRegister;
            captured.physicalTrack = payload->_physicalTrack;
            captured.command = payload->_command;
            captured.status = payload->_status;
            captured.busy = payload->_busy;
            captured.drq = payload->_drq;
            captured.motorOn = payload->_motorOn;
            captured.diskInserted = payload->_diskInserted;
            std::lock_guard<std::mutex> lock(g_captureMutex);
            g_capturedFdcStates.push_back(captured);
        }
    }

    void clearCaptures()
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        g_capturedFdcStates.clear();
    }

    size_t captureCount()
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        return g_capturedFdcStates.size();
    }

    bool lastCapture(CapturedFdcState& out)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        if (g_capturedFdcStates.empty())
        {
            return false;
        }
        out = g_capturedFdcStates.back();
        return true;
    }

    bool hasCaptureWithTrack(uint8_t track)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        for (const auto& captured : g_capturedFdcStates)
        {
            if (captured.trackRegister == track)
            {
                return true;
            }
        }
        return false;
    }

    bool hasCaptureWithSector(uint8_t sector)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        for (const auto& captured : g_capturedFdcStates)
        {
            if (captured.sectorRegister == sector)
            {
                return true;
            }
        }
        return false;
    }

    bool hasCaptureWithDriveAndSide(uint8_t driveId, uint8_t side)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        for (const auto& captured : g_capturedFdcStates)
        {
            if (captured.driveId == driveId && captured.side == side)
            {
                return true;
            }
        }
        return false;
    }

    // Returns a copy taken under the capture mutex - a pointer into the
    // vector would dangle as soon as the worker thread push_backs again.
    bool findCaptureWithCommand(uint8_t command, CapturedFdcState& out)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        for (const auto& captured : g_capturedFdcStates)
        {
            if (captured.command == command && captured.busy)
            {
                out = captured;
                return true;
            }
        }
        return false;
    }
}  // namespace

// ==================== Fixture ====================

class FDCNotificationTest : public ::testing::Test
{
protected:
    static void SetUpTestSuite()
    {
        // Ensure MessageCenter is running
        MessageCenter::DefaultMessageCenter(true);

        // Register observers once for entire test suite
        if (!g_observersRegistered)
        {
            MessageCenter& mc = MessageCenter::DefaultMessageCenter();
            mc.AddObserver(NC_FDC_STATE_CHANGED, &onFdcStateChanged);
            g_observersRegistered = true;
        }
    }

    static bool WaitForCondition(const std::function<bool()>& condition, int timeoutMs = 200)
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

    // Barrier against cross-test pollution. The previous test's last posts
    // may still sit in the MessageCenter queue while its own WaitForCondition
    // already returned - under parallel-shard load the worker can lag several
    // messages behind. Clearing the capture vector in SetUp alone would let
    // those late payloads land AFTER the clear and pollute this test. Posting
    // a sentinel through the same queue and waiting for it to be captured
    // guarantees every earlier post (any topic, single FIFO worker) has been
    // dispatched by the time SetUp clears.
    static constexpr uint8_t SENTINEL_TRACK = 0xFE;  // No test writes this track value

    static void DrainPendingNotifications()
    {
        FDCStatePayload* sentinel = new FDCStatePayload(unreal::UUID());
        sentinel->_trackRegister = SENTINEL_TRACK;
        MessageCenter::DefaultMessageCenter().Post(NC_FDC_STATE_CHANGED, sentinel, true);

        if (!WaitForCondition([]() { return hasCaptureWithTrack(SENTINEL_TRACK); }, 1000))
        {
            FAIL() << "MessageCenter drain sentinel not dispatched within 1000 ms";
        }
    }

    void SetUp() override
    {
        // Drain in-flight posts from the previous test, then clear captured events
        DrainPendingNotifications();
        clearCaptures();

        // Full context wiring: portDeviceOutMethod dereferences pCore->GetZ80()
        // and pMemory in its debug-print preamble, and the command path's
        // WD1793Collector uses pCore->GetMemory() — both must be present.
        // WD1793CUT stubs updateTimeFromEmulatorState (no live T-state counters).
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
        _fdc = new WD1793CUT(_context);
    }

    void TearDown() override
    {
        // WD1793 does not own the FDDs — delete them explicitly before the context
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

    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;
    WD1793CUT* _fdc = nullptr;
};

// ==================== Tests ====================

TEST_F(FDCNotificationTest, TrackRegisterWritePostsNotification)
{
    _fdc->portDeviceOutMethod(0x003F, 40);  // #3F - track register

    ASSERT_TRUE(WaitForCondition([]() { return hasCaptureWithTrack(40); })) << "No payload with trackRegister == 40";

    EXPECT_EQ(_fdc->getTrackRegister(), 40);
}

TEST_F(FDCNotificationTest, SectorRegisterWritePostsNotification)
{
    _fdc->portDeviceOutMethod(0x005F, 9);  // #5F - sector register

    ASSERT_TRUE(WaitForCondition([]() { return hasCaptureWithSector(9); })) << "No payload with sectorRegister == 9";

    EXPECT_EQ(_fdc->getSectorRegister(), 9);
}

TEST_F(FDCNotificationTest, NoPostWhenNothingChanged)
{
    _fdc->portDeviceOutMethod(0x003F, 40);  // #3F - track register
    ASSERT_TRUE(WaitForCondition([]() { return hasCaptureWithTrack(40); }));
    clearCaptures();

    // Re-write the same value — the diff gate must swallow it
    _fdc->portDeviceOutMethod(0x003F, 40);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    const size_t leakedPayloads = captureCount();
    EXPECT_EQ(leakedPayloads, 0u) << "Diff gate failed: " << leakedPayloads
                                  << " payload(s) posted for unchanged state";
}

TEST_F(FDCNotificationTest, Beta128WriteChangesDriveAndSide)
{
    // #FF value: drive bits [1:0] = 3, side bit [4] = 0 (top side), reset bit [2] = 1 (inactive)
    _fdc->portDeviceOutMethod(0x00FF, 0x07);

    ASSERT_TRUE(WaitForCondition([]() { return hasCaptureWithDriveAndSide(3, 1); })) << "No payload with driveId == 3, side == 1";
}

TEST_F(FDCNotificationTest, CommandStartSetsBusyAndCommandByte)
{
    // Prepare a SEEK: data register = target track 20, track register stays 0
    _fdc->portDeviceOutMethod(0x007F, 20);  // #7F - data register (no notification expected)

    _fdc->portDeviceOutMethod(0x001F, 0x10);  // #1F - SEEK command

    CapturedFdcState captured;
    ASSERT_TRUE(WaitForCondition([&captured]() { return findCaptureWithCommand(0x10, captured); })) << "No BUSY payload for SEEK";

    EXPECT_TRUE(captured.busy);
    EXPECT_EQ(captured.command, 0x10);
    EXPECT_EQ(captured.trackRegister, 0);
}

TEST_F(FDCNotificationTest, PayloadCarriesEmulatorId)
{
    // The test context has no Emulator object — payload must carry a nil UUID
    // (canonical string form of the all-zeros UUID)
    _fdc->portDeviceOutMethod(0x003F, 5);

    ASSERT_TRUE(WaitForCondition([]() { return captureCount() > 0; }));

    CapturedFdcState last;
    ASSERT_TRUE(lastCapture(last));
    EXPECT_EQ(last.emulatorId, unreal::UUID().toString());
}

TEST_F(FDCNotificationTest, GettersExposeDriveAndSide)
{
    EXPECT_EQ(_fdc->getSelectedDriveIndex(), 0);
    EXPECT_FALSE(_fdc->getSideUp());

    // #FF value: drive bits [1:0] = 2, side bit [4] = 0 (top side), reset bit [2] = 1 (inactive)
    _fdc->portDeviceOutMethod(0x00FF, 0x06);

    EXPECT_EQ(_fdc->getSelectedDriveIndex(), 2);
    EXPECT_TRUE(_fdc->getSideUp());
}
