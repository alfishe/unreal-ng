// DescribeBlockLayoutMismatch: sector-0 heuristic distinguishing an MBR-partitioned hard-disk
// image from a raw FAT-at-sector-0 SD card image (docs/inprogress/2026-09-29-media-drop-targets/
// design.md §10). Advisory only - never fails the insert, so these tests check the returned
// string is empty (no concern) or non-empty (a mismatch note), not its exact wording.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "emulator/io/storage/memorydisk.h"
#include "emulator/media/blockadvisory.h"

namespace
{

const std::vector<std::string> kIdeHddTags = {"ide", "master", "nemo-divide", "hdd"};
const std::vector<std::string> kSdTags = {"sd", "zcontroller", "primary", "boot"};

void WriteFatBootSector(MemoryDisk& disk, bool fat32)
{
    uint8_t* s = disk.Data();
    s[0] = 0xEB;  // jmp opcode
    s[1] = 0x3C;
    s[2] = 0x90;
    if (fat32)
        std::memcpy(s + 0x52, "FAT32   ", 8);
    else
        std::memcpy(s + 0x36, "FAT16   ", 8);
    s[510] = 0x55;
    s[511] = 0xAA;
}

void WriteMbr(MemoryDisk& disk)
{
    uint8_t* s = disk.Data();
    s[0] = 0xFA;  // boot code, not a FAT jump opcode
    uint8_t* entry = s + 446;
    entry[4] = 0x0B;               // FAT32 partition type
    uint32_t startLba = 128, count = 122880;
    std::memcpy(entry + 8, &startLba, 4);
    std::memcpy(entry + 12, &count, 4);
    s[510] = 0x55;
    s[511] = 0xAA;
}

}  // namespace

TEST(BlockAdvisory_Test, IdeHddSlot_AcceptsMbr)
{
    MemoryDisk disk(204800);
    WriteMbr(disk);
    EXPECT_TRUE(DescribeBlockLayoutMismatch(disk, kIdeHddTags).empty());
}

TEST(BlockAdvisory_Test, IdeHddSlot_FlagsRawFatImage)
{
    MemoryDisk disk(204800);
    WriteFatBootSector(disk, /*fat32*/ true);
    const std::string note = DescribeBlockLayoutMismatch(disk, kIdeHddTags);
    EXPECT_FALSE(note.empty());
}

TEST(BlockAdvisory_Test, IdeHddSlot_FlagsNoBootSignature)
{
    MemoryDisk disk(204800);  // zero-filled: no 0x55AA
    const std::string note = DescribeBlockLayoutMismatch(disk, kIdeHddTags);
    EXPECT_FALSE(note.empty());
}

TEST(BlockAdvisory_Test, SdSlot_AcceptsRawFat)
{
    MemoryDisk disk(204800);
    WriteFatBootSector(disk, /*fat32*/ true);
    EXPECT_TRUE(DescribeBlockLayoutMismatch(disk, kSdTags).empty());
}

TEST(BlockAdvisory_Test, SdSlot_FlagsMbrImage)
{
    MemoryDisk disk(204800);
    WriteMbr(disk);
    const std::string note = DescribeBlockLayoutMismatch(disk, kSdTags);
    EXPECT_FALSE(note.empty());
}

TEST(BlockAdvisory_Test, UnrelatedSlot_NeverChecked)
{
    MemoryDisk disk(204800);  // zero-filled, no signature at all
    EXPECT_TRUE(DescribeBlockLayoutMismatch(disk, {"floppy", "a"}).empty());
}
