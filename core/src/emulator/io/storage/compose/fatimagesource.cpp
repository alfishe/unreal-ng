#include "stdafx.h"

#include "fatimagesource.h"

#include <memory>

#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/compose/unionbuilder.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/hostfolder/servicefilefilter.h"
#include "emulator/io/storage/subrangedevice.h"

namespace
{
    constexpr uint8_t kKeptAttributes = 0x07;  ///< read-only, hidden, system
    constexpr int kMaxDepth = 64;               ///< deeper is a directory loop, not a tree

    bool Matches(const std::vector<std::string>& patterns, const std::string& name)
    {
        for (const std::string& pattern : patterns)
        {
            if (MatchesWildcard(pattern, name))
                return true;
        }
        return false;
    }

    struct Walker
    {
        FatVolumeReader& reader;
        const FatImageSourceOptions& options;
        uint16_t device;
        FileTree& out;
        std::vector<std::string>* report;
        std::string* error;
        bool lazy = false;              ///< C4b: subdirectories become unexpanded nodes
        uint32_t* directoriesRead = nullptr;

        bool Copy(uint32_t dirCluster, uint32_t into, const std::string& path, int depth)
        {
            if (directoriesRead)
                ++*directoriesRead;
            if (depth > kMaxDepth)
            {
                if (error)
                    *error = path + ": directories nest deeper than " + std::to_string(kMaxDepth) + " (a loop in the image)";
                return false;
            }
            std::vector<FatDirEntryInfo> entries;
            std::string why;
            if (!reader.ListDirectory(dirCluster, entries, &why))
            {
                if (error)
                    *error = path + ": " + why;
                return false;
            }
            std::vector<FatChainExtent> chain;
            for (const FatDirEntryInfo& entry : entries)
            {
                if (Matches(options.exclude, entry.name))
                {
                    if (report)
                        report->push_back(path + entry.name + ": skipped, excluded");
                    continue;
                }
                if (!entry.isDirectory && !options.include.empty() && !Matches(options.include, entry.name))
                {
                    if (report)
                        report->push_back(path + entry.name + ": not included");
                    continue;
                }
                TreeNode node;
                node.name = entry.name;
                node.isDirectory = entry.isDirectory;
                node.mtimeUtc = FatVolumeReader::DosToUnix(entry.date, entry.time);
                node.attributes = entry.attributes & kKeptAttributes;
                if (!entry.isDirectory)
                {
                    node.data.bytes = entry.size;
                    if (entry.size > 0)
                    {
                        if (!reader.ChainExtents(entry.firstCluster, entry.size, chain, &why))
                        {
                            if (report)
                                report->push_back(path + entry.name + ": skipped, " + why);
                            continue;
                        }
                        std::vector<Extent>& extents = out.Extents();
                        node.data.storage = FileData::Storage::DeviceExtents;
                        node.data.source = device;
                        node.data.firstExtent = static_cast<uint32_t>(extents.size());
                        node.data.extentCount = static_cast<uint32_t>(chain.size());
                        uint32_t fileSector = 0;
                        for (const FatChainExtent& run : chain)
                        {
                            extents.push_back(Extent{run.lba, run.sectors, fileSector});
                            fileSector += run.sectors;
                        }
                    }
                }
                if (entry.isDirectory && entry.firstCluster != 0 && lazy)
                {
                    node.unexpanded = true;
                    node.baseCluster = entry.firstCluster;
                }
                const uint32_t index = out.Add(into, std::move(node));
                if (entry.isDirectory && entry.firstCluster == 0)
                {
                    // Cluster 0 means the root: a damaged entry, kept as an empty directory
                    if (report)
                        report->push_back(path + entry.name + ": a directory without clusters, kept empty");
                    continue;
                }
                if (entry.isDirectory && !lazy && !Copy(entry.firstCluster, index, path + entry.name + "/", depth + 1))
                    return false;
            }
            return true;
        }
    };
}  // namespace

bool FatImageSource::Enumerate(uint16_t device, const FatImageSourceOptions& options, SourcePool& pool, FileTree& out,
                               std::vector<std::string>* report, std::string* error, uint64_t* identity, uint16_t* volumeOut)
{
    // The volume: the whole image, or a window of it for an explicit partition
    uint16_t volume = device;
    if (options.partition)
    {
        const std::string key = pool.DeviceKey(device) + "#partition" + std::to_string(*options.partition);
        const int known = pool.FindDevice(key);
        if (known >= 0)
            volume = static_cast<uint16_t>(known);
        else
        {
            FatPartition partition;
            if (!FatVolumeReader::FindPartition(pool.Device(device), *options.partition, partition, error))
                return false;
            volume = pool.AddDevice(std::make_shared<SubRangeDevice>(pool.DevicePtr(device), partition.first, partition.count),
                                    pool.DeviceKey(device).empty() ? std::string() : key);
        }
    }

    FatVolumeReader reader;
    std::string why;
    if (!reader.Open(pool.Device(volume), options.codePage, &why))
    {
        if (error)
            *error = "no FAT volume: " + why;
        return false;
    }
    FatDirEntryInfo root;
    if (!reader.Stat(options.from, root, &why) || !root.isDirectory)
    {
        if (error)
            *error = "'" + options.from + "' is not a directory of the image";
        return false;
    }
    if (options.from != "/" && !options.from.empty())
        out.Node(FileTree::kRoot).mtimeUtc = FatVolumeReader::DosToUnix(root.date, root.time);

    if (identity)
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        for (uint64_t v : {pool.Device(volume).ContentId(), static_cast<uint64_t>(options.codePage)})
        {
            for (int i = 0; i < 8; i++)
            {
                h ^= static_cast<uint8_t>(v >> (8 * i));
                h *= 0x100000001b3ULL;
            }
        }
        *identity = h;
    }

    if (volumeOut)
        *volumeOut = volume;
    Walker walker{reader, options, volume, out, report, error, options.lazy};
    return walker.Copy(root.firstCluster, FileTree::kRoot, "/", 0);
}

FatImageExpander::FatImageExpander(SourcePool& pool, uint16_t volume, const FatImageSourceOptions& options)
    : _pool(pool), _volume(volume), _options(options)
{
    _open = _reader.Open(_pool.Device(_volume), _options.codePage);
}

bool FatImageExpander::Expand(FileTree& tree, uint32_t node, std::vector<std::string>* report, std::string* error)
{
    if (!tree.Node(node).unexpanded)
        return true;
    if (!_open)
    {
        if (error)
            *error = "the image's FAT volume cannot be read any more";
        return false;
    }
    int depth = 0;
    for (uint32_t at = node; at != FileTree::kRoot; at = tree.Node(at).parent)
        depth++;
    tree.Node(node).unexpanded = false;
    Walker walker{_reader, _options, _volume, tree, report, error, true, &_directoriesRead};
    return walker.Copy(tree.Node(node).baseCluster, node, tree.PathOf(node) + "/", depth);
}

bool FatImageExpander::ExpandPath(FileTree& tree, const std::string& path, std::vector<std::string>* report, std::string* error)
{
    uint32_t at = FileTree::kRoot;
    size_t pos = 0;
    while (true)
    {
        if (!Expand(tree, at, report, error))
            return false;
        while (pos < path.size() && path[pos] == '/')
            pos++;
        if (pos >= path.size())
            return true;
        const size_t slash = path.find('/', pos);
        const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        pos = slash == std::string::npos ? path.size() : slash;
        const std::string key = UnionBuilder::FatKey(part);
        uint32_t next = FileTree::kNone;
        for (uint32_t child : tree.Node(at).children)
        {
            const TreeNode& c = tree.Node(child);
            if (c.isDirectory && UnionBuilder::FatKey(c.name) == key)
                next = child;
        }
        if (next == FileTree::kNone)
            return true;  // the image has no directory there: nothing below to read
        at = next;
    }
}

bool FatImageExpander::ExpandAll(FileTree& tree, std::vector<std::string>* report, std::string* error)
{
    // Nodes are appended while expanding: every index is visited once, children of a full read are never unexpanded
    for (uint32_t n = 0; n < tree.NodeCount(); n++)
    {
        if (!tree.Node(n).unexpanded)
            continue;
        if (!_open)
        {
            if (error)
                *error = "the image's FAT volume cannot be read any more";
            return false;
        }
        int depth = 0;
        for (uint32_t at = n; at != FileTree::kRoot; at = tree.Node(at).parent)
            depth++;
        tree.Node(n).unexpanded = false;
        Walker walker{_reader, _options, _volume, tree, report, error, false, &_directoriesRead};
        if (!walker.Copy(tree.Node(n).baseCluster, n, tree.PathOf(n) + "/", depth))
            return false;
    }
    return true;
}

std::pair<uint64_t, uint64_t> FatImageExpander::Count(uint32_t cluster)
{
    const auto known = _counts.find(cluster);
    if (known != _counts.end())
        return known->second;
    uint64_t files = 0, bytes = 0;
    if (_open)
        CountInto(cluster, 1, files, bytes);
    _counts.emplace(cluster, std::make_pair(files, bytes));
    return {files, bytes};
}

void FatImageExpander::CountInto(uint32_t cluster, int depth, uint64_t& files, uint64_t& bytes)
{
    std::vector<FatDirEntryInfo> entries;
    if (depth > kMaxDepth || !_reader.ListDirectory(cluster, entries))
        return;
    for (const FatDirEntryInfo& entry : entries)
    {
        if (Matches(_options.exclude, entry.name))
            continue;
        if (entry.isDirectory)
        {
            if (entry.firstCluster != 0)
                CountInto(entry.firstCluster, depth + 1, files, bytes);
            continue;
        }
        if (!_options.include.empty() && !Matches(_options.include, entry.name))
            continue;
        files++;
        bytes += entry.size;
    }
}
