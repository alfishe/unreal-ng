// ExtentReader: the read hot path of synthesized volumes (multi-source tdd.md §4.1)

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <vector>

#include "_helpers/heapcounter.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/compose/extentreader.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/memorydisk.h"

namespace
{
    /// A 64-sector memory disk whose sector n is filled with byte n + 1
    std::shared_ptr<MemoryDisk> NumberedDisk()
    {
        auto disk = std::make_shared<MemoryDisk>(64);
        for (uint64_t lba = 0; lba < 64; lba++)
            std::memset(disk->Data() + lba * 512, static_cast<int>(lba + 1), 512);
        return disk;
    }

    FileData DeviceFile(uint16_t source, uint32_t firstExtent, uint32_t count, uint64_t bytes)
    {
        FileData data;
        data.storage = FileData::Storage::DeviceExtents;
        data.source = source;
        data.firstExtent = firstExtent;
        data.extentCount = count;
        data.bytes = bytes;
        return data;
    }
}  // namespace

TEST(ExtentReader_Test, SingleExtentDirect)
{
    SourcePool pool;
    const uint16_t source = pool.AddDevice(NumberedDisk());
    std::vector<Extent> extents = {{10, 4, 0}};
    ExtentReader reader(pool, extents);
    const FileData data = DeviceFile(source, 0, 1, 4 * 512);

    uint8_t buffer[512 + 16];
    std::memset(buffer, 0xCC, sizeof buffer);
    ASSERT_TRUE(reader.ReadFileSector(data, 2, buffer));
    EXPECT_EQ(buffer[0], 13) << "file sector 2 = source LBA 12";
    EXPECT_EQ(buffer[511], 13);
    for (size_t i = 512; i < sizeof buffer; i++)
        ASSERT_EQ(buffer[i], 0xCC) << "nothing written past the 512 bytes";
}

TEST(ExtentReader_Test, MultiExtentBoundaries)
{
    SourcePool pool;
    const uint16_t source = pool.AddDevice(NumberedDisk());
    // A fragmented file: sectors 0-2 at LBA 5, 3-3 at LBA 30, 4-7 at LBA 50
    std::vector<Extent> extents = {{5, 3, 0}, {30, 1, 3}, {50, 4, 4}};
    ExtentReader reader(pool, extents);
    const FileData data = DeviceFile(source, 0, 3, 8 * 512);

    const uint8_t expected[8] = {6, 7, 8, 31, 51, 52, 53, 54};
    uint8_t buffer[512];
    for (uint64_t s = 0; s < 8; s++)
    {
        ASSERT_TRUE(reader.ReadFileSector(data, s, buffer));
        EXPECT_EQ(buffer[0], expected[s]) << "file sector " << s;
    }
    // Random order too
    for (uint64_t s : {7u, 0u, 3u, 5u, 2u})
    {
        ASSERT_TRUE(reader.ReadFileSector(data, s, buffer));
        EXPECT_EQ(buffer[0], expected[s]) << "file sector " << s;
    }
}

TEST(ExtentReader_Test, PastEofZeroAndSlackZeroed)
{
    SourcePool pool;
    const uint16_t source = pool.AddDevice(NumberedDisk());
    std::vector<Extent> extents = {{10, 4, 0}};
    ExtentReader reader(pool, extents);
    const FileData data = DeviceFile(source, 0, 1, 512 + 100);  // 1.2 sectors

    uint8_t buffer[512];
    ASSERT_TRUE(reader.ReadFileSector(data, 1, buffer));
    EXPECT_EQ(buffer[99], 12) << "the file's last bytes";
    EXPECT_EQ(buffer[100], 0) << "the source's bytes past the file's end are not shown";
    EXPECT_EQ(buffer[511], 0);

    std::memset(buffer, 0xCC, sizeof buffer);
    ASSERT_TRUE(reader.ReadFileSector(data, 2, buffer));
    for (uint8_t b : buffer)
        ASSERT_EQ(b, 0) << "past the end: zeros";
}

TEST(ExtentReader_Test, SequentialUsesLastHit)
{
    SourcePool pool;
    const uint16_t source = pool.AddDevice(NumberedDisk());
    std::vector<Extent> extents;
    for (uint32_t i = 0; i < 16; i++)
        extents.push_back({i * 3, 2, i * 2});  // 16 extents of 2 sectors, gaps between them on the source
    ExtentReader reader(pool, extents);
    const FileData data = DeviceFile(source, 0, 16, 32 * 512);

    uint8_t buffer[512];
    for (uint64_t s = 0; s < 32; s++)
        ASSERT_TRUE(reader.ReadFileSector(data, s, buffer));
    EXPECT_EQ(reader.Searches(), 1u) << "only the first read searches; the rest are the same or the next extent";
}

TEST(ExtentReader_Test, NoHeapAllocationPerRead)
{
    if (!HeapCounter::Available())
        GTEST_SKIP() << "no allocator block-size query on this platform";
    ScratchFolder folder("extentreader-heap");
    const auto path = folder.File("data.bin", std::string(4096, 'x'));
    SourcePool pool;
    const uint16_t source = pool.AddDevice(NumberedDisk());
    std::vector<Extent> extents = {{0, 8, 0}};
    ExtentReader reader(pool, extents);
    const FileData device = DeviceFile(source, 0, 1, 8 * 512);
    FileData host;
    host.storage = FileData::Storage::HostFile;
    host.hostFile = pool.AddHostFile(path, 4096);
    host.bytes = 4096;
    FileData zero;
    zero.bytes = 4096;

    uint8_t buffer[512];
    reader.ReadFileSector(host, 0, buffer);  // the host stream opens here, once
    HeapCounter::Start();
    for (int round = 0; round < 1000; round++)
    {
        reader.ReadFileSector(device, round % 8, buffer);
        reader.ReadFileSector(host, round % 8, buffer);
        reader.ReadFileSector(zero, round % 8, buffer);
    }
    HeapCounter::Stop();
    EXPECT_EQ(HeapCounter::Allocations(), 0) << "reads allocate nothing";
}

TEST(ExtentReader_Test, HostFileShrankReadsZeroAndWarns)
{
    ScratchFolder folder("extentreader-shrink");
    const auto path = folder.File("data.bin", std::string(3000, 'a'));
    SourcePool pool;
    std::vector<Extent> extents;
    ExtentReader reader(pool, extents);
    FileData host;
    host.storage = FileData::Storage::HostFile;
    host.hostFile = pool.AddHostFile(path, 3000);
    host.bytes = 3000;

    std::ofstream(path, std::ios::binary | std::ios::trunc) << "tiny";
    uint8_t buffer[512];
    ASSERT_TRUE(reader.ReadFileSector(host, 0, buffer));
    EXPECT_EQ(buffer[0], 't');
    EXPECT_EQ(buffer[4], 0);
    ASSERT_TRUE(reader.ReadFileSector(host, 5, buffer));
    EXPECT_EQ(buffer[0], 0);
    ASSERT_EQ(pool.Warnings().size(), 1u) << "one warning per file";
}
