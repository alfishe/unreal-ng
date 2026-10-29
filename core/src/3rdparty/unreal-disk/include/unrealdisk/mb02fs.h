#pragma once

/// @file mb02fs.h
/// @brief MB-02 (BS-DOS 199..308) floppies, read and written: the boot sector, the FAT of logical sectors, the
/// directory tree of the DIRS sector, the files of a directory and their bodies.
///
/// 1024-byte sectors addressed by LSN = 2 * SPT * track + SPT * side + (sector - 1); one sector a cluster. The FAT
/// holds a 16-bit word a sector (bit 15 + bit 14: the next LSN in bits 0-13; bit 15 alone: the chain's last sector
/// with its byte count in bits 0-13; high byte #FF: reserved or bad; bit 15 clear: free). The DIRS sector lists up
/// to 256 directories (#80, name XOR, first LSN); a directory is a chain of 32-byte entries, entry 0 its header
/// (parent number at #05, name at #06), the rest files: flags, MS-DOS date, the 17-byte tape header, body address,
/// 32-bit body length, body flag, attributes, first body LSN.
/// Writing (with a SectorWriter) keeps both FAT copies the same and writes the boot, DIRS and directory sectors to
/// the backup sector (LSN 1) first, as BS-DOS does; new sectors are the free ones from LSN 2 up.
/// Layout: docs/emulator/design/disk-file-systems.md (MB-02 layout, in the unreal-ng repository).

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace unrealdisk
{
class DiskImage;

struct Mb02Boot
{
    uint16_t tracks = 0;      ///< #04: cylinders
    uint16_t sectors = 0;     ///< #06: sectors a track (each side)
    uint16_t dirs = 0;        ///< #0C: the LSN of the DIRS sector
    uint16_t fatSectors = 0;  ///< #0E: 1..4
    uint16_t fat1 = 0;        ///< #12: the first FAT1 sector
    uint16_t fat2 = 0;        ///< #14: the first FAT2 sector
    uint16_t date = 0;        ///< #23: MS-DOS date of the format
    uint16_t time = 0;        ///< #21
    std::string name;         ///< #26: the disk name, trailing blanks and zeroes cut ("System 308")

    uint32_t Sectors() const
    {
        return static_cast<uint32_t>(tracks) * 2 * sectors;
    }
};

struct Mb02Directory
{
    uint8_t number = 0;  ///< its DIRS slot (0: the root, "System dir")
    uint8_t parent = 0;  ///< header #05
    std::string name;    ///< header #06-0F, trailing blanks and zeroes cut
    uint16_t first = 0;  ///< its first sector
    uint16_t date = 0;   ///< header #03: MS-DOS date (0: none)
    uint16_t time = 0;   ///< header #01
};

struct Mb02File
{
    uint8_t directory = 0;      ///< the DIRS number it is in
    uint32_t entry = 0;         ///< its slot in the directory (1..: entry 0 is the header)
    uint8_t flags = 0;          ///< #00: kValid, kSpecial, kBody, kHeader
    uint16_t date = 0;          ///< #03: MS-DOS date (0: none)
    uint16_t time = 0;          ///< #01
    uint8_t type = 0;           ///< #05: the tape header's type (0 BASIC, 3 CODE...)
    std::string name;           ///< #06-0F, trailing blanks cut (empty without a header)
    uint16_t headerLength = 0;  ///< #10
    uint16_t param1 = 0;        ///< #12: CODE start, BASIC autostart line
    uint16_t param2 = 0;        ///< #14
    uint16_t address = 0;       ///< #16: where the body loads
    uint32_t length = 0;        ///< #18: body bytes
    uint8_t bodyFlag = 0;       ///< #1C: the body block's flag byte (#FF)
    uint8_t attributes = 0;     ///< #1D (bits not documented)
    uint16_t first = 0;         ///< #1E: the first body sector (bits 0-13)

    static constexpr uint8_t kValid = 0x80, kSpecial = 0x40, kBody = 0x20, kHeader = 0x10;
};

class Mb02FileSystem
{
public:
    /// The 1024 bytes of logical sector `lsn`
    using SectorReader = std::function<bool(uint32_t lsn, uint8_t* dst)>;
    using SectorWriter = std::function<bool(uint32_t lsn, const uint8_t* src)>;

    static constexpr uint32_t kSector = 1024;

    struct WriteOptions
    {
        uint16_t start = 0;      ///< param 1: CODE's start and body address, a program's autostart line
        uint8_t type = 3;        ///< the header's type: 3 CODE, 0 a program (its body at 23755, as BS-DOS saves one)
        uint16_t param2 = 0x8000;  ///< a program's length without variables
        uint16_t date = 0;   ///< MS-DOS date and time (0: none)
        uint16_t time = 0;
    };

    explicit Mb02FileSystem(SectorReader reader) : _reader(std::move(reader)) {}

    /// The disk image's sectors: track LSN / (2 * SPT), side LSN / SPT & 1, id LSN % SPT + 1, SPT from the boot
    /// sector (no check: Open tells)
    static Mb02FileSystem OnDisk(DiskImage& image);

    /// Writes go through `writer` (without one the file system is read only)
    void SetWriter(SectorWriter writer)
    {
        _writer = std::move(writer);
    }

    /// Reads the boot sector and the FAT (FAT2 where a FAT1 sector does not read); false with `why` when the disk is
    /// no MB-02 one: the boot marks (JR at #00, #02 at #03, 2 sides, 1 sector a cluster, #00 at #20 and #25), a FAT
    /// of 1..4 sectors covering the disk with the boot and its backup reserved (#FF00 at LSN 1), the DIRS sector
    /// inside the disk with the root directory valid
    bool Open(std::string* why = nullptr);

    const Mb02Boot& Boot() const
    {
        return _boot;
    }

    /// The valid directories of the DIRS sector, by number
    bool Directories(std::vector<Mb02Directory>& out, std::string& error) const;

    /// The valid files of directory `number`, in entry order
    bool List(uint8_t number, std::vector<Mb02File>& out, std::string& error) const;

    /// The file's `length` body bytes along its FAT chain; a chain that ends early, loops or leaves the disk fails
    bool Read(const Mb02File& file, std::vector<uint8_t>& data, std::string& error) const;

    /// The sectors the FAT marks free
    uint32_t FreeSectors() const;

    /// `bytes` as a CODE file `name` (1-10 bytes; the tape header's name, blanks after it) in directory `dir`; an
    /// older file of the name there goes first. The directory grows by a sector when it is full
    bool Write(uint8_t dir, const std::string& name, const std::vector<uint8_t>& bytes, const WriteOptions& options,
               std::string& error);
    /// The entry marked deleted (bit 7 of #00 clear) and its body's sectors freed
    bool Delete(const Mb02File& file, std::string& error);
    /// Another name (a file with a header), in directory `dir` (the entry moves when it is another one); false when
    /// a file of the name is there
    bool Rename(const Mb02File& file, uint8_t dir, const std::string& name, std::string& error);
    bool SetTime(const Mb02File& file, uint16_t date, uint16_t time, std::string& error);
    /// A new empty directory under `parent` in the first free DIRS slot (1..255): one sector, its header entry
    bool MakeDirectory(uint8_t parent, const std::string& name, uint16_t date, uint16_t time, std::string& error);
    /// An empty directory (no files, no directories under it) out of the DIRS sector, its sectors freed
    bool DeleteDirectory(uint8_t number, std::string& error);
    /// The directory's name and parent (header #05, #06; the DIRS name XOR follows)
    bool RenameDirectory(uint8_t number, uint8_t parent, const std::string& name, std::string& error);
    bool SetDirectoryTime(uint8_t number, uint16_t date, uint16_t time, std::string& error);
    /// The disk name (#26, 10 bytes, blanks after it)
    bool SetName(const std::string& name, std::string& error);

private:
    /// The sectors of the chain from `first`; `last` the bytes used in its last one
    bool Chain(uint16_t first, std::vector<uint16_t>& out, uint32_t& last, std::string& error) const;
    bool Sectors(const std::vector<uint16_t>& chain, std::vector<uint8_t>& out, std::string& error) const;
    uint16_t Fat(uint32_t lsn) const;

    bool Writable(std::string& error) const;
    void SetFat(uint32_t lsn, uint16_t value);
    /// Both FAT copies
    bool WriteFat(std::string& error);
    /// A boot, DIRS or directory sector: the backup sector first, then its place
    bool WriteSystem(uint32_t lsn, const uint8_t* data, std::string& error);
    /// `count` free sectors from LSN 2 up (marked in the FAT in memory as a chain ending with `last` bytes)
    bool Allocate(uint32_t count, uint32_t last, std::vector<uint16_t>& out, std::string& error);
    void FreeChain(uint16_t first);
    /// The directory's sectors and their bytes
    bool DirectoryBytes(uint8_t number, std::vector<uint16_t>& chain, std::vector<uint8_t>& bytes,
                        std::string& error) const;
    /// A free entry (1..) of the directory's bytes, a new sector at the end of its chain when there is none (the
    /// chain and the bytes grow, the FAT in memory too)
    bool FreeEntry(std::vector<uint16_t>& chain, std::vector<uint8_t>& bytes, uint32_t& entry, std::string& error);
    /// The directory sector holding `entry` written back
    bool WriteEntry(const std::vector<uint16_t>& chain, const std::vector<uint8_t>& bytes, uint32_t entry,
                    std::string& error);
    static bool ValidName(const std::string& name, std::string& error);

    SectorReader _reader;
    SectorWriter _writer;
    Mb02Boot _boot;
    std::vector<uint8_t> _fat;
};
}  // namespace unrealdisk
