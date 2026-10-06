#include "stdafx.h"

#include "changeattributor.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/fat/fatvolumereader.h"

const char* FileChange::OpName(Op op)
{
    switch (op)
    {
        case Op::Create:
            return "create";
        case Op::Modify:
            return "modify";
        case Op::Delete:
            return "delete";
        case Op::Rename:
            return "rename";
        case Op::Mkdir:
            return "mkdir";
        case Op::Rmdir:
            return "rmdir";
        case Op::Attributes:
            return "attributes";
    }
    return "?";
}

namespace
{
    constexpr uint8_t kAttributeBits = 0x07;  // read-only, hidden, system: what a guest sets on purpose

    std::string Key(const std::string& name)
    {
        std::string key = name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return key;
    }

    std::string Join(const std::string& dir, const std::string& name)
    {
        return (dir == "/" ? std::string() : dir) + "/" + name;
    }

    /// An entry that went away or appeared in one directory, before the pairing into renames
    struct Candidate
    {
        std::string path;
        FatDirEntryInfo entry;
        int layer = -1;
    };

    /// One side of the comparison: a reader over the medium and the paths of its directories
    struct Side
    {
        IBlockDevice* device = nullptr;
        FatVolumeReader reader;
        std::unordered_map<uint32_t, std::vector<FatDirEntryInfo>> listings;
        std::unordered_map<uint32_t, std::string> paths;  ///< "" : not a directory of this side
        uint32_t* reads = nullptr;

        uint32_t Normalize(uint32_t cluster) const
        {
            return reader.Type() == FatReaderType::Fat32 && cluster == reader.RootCluster() ? 0 : cluster;
        }

        uint64_t ClusterLba(uint32_t cluster) const
        {
            return reader.VolumeStart() + reader.DataStart() + static_cast<uint64_t>(cluster - 2) * reader.SectorsPerCluster();
        }

        const std::vector<FatDirEntryInfo>* List(uint32_t cluster)
        {
            auto it = listings.find(cluster);
            if (it != listings.end())
                return &it->second;
            std::vector<FatDirEntryInfo> entries;
            (*reads)++;
            if (!reader.ListDirectory(cluster, entries))
                return nullptr;
            return &listings.emplace(cluster, std::move(entries)).first->second;
        }

        /// The path of the directory whose first cluster is `cluster` (0: the root), found through its
        /// ".." entry and its parent's listing; "" when this side has no such directory
        std::string PathOf(uint32_t cluster, int depth = 0)
        {
            if (cluster == 0)
                return "/";
            if (const auto it = paths.find(cluster); it != paths.end())
                return it->second;
            std::string path;
            uint8_t sector[IBlockDevice::kSectorSize];
            if (depth < 64 && cluster >= 2 && cluster < reader.ClusterCount() + 2 && device->ReadSector(ClusterLba(cluster), sector) &&
                sector[0] == '.' && sector[32] == '.' && sector[33] == '.' && (sector[32 + 11] & 0x10))
            {
                uint32_t parent = sector[32 + 26] | (sector[32 + 27] << 8);
                if (reader.Type() == FatReaderType::Fat32)
                    parent |= static_cast<uint32_t>(sector[32 + 20] | (sector[32 + 21] << 8)) << 16;
                parent = Normalize(parent);
                const std::string parentPath = parent == cluster ? std::string() : PathOf(parent, depth + 1);
                if (!parentPath.empty())
                {
                    if (const auto* entries = List(parent))
                    {
                        for (const FatDirEntryInfo& e : *entries)
                        {
                            if (e.isDirectory && e.firstCluster == cluster)
                            {
                                path = Join(parentPath, e.name);
                                break;
                            }
                        }
                    }
                }
            }
            paths[cluster] = path;
            return path;
        }
    };

    class Attribution
    {
    public:
        Attribution(Side& before, Side& after, const IChangeView& changes, const IComposedLayout* layout, ChangeSet& out)
            : _before(before), _after(after), _changes(changes), _layout(layout), _out(out)
        {
        }

        void Run(const std::set<uint32_t>& scope)
        {
            for (uint32_t cluster : scope)
                Compare(cluster);
            Pair();
            CheckFat();
            std::stable_sort(_out.changes.begin(), _out.changes.end(),
                             [](const FileChange& a, const FileChange& b) { return a.path < b.path; });
        }

        bool fatChanged = false;

    private:
        int LayerOf(const FatDirEntryInfo& entry) const
        {
            if (!_layout || entry.firstCluster < 2)
                return -1;
            const SectorOwner owner = _layout->OwnerOf(_before.ClusterLba(entry.firstCluster));
            return owner.Known() ? owner.layer : -1;
        }

        bool DataChanged(const FatDirEntryInfo& entry)
        {
            if (entry.firstCluster < 2)
                return false;
            std::vector<FatChainExtent> extents;
            if (!_after.reader.ChainExtents(entry.firstCluster, entry.size, extents))
                return true;  // a chain the size does not fit: something changed
            for (const FatChainExtent& x : extents)
            {
                if (_changes.ChangedIn(x.lba, x.sectors))
                    return true;
            }
            return false;
        }

        void Emit(FileChange::Op op, const std::string& path, int layer, uint64_t sizeBefore, uint64_t sizeAfter,
                  const std::string& oldPath = {})
        {
            FileChange change;
            change.op = op;
            change.path = path;
            change.oldPath = oldPath;
            change.layer = layer;
            change.sizeBefore = sizeBefore;
            change.sizeAfter = sizeAfter;
            _out.changes.push_back(std::move(change));
        }

        /// The clusters a changed file or directory uses after the writes (lost-cluster check)
        void Claim(const std::string& path, uint32_t firstCluster)
        {
            if (!fatChanged || firstCluster < 2)
                return;
            std::vector<uint32_t> clusters;
            if (!_after.reader.ChainClusters(firstCluster, clusters))
                return;
            for (uint32_t c : clusters)
            {
                const auto [it, added] = _claimed.emplace(c, path);
                if (!added && it->second != path)
                    _out.warnings.push_back("cross-linked: " + it->second + " and " + path + " share cluster " + std::to_string(c));
            }
        }

        /// A directory present before and after (by its first cluster): its entries compared
        void Compare(uint32_t cluster)
        {
            const std::string beforePath = _before.PathOf(cluster);
            const std::string afterPath = _after.PathOf(cluster);
            if (beforePath.empty() || afterPath.empty())
                return;  // made or removed: its parent's comparison reports it
            const auto* beforeList = _before.List(cluster);
            const auto* afterList = _after.List(cluster);
            if (!beforeList || !afterList)
            {
                _out.warnings.push_back(afterPath + ": the directory cannot be read");
                return;
            }
            Claim(afterPath, cluster);

            std::unordered_map<std::string, const FatDirEntryInfo*> before;
            for (const FatDirEntryInfo& e : *beforeList)
                before.emplace(Key(e.name), &e);
            std::unordered_set<std::string> seen;
            for (const FatDirEntryInfo& now : *afterList)
            {
                const std::string key = Key(now.name);
                seen.insert(key);
                const std::string path = Join(afterPath, now.name);
                const auto it = before.find(key);
                if (it == before.end() || it->second->isDirectory != now.isDirectory)
                {
                    if (it != before.end())
                        _deleted.push_back({Join(beforePath, it->second->name), *it->second, LayerOf(*it->second)});
                    _created.push_back({path, now, -1});
                    continue;
                }
                const FatDirEntryInfo& was = *it->second;
                if (now.isDirectory)
                    continue;  // its own sectors bring it into the scope when its entries changed
                const int layer = LayerOf(was);
                if (now.size != was.size || now.firstCluster != was.firstCluster || DataChanged(now))
                {
                    Emit(FileChange::Op::Modify, path, layer, was.size, now.size);
                    Claim(path, now.firstCluster);
                }
                else if ((now.attributes & kAttributeBits) != (was.attributes & kAttributeBits))
                    Emit(FileChange::Op::Attributes, path, layer, was.size, now.size);
            }
            for (const FatDirEntryInfo& was : *beforeList)
            {
                if (!seen.count(Key(was.name)))
                    _deleted.push_back({Join(beforePath, was.name), was, LayerOf(was)});
            }
        }

        /// A delete and a create of the same chain are a rename (or a move); the rest expand
        void Pair()
        {
            std::vector<bool> used(_created.size(), false);
            for (const Candidate& gone : _deleted)
            {
                size_t match = _created.size();
                for (size_t i = 0; i < _created.size() && match == _created.size(); i++)
                {
                    const FatDirEntryInfo& e = _created[i].entry;
                    if (!used[i] && gone.entry.firstCluster >= 2 && e.firstCluster == gone.entry.firstCluster &&
                        e.isDirectory == gone.entry.isDirectory && e.size == gone.entry.size)
                        match = i;
                }
                if (match != _created.size())
                {
                    used[match] = true;
                    Emit(FileChange::Op::Rename, _created[match].path, gone.layer, gone.entry.size, gone.entry.size, gone.path);
                    continue;
                }
                if (gone.entry.isDirectory)
                    ExpandDeleted(gone.path, gone.entry.firstCluster, gone.layer, 0);
                else
                    Emit(FileChange::Op::Delete, gone.path, gone.layer, gone.entry.size, 0);
            }
            for (size_t i = 0; i < _created.size(); i++)
            {
                if (used[i])
                    continue;
                const Candidate& made = _created[i];
                if (made.entry.isDirectory)
                    ExpandCreated(made.path, made.entry.firstCluster, 0);
                else
                {
                    Emit(FileChange::Op::Create, made.path, -1, 0, made.entry.size);
                    Claim(made.path, made.entry.firstCluster);
                }
            }
        }

        void ExpandDeleted(const std::string& path, uint32_t cluster, int layer, int depth)
        {
            Emit(FileChange::Op::Rmdir, path, layer, 0, 0);
            if (depth > 64 || cluster < 2)
                return;
            if (const auto* entries = _before.List(cluster))
            {
                for (const FatDirEntryInfo& e : *entries)
                {
                    if (e.isDirectory)
                        ExpandDeleted(Join(path, e.name), e.firstCluster, LayerOf(e), depth + 1);
                    else
                        Emit(FileChange::Op::Delete, Join(path, e.name), LayerOf(e), e.size, 0);
                }
            }
        }

        void ExpandCreated(const std::string& path, uint32_t cluster, int depth)
        {
            Emit(FileChange::Op::Mkdir, path, -1, 0, 0);
            Claim(path, cluster);
            if (depth > 64 || cluster < 2)
                return;
            if (const auto* entries = _after.List(cluster))
            {
                for (const FatDirEntryInfo& e : *entries)
                {
                    if (e.isDirectory)
                        ExpandCreated(Join(path, e.name), e.firstCluster, depth + 1);
                    else
                    {
                        Emit(FileChange::Op::Create, Join(path, e.name), -1, 0, e.size);
                        Claim(Join(path, e.name), e.firstCluster);
                    }
                }
            }
        }

        /// Clusters the writes allocated in the first FAT that no changed file or directory uses
        void CheckFat()
        {
            if (!fatChanged)
                return;
            const FatVolumeReader& r = _before.reader;
            const uint64_t first = r.VolumeStart() + r.ReservedSectors();
            const bool fat32 = r.Type() == FatReaderType::Fat32;
            const bool fat12 = r.Type() == FatReaderType::Fat12;
            uint64_t lost = 0;
            uint8_t was[IBlockDevice::kSectorSize];
            uint8_t now[IBlockDevice::kSectorSize];
            for (std::optional<uint64_t> lba = _changes.NextChanged(first); lba && *lba < first + r.FatSectors();
                 lba = _changes.NextChanged(*lba + 1))
            {
                if (!_before.device->ReadSector(*lba, was) || !_changes.ReadChanged(*lba, now))
                    continue;
                const uint64_t sectorIndex = *lba - first;
                if (fat12)
                    continue;  // 12-bit entries straddle sectors: FAT12 media are floppies, checked by their own tools
                const uint32_t perSector = fat32 ? 128 : 256;
                for (uint32_t i = 0; i < perSector; i++)
                {
                    const uint64_t cluster = sectorIndex * perSector + i;
                    if (cluster < 2 || cluster >= static_cast<uint64_t>(r.ClusterCount()) + 2)
                        continue;
                    uint32_t before = 0, after = 0;
                    if (fat32)
                    {
                        before = (was[i * 4] | (was[i * 4 + 1] << 8) | (was[i * 4 + 2] << 16) | (static_cast<uint32_t>(was[i * 4 + 3]) << 24)) &
                                 0x0FFFFFFF;
                        after = (now[i * 4] | (now[i * 4 + 1] << 8) | (now[i * 4 + 2] << 16) | (static_cast<uint32_t>(now[i * 4 + 3]) << 24)) &
                                0x0FFFFFFF;
                    }
                    else
                    {
                        before = was[i * 2] | (was[i * 2 + 1] << 8);
                        after = now[i * 2] | (now[i * 2 + 1] << 8);
                    }
                    if (before == 0 && after != 0 && !_claimed.count(static_cast<uint32_t>(cluster)))
                        lost++;
                }
            }
            if (lost)
                _out.warnings.push_back(std::to_string(lost) +
                                        " cluster(s) allocated in the FAT that no changed file or directory uses (lost clusters)");
        }

        Side& _before;
        Side& _after;
        const IChangeView& _changes;
        const IComposedLayout* _layout;
        ChangeSet& _out;
        std::vector<Candidate> _deleted;
        std::vector<Candidate> _created;
        std::map<uint32_t, std::string> _claimed;
    };

    /// Every directory of a side (the full scan without a layout)
    void AllDirectories(Side& side, uint32_t cluster, std::set<uint32_t>& out, int depth)
    {
        if (!out.insert(cluster).second || depth > 64)
            return;
        if (const auto* entries = side.List(cluster))
        {
            for (const FatDirEntryInfo& e : *entries)
            {
                if (e.isDirectory && e.firstCluster >= 2)
                    AllDirectories(side, e.firstCluster, out, depth + 1);
            }
        }
    }
}  // namespace

bool ChangeAttributor::Attribute(IBlockDevice& before, IBlockDevice& after, const IChangeView& changes, const IComposedLayout* layout,
                                 ChangeSet& out, std::string* error)
{
    out = ChangeSet();
    out.changedSectors = changes.ChangedSectors();
    Side b, a;
    b.device = &before;
    a.device = &after;
    b.reads = a.reads = &out.directoriesRead;
    std::string why;
    if (!b.reader.Open(before, CodePage::Cp866, &why))
    {
        if (error)
            *error = "not a FAT volume: " + why;
        return false;
    }
    if (!changes.ChangedSectors())
        return true;
    if (!a.reader.Open(after, CodePage::Cp866, &why))
    {
        out.warnings.push_back("the volume cannot be read any more (" + why + "): reformatted or broken");
        return true;
    }

    // The evidence: what the changed sectors held
    const FatVolumeReader& r = b.reader;
    const uint64_t volumeStart = r.VolumeStart();
    const uint64_t fatStart = volumeStart + r.ReservedSectors();
    const uint64_t fatEnd = fatStart + static_cast<uint64_t>(r.FatCount()) * r.FatSectors();
    const uint64_t rootEnd = fatEnd + r.RootDirSectors();
    std::set<uint32_t> scope;
    std::set<std::string> notes;
    bool fatChanged = false;
    bool unplaced = false;
    for (std::optional<uint64_t> next = changes.NextChanged(0); next; next = changes.NextChanged(*next + 1))
    {
        const uint64_t lba = *next;
        if (lba < volumeStart)
            notes.insert(lba == 0 ? "the partition table (LBA 0) changed" : "a sector between the MBR and the partition changed");
        else if (lba < fatStart)
        {
            const uint64_t rel = lba - volumeStart;
            const bool fsInfo = r.Type() == FatReaderType::Fat32 && (rel == r.FsInfoSector() || rel == 7);
            if (!fsInfo)
                notes.insert("the boot sector or a reserved sector changed");
        }
        else if (lba < fatEnd)
            fatChanged = true;
        else if (lba < rootEnd)
            scope.insert(0);
        else if (layout)
        {
            const SectorOwner owner = layout->OwnerOf(lba);
            if (owner.role == SectorRole::Directory || owner.role == SectorRole::FileData)
                scope.insert(owner.Known() ? owner.dirCluster : 0);
            // Free: new data; the entries that name it are in a changed directory
        }
        else
            unplaced = true;
    }
    for (const std::string& note : notes)
        out.warnings.push_back(note);
    if (!layout && (unplaced || fatChanged))
    {
        out.fullScan = true;
        AllDirectories(b, 0, scope, 0);
    }

    Attribution attribution(b, a, changes, layout, out);
    attribution.fatChanged = fatChanged;
    attribution.Run(scope);
    return true;
}
