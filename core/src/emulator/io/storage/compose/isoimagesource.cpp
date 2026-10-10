#include "stdafx.h"

#include "isoimagesource.h"

#include "emulator/io/storage/cd/iso9660reader.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/hostfolder/servicefilefilter.h"

namespace
{
    constexpr int kMaxDepth = 64;  ///< deeper is a loop in a damaged image

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
        Iso9660Reader& reader;
        const IsoImageSourceOptions& options;
        uint16_t device;
        FileTree& out;
        std::vector<std::string>* report;
        std::string* error;

        bool Copy(const IsoDirEntry& dir, uint32_t into, const std::string& path, int depth)
        {
            if (depth > kMaxDepth)
            {
                if (error)
                    *error = path + ": directories nest deeper than " + std::to_string(kMaxDepth);
                return false;
            }
            std::vector<IsoDirEntry> entries;
            std::string why;
            if (!reader.List(dir, entries, &why))
            {
                if (error)
                    *error = path + ": " + why;
                return false;
            }
            for (const IsoDirEntry& entry : entries)
            {
                if (Matches(options.exclude, entry.name))
                {
                    if (report)
                        report->push_back(path + entry.name + ": skipped, excluded");
                    continue;
                }
                if (!entry.isDirectory && (entry.associated || entry.interleaved))
                {
                    // An associated file shares its name with the real one; an interleaved one is not one run of
                    // blocks per section, which the extents cannot describe
                    if (report)
                        report->push_back(path + entry.name + (entry.associated ? ": skipped, an associated file"
                                                                                : ": skipped, recorded interleaved"));
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
                node.mtimeUtc = entry.mtimeUtc;
                node.attributes = entry.hidden ? 0x02 : 0;
                if (!entry.isDirectory)
                {
                    node.data.bytes = entry.size;
                    if (entry.size > 0)
                    {
                        std::vector<Extent>& extents = out.Extents();
                        node.data.storage = FileData::Storage::DeviceExtents;
                        node.data.source = device;
                        node.data.firstExtent = static_cast<uint32_t>(extents.size());
                        uint32_t fileSector = 0;
                        for (const auto& [block, bytes] : entry.sections)
                        {
                            const uint32_t sectors = (bytes + 511) / 512;
                            if (sectors == 0)
                                continue;
                            extents.push_back(Extent{static_cast<uint64_t>(block) * 4, sectors, fileSector});
                            fileSector += sectors;
                        }
                        node.data.extentCount = static_cast<uint32_t>(extents.size() - node.data.firstExtent);
                    }
                }
                const uint32_t index = out.Add(into, std::move(node));
                if (entry.isDirectory && !Copy(entry, index, path + entry.name + "/", depth + 1))
                    return false;
            }
            return true;
        }
    };
}  // namespace

bool IsoImageSource::Enumerate(uint16_t device, const IsoImageSourceOptions& options, SourcePool& pool, FileTree& out,
                               std::vector<std::string>* report, std::string* error, uint64_t* identity)
{
    Iso9660Reader reader;
    std::string why;
    if (!reader.Open(pool.Device(device), &why))
    {
        if (error)
            *error = why;
        return false;
    }
    IsoDirEntry root;
    if (!reader.Stat(options.from, root, &why) || !root.isDirectory)
    {
        if (error)
            *error = "'" + options.from + "' is not a directory of the image";
        return false;
    }
    out.Node(FileTree::kRoot).mtimeUtc = root.mtimeUtc;
    if (identity)
        *identity = pool.Device(device).ContentId();
    Walker walker{reader, options, device, out, report, error};
    return walker.Copy(root, FileTree::kRoot, "/", 0);
}
