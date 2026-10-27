// NFR-P3 (multi-source test-and-benchmark-plan.md §5.5): a sector read of a composite allocates nothing, whatever the
// composite is built as (a folder rebuild, an image-source rebuild, a graft, a partitioned disk, an ISO target) and
// with a session over it. Every sector is read once first (host files open their streams then), then again while
// the heap is counted.

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/fatsourceimage.h"
#include "_helpers/heapcounter.h"
#include "_helpers/scratchfolder.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/compositemediumfactory.h"

namespace
{
    int64_t AllocationsOfASecondRead(IBlockDevice& device)
    {
        const uint64_t sectors = std::min<uint64_t>(device.SectorCount(), 8192);
        uint8_t buffer[512];
        for (uint64_t lba = 0; lba < sectors; lba++)
            device.ReadSector(lba, buffer);
        HeapCounter::Start();
        for (uint64_t lba = 0; lba < sectors; lba++)
            device.ReadSector(lba, buffer);
        HeapCounter::Stop();
        return HeapCounter::Allocations();
    }
}  // namespace

TEST(ComposeReadAllocations_Test, NoAllocationPerSectorRead)
{
    if (!HeapCounter::Available())
        GTEST_SKIP() << "no allocator block-size query on this platform";
    ScratchFolder folder("compose-read-alloc");
    folder.File("files/README.TXT", "readme");
    folder.File("files/GAMES/ELITE.TRD", std::string(20000, 'e'));
    folder.File("up/NEW.BIN", std::string(5000, 'n'));
    {
        ScratchFolder base("compose-read-alloc-base");
        base.File("DOS.SYS", std::string(3000, 'd'));
        base.File("SUB/A.TXT", "a");
        const FatSourceImage image = FolderToFatDisk(base.Path(), FatType::Fat16);
        ASSERT_TRUE(image.ok()) << image.error;
        ASSERT_TRUE(SaveSparse(image, folder.Path() / "base.img"));
    }
    const std::vector<std::pair<const char*, std::string>> cases = {
        {"folder rebuild", "version: 1\ntarget: {build: rebuild, free: 1MiB}\nlayers: [{source: {folder: files}}, {source: {folder: up}}]\n"},
        {"image rebuild", "version: 1\ntarget: {build: rebuild, free: 1MiB}\nlayers: [{source: {image: base.img}}, {source: {folder: up}}]\n"},
        {"graft", "version: 1\ntarget: {build: graft}\nlayers: [{source: {image: base.img}}, {source: {folder: up}}]\n"},
        {"partitions", "version: 1\npartitions:\n  - {name: dos, source: {image: base.img, partition: 1}}\n"
                       "  - {name: data, fs: fat16, compose: {free: 1MiB, layers: [{source: {folder: files}}]}}\n"},
        {"iso target", "version: 1\ntarget: {kind: optical}\nlayers: [{source: {folder: files}}, {source: {folder: up}}]\n"},
    };
    for (const auto& [name, yaml] : cases)
    {
        folder.File("disk.ucompose.yaml", yaml);
        std::unique_ptr<IBlockDevice> volume;
        CompositeInfo info;
        const MediaResult built = CompositeMediumFactory::Build(ComposeDescriptor::Load(folder.Path() / "disk.ucompose.yaml"), {},
                                                                volume, info);
        ASSERT_TRUE(built.Ok()) << name << ": " << built.message;
        EXPECT_EQ(AllocationsOfASecondRead(*volume), 0) << name;

        // A session over it, with a changed sector: hits and misses alike
        SessionWriteMap session(std::move(volume));
        const uint8_t sector[512] = {1};
        ASSERT_TRUE(session.WriteSector(100, sector));
        EXPECT_EQ(AllocationsOfASecondRead(session), 0) << name << " with a session";
    }
}
