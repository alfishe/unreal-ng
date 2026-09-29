// ReadOnlyGuard: reads pass through, writes fail, the medium never changes

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/readonlyguard.h"

TEST(ReadOnlyGuard_Test, ReadsPassWritesFail)
{
    auto disk = std::make_unique<MemoryDisk>(4);
    disk->Data()[512 + 7] = 0x99;
    MemoryDisk* medium = disk.get();
    ReadOnlyGuard guard(std::move(disk));

    EXPECT_EQ(guard.SectorCount(), 4u);
    EXPECT_FALSE(guard.IsWritable());
    uint8_t sector[512];
    ASSERT_TRUE(guard.ReadSector(1, sector));
    EXPECT_EQ(sector[7], 0x99);

    const std::vector<uint8_t> data(512, 0x11);
    EXPECT_FALSE(guard.WriteSector(1, data.data()));
    EXPECT_EQ(medium->Data()[512], 0x00) << "the medium is untouched";
    EXPECT_EQ(guard.ContentId(), medium->ContentId());
}
