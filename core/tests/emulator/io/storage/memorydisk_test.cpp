// MemoryDisk: a zero-filled in-memory disk

#include <gtest/gtest.h>

#include <vector>

#include "emulator/io/storage/memorydisk.h"

TEST(MemoryDisk_Test, ZeroFilledReadWriteAndBounds)
{
    MemoryDisk disk(8);
    EXPECT_EQ(disk.SectorCount(), 8u);
    EXPECT_TRUE(disk.IsWritable());

    uint8_t sector[512];
    ASSERT_TRUE(disk.ReadSector(7, sector));
    EXPECT_EQ(sector[0], 0);
    EXPECT_EQ(sector[511], 0);
    EXPECT_FALSE(disk.ReadSector(8, sector));

    const std::vector<uint8_t> data(512, 0x5A);
    ASSERT_TRUE(disk.WriteSector(3, data.data()));
    ASSERT_TRUE(disk.ReadSector(3, sector));
    EXPECT_EQ(sector[200], 0x5A);
    EXPECT_EQ(disk.Data()[3 * 512 + 100], 0x5A) << "direct access sees the write";
    EXPECT_FALSE(disk.WriteSector(8, data.data()));
}

TEST(MemoryDisk_Test, ReadOnlyAndDistinctIds)
{
    MemoryDisk a(2, /*writable*/ false);
    MemoryDisk b(2);
    const std::vector<uint8_t> data(512, 1);
    EXPECT_FALSE(a.WriteSector(0, data.data()));
    EXPECT_NE(a.ContentId(), b.ContentId()) << "every memory disk is its own medium";
}
