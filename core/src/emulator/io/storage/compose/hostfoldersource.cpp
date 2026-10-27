#include "stdafx.h"

#include "hostfoldersource.h"

#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/hostfolder/foldersnapshot.h"
#include "emulator/io/storage/hostfolder/servicefilefilter.h"

namespace
{
    constexpr uint8_t kHidden = 0x02;

    bool Included(const std::vector<std::string>& include, const std::string& name)
    {
        if (include.empty())
            return true;
        for (const std::string& pattern : include)
        {
            if (MatchesWildcard(pattern, name))
                return true;
        }
        return false;
    }

    void Copy(const FolderEntry& dir, uint32_t into, const HostFolderSourceOptions& options, SourcePool& pool,
              FileTree& out, const std::string& path, std::vector<std::string>* report)
    {
        for (const FolderEntry& child : dir.children)
        {
            if (!child.isDirectory && !Included(options.include, child.name))
            {
                if (report)
                    report->push_back(path + child.name + ": not included");
                continue;
            }
            TreeNode node;
            node.name = child.name;
            node.isDirectory = child.isDirectory;
            node.mtimeUtc = child.mtimeUtc;
            if (!child.name.empty() && child.name[0] == '.')
                node.attributes |= kHidden;  // hidden, as on the host
            if (!child.isDirectory)
            {
                node.data.bytes = child.size;
                if (child.size > 0)
                {
                    node.data.storage = FileData::Storage::HostFile;
                    node.data.hostFile = pool.AddHostFile(child.hostPath, child.size);
                }
            }
            const uint32_t index = out.Add(into, std::move(node));
            if (child.isDirectory)
                Copy(child, index, options, pool, out, path + child.name + "/", report);
        }
    }
}  // namespace

bool HostFolderSource::Enumerate(const FolderSnapshot& snapshot, const HostFolderSourceOptions& options, SourcePool& pool,
                                 FileTree& out, std::vector<std::string>* report, std::string* error)
{
    const FolderEntry* root = &snapshot.Root();
    size_t pos = 0;
    const std::string& from = options.from;
    while (pos < from.size())
    {
        const size_t slash = from.find('/', pos);
        const std::string part = from.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        pos = slash == std::string::npos ? from.size() : slash + 1;
        if (part.empty() || part == ".")
            continue;
        const FolderEntry* next = nullptr;
        for (const FolderEntry& child : root->children)
        {
            if (child.name == part)
                next = &child;
        }
        if (!next || !next->isDirectory)
        {
            if (error)
                *error = "'" + from + "' is not a folder of the source";
            return false;
        }
        root = next;
    }
    out.Node(FileTree::kRoot).mtimeUtc = root->mtimeUtc;
    Copy(*root, FileTree::kRoot, options, pool, out, "/", report);
    return true;
}
