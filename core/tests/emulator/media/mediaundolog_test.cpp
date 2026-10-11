#include <gtest/gtest.h>

#include <map>

#include "emulator/media/mediaundolog.h"

namespace
{
using Unit = MediaUndoLog::Unit;
using Image = MediaUndoLog::Image;

/// Sectors of one byte each, and the switch
class FakeUnits : public MediaUndoLog::IUnits
{
public:
    std::map<uint64_t, uint8_t> sectors;
    bool protect = false;

    bool Read(Unit unit, uint64_t index, Image& out) override
    {
        if (unit == Unit::Protect)
            out = protect;
        else
            out = std::vector<uint8_t>{sectors[index]};
        return true;
    }
    bool Write(Unit unit, uint64_t index, const Image& image) override
    {
        if (unit == Unit::Protect)
            protect = std::get<bool>(image);
        else
            sectors[index] = std::get<std::vector<uint8_t>>(image)[0];
        return true;
    }

    /// The guest changes a sector in @p version (the log first keeps what it was)
    void Change(MediaUndoLog& log, uint64_t version, uint64_t lba, uint8_t value)
    {
        if (!log.Has(version, Unit::Sector, lba))
        {
            Image before;
            Read(Unit::Sector, lba, before);
            log.Keep(version, version, Unit::Sector, lba, std::move(before));
        }
        sectors[lba] = value;
    }
};
}  // namespace

TEST(MediaUndoLog_Test, GoesBackToEveryVersionAndForwardAgain)
{
    MediaUndoLog log;
    FakeUnits disk;
    disk.sectors = {{1, 0x10}, {2, 0x20}};
    log.Reset(0);
    disk.Change(log, 1, 1, 0x11);
    disk.Change(log, 1, 1, 0x12);  // the same version again: nothing more kept
    disk.Change(log, 2, 2, 0x21);
    disk.Change(log, 3, 1, 0x13);
    EXPECT_EQ(log.Entries(), 3u);

    ASSERT_TRUE(log.Move(3, 1, disk));
    EXPECT_EQ(disk.sectors[1], 0x12);
    EXPECT_EQ(disk.sectors[2], 0x20);
    ASSERT_TRUE(log.Move(1, 0, disk));
    EXPECT_EQ(disk.sectors[1], 0x10);

    ASSERT_TRUE(log.Move(0, 2, disk));
    EXPECT_EQ(disk.sectors[1], 0x12);
    EXPECT_EQ(disk.sectors[2], 0x21);
    ASSERT_TRUE(log.Move(2, 3, disk));
    EXPECT_EQ(disk.sectors[1], 0x13);
}

TEST(MediaUndoLog_Test, AChangeAfterGoingBackDropsTheFuture)
{
    MediaUndoLog log;
    FakeUnits disk;
    disk.sectors = {{5, 0x50}};
    log.Reset(0);
    disk.Change(log, 1, 5, 0x51);
    disk.Change(log, 2, 5, 0x52);
    ASSERT_TRUE(log.Move(2, 1, disk));

    log.DropAbove(1);
    disk.Change(log, 2, 5, 0x5F);
    EXPECT_FALSE(log.Move(2, 3, disk)) << "the old version 3 never was";
    ASSERT_TRUE(log.Move(2, 0, disk));
    EXPECT_EQ(disk.sectors[5], 0x50);
    ASSERT_TRUE(log.Move(0, 2, disk));
    EXPECT_EQ(disk.sectors[5], 0x5F);
}

TEST(MediaUndoLog_Test, NothingBeforeTheFloor)
{
    MediaUndoLog log;
    FakeUnits disk;
    disk.sectors = {{0, 1}};
    log.Reset(4);
    disk.Change(log, 5, 0, 2);
    EXPECT_FALSE(log.Move(5, 3, disk));
    EXPECT_EQ(disk.sectors[0], 2) << "a refused move changes nothing";
    EXPECT_TRUE(log.Move(5, 4, disk));
    EXPECT_EQ(disk.sectors[0], 1);
}

TEST(MediaUndoLog_Test, PastTheBudgetTheOldestVersionsGo)
{
    constexpr size_t kBudget = 2048;  // room for a few entries
    MediaUndoLog log(kBudget);
    FakeUnits disk;
    log.Reset(0);
    for (uint64_t v = 1; v <= 40; v++)
        disk.Change(log, v, v, static_cast<uint8_t>(v));
    EXPECT_GT(log.Floor(), 0u);
    EXPECT_LE(log.Bytes(), kBudget);
    EXPECT_GT(log.Entries(), 1u);
    EXPECT_FALSE(log.Move(40, 0, disk));
    EXPECT_TRUE(log.Move(40, log.Floor(), disk));
}

TEST(MediaUndoLog_Test, AReplayMeetsTheSwitchAtItsFrame)
{
    MediaUndoLog log;
    FakeUnits disk;
    log.Reset(0);
    disk.Change(log, 1, 7, 0x70);                                // frame 1: a write
    log.Keep(2, 9, Unit::Protect, 0, Image{false}, Image{true});  // frame 9: the switch on
    disk.protect = true;
    disk.Change(log, 3, 7, 0x71);                                // frame 9: a write after it

    EXPECT_FALSE(log.SwitchAt(0, 9).has_value()) << "version 1 is a write: the replay runs it itself";
    EXPECT_FALSE(log.SwitchAt(1, 8).has_value()) << "another frame";
    ASSERT_TRUE(log.SwitchAt(1, 9).has_value());
    EXPECT_TRUE(*log.SwitchAt(1, 9));

    ASSERT_TRUE(log.Move(3, 1, disk));
    EXPECT_FALSE(disk.protect) << "back before the switch";
    ASSERT_TRUE(log.Move(1, 2, disk));
    EXPECT_TRUE(disk.protect);
}
