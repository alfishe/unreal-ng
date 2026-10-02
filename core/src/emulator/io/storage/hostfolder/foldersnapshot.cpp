#include "stdafx.h"

#include "foldersnapshot.h"

#include <algorithm>
#include <set>
#include <system_error>

#include "common/filemtime.h"
#include "servicefilefilter.h"

namespace
{
    std::string ToUtf8(const std::filesystem::path& path)
    {
        const auto text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    void Mix(uint64_t& hash, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; i++)
        {
            hash ^= bytes[i];
            hash *= 0x100000001b3ULL;
        }
    }

    class Scanner
    {
    public:
        Scanner(const FolderScanOptions& options, const ServiceFileFilter& filter, std::vector<SkippedEntry>& skipped)
            : _options(options), _filter(filter), _skipped(skipped)
        {
        }

        void ScanFolder(const std::filesystem::path& hostPath, const std::string& relative, uint32_t depth,
                        FolderEntry& folder)
        {
            std::error_code ec;
            std::filesystem::directory_iterator it(hostPath, std::filesystem::directory_options::skip_permission_denied, ec);
            if (ec)
            {
                Skip(relative.empty() ? "." : relative, "unreadable folder");
                return;
            }

            std::vector<FolderEntry> folders;
            std::vector<FolderEntry> files;
            for (; it != std::filesystem::directory_iterator(); it.increment(ec))
            {
                if (_options.cancelRequested && _options.cancelRequested())
                {
                    _cancelled = true;
                    break;
                }

                if (ec)
                {
                    Skip(relative.empty() ? "." : relative, "folder listing stopped: " + ec.message());
                    break;
                }

                const std::filesystem::directory_entry& entry = *it;
                if (_options.onProgress)
                    _options.onProgress(++_progressCounter, _totalFileBytes);
                const std::string name = ToUtf8(entry.path().filename());
                const std::string path = relative.empty() ? name : relative + "/" + name;

                std::string collection;
                if (_filter.IsService(name, &collection))
                {
                    // Host-OS housekeeping (Finder's .DS_Store, Thumbs.db, *~)
                    // can land in the folder at any moment: it leaves no trace
                    // at all, so a snapshot of the same folder stays identical
                    // whatever the host drops into it
                    if (!_filter.IsOsNoiseCollection(collection))
                        Skip(path, "service (" + collection + ")");
                    continue;
                }
                bool excluded = false;
                for (const std::string& pattern : _options.excludePatterns)
                    excluded = excluded || MatchesWildcard(pattern, name);
                if (excluded)
                {
                    Skip(path, "excluded by the manifest");
                    continue;
                }

                std::error_code linkEc;
                const bool isLink = entry.is_symlink(linkEc);
                if (isLink && !_options.followLinks)
                {
                    Skip(path, "symbolic link");
                    continue;
                }

                std::error_code typeEc;
                const bool isDirectory = entry.is_directory(typeEc);
                const bool isFile = entry.is_regular_file(typeEc);
                if (!isDirectory && !isFile)
                {
                    Skip(path, "not a file or folder");
                    continue;
                }

                if (folders.size() + files.size() >= _options.maxEntriesPerFolder)
                {
                    Skip(path, "the folder has more than " + std::to_string(_options.maxEntriesPerFolder) + " entries");
                    continue;
                }
                if (_entryCount >= _options.maxEntries)
                {
                    Skip(path, "more than " + std::to_string(_options.maxEntries) + " entries in the tree");
                    continue;
                }

                FolderEntry item;
                item.name = name;
                item.hostPath = entry.path();
                item.isDirectory = isDirectory;
                if (!GetMTimeUnixSeconds(entry.path(), item.mtimeUtc))
                    item.mtimeUtc = 0;

                if (isDirectory)
                {
                    if (!_options.recursive)
                    {
                        Skip(path, "subfolder (this medium takes the top level only)");
                        continue;
                    }
                    if (depth + 1 > _options.maxDepth)
                    {
                        Skip(path, "deeper than " + std::to_string(_options.maxDepth) + " levels");
                        continue;
                    }
                    if (isLink && !EnterOnce(entry.path()))
                    {
                        Skip(path, "symbolic link cycle");
                        continue;
                    }
                    _entryCount++;
                    ScanFolder(entry.path(), path, depth + 1, item);
                    folders.push_back(std::move(item));
                    if (_cancelled)
                        break;  // unwind every recursion level without visiting further siblings
                }
                else
                {
                    std::error_code sizeEc;
                    item.size = entry.file_size(sizeEc);
                    if (sizeEc)
                    {
                        Skip(path, "unreadable file");
                        continue;
                    }
                    if (item.size > _options.maxFileSize)
                    {
                        Skip(path, "larger than " + std::to_string(_options.maxFileSize) + " bytes");
                        continue;
                    }
                    _entryCount++;
                    _totalFileBytes += item.size;
                    files.push_back(std::move(item));
                }
            }

            auto byName = [](const FolderEntry& a, const FolderEntry& b) { return a.name < b.name; };
            std::sort(folders.begin(), folders.end(), byName);
            std::sort(files.begin(), files.end(), byName);
            folder.children = std::move(folders);
            for (FolderEntry& file : files)
                folder.children.push_back(std::move(file));
        }

        uint32_t EntryCount() const { return _entryCount; }
        uint64_t TotalFileBytes() const { return _totalFileBytes; }
        bool IsCancelled() const { return _cancelled; }

    private:
        void Skip(const std::string& path, std::string reason) { _skipped.push_back({path, std::move(reason)}); }

        bool EnterOnce(const std::filesystem::path& path)
        {
            std::error_code ec;
            const auto canonical = std::filesystem::canonical(path, ec);
            return !ec && _visited.insert(ToUtf8(canonical)).second;
        }

        const FolderScanOptions& _options;
        const ServiceFileFilter& _filter;
        std::vector<SkippedEntry>& _skipped;
        std::set<std::string> _visited;
        uint32_t _entryCount = 0;
        uint64_t _totalFileBytes = 0;
        uint64_t _progressCounter = 0;
        bool _cancelled = false;
    };

    void HashTree(const FolderEntry& entry, const std::string& relative, uint64_t& hash)
    {
        for (const FolderEntry& child : entry.children)
        {
            const std::string path = relative + "/" + child.name;
            Mix(hash, path.data(), path.size());
            const uint8_t kind = child.isDirectory ? 1 : 0;
            Mix(hash, &kind, 1);
            Mix(hash, &child.size, sizeof child.size);
            Mix(hash, &child.mtimeUtc, sizeof child.mtimeUtc);
            if (child.isDirectory)
                HashTree(child, path, hash);
        }
    }
}  // namespace

bool FolderSnapshot::Scan(const std::filesystem::path& folder, const FolderScanOptions& options, FolderSnapshot& out,
                          std::string* error)
{
    out = FolderSnapshot();

    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec))
    {
        if (error)
            *error = "not a folder: " + ToUtf8(folder);
        return false;
    }

    const ServiceFileFilter defaults;
    const ServiceFileFilter& filter = options.filter ? *options.filter : defaults;

    out._root.name = ToUtf8(folder.filename());
    out._root.hostPath = folder;
    out._root.isDirectory = true;

    Scanner scanner(options, filter, out._skipped);
    scanner.ScanFolder(folder, "", 0, out._root);
    if (scanner.IsCancelled())
    {
        if (error)
            *error = kCancelledError;
        return false;
    }
    out._entryCount = scanner.EntryCount();
    out._totalFileBytes = scanner.TotalFileBytes();

    out._identity = 0xcbf29ce484222325ULL;
    HashTree(out._root, "", out._identity);
    return true;
}
