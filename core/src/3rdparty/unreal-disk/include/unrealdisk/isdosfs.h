#pragma once

// The iS-DOS file system (Iskra Soft: Classic to 1996, Chic from 1997; TASiS of NedoPC keeps it) on a device of 256-byte
// blocks, read only: the device descriptor (block 0), the catalogue tree (32-byte descriptors, entry 0 the catalogue
// itself, at most 128 entries), continuous and segmented files (a segment table of up to 85 runs), the allocation
// bitmap (from block 1, set = used). Blocks come from a BlockReader: a floppy (OnDisk: the descriptor's geometry and
// sector numbering table, normally 5 x 1024-byte sectors with ids 1, 2, 3, 4, 9) or a linear dump (OnBytes: ISD, XiSD
// IMG; an HDD device is a run of blocks at a cylinder of the disk, see the emulator's DiskFs).
// Layout and sources: the iS-DOS appendices ("Пакет документации о рестартах iS-DOS", 2004) and XiSD; written down in
// docs/emulator/design/disk-file-systems.md.
//
// Damage never stops a listing: a catalogue that cannot be read is the entry's `error`, a loop (a sub-catalogue that is
// an ancestor or already listed) and nesting past 16 levels are cut; a file whose blocks are missing reads its prefix
// and fails with the block.
//
// Example: ISDOS50.FDI (Classic, "is-DOS94"): List gives 537 files and 54 catalogues, the root starting device.sys,
// boot.sys, SHELL, UTIL; Find("read_me.txt") and Read give its 1641 bytes.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace unrealdisk
{
class DiskImage;

/// Block 0, the device descriptor
struct IsdosBoot
{
    bool chic = false;           ///< "DSK" at +13 (Chic), else at +10 (Classic)
    std::string title;           ///< the device name, UTF-8 (CP866), trailing blanks removed
    uint16_t blocks = 0;         ///< the device size in 256-byte blocks
    uint16_t root = 0;           ///< block 0 of the main catalogue
    uint8_t cylinders = 0;
    uint8_t diskType = 0;        ///< bit 0: 80 tracks, bit 1: two sides
    uint8_t sectorBlocks = 0;    ///< blocks per sector: 1, 2, 4 (256, 512, 1024 bytes)
    uint8_t sectors = 0;         ///< sectors per track
    uint16_t date = 0;           ///< MS-DOS date
    std::vector<uint8_t> sectorTable;  ///< position -> sector id - 1 (00 01 02 03 08 on a normal floppy)
};

struct IsdosFile
{
    std::string path;            ///< from the root, '/' between levels: "SHELL/HELP/exit.hlp"
    std::string name;            ///< "exit.hlp": name.ext, UTF-8 (CP866), blanks trimmed, '/' as '_'
    uint8_t rawName[11] = {};    ///< the 8 + 3 bytes as stored
    bool directory = false;
    bool system = false;         ///< device.sys / boot.sys (status #FF, special #FF): files, not catalogues
    uint8_t status = 0;          ///< bit 0 exists, 2 / 3 / 7 read / write / delete protected, 4 hidden, 5 catalogue, 6 continuous
    uint8_t special = 0;
    uint16_t load = 0;           ///< the default load address (files)
    uint32_t length = 0;         ///< the file's bytes; a catalogue's size field
    uint16_t first = 0;          ///< continuous: the first block; segmented: the segment table; catalogues: block 0
    uint16_t date = 0;           ///< MS-DOS date (0: none)
    uint16_t time = 0;           ///< MS-DOS time
    uint16_t checksum = 0;       ///< FSUM as stored (its algorithm is not known: not checked)
    std::string error;           ///< a catalogue that did not open (its contents are not listed)

    bool Segmented() const { return (status & 0x40) == 0; }
    bool Hidden() const { return (status & 0x10) != 0; }
    /// XiSD's letters: S segmented (. continuous), R / W / D protected, H hidden
    std::string Attributes() const;
};

class IsdosFileSystem
{
public:
    /// Block `block` (256 bytes) into `dst`; false when it cannot be read (a missing sector, past the medium)
    using BlockReader = std::function<bool(uint32_t block, uint8_t* dst)>;

    explicit IsdosFileSystem(BlockReader reader);

    /// A floppy: block 0 in sector id 1 of cylinder 0 head 0, then the descriptor's geometry and sector table; the
    /// image keeps living while the file system is used
    static IsdosFileSystem OnDisk(DiskImage& image);
    /// A linear dump: block n at n * 256 (the bytes keep living while the file system is used)
    static IsdosFileSystem OnBytes(const uint8_t* data, size_t size);

    /// 256 bytes that are an iS-DOS device descriptor with a plausible geometry ("DSK" at +13 or +10 alone is not
    /// enough: the "EXTENDED CPC DSK File" header has it at +13)
    static bool IsDescriptor(const uint8_t* block);

    /// The descriptor reads and the main catalogue opens, naming itself as its own block 0 and parent
    bool IsIsdos() const;
    /// The descriptor (empty when block 0 is no descriptor)
    const IsdosBoot& Boot() const { return _boot; }

    /// The whole tree depth first in catalogue order, a catalogue right before its contents, deleted entries left out.
    /// `notes` (may be null) gets what was cut (loops, stale entries, unreadable catalogues). False with the reason
    /// when the main catalogue does not open
    bool List(std::vector<IsdosFile>& out, std::vector<std::string>* notes, std::string& error) const;
    /// The entries of the catalogue at `path` ("" or "/" the root), not descending
    bool ListDirectory(const std::string& path, std::vector<IsdosFile>& out, std::string& error) const;
    /// The entry at `path` (each level's name.ext, ASCII case ignored)
    bool Find(const std::string& path, IsdosFile& out, std::string& error) const;
    /// The file's `length` bytes; on a missing block false with the block, `data` holding the readable prefix
    bool Read(const IsdosFile& file, std::vector<uint8_t>& data, std::string& error) const;
    /// Free blocks by the bitmap; false when the bitmap does not read
    bool FreeBlocks(uint32_t& free) const;

private:
    struct Run
    {
        uint32_t start = 0;
        uint32_t count = 0;
    };
    struct Catalogue
    {
        std::vector<IsdosFile> entries;  ///< paths relative to the catalogue
        std::string error;               ///< read partly
        uint32_t stale = 0;              ///< entries past NFILE still marked as existing
    };

    bool Block(uint32_t block, uint8_t* dst) const;
    bool Runs(uint8_t status, uint16_t first, uint16_t segmentTable, uint32_t size, std::vector<Run>& runs, std::string& error) const;
    bool ReadRuns(const std::vector<Run>& runs, uint32_t size, std::vector<uint8_t>& data, std::string& error) const;
    bool OpenCatalogue(uint16_t first, Catalogue& out, std::string& error) const;
    bool RootOk() const;

    BlockReader _reader;
    IsdosBoot _boot;
    bool _descriptor = false;
};
}  // namespace unrealdisk
