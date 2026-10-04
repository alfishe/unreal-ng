// HostWriteHold: the top of a write-through stack. Passes writes through, or,
// held while time travel replays history, keeps them in memory and writes them
// to the medium when released (FR-20)

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "emulator/io/storage/hostwritehold.h"
#include "emulator/io/storage/memorydisk.h"

TEST(HostWriteHold_Test, HeldWritesStayInMemoryUntilReleased)
{
    auto disk = std::make_unique<MemoryDisk>(4);
    MemoryDisk* medium = disk.get();
    HostWriteHold hold(std::move(disk));
    const std::vector<uint8_t> a(512, 0x11), b(512, 0x22);
    uint8_t sector[512];

    ASSERT_TRUE(hold.WriteSector(0, a.data()));
    EXPECT_EQ(medium->Data()[0], 0x11) << "not held: written through";

    ASSERT_TRUE(hold.SetHolding(true));
    ASSERT_TRUE(hold.WriteSector(1, b.data()));
    EXPECT_EQ(medium->Data()[512], 0x00) << "held: the medium is untouched";
    ASSERT_TRUE(hold.ReadSector(1, sector));
    EXPECT_EQ(sector[0], 0x22) << "the guest reads what it wrote";
    ASSERT_TRUE(hold.ReadSector(0, sector));
    EXPECT_EQ(sector[0], 0x11) << "unwritten sectors read from the medium";
    EXPECT_FALSE(hold.WriteSector(4, b.data())) << "beyond the end, as the medium would answer";
    EXPECT_EQ(hold.HeldSectors(), 1u);

    ASSERT_TRUE(hold.SetHolding(false));
    EXPECT_EQ(medium->Data()[512], 0x22) << "released: written to the medium";
    EXPECT_EQ(hold.HeldSectors(), 0u);
}
