#pragma once

/// @file diskfs.h
/// @brief The files on any disk, one way for every file system: the volumes of a disk, a directory's entries, a
/// file's bytes. Floppies (DiskImage): TR-DOS, +3DOS, iS-DOS / TASiS, ATM CP/M, MB-02, MB-01 (read only), MDOS, FAT12
/// (Sprinter DSS, MS-DOS 720K / 1.44M). Hard disks, SD and CF cards (BlockDevice): the FAT partitions of the MBR or a
/// superfloppy (Estex DSS, NedoOS, any FAT12 / 16 / 32), the ATM CP/M partitions (the configuration record in sector 0,
/// the .CDS descriptors, else a directory at a track start, read only), iS-DOS / TASiS devices: the whole disk, or runs
/// of cylinders as the ATM-Turbo HDD driver lays them out (a descriptor at a cylinder boundary), the BS-DOS 309 floppies
/// of a #47 partition, and the virtual disks of an MDOS3 partition (MBR type #49, or the whole device from its first
/// "SDOS" sector). Reading everywhere; writing, deleting and renaming files on every one of them (a block device that
/// takes writes).
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
    std::string id;  ///< the name to pick it: "trdos", "plus3", "isdos", "fat" (a floppy, a superfloppy), "p1".."p4"
                     ///< (an MBR partition), "cpm" (an ATM or Profi CP/M floppy), "mb02", "mdos", "cpm-<cylinder>" (an ATM CP/M
                     ///< hard disk partition), "isdos-<cylinder>" (an iS-DOS device on a hard disk), "mdos-<slot>"
                     ///< (an MDOS3 virtual disk; "mdos-p<partition>-<slot>" with two #49 partitions or more), "mb02-<n>"
                     ///< (the BS-DOS 309 floppy @n of a #47 partition)
    std::string fs;        ///< trdos, plus3, isdos, cpm, fat12, fat16, fat32
    std::string system;    ///< what it reads as: "TR-DOS", "+3DOS", "iS-DOS Chic", "iS-DOS Classic", "FAT16"
    std::string label;     ///< the disk title or volume label
    uint64_t offset = 0;   ///< block media: the volume's first byte on the disk
    uint64_t bytes = 0;    ///< its size
    int64_t freeBytes = -1;  ///< -1: not known
    bool writable = false;   ///< Write, Delete, Rename and the Set calls work on it
    std::string attributes;  ///< the attribute letters SetAttributes takes: FAT "RHSA", iS-DOS "RWHD", +3DOS "RSA"
    bool times = false;      ///< SetTime works (FAT, iS-DOS, MB-02)
    bool labels = false;     ///< SetLabel works (FAT, TR-DOS, iS-DOS, MB-02, MDOS)
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
    /// special, +3DOS user / basicType / param2, ATM CP/M user / extents / block, MB-02
    /// type / param2 / address / entry / sector / flags / attributes, MDOS type / param2 / sector / slot / extra, FAT
    /// cluster / attributes
    std::vector<std::pair<std::string, int64_t>> fields;

    /// The value of field `key`, `otherwise` when the entry has none
    int64_t Field(const std::string& key, int64_t otherwise = 0) const;
};

struct DiskFsWriteOptions
{
    uint16_t start = 0;        ///< TR-DOS: the start field; +3DOS, MB-02, MDOS: param 1, CODE's load address or a
                               ///< program's autostart line; iS-DOS: the load address
    bool lengthField = true;   ///< TR-DOS: false leaves 0 in the length field (XAS reads whole sectors)
    bool header = true;        ///< +3DOS: a +3DOS header in front, as +3 BASIC's SAVE writes it
    int64_t mtime = 0;         ///< FAT, iS-DOS, MB-02: the file's date and time as UTC seconds of the DOS wall clock
                               ///< (0: 1980-01-01 00:00)
    int basicType = -1;        ///< +3DOS, MB-02: the header's type, 0 a program, 1 a number array, 2 a character array,
                               ///< 3 CODE (-1: CODE); MB-02 takes 0 and 3, MDOS takes the type from the name
    int32_t param2 = -1;       ///< +3DOS, MB-02, MDOS: the header's param 2 (-1: a program's length, else 32768)
    std::string attributes;    ///< the letters SetAttributes takes, set once the file is written: "RA" exactly these,
                               ///< "+R-H" changes to what the file system writes ("": as written)
};

struct DiskFsDirectoryOptions
{
    int64_t mtime = 0;        ///< as in DiskFsWriteOptions
    bool continuous = false;  ///< iS-DOS: a continuous catalogue (TASiS mkdir.com /c)
    uint32_t blocks = 0;      ///< iS-DOS: the catalogue's size, 1-16 blocks (mkdir.com /s; 0: the standard size)
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
    /// `data` as the file `path` (TR-DOS "NAME.T", +3DOS "NAME.TYP", ATM CP/M "USERn/NAME.TYP", FAT and iS-DOS "DIR/name.ext", MB-02 "dir/name", MDOS
    /// "NAME.T" in a directory
    /// that is there): an older file of that name is replaced. The disk is changed in place; saving it is the
    /// caller's. False on a volume that is not writable
    bool Write(const std::string& volume, const std::string& path, const std::vector<uint8_t>& data,
               const DiskFsWriteOptions& options, std::string& error);
    /// Delete the file `path` (FAT, iS-DOS, MB-02: an empty directory too). TR-DOS marks it deleted as TR-DOS does (its
    /// sectors stay used until the disk is compacted). `recursive`: a directory goes with everything in it; every
    /// entry is checked first (a protected iS-DOS file, a read-only FAT entry), so nothing goes when one would stay
    bool Delete(const std::string& volume, const std::string& path, std::string& error, bool recursive = false);
    /// A new empty directory `path` (FAT, iS-DOS, MB-02) in a directory that is there; `parents`: the missing
    /// directories on the way are made too (of the standard kind and size), and one that is there already is no error.
    /// `continuous` and `blocks` are refused on a file system other than iS-DOS
    bool MakeDirectory(const std::string& volume, const std::string& path, bool parents,
                       const DiskFsDirectoryOptions& options, std::string& error);
    bool MakeDirectory(const std::string& volume, const std::string& path, bool parents, int64_t mtime,
                       std::string& error)
    {
        DiskFsDirectoryOptions options;
        options.mtime = mtime;
        return MakeDirectory(volume, path, parents, options, error);
    }
    /// The file `path` takes the name `to`: a bare name keeps it in its directory; FAT, iS-DOS and MB-02 take a path from
    /// the root too ("/name" the root), moving it to another directory of the volume, a directory with its contents (not
    /// into itself). False when `to` is there already
    bool Rename(const std::string& volume, const std::string& path, const std::string& to, std::string& error);
    /// The attributes of `path` by the letters of its file system (DiskFsVolume::attributes; FAT R read only, H hidden,
    /// S system, A archive; iS-DOS R / W read / write protected, H hidden, D delete protected; +3DOS R read only,
    /// S system, A archive): "+R-H" sets and clears, "RA" leaves exactly these set. Other letters are refused
    bool SetAttributes(const std::string& volume, const std::string& path, const std::string& attributes, std::string& error);
    /// The date and time of `path` (FAT, iS-DOS, MB-02), `mtime` as in DiskFsWriteOptions
    bool SetTime(const std::string& volume, const std::string& path, int64_t mtime, std::string& error);
    /// The volume's name: a FAT volume label ("" removes it; `mtime` its entry's time), the TR-DOS disk title, the
    /// iS-DOS device name, the MB-02 or MDOS disk name
    bool SetLabel(const std::string& volume, const std::string& label, int64_t mtime, std::string& error);

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
