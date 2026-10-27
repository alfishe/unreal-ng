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

/// A FAT partition type an MBR entry can carry
bool IsFatPartitionType(uint8_t type)
{
    return type == 0x01 || type == 0x04 || type == 0x06 || type == 0x0B || type == 0x0C || type == 0x0E;
}

/// The flavour of a FAT VBR: FAT12/16 carry a non-zero root-directory entry
/// count in the BPB, FAT32 leaves it zero (its root is a cluster chain).
/// FAT12 reports as "unknown" - no such media is built or read here
std::optional<FatType> ClassifyFatVbr(const std::array<uint8_t, IBlockDevice::kSectorSize>& sector)
{
    if (!LooksLikeFatBootSector(sector))
        return std::nullopt;
    uint16_t rootEntries = 0;
    std::memcpy(&rootEntries, &sector[17], 2);
    uint16_t totalSectors16 = 0;
    std::memcpy(&totalSectors16, &sector[19], 2);
    uint32_t totalSectors = totalSectors16;
    if (totalSectors == 0)
        std::memcpy(&totalSectors, &sector[32], 4);  // a volume over 32 MiB keeps its size in the 32-bit field
    uint8_t sectorsPerCluster = sector[13];
    if (rootEntries != 0 && sectorsPerCluster != 0 && totalSectors != 0)
    {
        // FAT12 (under 4085 clusters) and FAT16: the FAT16 family, the one a slot's fsCompatibility names
        // (a FAT32-only slot refuses both; the composites count FAT12 so too)
        return FatType::Fat16;
    }
    if (rootEntries == 0 && sectorsPerCluster != 0)
        return FatType::Fat32;
    return std::nullopt;
}

}  // namespace

SectorZeroLayout ClassifySectorZero(const uint8_t* sector, size_t size)
{
    if (!sector || size < IBlockDevice::kSectorSize)
        return SectorZeroLayout::Unknown;
    std::array<uint8_t, IBlockDevice::kSectorSize> copy{};
    std::memcpy(copy.data(), sector, copy.size());
    if (!HasBootSignature(copy))
        return SectorZeroLayout::Unknown;
    if (LooksLikeFatBootSector(copy))
        return SectorZeroLayout::FatVolume;
    if (LooksLikePartitionTable(copy))
        return SectorZeroLayout::PartitionTable;
    return SectorZeroLayout::Unknown;
}

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

std::vector<FatVolumeProbe> ProbeFatVolumes(IBlockDevice& block)
{
    std::vector<FatVolumeProbe> volumes;
    std::array<uint8_t, IBlockDevice::kSectorSize> sector{};
    if (!block.ReadSector(0, sector.data()) || !HasBootSignature(sector))
        return volumes;
    if (LooksLikeFatBootSector(sector))
    {
        if (std::optional<FatType> fs = ClassifyFatVbr(sector))
            volumes.push_back({0, *fs});
        return volumes;
    }
    if (!LooksLikePartitionTable(sector))
        return volumes;

    auto probe = [&block, &volumes](uint64_t start, uint32_t partition) {
        std::array<uint8_t, IBlockDevice::kSectorSize> vbr{};
        if (start == 0 || start >= block.SectorCount() || !block.ReadSector(start, vbr.data()) || !HasBootSignature(vbr))
            return;
        if (std::optional<FatType> fs = ClassifyFatVbr(vbr))
            volumes.push_back({partition, *fs});
    };
    uint32_t extended = 0;
    for (int i = 0; i < 4; ++i)
    {
        const uint8_t* entry = &sector[446 + i * 16];
        uint32_t start = 0;
        std::memcpy(&start, entry + 8, 4);
        if (IsFatPartitionType(entry[4]))
            probe(start, static_cast<uint32_t>(i + 1));
        else if ((entry[4] == 0x05 || entry[4] == 0x0F) && extended == 0)
            extended = start;
    }
    // Logical partitions: each EBR holds one partition (relative to the EBR) and the next EBR (relative to the
    // extended partition); a loop or a damaged chain ends the walk
    uint64_t ebr = extended;
    for (uint32_t logical = 5; ebr != 0 && logical < 5 + 128; logical++)
    {
        std::array<uint8_t, IBlockDevice::kSectorSize> table{};
        if (ebr >= block.SectorCount() || !block.ReadSector(ebr, table.data()) || !HasBootSignature(table))
            break;
        uint32_t relative = 0;
        std::memcpy(&relative, &table[446 + 8], 4);
        if (IsFatPartitionType(table[446 + 4]))
            probe(ebr + relative, logical);
        uint32_t next = 0;
        std::memcpy(&next, &table[446 + 16 + 8], 4);
        const uint8_t nextType = table[446 + 16 + 4];
        const uint64_t following = (nextType == 0x05 || nextType == 0x0F) && next != 0 ? uint64_t(extended) + next : 0;
        if (following <= ebr)
            break;  // the chain only goes forward
        ebr = following;
    }
    return volumes;
}

std::optional<FatType> ProbeFatType(IBlockDevice& block)
{
    std::array<uint8_t, IBlockDevice::kSectorSize> sector{};
    if (!block.ReadSector(0, sector.data()) || !HasBootSignature(sector))
        return std::nullopt;

    if (LooksLikeFatBootSector(sector))
        return ClassifyFatVbr(sector);

    if (!LooksLikePartitionTable(sector))
        return std::nullopt;
    for (int i = 0; i < 4; ++i)
    {
        const uint8_t* entry = &sector[446 + i * 16];
        if (!IsFatPartitionType(entry[4]))
            continue;
        uint32_t start = 0;
        std::memcpy(&start, entry + 8, 4);
        std::array<uint8_t, IBlockDevice::kSectorSize> vbr{};
        if (start == 0 || !block.ReadSector(start, vbr.data()) || !HasBootSignature(vbr))
            continue;
        if (std::optional<FatType> fs = ClassifyFatVbr(vbr))
            return fs;
    }
    return std::nullopt;
}
