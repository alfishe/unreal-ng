#pragma once

/// @file mdosfs.h
/// @brief MDOS (Didaktik D40 / D80, MDOS 1 / 2, the MDOS3 virtual disks of divIDE), read only: the boot sector, the
/// 12-bit FAT, the flat directory of 128 files and their bodies; the slots of an MDOS3 partition.
///
/// 512-byte sectors addressed by LSN = (cylinder * sides + side) * SPT + (sector - 1). The boot sector (LSN 0) has
/// "SDOS" at 204, the disk name at 192, the geometry at #B1..#B3. The FAT (LSN 1-5) holds a 12-bit word a sector, 341
/// a FAT sector (#000 free, #DDD reserved, #DFF bad, #C00 an empty file, #E00 + n the last sector with n bytes, #E00
/// a full one, else the next LSN). The directory (LSN 6-13) is 128 entries of 32 bytes: the type letter, the name,
/// the length, the start, the second parameter, the first LSN, the attributes HSPARWED. Data from LSN 14 on.
/// Layout: docs/emulator/design/disk-file-systems.md (MDOS layout, in the unreal-ng repository).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace unrealdisk
{
class DiskImage;

struct MdosBoot
{
    uint8_t cylinders = 0;  ///< #B2
    uint8_t sides = 0;      ///< bit 4 of #B1: 2
    uint8_t sectors = 0;    ///< #B3: a track
    std::string name;       ///< 192-201, zeroes cut ("bootdisk")

    uint32_t Sectors() const
    {
        return static_cast<uint32_t>(cylinders) * sides * sectors;
    }
};

struct MdosFile
{
    uint32_t slot = 0;       ///< its directory entry (0..127)
    char type = 0;           ///< 'P' program, 'B' bytes, 'N' / 'C' number / character array, 'S' snapshot, 'Q'
                             ///< sequential
    std::string name;        ///< 1-10, zeroes cut
    uint32_t length = 0;     ///< 11-12 and 21: 24 bits
    uint16_t start = 0;      ///< 13: the load address, a program's autostart line
    uint16_t param2 = 0;     ///< 15: a program's length without its variables
    uint16_t first = 0;      ///< 17: the first sector
    uint8_t extra = 0;       ///< 19: the flag byte of a headless file (type 'H', the tape emulator's)
    uint8_t attributes = 0;  ///< 20: H S P A R W E D, bit 7 to bit 0
};

/// A virtual disk of an MDOS3 partition: 1693 sectors a slot, an info sector ("SDOS", the slot's name) and the disk
struct Mdos3Slot
{
    uint32_t index = 0;
    uint64_t firstSector = 0;  ///< the disk's LSN 0 on the device (the info sector before it)
    std::string name;          ///< info 4-35, blanks cut
};

class MdosFileSystem
{
public:
    /// The 512 bytes of logical sector `lsn`
    using SectorReader = std::function<bool(uint32_t lsn, uint8_t* dst)>;

    static constexpr uint32_t kSector = 512;
    static constexpr uint32_t kSlotSectors = 1693;  ///< an MDOS3 slot: the info sector and a 94 x 2 x 9 disk
    static constexpr uint8_t kMaxCylinders = 94;    ///< MDOS3's enlarged disks

    explicit MdosFileSystem(SectorReader reader) : _reader(std::move(reader)) {}

    /// The disk image's sectors, SPT and sides from the boot sector (no check: Open tells)
    static MdosFileSystem OnDisk(DiskImage& image);

    /// The slots of an MDOS3 partition of `sectors` sectors whose info sectors read "SDOS" (`read` gives a sector of
    /// the partition)
    static std::vector<Mdos3Slot> Slots(const std::function<bool(uint64_t, uint8_t*)>& read, uint64_t sectors);

    /// Reads the boot sector and the FAT; false with `why` when the disk is no MDOS one: "SDOS" at 204, a geometry
    /// (1..94 cylinders, 1..18 sectors) of at least 14 sectors, the FAT reserving the boot sector, itself and the
    /// directory (#DDD for LSN 0-13)
    bool Open(std::string* why = nullptr);

    const MdosBoot& Boot() const
    {
        return _boot;
    }

    /// The used directory entries in slot order (type byte neither 0 nor #E5)
    bool List(std::vector<MdosFile>& out, std::string& error) const;

    /// The file's `length` bytes along its FAT chain; a chain that ends early, loops or leaves the disk fails
    bool Read(const MdosFile& file, std::vector<uint8_t>& data, std::string& error) const;

    /// The sectors the FAT marks free
    uint32_t FreeSectors() const;

    /// The FAT word of `lsn` (#DDD past the FAT)
    uint16_t Fat(uint32_t lsn) const;

private:
    SectorReader _reader;
    MdosBoot _boot;
    std::vector<uint8_t> _fat;  ///< LSN 1-5
};
}  // namespace unrealdisk
