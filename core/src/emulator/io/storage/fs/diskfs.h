#pragma once

/// @file diskfs.h
/// @brief The files on any disk the emulator holds, one way for every file system: the volumes of a medium, a
/// directory's entries, a file's bytes. Floppies (DiskImage): TR-DOS, +3DOS, iS-DOS / TASiS, FAT12 (Sprinter DSS,
/// MS-DOS 720K / 1.44M). Hard disks, SD and CF cards (IBlockDevice): the FAT partitions of the MBR or a superfloppy
/// (Estex DSS, NedoOS, any FAT12 / 16 / 32), and iS-DOS / TASiS devices: the whole disk, or runs of cylinders as the
/// ATM-Turbo HDD driver lays them out (a descriptor at a cylinder boundary). Read only.
///
/// Analyzers and every automation plane use the same class: MediaControl's `volumes`, `files` and `read` verbs (CLI
/// `media files A`, REST /media/{slot}/files, MCP `media`, Python / Lua `media_files`), and C++ through DiskFs::Open.
/// The caller reads at a coherent moment (the emulator paused, parked or on its own thread). Design:
/// docs/emulator/design/disk-file-systems.md.
///
/// Example: ISDOS50.FDI in drive A: Volumes() gives one "isdos" volume ("iS-DOS Classic", "is-DOS94");
/// List("", "SHELL", false) the entries of SHELL; Read("", "read_me.txt") its 1641 bytes.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/unicodehelper.h"

class IBlockDevice;
class Medium;
namespace unrealdisk
{
class DiskImage;
}

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
    /// What only this file system has: TR-DOS track / sector / sectors, iS-DOS status / first / special, +3DOS user /
    /// basicType / param2, FAT cluster / attributes
    std::vector<std::pair<std::string, int64_t>> fields;
};

class DiskFs
{
public:
    explicit DiskFs(unrealdisk::DiskImage& floppy);
    /// `page`: FAT short names
    explicit DiskFs(IBlockDevice& device, CodePage page = CodePage::Cp866);
    ~DiskFs();
    DiskFs(const DiskFs&) = delete;
    DiskFs& operator=(const DiskFs&) = delete;

    /// A slot's medium: a floppy, or a block medium read under the time travel read tap (nothing is journalled).
    /// Null for tapes, CDs and empty slots
    static std::unique_ptr<DiskFs> Open(Medium& medium);

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
    /// A file's bytes. On damage false with the reason, `data` holding what could be read
    bool Read(const std::string& volume, const std::string& path, std::vector<uint8_t>& data, std::string& error);

    /// A file system's reader (diskfs.cpp)
    struct Driver;

private:
    void Probe();
    Driver* Find(const std::string& volume, std::string& error);

    unrealdisk::DiskImage* _floppy = nullptr;
    IBlockDevice* _device = nullptr;
    CodePage _page = CodePage::Cp866;
    bool _probed = false;
    std::vector<DiskFsVolume> _volumes;
    std::vector<std::unique_ptr<Driver>> _drivers;
    std::vector<std::string> _notes;
};
