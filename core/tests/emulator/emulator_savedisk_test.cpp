#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include <common/filehelper.h>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/testpathhelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "loaders/disk/loader_udi.h"

/// Emulator::SaveDisk - format by extension, UDI re-target when the strict format refuses
/// (docs/inprogress/2026-09-02-universal-track-model/loader-registry.md, section 4)

class EmulatorSaveDisk_Test : public ::testing::Test
{
protected:
    std::shared_ptr<Emulator> _emulator;

    void SetUp() override
    {
        EmulatorManager* manager = EmulatorManager::GetInstance();
        _emulator = manager->CreateEmulator("", LoggerLevel::LogError);  // Initialised by the manager
        ASSERT_NE(_emulator, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorManager::GetInstance()->RemoveEmulator(_emulator->GetId());
            _emulator.reset();
        }
    }

    static void removeFile(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    static std::vector<uint8_t> readFile(const std::string& path)
    {
        std::vector<uint8_t> result;
        if (FileHelper::FileExists(path))
        {
            size_t size = FileHelper::GetFileSize(path);
            result.resize(size);
            if (size > 0) FileHelper::ReadFileToBuffer(path, result.data(), size);
        }
        return result;
    }

    /// Copy a fixture into the scratch directory so the test can save over it
    static std::string scratchCopy(const char* fixture, const char* name)
    {
        std::string target = TestPathHelper::GetTestScratchPath(name);
        removeFile(target);
        std::error_code ec;
        std::filesystem::copy_file(TestPathHelper::GetTestDataPath(fixture), target, ec);
        return target;
    }
};

TEST_F(EmulatorSaveDisk_Test, Save_Trd_InPlace)
{
    std::string trd = scratchCopy("loaders/trd/EyeAche.trd", "savedisk.trd");
    ASSERT_TRUE(_emulator->LoadDisk(trd));

    DiskImage* image = _emulator->GetContext()->coreState.diskImages[0];
    ASSERT_NE(image, nullptr);
    uint8_t data[256];
    std::memset(data, 0x5A, sizeof(data));
    image->getTrack(3)->writeSectorData(2, data, 256);
    EXPECT_TRUE(image->isDirty());

    Emulator::DiskSaveResult result = _emulator->SaveDisk(0);
    EXPECT_TRUE(result.saved) << result.reason;
    EXPECT_FALSE(result.retargeted);
    EXPECT_EQ(result.savedPath, trd);
    EXPECT_FALSE(image->isDirty());

    std::vector<uint8_t> file = readFile(trd);
    ASSERT_EQ(file.size(), 655360u);
    EXPECT_EQ(file[3 * 4096 + 2 * 256], 0x5A);

    removeFile(trd);
}

TEST_F(EmulatorSaveDisk_Test, Save_Trd_RefusedByGeometry_RetargetsToUdi)
{
    std::string trd = scratchCopy("loaders/trd/EyeAche.trd", "retarget.trd");
    std::string udi = TestPathHelper::GetTestScratchPath("retarget.udi");
    removeFile(udi);
    std::vector<uint8_t> original = readFile(trd);
    ASSERT_TRUE(_emulator->LoadDisk(trd));

    // A +3 track makes the image unrepresentable in TRD
    DiskImage* image = _emulator->GetContext()->coreState.diskImages[0];
    image->getTrackForCylinderAndSide(10, 0)->formatTrack(10, 0, DiskImage::TrackFormatSpec::plus3());

    // Observe the re-target notification
    std::atomic<int> notifications{0};
    std::string notifiedPath, notifiedReason;
    std::mutex notifyMutex;
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    ObserverCallbackFunc callback = [&](int, Message* message)
    {
        auto* payload = dynamic_cast<FDDDiskPayload*>(message->obj);
        if (payload) {
            std::lock_guard<std::mutex> lock(notifyMutex);
            notifications++;
            notifiedPath = payload->_diskPath;
            notifiedReason = payload->_reason;
        }
    };
    uint64_t callbackId = messageCenter.AddObserver(NC_FDD_DISK_SAVE_RETARGETED, callback);

    Emulator::DiskSaveResult result = _emulator->SaveDisk(0);

    // Wait for async notification to be delivered (MessageCenter dispatches asynchronously)
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (notifications.load() == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(250));
    }

    messageCenter.RemoveObserverById(NC_FDD_DISK_SAVE_RETARGETED, callbackId);

    EXPECT_TRUE(result.saved) << result.reason;
    EXPECT_TRUE(result.retargeted);
    EXPECT_EQ(result.savedPath, udi);
    EXPECT_NE(result.reason.find("cylinder 10"), std::string::npos) << result.reason;
    EXPECT_EQ(image->getFilePath(), udi);
    EXPECT_EQ(_emulator->GetContext()->coreState.diskFilePaths[0], udi);
    EXPECT_FALSE(image->isDirty());

    // Original untouched, UDI holds the +3 track
    EXPECT_EQ(readFile(trd), original);
    LoaderUDI back(nullptr, udi);
    ASSERT_TRUE(back.loadImage());
    EXPECT_EQ(back.getImage()->getTrackForCylinderAndSide(10, 0)->sectorCount(), 9u);
    delete back.getImage();

    EXPECT_EQ(notifications.load(), 1);
    {
        std::lock_guard<std::mutex> lock(notifyMutex);
        EXPECT_EQ(notifiedPath, udi);
        EXPECT_FALSE(notifiedReason.empty());
    }

    // Retarget disabled: plain failure, nothing written
    removeFile(udi);
    result = _emulator->SaveDisk(0, trd, false);
    EXPECT_FALSE(result.saved);
    EXPECT_FALSE(result.retargeted);
    EXPECT_FALSE(FileHelper::FileExists(udi));

    removeFile(trd);
    removeFile(udi);
}

TEST_F(EmulatorSaveDisk_Test, Save_As_ByExtension)
{
    std::string trd = scratchCopy("loaders/trd/EyeAche.trd", "saveas.trd");
    ASSERT_TRUE(_emulator->LoadDisk(trd));

    std::string udi = TestPathHelper::GetTestScratchPath("saveas.udi");
    std::string fdi = TestPathHelper::GetTestScratchPath("saveas.fdi");
    removeFile(udi);
    removeFile(fdi);

    Emulator::DiskSaveResult result = _emulator->SaveDisk(0, udi);
    EXPECT_TRUE(result.saved) << result.reason;
    EXPECT_TRUE(FileHelper::FileExists(udi));
    EXPECT_EQ(_emulator->GetContext()->coreState.diskImages[0]->getFilePath(), udi);

    result = _emulator->SaveDisk(0, fdi);
    EXPECT_TRUE(result.saved) << result.reason;
    EXPECT_TRUE(FileHelper::FileExists(fdi));

    EXPECT_FALSE(_emulator->SaveDisk(1).saved) << "no disk in drive B";
    EXPECT_FALSE(_emulator->SaveDisk(7).saved);

    removeFile(trd);
    removeFile(udi);
    removeFile(fdi);
}

/// Emulator::LoadDisk selects the loader by extension for every implemented format
TEST_F(EmulatorSaveDisk_Test, LoadDisk_ByExtension)
{
    struct Case { const char* fixture; uint8_t cylinders; uint8_t sides; size_t track0Sectors; };
    const Case cases[] =
    {
        { "loaders/trd/EyeAche.trd", 80, 2, 16 },
        { "loaders/scl/eyeache2.scl", 80, 2, 16 },
        { "loaders/udi/beta128-empty.udi", 86, 2, 16 },
        { "loaders/fdi/VORON1.FDI", 81, 2, 1 },   // track 0 of this image carries a single 1024-byte sector
        { "loaders/dsk/plus3-blank.dsk", 40, 1, 9 },
        { "loaders/td0/trdos-sample.td0", 80, 2, 16 },
        { "loaders/mgt/synthetic.mgt", 80, 2, 10 },
        { "loaders/mgt/synthetic.img", 80, 2, 10 },
    };

    for (const Case& c : cases)
    {
        std::string path = TestPathHelper::GetTestDataPath(c.fixture);
        ASSERT_TRUE(_emulator->LoadDisk(path)) << c.fixture;

        DiskImage* image = _emulator->GetContext()->coreState.diskImages[0];
        ASSERT_NE(image, nullptr) << c.fixture;
        EXPECT_EQ(image->getCylinders(), c.cylinders) << c.fixture;
        EXPECT_EQ(image->getSides(), c.sides) << c.fixture;
        EXPECT_EQ(image->getTrack(0)->sectorCount(), c.track0Sectors) << c.fixture;
        EXPECT_EQ(image->getFilePath(), path) << c.fixture;
        EXPECT_EQ(_emulator->GetContext()->coreState.diskFilePaths[0], path) << c.fixture;
        EXPECT_EQ(_emulator->GetContext()->coreState.diskDrives[0]->getDiskImage(), image) << c.fixture;
    }

    EXPECT_FALSE(_emulator->LoadDisk(TestPathHelper::GetTestDataPath("loaders/sna/action.sna"))) << "not a disk";
}
