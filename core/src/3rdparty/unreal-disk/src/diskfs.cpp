#include "unrealdisk/diskfs.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <set>

#include "unrealdisk/blockdevice.h"
#include "unrealdisk/cpmfs.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/fatfs.h"
#include "unrealdisk/fatvolumereader.h"
#include "unrealdisk/floppyblockdevice.h"
#include "unrealdisk/isdosfs.h"
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
/// iS-DOS devices are looked for at this many cylinder boundaries at most (a 2 GB disk of 16 x 63 cylinders)
constexpr uint64_t kMaxCylinderScan = 4096;

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
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
    virtual bool MakeDirectory(const std::string&, int64_t, std::string&) { return false; }
    /// Whether the entry may be deleted (a recursive delete checks every entry before it deletes one)
    virtual bool Deletable(const DiskFsEntry&, std::string&) { return true; }
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
        return fs.Write(path, data, write, error);
    }

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

    bool MakeDirectory(const std::string& path, int64_t mtime, std::string& error) override
    {
        IsdosFileSystem::WriteOptions write;
        FatVolumeReader::UnixToDos(mtime, write.date, write.time);
        return fs.MakeCatalogue(path, write, error);
    }

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

    bool MakeDirectory(const std::string& path, int64_t mtime, std::string& error) override
    {
        std::string dir, name;
        SplitPath(path, dir, name);
        return Edit(path, error, [&](FatFs& fs) {
            uint16_t date = 0, time = 0;
            FatVolumeReader::UnixToDos(mtime, date, time);
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

    /// User 0 at the root, users 1-15 in the directories USER1..USER15
    static std::string PathOf(const CpmFile& f)
    {
        const std::string name = Ascii(f.FullName());
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
                e.name = Ascii(f.FullName());
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

    CpmFileSystem fs;
};

void DescribeCpm(const CpmFileSystem& fs, DiskFsVolume& volume)
{
    volume.fs = "cpm";
    volume.system = "ATM CP/M";
    uint32_t free = 0;
    if (fs.FreeBlocks(free))
        volume.freeBytes = static_cast<int64_t>(free) * CpmGeometry::kBlock;
}

/// The bytes of an ATM CP/M hard disk partition from its directory on: the BIOS's cylinder, head and sector of the
/// channel's geometry, at the disk's own (`heads` x `sectors`)
CpmFileSystem::ByteReader CpmReaderAt(BlockDevice& device, const AtmCpmChannel& channel, uint64_t heads,
                                      uint64_t sectors, std::shared_ptr<SectorCache> cache)
{
    const uint64_t directory = static_cast<uint64_t>(channel.reserved) * channel.sectors;
    const uint64_t perTrack = channel.sectors, partHeads = channel.heads, start = channel.startCylinder;
    return [&device, directory, perTrack, partHeads, start, heads, sectors, cache](uint64_t offset, uint8_t* dst,
                                                                                   uint32_t size) {
        while (size > 0)
        {
            const uint64_t logical = directory + offset / kSector;
            const uint64_t track = logical / perTrack;
            const uint64_t lba =
                ((start + track / partHeads) * heads + track % partHeads) * sectors + logical % perTrack;
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
        CpmFileSystem cpm = CpmFileSystem::AtmFloppy(*_floppy);
        const bool isCpm = cpm.IsCpm();
        if (isCpm)
        {
            DiskFsVolume v;
            v.id = "cpm";
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
    std::vector<std::pair<uint64_t, uint64_t>> taken;  ///< sector ranges of the FAT volumes
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
    std::vector<AtmCpmChannel> channels;
    if (CpmFileSystem::ParseAtmConfig(s0, channels))
    {
        std::set<std::vector<uint32_t>> seen;
        for (size_t i = 0; i < channels.size(); i++)
        {
            const AtmCpmChannel c = channels[i];
            if (c.driver != 4 || c.sectorSize != kSector || c.heads == 0 || c.sectors == 0 || c.heads > heads ||
                c.sectors > sectors || !seen.insert({c.startCylinder, c.heads, c.sectors, c.reserved}).second)
                continue;
            const CpmGeometry g = c.Geometry();
            const uint64_t first = static_cast<uint64_t>(c.startCylinder) * heads * sectors;
            const uint64_t end = first + static_cast<uint64_t>(c.records) * 128 / kSector;
            bool overlaps = false;
            for (const auto& [from, to] : taken)
                overlaps = overlaps || (first < to && from < end);
            if (g.blocks == 0 || overlaps || end > device.SectorCount())
                continue;
            CpmFileSystem fs(CpmReaderAt(device, c, heads, sectors, std::make_shared<SectorCache>()), g);
            std::string why;
            if (!fs.IsCpm(&why))
            {
                if (c.drive)
                    _notes.push_back(std::string("ATM CP/M drive ") + c.drive + ": has no directory (" + why + ")");
                continue;
            }
            std::vector<CpmFile> files;
            fs.List(files, why);
            for (const CpmFile& f : files)
            {
                std::vector<uint8_t> text;
                AtmCpmChannel cds;
                if (f.user == 0 && f.type == "CDS" && f.size <= 4096 && fs.Read(f, text, why) &&
                    CpmFileSystem::ParseCds(std::string(text.begin(), text.end()), cds) && cds.driver == 4 &&
                    cds.unit == c.unit)
                {
                    cds.source = f.FullName();
                    channels.push_back(cds);
                }
            }
            DiskFsVolume v;
            v.id = "cpm-" + std::to_string(c.startCylinder);
            DescribeCpm(fs, v);
            v.label = c.drive ? std::string(1, c.drive) + ":" : c.source;
            v.offset = first * kSector;
            v.bytes = static_cast<uint64_t>(c.records) * 128;
            taken.emplace_back(first, end);
            add(std::move(v), std::make_unique<CpmDriver>(std::move(fs)));
        }
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
    const uint64_t cylinder = heads * sectors;
    const uint64_t cylinders = std::min(device.SectorCount() / cylinder, kMaxCylinderScan);
    for (uint64_t c = 1; c < cylinders; c++)
        isdosAt(c * cylinder, "isdos-" + std::to_string(c));
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

bool DiskFs::Write(const std::string& volume, const std::string& path, const std::vector<uint8_t>& data,
                   const DiskFsWriteOptions& options, std::string& error)
{
    Driver* driver = FindWritable(volume, error);
    const std::string clean = CleanPath(path);
    if (driver && clean.empty())
    {
        error = "name a file";
        return false;
    }
    return driver && driver->Write(clean, data, options, error);
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

bool DiskFs::MakeDirectory(const std::string& volume, const std::string& path, bool parents, int64_t mtime, std::string& error)
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
    auto make = [&](const std::string& dir) {
        error.clear();
        if (driver->MakeDirectory(dir, mtime, error))
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
    if (v.attributes.empty())
    {
        error = "the " + v.system + " volume '" + v.id + "' has no file attributes";
        return false;
    }
    const std::string clean = CleanPath(path);
    DiskFsEntry entry;
    if (clean.empty())
    {
        error = "name the file";
        return false;
    }
    if (!Stat(volume, clean, entry, error))
        return false;
    // The letters set now, then the change: "+R-H" or exactly "RA"
    std::string set;
    for (const char c : entry.attributes)
        if (v.attributes.find(c) != std::string::npos)
            set += c;
    const bool relative = !attributes.empty() && (attributes[0] == '+' || attributes[0] == '-');
    if (!relative)
        set.clear();
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
    for (const auto& [name, value] : fields)
        if (name == key)
            return value;
    return otherwise;
}
}  // namespace unrealdisk
