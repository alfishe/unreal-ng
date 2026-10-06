#include "stdafx.h"

#include "graftvolume.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>

#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/compose/unionbuilder.h"
#include "emulator/io/storage/hostfolder/fatnamemapper.h"
#include "emulator/io/storage/subrangedevice.h"

namespace
{
    constexpr uint32_t kSector = 512;

    void Put16(uint8_t* p, uint32_t v)
    {
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
    }
    void Put32(uint8_t* p, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
    uint32_t Get32(const uint8_t* p)
    {
        return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24);
    }

    /// A short name as the 11 bytes of a directory entry
    FatShortName ShortNameOf(const std::vector<uint8_t>& slots)
    {
        FatShortName name{};
        std::copy(slots.end() - 32, slots.end() - 21, name.begin());
        return name;
    }

    /// Free clusters as runs, allocated best fit (whole file in one run) or, failing
    /// that, from the largest runs in turn
    class FreeRuns
    {
    public:
        struct Piece
        {
            uint32_t first;
            uint32_t count;
        };

        explicit FreeRuns(const std::vector<bool>& free)
        {
            for (uint32_t i = 0; i < free.size();)
            {
                if (!free[i])
                {
                    i++;
                    continue;
                }
                uint32_t j = i;
                while (j < free.size() && free[j])
                    j++;
                _runs.push_back({i + 2, j - i});
                _total += j - i;
                i = j;
            }
        }

        uint64_t Total() const { return _total; }

        /// `count` clusters (count <= Total())
        std::vector<Piece> Allocate(uint32_t count)
        {
            std::vector<Piece> pieces;
            if (count == 0)
                return pieces;
            // Best fit: the smallest run that holds it all
            size_t best = _runs.size();
            for (size_t i = 0; i < _runs.size(); i++)
            {
                if (_runs[i].count >= count && (best == _runs.size() || _runs[i].count < _runs[best].count))
                    best = i;
            }
            if (best != _runs.size())
            {
                pieces.push_back({_runs[best].first, count});
                Take(best, count);
                return pieces;
            }
            // Fragments: the largest runs first
            while (count > 0 && !_runs.empty())
            {
                size_t largest = 0;
                for (size_t i = 1; i < _runs.size(); i++)
                {
                    if (_runs[i].count > _runs[largest].count)
                        largest = i;
                }
                const uint32_t take = std::min(count, _runs[largest].count);
                pieces.push_back({_runs[largest].first, take});
                Take(largest, take);
                count -= take;
            }
            return pieces;
        }

        /// The lowest free cluster, or 0 when none is left
        uint32_t FirstFree() const
        {
            uint32_t first = 0;
            for (const Piece& run : _runs)
            {
                if (run.count > 0 && (first == 0 || run.first < first))
                    first = run.first;
            }
            return first;
        }

    private:
        void Take(size_t index, uint32_t count)
        {
            _runs[index].first += count;
            _runs[index].count -= count;
            _total -= count;
            if (_runs[index].count == 0)
                _runs.erase(_runs.begin() + static_cast<std::ptrdiff_t>(index));
        }

        std::vector<Piece> _runs;
        uint64_t _total = 0;
    };
}  // namespace

/// The build, kept apart from the volume (which holds only what reads need)
class GraftBuilder
{
public:
    GraftBuilder(GraftVolume& volume, FatVolumeReader& reader, IBlockDevice& base, uint64_t volumeStart, const GraftOptions& options,
                 std::vector<std::string>* report)
        : _volume(volume), _reader(reader), _base(base), _volumeStart(volumeStart), _options(options), _report(report),
          _tree(*volume._tree)
    {
    }

    bool Build(std::string* error, GraftFailure* failure);

private:
    /// A directory to write: a touched base directory or a new one
    struct DirJob
    {
        uint32_t node = 0;                      ///< the union directory
        bool isNew = false;
        bool isRoot = false;
        bool fixedRoot = false;                 ///< the FAT12 / FAT16 root region
        size_t parentJob = SIZE_MAX;            ///< new directories: the job of the parent
        uint32_t parentCluster = 0;             ///< "..": the parent's first cluster, 0 for the root
        std::vector<uint32_t> chain;            ///< clusters (base: existing, then grown; new: allocated)
        std::vector<std::vector<uint8_t>> kept; ///< slot groups copied from the base, in base order
        std::vector<std::pair<size_t, uint32_t>> replaced;  ///< (kept index, union file node)
        std::vector<uint32_t> added;            ///< union children to add, in union order
        std::vector<FatMappedName> addedNames;
        uint32_t slots = 0;
    };

    bool Walk(uint32_t baseCluster, uint32_t unionDir, bool isRoot, uint32_t parentCluster, std::string* error);
    void AddNewDirectory(uint32_t node, size_t parentJob);
    bool Release(const FatDirEntryInfo& entry, std::string* error);
    bool NameAndSize(std::string* error, GraftFailure* failure);
    bool Allocate(std::string* error, GraftFailure* failure);
    void Encode();
    void PatchFsInfo();
    bool PatchBoot(std::string* error);

    void SetFat(uint32_t cluster, uint32_t value);
    void Chain(const std::vector<uint32_t>& clusters);
    uint32_t EndOfChain() const;
    uint8_t* PatchSector(uint64_t lba, bool loadFromBase);
    /// Provenance: the clusters of directory `node`
    void RecordDirectory(uint32_t node, bool isRoot, const std::vector<uint32_t>& chain)
    {
        const uint32_t first = isRoot || chain.empty() ? 0 : chain.front();
        _volume._dirClusterOfNode[node] = first;
        for (uint32_t cluster : chain)
            _volume._dirClusters.push_back({cluster, first, node});
    }
    uint64_t ClusterLba(uint32_t cluster) const
    {
        return _volumeStart + _reader.DataStart() + static_cast<uint64_t>(cluster - 2) * _reader.SectorsPerCluster();
    }
    uint32_t ClusterBytes() const { return _reader.SectorsPerCluster() * kSector; }
    void Note(const std::string& line)
    {
        if (_report)
            _report->push_back(line);
    }

    GraftVolume& _volume;
    FatVolumeReader& _reader;
    IBlockDevice& _base;
    uint64_t _volumeStart;
    const GraftOptions& _options;
    std::vector<std::string>* _report;
    const FileTree& _tree;

    std::vector<bool> _free;
    std::vector<DirJob> _jobs;
    std::vector<uint32_t> _files;                     ///< added union files with data
    std::map<uint32_t, uint32_t> _fileFirstCluster;   ///< union file node -> first cluster
    std::map<uint32_t, size_t> _dirJobOf;             ///< new directory node -> job
    std::map<uint32_t, std::array<uint8_t, 512>> _fat; ///< FAT sectors (copy 0) being patched
    std::map<uint64_t, std::array<uint8_t, 512>> _patches;
    uint32_t _firstFree = 0;
};

bool GraftBuilder::Build(std::string* error, GraftFailure* failure)
{
    *failure = GraftFailure::Broken;
    if (!_reader.ScanFree(_free, error))
        return false;
    if (!Walk(0, FileTree::kRoot, true, 0, error))
        return false;
    if (!NameAndSize(error, failure))
        return false;
    if (!Allocate(error, failure))
        return false;
    Encode();
    PatchFsInfo();
    if (!PatchBoot(error))
    {
        *failure = GraftFailure::DoesNotFit;
        return false;
    }

    // Every changed FAT sector, in every FAT copy
    for (const auto& [index, bytes] : _fat)
    {
        for (uint32_t copy = 0; copy < _reader.FatCount(); copy++)
        {
            const uint64_t lba = _volumeStart + _reader.ReservedSectors() + static_cast<uint64_t>(copy) * _reader.FatSectors() + index;
            std::memcpy(PatchSector(lba, false), bytes.data(), kSector);
        }
    }
    _volume._patchLba.reserve(_patches.size());
    _volume._patchData.reserve(_patches.size() * kSector);
    for (const auto& [lba, bytes] : _patches)
    {
        _volume._patchLba.push_back(lba);
        _volume._patchData.insert(_volume._patchData.end(), bytes.begin(), bytes.end());
    }
    std::sort(_volume._runs.begin(), _volume._runs.end(),
              [](const GraftVolume::Run& a, const GraftVolume::Run& b) { return a.firstCluster < b.firstCluster; });
    for (const DirJob& job : _jobs)
        RecordDirectory(job.node, job.isRoot, job.fixedRoot ? std::vector<uint32_t>() : job.chain);
    std::sort(_volume._dirClusters.begin(), _volume._dirClusters.end(),
              [](const GraftVolume::DirCluster& a, const GraftVolume::DirCluster& b) { return a.cluster < b.cluster; });
    *failure = GraftFailure::None;
    return true;
}

bool GraftBuilder::Walk(uint32_t baseCluster, uint32_t unionDir, bool isRoot, uint32_t parentCluster, std::string* error)
{
    std::vector<FatRawEntry> raw;
    std::vector<FatDirEntryInfo> entries;
    std::string why;
    if (!_reader.ReadRawDirectory(baseCluster, raw, entries, &why))
    {
        if (error)
            *error = _tree.PathOf(unionDir) + ": " + why;
        return false;
    }

    DirJob job;
    job.node = unionDir;
    job.isRoot = isRoot;
    job.fixedRoot = isRoot && _reader.Type() != FatReaderType::Fat32;
    job.parentCluster = parentCluster;
    const uint32_t ownCluster = isRoot ? (_reader.Type() == FatReaderType::Fat32 ? _reader.RootCluster() : 0) : baseCluster;
    if (!job.fixedRoot && !_reader.ChainClusters(ownCluster, job.chain, &why))
    {
        if (error)
            *error = _tree.PathOf(unionDir) + ": " + why;
        return false;
    }

    const std::vector<uint32_t>& children = _tree.Node(unionDir).children;
    std::vector<bool> matched(children.size(), false);
    bool touched = false;
    std::vector<std::pair<FatDirEntryInfo, uint32_t>> recurse;

    size_t info = 0;
    for (const FatRawEntry& group : raw)
    {
        if (group.isLabel)
        {
            job.kept.push_back(group.slots);
            continue;
        }
        const FatDirEntryInfo& entry = entries[info++];

        // The base entry itself, unchanged: a layer-0 union child of the same name
        size_t found = children.size();
        for (size_t i = 0; i < children.size() && found == children.size(); i++)
        {
            const TreeNode& child = _tree.Node(children[i]);
            if (!matched[i] && child.layer == 0 && child.isDirectory == entry.isDirectory && child.name == entry.name)
                found = i;
        }
        if (found != children.size())
        {
            matched[found] = true;
            job.kept.push_back(group.slots);
            if (entry.isDirectory)
                recurse.push_back({entry, children[found]});
            continue;
        }

        touched = true;
        // An upper file of the same FAT key replaces a base file in place
        if (!entry.isDirectory)
        {
            const std::string key = UnionBuilder::FatKey(entry.name);
            for (size_t i = 0; i < children.size() && found == children.size(); i++)
            {
                const TreeNode& child = _tree.Node(children[i]);
                if (!matched[i] && child.layer != 0 && !child.isDirectory && UnionBuilder::FatKey(child.name) == key)
                    found = i;
            }
        }
        if (!Release(entry, error))
            return false;
        if (found != children.size())
        {
            matched[found] = true;
            job.replaced.push_back({job.kept.size(), children[found]});
            job.kept.push_back(group.slots);
            if (_tree.Node(children[found]).data.bytes > 0)
                _files.push_back(children[found]);
        }
    }

    for (size_t i = 0; i < children.size(); i++)
    {
        if (!matched[i])
        {
            job.added.push_back(children[i]);
            touched = true;
        }
    }

    size_t self = SIZE_MAX;
    if (!touched)
        RecordDirectory(job.node, isRoot, job.chain);
    if (touched)
    {
        self = _jobs.size();
        const std::vector<uint32_t> added = job.added;
        _jobs.push_back(std::move(job));
        for (uint32_t child : added)  // AddNewDirectory grows _jobs: no reference into it here
        {
            const TreeNode& node = _tree.Node(child);
            if (node.isDirectory)
                AddNewDirectory(child, self);
            else if (node.data.bytes > 0)
                _files.push_back(child);
        }
    }

    for (const auto& [entry, node] : recurse)
    {
        if (!Walk(entry.firstCluster, node, false, isRoot ? 0 : baseCluster, error))
            return false;
    }
    return true;
}

void GraftBuilder::AddNewDirectory(uint32_t node, size_t parentJob)
{
    DirJob job;
    job.node = node;
    job.isNew = true;
    job.parentJob = parentJob;
    job.added = _tree.Node(node).children;
    const size_t self = _jobs.size();
    _dirJobOf[node] = self;
    _jobs.push_back(std::move(job));
    for (uint32_t child : _tree.Node(node).children)
    {
        const TreeNode& c = _tree.Node(child);
        if (c.isDirectory)
            AddNewDirectory(child, self);
        else if (c.data.bytes > 0)
            _files.push_back(child);
    }
}

bool GraftBuilder::Release(const FatDirEntryInfo& entry, std::string* error)
{
    if (entry.firstCluster < 2)
        return true;  // an empty file
    if (entry.isDirectory)
    {
        std::vector<FatDirEntryInfo> children;
        std::string why;
        if (!_reader.ListDirectory(entry.firstCluster, children, &why))
        {
            if (error)
                *error = entry.name + ": " + why;
            return false;
        }
        for (const FatDirEntryInfo& child : children)
        {
            if (!Release(child, error))
                return false;
        }
    }
    std::vector<uint32_t> clusters;
    std::string why;
    if (!_reader.ChainClusters(entry.firstCluster, clusters, &why))
    {
        // A broken chain: release what the entry's size says can be trusted, nothing more
        Note(entry.name + ": its cluster chain is broken (" + why + "), its clusters stay allocated");
        return true;
    }
    for (uint32_t c : clusters)
    {
        _free[c - 2] = true;
        SetFat(c, 0);
    }
    return true;
}

bool GraftBuilder::NameAndSize(std::string* error, GraftFailure* failure)
{
    for (DirJob& job : _jobs)
    {
        std::set<FatShortName> taken;
        uint32_t slots = job.isRoot ? 0 : 2;  // "." and ".." in every directory but the root
        for (const auto& group : job.kept)
        {
            taken.insert(ShortNameOf(group));
            slots += static_cast<uint32_t>(group.size() / 32);
        }
        std::vector<std::string> names;
        for (uint32_t child : job.added)
            names.push_back(_tree.Node(child).name);
        job.addedNames = FatNameMapper::MapFolder(names, _options.codePage, taken);

        std::vector<uint32_t> keptAdded;
        std::vector<FatMappedName> keptNames;
        for (size_t i = 0; i < job.added.size(); i++)
        {
            const FatMappedName& name = job.addedNames[i];
            if (!name.ok)
            {
                Note(_tree.PathOf(job.added[i]) + ": skipped, " + name.reason);
                continue;
            }
            keptAdded.push_back(job.added[i]);
            keptNames.push_back(name);
            slots += 1 + (name.hasLongName ? static_cast<uint32_t>((name.longName.size() + 12) / 13) : 0);
        }
        job.added = std::move(keptAdded);
        job.addedNames = std::move(keptNames);
        job.slots = slots;

        if (job.fixedRoot && slots > _reader.RootEntries())
        {
            *failure = GraftFailure::DoesNotFit;
            if (error)
                *error = "the root directory needs " + std::to_string(slots) + " entries, the base's holds " +
                         std::to_string(_reader.RootEntries());
            return false;
        }
    }
    return true;
}

bool GraftBuilder::Allocate(std::string* error, GraftFailure* failure)
{
    const uint32_t clusterBytes = ClusterBytes();
    auto clustersFor = [clusterBytes](uint64_t bytes) { return static_cast<uint32_t>((bytes + clusterBytes - 1) / clusterBytes); };

    // What is needed: directory growth, new directories, files
    uint64_t needed = 0;
    for (const DirJob& job : _jobs)
    {
        if (job.fixedRoot)
            continue;
        const uint32_t want = std::max<uint32_t>(1, clustersFor(static_cast<uint64_t>(job.slots) * 32));
        if (want > job.chain.size())
            needed += want - job.chain.size();
    }
    std::vector<uint32_t> files = _files;
    std::sort(files.begin(), files.end(), [this](uint32_t a, uint32_t b) {
        const uint64_t sa = _tree.Node(a).data.bytes, sb = _tree.Node(b).data.bytes;
        return sa != sb ? sa > sb : a < b;
    });
    files.erase(std::unique(files.begin(), files.end()), files.end());
    for (uint32_t f : files)
        needed += clustersFor(_tree.Node(f).data.bytes);

    FreeRuns runs(_free);
    if (needed > runs.Total())
    {
        *failure = GraftFailure::DoesNotFit;
        if (error)
            *error = "the upper layers need " + std::to_string(needed) + " clusters, the base has " +
                     std::to_string(runs.Total()) + " free (" + std::to_string((needed - runs.Total()) * clusterBytes / 1024) +
                     " KiB short)";
        return false;
    }

    auto take = [&runs](uint32_t count) {
        std::vector<uint32_t> clusters;
        for (const FreeRuns::Piece& piece : runs.Allocate(count))
        {
            for (uint32_t c = 0; c < piece.count; c++)
                clusters.push_back(piece.first + c);
        }
        return clusters;
    };

    // Directories: grow or create
    for (DirJob& job : _jobs)
    {
        if (job.fixedRoot)
            continue;
        const uint32_t want = std::max<uint32_t>(1, clustersFor(static_cast<uint64_t>(job.slots) * 32));
        if (want <= job.chain.size())
            continue;
        const std::vector<uint32_t> more = take(want - static_cast<uint32_t>(job.chain.size()));
        const bool grows = !job.chain.empty();
        job.chain.insert(job.chain.end(), more.begin(), more.end());
        Chain(job.chain);
        if (grows)
            Note(_tree.PathOf(job.node) + ": directory grown by " + std::to_string(more.size()) + " cluster(s)");
    }
    // New directories know their parents' clusters now
    for (DirJob& job : _jobs)
    {
        if (!job.isNew)
            continue;
        const DirJob& parent = _jobs[job.parentJob];
        const bool parentIsRoot = !parent.isNew && parent.node == FileTree::kRoot;
        job.parentCluster = parentIsRoot ? 0 : parent.chain.front();
    }

    // Files, largest first
    for (uint32_t f : files)
    {
        const FileData& data = _tree.Node(f).data;
        const uint32_t count = clustersFor(data.bytes);
        uint32_t fileCluster = 0;
        std::vector<uint32_t> all;
        for (const FreeRuns::Piece& piece : runs.Allocate(count))
        {
            _volume._runs.push_back({piece.first, piece.count, fileCluster, f});
            fileCluster += piece.count;
            for (uint32_t c = 0; c < piece.count; c++)
                all.push_back(piece.first + c);
        }
        Chain(all);
        _fileFirstCluster[f] = all.front();
        _volume._filesGrafted++;
    }
    _volume._freeClusters = static_cast<uint32_t>(runs.Total());
    _firstFree = runs.FirstFree();
    return true;
}

void GraftBuilder::Encode()
{
    const uint32_t clusterBytes = ClusterBytes();
    for (const DirJob& job : _jobs)
    {
        std::vector<uint8_t> bytes(job.fixedRoot ? static_cast<size_t>(_reader.RootDirSectors()) * kSector
                                                 : job.chain.size() * clusterBytes,
                                   0);
        size_t at = 0;
        auto entry = [&](const FatShortName& name, uint8_t attr, uint32_t cluster, uint32_t size, int64_t mtime) {
            uint8_t* e = bytes.data() + at;
            std::copy(name.begin(), name.end(), e);
            e[11] = attr;
            uint16_t date = 0;
            uint16_t time = 0;
            FatVolumeReader::UnixToDos(_options.fixedTimeUtc.value_or(mtime), date, time);
            Put16(e + 14, time);
            Put16(e + 16, date);
            Put16(e + 18, date);
            Put16(e + 20, cluster >> 16);
            Put16(e + 22, time);
            Put16(e + 24, date);
            Put16(e + 26, cluster & 0xFFFF);
            Put32(e + 28, size);
            at += 32;
        };
        auto clusterOf = [this](uint32_t node) -> uint32_t {
            const TreeNode& n = _tree.Node(node);
            if (n.isDirectory)
            {
                const auto it = _dirJobOf.find(node);
                return it == _dirJobOf.end() ? 0 : _jobs[it->second].chain.front();
            }
            const auto it = _fileFirstCluster.find(node);
            return it == _fileFirstCluster.end() ? 0 : it->second;
        };

        if (!job.isRoot)
        {
            FatShortName dot;
            dot.fill(' ');
            dot[0] = '.';
            const int64_t mtime = _tree.Node(job.node).mtimeUtc;
            entry(dot, 0x10, job.chain.front(), 0, mtime);
            dot[1] = '.';
            entry(dot, 0x10, job.parentCluster, 0, mtime);
        }
        for (size_t k = 0; k < job.kept.size(); k++)
        {
            const std::vector<uint8_t>& group = job.kept[k];
            std::copy(group.begin(), group.end(), bytes.begin() + static_cast<std::ptrdiff_t>(at));
            at += group.size();
            for (const auto& [index, node] : job.replaced)
            {
                if (index != k)
                    continue;
                uint8_t* e = bytes.data() + at - 32;  // the short entry
                const TreeNode& n = _tree.Node(node);
                const uint32_t cluster = clusterOf(node);
                uint16_t date = 0;
                uint16_t time = 0;
                FatVolumeReader::UnixToDos(_options.fixedTimeUtc.value_or(n.mtimeUtc), date, time);
                e[11] = static_cast<uint8_t>((e[11] & ~0x07) | (n.attributes & 0x07));
                Put16(e + 20, cluster >> 16);
                Put16(e + 22, time);
                Put16(e + 24, date);
                Put16(e + 26, cluster & 0xFFFF);
                Put32(e + 28, static_cast<uint32_t>(n.data.bytes));
            }
        }
        for (size_t i = 0; i < job.added.size(); i++)
        {
            const TreeNode& node = _tree.Node(job.added[i]);
            const FatMappedName& name = job.addedNames[i];
            if (name.hasLongName)
            {
                for (const auto& lfn : FatNameMapper::LongNameEntries(name.longName, FatNameMapper::Checksum(name.shortName)))
                {
                    std::copy(lfn.begin(), lfn.end(), bytes.data() + at);
                    at += 32;
                }
            }
            const uint8_t attr = static_cast<uint8_t>((node.isDirectory ? 0x10 : 0x20) | (node.attributes & 0x07));
            entry(name.shortName, attr, clusterOf(job.added[i]), node.isDirectory ? 0 : static_cast<uint32_t>(node.data.bytes),
                  node.mtimeUtc);
        }

        // Into patch sectors
        if (job.fixedRoot)
        {
            const uint64_t first =
                _volumeStart + _reader.ReservedSectors() + static_cast<uint64_t>(_reader.FatCount()) * _reader.FatSectors();
            for (uint32_t s = 0; s < _reader.RootDirSectors(); s++)
                std::memcpy(PatchSector(first + s, false), bytes.data() + static_cast<size_t>(s) * kSector, kSector);
        }
        else
        {
            for (size_t c = 0; c < job.chain.size(); c++)
            {
                for (uint32_t s = 0; s < _reader.SectorsPerCluster(); s++)
                    std::memcpy(PatchSector(ClusterLba(job.chain[c]) + s, false),
                                bytes.data() + c * clusterBytes + static_cast<size_t>(s) * kSector, kSector);
            }
        }
        _volume._directoriesEncoded++;
    }
}

void GraftBuilder::PatchFsInfo()
{
    if (_reader.Type() != FatReaderType::Fat32 || _reader.FsInfoSector() == 0)
        return;
    const uint64_t lba = _volumeStart + _reader.FsInfoSector();
    uint8_t* s = PatchSector(lba, true);
    if (Get32(s) != 0x41615252 || Get32(s + 484) != 0x61417272)
    {
        _patches.erase(lba);
        return;
    }
    Put32(s + 488, _volume._freeClusters);
    Put32(s + 492, _firstFree ? _firstFree : 0xFFFFFFFF);
}

bool GraftBuilder::PatchBoot(std::string* error)
{
    if (!_options.boot || _options.boot->Empty())
        return true;
    const FatBootPlan& boot = *_options.boot;
    auto fail = [error](const std::string& text) {
        if (error)
            *error = text;
        return false;
    };
    const bool fat32 = _reader.Type() == FatReaderType::Fat32;
    if (!boot.mbrCode.empty())
    {
        if (_volumeStart == 0)
            return fail("boot: the base has no MBR for the MBR code");
        if (boot.mbrCode.size() > 446)
            return fail("boot: the MBR code is " + std::to_string(boot.mbrCode.size()) + " bytes, the MBR holds 446");
        std::copy(boot.mbrCode.begin(), boot.mbrCode.end(), PatchSector(0, true));
    }
    if (!boot.volumeCode.empty())
    {
        const size_t codeStart = fat32 ? 90 : 62;
        uint8_t* s = PatchSector(_volumeStart, true);
        const bool partitionEntry = _volumeStart == 0 && std::any_of(s + 446, s + 510, [](uint8_t b) { return b != 0; });
        const size_t codeEnd = partitionEntry ? 446 : 510;
        if (boot.volumeCode.size() > codeEnd - codeStart)
            return fail("boot: the volume boot code is " + std::to_string(boot.volumeCode.size()) + " bytes, the base's boot sector holds " +
                        std::to_string(codeEnd - codeStart));
        std::copy(boot.volumeCode.begin(), boot.volumeCode.end(), s + codeStart);
        const uint32_t backup = fat32 ? static_cast<uint32_t>(s[50] | (s[51] << 8)) : 0;
        if (backup != 0 && backup < _reader.ReservedSectors())
            std::copy(boot.volumeCode.begin(), boot.volumeCode.end(), PatchSector(_volumeStart + backup, true) + codeStart);
    }
    for (const auto& [lba, sector] : boot.reserved)
    {
        if (lba >= _reader.ReservedSectors())
            return fail("boot: reserved sector " + std::to_string(lba) + " is past the base's " + std::to_string(_reader.ReservedSectors()) +
                        " reserved sectors (a graft keeps the base's layout)");
        if (fat32 && (lba == _reader.FsInfoSector() || lba == 6 || lba == 7))
            return fail("boot: reserved sector " + std::to_string(lba) + " is the base's FSInfo or boot record backup");
        std::memcpy(PatchSector(_volumeStart + lba, false), sector.data(), kSector);
    }
    return true;
}

uint32_t GraftBuilder::EndOfChain() const
{
    switch (_reader.Type())
    {
        case FatReaderType::Fat12: return 0xFFF;
        case FatReaderType::Fat16: return 0xFFFF;
        case FatReaderType::Fat32: return 0x0FFFFFFF;
    }
    return 0x0FFFFFFF;
}

void GraftBuilder::Chain(const std::vector<uint32_t>& clusters)
{
    for (size_t i = 0; i < clusters.size(); i++)
        SetFat(clusters[i], i + 1 < clusters.size() ? clusters[i + 1] : EndOfChain());
}

void GraftBuilder::SetFat(uint32_t cluster, uint32_t value)
{
    auto sector = [this](uint32_t index) -> uint8_t* {
        auto it = _fat.find(index);
        if (it == _fat.end())
        {
            std::array<uint8_t, 512> bytes{};
            _base.ReadSector(_volumeStart + _reader.ReservedSectors() + index, bytes.data());
            it = _fat.emplace(index, bytes).first;
        }
        return it->second.data();
    };
    auto setByte = [&sector](uint64_t offset, uint8_t v) { sector(static_cast<uint32_t>(offset / kSector))[offset % kSector] = v; };
    auto getByte = [&sector](uint64_t offset) { return sector(static_cast<uint32_t>(offset / kSector))[offset % kSector]; };

    switch (_reader.Type())
    {
        case FatReaderType::Fat12:
        {
            const uint64_t at = cluster + cluster / 2;
            uint16_t v = static_cast<uint16_t>(getByte(at) | (getByte(at + 1) << 8));
            v = (cluster & 1) ? static_cast<uint16_t>((v & 0x000F) | ((value & 0xFFF) << 4))
                              : static_cast<uint16_t>((v & 0xF000) | (value & 0xFFF));
            setByte(at, static_cast<uint8_t>(v));
            setByte(at + 1, static_cast<uint8_t>(v >> 8));
            break;
        }
        case FatReaderType::Fat16:
        {
            const uint64_t at = static_cast<uint64_t>(cluster) * 2;
            setByte(at, static_cast<uint8_t>(value));
            setByte(at + 1, static_cast<uint8_t>(value >> 8));
            break;
        }
        case FatReaderType::Fat32:
        {
            const uint64_t at = static_cast<uint64_t>(cluster) * 4;
            const uint32_t high = getByte(at + 3) & 0xF0u;  // the reserved top bits stay
            setByte(at, static_cast<uint8_t>(value));
            setByte(at + 1, static_cast<uint8_t>(value >> 8));
            setByte(at + 2, static_cast<uint8_t>(value >> 16));
            setByte(at + 3, static_cast<uint8_t>(high | ((value >> 24) & 0x0F)));
            break;
        }
    }
}

uint8_t* GraftBuilder::PatchSector(uint64_t lba, bool loadFromBase)
{
    auto it = _patches.find(lba);
    if (it == _patches.end())
    {
        std::array<uint8_t, 512> bytes{};
        if (loadFromBase)
            _base.ReadSector(lba, bytes.data());
        it = _patches.emplace(lba, bytes).first;
    }
    return it->second.data();
}

// --- GraftVolume ---

GraftVolume::GraftVolume(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool, uint16_t baseDevice)
    : _tree(std::move(tree)), _pool(std::move(pool)), _base(&_pool->Device(baseDevice)), _reader(*_pool, _tree->Extents()),
      _baseDevice(baseDevice)
{
}

std::unique_ptr<GraftVolume> GraftVolume::Build(std::shared_ptr<const FileTree> tree, std::shared_ptr<SourcePool> pool,
                                                uint16_t baseDevice, const GraftOptions& options, uint64_t sourceIdentity,
                                                std::string description, std::string* error, std::vector<std::string>* report,
                                                GraftFailure* failure)
{
    GraftFailure ignored = GraftFailure::None;
    GraftFailure& why = failure ? *failure : ignored;
    why = GraftFailure::BaseNotFat;

    std::unique_ptr<GraftVolume> volume(new GraftVolume(tree, pool, baseDevice));

    // The volume: the image's own (a superfloppy or its first FAT partition), or partition n
    std::shared_ptr<IBlockDevice> window;
    uint64_t offset = 0;
    if (options.partition)
    {
        FatPartition partition;
        if (!FatVolumeReader::FindPartition(*volume->_base, *options.partition, partition, error))
            return nullptr;
        window = std::make_shared<SubRangeDevice>(pool->DevicePtr(baseDevice), partition.first, partition.count);
        offset = partition.first;
    }
    FatVolumeReader reader;
    std::string openError;
    if (!reader.Open(window ? *window : *volume->_base, options.codePage, &openError))
    {
        if (error)
            *error = "no FAT volume: " + openError;
        return nullptr;
    }
    const uint64_t volumeStart = offset + reader.VolumeStart();

    GraftBuilder builder(*volume, reader, *volume->_base, volumeStart, options, report);
    if (!builder.Build(error, &why))
        return nullptr;

    volume->_type = reader.Type();
    volume->_dataStart = volumeStart + reader.DataStart();
    volume->_sectorsPerCluster = reader.SectorsPerCluster();
    volume->_clusterCount = reader.ClusterCount();
    volume->_volumeStart = volumeStart;
    volume->_fatStart = volumeStart + reader.ReservedSectors();
    volume->_rootStart = volume->_fatStart + static_cast<uint64_t>(reader.FatCount()) * reader.FatSectors();
    // The base files' extents name the image, or the window FatImageSource opened for an explicit partition
    volume->_extentDevice = baseDevice;
    if (options.partition)
    {
        volume->_extentDevice = pool->FindDevice(pool->DeviceKey(baseDevice) + "#partition" + std::to_string(*options.partition));
        volume->_extentOffset = offset;
    }
    volume->_description = std::move(description);

    uint64_t id = sourceIdentity;
    for (uint64_t v : {volume->_base->ContentId(), static_cast<uint64_t>(options.partition.value_or(0)),
                       static_cast<uint64_t>(options.codePage), static_cast<uint64_t>(options.fixedTimeUtc.value_or(0))})
    {
        for (int i = 0; i < 8; i++)
        {
            id ^= static_cast<uint8_t>(v >> (8 * i));
            id *= 0x100000001b3ULL;
        }
    }
    volume->_contentId = id;
    return volume;
}

uint64_t GraftVolume::SectorCount() const
{
    return _base->SectorCount();
}

std::optional<BlockGeometry> GraftVolume::NativeGeometry() const
{
    return _base->NativeGeometry();
}

std::string GraftVolume::Describe() const
{
    return _description + " (graft onto " + _base->Describe() + ": " + std::to_string(_filesGrafted) + " files, " +
           std::to_string(_directoriesEncoded) + " directories patched)";
}

const GraftVolume::Run* GraftVolume::FindRun(uint64_t lba, uint64_t& cluster) const
{
    if (_runs.empty() || lba < _dataStart)
        return nullptr;
    cluster = (lba - _dataStart) / _sectorsPerCluster + 2;
    if (cluster >= static_cast<uint64_t>(_clusterCount) + 2)
        return nullptr;
    const Run& last = _runs[_lastRun];
    if (cluster >= last.firstCluster && cluster < static_cast<uint64_t>(last.firstCluster) + last.clusters)
        return &last;
    auto it = std::upper_bound(_runs.begin(), _runs.end(), cluster, [](uint64_t c, const Run& r) { return c < r.firstCluster; });
    if (it == _runs.begin())
        return nullptr;
    --it;
    if (cluster >= static_cast<uint64_t>(it->firstCluster) + it->clusters)
        return nullptr;
    _lastRun = static_cast<size_t>(it - _runs.begin());
    return &*it;
}

void GraftVolume::IndexBaseFiles() const
{
    _baseIndexed = true;
    if (_extentDevice < 0)
        return;
    const std::vector<Extent>& extents = _tree->Extents();
    for (uint32_t n = 0; n < _tree->NodeCount(); n++)
    {
        const TreeNode& node = _tree->Node(n);
        if (node.isDirectory || node.layer != 0 || node.data.storage != FileData::Storage::DeviceExtents ||
            node.data.source != static_cast<uint16_t>(_extentDevice))
            continue;
        for (uint32_t e = 0; e < node.data.extentCount; e++)
        {
            const Extent& x = extents[node.data.firstExtent + e];
            _baseRuns.push_back({x.sourceLba + _extentOffset, x.sectors, x.fileSectorStart, n});
        }
    }
    std::sort(_baseRuns.begin(), _baseRuns.end(), [](const BaseRun& a, const BaseRun& b) { return a.lba < b.lba; });
}

SectorOwner GraftVolume::OwnerOf(uint64_t lba) const
{
    SectorOwner owner;
    owner.patched = std::binary_search(_patchLba.begin(), _patchLba.end(), lba);
    if (lba < _volumeStart)
    {
        owner.role = lba == 0 ? SectorRole::PartitionTable : SectorRole::BootArea;
        return owner;
    }
    if (lba < _fatStart)
        owner.role = SectorRole::VolumeHeader;
    else if (lba < _rootStart)
        owner.role = SectorRole::Fat;
    else if (lba < _dataStart)
    {
        owner.role = SectorRole::Directory;
        owner.node = FileTree::kRoot;
        owner.offset = (lba - _rootStart) * kSector;
    }
    else
    {
        const uint64_t cluster = (lba - _dataStart) / _sectorsPerCluster + 2;
        uint64_t runCluster = 0;
        const auto dir = std::lower_bound(_dirClusters.begin(), _dirClusters.end(), cluster,
                                          [](const DirCluster& d, uint64_t c) { return d.cluster < c; });
        if (dir != _dirClusters.end() && dir->cluster == cluster)
        {
            owner.role = SectorRole::Directory;
            owner.node = dir->node;
            // The offset counts the directory's clusters before this one
            uint64_t before = 0;
            for (const DirCluster& d : _dirClusters)
                before += d.node == dir->node && d.cluster < cluster ? 1 : 0;
            owner.offset = (before * _sectorsPerCluster + (lba - _dataStart) % _sectorsPerCluster) * kSector;
        }
        else if (const Run* run = FindRun(lba, runCluster))
        {
            owner.role = SectorRole::FileData;
            owner.node = run->node;
            owner.offset = ((run->fileClusterStart + (runCluster - run->firstCluster)) * _sectorsPerCluster +
                            (lba - _dataStart) % _sectorsPerCluster) * kSector;
        }
        else
        {
            if (!_baseIndexed)
                IndexBaseFiles();
            auto it = std::upper_bound(_baseRuns.begin(), _baseRuns.end(), lba, [](uint64_t l, const BaseRun& r) { return l < r.lba; });
            if (it != _baseRuns.begin() && lba < (it - 1)->lba + (it - 1)->sectors)
            {
                --it;
                owner.role = SectorRole::FileData;
                owner.node = it->node;
                owner.offset = (it->fileSectorStart + (lba - it->lba)) * kSector;
            }
        }
    }
    if (owner.HasNode())
    {
        const TreeNode& node = _tree->Node(owner.node);
        owner.layer = node.layer;
        const uint32_t dir = owner.role == SectorRole::Directory ? owner.node : node.parent;
        const auto it = _dirClusterOfNode.find(dir);
        owner.dirCluster = it == _dirClusterOfNode.end() ? 0 : it->second;
    }
    return owner;
}

GraftSectorOrigin GraftVolume::SectorOrigin(uint64_t lba) const
{
    if (std::binary_search(_patchLba.begin(), _patchLba.end(), lba))
        return GraftSectorOrigin::Patch;
    uint64_t cluster = 0;
    return FindRun(lba, cluster) ? GraftSectorOrigin::Graft : GraftSectorOrigin::Base;
}

bool GraftVolume::ReadSector(uint64_t lba, uint8_t* dst)
{
    // A patched sector: directories, FAT, FSInfo
    const auto patch = std::lower_bound(_patchLba.begin(), _patchLba.end(), lba);
    if (patch != _patchLba.end() && *patch == lba)
    {
        std::memcpy(dst, _patchData.data() + static_cast<size_t>(patch - _patchLba.begin()) * kSector, kSector);
        return true;
    }

    // A grafted cluster: the upper file's sector
    uint64_t cluster = 0;
    if (const Run* run = FindRun(lba, cluster))
    {
        const uint64_t fileSector =
            (run->fileClusterStart + (cluster - run->firstCluster)) * _sectorsPerCluster + (lba - _dataStart) % _sectorsPerCluster;
        return _reader.ReadFileSector(_tree->Node(run->node).data, fileSector, dst);
    }
    return _base->ReadSector(lba, dst);
}
