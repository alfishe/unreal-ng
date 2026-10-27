// GuestProgramWatch: a run of sectors read inside a program file on a block medium names the file the guest is
// loading (NC_GUEST_PROGRAM), so a game profile can find a title started from NextZXOS's Browser

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/fatsourceimage.h"
#include "_helpers/testwaithelper.h"
#include "emulator/media/guestprogramwatch.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

namespace
{
    std::vector<uint8_t> Bytes(size_t size, uint8_t seed)
    {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; i++)
            data[i] = static_cast<uint8_t>(seed + i * 7 + i / 512);
        return data;
    }

    constexpr uint64_t kFloppyDataStart = 33;  ///< Fat12Floppy: 1 reserved + 2 x 9 FAT + 14 root sectors

    /// The files named by NC_GUEST_PROGRAM while it lives
    struct Listener
    {
        MessageCenter& mc = MessageCenter::DefaultMessageCenter();
        std::atomic<int> count{0};
        std::mutex mutex;
        std::vector<std::string> paths;
        std::vector<std::vector<uint8_t>> data;
        uint64_t id = mc.AddObserver(NC_GUEST_PROGRAM, [this](int, Message* message) {
            auto* payload = message ? dynamic_cast<GuestProgramPayload*>(message->obj) : nullptr;
            if (!payload)
                return;
            {
                std::lock_guard<std::mutex> lock(mutex);
                paths.push_back(payload->path);
                data.push_back(payload->data);
            }
            count++;
        });
        ~Listener() { mc.RemoveObserverById(NC_GUEST_PROGRAM, id); }
    };

    void Read(GuestProgramWatch& watch, IBlockDevice& disk, uint64_t first, uint32_t sectors)
    {
        uint8_t sector[512];
        for (uint32_t i = 0; i < sectors; i++)
        {
            ASSERT_TRUE(disk.ReadSector(first + i, sector));
            watch.OnSectorRead("sd.zc", first + i, disk);
        }
    }
}  // namespace

TEST(GuestProgramWatch_Test, ProgramNames)
{
    EXPECT_TRUE(GuestProgramWatch::IsProgramName("Game.NEX"));
    EXPECT_TRUE(GuestProgramWatch::IsProgramName("ds_next.snx"));
    EXPECT_TRUE(GuestProgramWatch::IsProgramName("a.b.tap"));
    EXPECT_FALSE(GuestProgramWatch::IsProgramName("module.bin"));
    EXPECT_FALSE(GuestProgramWatch::IsProgramName("readme"));
}

/// Sixteen sectors in a row inside GAME.NEX name it, once, with its bytes
TEST(GuestProgramWatch_Test, ARunInsideAProgramFileIsReported)
{
    const std::vector<uint8_t> game = Bytes(40 * 512, 3);
    auto disk = Fat12Floppy({{"README.TXT", Bytes(2 * 512, 1)}, {"GAME.NEX", game}});
    GuestProgramWatch watch(nullptr);
    Listener listener;

    Read(watch, *disk, kFloppyDataStart + 2, 40);  // README takes cluster 2..3, GAME.NEX starts at sector +2
    ASSERT_TRUE(TestWait::ForExactly(listener.count, 1));
    std::lock_guard<std::mutex> lock(listener.mutex);
    EXPECT_EQ(listener.paths.front(), "/GAME.NEX");
    EXPECT_EQ(listener.data.front(), game);
}

/// The FAT and the directory are read in long runs too, and a peek at a header is short: none of them is a load
TEST(GuestProgramWatch_Test, MetadataAndShortReadsAreNot)
{
    auto disk = Fat12Floppy({{"GAME.NEX", Bytes(40 * 512, 3)}, {"NOTES.TXT", Bytes(30 * 512, 5)}});
    GuestProgramWatch watch(nullptr);
    Listener listener;

    Read(watch, *disk, 0, 33);                      // boot sector, both FATs, the root directory
    Read(watch, *disk, kFloppyDataStart, 15);       // fifteen sectors of GAME.NEX
    Read(watch, *disk, kFloppyDataStart + 40, 30);  // a long run in a file that is no program
    ASSERT_TRUE(TestWait::DrainMessageCenter(MessageCenter::DefaultMessageCenter()));
    EXPECT_EQ(listener.count.load(), 0);
}

/// A loader that peeks at the file and reads it again does not report the load twice; another file does report
TEST(GuestProgramWatch_Test, TheSameFileTwiceInARowIsOneLoad)
{
    auto disk = Fat12Floppy({{"ONE.TAP", Bytes(20 * 512, 3)}, {"TWO.TAP", Bytes(20 * 512, 9)}});
    GuestProgramWatch watch(nullptr);
    Listener listener;

    Read(watch, *disk, kFloppyDataStart, 20);
    Read(watch, *disk, kFloppyDataStart, 20);
    Read(watch, *disk, kFloppyDataStart + 20, 20);
    ASSERT_TRUE(TestWait::ForExactly(listener.count, 2));
    std::lock_guard<std::mutex> lock(listener.mutex);
    EXPECT_EQ(listener.paths[0], "/ONE.TAP");
    EXPECT_EQ(listener.paths[1], "/TWO.TAP");
}
