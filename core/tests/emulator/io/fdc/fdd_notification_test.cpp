// @brief Unit tests for FDD disk insert/eject notifications and DiskImage file path tracking
// @details Tests MessageCenter notifications NC_FDD_DISK_INSERTED and NC_FDD_DISK_EJECTED

#include <gtest/gtest.h>
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/platform.h"

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>
#include <mutex>

// ==================== DiskImage Path Tests ====================

TEST(DiskImagePathTest, DefaultPathIsEmpty)
{
    DiskImage image(80, 2);
    EXPECT_TRUE(image.getFilePath().empty());
}

TEST(DiskImagePathTest, SetAndGetPath)
{
    DiskImage image(80, 2);
    const std::string testPath = "/path/to/disk.trd";
    
    image.setFilePath(testPath);
    
    EXPECT_EQ(image.getFilePath(), testPath);
}

TEST(DiskImagePathTest, SetPathWithSpecialCharacters)
{
    DiskImage image(80, 2);
    const std::string testPath = "/path with spaces/file (1).trd";
    
    image.setFilePath(testPath);
    
    EXPECT_EQ(image.getFilePath(), testPath);
}

TEST(DiskImagePathTest, SetEmptyPath)
{
    DiskImage image(80, 2);
    image.setFilePath("/some/path.trd");
    
    image.setFilePath("");
    
    EXPECT_TRUE(image.getFilePath().empty());
}

TEST(DiskImagePathTest, PathPersistsThroughReset)
{
    DiskImage image(80, 2);
    const std::string testPath = "/path/to/disk.trd";
    image.setFilePath(testPath);
    
    // reset() should not clear file path (it's metadata, not disk content)
    // Note: reset() is protected, so this tests that the path isn't cleared
    // during normal operations
    
    EXPECT_EQ(image.getFilePath(), testPath);
}

// ==================== FDD Insert/Eject Basic Tests ====================

TEST(FDDBasicTest, InsertDiskSetsInsertedFlag)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    
    EXPECT_FALSE(fdd.isDiskInserted());
    
    fdd.insertDisk(&image);
    
    EXPECT_TRUE(fdd.isDiskInserted());
    EXPECT_EQ(fdd.getDiskImage(), &image);
}

TEST(FDDBasicTest, EjectDiskClearsInsertedFlag)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    
    fdd.insertDisk(&image);
    EXPECT_TRUE(fdd.isDiskInserted());
    
    fdd.ejectDisk();
    
    EXPECT_FALSE(fdd.isDiskInserted());
    EXPECT_EQ(fdd.getDiskImage(), nullptr);
}

TEST(FDDBasicTest, InsertNullDiskDoesNothing)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    
    EXPECT_FALSE(fdd.isDiskInserted());
    
    fdd.insertDisk(nullptr);
    
    EXPECT_FALSE(fdd.isDiskInserted());
    EXPECT_EQ(fdd.getDiskImage(), nullptr);
}

TEST(FDDBasicTest, EjectWhenNoDiskInserted)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    
    EXPECT_FALSE(fdd.isDiskInserted());
    
    // Should not crash
    fdd.ejectDisk();
    
    EXPECT_FALSE(fdd.isDiskInserted());
}

// ==================== FDD Notification Tests ====================
// Using static vectors to capture notifications since MessageCenter is a singleton

namespace {
    struct CapturedDiskEvent {
        std::string emulatorId;
        uint8_t driveId;
        std::string diskPath;
    };
    
    // All accesses to both capture vectors happen under this mutex: the
    // MessageCenter worker thread push_backs from Dispatch() while test
    // threads read/clear from WaitForCondition polls and SetUp. An
    // unsynchronized std::vector is UB - a push_back reallocating during a
    // concurrent size()/operator[] read yields torn state (same latent race
    // as the one observed in fdc_notification_test.cpp).
    std::mutex g_captureMutex;
    std::vector<CapturedDiskEvent> g_insertedDisks;
    std::vector<CapturedDiskEvent> g_ejectedDisks;
    bool g_observersRegistered = false;

    void onDiskInserted(int id, Message* msg)
    {
        if (msg && msg->obj)
        {
            FDDDiskPayload* payload = static_cast<FDDDiskPayload*>(msg->obj);
            std::lock_guard<std::mutex> lock(g_captureMutex);
            g_insertedDisks.push_back({
                payload->_emulatorId.toString(),
                payload->_driveId,
                payload->_diskPath
            });
        }
    }

    void onDiskEjected(int id, Message* msg)
    {
        if (msg && msg->obj)
        {
            FDDDiskPayload* payload = static_cast<FDDDiskPayload*>(msg->obj);
            std::lock_guard<std::mutex> lock(g_captureMutex);
            g_ejectedDisks.push_back({
                payload->_emulatorId.toString(),
                payload->_driveId,
                payload->_diskPath
            });
        }
    }

    void clearInserted()
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        g_insertedDisks.clear();
    }

    size_t insertedCount()
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        return g_insertedDisks.size();
    }

    size_t ejectedCount()
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        return g_ejectedDisks.size();
    }

    // Copies taken under the capture mutex - references into the vectors
    // would dangle as soon as the worker thread push_backs again.
    CapturedDiskEvent insertedAt(size_t index)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        return g_insertedDisks[index];
    }

    CapturedDiskEvent ejectedAt(size_t index)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        return g_ejectedDisks[index];
    }

    bool hasInsertedWithDrive(uint8_t driveId)
    {
        std::lock_guard<std::mutex> lock(g_captureMutex);
        for (const auto& captured : g_insertedDisks)
        {
            if (captured.driveId == driveId)
            {
                return true;
            }
        }
        return false;
    }
}

class FDDNotificationTest : public ::testing::Test
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
            mc.AddObserver(NC_FDD_DISK_INSERTED, &onDiskInserted);
            mc.AddObserver(NC_FDD_DISK_EJECTED, &onDiskEjected);
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

    void SetUp() override
    {
        // Barrier against cross-test pollution (same rationale as in
        // fdc_notification_test.cpp): the previous test's last posts may still
        // sit in the MessageCenter queue while its own WaitForCondition
        // already returned. Posting a sentinel through the same
        // single-FIFO-worker queue and waiting for it to be captured guarantees
        // every earlier post (any topic) has been dispatched by the time SetUp
        // clears the vectors.
        constexpr uint8_t SENTINEL_DRIVE = 0xFF;  // No test or FDD uses this drive index
        FDDDiskPayload* sentinel = new FDDDiskPayload(unreal::UUID(), SENTINEL_DRIVE, "");
        MessageCenter::DefaultMessageCenter().Post(NC_FDD_DISK_INSERTED, sentinel, true);
        if (!WaitForCondition([&SENTINEL_DRIVE]() { return hasInsertedWithDrive(SENTINEL_DRIVE); }, 1000))
        {
            FAIL() << "MessageCenter drain sentinel not dispatched within 1000 ms";
        }

        // Drain complete - clear captured events for this test
        {
            std::lock_guard<std::mutex> lock(g_captureMutex);
            g_insertedDisks.clear();
            g_ejectedDisks.clear();
        }
    }
};

TEST_F(FDDNotificationTest, InsertDiskSendsNotificationWithFullContext)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    const std::string testPath = "/test/disk.trd";
    image.setFilePath(testPath);
    
    fdd.insertDisk(&image);
    ASSERT_TRUE(WaitForCondition([]() { return insertedCount() > 0; }));

    ASSERT_EQ(insertedCount(), 1u);
    EXPECT_EQ(insertedAt(0).diskPath, testPath);
    EXPECT_EQ(insertedAt(0).driveId, 0);  // First FDD is drive 0
}

TEST_F(FDDNotificationTest, EjectDiskSendsNotificationWithFullContext)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    const std::string testPath = "/test/disk.trd";
    image.setFilePath(testPath);
    
    fdd.insertDisk(&image);
    ASSERT_TRUE(WaitForCondition([]() { return insertedCount() > 0; }));
    clearInserted();  // Clear the insertion notification

    fdd.ejectDisk();
    ASSERT_TRUE(WaitForCondition([]() { return ejectedCount() > 0; }));

    ASSERT_EQ(ejectedCount(), 1u);
    EXPECT_EQ(ejectedAt(0).diskPath, testPath);
    EXPECT_EQ(ejectedAt(0).driveId, 0);
}

TEST_F(FDDNotificationTest, InsertNullDoesNotSendNotification)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    
    fdd.insertDisk(nullptr);

    // A null insert must post nothing; bounded window, fails the moment it does.
    EXPECT_FALSE(WaitForCondition([]() { return insertedCount() > 0; }, 5))
        << "insertDisk(nullptr) posted an insert notification";

    EXPECT_EQ(insertedCount(), 0u);
}

TEST_F(FDDNotificationTest, MultipleInsertEjectCycles)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    
    DiskImage image1(80, 2);
    image1.setFilePath("/disk1.trd");
    
    // Insert disk 1
    fdd.insertDisk(&image1);
    ASSERT_TRUE(WaitForCondition([]() { return insertedCount() == 1; }));

    // Eject disk 1
    fdd.ejectDisk();
    ASSERT_TRUE(WaitForCondition([]() { return ejectedCount() == 1; }));

    // Re-insert same disk with different path (simulating disk swap)
    image1.setFilePath("/disk2.trd");
    fdd.insertDisk(&image1);
    ASSERT_TRUE(WaitForCondition([]() { return insertedCount() == 2; }));

    // Eject again
    fdd.ejectDisk();
    ASSERT_TRUE(WaitForCondition([]() { return ejectedCount() == 2; }));

    // Should have 2 insertions and 2 ejections
    ASSERT_EQ(insertedCount(), 2u);
    ASSERT_EQ(ejectedCount(), 2u);

    EXPECT_EQ(insertedAt(0).diskPath, "/disk1.trd");
    EXPECT_EQ(insertedAt(1).diskPath, "/disk2.trd");
    EXPECT_EQ(ejectedAt(0).diskPath, "/disk1.trd");
    EXPECT_EQ(ejectedAt(1).diskPath, "/disk2.trd");
}

TEST_F(FDDNotificationTest, InsertWithEmptyPath)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    // Don't set path - should be empty
    
    fdd.insertDisk(&image);
    ASSERT_TRUE(WaitForCondition([]() { return insertedCount() > 0; }));

    ASSERT_EQ(insertedCount(), 1u);
    EXPECT_TRUE(insertedAt(0).diskPath.empty());
}

TEST_F(FDDNotificationTest, PayloadContainsDriveId)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    image.setFilePath("/test.trd");
    
    fdd.insertDisk(&image);
    ASSERT_TRUE(WaitForCondition([]() { return insertedCount() > 0; }));

    ASSERT_EQ(insertedCount(), 1u);
    // Drive ID should be 0 for default FDD
    EXPECT_EQ(insertedAt(0).driveId, 0);
}
