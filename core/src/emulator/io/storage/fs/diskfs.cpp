#include "emulator/io/storage/fs/diskfs.h"

#include <algorithm>
#include <cstring>
#include <set>

#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/fs/floppyblockdevice.h"
#include "emulator/io/storage/mediareadtap.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/media/medium.h"
#include "unrealdisk/diskimage.h"
#include "unrealdisk/isdosfs.h"
#include "unrealdisk/plus3fs.h"
#include "unrealdisk/trdoscatalog.h"
#include "unrealdisk/trdosfs.h"

namespace
{
constexpr uint32_t kSector = IBlockDevice::kSectorSize;
/// The cylinder of a hard disk without a geometry in its image (16 heads, 63 sectors: the ATM-Turbo IDE images)
constexpr uint64_t kDefaultCylinderSectors = 16 * 63;
/// iS-DOS devices are looked for at this many cylinder boundaries at most (a 2 GB disk of 16 x 63 cylinders)
constexpr uint64_t kMaxCylinderScan = 4096;

std::string Lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

/// "/A//B/" -> "A/B"
std::string CleanPath(const std::string& path)
{
    std::string out;
    std::string part;
    auto flush = [&]() {
        if (!part.empty() && part != ".")
            out += (out.empty() ? "" : "/") + part;
        part.clear();
    };
    for (const char c : path)
    {
        if (c == '/' || c == '\\')
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

std::shared_ptr<IBlockDevice> Borrow(IBlockDevice& device)
{
    return std::shared_ptr<IBlockDevice>(&device, [](IBlockDevice*) {});
}

/// 256-byte blocks from `offset` bytes into a block device (the last sector read is kept)
unrealdisk::IsdosFileSystem::BlockReader BlockReaderAt(IBlockDevice& device, uint64_t offset)
{
    struct Cache
    {
        uint64_t lba = UINT64_MAX;
        uint8_t sector[kSector] = {};
    };
    auto cache = std::make_shared<Cache>();
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
}  // namespace

/// region <Drivers>

struct DiskFs::Driver
{
    virtual ~Driver() = default;
    /// Every entry of the volume in listing order (a directory before its contents); drivers that read directories
    /// one at a time override List and Stat instead
    virtual bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>& notes, std::string& error) = 0;
    virtual bool Read(const std::string& path, std::vector<uint8_t>& data, std::string& error) = 0;

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
std::string TrdosName(const unrealdisk::TrdosFile& f)
{
    return Ascii(f.TrimmedName()) + "." + Ascii(std::string(1, f.type));
}

struct TrdosDriver : DiskFs::Driver
{
    explicit TrdosDriver(unrealdisk::DiskImage& image) : fs(image) {}

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>&, std::string& error) override
    {
        std::vector<unrealdisk::TrdosFile> files;
        if (!fs.List(files, error))
            return false;
        for (const unrealdisk::TrdosFile& f : files)
        {
            DiskFsEntry e;
            e.name = e.path = TrdosName(f);
            e.size = f.length;
            e.start = f.start;
            e.fields = {{"sectors", f.sectors}, {"track", f.firstTrack}, {"sector", f.firstSector}, {"slot", f.slot}};
            out.push_back(std::move(e));
        }
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::string& error) override
    {
        std::vector<unrealdisk::TrdosFile> files;
        if (!fs.List(files, error))
            return false;
        for (int pass = 0; pass < 2; pass++)
            for (auto it = files.rbegin(); it != files.rend(); ++it)
                if (pass == 0 ? TrdosName(*it) == path : Lower(TrdosName(*it)) == Lower(path))
                    return fs.Read(*it, data, nullptr, error);
        error = "no " + path + " on the TR-DOS disk";
        return false;
    }

    unrealdisk::TrdosFileSystem fs;
};

struct Plus3Driver : DiskFs::Driver
{
    explicit Plus3Driver(unrealdisk::DiskImage& image) : fs(image) {}

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>&, std::string& error) override
    {
        std::vector<unrealdisk::Plus3File> files;
        if (!fs.List(files, error))
            return false;
        for (const unrealdisk::Plus3File& f : files)
        {
            DiskFsEntry e;
            e.name = e.path = Ascii(f.FullName());
            e.size = f.length;
            e.start = f.header ? f.param1 : 0;
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

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::string& error) override
    {
        unrealdisk::Plus3File f;
        return fs.Find(path, f, error) && fs.Read(f, data, error);
    }

    unrealdisk::Plus3FileSystem fs;
};

DiskFsEntry IsdosEntry(const unrealdisk::IsdosFile& f)
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
    explicit IsdosDriver(unrealdisk::IsdosFileSystem fs) : fs(std::move(fs)) {}

    bool All(std::vector<DiskFsEntry>& out, std::vector<std::string>& notes, std::string& error) override
    {
        std::vector<unrealdisk::IsdosFile> files;
        if (!fs.List(files, &notes, error))
            return false;
        for (const unrealdisk::IsdosFile& f : files)
            out.push_back(IsdosEntry(f));
        return true;
    }

    bool List(const std::string& dir, bool recursive, std::vector<DiskFsEntry>& out, std::vector<std::string>& notes,
              std::string& error) override
    {
        if (recursive)
            return Driver::List(dir, recursive, out, notes, error);
        std::vector<unrealdisk::IsdosFile> files;
        if (!fs.ListDirectory(dir, files, error))
            return false;
        for (const unrealdisk::IsdosFile& f : files)
            out.push_back(IsdosEntry(f));
        return true;
    }

    bool Stat(const std::string& path, DiskFsEntry& out, std::vector<std::string>&, std::string& error) override
    {
        unrealdisk::IsdosFile f;
        if (!fs.Find(path, f, error))
            return false;
        out = IsdosEntry(f);
        return true;
    }

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::string& error) override
    {
        unrealdisk::IsdosFile f;
        return fs.Find(path, f, error) && fs.Read(f, data, error);
    }

    unrealdisk::IsdosFileSystem fs;
};

struct FatDriver : DiskFs::Driver
{
    std::shared_ptr<IBlockDevice> device;  ///< the volume's device (a floppy view, a partition window, the disk)
    FatVolumeReader reader;

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

    bool Read(const std::string& path, std::vector<uint8_t>& data, std::string& error) override
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
std::unique_ptr<FatDriver> OpenFat(std::shared_ptr<IBlockDevice> device, CodePage page, DiskFsVolume& volume)
{
    auto driver = std::make_unique<FatDriver>();
    driver->device = std::move(device);
    if (!driver->reader.Open(*driver->device, page))
        return nullptr;
    volume.fs = FatName(driver->reader.Type());
    volume.system = volume.fs == "fat12" ? "FAT12" : volume.fs == "fat16" ? "FAT16" : "FAT32";
    volume.label = driver->reader.Label();
    volume.bytes = static_cast<uint64_t>(driver->reader.VolumeSectors()) * kSector;
    std::vector<bool> free;
    if (driver->reader.ScanFree(free))
        volume.freeBytes = static_cast<int64_t>(std::count(free.begin(), free.end(), true)) * driver->reader.SectorsPerCluster() * kSector;
    return driver;
}

void DescribeIsdos(const unrealdisk::IsdosFileSystem& fs, DiskFsVolume& volume)
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

DiskFs::DiskFs(unrealdisk::DiskImage& floppy) : _floppy(&floppy) {}

DiskFs::DiskFs(IBlockDevice& device, CodePage page) : _device(&device), _page(page) {}

DiskFs::~DiskFs() = default;

std::unique_ptr<DiskFs> DiskFs::Open(Medium& medium)
{
    if (unrealdisk::DiskImage* floppy = medium.Floppy())
        return std::make_unique<DiskFs>(*floppy);
    if (medium.Kind() != MediaKind::Block || !medium.Block())
        return nullptr;
    // Under the read tap: a host read is no guest read, time travel keeps nothing of it
    IBlockDevice& device = medium.ReadTap() ? medium.ReadTap()->Base() : *medium.Block();
    return std::make_unique<DiskFs>(device, medium.Options().codePage.value_or(CodePage::Cp866));
}

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
        _volumes.push_back(std::move(volume));
        _drivers.push_back(std::move(driver));
    };

    if (_floppy)
    {
        if (unrealdisk::TrdosCatalog::IsTrdos(*_floppy))
        {
            DiskFsVolume v;
            v.id = v.fs = "trdos";
            v.system = "TR-DOS";
            unrealdisk::TrdosCatalog catalog;
            if (catalog.Parse(*_floppy))
                v.freeBytes = static_cast<int64_t>(catalog.FreeSectors()) * 256;
            // The disk title: 8 bytes at #F5 of the info sector (track 0, sector 9)
            unrealdisk::DiskImage::Track* track0 = _floppy->getTrack(0);
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
        auto floppy = std::make_shared<FloppyBlockDevice>(*_floppy);
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
        if (!fat && unrealdisk::Plus3FileSystem(*_floppy).IsPlus3())
        {
            DiskFsVolume v;
            v.id = v.fs = "plus3";
            v.system = "+3DOS";
            v.bytes = static_cast<uint64_t>(floppy->SectorCount()) * kSector;
            add(std::move(v), std::make_unique<Plus3Driver>(*_floppy));
        }
        unrealdisk::IsdosFileSystem isdos = unrealdisk::IsdosFileSystem::OnDisk(*_floppy);
        if (isdos.IsIsdos())
        {
            DiskFsVolume v;
            v.id = "isdos";
            DescribeIsdos(isdos, v);
            add(std::move(v), std::make_unique<IsdosDriver>(std::move(isdos)));
        }
        return;
    }

    if (!_device || _device->SectorCount() == 0)
        return;
    IBlockDevice& device = *_device;
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

    // iS-DOS: the whole disk is one device, or devices start at cylinder boundaries (the ATM-Turbo HDD driver)
    auto isdosAt = [&](uint64_t lba, const std::string& id) {
        for (const auto& [first, end] : taken)
            if (lba >= first && lba < end)
                return false;
        uint8_t s[kSector];
        if (!device.ReadSector(lba, s) || !unrealdisk::IsdosFileSystem::IsDescriptor(s))
            return false;
        unrealdisk::IsdosFileSystem fs(BlockReaderAt(device, lba * kSector));
        if (!fs.IsIsdos())
            return false;
        DiskFsVolume v;
        v.id = id;
        v.offset = lba * kSector;
        DescribeIsdos(fs, v);
        taken.emplace_back(lba, lba + (v.bytes + kSector - 1) / kSector);
        add(std::move(v), std::make_unique<IsdosDriver>(std::move(fs)));
        return true;
    };
    if (isdosAt(0, "isdos"))
        return;
    const std::optional<BlockGeometry> geometry = device.NativeGeometry();
    const uint64_t cylinder = geometry && geometry->heads && geometry->sectors
                                  ? static_cast<uint64_t>(geometry->heads) * geometry->sectors
                                  : kDefaultCylinderSectors;
    const uint64_t cylinders = std::min(device.SectorCount() / cylinder, kMaxCylinderScan);
    for (uint64_t c = 1; c < cylinders; c++)
        isdosAt(c * cylinder, "isdos-" + std::to_string(c));
}

DiskFs::Driver* DiskFs::Find(const std::string& volume, std::string& error)
{
    Probe();
    if (_volumes.empty())
    {
        error = "no file system found on the disk";
        return nullptr;
    }
    const std::string wanted = Lower(volume);
    if (wanted.empty())
        return _drivers.front().get();
    for (size_t i = 0; i < _volumes.size(); i++)
        if (Lower(_volumes[i].id) == wanted || Lower(_volumes[i].fs) == wanted || std::to_string(i) == wanted)
            return _drivers[i].get();
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

bool DiskFs::Read(const std::string& volume, const std::string& path, std::vector<uint8_t>& data, std::string& error)
{
    data.clear();
    Driver* driver = Find(volume, error);
    const std::string clean = CleanPath(path);
    if (driver && clean.empty())
    {
        error = "name a file";
        return false;
    }
    return driver && driver->Read(clean, data, error);
}
