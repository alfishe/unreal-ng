#include "unrealdisk/diskfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <set>

#include "unrealdisk/blockdevice.h"
#include "unrealdisk/cpmfs.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/fatfs.h"
#include "unrealdisk/fatvolumereader.h"
#include "unrealdisk/floppyblockdevice.h"
#include "unrealdisk/isdosfs.h"
#include "unrealdisk/mb01fs.h"
#include "unrealdisk/mb02fs.h"
#include "unrealdisk/mdosfs.h"
#include "unrealdisk/plus3fs.h"
#include "unrealdisk/subrangedevice.h"
#include "unrealdisk/trdoscatalog.h"
#include "unrealdisk/trdosfs.h"

namespace unrealdisk
{
namespace
{
constexpr uint32_t kSector = BlockDevice::kSectorSize;
/// The geometry of a hard disk without one in its image (16 heads, 63 sectors: the ATM-Turbo IDE images)
constexpr uint64_t kDefaultHeads = 16;
constexpr uint64_t kDefaultSectors = 63;
/// Outside the FAT volumes an MDOS3 slot without a #49 partition is looked for in this many sectors from LBA 0
constexpr uint64_t kMdos3Search = 4096;
/// After the end of a volume an iS-DOS device is looked for in this many sectors: a cylinder of 16 heads of 63 (the
/// most an IDE drive reports), where the HDD driver starts the next device
constexpr uint64_t kIsdosChainWindow = 16 * 63;
/// A BS-DOS 309 virtual floppy on its #47 partition: 2 MiB
constexpr uint64_t kBsdosDisc = 4096;

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string Upper(std::string text)
{
    for (char& c : text)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return text;
}

/// "/A//B/" -> "A/B"; a backslash separates too, unless it starts a \\xNN byte of a name
std::string CleanPath(const std::string& path)
{
    std::string out;
    std::string part;
    auto flush = [&]() {
        if (!part.empty() && part != ".")
            out += (out.empty() ? "" : "/") + part;
        part.clear();
    };
    for (size_t i = 0; i < path.size(); i++)
    {
        const char c = path[i];
        const bool escape = c == '\\' && i + 3 < path.size() && path[i + 1] == 'x' &&
                            std::isxdigit(static_cast<unsigned char>(path[i + 2])) && std::isxdigit(static_cast<unsigned char>(path[i + 3]));
        if (c == '/' || (c == '\\' && !escape))
            flush();
        else
            part += c;
    }
    flush();
    return out;
}

/// ASCII name bytes, control and high bytes as \xNN
std::string Ascii(const std::string& raw)
{
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (const char ch : raw)
    {
        const uint8_t c = static_cast<uint8_t>(ch);
        if (c < 0x20 || c >= 0x7F)
        {
            out += "\\x";
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
        else
            out += static_cast<char>(c);
    }
    return out;
}

/// Entries under `dir` ("" = all) from a flat list of paths; `recursive` false: only the direct children
bool Under(const std::string& path, const std::string& dir, bool recursive)
{
    if (dir.empty())
        return recursive || path.find('/') == std::string::npos;
    if (path.size() <= dir.size() + 1 || Lower(path.substr(0, dir.size() + 1)) != Lower(dir) + "/")
        return false;
    return recursive || path.find('/', dir.size() + 1) == std::string::npos;
}

std::shared_ptr<BlockDevice> Borrow(BlockDevice& device)
{
    return std::shared_ptr<BlockDevice>(&device, [](BlockDevice*) {});
}

/// The last sector a block reader read: its writer keeps it current
struct SectorCache
{
    uint64_t lba = UINT64_MAX;
    uint8_t sector[kSector] = {};
};

/// 256-byte blocks from `offset` bytes into a block device (the last sector read is kept in `cache`)
IsdosFileSystem::BlockReader BlockReaderAt(BlockDevice& device, uint64_t offset, std::shared_ptr<SectorCache> cache)
{
    return [&device, offset, cache](uint32_t block, uint8_t* dst) {
        const uint64_t byte = offset + static_cast<uint64_t>(block) * 256;
        const uint64_t lba = byte / kSector;
        if (lba != cache->lba)
        {
            if (!device.ReadSector(lba, cache->sector))
            {
                cache->lba = UINT64_MAX;
                return false;
            }
            cache->lba = lba;
        }
        std::memcpy(dst, cache->sector + byte % kSector, 256);
        return true;
    };
}

/// 256-byte blocks written into their 512-byte sectors (read, patched, written back)
IsdosFileSystem::BlockWriter BlockWriterAt(BlockDevice& device, uint64_t offset, std::shared_ptr<SectorCache> cache)
{
    return [&device, offset, cache](uint32_t block, const uint8_t* src) {
        const uint64_t byte = offset + static_cast<uint64_t>(block) * 256;
        const uint64_t lba = byte / kSector;
        uint8_t sector[kSector];
        cache->lba = UINT64_MAX;
        if (!device.ReadSector(lba, sector))
            return false;
        std::memcpy(sector + byte % kSector, src, 256);
        return device.WriteSector(lba, sector);
    };
}

using SectorRanges = std::vector<std::pair<uint64_t, uint64_t>>;

/// The first sector from `from` up to `to` outside the `taken` ranges for which `match` holds, each multiple of
/// `step` read (a run the device knows to be zeros is skipped: no signature is all zeros); `to` when none
uint64_t ScanUntaken(BlockDevice& device, uint64_t from, uint64_t to, SectorRanges taken,
                     const std::function<bool(const uint8_t*)>& match, uint64_t step = 1)
{
    std::sort(taken.begin(), taken.end());
    auto align = [step](uint64_t lba) { return (lba + step - 1) / step * step; };
    size_t next = 0;
    uint8_t s[kSector];
    uint64_t asked = 0;  // ZeroRun is asked once in 128 sectors (a file image answers it with a system call)
    for (uint64_t lba = align(from); lba < to;)
    {
        while (next < taken.size() && taken[next].second <= lba)
            next++;
        if (next < taken.size() && taken[next].first <= lba)
        {
            lba = align(taken[next].second);
            asked = 0;
            continue;
        }
        if (lba >= asked)
        {
            if (const uint64_t zeros = device.ZeroRun(lba))
            {
                lba = align(lba + zeros);
                continue;
            }
            asked = lba + 128;
        }
        if (device.ReadSector(lba, s) && match(s))
            return lba;
        lba += step;
    }
    return to;
}

/// The lowest device LBA where a file of the FAT volumes starts with "SDOS": an .md3 file, or MDOS3 slot files, on a
/// card. The firmware reads the disk sector by sector up to the first; a file's first sector is where it meets it
/// (`none` when no file starts so)
uint64_t FirstSdosFile(const std::vector<std::pair<std::shared_ptr<BlockDevice>, uint64_t>>& fats, CodePage page,
                       uint64_t none)
{
    uint64_t best = none;
    for (const auto& [volume, first] : fats)
    {
        FatFs fs(*volume, page);
        if (!fs.Open())
            continue;
        std::vector<FatNode> dirs{fs.Root()};
        std::set<uint32_t> seen;  // a damaged tree may loop
        while (!dirs.empty())
        {
            const FatNode dir = dirs.back();
            dirs.pop_back();
            std::vector<FatNode> entries;
            if (!fs.List(dir, entries))
                continue;
            for (const FatNode& node : entries)
            {
                std::vector<std::pair<uint64_t, uint32_t>> runs;
                uint8_t s[kSector];
                if (node.IsDirectory())
                {
                    if (node.firstCluster && seen.insert(node.firstCluster).second)
                        dirs.push_back(node);
                }
                else if (node.size >= kSector && node.firstCluster && fs.Extents(node, runs) && !runs.empty() &&
                         first + runs[0].first < best && volume->ReadSector(runs[0].first, s) &&
                         std::memcmp(s, "SDOS", 4) == 0)
                    best = first + runs[0].first;
            }
        }
    }
    return best;
}
}  // namespace

/// region <Drivers>

struct DiskFs::Driver
{
    virtual ~Driver() = default;
    /// Every entry of the volume in listing order (a directory before its contents); drivers that read directories
    /// one at a time override List and Stat instead
    virtual bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>& notes, std::string& error) = 0;
    virtual bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>* slack, std::string& error) = 0;
    virtual bool Write(const std::string&, const std::vector<uint8_t>&, const DiskFsWriteOptions&, std::string& error)
    {
        error = "the volume is read only";
        return false;
    }
    virtual bool Delete(const std::string&, std::string& error)
    {
        error = "the volume is read only";
        return false;
    }
    virtual bool Rename(const std::string&, const std::string&, std::string& error)
    {
        error = "the volume is read only";
        return false;
    }
    /// A new empty directory; false with no reason: the file system has none
    virtual bool MakeDirectory(const std::string&, const DiskFsDirectoryOptions&, std::string&) { return false; }
    /// Whether MakeDirectory takes `continuous` and `blocks`
    virtual bool SizedDirectories() const { return false; }
    /// Whether the entry may be deleted (a recursive delete checks every entry before it deletes one)
    virtual bool Deletable(const DiskFsEntry&, std::string&) { return true; }
    /// The header types Write takes as DiskFsWriteOptions::basicType, as digits ("": none)
    virtual std::string BasicTypes() const { return {}; }
    /// Whether Write takes DiskFsWriteOptions::param2
    virtual bool TakesParam2() const { return false; }
    /// The attribute letters SetAttributes takes ("": none)
    virtual std::string AttributeLetters() const { return {}; }
    /// The entry's attributes set to exactly `letters` (of AttributeLetters, upper case)
    virtual bool SetAttributes(const DiskFsEntry&, const std::string&, std::string& error)
    {
        error = "the volume has no attributes";
        return false;
    }
    /// False with no reason: the file system keeps no times / no name
    virtual bool SetTime(const std::string&, int64_t, std::string&) { return false; }
    virtual bool SetLabel(const std::string&, int64_t, std::string&) { return false; }
    virtual bool HasTimes() const { return false; }
    virtual bool HasLabel() const { return false; }
    /// The name as the volume keeps it after SetLabel ("": as given)
    virtual std::string Label() const { return {}; }

    virtual bool List(const std::string& dir, bool recursive, std::vector<DiskFsEntry>& out, std::vector<std::string>& notes,
                      std::string& error)
    {
        std::vector<DiskFsEntry> all;
        if (!All(all, notes, error))
            return false;
        if (!dir.empty())
        {
            DiskFsEntry entry;
            if (!Stat(dir, entry, notes, error))
                return false;
            if (!entry.directory)
            {
                error = dir + " is no directory";
                return false;
            }
        }
        for (DiskFsEntry& e : all)
            if (Under(e.path, dir, recursive))
                out.push_back(std::move(e));
        return true;
    }

    virtual bool Stat(const std::string& path, DiskFsEntry& out, std::vector<std::string>& notes, std::string& error)
    {
        std::vector<DiskFsEntry> all;
        if (!All(all, notes, error))
            return false;
        // The last match: TR-DOS finds the last live file of a name; exact case first
        for (int pass = 0; pass < 2; pass++)
            for (auto it = all.rbegin(); it != all.rend(); ++it)
                if (pass == 0 ? it->path == path : Lower(it->path) == Lower(path))
                {
                    out = *it;
                    return true;
                }
        error = "no " + path + " on the volume";
        return false;
    }
};

namespace
{
/// `letters` (each one of `all`) as bits: the first letter of `all` bit 0
uint32_t Bits(const std::string& letters, const std::string& all)
{
    uint32_t bits = 0;
    for (const char c : letters)
    {
        const size_t at = all.find(c);
        if (at != std::string::npos)
            bits |= 1u << at;
    }
    return bits;
}

std::string TrdosName(const TrdosFile& f)
{
    return Ascii(f.TrimmedName()) + "." + Ascii(std::string(1, f.type));
}

/// The reverse of Ascii: \\xNN back to its byte
std::string Unescape(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); i++)
    {
        if (text[i] == '\\' && i + 3 < text.size() && text[i + 1] == 'x' && std::isxdigit(static_cast<unsigned char>(text[i + 2])) &&
            std::isxdigit(static_cast<unsigned char>(text[i + 3])))
        {
            out += static_cast<char>(std::stoi(text.substr(i + 2, 2), nullptr, 16));
            i += 3;
        }
        else
            out += text[i];
    }
    return out;
}

/// "NAME.T" as TrdosName gives it (bytes that are no printable ASCII as \\xNN) -> the name without trailing blanks
/// and the type byte
bool ParseTrdosName(const std::string& path, std::string& name, char& type)
{
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos || dot == 0 || path.find('/') != std::string::npos)
        return false;
    const std::string tail = Unescape(path.substr(dot + 1));
    if (tail.size() != 1)
        return false;
    type = tail[0];
    name = Unescape(path.substr(0, dot));
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return !name.empty() && name.size() <= 8;
}

struct TrdosDriver : DiskFs::Driver
{
    explicit TrdosDriver(DiskImage& image) : fs(image) {}

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>&, std::string& error) override
    {
        std::vector<TrdosFile> files;
        if (!fs.List(files, error))
            return false;
        for (const TrdosFile& f : files)
        {
            DiskFsEntry e;
            e.name = e.path = TrdosName(f);
            e.size = f.length;
            e.start = f.start;
            e.fields = {{"type", static_cast<uint8_t>(f.type)}, {"sectors", f.sectors}, {"track", f.firstTrack},
                        {"sector", f.firstSector}, {"slot", f.slot}};
            out.push_back(std::move(e));
        }
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>* slack, std::string& error) override
    {
        std::vector<TrdosFile> files;
        if (!fs.List(files, error))
            return false;
        for (int pass = 0; pass < 2; pass++)
            for (auto it = files.rbegin(); it != files.rend(); ++it)
                if (pass == 0 ? TrdosName(*it) == path : Lower(TrdosName(*it)) == Lower(path))
                    return fs.Read(*it, data, slack, error);
        error = "no " + path + " on the TR-DOS disk";
        return false;
    }

    bool Write(const std::string& path, const std::vector<uint8_t>& data, const DiskFsWriteOptions& options,
               std::string& error) override
    {
        std::string name;
        char type = 0;
        if (!Parse(path, name, type, error))
            return false;
        if (data.size() > 0xFFFF)
        {
            error = "a TR-DOS file holds at most 65535 bytes";
            return false;
        }
        TrdosFileSystem::WriteOptions write;
        write.start = options.start;
        write.lengthField = options.lengthField;
        return fs.Write(name, type, data, write, error);
    }

    bool Delete(const std::string& path, std::string& error) override
    {
        std::string name;
        char type = 0;
        return Parse(path, name, type, error) && fs.Delete(name, type, error);
    }

    bool Rename(const std::string& path, const std::string& to, std::string& error) override
    {
        std::string name, newName;
        char type = 0, newType = 0;
        return Parse(path, name, type, error) && Parse(to.substr(to.front() == '/'), newName, newType, error) &&
               fs.Rename(name, type, newName, newType, error);
    }

    bool SetLabel(const std::string& label, int64_t, std::string& error) override { return fs.SetTitle(label, error); }
    bool HasLabel() const override { return true; }

    static bool Parse(const std::string& path, std::string& name, char& type, std::string& error)
    {
        if (ParseTrdosName(path, name, type))
            return true;
        error = "a TR-DOS file is NAME.T: at most 8 characters and the type letter (" + path + ")";
        return false;
    }

    TrdosFileSystem fs;
};

struct Plus3Driver : DiskFs::Driver
{
    explicit Plus3Driver(DiskImage& image) : fs(image) {}

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>&, std::string& error) override
    {
        std::vector<Plus3File> files;
        if (!fs.List(files, error))
            return false;
        for (const Plus3File& f : files)
        {
            DiskFsEntry e;
            e.name = e.path = Ascii(f.FullName());
            e.size = f.length;
            e.start = f.header ? f.param1 : 0;
            static const char letters[] = "RSA";
            for (int bit = 0; bit < 3; bit++)
                e.attributes += (f.attributes & (1 << bit)) ? letters[bit] : '.';
            e.fields = {{"user", f.user}, {"onDisk", f.size}, {"header", f.header ? 1 : 0}};
            if (f.header)
            {
                e.fields.emplace_back("basicType", f.basicType);
                e.fields.emplace_back("param2", f.param2);
            }
            out.push_back(std::move(e));
        }
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>*, std::string& error) override
    {
        Plus3File f;
        return fs.Find(path, f, error) && fs.Read(f, data, error);
    }

    bool Write(const std::string& path, const std::vector<uint8_t>& data, const DiskFsWriteOptions& options,
               std::string& error) override
    {
        Plus3FileSystem::WriteOptions write;
        write.header = options.header;
        write.param1 = options.start;
        if (!options.header && (options.basicType >= 0 || options.param2 >= 0))
        {
            error = "a type and param 2 go in the +3DOS header: leave header on";
            return false;
        }
        write.basicType = static_cast<uint8_t>(options.basicType >= 0 ? options.basicType : 3);
        write.param2 = static_cast<uint16_t>(options.param2 >= 0           ? options.param2
                                             : write.basicType == 0 ? std::min<size_t>(data.size(), 0xFFFF)
                                                                    : 0x8000);
        return fs.Write(path, data, write, error);
    }

    std::string BasicTypes() const override { return "0123"; }
    bool TakesParam2() const override { return true; }

    bool Delete(const std::string& path, std::string& error) override { return fs.Delete(path, error); }

    bool Rename(const std::string& path, const std::string& to, std::string& error) override
    {
        return fs.Rename(path, to.substr(to.front() == '/'), error);
    }

    std::string AttributeLetters() const override { return "RSA"; }

    bool SetAttributes(const DiskFsEntry& entry, const std::string& letters, std::string& error) override
    {
        return fs.SetAttributes(entry.path, static_cast<uint8_t>(Bits(letters, "RSA")), error);
    }

    Plus3FileSystem fs;
};

DiskFsEntry IsdosEntry(const IsdosFile& f)
{
    DiskFsEntry e;
    e.path = f.path;
    e.name = f.name;
    e.directory = f.directory;
    e.size = f.length;
    e.start = f.directory ? 0 : f.load;
    e.attributes = f.Attributes();
    e.mtime = f.date ? FatVolumeReader::DosToUnix(f.date, f.time) : 0;
    e.error = f.error;
    e.fields = {{"status", f.status}, {"first", f.first}, {"special", f.special}, {"system", f.system ? 1 : 0}};
    return e;
}

struct IsdosDriver : DiskFs::Driver
{
    explicit IsdosDriver(IsdosFileSystem fs) : fs(std::move(fs)) {}

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>& notes, std::string& error) override
    {
        std::vector<IsdosFile> files;
        if (!fs.List(files, &notes, error))
            return false;
        for (const IsdosFile& f : files)
            out.push_back(IsdosEntry(f));
        return true;
    }

    bool List(const std::string& dir, bool recursive, std::vector<DiskFsEntry>& out, std::vector<std::string>& notes,
              std::string& error) override
    {
        if (recursive)
            return Driver::List(dir, recursive, out, notes, error);
        std::vector<IsdosFile> files;
        if (!fs.ListDirectory(dir, files, error))
            return false;
        for (const IsdosFile& f : files)
            out.push_back(IsdosEntry(f));
        return true;
    }

    bool Stat(const std::string& path, DiskFsEntry& out, std::vector<std::string>&, std::string& error) override
    {
        IsdosFile f;
        if (!fs.Find(path, f, error))
            return false;
        out = IsdosEntry(f);
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>*, std::string& error) override
    {
        IsdosFile f;
        return fs.Find(path, f, error) && fs.Read(f, data, error);
    }

    bool Write(const std::string& path, const std::vector<uint8_t>& data, const DiskFsWriteOptions& options,
               std::string& error) override
    {
        IsdosFileSystem::WriteOptions write;
        write.load = options.start;
        FatVolumeReader::UnixToDos(options.mtime, write.date, write.time);
        return fs.Write(path, data, write, error);
    }

    bool Delete(const std::string& path, std::string& error) override { return fs.Delete(path, error); }

    bool Rename(const std::string& path, const std::string& to, std::string& error) override
    {
        return fs.Rename(path, to, error);
    }

    bool MakeDirectory(const std::string& path, const DiskFsDirectoryOptions& options, std::string& error) override
    {
        IsdosFileSystem::WriteOptions write;
        FatVolumeReader::UnixToDos(options.mtime, write.date, write.time);
        write.continuous = options.continuous;
        write.blocks = options.blocks;
        return fs.MakeCatalogue(path, write, error);
    }

    bool SizedDirectories() const override { return true; }

    bool Deletable(const DiskFsEntry& entry, std::string& error) override { return fs.Deletable(entry.path, error); }

    std::string AttributeLetters() const override { return "RWHD"; }

    bool SetAttributes(const DiskFsEntry& entry, const std::string& letters, std::string& error) override
    {
        static const uint8_t bits[] = {IsdosFileSystem::kReadProtect, IsdosFileSystem::kWriteProtect, IsdosFileSystem::kHide,
                                       IsdosFileSystem::kDeleteProtect};
        const uint32_t chosen = Bits(letters, "RWHD");
        uint8_t status = 0;
        for (int k = 0; k < 4; k++)
            if (chosen & (1u << k))
                status |= bits[k];
        return fs.SetAttributes(entry.path, status, error);
    }

    bool SetTime(const std::string& path, int64_t mtime, std::string& error) override
    {
        uint16_t date = 0, time = 0;
        FatVolumeReader::UnixToDos(mtime, date, time);
        return fs.SetTime(path, date, time, error);
    }

    bool SetLabel(const std::string& label, int64_t, std::string& error) override { return fs.SetTitle(label, error); }
    bool HasTimes() const override { return true; }
    bool HasLabel() const override { return true; }
    std::string Label() const override { return fs.Boot().title; }

    IsdosFileSystem fs;
};

std::string FatError(FatFs::Error error)
{
    switch (error)
    {
        case FatFs::Error::NotFound: return "not found";
        case FatFs::Error::Exists: return "there already";
        case FatFs::Error::NoSpace: return "the volume (or the root directory) is full";
        case FatFs::Error::ReadOnly: return "read only (the device, or the entry's read-only attribute)";
        case FatFs::Error::Io: return "a sector does not read or write";
        case FatFs::Error::InvalidName: return "not a FAT name";
        case FatFs::Error::NotEmpty: return "the directory is not empty";
        case FatFs::Error::NotDirectory: return "not a directory";
        case FatFs::Error::IsDirectory: return "a directory";
        case FatFs::Error::Damaged: return "a cluster chain leaves the volume or loops";
        default: return "failed";
    }
}

/// "A/B/C.TXT" -> "A/B", "C.TXT"
void SplitPath(const std::string& path, std::string& dir, std::string& name)
{
    const size_t slash = path.rfind('/');
    dir = slash == std::string::npos ? std::string() : path.substr(0, slash);
    name = slash == std::string::npos ? path : path.substr(slash + 1);
}

struct FatDriver : DiskFs::Driver
{
    std::shared_ptr<BlockDevice> device;  ///< the volume's device (a floppy view, a partition window, the disk)
    CodePage page = CodePage::Cp866;
    FatVolumeReader reader;

    /// A FatFs on the device for one change (`change` may set the reason itself); the reader opens again after it
    /// (its tables are cached)
    template <typename Change>
    bool Edit(const std::string& what, std::string& error, Change change)
    {
        FatFs fs(*device, page);
        std::string why;
        if (!fs.Open(&why))
        {
            error = "the FAT volume does not open: " + why;
            return false;
        }
        error.clear();
        const bool ok = change(fs);
        if (!ok && error.empty())
            error = what + ": " + FatError(fs.LastError());
        reader.Open(*device, page);
        return ok;
    }

    bool Write(const std::string& path, const std::vector<uint8_t>& data, const DiskFsWriteOptions& options,
               std::string& error) override
    {
        if (data.size() > UINT32_MAX)
        {
            error = "a FAT file holds less than 4 GiB";
            return false;
        }
        std::string dir, name;
        SplitPath(path, dir, name);
        return Edit(path, error, [&](FatFs& fs) {
            uint16_t date = 0, time = 0;
            FatVolumeReader::UnixToDos(options.mtime, date, time);
            fs.SetClock(date, time);
            FatNode parent, file;
            if (!fs.Resolve("/" + dir, parent))
                return false;
            const bool exists = fs.Find(parent, name, file);
            if (exists && file.IsDirectory())
            {
                error = path + " is a directory";
                return false;
            }
            // Room first: a file replaced gives its clusters back
            const uint64_t cluster = fs.ClusterBytes();
            const uint64_t needed = (data.size() + cluster - 1) / cluster;
            const uint64_t had = exists ? (file.size + cluster - 1) / cluster : 0;
            if (needed > fs.FreeClusters() + had)
            {
                error = path + ": the volume has " + std::to_string((fs.FreeClusters() + had) * cluster) + " bytes free, the file needs " +
                        std::to_string(data.size());
                return false;
            }
            if (exists ? !fs.Resize(file, 0) : !fs.Create(parent, name, false, file))
                return false;
            size_t written = 0;
            return data.empty() || fs.Write(file, 0, data.data(), data.size(), written);
        });
    }

    bool Delete(const std::string& path, std::string& error) override
    {
        return Edit(path, error, [&](FatFs& fs) {
            FatNode node;
            return fs.Resolve("/" + path, node) && fs.Remove(node);
        });
    }

    bool Deletable(const DiskFsEntry& entry, std::string& error) override
    {
        if (entry.attributes.empty() || entry.attributes[0] != 'R')
            return true;
        error = entry.path + " is read only";
        return false;
    }

    bool MakeDirectory(const std::string& path, const DiskFsDirectoryOptions& options, std::string& error) override
    {
        std::string dir, name;
        SplitPath(path, dir, name);
        return Edit(path, error, [&](FatFs& fs) {
            uint16_t date = 0, time = 0;
            FatVolumeReader::UnixToDos(options.mtime, date, time);
            fs.SetClock(date, time);
            FatNode parent, made;
            return fs.Resolve("/" + dir, parent) && fs.Create(parent, name, true, made);
        });
    }

    bool Rename(const std::string& path, const std::string& to, std::string& error) override
    {
        // A bare name stays in the file's directory; a path moves it
        std::string dir, name, from, ignored;
        SplitPath(to, dir, name);
        SplitPath(path, from, ignored);
        if (to.find('/') == std::string::npos)
            dir = from;
        if (!dir.empty() && dir.front() == '/')
            dir.erase(0, 1);  // "/name": the root
        // A directory does not go into itself
        if (dir == path || dir.compare(0, path.size() + 1, path + "/") == 0)
        {
            error = "a directory cannot move into itself (" + path + " -> " + to + ")";
            return false;
        }
        return Edit(path + " -> " + to, error, [&](FatFs& fs) {
            FatNode node, parent, moved;
            return fs.Resolve("/" + path, node) && fs.Resolve("/" + dir, parent) && fs.Rename(node, parent, name, moved);
        });
    }

    std::string AttributeLetters() const override { return "RHSA"; }

    bool SetAttributes(const DiskFsEntry& entry, const std::string& letters, std::string& error) override
    {
        const uint32_t chosen = Bits(letters, "RHSA");
        const uint8_t bits = static_cast<uint8_t>((chosen & 7u) | ((chosen & 8u) ? FatNode::kArchive : 0));
        return Edit(entry.path, error, [&](FatFs& fs) {
            FatNode node;
            return fs.Resolve("/" + entry.path, node) && fs.SetAttributes(node, bits);
        });
    }

    bool SetTime(const std::string& path, int64_t mtime, std::string& error) override
    {
        uint16_t date = 0, time = 0;
        FatVolumeReader::UnixToDos(mtime, date, time);
        return Edit(path, error, [&](FatFs& fs) {
            FatNode node;
            return fs.Resolve("/" + path, node) && fs.SetTime(node, date, time);
        });
    }

    bool SetLabel(const std::string& label, int64_t mtime, std::string& error) override
    {
        return Edit("the label", error, [&](FatFs& fs) {
            uint16_t date = 0, time = 0;
            FatVolumeReader::UnixToDos(mtime, date, time);
            fs.SetClock(date, time);
            if (fs.SetLabel(label))
                return true;
            if (fs.LastError() == FatFs::Error::InvalidName)
                error = "a FAT label has up to 11 characters: letters, digits, blanks (not in front) and ! # $ % & ' ( ) - @ ^ _ ` { } ~ (" +
                        label + ")";
            return false;
        });
    }

    bool HasTimes() const override { return true; }
    bool HasLabel() const override { return true; }
    std::string Label() const override { return reader.Label(); }

    DiskFsEntry Entry(const FatDirEntryInfo& info, const std::string& dir) const
    {
        DiskFsEntry e;
        e.name = info.name;
        e.path = dir.empty() ? info.name : dir + "/" + info.name;
        e.directory = info.isDirectory;
        e.size = info.size;
        static const char letters[] = "RHSVDA";
        for (int bit = 0; bit < 6; bit++)
            e.attributes += (info.attributes & (1 << bit)) ? letters[bit] : '.';
        e.mtime = info.date ? FatVolumeReader::DosToUnix(info.date, info.time) : 0;
        e.fields = {{"cluster", info.firstCluster}, {"attributes", info.attributes}};
        return e;
    }

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>& notes, std::string& error) override
    {
        return List("", true, out, notes, error);
    }

    bool List(const std::string& dir, bool recursive, std::vector<DiskFsEntry>& out, std::vector<std::string>& notes,
              std::string& error) override
    {
        std::vector<FatDirEntryInfo> entries;
        if (!reader.List("/" + dir, entries, &error))
            return false;
        // Directory clusters already walked: a cross-linked or looping tree is cut
        std::set<uint32_t> seen;
        struct Level
        {
            std::vector<FatDirEntryInfo> entries;
            size_t next = 0;
            std::string dir;
        };
        std::vector<Level> stack;
        stack.push_back({std::move(entries), 0, dir});
        while (!stack.empty())
        {
            Level& level = stack.back();
            if (level.next >= level.entries.size())
            {
                stack.pop_back();
                continue;
            }
            const FatDirEntryInfo info = level.entries[level.next++];
            DiskFsEntry e = Entry(info, level.dir);
            const std::string path = e.path;
            const bool descend = recursive && info.isDirectory;
            if (descend && (info.firstCluster < 2 || !seen.insert(info.firstCluster).second || stack.size() > 32))
            {
                e.error = "directory cluster " + std::to_string(info.firstCluster) + " already walked";
                notes.push_back(path + ": " + e.error);
                out.push_back(std::move(e));
                continue;
            }
            std::vector<FatDirEntryInfo> sub;
            std::string why;
            if (descend && !reader.ListDirectory(info.firstCluster, sub, &why))
                e.error = why;
            out.push_back(std::move(e));
            if (descend && !sub.empty())
                stack.push_back({std::move(sub), 0, path});
        }
        return true;
    }

    bool Stat(const std::string& path, DiskFsEntry& out, std::vector<std::string>&, std::string& error) override
    {
        FatDirEntryInfo info;
        if (!reader.Stat("/" + path, info, &error))
            return false;
        const size_t slash = path.rfind('/');
        out = Entry(info, slash == std::string::npos ? "" : path.substr(0, slash));
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>*, std::string& error) override
    {
        return reader.ReadFile("/" + path, data, &error);
    }
};

std::string FatName(FatReaderType type)
{
    switch (type)
    {
        case FatReaderType::Fat12: return "fat12";
        case FatReaderType::Fat16: return "fat16";
        default: return "fat32";
    }
}

/// A FAT volume on `device` (its sector 0 a boot sector or an MBR whose first FAT partition is the volume)
std::unique_ptr<FatDriver> OpenFat(std::shared_ptr<BlockDevice> device, CodePage page, DiskFsVolume& volume)
{
    auto driver = std::make_unique<FatDriver>();
    driver->device = std::move(device);
    driver->page = page;
    if (!driver->reader.Open(*driver->device, page))
        return nullptr;
    volume.fs = FatName(driver->reader.Type());
    volume.system = volume.fs == "fat12" ? "FAT12" : volume.fs == "fat16" ? "FAT16" : "FAT32";
    volume.label = driver->reader.Label();
    volume.bytes = static_cast<uint64_t>(driver->reader.VolumeSectors()) * kSector;
    volume.writable = driver->device->IsWritable();
    std::vector<bool> free;
    if (driver->reader.ScanFree(free))
        volume.freeBytes = static_cast<int64_t>(std::count(free.begin(), free.end(), true)) * driver->reader.SectorsPerCluster() * kSector;
    return driver;
}

struct CpmDriver : DiskFs::Driver
{
    explicit CpmDriver(CpmFileSystem fs) : fs(std::move(fs)) {}

    /// The name as listed: a name of eleven blanks (CP/M keeps one) as "\x20", the escape of a blank
    static std::string NameOf(const CpmFile& f)
    {
        const std::string name = Ascii(f.FullName());
        return name.empty() ? "\\x20" : name;
    }

    /// User 0 at the root, users 1-15 in the directories USER1..USER15
    static std::string PathOf(const CpmFile& f)
    {
        const std::string name = NameOf(f);
        return f.user ? "USER" + std::to_string(f.user) + "/" + name : name;
    }

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>&, std::string& error) override
    {
        std::vector<CpmFile> files;
        if (!fs.List(files, error))
            return false;
        for (uint8_t user = 0; user < 16; user++)
        {
            bool listed = false;
            for (const CpmFile& f : files)
            {
                if (f.user != user)
                    continue;
                if (user && !listed)
                {
                    DiskFsEntry d;
                    d.name = d.path = "USER" + std::to_string(user);
                    d.directory = true;
                    d.fields = {{"user", user}};
                    out.push_back(std::move(d));
                }
                listed = true;
                DiskFsEntry e;
                e.path = PathOf(f);
                e.name = NameOf(f);
                e.size = f.size;
                static const char letters[] = "RSA";
                for (int bit = 0; bit < 3; bit++)
                    e.attributes += (f.attributes & (1 << bit)) ? letters[bit] : '.';
                e.fields = {{"user", f.user}, {"extents", f.extents}, {"block", f.firstBlock}};
                out.push_back(std::move(e));
            }
        }
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>*, std::string& error) override
    {
        std::vector<CpmFile> files;
        if (!fs.List(files, error))
            return false;
        for (int pass = 0; pass < 2; pass++)
            for (const CpmFile& f : files)
                if (pass == 0 ? PathOf(f) == path : Lower(PathOf(f)) == Lower(path))
                    return fs.Read(f, data, error);
        error = "no " + path + " on the CP/M volume";
        return false;
    }

    /// "USERn/NAME.TYP" (n 1-15) or "NAME.TYP" of user 0 into its parts, the name upper-cased
    static bool Parse(const std::string& path, uint8_t& user, std::string& name, std::string& type, std::string& error)
    {
        std::string file = path.substr(path.front() == '/');
        user = 0;
        const size_t slash = file.find('/');
        if (slash != std::string::npos)
        {
            const std::string dir = Upper(file.substr(0, slash));
            const bool digits = dir.size() >= 5 && dir.size() <= 6 && dir.compare(0, 4, "USER") == 0 && dir[4] != '0' &&
                                dir.find_first_not_of("0123456789", 4) == std::string::npos;
            const int n = digits ? std::stoi(dir.substr(4)) : 0;
            if (n < 1 || n > 15)
            {
                error = "an ATM CP/M directory is a user area USER1..USER15 (" + path + ")";
                return false;
            }
            user = static_cast<uint8_t>(n);
            file = file.substr(slash + 1);
        }
        file = Upper(file);
        const size_t dot = file.find('.');
        name = file.substr(0, dot);
        type = dot == std::string::npos ? std::string() : file.substr(dot + 1);
        if (!CpmFileSystem::ValidName(name, type) || (dot != std::string::npos && type.empty()))
        {
            error =
                "not a CP/M name NAME.TYP (1-8 and 0-3 characters, no blank, none of < > . , ; : = ? * [ ]): " + path;
            return false;
        }
        return true;
    }

    bool Find(const std::string& path, CpmFile& out, std::string& error)
    {
        std::vector<CpmFile> files;
        if (!fs.List(files, error))
            return false;
        for (int pass = 0; pass < 2; pass++)
            for (const CpmFile& f : files)
                if (pass == 0 ? PathOf(f) == path : Lower(PathOf(f)) == Lower(path))
                {
                    out = f;
                    return true;
                }
        error = "no " + path + " on the CP/M volume";
        return false;
    }

    /// A user area: "USERn"
    static bool UserArea(const std::string& path, uint8_t& user)
    {
        std::string name, type, error;
        return path.find('/') == std::string::npos && Parse(path + "/X", user, name, type, error);
    }

    bool Write(const std::string& path, const std::vector<uint8_t>& data, const DiskFsWriteOptions&,
               std::string& error) override
    {
        uint8_t user = 0;
        std::string name, type;
        return Parse(path, user, name, type, error) && fs.Write(user, name, type, data, error);
    }

    bool Delete(const std::string& path, std::string& error) override
    {
        // A user area goes with its last file
        uint8_t user = 0;
        if (UserArea(path, user))
        {
            std::vector<CpmFile> files;
            if (!fs.List(files, error))
                return false;
            for (const CpmFile& f : files)
                if (f.user == user)
                {
                    error = path + " is not empty";
                    return false;
                }
            return true;
        }
        CpmFile f;
        return Find(path, f, error) && fs.Delete(f, error);
    }

    bool Deletable(const DiskFsEntry& entry, std::string& error) override
    {
        if (entry.directory || entry.attributes.find('R') == std::string::npos)
            return true;
        error = entry.path + " is read only";
        return false;
    }

    bool Rename(const std::string& path, const std::string& to, std::string& error) override
    {
        CpmFile f;
        if (!Find(path, f, error))
            return false;
        // A bare name keeps the user area; a path goes from the root
        const bool fromRoot = to.find('/') != std::string::npos;
        uint8_t user = 0;
        std::string name, type;
        if (!Parse(to, user, name, type, error))
            return false;
        return fs.Rename(f, fromRoot ? user : f.user, name, type, error);
    }

    std::string AttributeLetters() const override { return "RSA"; }

    bool SetAttributes(const DiskFsEntry& entry, const std::string& letters, std::string& error) override
    {
        CpmFile f;
        return Find(entry.path, f, error) && fs.SetAttributes(f, static_cast<uint8_t>(Bits(letters, "RSA")), error);
    }

    CpmFileSystem fs;
};

void DescribeCpm(const CpmFileSystem& fs, DiskFsVolume& volume, const char* system = "ATM CP/M")
{
    volume.fs = "cpm";
    volume.system = system;
    uint32_t free = 0;
    if (fs.FreeBlocks(free))
        volume.freeBytes = static_cast<int64_t>(free) * CpmGeometry::kBlock;
}

/// The bytes of an ATM CP/M hard disk partition from its directory on: the BIOS's cylinder, head and sector of the
/// channel's geometry, at the disk's own (`heads` x `sectors`)
std::function<uint64_t(uint64_t)> CpmLba(const AtmCpmChannel& channel, uint64_t heads, uint64_t sectors)
{
    const uint64_t directory = static_cast<uint64_t>(channel.reserved) * channel.sectors;
    const uint64_t perTrack = channel.sectors, partHeads = channel.heads, start = channel.startCylinder;
    return [directory, perTrack, partHeads, start, heads, sectors](uint64_t offset) {
        const uint64_t logical = directory + offset / kSector;
        const uint64_t track = logical / perTrack;
        return ((start + track / partHeads) * heads + track % partHeads) * sectors + logical % perTrack;
    };
}

CpmFileSystem::ByteReader CpmReaderAt(BlockDevice& device, const AtmCpmChannel& channel, uint64_t heads,
                                      uint64_t sectors, std::shared_ptr<SectorCache> cache)
{
    return [&device, lbaOf = CpmLba(channel, heads, sectors), cache](uint64_t offset, uint8_t* dst, uint32_t size) {
        while (size > 0)
        {
            const uint64_t lba = lbaOf(offset);
            if (lba != cache->lba)
            {
                if (!device.ReadSector(lba, cache->sector))
                {
                    cache->lba = UINT64_MAX;
                    return false;
                }
                cache->lba = lba;
            }
            const uint32_t n = std::min<uint32_t>(size, static_cast<uint32_t>(kSector - offset % kSector));
            std::memcpy(dst, cache->sector + offset % kSector, n);
            dst += n;
            offset += n;
            size -= n;
        }
        return true;
    };
}

/// The writer of CpmReaderAt: each sector read, patched and written back, the reader's cache kept current
CpmFileSystem::ByteWriter CpmWriterAt(BlockDevice& device, const AtmCpmChannel& channel, uint64_t heads,
                                      uint64_t sectors, std::shared_ptr<SectorCache> cache)
{
    return [&device, lbaOf = CpmLba(channel, heads, sectors), cache](uint64_t offset, const uint8_t* src,
                                                                     uint32_t size) {
        while (size > 0)
        {
            const uint64_t lba = lbaOf(offset);
            const uint32_t n = std::min<uint32_t>(size, static_cast<uint32_t>(kSector - offset % kSector));
            uint8_t sector[kSector];
            cache->lba = UINT64_MAX;
            if (n < kSector && !device.ReadSector(lba, sector))
                return false;
            std::memcpy(sector + offset % kSector, src, n);
            if (!device.WriteSector(lba, sector))
                return false;
            src += n;
            offset += n;
            size -= n;
        }
        return true;
    };
}

/// The bytes of a Profi CP/M hard disk partition from sector `base` on. The Profi's IDE port reads the high byte of
/// each word first, so a dump of its disk has the two bytes of every word swapped
CpmFileSystem::ByteReader ProfiReaderAt(BlockDevice& device, uint64_t base, std::shared_ptr<SectorCache> cache)
{
    return [&device, base, cache](uint64_t offset, uint8_t* dst, uint32_t size) {
        while (size > 0)
        {
            const uint64_t lba = base + offset / kSector;
            if (lba != cache->lba)
            {
                uint8_t raw[kSector];
                if (!device.ReadSector(lba, raw))
                {
                    cache->lba = UINT64_MAX;
                    return false;
                }
                for (uint32_t i = 0; i < kSector; i++)
                    cache->sector[i] = raw[i ^ 1];
                cache->lba = lba;
            }
            const uint32_t n = std::min<uint32_t>(size, static_cast<uint32_t>(kSector - offset % kSector));
            std::memcpy(dst, cache->sector + offset % kSector, n);
            dst += n;
            offset += n;
            size -= n;
        }
        return true;
    };
}

/// The writer of ProfiReaderAt: each sector read, patched and written back swapped, the reader's cache dropped
CpmFileSystem::ByteWriter ProfiWriterAt(BlockDevice& device, uint64_t base, std::shared_ptr<SectorCache> cache)
{
    return [&device, base, cache](uint64_t offset, const uint8_t* src, uint32_t size) {
        while (size > 0)
        {
            const uint64_t lba = base + offset / kSector;
            const uint32_t n = std::min<uint32_t>(size, static_cast<uint32_t>(kSector - offset % kSector));
            uint8_t raw[kSector], sector[kSector];
            cache->lba = UINT64_MAX;
            if (n < kSector && !device.ReadSector(lba, raw))
                return false;
            for (uint32_t i = 0; i < kSector && n < kSector; i++)
                sector[i] = raw[i ^ 1];
            std::memcpy(sector + offset % kSector, src, n);
            for (uint32_t i = 0; i < kSector; i++)
                raw[i] = sector[i ^ 1];
            if (!device.WriteSector(lba, raw))
                return false;
            src += n;
            offset += n;
            size -= n;
        }
        return true;
    };
}

/// A partition of the Profi hard disk table (FORMATHD.COM, HDD22.DRV): cylinders of the header's geometry
struct ProfiPartition
{
    std::string name;
    uint32_t startCylinder = 0, cylinders = 0, entries = 0;
};

/// The Profi hard disk header at cylinder 1, head 0, sector 1 (LBA heads x sectors) and its partition table in the
/// next four sectors, read swapped: words H, S, C - 2, 0, 0, H x S, then the partition count, the active partition,
/// the free cylinders and "ProfiHDD"; a partition entry of 32 bytes is the name (16 bytes, zero padded), 0, #AA, the
/// start cylinder, the cylinders, the size in KB and the directory entries
bool FindProfiDisk(BlockDevice& device, uint64_t& heads, uint64_t& sectors, std::vector<ProfiPartition>& partitions)
{
    const auto word = [](const uint8_t* p) { return static_cast<uint32_t>(p[0] | p[1] << 8); };
    std::set<uint64_t> tried;
    for (uint64_t h = 1; h <= 16; h++)
        for (uint64_t s = 1; s <= 63; s++)
        {
            const uint64_t lba = h * s;
            uint8_t raw[kSector], header[kSector];
            if (!tried.insert(lba).second || lba + 5 > device.SectorCount() || !device.ReadSector(lba, raw))
                continue;
            for (uint32_t i = 0; i < kSector; i++)
                header[i] = raw[i ^ 1];
            if (std::memcmp(header + 16, "ProfiHDD", 8) != 0 || word(header) * word(header + 2) != lba ||
                word(header + 10) != lba)
                continue;
            heads = word(header);
            sectors = word(header + 2);
            const uint32_t count = std::min<uint32_t>(header[12], 64);
            partitions.clear();
            uint8_t table[4 * kSector];
            for (uint32_t k = 0; k < 4; k++)
            {
                if (!device.ReadSector(lba + 1 + k, raw))
                    return false;
                for (uint32_t i = 0; i < kSector; i++)
                    table[k * kSector + i] = raw[i ^ 1];
            }
            for (uint32_t n = 0; n < count; n++)
            {
                const uint8_t* e = table + n * 32;
                if (e[16] != 0 || e[17] != 0xAA)
                    continue;
                ProfiPartition p;
                p.name.assign(reinterpret_cast<const char*>(e), strnlen(reinterpret_cast<const char*>(e), 16));
                p.startCylinder = word(e + 18);
                p.cylinders = word(e + 20);
                p.entries = word(e + 24);
                partitions.push_back(p);
            }
            return true;
        }
    return false;
}

/// The tree of a BS-DOS disk, MB-02 (Mb02FileSystem) or MB-01 (Mb01FileSystem): its directories by number with a
/// parent each, the files of a directory by entry; both write the same way
template <typename Fs>
struct BsdosTree : DiskFs::Driver
{
    BsdosTree(Fs fs, const char* system) : fs(std::move(fs)), system(system) {}

    /// The name bytes as Ascii gives them, the path separators as \xNN too
    static std::string NameOf(const std::string& raw)
    {
        std::string out;
        for (const char c : raw)
            out += c == '/' ? "\\x2f" : c == '\\' ? "\\x5c" : Ascii(std::string(1, c));
        return out;
    }

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>& notes, std::string& error) override
    {
        std::vector<Mb02Directory> dirs;
        if (!fs.Directories(dirs, error))
            return false;
        // The tree through the parent bytes: a directory whose parent is missing, itself or its own descendant hangs
        // under the root
        std::vector<int> parent(256, -1);
        for (const Mb02Directory& d : dirs)
            parent[d.number] = d.number ? d.parent : -1;
        for (const Mb02Directory& d : dirs)
        {
            if (d.number == 0)
                continue;
            int p = d.parent;
            for (int steps = 0; p > 0 && p != d.number && parent[p] >= 0 && steps < 256; steps++)
                p = parent[p];
            if (p != 0)
            {
                notes.push_back("directory " + std::to_string(d.number) +
                                " has no parent on the disk: listed at the root");
                parent[d.number] = 0;
            }
        }
        Emit(0, "", dirs, parent, out, notes, 0);
        return true;
    }

    void Emit(uint8_t number, const std::string& prefix, const std::vector<Mb02Directory>& dirs,
              const std::vector<int>& parent, std::vector<DiskFsEntry>& out, std::vector<std::string>& notes, int depth)
    {
        std::vector<Mb02File> files;
        std::string error;
        if (!fs.List(number, files, error))
        {
            notes.push_back("directory " + std::to_string(number) + ": " + error);
            return;
        }
        std::set<std::string> names;
        // A later file of a name already there gets ~<entry>, one without a header ~<entry> alone
        auto unique = [&names](std::string name, const std::string& suffix) {
            if (name.empty() || !names.insert(Lower(name)).second)
            {
                name += suffix;
                names.insert(Lower(name));
            }
            return name;
        };
        for (const Mb02File& f : files)
        {
            DiskFsEntry e;
            e.name = unique(NameOf(f.name), "~" + std::to_string(f.entry));
            e.path = prefix + e.name;
            e.size = f.length;
            e.start = f.param1;
            if (f.date)
                e.mtime = FatVolumeReader::DosToUnix(f.date, f.time);
            e.fields = {{"type", f.type},    {"param2", f.param2}, {"address", f.address},      {"entry", f.entry},
                        {"sector", f.first}, {"flags", f.flags},   {"attributes", f.attributes}};
            out.push_back(std::move(e));
        }
        if (depth >= 256)
            return;
        for (const Mb02Directory& d : dirs)
        {
            if (d.number == 0 || parent[d.number] != number)
                continue;
            DiskFsEntry e;
            e.name = unique(NameOf(d.name), "~D" + std::to_string(d.number));
            e.path = prefix + e.name;
            e.directory = true;
            if (d.date)
                e.mtime = FatVolumeReader::DosToUnix(d.date, d.time);
            e.fields = {{"dir", d.number}, {"sector", d.first}};
            const std::string path = e.path;
            out.push_back(std::move(e));
            Emit(d.number, path + "/", dirs, parent, out, notes, depth + 1);
        }
    }

    /// The DIRS number of directory `dir` ("" the root)
    bool DirNumber(const std::string& dir, uint8_t& number, std::string& error)
    {
        number = 0;
        if (dir.empty())
            return true;
        std::vector<std::string> notes;
        DiskFsEntry entry;
        if (!Stat(dir, entry, notes, error))
            return false;
        if (!entry.directory)
        {
            error = dir + " is no directory";
            return false;
        }
        number = static_cast<uint8_t>(entry.Field("dir"));
        return true;
    }

    /// The file `path` names (as listed: a later file of a name with ~<entry>)
    bool FileOf(const std::string& path, Mb02File& out, std::string& error)
    {
        std::vector<std::string> notes;
        DiskFsEntry entry;
        if (!Stat(path, entry, notes, error))
            return false;
        if (entry.directory)
        {
            error = path + " is a directory";
            return false;
        }
        std::string dir, name;
        SplitPath(entry.path, dir, name);
        uint8_t number = 0;
        std::vector<Mb02File> files;
        if (!DirNumber(dir, number, error) || !fs.List(number, files, error))
            return false;
        for (const Mb02File& f : files)
            if (f.entry == entry.Field("entry"))
            {
                out = f;
                return true;
            }
        error = "no " + path + " on the " + std::string(system) + " volume";
        return false;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>*, std::string& error) override
    {
        Mb02File f;
        return FileOf(path, f, error) && fs.Read(f, data, error);
    }

    static void Dos(int64_t mtime, uint16_t& date, uint16_t& time)
    {
        date = time = 0;
        if (mtime)
            FatVolumeReader::UnixToDos(mtime, date, time);
    }

    bool Write(const std::string& path, const std::vector<uint8_t>& data, const DiskFsWriteOptions& options,
               std::string& error) override
    {
        std::string dir, name;
        SplitPath(path, dir, name);
        uint8_t number = 0;
        if (!DirNumber(dir, number, error))
            return false;
        typename Fs::WriteOptions write;
        write.start = options.start;
        write.type = static_cast<uint8_t>(options.basicType >= 0 ? options.basicType : 3);
        write.param2 = static_cast<uint16_t>(options.param2 >= 0  ? options.param2
                                             : write.type == 0 ? std::min<size_t>(data.size(), 0xFFFF)
                                                               : 0x8000);
        Dos(options.mtime, write.date, write.time);
        return fs.Write(number, Unescape(name), data, write, error);
    }

    std::string BasicTypes() const override { return "03"; }
    bool TakesParam2() const override { return true; }

    bool Delete(const std::string& path, std::string& error) override
    {
        std::vector<std::string> notes;
        DiskFsEntry entry;
        if (!Stat(path, entry, notes, error))
            return false;
        if (entry.directory)
            return fs.DeleteDirectory(static_cast<uint8_t>(entry.Field("dir")), error);
        Mb02File f;
        return FileOf(path, f, error) && fs.Delete(f, error);
    }

    /// A bare name stays in its directory; a path (with '/') is from the root
    bool Rename(const std::string& path, const std::string& to, std::string& error) override
    {
        std::vector<std::string> notes;
        DiskFsEntry entry;
        if (!Stat(path, entry, notes, error))
            return false;
        std::string fromDir, fromName, toDir, toName;
        SplitPath(entry.path, fromDir, fromName);
        if (to.find('/') == std::string::npos)
        {
            toDir = fromDir;
            toName = to;
        }
        else
            SplitPath(to.substr(to.front() == '/'), toDir, toName);
        uint8_t number = 0;
        if (!DirNumber(toDir, number, error))
            return false;
        if (entry.directory)
            return fs.RenameDirectory(static_cast<uint8_t>(entry.Field("dir")), number, Unescape(toName), error);
        Mb02File f;
        return FileOf(path, f, error) && fs.Rename(f, number, Unescape(toName), error);
    }

    bool MakeDirectory(const std::string& path, const DiskFsDirectoryOptions& options, std::string& error) override
    {
        std::string dir, name;
        SplitPath(path, dir, name);
        uint8_t number = 0;
        uint16_t date = 0, time = 0;
        Dos(options.mtime, date, time);
        return DirNumber(dir, number, error) && fs.MakeDirectory(number, Unescape(name), date, time, error);
    }

    bool SetTime(const std::string& path, int64_t mtime, std::string& error) override
    {
        std::vector<std::string> notes;
        DiskFsEntry entry;
        uint16_t date = 0, time = 0;
        Dos(mtime, date, time);
        if (!Stat(path, entry, notes, error))
            return false;
        if (entry.directory)
            return fs.SetDirectoryTime(static_cast<uint8_t>(entry.Field("dir")), date, time, error);
        Mb02File f;
        return FileOf(path, f, error) && fs.SetTime(f, date, time, error);
    }

    bool SetLabel(const std::string& label, int64_t, std::string& error) override { return fs.SetName(label, error); }
    bool HasTimes() const override { return true; }
    bool HasLabel() const override { return true; }
    std::string Label() const override { return Ascii(fs.Boot().name); }

    Fs fs;
    const char* system;
};

struct Mb01Driver : BsdosTree<Mb01FileSystem>
{
    explicit Mb01Driver(Mb01FileSystem fs) : BsdosTree(std::move(fs), "MB-01") {}
};

struct Mb02Driver : BsdosTree<Mb02FileSystem>
{
    explicit Mb02Driver(Mb02FileSystem fs) : BsdosTree(std::move(fs), "MB-02") {}
};

struct MdosDriver : DiskFs::Driver
{
    explicit MdosDriver(MdosFileSystem fs) : fs(std::move(fs)) {}

    /// "name.T" as TR-DOS names go: the type letter after the dot, the path separators as \\xNN too
    static std::string PathOf(const MdosFile& f)
    {
        std::string out;
        for (const char c : f.name + "." + std::string(1, f.type))
            out += c == '/' ? "\\x2f" : c == '\\' ? "\\x5c" : Ascii(std::string(1, c));
        return out;
    }

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>&, std::string& error) override
    {
        std::vector<MdosFile> files;
        if (!fs.List(files, error))
            return false;
        std::set<std::string> names;
        for (const MdosFile& f : files)
        {
            DiskFsEntry e;
            e.name = e.path = PathOf(f);
            // A later file of a name already there: ~<slot> before the type
            if (!names.insert(Lower(e.path)).second)
            {
                MdosFile renamed = f;
                renamed.name += "~" + std::to_string(f.slot);
                e.name = e.path = PathOf(renamed);
                names.insert(Lower(e.path));
            }
            e.size = f.length;
            e.start = f.start;
            static const char letters[] = "HSPARWED";
            for (int bit = 7; bit >= 0; bit--)
                e.attributes += (f.attributes & (1 << bit)) ? letters[7 - bit] : '.';
            e.fields = {{"type", static_cast<uint8_t>(f.type)},
                        {"param2", f.param2},
                        {"sector", f.first},
                        {"slot", f.slot},
                        {"extra", f.extra}};
            out.push_back(std::move(e));
        }
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::vector<uint8_t>*, std::string& error) override
    {
        std::vector<std::string> notes;
        DiskFsEntry entry;
        if (!Stat(path, entry, notes, error))
            return false;
        std::vector<MdosFile> files;
        if (!fs.List(files, error))
            return false;
        for (const MdosFile& f : files)
            if (f.slot == entry.Field("slot"))
                return fs.Read(f, data, error);
        error = "no " + path + " on the MDOS volume";
        return false;
    }

    /// "name.T" (\\xNN escapes as PathOf writes them) -> the name bytes and the type
    static bool Parse(const std::string& path, std::string& name, char& type, std::string& error)
    {
        const size_t dot = path.rfind('.');
        const std::string tail = dot == std::string::npos ? std::string() : Unescape(path.substr(dot + 1));
        if (dot == std::string::npos || dot == 0 || tail.size() != 1 || path.find('/') != std::string::npos)
        {
            error = "an MDOS file is name.T: at most 10 characters and the type letter (" + path + ")";
            return false;
        }
        type = tail[0];
        name = Unescape(path.substr(0, dot));
        return true;
    }

    /// The directory entry `path` names (as listed: a later file of a name with ~<slot>)
    bool Entry(const std::string& path, MdosFile& out, std::string& error)
    {
        std::vector<std::string> notes;
        DiskFsEntry entry;
        if (!Stat(path, entry, notes, error))
            return false;
        std::vector<MdosFile> files;
        if (!fs.List(files, error))
            return false;
        for (const MdosFile& f : files)
            if (f.slot == entry.Field("slot"))
            {
                out = f;
                return true;
            }
        error = "no " + path + " on the MDOS volume";
        return false;
    }

    bool Write(const std::string& path, const std::vector<uint8_t>& data, const DiskFsWriteOptions& options,
               std::string& error) override
    {
        std::string name;
        char type = 0;
        if (!Parse(path, name, type, error))
            return false;
        MdosFileSystem::WriteOptions write;
        write.start = options.start;
        // A program: its length without variables; else as a tape header's CODE
        write.param2 = static_cast<uint16_t>(options.param2 >= 0 ? options.param2
                                             : type == 'P'        ? std::min<size_t>(data.size(), 0xFFFF)
                                                                  : 0x8000);
        return fs.Write(name, type, data, write, error);
    }

    bool TakesParam2() const override { return true; }

    bool Delete(const std::string& path, std::string& error) override
    {
        MdosFile f;
        return Entry(path, f, error) && fs.Delete(f, error);
    }

    bool Deletable(const DiskFsEntry& entry, std::string& error) override
    {
        if (entry.attributes.find('D') != std::string::npos)
            return true;
        error = entry.path + " may not be deleted (no D attribute)";
        return false;
    }

    bool Rename(const std::string& path, const std::string& to, std::string& error) override
    {
        MdosFile f;
        std::string name;
        char type = 0;
        return Entry(path, f, error) && Parse(to.substr(to.front() == '/'), name, type, error) &&
               fs.Rename(f, name, type, error);
    }

    std::string AttributeLetters() const override { return "HSPARWED"; }

    bool SetAttributes(const DiskFsEntry& entry, const std::string& letters, std::string& error) override
    {
        MdosFile f;
        if (!Entry(entry.path, f, error))
            return false;
        // HSPARWED from bit 7 down: Bits counts from the first letter up
        return fs.SetAttributes(f, static_cast<uint8_t>(Bits(letters, "DEWRAPSH")), error);
    }

    bool SetLabel(const std::string& label, int64_t, std::string& error) override
    {
        if (!setSlotName)
            return fs.SetName(label, error);
        if (!setSlotName(label, error))
            return false;
        slotName = label;
        return true;
    }
    bool HasLabel() const override { return true; }
    std::string Label() const override { return Ascii(setSlotName ? slotName : fs.Boot().name); }

    MdosFileSystem fs;
    /// An MDOS3 slot: its name in the info sector is the volume's
    std::string slotName;
    std::function<bool(const std::string&, std::string&)> setSlotName;
};

void DescribeMdos(const MdosFileSystem& fs, DiskFsVolume& volume)
{
    volume.fs = "mdos";
    volume.system = "MDOS";
    volume.label = Ascii(fs.Boot().name);
    volume.bytes = static_cast<uint64_t>(fs.Boot().Sectors()) * MdosFileSystem::kSector;
    volume.freeBytes = static_cast<int64_t>(fs.FreeSectors()) * MdosFileSystem::kSector;
}

void DescribeIsdos(const IsdosFileSystem& fs, DiskFsVolume& volume)
{
    volume.fs = "isdos";
    volume.system = fs.Boot().chic ? "iS-DOS Chic" : "iS-DOS Classic";
    volume.label = fs.Boot().title;
    volume.bytes = static_cast<uint64_t>(fs.Boot().blocks) * 256;
    uint32_t free = 0;
    if (fs.FreeBlocks(free))
        volume.freeBytes = static_cast<int64_t>(free) * 256;
}
}  // namespace

/// endregion </Drivers>

DiskFs::DiskFs(DiskImage& floppy) : _floppy(&floppy) {}

DiskFs::DiskFs(BlockDevice& device, CodePage page) : _device(&device), _page(page) {}

DiskFs::~DiskFs() = default;

const std::vector<DiskFsVolume>& DiskFs::Volumes()
{
    Probe();
    return _volumes;
}

void DiskFs::Probe()
{
    if (_probed)
        return;
    _probed = true;
    auto add = [this](DiskFsVolume volume, std::unique_ptr<Driver> driver) {
        volume.attributes = driver->AttributeLetters();
        volume.times = driver->HasTimes();
        volume.labels = driver->HasLabel();
        _volumes.push_back(std::move(volume));
        _drivers.push_back(std::move(driver));
    };

    if (_floppy)
    {
        // ATM CP/M before TR-DOS: its reserved cylinder 0 often holds an empty TR-DOS catalogue, which is no volume
        // then
        // Profi CP/M: 1024-byte sectors (5 a track), the directory from cylinder 0 (Micco / Kondor MicroDOS) or
        // cylinder 2 (Klug CP/M); it ends the probe
        const DiskImage::Track* first = _floppy->getTrackForCylinderAndSide(0, 0);
        const DiskImage::Sector* one = first ? first->findSector(1) : nullptr;
        if (one && one->dataSize == 1024)
        {
            CpmFileSystem profi = CpmFileSystem::ProfiFloppy(*_floppy);
            // FORMAT.COM's system track (sectors 1, 2, 3, 4, 9) marks a Profi disk: an empty one is a volume too
            const bool marked = first->findSector(9) && !first->findSector(5);
            const char* system = "Profi CP/M";
            bool found = profi.IsCpm(nullptr, marked);
            if (!found)
            {
                CpmFileSystem klug = CpmFileSystem::KlugFloppy(*_floppy);
                found = klug.IsCpm();
                if (found)
                {
                    profi = std::move(klug);
                    system = "Klug CP/M";
                }
            }
            if (found)
            {
                DiskFsVolume v;
                v.id = "cpm";
                v.writable = true;
                DescribeCpm(profi, v, system);
                v.bytes = static_cast<uint64_t>(_floppy->getCylinders()) * _floppy->getSides() * 5 * 1024;
                add(std::move(v), std::make_unique<CpmDriver>(std::move(profi)));
                return;
            }
        }
        CpmFileSystem cpm = CpmFileSystem::AtmFloppy(*_floppy);
        const bool isCpm = cpm.IsCpm();
        if (isCpm)
        {
            DiskFsVolume v;
            v.id = "cpm";
            v.writable = true;
            DescribeCpm(cpm, v);
            v.bytes = static_cast<uint64_t>(_floppy->getCylinders()) * _floppy->getSides() * 16 * 256;
            add(std::move(v), std::make_unique<CpmDriver>(std::move(cpm)));
        }
        TrdosCatalog catalog;
        const bool parsed = catalog.Parse(*_floppy);
        if (TrdosCatalog::IsTrdos(*_floppy) && !(isCpm && (!parsed || catalog.FileCount() == 0)))
        {
            DiskFsVolume v;
            v.id = v.fs = "trdos";
            v.system = "TR-DOS";
            v.writable = true;
            if (parsed)
                v.freeBytes = static_cast<int64_t>(catalog.FreeSectors()) * 256;
            // The disk title: 8 bytes at #F5 of the info sector (track 0, sector 9)
            DiskImage::Track* track0 = _floppy->getTrack(0);
            if (const uint8_t* info = track0 ? track0->getDataForSector(8) : nullptr)
            {
                std::string title(reinterpret_cast<const char*>(info + 0xF5), 8);
                while (!title.empty() && (title.back() == ' ' || title.back() == 0))
                    title.pop_back();
                v.label = Ascii(title);
            }
            v.bytes = static_cast<uint64_t>(_floppy->getCylinders()) * _floppy->getSides() * 16 * 256;
            add(std::move(v), std::make_unique<TrdosDriver>(*_floppy));
        }
        // MDOS before FAT and +3DOS: its 512-byte sectors carry no BPB, and its data may pass for a CP/M directory
        MdosFileSystem mdos = MdosFileSystem::OnDisk(*_floppy);
        if (mdos.Open())
        {
            DiskFsVolume v;
            v.id = "mdos";
            DescribeMdos(mdos, v);
            v.writable = true;
            add(std::move(v), std::make_unique<MdosDriver>(std::move(mdos)));
            return;
        }
        // FAT before +3DOS: a PC disk has 512-byte sectors too, and its data area may pass for a CP/M directory
        // Writable: whether the disk may change is the caller's (a write-protected drive, a read-only medium)
        auto floppy = std::make_shared<FloppyBlockDevice>(*_floppy, true);
        bool fat = false;
        if (floppy->IsUniform())
        {
            DiskFsVolume v;
            v.id = "fat";
            if (auto driver = OpenFat(floppy, _page, v))
            {
                fat = true;
                add(std::move(v), std::move(driver));
            }
        }
        if (!fat && Plus3FileSystem(*_floppy).IsPlus3())
        {
            DiskFsVolume v;
            v.id = v.fs = "plus3";
            v.system = "+3DOS";
            v.writable = true;
            v.bytes = static_cast<uint64_t>(floppy->SectorCount()) * kSector;
            add(std::move(v), std::make_unique<Plus3Driver>(*_floppy));
        }
        // MB-02 before iS-DOS: both keep 1024-byte sectors, and the boot's formatter version sits where an iS-DOS
        // descriptor keeps its geometry
        Mb02FileSystem mb02 = Mb02FileSystem::OnDisk(*_floppy);
        if (mb02.Open())
        {
            DiskFsVolume v;
            v.id = v.fs = "mb02";
            v.system = "MB-02 BS-DOS";
            v.label = Ascii(mb02.Boot().name);
            v.bytes = static_cast<uint64_t>(mb02.Boot().Sectors()) * Mb02FileSystem::kSector;
            v.freeBytes = static_cast<int64_t>(mb02.FreeSectors()) * Mb02FileSystem::kSector;
            v.writable = true;
            add(std::move(v), std::make_unique<Mb02Driver>(std::move(mb02)));
            return;
        }
        Mb01FileSystem mb01 = Mb01FileSystem::OnDisk(*_floppy);
        if (mb01.Open())
        {
            DiskFsVolume v;
            v.id = v.fs = "mb01";
            v.system = "MB-01 BS-DOS";
            v.label = Ascii(mb01.Boot().name);
            v.bytes = static_cast<uint64_t>(mb01.Boot().Sectors()) * Mb01FileSystem::kSector;
            v.freeBytes = static_cast<int64_t>(mb01.FreeSectors()) * Mb01FileSystem::kSector;
            v.writable = true;
            add(std::move(v), std::make_unique<Mb01Driver>(std::move(mb01)));
            return;
        }
        IsdosFileSystem isdos = IsdosFileSystem::OnDisk(*_floppy);
        if (isdos.IsIsdos())
        {
            DiskFsVolume v;
            v.id = "isdos";
            DescribeIsdos(isdos, v);
            v.writable = true;
            add(std::move(v), std::make_unique<IsdosDriver>(std::move(isdos)));
        }
        return;
    }

    if (!_device || _device->SectorCount() == 0)
        return;
    BlockDevice& device = *_device;
    uint8_t s0[kSector];
    if (!device.ReadSector(0, s0))
    {
        _notes.push_back("sector 0 does not read");
        return;
    }

    // FAT: a superfloppy, else the partitions of the MBR
    std::vector<std::pair<uint64_t, uint64_t>> taken;  ///< sector ranges of the volumes found
    std::vector<std::pair<std::shared_ptr<BlockDevice>, uint64_t>> fats;  ///< each FAT volume's device and first LBA
    {
        DiskFsVolume v;
        v.id = "fat";
        FatPartition partition;
        std::string why;
        if (!FatVolumeReader::FindPartition(device, 1, partition, &why) && why.find("no partition table") != std::string::npos)
        {
            if (auto driver = OpenFat(Borrow(device), _page, v))
            {
                taken.emplace_back(0, v.bytes / kSector);
                fats.emplace_back(Borrow(device), 0);
                add(std::move(v), std::move(driver));
            }
        }
        else
        {
            for (uint32_t number = 1; number <= 4; number++)
            {
                if (!FatVolumeReader::FindPartition(device, number, partition, nullptr))
                    continue;
                DiskFsVolume p;
                p.id = "p" + std::to_string(number);
                const uint64_t count = std::min<uint64_t>(partition.count, device.SectorCount() - partition.first);
                auto window = std::make_shared<SubRangeDevice>(Borrow(device), partition.first, count);
                if (auto driver = OpenFat(window, _page, p))
                {
                    p.offset = partition.first * kSector;
                    taken.emplace_back(partition.first, partition.first + partition.count);
                    fats.emplace_back(window, partition.first);
                    add(std::move(p), std::move(driver));
                }
                else
                    _notes.push_back("partition " + std::to_string(number) + " (type " + std::to_string(partition.type) +
                                     ") has no FAT volume that opens");
            }
        }
    }

    const std::optional<BlockGeometry> geometry = device.NativeGeometry();
    const bool known = geometry && geometry->heads && geometry->sectors;
    const uint64_t heads = known ? geometry->heads : kDefaultHeads;
    const uint64_t sectors = known ? geometry->sectors : kDefaultSectors;

    // ATM CP/M: the drives of the configuration record in sector 0, then the hard disk descriptors (.CDS) of the same
    // unit kept on them (AUTOEXEC.SUB assigns them)
    // The .CDS files of user 0 on `fs` that describe a hard disk partition of `unit`
    auto atmDescriptors = [](const CpmFileSystem& fs, uint8_t unit, std::vector<AtmCpmChannel>& channels) {
        std::vector<CpmFile> files;
        std::string why;
        fs.List(files, why);
        for (const CpmFile& f : files)
        {
            std::vector<uint8_t> text;
            AtmCpmChannel cds;
            if (f.user == 0 && f.type == "CDS" && f.size <= 4096 && fs.Read(f, text, why) &&
                CpmFileSystem::ParseCds(std::string(text.begin(), text.end()), cds) && cds.driver == 4 &&
                cds.unit == unit)
            {
                cds.source = f.FullName();
                channels.push_back(cds);
            }
        }
    };
    std::set<std::vector<uint32_t>> atmSeen;
    auto addAtm = [&](std::vector<AtmCpmChannel>& channels) {
        for (size_t i = 0; i < channels.size(); i++)
        {
            const AtmCpmChannel c = channels[i];
            if (c.driver != 4 || c.sectorSize != kSector || c.heads == 0 || c.sectors == 0 || c.heads > heads ||
                c.sectors > sectors || !atmSeen.insert({c.startCylinder, c.heads, c.sectors, c.reserved}).second)
                continue;
            const CpmGeometry g = c.Geometry();
            const uint64_t first = static_cast<uint64_t>(c.startCylinder) * heads * sectors;
            const uint64_t end = first + static_cast<uint64_t>(c.records) * 128 / kSector;
            bool overlaps = false;
            for (const auto& [from, to] : taken)
                overlaps = overlaps || (first < to && from < end);
            if (g.blocks == 0 || overlaps || end > device.SectorCount())
                continue;
            auto cache = std::make_shared<SectorCache>();
            CpmFileSystem fs(CpmReaderAt(device, c, heads, sectors, cache), g);
            std::string why;
            if (!fs.IsCpm(&why))
            {
                if (c.drive)
                    _notes.push_back(std::string("ATM CP/M drive ") + c.drive + ": has no directory (" + why + ")");
                continue;
            }
            atmDescriptors(fs, c.unit, channels);
            DiskFsVolume v;
            v.id = "cpm-" + std::to_string(c.startCylinder);
            DescribeCpm(fs, v);
            if (device.IsWritable())
            {
                fs.SetWriter(CpmWriterAt(device, c, heads, sectors, cache));
                v.writable = true;
            }
            v.label = c.drive ? std::string(1, c.drive) + ":" : c.source;
            v.offset = first * kSector;
            v.bytes = static_cast<uint64_t>(c.records) * 128;
            taken.emplace_back(first, end);
            add(std::move(v), std::make_unique<CpmDriver>(std::move(fs)));
        }
    };
    std::vector<AtmCpmChannel> channels;
    if (CpmFileSystem::ParseAtmConfig(s0, channels))
        addAtm(channels);

    // Profi CP/M: the partitions of the table after the ProfiHDD header, each a CP/M volume of 2 KB blocks from its
    // first cylinder on with the directory first (HDD22.DRV: DSM = cylinders x H x S / 4 - 1, DRM the entry's). The
    // Profi BIOS does not read the MBR: a card may carry a FAT volume over the same sectors (a 3.2 GB SPM disk has
    // FAT32 from sector 63 and CP/M written over its FATs), both are shown
    {
        uint64_t profiHeads = 0, profiSectors = 0;
        std::vector<ProfiPartition> partitions;
        if (FindProfiDisk(device, profiHeads, profiSectors, partitions))
        {
            taken.emplace_back(0, 2 * profiHeads * profiSectors);  // the header and the system on cylinder 1
            for (const ProfiPartition& p : partitions)
            {
                const uint64_t first = static_cast<uint64_t>(p.startCylinder) * profiHeads * profiSectors;
                const uint64_t count = static_cast<uint64_t>(p.cylinders) * profiHeads * profiSectors;
                CpmGeometry g;
                g.blocks = static_cast<uint32_t>(std::min<uint64_t>(count / 4, 65536));
                g.entries = p.entries;
                if (p.startCylinder < 2 || count / 4 == 0 || p.entries == 0 ||
                    first + count > device.SectorCount() ||
                    static_cast<uint64_t>(g.entries) * 32 > static_cast<uint64_t>(g.blocks) * CpmGeometry::kBlock)
                {
                    _notes.push_back("Profi partition " + p.name + " does not fit the disk");
                    continue;
                }
                auto cache = std::make_shared<SectorCache>();
                CpmFileSystem fs(ProfiReaderAt(device, first, cache), g);
                std::string why;
                if (!fs.IsCpm(&why, true))
                {
                    _notes.push_back("Profi partition " + p.name + " has no directory (" + why + ")");
                    continue;
                }
                DiskFsVolume v;
                v.id = "cpm-" + std::to_string(p.startCylinder);
                DescribeCpm(fs, v, "Profi CP/M");
                if (device.IsWritable())
                {
                    fs.SetWriter(ProfiWriterAt(device, first, cache));
                    v.writable = true;
                }
                v.label = p.name;
                v.offset = first * kSector;
                v.bytes = count * kSector;
                taken.emplace_back(first, first + count);
                add(std::move(v), std::make_unique<CpmDriver>(std::move(fs)));
            }
        }
    }

    // MDOS3 (divIDE), as its firmware finds them (nmi.a80 ZPRACPAR, PARTSEL): the primary MBR partitions of type #49,
    // else the whole disk from its first sector that starts with "SDOS" (a FAT16 card whose first files are the
    // slots, an .md3 file; looked for after iS-DOS, below). A partition is slots of 1693 sectors, an info sector
    // ("SDOS", the slot's name) and a virtual disk
    struct Mdos3Partition
    {
        uint64_t base = 0, count = 0;
        uint32_t number = 0;  ///< 1..4 for an MBR partition, 0 for the whole disk
    };
    std::vector<Mdos3Partition> partitions;
    if (s0[510] == 0x55 && s0[511] == 0xAA)
        for (uint32_t number = 0; number < 4; number++)
        {
            const uint8_t* entry = s0 + 446 + number * 16;
            const uint64_t first = entry[8] | entry[9] << 8 | entry[10] << 16 | static_cast<uint64_t>(entry[11]) << 24;
            const uint64_t count = entry[12] | entry[13] << 8 | entry[14] << 16 | static_cast<uint64_t>(entry[15]) << 24;
            if (entry[4] == 0x49 && count && first < device.SectorCount())
                partitions.push_back({first, std::min(count, device.SectorCount() - first), number + 1});
        }
    auto addMdos3 = [&] {
        for (const auto& [base, count, number] : partitions)
        {
            auto read = [&device, base = base](uint64_t lba, uint8_t* dst) {
                return device.ReadSector(base + lba, dst);
            };
            for (const Mdos3Slot& slot : MdosFileSystem::Slots(read, count))
            {
                const uint64_t first = base + slot.firstSector;
                MdosFileSystem fs([&device, first](uint32_t lsn, uint8_t* dst) {
                    return lsn < MdosFileSystem::kSlotSectors - 1 && device.ReadSector(first + lsn, dst);
                });
                if (!fs.Open())
                    continue;
                // Info byte 36 '1': MDOS3 keeps the slot write protected
                const bool writable = device.IsWritable() && !slot.writeProtected;
                if (writable)
                    fs.SetWriter([&device, first](uint32_t lsn, const uint8_t* src) {
                        return lsn < MdosFileSystem::kSlotSectors - 1 && device.WriteSector(first + lsn, src);
                    });
                DiskFsVolume v;
                // A second #49 partition: the partition's number before the slot's
                v.id = partitions.size() > 1 ? "mdos-p" + std::to_string(number) + "-" + std::to_string(slot.index)
                                             : "mdos-" + std::to_string(slot.index);
                DescribeMdos(fs, v);
                if (!slot.name.empty())
                    v.label = Ascii(slot.name);
                v.offset = first * kSector;
                v.writable = writable;
                taken.emplace_back(first - 1, first - 1 + MdosFileSystem::kSlotSectors);
                auto driver = std::make_unique<MdosDriver>(std::move(fs));
                // The volume's name is the slot's: 32 characters at 4 of the info sector, blanks after it
                driver->slotName = slot.name;
                driver->setSlotName = [&device, info = first - 1](const std::string& name, std::string& error) {
                    uint8_t s[kSector];
                    if (name.size() > 32)
                    {
                        error = "an MDOS3 slot name has at most 32 characters";
                        return false;
                    }
                    if (!device.ReadSector(info, s))
                    {
                        error = "the slot's info sector does not read";
                        return false;
                    }
                    std::memset(s + 4, ' ', 32);
                    std::memcpy(s + 4, name.data(), name.size());
                    if (!device.WriteSector(info, s))
                    {
                        error = "the slot's info sector does not write";
                        return false;
                    }
                    return true;
                };
                add(std::move(v), std::move(driver));
            }
        }
    };
    addMdos3();

    // BS-DOS 309 (the divIDE HDD BIOS, bsdos309-hdd_bios.a80): the first primary MBR partition of type #47 holds the
    // virtual floppies @1..@255 back to back, 2 MiB each. The BIOS places a floppy's track, side and sector (as
    // BS-DOS computes them from the LSN with the boot sector's SPT) at ((track * 2 + side) * 11 + sector - 1) * 2:
    // 11 sectors a track whatever the boot says, a 1024-byte sector two disk sectors
    if (s0[510] == 0x55 && s0[511] == 0xAA)
        for (uint32_t number = 0; number < 4; number++)
        {
            const uint8_t* entry = s0 + 446 + number * 16;
            const uint64_t first = entry[8] | entry[9] << 8 | entry[10] << 16 | static_cast<uint64_t>(entry[11]) << 24;
            uint64_t count = entry[12] | entry[13] << 8 | entry[14] << 16 | static_cast<uint64_t>(entry[15]) << 24;
            if (entry[4] != 0x47 || count == 0 || first >= device.SectorCount())
                continue;
            count = std::min(count, device.SectorCount() - first);
            const uint64_t discs = std::min<uint64_t>(255, count / kBsdosDisc);
            for (uint64_t n = 1; n <= discs; n++)
            {
                const uint64_t base = first + (n - 1) * kBsdosDisc;
                uint8_t boot[kSector];
                if (!device.ReadSector(base, boot))
                    continue;
                const uint32_t spt = boot[6] | boot[7] << 8;
                if (spt == 0)
                    continue;
                auto lbaOf = [base, spt](uint32_t lsn, uint64_t& lba) {
                    const uint64_t track = lsn / (2 * spt), side = lsn / spt % 2, sector = lsn % spt;
                    const uint64_t physical = (track * 2 + side) * 11 + sector;
                    lba = base + physical * 2;
                    return physical * 2 + 1 < kBsdosDisc;
                };
                Mb02FileSystem fs([&device, lbaOf](uint32_t lsn, uint8_t* dst) {
                    uint64_t lba = 0;
                    return lbaOf(lsn, lba) && device.ReadSector(lba, dst) && device.ReadSector(lba + 1, dst + kSector);
                });
                if (!fs.Open())
                    continue;
                if (device.IsWritable())
                    fs.SetWriter([&device, lbaOf](uint32_t lsn, const uint8_t* src) {
                        uint64_t lba = 0;
                        return lbaOf(lsn, lba) && device.WriteSector(lba, src) &&
                               device.WriteSector(lba + 1, src + kSector);
                    });
                DiskFsVolume v;
                v.id = "mb02-" + std::to_string(n);
                v.fs = "mb02";
                v.system = "MB-02 BS-DOS";
                v.label = Ascii(fs.Boot().name);
                v.offset = base * kSector;
                v.bytes = static_cast<uint64_t>(fs.Boot().Sectors()) * Mb02FileSystem::kSector;
                v.freeBytes = static_cast<int64_t>(fs.FreeSectors()) * Mb02FileSystem::kSector;
                v.writable = device.IsWritable();
                add(std::move(v), std::make_unique<Mb02Driver>(std::move(fs)));
            }
            taken.emplace_back(first, first + count);
            break;
        }

    // iS-DOS: the whole disk is one device, or devices start at cylinder boundaries (the ATM-Turbo HDD driver)
    auto isdosAt = [&](uint64_t lba, const std::string& id) {
        for (const auto& [first, end] : taken)
            if (lba >= first && lba < end)
                return false;
        uint8_t s[kSector];
        if (!device.ReadSector(lba, s) || !IsdosFileSystem::IsDescriptor(s))
            return false;
        auto cache = std::make_shared<SectorCache>();
        IsdosFileSystem fs(BlockReaderAt(device, lba * kSector, cache));
        if (!fs.IsIsdos())
            return false;
        if (device.IsWritable())
            fs.SetWriter(BlockWriterAt(device, lba * kSector, cache));
        DiskFsVolume v;
        v.id = id;
        v.offset = lba * kSector;
        DescribeIsdos(fs, v);
        v.writable = device.IsWritable();
        taken.emplace_back(lba, lba + (v.bytes + kSector - 1) / kSector);
        add(std::move(v), std::make_unique<IsdosDriver>(std::move(fs)));
        return true;
    };
    if (isdosAt(0, "isdos"))
        return;
    // The driver keeps each device's first cylinder in its own settings (ide+.blk set by idetune.com, ZX Format 5),
    // the devices one after the other from cylinder 0, in the geometry it was told: the image's cylinders, then the
    // sectors of a cylinder (16 heads of 63 sectors at most) after the end of every volume found, a device found
    // there adding its own end
    const uint64_t cylinder = heads * sectors;
    auto idOf = [cylinder](uint64_t lba) {
        return lba % cylinder == 0 ? "isdos-" + std::to_string(lba / cylinder) : "isdos-lba" + std::to_string(lba);
    };
    for (uint64_t lba = cylinder; lba < device.SectorCount(); lba += cylinder)
        isdosAt(lba, idOf(lba));
    for (size_t i = 0; i <= taken.size(); i++)
    {
        const uint64_t from = i == 0 ? 1 : taken[i - 1].second, to = std::min(device.SectorCount(), from + kIsdosChainWindow);
        for (uint64_t lba = from; lba < to; lba++)
            if (isdosAt(lba, idOf(lba)))
                break;
    }

    // MDOS3 with no #49 partition: the firmware reads the whole disk from LBA 0 up to the first "SDOS" sector. Here
    // the FAT volumes' files by their first sectors wherever they are, and the other sectors of the first
    // kMdos3Search (an .md3 written to a disk whole): reading every sector of a large disk takes seconds, too slow
    // for each look
    if (partitions.empty())
    {
        const uint64_t file = FirstSdosFile(fats, _page, device.SectorCount());
        const uint64_t raw = ScanUntaken(device, 0, std::min(file, kMdos3Search), taken,
                                         [](const uint8_t* s) { return std::memcmp(s, "SDOS", 4) == 0; });
        const uint64_t lba = raw < std::min(file, kMdos3Search) ? raw : file;
        if (lba < device.SectorCount())
        {
            partitions.push_back({lba, device.SectorCount() - lba, 0});
            addMdos3();
        }
    }

    // ATM CP/M partitions no configuration record names (assigned from a floppy's .CDS, or the record lost). A
    // partition starts at a cylinder, its directory past the reserved tracks (the NedoPC installation guide: PATBEG,
    // SYS, the disk's own heads and sectors), so at the start of a track; looked for outside the volumes found. The
    // size, the directory's entries and the width of the block numbers are not on the disk: the space up to the next
    // volume (8 MB at most, DBLDR's 16 bits), the most entries that read (512 down to 64) and the width the BIOS gives
    // that size, else the other. A .CDS of unit 0 kept on one gives its partitions as the BIOS sees them; one no .CDS
    // describes is read only (a write could run past its unknown end)
    {
        const uint64_t total = device.SectorCount(), cylinderSectors = heads * sectors;
        auto nextVolume = [&](uint64_t lba) {
            uint64_t next = total;
            for (const auto& [from, to] : taken)
                if (from > lba)
                    next = std::min(next, from);
            return next;
        };
        auto channelAt = [&](uint64_t lba) {
            AtmCpmChannel c;
            c.driver = 4;
            c.heads = static_cast<uint32_t>(heads);
            c.sectors = static_cast<uint32_t>(sectors);
            c.sectorSize = kSector;
            c.startCylinder = static_cast<uint32_t>(lba / cylinderSectors);
            c.reserved = static_cast<uint32_t>(lba % cylinderSectors / sectors);
            return c;
        };
        // The geometry the directory at `lba` reads with in `count` sectors
        auto fit = [&](uint64_t lba, uint64_t count, CpmGeometry& out) {
            const uint32_t room =
                static_cast<uint32_t>(std::min<uint64_t>(count * kSector / CpmGeometry::kBlock, 4096));
            const AtmCpmChannel c = channelAt(lba);
            auto cache = std::make_shared<SectorCache>();
            // The width the BIOS gives a volume of `room` blocks first; 16-bit numbers only past 256 blocks
            const bool bios = room > 256;
            for (const bool wide : {bios, !bios})
            {
                if (wide && room <= 256)
                    continue;
                for (uint32_t entries = 512; entries >= 64; entries -= 64)
                {
                    CpmGeometry g;
                    g.blocks = wide ? room : std::min<uint32_t>(room, 256);
                    g.entries = entries;
                    if (g.DirectoryBlocks() >= g.blocks)
                        continue;
                    if (CpmFileSystem(CpmReaderAt(device, c, heads, sectors, cache), g).IsCpm())
                    {
                        out = g;
                        return true;
                    }
                }
            }
            return false;
        };
        std::vector<uint64_t> found;  // the directories' LBAs
        for (uint64_t from = 0; from < total;)
        {
            const uint64_t lba = ScanUntaken(
                device, from, total, taken, [](const uint8_t* s) { return CpmFileSystem::LooksLikeDirectory(s, 16); },
                sectors);
            if (lba >= total)
                break;
            CpmGeometry g;
            uint32_t last = 0;
            auto cache = std::make_shared<SectorCache>();
            if (fit(lba, nextVolume(lba) - lba, g) &&
                CpmFileSystem(CpmReaderAt(device, channelAt(lba), heads, sectors, cache), g).LastUsedBlock(last))
            {
                // A file's bytes hold no partition
                found.push_back(lba);
                from = lba + (static_cast<uint64_t>(last) + 1) * (CpmGeometry::kBlock / kSector);
            }
            else
                from = lba + 1;
        }
        // Each up to the next one's cylinder; the .CDS files on them first
        auto room = [&](size_t i) {
            const uint64_t next = i + 1 < found.size() ? found[i + 1] / cylinderSectors * cylinderSectors : total;
            return std::min(next, nextVolume(found[i])) - found[i];
        };
        std::vector<AtmCpmChannel> described;
        for (size_t i = 0; i < found.size(); i++)
        {
            CpmGeometry g;
            auto cache = std::make_shared<SectorCache>();
            if (fit(found[i], room(i), g))
                atmDescriptors(CpmFileSystem(CpmReaderAt(device, channelAt(found[i]), heads, sectors, cache), g), 0,
                               described);
        }
        addAtm(described);
        for (size_t i = 0; i < found.size(); i++)
        {
            bool overlaps = false;
            for (const auto& [from, to] : taken)
                overlaps = overlaps || (found[i] >= from && found[i] < to);
            CpmGeometry g;
            if (overlaps || !fit(found[i], room(i), g))
                continue;
            const AtmCpmChannel c = channelAt(found[i]);
            auto cache = std::make_shared<SectorCache>();
            CpmFileSystem fs(CpmReaderAt(device, c, heads, sectors, cache), g);
            DiskFsVolume v;
            v.id = "cpm-" + std::to_string(c.startCylinder);
            DescribeCpm(fs, v);
            v.offset = found[i] * kSector;
            v.bytes = static_cast<uint64_t>(g.blocks) * CpmGeometry::kBlock;
            taken.emplace_back(found[i], found[i] + v.bytes / kSector);
            _notes.push_back("ATM CP/M partition at cylinder " + std::to_string(c.startCylinder) +
                             " found by its directory: no descriptor gives its size, read only");
            add(std::move(v), std::make_unique<CpmDriver>(std::move(fs)));
        }
    }
}

DiskFs::Driver* DiskFs::Find(const std::string& volume, std::string& error, size_t* index)
{
    Probe();
    if (_volumes.empty())
    {
        error = "no file system found on the disk";
        return nullptr;
    }
    const std::string wanted = Lower(volume);
    for (size_t i = 0; i < _volumes.size(); i++)
        if (wanted.empty() || Lower(_volumes[i].id) == wanted || Lower(_volumes[i].fs) == wanted || std::to_string(i) == wanted)
        {
            if (index)
                *index = i;
            return _drivers[i].get();
        }
    std::string list;
    for (const DiskFsVolume& v : _volumes)
        list += (list.empty() ? "" : ", ") + v.id;
    error = "no volume '" + volume + "' (volumes: " + list + ")";
    return nullptr;
}

bool DiskFs::List(const std::string& volume, const std::string& path, bool recursive, std::vector<DiskFsEntry>& out,
                  std::string& error)
{
    out.clear();
    _notes.clear();
    Driver* driver = Find(volume, error);
    return driver && driver->List(CleanPath(path), recursive, out, _notes, error);
}

bool DiskFs::Stat(const std::string& volume, const std::string& path, DiskFsEntry& out, std::string& error)
{
    Driver* driver = Find(volume, error);
    const std::string clean = CleanPath(path);
    if (driver && clean.empty())
    {
        out = {};
        out.directory = true;
        return true;
    }
    return driver && driver->Stat(clean, out, _notes, error);
}

bool DiskFs::Read(const std::string& volume, const std::string& path, std::vector<uint8_t>& data, std::string& error,
                  std::vector<uint8_t>* slack)
{
    data.clear();
    if (slack)
        slack->clear();
    Driver* driver = Find(volume, error);
    const std::string clean = CleanPath(path);
    if (driver && clean.empty())
    {
        error = "name a file";
        return false;
    }
    return driver && driver->Read(clean, data, slack, error);
}

DiskFs::Driver* DiskFs::FindWritable(const std::string& volume, std::string& error)
{
    size_t index = 0;
    Driver* driver = Find(volume, error, &index);
    if (driver && !_volumes[index].writable)
    {
        error = "the " + _volumes[index].system + " volume '" + _volumes[index].id + "' is read only";
        return nullptr;
    }
    return driver;
}

namespace
{
/// `attributes` applied to the letters `current`: "+R-H" changes them, "RA" replaces them. False with the reason on a
/// letter the volume does not take
bool AttributeSet(const DiskFsVolume& v, const std::string& current, const std::string& attributes, std::string& set,
                  std::string& error)
{
    if (v.attributes.empty())
    {
        error = "the " + v.system + " volume '" + v.id + "' has no file attributes";
        return false;
    }
    set.clear();
    const bool relative = !attributes.empty() && (attributes[0] == '+' || attributes[0] == '-');
    if (relative)
        for (const char c : current)
            if (v.attributes.find(c) != std::string::npos)
                set += c;
    char sign = '+';
    for (const char raw : attributes)
    {
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        if (relative && (c == '+' || c == '-'))
        {
            sign = c;
            continue;
        }
        if (v.attributes.find(c) == std::string::npos)
        {
            error = "attributes '" + attributes + "': the " + v.system + " volume takes " + v.attributes + " (+R-H changes, RA sets exactly)";
            return false;
        }
        const size_t at = set.find(c);
        if (sign == '+' && at == std::string::npos)
            set += c;
        else if (sign == '-' && at != std::string::npos)
            set.erase(at, 1);
    }
    if (attributes.empty() || (relative && attributes.find_first_not_of("+-") == std::string::npos))
    {
        error = "name the attributes: the " + v.system + " volume takes " + v.attributes + " (+R-H changes, RA sets exactly)";
        return false;
    }
    return true;
}
}  // namespace

bool DiskFs::Write(const std::string& volume, const std::string& path, const std::vector<uint8_t>& data,
                   const DiskFsWriteOptions& options, std::string& error)
{
    size_t index = 0;
    if (!Find(volume, error, &index))
        return false;
    Driver* driver = FindWritable(volume, error);
    const std::string clean = CleanPath(path);
    if (driver && clean.empty())
    {
        error = "name a file";
        return false;
    }
    if (!driver)
        return false;
    // Everything asked for is checked before the disk changes
    const DiskFsVolume& v = _volumes[index];
    if (options.basicType >= 0)
    {
        static const char* const kTypes[] = {"a program (0)", "a number array (1)", "a character array (2)", "CODE (3)"};
        const std::string types = driver->BasicTypes();
        if (options.basicType > 3 || types.find(static_cast<char>('0' + options.basicType)) == std::string::npos)
        {
            std::string takes;
            for (const char t : types)
                takes += (takes.empty() ? "" : ", ") + std::string(kTypes[t - '0']);
            error = "type " + std::to_string(options.basicType) + ": the " + v.system + " volume takes " +
                    (takes.empty() ? "no header type" : takes);
            return false;
        }
    }
    if (options.param2 >= 0 && (!driver->TakesParam2() || options.param2 > 0xFFFF))
    {
        error = driver->TakesParam2() ? "param2 is 0-65535" : "the " + v.system + " volume keeps no header param 2";
        return false;
    }
    std::string set;
    if (!options.attributes.empty() && !AttributeSet(v, {}, options.attributes, set, error))
        return false;
    if (!driver->Write(clean, data, options, error))
        return false;
    return options.attributes.empty() || SetAttributes(volume, clean, options.attributes, error);
}

bool DiskFs::Delete(const std::string& volume, const std::string& path, std::string& error, bool recursive)
{
    Driver* driver = FindWritable(volume, error);
    const std::string clean = CleanPath(path);
    if (driver && clean.empty())
    {
        error = "name a file";
        return false;
    }
    if (!driver)
        return false;
    DiskFsEntry entry;
    if (!recursive || !Stat(volume, clean, entry, error) || !entry.directory)
        return driver->Delete(clean, error);

    // Every entry checked before one goes; then the deepest first (a listing puts a directory before its contents)
    std::vector<DiskFsEntry> entries;
    if (!List(volume, clean, true, entries, error))
        return false;
    entries.insert(entries.begin(), entry);
    for (const DiskFsEntry& e : entries)
        if (!driver->Deletable(e, error))
            return false;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if (!driver->Delete(it->path, error))
            return false;
    return true;
}

bool DiskFs::MakeDirectory(const std::string& volume, const std::string& path, bool parents,
                           const DiskFsDirectoryOptions& options, std::string& error)
{
    size_t index = 0;
    if (!Find(volume, error, &index))
        return false;
    Driver* driver = FindWritable(volume, error);
    const std::string clean = CleanPath(path);
    if (driver && clean.empty())
    {
        error = "name the directory";
        return false;
    }
    if (!driver)
        return false;
    if ((options.continuous || options.blocks) && !driver->SizedDirectories())
    {
        error = "continuous and blocks are for iS-DOS catalogues, not " + _volumes[index].system;
        return false;
    }
    // The directories on the way are of the standard kind, `options` makes the last one
    DiskFsDirectoryOptions plain;
    plain.mtime = options.mtime;
    auto make = [&](const std::string& dir) {
        error.clear();
        if (driver->MakeDirectory(dir, dir.size() == clean.size() ? options : plain, error))
            return true;
        if (error.empty())
            error = "the " + _volumes[index].system + " volume '" + _volumes[index].id + "' has no directories";
        return false;
    };
    DiskFsEntry entry;
    std::string missing;
    if (!parents)
    {
        if (Stat(volume, clean, entry, missing))
        {
            error = clean + " is there already";
            return false;
        }
        return make(clean);
    }
    // Each directory on the way: there already, or made
    for (size_t at = clean.find('/');; at = clean.find('/', at + 1))
    {
        const std::string step = clean.substr(0, at);
        if (Stat(volume, step, entry, missing))
        {
            if (!entry.directory)
            {
                error = step + " is a file";
                return false;
            }
        }
        else if (!make(step))
            return false;
        if (at == std::string::npos)
            return true;
    }
}

bool DiskFs::SetAttributes(const std::string& volume, const std::string& path, const std::string& attributes, std::string& error)
{
    size_t index = 0;
    if (!Find(volume, error, &index))
        return false;
    Driver* driver = FindWritable(volume, error);
    if (!driver)
        return false;
    const DiskFsVolume& v = _volumes[index];
    std::string set;
    if (!AttributeSet(v, {}, attributes, set, error))
        return false;
    const std::string clean = CleanPath(path);
    DiskFsEntry entry;
    if (clean.empty())
    {
        error = "name the file";
        return false;
    }
    // The letters set now, then the change: "+R-H" or exactly "RA"
    if (!Stat(volume, clean, entry, error) || !AttributeSet(v, entry.attributes, attributes, set, error))
        return false;
    error.clear();
    return driver->SetAttributes(entry, set, error);
}

bool DiskFs::SetTime(const std::string& volume, const std::string& path, int64_t mtime, std::string& error)
{
    size_t index = 0;
    if (!Find(volume, error, &index))
        return false;
    Driver* driver = FindWritable(volume, error);
    if (!driver)
        return false;
    const std::string clean = CleanPath(path);
    if (clean.empty())
    {
        error = "name the file";
        return false;
    }
    error.clear();
    if (driver->SetTime(clean, mtime, error))
        return true;
    if (error.empty())
        error = "the " + _volumes[index].system + " volume '" + _volumes[index].id + "' keeps no file times";
    return false;
}

bool DiskFs::SetLabel(const std::string& volume, const std::string& label, int64_t mtime, std::string& error)
{
    size_t index = 0;
    if (!Find(volume, error, &index))
        return false;
    Driver* driver = FindWritable(volume, error);
    if (!driver)
        return false;
    error.clear();
    if (driver->SetLabel(label, mtime, error))
    {
        std::string trimmed = label;
        while (!trimmed.empty() && trimmed.back() == ' ')
            trimmed.pop_back();
        const std::string kept = driver->Label();
        _volumes[index].label = kept.empty() ? trimmed : kept;
        return true;
    }
    if (error.empty())
        error = "the " + _volumes[index].system + " volume '" + _volumes[index].id + "' has no label";
    return false;
}

bool DiskFs::Rename(const std::string& volume, const std::string& path, const std::string& to, std::string& error)
{
    Driver* driver = FindWritable(volume, error);
    const std::string from = CleanPath(path);
    std::string target = CleanPath(to);
    if (driver && (from.empty() || target.empty()))
    {
        error = "name the file and its new name";
        return false;
    }
    // A leading slash stays: "/name" goes to the root, a bare "name" stays in the file's directory
    if (to.front() == '/' || to.front() == '\\')
        target = "/" + target;
    return driver && driver->Rename(from, target, error);
}

int64_t DiskFsEntry::Field(const std::string& key, int64_t otherwise) const
{
    for (const auto& [field, value] : fields)
        if (field == key)
            return value;
    return otherwise;
}
}  // namespace unrealdisk
