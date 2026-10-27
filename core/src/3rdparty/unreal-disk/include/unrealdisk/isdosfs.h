#pragma once

// The iS-DOS file system (Iskra Soft: Classic to 1996, Chic from 1997; TASiS of NedoPC keeps it) on a device of 256-byte
// blocks: the device descriptor (block 0), the catalogue tree (32-byte descriptors, entry 0 the catalogue
// itself, at most 128 entries), continuous and segmented files (a segment table of up to 85 runs), the allocation
// bitmap (from block 1, set = used). Blocks come from a BlockReader: a floppy (OnDisk: the descriptor's geometry and
// sector numbering table, normally 5 x 1024-byte sectors with ids 1, 2, 3, 4, 9) or a linear dump (OnBytes: ISD, XiSD
// IMG; an HDD device is a run of blocks at a cylinder of the disk, see the emulator's DiskFs).
// Layout and sources: the iS-DOS appendices ("Пакет документации о рестартах iS-DOS", 2004) and XiSD; written down in
// docs/emulator/design/disk-file-systems.md.
//
// Writing (a BlockWriter given: OnDisk has one) follows the kernel (inst_dos.sys of ISDOS50, disassembled): a new file
// takes the first deleted slot of its catalogue, else slot NFILE (a segmented catalogue grows by a block when it is
// full; a continuous one, which the kernel leaves full, takes the block after it, else becomes segmented); its blocks are the first free run (continuous up to 255 blocks, else segmented); the checksum FSUM is
// 0 - the sum of its bytes, as TASiS keeps it. A deleted entry only loses its status bit 0 (NFILE stays, NEFIL counts
// down), its blocks are freed. device.sys, boot.sys and the kernel file the descriptor keeps a copy of never change.
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
    /// 256 bytes from `src` as block `block`; false when it cannot be written
    using BlockWriter = std::function<bool(uint32_t block, const uint8_t* src)>;

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

    /// Writes go through `writer` (none: read only)
    void SetWriter(BlockWriter writer) { _writer = std::move(writer); }
    bool Writable() const { return static_cast<bool>(_writer); }

    struct WriteOptions
    {
        uint16_t load = 0;   ///< the default load address
        uint16_t date = 0;   ///< MS-DOS date
        uint16_t time = 0;   ///< MS-DOS time (+28, which XiSD writes; the kernel ignores it)
    };
    /// `bytes` as the file `path` ("DIR/name.ext", the catalogue there already): an older file of that name (ASCII
    /// case ignored) is replaced, keeping its slot. False with the reason: no such catalogue, a bad name, a protected
    /// or system file, no room in the catalogue or on the device
    bool Write(const std::string& path, const std::vector<uint8_t>& bytes, const WriteOptions& options, std::string& error);
    /// Delete a file or an empty catalogue: its blocks freed, its status bit 0 cleared (a catalogue's parent entry
    /// loses bit 5 and the level byte +16, its own entry 0 bit 0, as delete.com leaves them). Delete-protected and
    /// system files are refused
    bool Delete(const std::string& path, std::string& error);
    /// Whether Delete would take `path` as far as the entry goes (not a system or delete-protected file)
    bool Deletable(const std::string& path, std::string& error) const;
    /// `path` takes the name `to`: a bare name stays in its catalogue, a path ("DIR/new.ext") moves the entry to
    /// another catalogue. A moved catalogue takes its tree along: its entry 0 names the new parent, the levels of it
    /// and every catalogue under it follow (the kernel has no such call; move.com skips catalogues). Not into itself,
    /// not deeper than kMaxLevel
    bool Rename(const std::string& path, const std::string& to, std::string& error);
    /// A new empty catalogue `path` ("DIR/NAME", upper-cased) as mkdir.com makes it: segmented (status #21), a table
    /// block, then one run of 2 blocks (Classic) or 3 (Chic); entry 0 names the parent, the level one deeper (+16
    /// Classic, +23 Chic), NFILE 1; at most kMaxLevel levels under the root
    bool MakeCatalogue(const std::string& path, const WriteOptions& options, std::string& error);
    static constexpr uint32_t kMaxLevel = 6;   ///< mkdir.com refuses a parent of level 6
    /// The status bits SetAttributes changes: read, write and delete protection, hidden
    static constexpr uint8_t kReadProtect = 0x04, kWriteProtect = 0x08, kHide = 0x10, kDeleteProtect = 0x80;
    /// The status bits of `path` in `attributes` (kReadProtect | kWriteProtect | kHide | kDeleteProtect), the other
    /// bits kept; a catalogue's entry 0 takes them too. System files and the kernel file are refused
    bool SetAttributes(const std::string& path, uint8_t attributes, std::string& error);
    /// The date (+30) and time (+28) of `path`; a catalogue's entry 0 takes them too
    bool SetTime(const std::string& path, uint16_t date, uint16_t time, std::string& error);
    /// The device name in the descriptor (+2: 8 characters, 11 on a Chic disk that has room for them), padded with
    /// blanks; CP866 letters as in names
    bool SetTitle(const std::string& title, std::string& error);
    /// `name` (UTF-8, Cyrillic as CP866) as the 8 + 3 bytes of a descriptor: 1-8 characters, a type of up to 3, the
    /// characters iS-DOS takes (letters, digits, # $ & + - = _ ` ! @ % ' ( ) { | } ~); a catalogue's in upper case
    static bool EncodeName(const std::string& name, bool catalogue, uint8_t out[11]);

private:
    struct Run
    {
        uint32_t start = 0;
        uint32_t count = 0;
    };
    /// A catalogue's blocks for editing: every block it holds, in order, and their bytes
    struct CatalogueImage
    {
        uint16_t first = 0;
        bool continuous = false;
        uint16_t table = 0;            ///< segmented: the segment table block
        std::vector<uint32_t> blocks;
        std::vector<uint8_t> raw;      ///< blocks.size() * 256 bytes, entry i at i * 32
    };
    /// A live entry: its catalogue and slot
    struct Slot
    {
        CatalogueImage catalogue;
        uint32_t index = 0;
        IsdosFile file;
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
    bool Put(uint32_t block, const uint8_t* src);
    bool LoadCatalogue(uint16_t first, CatalogueImage& out, std::string& error) const;
    bool StoreEntry(const CatalogueImage& c, uint32_t index);
    /// The catalogue that holds `parts` but the last ("" the root)
    bool ParentOf(const std::vector<std::string>& parts, uint16_t& first, std::string& error) const;
    bool Locate(const std::string& path, Slot& out, std::string& error) const;
    /// A live entry changed by `change` (its catalogue's entry 0 too when it is one) and stored
    template <typename Change>
    bool EditEntry(const std::string& path, std::string& error, Change change);
    /// The descriptor's name width: 11 on a Chic disk without "DSK" at +10, else 8
    static size_t TitleWidth(const uint8_t* descriptor);
    /// Refuses the entries that never change (system files, the kernel file)
    bool Fixed(const uint8_t* entry) const;
    bool LoadBitmap(std::vector<uint8_t>& bits, std::string& error) const;
    bool StoreBitmap(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after);
    /// The blocks a file or catalogue entry holds (its segment table included)
    bool Held(const uint8_t* entry, std::vector<uint32_t>& blocks, std::string& error) const;
    /// A free slot of `c` (the first deleted one, else NFILE, growing the catalogue by a block from `bits`: a
    /// segmented one adds it to its table, a continuous one takes the block after it, else becomes segmented)
    bool TakeSlot(CatalogueImage& c, std::vector<uint8_t>& bits, uint32_t& index, std::string& error);
    /// The blocks TakeSlot would take for `c`, without changing anything: none (a free slot, room in its last block,
    /// or no room at all), the new block, or a continuous catalogue's new segment table and then the new block
    std::vector<uint32_t> GrowthBlocks(const CatalogueImage& c, const std::vector<uint8_t>& bits) const;
    /// `parent`'s entry of the catalogue `c` follows its head (continuous bit, +17): false when `parent` has none
    static bool FollowHead(CatalogueImage& parent, const CatalogueImage& c, uint32_t& index);
    /// A catalogue's level from its entry 0 (Classic +16, Chic +23), and where it goes
    static uint32_t LevelOf(const uint8_t* entry0) { return static_cast<uint32_t>(entry0[16] | entry0[23]); }
    void SetLevel(uint8_t* entry0, uint32_t level) const;
    /// The first blocks of the catalogues under `first`, any depth, with their level below it (1: its children)
    bool Subtree(uint16_t first, std::vector<std::pair<uint16_t, uint32_t>>& out, uint32_t depth, std::string& error) const;

    BlockReader _reader;
    BlockWriter _writer;
    IsdosBoot _boot;
    bool _descriptor = false;
};
}  // namespace unrealdisk
