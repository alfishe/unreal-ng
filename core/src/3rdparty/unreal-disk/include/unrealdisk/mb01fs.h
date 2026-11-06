#pragma once

/// @file mb01fs.h
/// @brief MB-01 (BS-DOS 52..105, Busy soft 1991-1992) floppies, read and written: the boot sector, the FAT, the one
/// list of directories and the one list of files of the whole disk.
///
/// The layout is Busy's description ("Format diskety MB-01" in mb02form.htm). 1024-byte sectors, both sides, LSN = 2 * SPT * track + SPT * side + (sector - 1) as on MB-02. The boot sector (#80 at 2,
/// #01 at 3) and the backup sector after it may be anywhere on track 0, side 0. The FAT holds a 16-bit word a sector:
/// bit 15 a deleted sector, bits 12-13 the kind (0 normal, 1 system, 2 bad, 3 past the FAT's end), bit 14 clear the
/// next LSN in bits 0-11, set the bytes of the chain's last sector; 0 is free. The two lists are chains of system
/// sectors from boot bytes 24 and 25: 16-byte directory entries (parent, MS-DOS time and date, valid, name), the
/// number of a directory its slot (0 the root), and 32-byte file entries (directory, MS-DOS time and date, the ID
/// byte: bit 7 deleted, 6 body, 5 header, 3-0 the type; the rest of the tape header, body address, 24-bit body
/// length, body flag, attributes, the first body LSN). Assumed where the description is silent: little-endian
/// fields, time before date (as MB-02), a FAT of several sectors chained in itself or else consecutive, a list
/// ending with its chain.
/// Writing (with a SectorWriter) follows MB-02's rules where the description is silent: both FAT copies kept the
/// same, the boot and list sectors written to the backup sector first, a new entry in the first free slot (a deleted
/// one or one with neither header nor body), a full list grown by a system sector at the end of its chain, new
/// sectors the free ones (word 0) from LSN 1 up, freed ones back to 0.
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
    using SectorWriter = std::function<bool(uint32_t lsn, const uint8_t* src)>;
    using WriteOptions = Mb02FileSystem::WriteOptions;

    static constexpr uint32_t kSector = 1024;

    explicit Mb01FileSystem(SectorReader reader, uint32_t bootLsn = 0) : _reader(std::move(reader)), _bootLsn(bootLsn)
    {
    }

    /// The disk image's sectors (track LSN / (2 * SPT), side LSN / SPT & 1, id LSN % SPT + 1); the boot sector the
    /// first one of track 0, side 0 marked #80 #01 at 2, SPT from it (none found: Open fails)
    static Mb01FileSystem OnDisk(DiskImage& image);

    /// Writes go through `writer` (without one the file system is read only)
    void SetWriter(SectorWriter writer)
    {
        _writer = std::move(writer);
    }

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

    /// `bytes` as a file `name` (1-10 bytes; the tape header's name, blanks after it) in directory `dir`; an older
    /// file of the name there goes first and leaves its slot to the new one
    bool Write(uint8_t dir, const std::string& name, const std::vector<uint8_t>& bytes, const WriteOptions& options,
               std::string& error);
    /// The entry marked deleted (bit 7 of the ID byte) and its body's sectors freed
    bool Delete(const Mb02File& file, std::string& error);
    /// Another name (a file with a header), in directory `dir` (byte 0 of the entry); false when a file of the name
    /// is there
    bool Rename(const Mb02File& file, uint8_t dir, const std::string& name, std::string& error);
    bool SetTime(const Mb02File& file, uint16_t date, uint16_t time, std::string& error);
    /// A new directory under `parent` in the first invalid slot (1..255) of the directory list
    bool MakeDirectory(uint8_t parent, const std::string& name, uint16_t date, uint16_t time, std::string& error);
    /// An empty directory (no files, no directories under it) made an invalid slot
    bool DeleteDirectory(uint8_t number, std::string& error);
    /// The directory's name and parent (bytes 6 and 0 of its slot)
    bool RenameDirectory(uint8_t number, uint8_t parent, const std::string& name, std::string& error);
    bool SetDirectoryTime(uint8_t number, uint16_t date, uint16_t time, std::string& error);
    /// The disk name (4, 10 bytes, blanks after it)
    bool SetName(const std::string& name, std::string& error);

    static constexpr uint8_t kDeleted = 0x80, kBody = 0x40, kHeader = 0x20;

private:
    /// The sectors of the chain from `first` and their bytes, the last one's count cut; `sectors` its LSNs
    bool ChainBytes(uint32_t first, std::vector<uint8_t>& out, std::string& error,
                    std::vector<uint32_t>* sectors = nullptr) const;
    uint16_t Fat(uint32_t lsn) const;

    bool Writable(std::string& error) const;
    void SetFat(uint32_t lsn, uint16_t value);
    /// Both FAT copies, each at the sectors Open found it
    bool WriteFat(std::string& error);
    /// A boot or list sector: the backup sector first, then its place
    bool WriteSystem(uint32_t lsn, const uint8_t* data, std::string& error);
    /// `count` free sectors from LSN 1 up, marked in the FAT in memory as a chain ending with `last` bytes
    bool Allocate(uint32_t count, uint32_t last, std::vector<uint32_t>& out, std::string& error);
    void FreeChain(uint32_t first);
    /// The bytes of a list (`first` its first sector) and its sectors
    bool ListBytes(uint32_t first, std::vector<uint32_t>& chain, std::vector<uint8_t>& bytes,
                   std::string& error) const;
    /// A new system sector of zeroes written at the end of the list's chain (the chain and the bytes grow, the FAT in
    /// memory too)
    bool Grow(std::vector<uint32_t>& chain, std::vector<uint8_t>& bytes, std::string& error);
    /// The list sector holding bytes `at` written back
    bool WriteAt(const std::vector<uint32_t>& chain, const std::vector<uint8_t>& bytes, size_t at,
                 std::string& error);

    SectorReader _reader;
    SectorWriter _writer;
    uint32_t _bootLsn = 0;
    Mb01Boot _boot;
    std::vector<uint8_t> _fat;
    std::vector<uint32_t> _fatAt[2];  ///< the sectors of FAT1 and FAT2
};
}  // namespace unrealdisk
