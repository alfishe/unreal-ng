#pragma once

/// @file gdosfs.h
/// @brief G+DOS (MGT +D, DISCiPLE): the directory of an MGT disk and its files, read only.
///
/// 80 cylinders, 2 sides, 10 sectors of 512 bytes (ids 1-10). The directory is cylinders 0-3 of side 0: 80 entries of
/// 256 bytes, two to a sector. An entry: byte 0 the type (bits 0-5; 0 a free entry, 1 BASIC, 2 number array, 3
/// character array, 4 CODE, 5 48K snapshot, 6 Microdrive file, 7 SCREEN$, 8 special, 9 128K snapshot, 10 opentype,
/// 11 execute; bit 6 hidden, bit 7 protected), 1-10 the name (blank padded), 11-12 the sectors used (high byte
/// first), 13 the first track (bit 7 side 1, bits 0-6 the cylinder), 14 the first sector, 15-209 the sector map,
/// 211-219 for the tape types (1-4, 7) the 9-byte tape header (type, length, start, param, autostart line; the same
/// 9 bytes start the file's data), 220-241 a 48K snapshot's registers. Each data sector holds 510 bytes and the
/// track and sector of the next one (0, 0 the last).
///
/// Example: data/boot/plusd/gdos-2a-system.mgt: List gives "+SYS 2a" (CODE, 14 sectors, 6656 bytes at #2000) first;
/// Read gives its 6656 bytes (the header cut off).
/// Layout: docs/emulator/design/disk-file-systems.md (G+DOS layout, in the unreal-ng repository).

#include <cstdint>
#include <string>
#include <vector>

namespace unrealdisk
{
class DiskImage;

struct GdosFile
{
    uint32_t slot = 0;         ///< its directory entry (0..79)
    uint8_t type = 0;          ///< bits 0-5 of byte 0
    bool hidden = false;       ///< bit 6
    bool protect = false;      ///< bit 7
    std::string name;          ///< trailing blanks cut
    uint16_t sectors = 0;      ///< the sectors used
    uint8_t track = 0;         ///< the first track (bit 7 side 1)
    uint8_t sector = 0;        ///< the first sector (1-10)
    uint32_t length = 0;       ///< the bytes Read gives: the tape types' header length, 49152 for a 48K snapshot,
                               ///< else every data byte of its sectors
    uint16_t start = 0;        ///< the tape header's start (CODE: the load address, BASIC: PROG)
    uint16_t param = 0;        ///< the tape header's param (BASIC: the program's length without variables)
    uint16_t autostart = 0;    ///< BASIC: the autostart line (>= 32768: none)

    /// The 9-byte tape header leads the data (types 1-4 and 7)
    bool HasTapeHeader() const { return type == 1 || type == 2 || type == 3 || type == 4 || type == 7; }
};

class GdosFileSystem
{
public:
    static constexpr uint32_t kSector = 512;
    static constexpr uint32_t kSectorsPerTrack = 10;
    static constexpr uint32_t kDirectoryCylinders = 4;
    static constexpr uint32_t kEntries = kDirectoryCylinders * kSectorsPerTrack * 2;
    static constexpr uint32_t kDataPerSector = 510;

    explicit GdosFileSystem(DiskImage& image) : _image(image) {}

    /// The disk has the geometry and its directory reads as G+DOS: every used entry has a G+DOS type (1-11; SAM DOS
    /// types are 16 and up), a first sector in 1-10 and a first track past the directory; an empty directory counts
    bool IsGdos(std::string* why = nullptr) const;

    /// The used entries in directory order; false with the reason when the directory does not read
    bool List(std::vector<GdosFile>& out, std::string& error) const;

    /// The file's `length` bytes (the tape header cut off), following the sector links. On damage false with the
    /// reason, `data` holding what could be read
    bool Read(const GdosFile& file, std::vector<uint8_t>& data, std::string& error) const;

    /// The free sectors (1600 less the directory and the used ones)
    uint32_t FreeSectors() const;

    /// Size of the whole disk in bytes
    uint64_t Bytes() const;

private:
    const uint8_t* Sector(uint8_t track, uint8_t sector) const;
    bool Entry(uint32_t slot, const uint8_t*& entry) const;

    DiskImage& _image;
};
}  // namespace unrealdisk
