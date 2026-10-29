#pragma once

/// @file mb01fs.h
/// @brief MB-01 (BS-DOS 52..105, Busy soft 1991-1992) floppies, read only: the boot sector, the FAT, the one list
/// of directories and the one list of files of the whole disk.
///
/// The layout is Busy's description ("Format diskety MB-01" in mb02form.htm); no MB-01 disk or BS-DOS 52-105
/// binary is known to survive (two MB-01 units were built), so it is not checked against a real disk. 1024-byte
/// sectors, both sides, LSN = 2 * SPT * track + SPT * side + (sector - 1) as on MB-02. The boot sector (#80 at 2,
/// #01 at 3) and the backup sector after it may be anywhere on track 0, side 0. The FAT holds a 16-bit word a sector:
/// bit 15 a deleted sector, bits 12-13 the kind (0 normal, 1 system, 2 bad, 3 past the FAT's end), bit 14 clear the
/// next LSN in bits 0-11, set the bytes of the chain's last sector; 0 is free. The two lists are chains of system
/// sectors from boot bytes 24 and 25: 16-byte directory entries (parent, MS-DOS time and date, valid, name), the
/// number of a directory its slot (0 the root), and 32-byte file entries (directory, MS-DOS time and date, the ID
/// byte: bit 7 deleted, 6 body, 5 header, 3-0 the type; the rest of the tape header, body address, 24-bit body
/// length, body flag, attributes, the first body LSN). Assumed where the description is silent: little-endian
/// fields, time before date (as MB-02), a FAT of several sectors chained in itself or else consecutive, a list
/// ending with its chain.
/// Layout: docs/emulator/design/disk-file-systems.md (MB-01 layout, in the unreal-ng repository).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "unrealdisk/mb02fs.h"

namespace unrealdisk
{
class DiskImage;

struct Mb01Boot
{
    uint32_t lsn = 0;         ///< where the boot sector is (its id - 1 on track 0, side 0)
    uint16_t tracks = 0;      ///< 14: cylinders
    uint16_t sectors = 0;     ///< 15: sectors a track (each side)
    uint16_t dirs = 0;        ///< 24: the first LSN of the directory list
    uint16_t files = 0;       ///< 25: the first LSN of the file list
    uint16_t fatSectors = 0;  ///< 26: 1..4
    uint32_t fat1 = 0;        ///< 27: the first FAT1 sector
    uint32_t fat2 = 0;        ///< 31: the first FAT2 sector
    uint16_t date = 0;        ///< 18: MS-DOS date of the format
    uint16_t time = 0;        ///< 16
    std::string name;         ///< 4: the disk name, trailing blanks and zeroes cut

    uint32_t Sectors() const
    {
        return static_cast<uint32_t>(tracks) * 2 * sectors;
    }
};

class Mb01FileSystem
{
public:
    /// The 1024 bytes of logical sector `lsn`
    using SectorReader = std::function<bool(uint32_t lsn, uint8_t* dst)>;

    static constexpr uint32_t kSector = 1024;

    explicit Mb01FileSystem(SectorReader reader, uint32_t bootLsn = 0) : _reader(std::move(reader)), _bootLsn(bootLsn)
    {
    }

    /// The disk image's sectors (track LSN / (2 * SPT), side LSN / SPT & 1, id LSN % SPT + 1); the boot sector the
    /// first one of track 0, side 0 marked #80 #01 at 2, SPT from it (none found: Open fails)
    static Mb01FileSystem OnDisk(DiskImage& image);

    /// Reads the boot sector and the FAT (FAT2 where a FAT1 sector does not read); false with `why` when the disk is
    /// no MB-01 one: the marks, 40 or 80 tracks of 1..22 sectors, a FAT of 1..4 sectors covering the disk, the lists
    /// and the FAT inside it, the boot sector and its backup system sectors in the FAT
    bool Open(std::string* why = nullptr);

    const Mb01Boot& Boot() const
    {
        return _boot;
    }

    /// The valid directories of the directory list, the root (0) first; `parent` from byte 0
    bool Directories(std::vector<Mb02Directory>& out, std::string& error) const;

    /// The valid files of directory `number`, in list order (`entry` the slot in the file list). `flags` keeps the
    /// MB-01 ID byte; Mb02File's kValid / kBody / kHeader do not apply
    bool List(uint8_t number, std::vector<Mb02File>& out, std::string& error) const;

    /// The file's `length` body bytes along its FAT chain; a chain that ends early, loops or leaves the disk fails
    bool Read(const Mb02File& file, std::vector<uint8_t>& data, std::string& error) const;

    /// The sectors the FAT marks free
    uint32_t FreeSectors() const;

    static constexpr uint8_t kDeleted = 0x80, kBody = 0x40, kHeader = 0x20;

private:
    /// The sectors of the chain from `first` and their bytes, the last one's count cut
    bool ChainBytes(uint32_t first, std::vector<uint8_t>& out, std::string& error) const;
    uint16_t Fat(uint32_t lsn) const;

    SectorReader _reader;
    uint32_t _bootLsn = 0;
    Mb01Boot _boot;
    std::vector<uint8_t> _fat;
};
}  // namespace unrealdisk
