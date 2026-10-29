#pragma once

// CP/M 2.2 file systems of the ATM-Turbo (its BIOS CP/M), read and written: the directory of 32-byte entries (user
// 0-15, name 8, type 3 with the attribute bits, extent, records, the block numbers), 2 KB blocks counted from the
// directory's first one, block numbers of 16 bits when the volume has more than 256 blocks (8 per entry, 16 KB an
// extent), else of 8 bits (16 per entry, two extents an entry). A file's size is the last extent's: (EX + 32 * S2) *
// 16 KB + RC records of 128 bytes; only the pointers its records need count (the rest may be garbage).
//
// Where the volumes are:
// - a floppy: the TR-DOS geometry (80 cylinders, 2 sides, 16 sectors of 256 bytes), cylinder 0 reserved, the
//   directory (128 entries) at byte #2000 of the linear image, 316 blocks;
// - a hard disk: no partition table. Sector 0 holds the boot configuration record (C9 00 00 55 AA, then tag / length
//   / body records ending with tag 0): an ASSIGN (tag 3) gives a drive its channel descriptor, the start cylinder,
//   the reserved tracks, the size in 128-byte records and the directory entries. Further partitions are the text
//   channel descriptors (.CDS: "DRIVER = 4", "PATBEG = 6", ...) the AUTOEXEC.SUB of the boot partition assigns with
//   AS.COM. The BIOS addresses the disk by cylinder, head and sector of the descriptor's geometry.
// Layout and sources (BIOS 1.03 and 1.07.13 disassembled, the BIOS CP/M manual, ATM_HDD.HDD): written down in
// docs/emulator/design/disk-file-systems.md.
//
// Writing goes as the BDOS does: the data in the lowest free blocks (the last record padded with #1A), then the
// directory entries; an older file of the name goes in the same directory write. A read-only file (t1') is not
// deleted, written over or renamed.
//
// Recognition is strict: every used entry has user 0-15, printable name bytes (not all blank), EX <= 31, S1 = 0, RC <=
// 128, and the blocks it needs inside the volume, past the directory and used once on the whole volume; at least one
// file.
//
// Example: prince.trd (ATM CP/M floppy): AtmFloppy(disk).IsCpm() is true; List gives PRINCE.DOC and the others; Read
// gives a file's records.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace unrealdisk
{
class DiskImage;

struct CpmGeometry
{
    uint32_t blocks = 0;   ///< DSM + 1: blocks of 2 KB from the directory's first one
    uint32_t entries = 0;  ///< DRM + 1: directory entries

    static constexpr uint32_t kBlock = 2048;

    /// Block numbers of 16 bits (8 an entry) past 256 blocks, else of 8 bits (16 an entry)
    bool Wide() const
    {
        return blocks > 256;
    }
    /// The blocks the directory takes from block 0 on
    uint32_t DirectoryBlocks() const
    {
        return (entries * 32 + kBlock - 1) / kBlock;
    }
};

struct CpmFile
{
    uint8_t user = 0;
    std::string name;          ///< up to 8 characters, the high bits cleared, trailing blanks removed
    std::string type;          ///< up to 3 characters, the attribute bits cleared, trailing blanks removed
    uint64_t size = 0;         ///< the records of the last extent (CP/M keeps no byte length)
    uint8_t attributes = 0;    ///< kReadOnly (t1'), kSystem (t2'), kArchive (t3')
    uint32_t extents = 0;      ///< its directory entries
    uint32_t firstBlock = 0;   ///< the first block of the first extent (0: an empty file)
    std::vector<uint8_t> key;  ///< user and the 11 name bytes without the high bits: its entries

    static constexpr uint8_t kReadOnly = 1, kSystem = 2, kArchive = 4;

    /// NAME.TYP (NAME when there is no type)
    std::string FullName() const
    {
        return type.empty() ? name : name + "." + type;
    }
};

/// A channel descriptor of the ATM-Turbo BIOS: one drive of the configuration record or of a .CDS file
struct AtmCpmChannel
{
    char drive = 0;              ///< 'A'.. from the configuration record; 0 from a .CDS file
    std::string source;          ///< the .CDS file's name ("HDDE.CDS"), empty for the configuration record
    uint8_t driver = 0;          ///< DTYP: 3 floppy, 4 hard disk
    uint8_t unit = 0;            ///< 0 master, 1 slave
    uint32_t heads = 0;          ///< DHEADF (a hard disk)
    uint32_t sectors = 0;        ///< DSECTT: per track
    uint32_t sectorSize = 0;     ///< DBYTES
    uint32_t cylinders = 0;      ///< DCYLN / TRACKS: the partition's
    uint32_t startCylinder = 0;  ///< DBEGCYL / PATBEG
    uint32_t reserved = 0;       ///< DALTCYL / SYS: reserved tracks before the directory
    uint32_t records = 0;        ///< DBLDR: the size in 128-byte records
    uint32_t entries = 0;        ///< DDIRENT / DIR

    /// DSM = ((DBLDR - SPT * OFF) >> 4) - 1 as the BIOS computes it; blocks 0 when the descriptor makes no volume
    CpmGeometry Geometry() const;
};

class CpmFileSystem
{
public:
    /// `size` bytes from byte `offset` of the volume (block 0, the directory, at offset 0)
    using ByteReader = std::function<bool(uint64_t offset, uint8_t* dst, uint32_t size)>;
    /// `size` bytes written from byte `offset` of the volume
    using ByteWriter = std::function<bool(uint64_t offset, const uint8_t* src, uint32_t size)>;

    CpmFileSystem(ByteReader reader, CpmGeometry geometry) : _reader(std::move(reader)), _geometry(geometry) {}

    /// The ATM CP/M floppy layout on a disk image (no check: IsCpm tells)
    static CpmFileSystem AtmFloppy(DiskImage& image);

    /// The Profi MicroDOS floppy layout (80 x 2 x 5 x 1024, the directory from cylinder 0, 128 entries, 400
    /// blocks) on a disk image (no check: IsCpm tells)
    static CpmFileSystem ProfiFloppy(DiskImage& image);

    /// Klug CP/M 2.3's layout of the same disk (two system cylinders, the directory from cylinder 2, 128 entries,
    /// 390 blocks) on a disk image (no check: IsCpm tells)
    static CpmFileSystem KlugFloppy(DiskImage& image);

    /// The ASSIGN records of a hard disk's sector 0 (512 bytes); false when it holds no configuration record
    static bool ParseAtmConfig(const uint8_t* sector, std::vector<AtmCpmChannel>& out);

    /// A text channel descriptor ("FIELD = value" lines, ';' comments); false when a line does not read, the driver is
    /// missing or a hard disk misses one of its fields (SECTORS, TRACKS, HEADSF, DIR, PATBEG)
    static bool ParseCds(const std::string& text, AtmCpmChannel& out);

    /// The directory reads and passes the checks above; `why` the first failure. `empty`: a directory without files
    /// passes too (a partition table found the volume)
    bool IsCpm(std::string* why = nullptr, bool empty = false) const;

    /// The files in directory order of their first entry; false with the reason when the directory does not read
    bool List(std::vector<CpmFile>& out, std::string& error) const;

    /// The file's records (`size` bytes); a block that does not read fails
    bool Read(const CpmFile& file, std::vector<uint8_t>& data, std::string& error) const;

    /// The blocks no entry uses (the directory's taken)
    bool FreeBlocks(uint32_t& out) const;

    /// Writes go through `writer`; without one the volume is read only
    void SetWriter(ByteWriter writer)
    {
        _writer = std::move(writer);
    }

    /// `bytes` as the file NAME.TYP of `user` (0-15), an older file of that name replaced. `name` and `type` as they
    /// go into the directory (ValidName)
    bool Write(uint8_t user, const std::string& name, const std::string& type, const std::vector<uint8_t>& bytes,
               std::string& error);
    /// Every entry of the file marked #E5
    bool Delete(const CpmFile& file, std::string& error);
    /// The file's entries take `user` and NAME.TYP (the attribute bits stay); false when that file is there
    bool Rename(const CpmFile& file, uint8_t user, const std::string& name, const std::string& type,
                std::string& error);
    /// kReadOnly / kSystem / kArchive on every entry of the file
    bool SetAttributes(const CpmFile& file, uint8_t attributes, std::string& error);
    /// 1-8 and 0-3 printable characters, none of < > . , ; : = ? * [ ] | / \ or a blank, no lower case
    static bool ValidName(const std::string& name, const std::string& type);

    const CpmGeometry& Geometry() const
    {
        return _geometry;
    }

private:
    bool Directory(std::vector<uint8_t>& out) const;
    /// The pointers entry `e` holds valid data in (its records need)
    uint32_t UsedPointers(const uint8_t* e) const;
    uint32_t Pointer(const uint8_t* e, uint32_t k) const;
    bool Writable(std::string& error) const;
    /// The entries of `after` that differ from `before`
    bool WriteDirectory(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after, std::string& error);
    /// The live entries of `key`; false with `error` when none is
    bool Entries(const std::vector<uint8_t>& dir, const std::vector<uint8_t>& key, std::vector<uint32_t>& out,
                 std::string& error) const;

    ByteReader _reader;
    ByteWriter _writer;
    CpmGeometry _geometry;
};
}  // namespace unrealdisk
