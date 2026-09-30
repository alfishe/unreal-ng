#include "emulator/media/blockadvisory.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "emulator/io/storage/iblockdevice.h"

namespace
{

bool HasTag(const std::vector<std::string>& tags, const char* tag)
{
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool HasBootSignature(const std::array<uint8_t, IBlockDevice::kSectorSize>& sector)
{
    return sector[510] == 0x55 && sector[511] == 0xAA;
}

/// A FAT12/16/32 VBR: a jump opcode at byte 0, and the "FAT" ASCII marker in
/// the BPB (FAT12/16 at offset 0x36, FAT32 at offset 0x52)
bool LooksLikeFatBootSector(const std::array<uint8_t, IBlockDevice::kSectorSize>& sector)
{
    if (sector[0] != 0xEB && sector[0] != 0xE9)
        return false;
    return std::memcmp(&sector[0x36], "FAT", 3) == 0 || std::memcmp(&sector[0x52], "FAT", 3) == 0;
}

/// An MBR partition table: at least one of the four entries at 446/462/478/494
/// has a non-zero type and a non-zero sector count - deliberately not a strict
/// checksum, this is an advisory hint, not a parser
bool LooksLikePartitionTable(const std::array<uint8_t, IBlockDevice::kSectorSize>& sector)
{
    for (int i = 0; i < 4; ++i)
    {
        const uint8_t* entry = &sector[446 + i * 16];
        const uint8_t type = entry[4];
        uint32_t count = 0;
        std::memcpy(&count, entry + 12, 4);
        if (type != 0 && count != 0)
            return true;
    }
    return false;
}

}  // namespace

std::string DescribeBlockLayoutMismatch(IBlockDevice& block, const std::vector<std::string>& tags)
{
    const bool expectsPartitionTable = HasTag(tags, "ide") && HasTag(tags, "hdd");
    const bool expectsRawFat = HasTag(tags, "sd");
    if (!expectsPartitionTable && !expectsRawFat)
        return {};

    std::array<uint8_t, IBlockDevice::kSectorSize> sector{};
    if (!block.ReadSector(0, sector.data()))
        return {};

    const bool hasSignature = HasBootSignature(sector);
    const bool fatVbr = hasSignature && LooksLikeFatBootSector(sector);
    const bool partitionTable = hasSignature && !fatVbr && LooksLikePartitionTable(sector);

    if (expectsPartitionTable)
    {
        if (fatVbr)
            return "sector 0 is a FAT boot sector with no partition table (the SD card layout) - "
                   "this IDE hard-disk boot path expects an MBR partition table (e.g. built by "
                   "hddfdisk); it likely will not boot here";
        if (!hasSignature)
            return "sector 0 has no boot signature (0x55AA) - this IDE hard-disk boot path expects an "
                   "MBR partition table; it likely will not boot here";
        return {};  // a partition table (or something this heuristic cannot tell apart from one)
    }

    // expectsRawFat
    if (partitionTable)
        return "sector 0 holds an MBR partition table (the IDE hard-disk layout) - this SD boot path "
               "reads a FAT filesystem directly from sector 0, with no MBR support; it likely will not "
               "boot here";
    if (!hasSignature)
        return "sector 0 does not look like a FAT boot sector (no 0x55AA signature) - this SD boot path "
               "reads a FAT filesystem directly from sector 0; it likely will not boot here";
    return {};
}
