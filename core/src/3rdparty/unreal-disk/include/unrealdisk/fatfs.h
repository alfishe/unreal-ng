#pragma once

/// @file fatfs.h
/// @brief A FAT12 / FAT16 / FAT32 volume on a BlockDevice as a file system that reads and writes: directory entries
/// with their places on the disk, a file's bytes at any offset, and the changes a DOS makes - create (long names with
/// their generated short name), write and extend, truncate, delete, rename or move, make and remove directories.
/// Every change goes straight to the device's sectors (a session layer above an image or a host folder keeps them),
/// both FAT copies updated; a FAT32 FSInfo is marked "free count unknown" at the first change, which every reader
/// accepts. FatVolumeReader finds the volume; this class does the rest from the FAT specification.
///
/// The caller holds no handles here: a FatNode is a directory entry as read, and any call takes it again, so a
/// DOS emulation (the Next's esxDOS API) keeps only paths and positions and opens the volume per call.

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "unrealdisk/fatvolumereader.h"
#include "unrealdisk/unicode.h"

namespace unrealdisk
{
class BlockDevice;

/// A directory entry and where it is stored
struct FatNode
{
    std::string name;       ///< the long name when there is one, else the short name (UTF-8)
    std::string shortName;  ///< "NAME.EXT" as stored (UTF-8)
    uint8_t attributes = 0;
    uint32_t firstCluster = 0;
    uint32_t size = 0;
    uint16_t date = 0;  ///< DOS date of the last write
    uint16_t time = 0;  ///< DOS time of the last write
    bool root = false;  ///< the root directory (no entry of its own)
    uint64_t entryLba = 0;     ///< the sector of the short entry (device LBA)
    uint16_t entryOffset = 0;  ///< its byte offset there
    std::vector<std::pair<uint64_t, uint16_t>> longSlots;  ///< the places of its long-name slots

    static constexpr uint8_t kReadOnly = 0x01, kHidden = 0x02, kSystem = 0x04, kVolume = 0x08, kDirectory = 0x10,
                             kArchive = 0x20;
    bool IsDirectory() const { return root || (attributes & kDirectory) != 0; }
};

class FatFs
{
public:
    enum class Error : uint8_t
    {
        None,
        NotFound,
        Exists,
        NoSpace,
        ReadOnly,     ///< the device does not take writes, or the entry has the read-only attribute
        Io,
        InvalidName,
        NotEmpty,
        NotDirectory,
        IsDirectory,
        Damaged,      ///< a chain that leaves the volume or loops
    };

    /// `page`: the code page of short names
    explicit FatFs(BlockDevice& device, CodePage page = CodePage::Cp866);

    /// Find the volume (FatVolumeReader: a superfloppy or the first FAT partition)
    bool Open(std::string* error = nullptr);
    bool Writable() const;
    FatReaderType Type() const { return _type; }
    uint32_t ClusterBytes() const { return _sectorsPerCluster * 512u; }
    uint32_t ClusterCount() const { return _clusterCount; }
    Error LastError() const { return _error; }

    /// The DOS date and time new and written entries get (a deterministic clock is the caller's: the emulated RTC)
    void SetClock(uint16_t date, uint16_t time)
    {
        _date = date;
        _time = time;
    }

    FatNode Root() const;
    /// The entry `name` (long or short, case-insensitive) of directory `dir`
    bool Find(const FatNode& dir, const std::string& name, FatNode& out);
    /// "/" or "/GAMES/SUB/FILE.TXT" ('/' or '\\' between parts) from the root
    bool Resolve(const std::string& path, FatNode& out);
    /// The entry stored at `lba` / `offset` again (an open file kept as its place): its contents, not its long name.
    /// NotFound when the entry was deleted since
    bool NodeAt(uint64_t lba, uint16_t offset, FatNode& out);
    /// The entries of `dir` in stored order: "." and "..", deleted entries and the volume label left out
    bool List(const FatNode& dir, std::vector<FatNode>& out);

    /// Up to `count` bytes of `file` from `offset` into `dst`; the bytes read (fewer at the end of the file)
    size_t Read(const FatNode& file, uint32_t offset, uint8_t* dst, size_t count);
    /// `count` bytes at `offset` (a gap past the end is zero-filled): the file grows as needed and its entry takes the
    /// size, the first cluster and the clock. `written` says how many went in when the volume fills up
    bool Write(FatNode& file, uint32_t offset, const uint8_t* src, size_t count, size_t& written);
    /// Cut the file to `size` bytes or extend it with zeros
    bool Resize(FatNode& file, uint32_t size);

    /// A new empty file, or a directory (with its "." and ".." entries), named `name` in `dir`
    bool Create(const FatNode& dir, const std::string& name, bool directory, FatNode& out);
    /// Delete a file, or an empty directory
    bool Remove(const FatNode& node);
    /// The entry moves to `dir` under `name` (same contents); a moved directory's ".." follows
    bool Rename(const FatNode& node, const FatNode& dir, const std::string& name, FatNode& out);
    /// Change the attribute bits (read-only, hidden, system, archive); the directory bit stays
    bool SetAttributes(FatNode& node, uint8_t attributes);
    /// The DOS date and time of the last write (the access date follows)
    bool SetTime(FatNode& node, uint16_t date, uint16_t time);
    /// The volume label: the root directory's label entry (made in a free slot, or removed when `label` is empty)
    /// and the boot sector's copy (a FAT32 backup boot sector's too). Up to 11 characters of a short name or blanks,
    /// upper-cased; InvalidName otherwise, NoSpace when the root has no free slot
    bool SetLabel(const std::string& label);

    /// The free clusters (a FAT scan)
    uint32_t FreeClusters();
    /// Where the file's clusters lie: runs of consecutive sectors (device LBA, sector count) in file order, a run per
    /// stretch of consecutive clusters (the Next's DISK_FILEMAP hands them to a streaming player)
    bool Extents(const FatNode& file, std::vector<std::pair<uint64_t, uint32_t>>& runs);

    /// The 11-byte short name for `name` when it is a valid 8.3 name as it stands ("README.TXT", "readme.txt" with
    /// the lower-case flags in `caseFlags`); false when it needs a long name
    static bool ShortNameOf(const std::string& name, uint8_t out[11], uint8_t& caseFlags);
    /// Whether `name` may name a FAT entry (long-name rules: no control characters, none of "*/:<>?\|, not empty,
    /// not only dots)
    static bool ValidName(const std::string& name);

private:
    bool Fail(Error error)
    {
        _error = error;
        return false;
    }
    bool ReadSector(uint64_t lba, uint8_t* dst);
    bool WriteSector(uint64_t lba, const uint8_t* src);
    uint64_t ClusterLba(uint32_t cluster) const;
    uint32_t FatGet(uint32_t cluster);
    bool FatSet(uint32_t cluster, uint32_t value);
    bool IsEnd(uint32_t value) const;
    uint32_t EndMark() const;
    bool Chain(uint32_t first, std::vector<uint32_t>& clusters);
    bool Allocate(uint32_t& cluster);
    bool ZeroCluster(uint32_t cluster);
    bool FreeChain(uint32_t first);
    /// The sectors a directory occupies, in order
    bool DirectorySectors(const FatNode& dir, std::vector<uint64_t>& sectors);
    bool ExtendDirectory(const FatNode& dir, std::vector<uint64_t>& sectors);
    bool StoreEntry(const FatNode& node);
    bool AddEntry(const FatNode& dir, const std::string& name, const uint8_t* shortEntry, FatNode& out);
    void MarkFsInfo();
    static uint32_t DirCluster(const FatNode& dir) { return dir.root ? 0 : dir.firstCluster; }

    BlockDevice& _device;
    CodePage _page;
    Error _error = Error::None;
    FatReaderType _type = FatReaderType::Fat16;
    uint64_t _volumeStart = 0;
    uint32_t _sectorsPerCluster = 1;
    uint32_t _reservedSectors = 0;
    uint32_t _fats = 2;
    uint32_t _fatSectors = 0;
    uint32_t _rootDirSectors = 0;
    uint32_t _rootCluster = 0;
    uint32_t _clusterCount = 0;
    uint64_t _dataStart = 0;  ///< device LBA of cluster 2
    uint32_t _fsInfoSector = 0;
    bool _fsInfoMarked = false;
    uint32_t _allocHint = 2;
    uint16_t _date = 0x21, _time = 0;  ///< 1980-01-01 00:00
    uint64_t _cacheLba = UINT64_MAX;
    uint8_t _cache[512] = {};
};
}  // namespace unrealdisk
