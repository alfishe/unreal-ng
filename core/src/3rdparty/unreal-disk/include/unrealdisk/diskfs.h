#pragma once

/// @file diskfs.h
/// @brief The files on any disk, one way for every file system: the volumes of a disk, a directory's entries, a
/// file's bytes. Floppies (DiskImage): TR-DOS, +3DOS, iS-DOS / TASiS, FAT12 (Sprinter DSS, MS-DOS 720K / 1.44M).
/// Hard disks, SD and CF cards (BlockDevice): the FAT partitions of the MBR or a superfloppy (Estex DSS, NedoOS, any
/// FAT12 / 16 / 32), and iS-DOS / TASiS devices: the whole disk, or runs of cylinders as the ATM-Turbo HDD driver
/// lays them out (a descriptor at a cylinder boundary). Reading everywhere; writing, deleting and renaming files on
/// TR-DOS, +3DOS, iS-DOS and FAT (a block device that takes writes).
///
/// Standard C++ only: the emulator (its media verbs `volumes`, `files`, `read` on every automation plane, the
/// debugger's disk files) and the standalone tools (zxasm, symconv) use the same class. The caller owns the disk and
/// reads it at a coherent moment. Design: docs/emulator/design/disk-file-systems.md (in the unreal-ng repository).
///
/// Example: ISDOS50.FDI: Volumes() gives one "isdos" volume ("iS-DOS Classic", "is-DOS94"); List("", "SHELL",
/// false) the entries of SHELL; Read("", "read_me.txt") its 1641 bytes.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "unrealdisk/unicode.h"

namespace unrealdisk
{
class BlockDevice;
class DiskImage;

struct DiskFsVolume
{
    std::string id;        ///< the name to pick it: "trdos", "plus3", "isdos", "fat" (a floppy, a superfloppy), "p1".."p4"
                           ///< (an MBR partition), "isdos-<cylinder>" (an iS-DOS device on a hard disk)
    std::string fs;        ///< trdos, plus3, isdos, fat12, fat16, fat32
    std::string system;    ///< what it reads as: "TR-DOS", "+3DOS", "iS-DOS Chic", "iS-DOS Classic", "FAT16"
    std::string label;     ///< the disk title or volume label
    uint64_t offset = 0;   ///< block media: the volume's first byte on the disk
    uint64_t bytes = 0;    ///< its size
    int64_t freeBytes = -1;  ///< -1: not known
    bool writable = false;   ///< Write, Delete and Rename work on it
};

struct DiskFsEntry
{
    std::string path;      ///< from the volume's root, '/' between levels, no leading '/': "SHELL/HELP/exit.hlp"
    std::string name;      ///< the last part: TR-DOS "NAME.T", +3DOS / FAT / iS-DOS "name.ext" (UTF-8)
    bool directory = false;
    uint64_t size = 0;     ///< the file's bytes (TR-DOS: the length field, +3DOS: past its header)
    uint32_t start = 0;    ///< the load address: TR-DOS start, iS-DOS load, +3DOS CODE param 1
    std::string attributes;
    int64_t mtime = 0;     ///< UTC seconds (0: none)
    std::string error;     ///< a directory that did not open
    /// What only this file system has: TR-DOS type / sectors / track / sector / slot, iS-DOS status / first /
    /// special, +3DOS user / basicType / param2, FAT cluster / attributes
    std::vector<std::pair<std::string, int64_t>> fields;

    /// The value of field `key`, `otherwise` when the entry has none
    int64_t Field(const std::string& key, int64_t otherwise = 0) const;
};

struct DiskFsWriteOptions
{
    uint16_t start = 0;        ///< TR-DOS: the start field; +3DOS: a CODE file's load address (param 1)
    bool lengthField = true;   ///< TR-DOS: false leaves 0 in the length field (XAS reads whole sectors)
    bool header = true;        ///< +3DOS: a +3DOS header in front, as +3 BASIC's SAVE writes it
    int64_t mtime = 0;         ///< FAT, iS-DOS: the file's date and time as UTC seconds of the DOS wall clock
                               ///< (0: 1980-01-01 00:00)
};

class DiskFs
{
public:
    explicit DiskFs(DiskImage& floppy);
    /// `page`: FAT short names
    explicit DiskFs(BlockDevice& device, CodePage page = CodePage::Cp866);
    ~DiskFs();
    DiskFs(const DiskFs&) = delete;
    DiskFs& operator=(const DiskFs&) = delete;

    /// Every volume found, probed once
    const std::vector<DiskFsVolume>& Volumes();
    /// Notes of the probe and of the last listing (loops cut, stale entries)
    const std::vector<std::string>& Notes() const { return _notes; }

    /// The entries of directory `path` ("" the root) of volume `volume` ("" the first); `recursive`: the whole
    /// tree under it, a directory right before its contents
    bool List(const std::string& volume, const std::string& path, bool recursive, std::vector<DiskFsEntry>& out,
              std::string& error);
    /// The entry at `path`
    bool Stat(const std::string& volume, const std::string& path, DiskFsEntry& out, std::string& error);
    /// A file's bytes. On damage false with the reason, `data` holding what could be read. `slack`: TR-DOS the bytes
    /// of its last sector past the length (sources saved by XAS keep text there), empty elsewhere
    bool Read(const std::string& volume, const std::string& path, std::vector<uint8_t>& data, std::string& error,
              std::vector<uint8_t>* slack = nullptr);
    /// `data` as the file `path` (TR-DOS "NAME.T", +3DOS "NAME.TYP", FAT and iS-DOS "DIR/name.ext" in a directory
    /// that is there): an older file of that name is replaced. The disk is changed in place; saving it is the
    /// caller's. False on a volume that is not writable
    bool Write(const std::string& volume, const std::string& path, const std::vector<uint8_t>& data,
               const DiskFsWriteOptions& options, std::string& error);
    /// Delete the file `path` (FAT, iS-DOS: an empty directory too). TR-DOS marks it deleted as TR-DOS does (its
    /// sectors stay used until the disk is compacted)
    bool Delete(const std::string& volume, const std::string& path, std::string& error);
    /// The file `path` takes the name `to`: a bare name keeps it in its directory; FAT and iS-DOS take a path from the
    /// root too, moving it to another directory of the volume (an iS-DOS catalogue is only renamed). False when `to`
    /// is there already
    bool Rename(const std::string& volume, const std::string& path, const std::string& to, std::string& error);

    /// A file system's reader (diskfs.cpp)
    struct Driver;

private:
    void Probe();
    Driver* Find(const std::string& volume, std::string& error, size_t* index = nullptr);
    /// The driver of a writable volume, else null with the reason
    Driver* FindWritable(const std::string& volume, std::string& error);

    DiskImage* _floppy = nullptr;
    BlockDevice* _device = nullptr;
    CodePage _page = CodePage::Cp866;
    bool _probed = false;
    std::vector<DiskFsVolume> _volumes;
    std::vector<std::unique_ptr<Driver>> _drivers;
    std::vector<std::string> _notes;
};
}  // namespace unrealdisk
