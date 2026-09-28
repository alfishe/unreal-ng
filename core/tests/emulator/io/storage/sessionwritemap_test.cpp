// SessionWriteMap: guest writes kept in memory over any medium (IDE design
// §12.3 "Session write map")

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/io/storage/memorydisk.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/storage/sessionwritemap.h"

namespace
{
    /// A read-only medium: sector s filled with (s + 1)
    std::unique_ptr<MemoryDisk> MakeBase(uint64_t sectors)
    {
        auto disk = std::make_unique<MemoryDisk>(sectors);
        for (uint64_t s = 0; s < sectors; s++)
            std::fill_n(disk->Data() + s * 512, 512, static_cast<uint8_t>(s + 1));
        disk->SetWritable(false);
        return disk;
    }
}  // namespace

TEST(SessionWriteMap_Test, WritesReadBackAndNeverReachTheMedium)
{
    auto base = MakeBase(4);
    MemoryDisk* medium = base.get();
    SessionWriteMap map(std::move(base));

    EXPECT_TRUE(map.IsWritable()) << "writable even over a read-only medium";
    const std::vector<uint8_t> data(512, 0xC3);
    ASSERT_TRUE(map.WriteSector(2, data.data()));
    EXPECT_EQ(map.ChangedSectors(), 1u);

    uint8_t sector[512];
    ASSERT_TRUE(map.ReadSector(2, sector));
    EXPECT_EQ(sector[10], 0xC3);
    ASSERT_TRUE(map.ReadSector(1, sector));
    EXPECT_EQ(sector[10], 2) << "unchanged sectors come from the medium";
    EXPECT_EQ(medium->Data()[2 * 512 + 10], 3) << "the medium never changes";

    EXPECT_FALSE(map.WriteSector(4, data.data())) << "past the end";
}

TEST(SessionWriteMap_Test, WritingTheOriginalBackFreesTheEntry)
{
    SessionWriteMap map(MakeBase(4));
    const std::vector<uint8_t> changed(512, 0xEE);
    const std::vector<uint8_t> original(512, 2);  // sector 1

    ASSERT_TRUE(map.WriteSector(1, changed.data()));
    EXPECT_EQ(map.ChangedSectors(), 1u);
    ASSERT_TRUE(map.WriteSector(1, original.data()));
    EXPECT_EQ(map.ChangedSectors(), 0u);

    ASSERT_TRUE(map.WriteSector(0, std::vector<uint8_t>(512, 1).data()));
    EXPECT_EQ(map.ChangedSectors(), 0u) << "rewriting unchanged data costs nothing";
}

TEST(SessionWriteMap_Test, DiscardAndContentId)
{
    SessionWriteMap map(MakeBase(4));
    const uint64_t pristine = map.ContentId();
    ASSERT_TRUE(map.WriteSector(3, std::vector<uint8_t>(512, 9).data()));
    EXPECT_NE(map.ContentId(), pristine) << "a change moves the id";

    map.Discard();
    EXPECT_EQ(map.ChangedSectors(), 0u);
    EXPECT_EQ(map.ContentId(), pristine);
    uint8_t sector[512];
    ASSERT_TRUE(map.ReadSector(3, sector));
    EXPECT_EQ(sector[0], 4);
}

TEST(SessionWriteMap_Test, ExportIsMediumPlusChangesByteExact)
{
    SessionWriteMap map(MakeBase(3));
    ASSERT_TRUE(map.WriteSector(1, std::vector<uint8_t>(512, 0xAB).data()));

    const std::string path = TestPathHelper::GetUniqueTestScratchPath("session-export.img");
    ASSERT_TRUE(map.ExportTo(path));

    auto exported = RawImage::Open(path, RawImage::Access::ReadOnly);
    ASSERT_NE(exported, nullptr);
    ASSERT_EQ(exported->SectorCount(), 3u);
    uint8_t sector[512];
    ASSERT_TRUE(exported->ReadSector(0, sector));
    EXPECT_EQ(sector[0], 1);
    ASSERT_TRUE(exported->ReadSector(1, sector));
    EXPECT_EQ(sector[511], 0xAB);
    ASSERT_TRUE(exported->ReadSector(2, sector));
    EXPECT_EQ(sector[0], 3);
    exported.reset();
    std::remove(path.c_str());
}
