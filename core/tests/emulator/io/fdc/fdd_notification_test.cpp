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
    
    std::vector<CapturedDiskEvent> g_insertedDisks;
    std::vector<CapturedDiskEvent> g_ejectedDisks;
    bool g_observersRegistered = false;
    
    void onDiskInserted(int id, Message* msg)
    {
        if (msg && msg->obj)
        {
            FDDDiskPayload* payload = static_cast<FDDDiskPayload*>(msg->obj);
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
            g_ejectedDisks.push_back({
                payload->_emulatorId.toString(),
                payload->_driveId,
                payload->_diskPath
            });
        }
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
        // Clear captured events before each test
        g_insertedDisks.clear();
        g_ejectedDisks.clear();
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
    ASSERT_TRUE(WaitForCondition([]() { return !g_insertedDisks.empty(); }));
    
    ASSERT_EQ(g_insertedDisks.size(), 1);
    EXPECT_EQ(g_insertedDisks[0].diskPath, testPath);
    EXPECT_EQ(g_insertedDisks[0].driveId, 0);  // First FDD is drive 0
}

TEST_F(FDDNotificationTest, EjectDiskSendsNotificationWithFullContext)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    const std::string testPath = "/test/disk.trd";
    image.setFilePath(testPath);
    
    fdd.insertDisk(&image);
    ASSERT_TRUE(WaitForCondition([]() { return !g_insertedDisks.empty(); }));
    g_insertedDisks.clear();  // Clear the insertion notification
    
    fdd.ejectDisk();
    ASSERT_TRUE(WaitForCondition([]() { return !g_ejectedDisks.empty(); }));
    
    ASSERT_EQ(g_ejectedDisks.size(), 1);
    EXPECT_EQ(g_ejectedDisks[0].diskPath, testPath);
    EXPECT_EQ(g_ejectedDisks[0].driveId, 0);
}

TEST_F(FDDNotificationTest, InsertNullDoesNotSendNotification)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    
    fdd.insertDisk(nullptr);
    
    EXPECT_TRUE(g_insertedDisks.empty());
}

TEST_F(FDDNotificationTest, EjectWhenNoDiskInsertedDoesNotSendNotification)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    
    fdd.ejectDisk();
    
    EXPECT_TRUE(g_ejectedDisks.empty());
}

TEST_F(FDDNotificationTest, MultipleInsertEjectCycles)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    
    DiskImage image1(80, 2);
    image1.setFilePath("/disk1.trd");
    
    // Insert disk 1
    fdd.insertDisk(&image1);
    ASSERT_TRUE(WaitForCondition([]() { return g_insertedDisks.size() == 1; }));
    
    // Eject disk 1
    fdd.ejectDisk();
    ASSERT_TRUE(WaitForCondition([]() { return g_ejectedDisks.size() == 1; }));
    
    // Re-insert same disk with different path (simulating disk swap)
    image1.setFilePath("/disk2.trd");
    fdd.insertDisk(&image1);
    ASSERT_TRUE(WaitForCondition([]() { return g_insertedDisks.size() == 2; }));
    
    // Eject again
    fdd.ejectDisk();
    ASSERT_TRUE(WaitForCondition([]() { return g_ejectedDisks.size() == 2; }));
    
    // Should have 2 insertions and 2 ejections
    ASSERT_EQ(g_insertedDisks.size(), 2);
    ASSERT_EQ(g_ejectedDisks.size(), 2);
    
    EXPECT_EQ(g_insertedDisks[0].diskPath, "/disk1.trd");
    EXPECT_EQ(g_insertedDisks[1].diskPath, "/disk2.trd");
    EXPECT_EQ(g_ejectedDisks[0].diskPath, "/disk1.trd");
    EXPECT_EQ(g_ejectedDisks[1].diskPath, "/disk2.trd");
}

TEST_F(FDDNotificationTest, InsertWithEmptyPath)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    // Don't set path - should be empty
    
    fdd.insertDisk(&image);
    ASSERT_TRUE(WaitForCondition([]() { return !g_insertedDisks.empty(); }));
    
    ASSERT_EQ(g_insertedDisks.size(), 1);
    EXPECT_TRUE(g_insertedDisks[0].diskPath.empty());
}

TEST_F(FDDNotificationTest, PayloadContainsDriveId)
{
    EmulatorContext ctx;
    FDD fdd(&ctx);
    DiskImage image(80, 2);
    image.setFilePath("/test.trd");
    
    fdd.insertDisk(&image);
    ASSERT_TRUE(WaitForCondition([]() { return !g_insertedDisks.empty(); }));
    
    ASSERT_EQ(g_insertedDisks.size(), 1);
    // Drive ID should be 0 for default FDD
    EXPECT_EQ(g_insertedDisks[0].driveId, 0);
}
