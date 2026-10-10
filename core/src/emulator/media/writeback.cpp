#include "stdafx.h"

#include "writeback.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <map>
#include <optional>
#include <system_error>

#include "common/filehelper.h"
#include "common/filemtime.h"
#include "common/hosttrash.h"
#include "emulator/io/storage/commitjournal.h"
#include "emulator/io/storage/compose/composedlayout.h"
#include "emulator/io/storage/compose/sourcepool.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/partitioneddisk.h"
#include "emulator/io/storage/subrangedevice.h"
#include "emulator/io/storage/sessionwritemap.h"
#include "emulator/media/composedescriptor.h"
#include "emulator/media/mediachanges.h"
#include "emulator/media/medium.h"

const char* WriteBackStep::KindName(Kind kind)
{
    switch (kind)
    {
        case Kind::Write: return "write";
        case Kind::Mkdir: return "mkdir";
        case Kind::Rename: return "rename";
        case Kind::Remove: return "remove";
        case Kind::Move: return "move";
        case Kind::Whiteout: return "whiteout";
        case Kind::Note: return "note";
        case Kind::Attributes: return "attributes";
        case Kind::Trash: return "trash";
    }
    return "?";
}

namespace
{
    namespace fs = std::filesystem;

    /// Recover's note for a directory the guest removed that still holds host files
    constexpr const char* kKeptNotEmpty = ": kept, it holds host files the guest did not see";

    std::string Upper(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return text;
    }

    std::string Parent(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        return slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
    }

    /// A name a host file system cannot store (Windows device names, a trailing dot or space there)
    bool HostCannotStore(const std::string& name)
    {
#ifdef _WIN32
        const std::string base = Upper(name.substr(0, name.find('.')));
        static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                         "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
        for (const char* r : reserved)
            if (base == r)
                return true;
        return !name.empty() && (name.back() == '.' || name.back() == ' ');
#else
        (void)name;
        return false;
#endif
    }

    struct Planner
    {
        Medium& medium;
        const ComposeDescriptor& d;
        const WriteBackOptions& options;
        WriteBackPlan& plan;
        FatVolumeReader after;
        const IComposedLayout* layout = nullptr;
        int upper = -1;
        std::map<std::string, int> plannedDirs;  ///< directories the plan makes (upper-case guest path) -> layer

        /// The host path of guest path `p` inside folder layer `i`, if `p` is under its mount
        std::optional<fs::path> HostIn(int i, const std::string& p) const
        {
            const ComposeLayer& l = d.layers[static_cast<size_t>(i)];
            if (l.source.kind != ComposeSource::Kind::Folder)
                return std::nullopt;
            std::string mount = l.mount == "/" ? "" : l.mount;
            if (!mount.empty() && Upper(p.substr(0, mount.size())) != Upper(mount))
                return std::nullopt;
            if (!mount.empty() && p.size() > mount.size() && p[mount.size()] != '/')
                return std::nullopt;
            std::string rel = p.substr(mount.size());
            std::string from = l.from == "/" ? "" : l.from;
            fs::path host = l.source.path;
            for (const std::string& part : {from, rel})
            {
                size_t pos = 0;
                while (pos < part.size())
                {
                    const size_t slash = part.find('/', pos);
                    const std::string piece = part.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
                    pos = slash == std::string::npos ? part.size() : slash + 1;
                    if (!piece.empty())
                        host /= FileHelper::ToFsPath(piece);
                }
            }
            return host;
        }

        bool Writable(int i) const
        {
            return i >= 0 && d.layers[static_cast<size_t>(i)].writable && d.layers[static_cast<size_t>(i)].source.kind == ComposeSource::Kind::Folder;
        }

        int LayerByName(const std::string& name) const
        {
            for (size_t i = 0; i < d.layers.size(); i++)
                if (d.layers[i].name == name)
                    return static_cast<int>(i);
            return -1;
        }

        /// DT-10 create: the topmost writable layer whose host folder holds `p`'s parent, else the upper layer
        int CreateTarget(const std::string& p) const
        {
            std::error_code ec;
            if (const auto planned = plannedDirs.find(Upper(Parent(p))); planned != plannedDirs.end())
                return planned->second;  // a directory this write-back makes: its files go with it
            for (int i = static_cast<int>(d.layers.size()) - 1; i >= 0; i--)
            {
                if (!Writable(i))
                    continue;
                const auto host = HostIn(i, Parent(p));
                if (host && fs::is_directory(*host, ec))
                    return i;
            }
            return upper;
        }

        /// The tree node of `p` at build time (T0), case-insensitively
        uint32_t NodeAtBuild(const std::string& p) const
        {
            if (!layout)
                return FileTree::kNone;
            const FileTree& t = layout->Tree();
            uint32_t at = FileTree::kRoot;
            size_t pos = 1;
            while (at != FileTree::kNone && pos < p.size())
            {
                const size_t slash = p.find('/', pos);
                const std::string part = Upper(p.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos));
                pos = slash == std::string::npos ? p.size() : slash + 1;
                uint32_t found = FileTree::kNone;
                for (uint32_t child : t.Node(at).children)
                    if (Upper(t.Node(child).name) == part)
                        found = child;
                at = found;
            }
            return at;
        }

        /// DT-12: may `host` be written or removed? It must be as the build saw it, or not exist when the build had
        /// no file there. Returns the host path to use (keep-both renames it), or none with an error recorded
        std::optional<fs::path> Gate(const std::string& p, fs::path host)
        {
            std::error_code ec;
            if (HostCannotStore(FileHelper::FromFsPath(host.filename())))
            {
                plan.errors.push_back(p + ": the host cannot store the name '" + FileHelper::FromFsPath(host.filename()) + "'");
                return std::nullopt;
            }
            bool changed = false;
            const uint32_t node = NodeAtBuild(p);
            const SourcePool* pool = layout ? layout->Pool() : nullptr;
            if (node != FileTree::kNone && pool && layout->Tree().Node(node).data.storage == FileData::Storage::HostFile &&
                fs::equivalent(pool->HostPath(layout->Tree().Node(node).data.hostFile), host, ec))
            {
                const uint64_t size = fs::file_size(host, ec);
                int64_t mtime = 0;
                changed = ec || size != pool->HostSize(layout->Tree().Node(node).data.hostFile) || !GetMTimeUnixSeconds(host, mtime) ||
                          mtime != layout->Tree().Node(node).mtimeUtc;
            }
            else
                changed = fs::exists(host, ec) && !fs::is_directory(host, ec);
            if (!changed)
                return host;
            if (!options.keepBoth)
            {
                plan.errors.push_back(p + ": " + FileHelper::FromFsPath(host) + " changed on the host since the build (conflict)");
                return std::nullopt;
            }
            const std::string stem = FileHelper::FromFsPath(host.stem());
            const std::string ext = FileHelper::FromFsPath(host.extension());
            return host.parent_path() / FileHelper::ToFsPath(stem + " (guest)" + ext);
        }

        void Add(WriteBackStep::Kind kind, const std::string& p, int layer, fs::path host, std::string detail = {}, fs::path from = {})
        {
            WriteBackStep s;
            s.kind = kind;
            s.path = p;
            s.layer = layer >= 0 ? d.layers[static_cast<size_t>(layer)].name : "";
            s.host = std::move(host);
            s.from = std::move(from);
            s.detail = std::move(detail);
            if (kind == WriteBackStep::Kind::Write)
            {
                FatDirEntryInfo e;
                if (after.Stat(p, e))
                {
                    s.bytes = e.size;
                    s.mtimeUtc = FatVolumeReader::DosToUnix(e.date, e.time);
                }
            }
            plan.steps.push_back(std::move(s));
        }

        /// The guest's file `p` into layer `layer` (create, modify, copy-up)
        void WriteInto(const std::string& p, int layer, bool directory)
        {
            if (layer < 0)
            {
                plan.errors.push_back(p + ": no writable layer for it (name writes.upper, or mark a folder layer writable: true)");
                return;
            }
            const auto host = HostIn(layer, p);
            if (!host)
            {
                plan.errors.push_back(p + ": outside layer '" + d.layers[static_cast<size_t>(layer)].name + "' (its mount is " +
                                      d.layers[static_cast<size_t>(layer)].mount + ")");
                return;
            }
            if (directory)
            {
                plannedDirs[Upper(p)] = layer;
                Add(WriteBackStep::Kind::Mkdir, p, layer, *host);
                return;
            }
            if (const auto gated = Gate(p, *host))
                Add(WriteBackStep::Kind::Write, p, layer, *gated);
        }

        /// DT-11: a guest delete of `p` owned by `owner`
        void Delete(const std::string& p, int owner, bool directory)
        {
            const ComposeLayer* l = owner >= 0 ? &d.layers[static_cast<size_t>(owner)] : nullptr;
            if (l && l->onDelete == DeletePolicy::Ignore)
            {
                Add(WriteBackStep::Kind::Note, p, owner, {}, "onDelete: ignore - it comes back on the next build");
                return;
            }
            if (!Writable(owner) || l->onDelete == DeletePolicy::Keep)
            {
                Add(WriteBackStep::Kind::Whiteout, p, owner, {}, Writable(owner) ? "onDelete: keep" : "a read-only layer");
                return;
            }
            const auto host = HostIn(owner, p);
            if (!host)
            {
                Add(WriteBackStep::Kind::Whiteout, p, owner, {}, "not under the layer's mount");
                return;
            }
            if (directory)
            {
                // The guest saw the directory empty; the host's may hold files the guest never saw (excluded, or
                // made since the build). Its files go one by one under the policy; the directory goes last, and
                // only when empty (a directory kept so is reported)
                Add(WriteBackStep::Kind::Remove, p, owner, *host, "rmdir: removed once empty");
                return;
            }
            const auto gated = Gate(p, *host);
            if (!gated)
                return;
            if (*gated != *host)
            {
                // keep-both: the host's file changed since the build; the guest deleted its own copy, the host's stays
                Add(WriteBackStep::Kind::Note, p, owner, *host, "changed on the host since the build: kept (keep-both)");
                return;
            }
            if (l->onDelete == DeletePolicy::Trash)
            {
                Add(WriteBackStep::Kind::Trash, p, owner, *host, "onDelete: trash");
                return;
            }
            if (l->onDelete == DeletePolicy::Move)
            {
                char stamp[32];
                const std::time_t now = std::time(nullptr);
                std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", std::gmtime(&now));
                fs::path to = (l->deletedFolder.empty() ? d.baseDir / "deleted" : l->deletedFolder) / stamp / FileHelper::ToFsPath(l->name);
                to /= fs::relative(*host, l->source.path);
                Add(WriteBackStep::Kind::Move, p, owner, to, "onDelete: move", *host);
            }
            else
                Add(WriteBackStep::Kind::Remove, p, owner, *host, "onDelete: delete");
        }
    };

    /// Every change of one volume into steps (DT-10 / DT-11)
    void PlanChanges(Planner& p, const std::vector<MediumChange>& changes)
    {
        for (const MediumChange& c : changes)
        {
            const int owner = p.LayerByName(c.layer);
            if (c.op == "create" || c.op == "mkdir")
                p.WriteInto(c.path, p.CreateTarget(c.path), c.op == "mkdir");
            else if (c.op == "modify")
                p.WriteInto(c.path, p.Writable(owner) ? owner : p.upper, false);
            else if (c.op == "attributes")
            {
                // Kept beside the descriptor, not on the host file (a read-only host file would refuse later write-backs)
                FatDirEntryInfo e;
                if (p.after.Stat(c.path, e))
                    p.Add(WriteBackStep::Kind::Attributes, c.path, owner, {}, ComposeDescriptor::AttributeBitsText(e.attributes & 0x07));
            }
            else if (c.op == "delete" || c.op == "rmdir")
                p.Delete(c.path, owner, c.op == "rmdir");
            else if (c.op == "rename")
            {
                const auto from = p.Writable(owner) ? p.HostIn(owner, c.oldPath) : std::nullopt;
                const auto to = p.Writable(owner) ? p.HostIn(owner, c.path) : std::nullopt;
                std::error_code ec;
                if (from && to && fs::is_directory(to->parent_path(), ec))
                {
                    if (const auto gated = p.Gate(c.path, *to))
                        p.Add(WriteBackStep::Kind::Rename, c.path, owner, *gated, "was " + c.oldPath, *from);
                }
                else
                {
                    FatDirEntryInfo e;
                    const bool directory = p.after.Stat(c.path, e) && e.isDirectory;
                    p.WriteInto(c.path, p.CreateTarget(c.path), directory);
                    p.Delete(c.oldPath, owner, directory);
                }
            }
        }
    }

    /// The upper layer of `p`'s descriptor: writes.upper, else the topmost writable folder layer
    void ChooseUpper(Planner& p, WriteBackPlan& plan)
    {
        p.upper = p.d.writes.upper.empty() ? -1 : p.LayerByName(p.d.writes.upper);
        if (!p.d.writes.upper.empty() && !p.Writable(p.upper))
            plan.errors.push_back("writes.upper: '" + p.d.writes.upper + "' is not a writable folder layer");
        if (p.d.writes.upper.empty())
            for (int i = static_cast<int>(p.d.layers.size()) - 1; i >= 0 && p.upper < 0; i--)
                if (p.Writable(i))
                    p.upper = i;
    }

    /// A partition of the session's partitioned disk as a volume of its own
    std::shared_ptr<IBlockDevice> PartitionWindow(SessionWriteMap& session, const PartitionedDisk::Part& part)
    {
        std::shared_ptr<IBlockDevice> disk(&session, [](IBlockDevice*) {});
        return std::make_shared<SubRangeDevice>(disk, part.start, part.sectors);
    }

    std::string Qualified(const WriteBackStep& s)
    {
        return s.partition.empty() ? s.path : s.partition + ":" + s.path;
    }
}  // namespace

fs::path WriteBack::JournalFor(const fs::path& descriptorFile)
{
    fs::path journal = descriptorFile;
    journal += ".writeback";
    return journal;
}

MediaResult WriteBack::Plan(Medium& medium, const ComposeDescriptor& d, const WriteBackOptions& options, WriteBackPlan& plan)
{
    plan = WriteBackPlan();
    SessionWriteMap* session = medium.Session();
    if (!session || medium.Source().type != MediaSourceType::Composite)
        return MediaResult::Fail(MediaError::BadRequest, "write-back needs a composite with session writes");
    if (d.file.empty())
        return MediaResult::Fail(MediaError::BadRequest, "an inline descriptor cannot take write-back: write it to a file");
    MediumChanges changes;
    MediaResult listed = ListMediumChanges(medium, changes);
    if (!listed.Ok())
        return listed;
    for (const std::string& w : changes.warnings)
        if (!options.force && (w.find("lost clusters") != std::string::npos || w.find("cross-linked") != std::string::npos))
            return MediaResult::Fail(MediaError::Dirty, "the guest's file system is inconsistent (" + w + "): write back with force");

    if (!d.hasPartitions)
    {
        Planner p{medium, d, options, plan, {}, dynamic_cast<const IComposedLayout*>(&session->Base()), -1, {}};
        if (!p.after.Open(*session))
            return MediaResult::Fail(MediaError::BadRequest, "the guest's volume cannot be read");
        ChooseUpper(p, plan);
        PlanChanges(p, changes.changes);
        return MediaResult::Success();
    }

    // C8d: a partitioned disk, one planner per composed partition over its own layers and its window of the disk
    auto* disk = dynamic_cast<PartitionedDisk*>(&session->Base());
    if (!disk)
        return MediaResult::Fail(MediaError::BadRequest, "the guest's partitioned disk cannot be read");
    std::map<std::string, std::vector<MediumChange>> byPartition;
    for (MediumChange c : changes.changes)
    {
        const size_t colon = c.path.find(':');
        if (colon == std::string::npos)
            continue;  // not a file of a partition (a warning says what changed)
        const std::string name = c.path.substr(0, colon);
        c.path = c.path.substr(colon + 1);
        if (!c.oldPath.empty() && c.oldPath.compare(0, colon + 1, name + ":") == 0)
            c.oldPath = c.oldPath.substr(colon + 1);
        if (c.layer.compare(0, name.size() + 1, name + "/") == 0)
            c.layer = c.layer.substr(name.size() + 1);  // "work/w": the partition's own layer "w"
        byPartition[name].push_back(std::move(c));
    }
    for (const auto& [name, partChanges] : byPartition)
    {
        const auto composed = std::find_if(d.partitions.begin(), d.partitions.end(), [&name = name](const ComposePartition& p) { return p.name == name; });
        const auto part = std::find_if(disk->Parts().begin(), disk->Parts().end(), [&name = name](const PartitionedDisk::Part& p) { return p.name == name; });
        if (composed == d.partitions.end() || part == disk->Parts().end())
            continue;
        if (!composed->compose)
        {
            plan.errors.push_back("partition " + name + " is an image as it is (" + std::to_string(partChanges.size()) +
                                  " change(s) there): commit or flatten it instead");
            continue;
        }
        const std::shared_ptr<IBlockDevice> window = PartitionWindow(*session, *part);
        const OffsetLayout layout(part->layout, part->layoutOffset);
        Planner p{medium, *composed->compose, options, plan, {}, part->layout ? &layout : nullptr, -1, {}};
        if (!p.after.Open(*window))
        {
            plan.errors.push_back("partition " + name + ": its volume cannot be read");
            continue;
        }
        ChooseUpper(p, plan);
        const size_t first = plan.steps.size();
        PlanChanges(p, partChanges);
        for (size_t i = first; i < plan.steps.size(); i++)
            plan.steps[i].partition = name;
    }
    return MediaResult::Success();
}

MediaResult WriteBack::Apply(Medium& medium, const ComposeDescriptor& d, const WriteBackPlan& plan)
{
    if (!plan.errors.empty())
        return MediaResult::Fail(MediaError::Dirty, "write-back refused: " + plan.errors.front() +
                                                        (plan.errors.size() > 1 ? " (and " + std::to_string(plan.errors.size() - 1) + " more)" : ""));
    // The guest's volume, or each partition's on a partitioned disk
    std::map<std::string, std::pair<std::shared_ptr<IBlockDevice>, std::unique_ptr<FatVolumeReader>>> readers;
    auto readerFor = [&](const std::string& partition) -> FatVolumeReader* {
        auto& entry = readers[partition];
        if (!entry.second)
        {
            entry.second = std::make_unique<FatVolumeReader>();
            IBlockDevice* volume = medium.Session();
            if (!partition.empty())
            {
                auto* disk = dynamic_cast<PartitionedDisk*>(&medium.Session()->Base());
                const auto part = disk ? std::find_if(disk->Parts().begin(), disk->Parts().end(),
                                                      [&partition](const PartitionedDisk::Part& p) { return p.name == partition; })
                                       : std::vector<PartitionedDisk::Part>::const_iterator();
                if (!disk || part == disk->Parts().end())
                    return nullptr;
                entry.first = PartitionWindow(*medium.Session(), *part);
                volume = entry.first.get();
            }
            if (!entry.second->Open(*volume))
                return nullptr;
        }
        return entry.second.get();
    };

    // Stage every new content next to its target (the same host volume: the rename is atomic)
    std::vector<std::pair<fs::path, fs::path>> staged;  ///< staged file, target
    std::error_code ec;
    for (size_t i = 0; i < plan.steps.size(); i++)
    {
        const WriteBackStep& s = plan.steps[i];
        if (s.kind != WriteBackStep::Kind::Write)
            continue;
        std::vector<uint8_t> data;
        FatVolumeReader* after = readerFor(s.partition);
        if (!after || !after->ReadFile(s.path, data))
            return MediaResult::Fail(MediaError::IoError, Qualified(s) + ": cannot be read from the guest's volume");
        fs::create_directories(s.host.parent_path(), ec);
        fs::path temp = s.host.parent_path() / FileHelper::ToFsPath(".unreal-staging-" + std::to_string(i));
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!out)
                return MediaResult::Fail(MediaError::IoError, "cannot stage " + FileHelper::FromFsPath(temp));
        }
        SetMTimeUnixSeconds(temp, s.mtimeUtc);
        staged.push_back({temp, s.host});
    }

    // The journal: every step, so a cut-short apply can be finished
    const fs::path journalPath = JournalFor(d.file);
    {
        std::ofstream journal(journalPath, std::ios::trunc);
        for (const auto& [from, to] : staged)
            journal << "write\t" << FileHelper::FromFsPath(from) << "\t" << FileHelper::FromFsPath(to) << "\n";
        for (const WriteBackStep& s : plan.steps)
        {
            if (s.kind == WriteBackStep::Kind::Mkdir)
                journal << "mkdir\t\t" << FileHelper::FromFsPath(s.host) << "\n";
            else if (s.kind == WriteBackStep::Kind::Rename || s.kind == WriteBackStep::Kind::Move)
                journal << "rename\t" << FileHelper::FromFsPath(s.from) << "\t" << FileHelper::FromFsPath(s.host) << "\n";
            else if (s.kind == WriteBackStep::Kind::Remove)
                journal << "remove\t\t" << FileHelper::FromFsPath(s.host) << "\n";
            else if (s.kind == WriteBackStep::Kind::Whiteout)
                journal << "whiteout\t\t" << Qualified(s) << "\n";
            else if (s.kind == WriteBackStep::Kind::Attributes)
                journal << "attributes\t" << s.detail << "\t" << Qualified(s) << "\n";
            else if (s.kind == WriteBackStep::Kind::Trash)
                journal << "trash\t\t" << FileHelper::FromFsPath(s.host) << "\n";
        }
        journal << "end\n";
        if (!journal)
            return MediaResult::Fail(MediaError::IoError, "cannot write " + FileHelper::FromFsPath(journalPath));
    }
    CommitJournal::Sync(journalPath);
    const std::string done = Recover(d.file);  // performs the journal
    if (fs::exists(journalPath, ec))
        return MediaResult::Fail(MediaError::IoError, "write-back stopped part way (" + done + "): inserting the descriptor again finishes it");
    MediaResult result = MediaResult::Success();
    result.report.push_back("write-back: " + std::to_string(plan.steps.size()) + " step(s) done");
    // Directories kept because they still hold host files
    for (size_t at = done.find("; "); at != std::string::npos; at = done.find("; ", at + 2))
    {
        const size_t end = done.find("; ", at + 2);
        result.report.push_back(done.substr(at + 2, end == std::string::npos ? std::string::npos : end - at - 2));
    }
    return result;
}

std::string WriteBack::Recover(const fs::path& descriptorFile)
{
    const fs::path journalPath = JournalFor(descriptorFile);
    std::error_code ec;
    if (!fs::is_regular_file(journalPath, ec))
        return {};
    std::ifstream in(journalPath);
    std::vector<std::vector<std::string>> lines;
    bool complete = false;
    std::string line;
    while (std::getline(in, line))
    {
        if (line == "end")
        {
            complete = true;
            break;
        }
        std::vector<std::string> fields;
        size_t pos = 0;
        while (true)
        {
            const size_t tab = line.find('\t', pos);
            fields.push_back(line.substr(pos, tab == std::string::npos ? std::string::npos : tab - pos));
            if (tab == std::string::npos)
                break;
            pos = tab + 1;
        }
        if (fields.size() == 3)
            lines.push_back(std::move(fields));
    }
    in.close();
    if (!complete)
    {
        // Never finished writing: nothing was applied; the staged files are left for a look
        fs::remove(journalPath, ec);
        return FileHelper::FromFsPath(journalPath.filename()) + ": an unfinished write-back journal was dropped (nothing was applied)";
    }

    // Each step is done when it can be: a repeat finds it done already
    std::vector<std::string> failed;
    std::vector<std::string> kept;  // directories the guest removed that still hold host files
    std::vector<std::string> whiteouts;
    std::vector<std::pair<std::string, std::string>> attributes;  // path, bits
    // Removes last, the deepest first: a directory goes once its files went
    const auto removes =
        std::stable_partition(lines.begin(), lines.end(), [](const std::vector<std::string>& f) { return f[0] != "remove"; });
    std::stable_sort(removes, lines.end(),
                     [](const std::vector<std::string>& a, const std::vector<std::string>& b) { return a[2].size() > b[2].size(); });
    for (const auto& f : lines)
    {
        const fs::path from = FileHelper::ToFsPath(f[1]);
        const fs::path to = FileHelper::ToFsPath(f[2]);
        if (f[0] == "mkdir")
            fs::create_directories(to, ec);
        else if (f[0] == "write" || f[0] == "rename")
        {
            if (!fs::exists(from, ec))
                continue;  // done before
            fs::create_directories(to.parent_path(), ec);
            ec.clear();
            fs::rename(from, to, ec);
            if (ec)
            {
                // Across volumes (a deleted-files folder elsewhere): copy, then remove
                ec.clear();
                fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                if (!ec)
                    fs::remove_all(from, ec);
            }
            if (ec)
                failed.push_back(f[1] + " -> " + f[2] + ": " + ec.message());
        }
        else if (f[0] == "remove")
        {
            if (fs::is_directory(to, ec))
            {
                if (fs::is_empty(to, ec))
                    fs::remove(to, ec);
                else if (!ec)
                    kept.push_back(f[2]);
            }
            else
                fs::remove(to, ec);
        }
        else if (f[0] == "whiteout")
            whiteouts.push_back(f[2]);
        else if (f[0] == "attributes")
            attributes.emplace_back(f[2], f[1]);
        else if (f[0] == "trash")
        {
            std::string why;
            if (fs::exists(fs::symlink_status(to, ec)) && !HostTrash::Move(to, &why))
                failed.push_back(f[2] + ": " + why);
        }
    }
    if (!attributes.empty())
    {
        // The sidecar rewritten: a path's line replaced, new ones added (paths compared case-insensitively)
        fs::path list = descriptorFile;
        list += ".attributes";
        std::vector<std::pair<std::string, std::string>> lines;  // path, bits
        if (std::ifstream current(list); current)
        {
            for (std::string l; std::getline(current, l);)
            {
                const size_t tab = l.find('\t');
                if (tab != std::string::npos)
                    lines.emplace_back(l.substr(tab + 1), l.substr(0, tab));
            }
        }
        for (const auto& [path, bits] : attributes)
        {
            auto it = std::find_if(lines.begin(), lines.end(), [&path = path](const auto& line) { return Upper(line.first) == Upper(path); });
            if (it != lines.end())
                it->second = bits;
            else
                lines.emplace_back(path, bits);
        }
        std::ofstream out(list, std::ios::trunc);
        for (const auto& [path, bits] : lines)
            out << bits << "\t" << path << "\n";
    }
    if (!whiteouts.empty())
    {
        fs::path list = descriptorFile;
        list += ".whiteout";
        std::vector<std::string> existing;
        if (std::ifstream current(list); current)
            for (std::string w; std::getline(current, w);)
                existing.push_back(w);
        std::ofstream out(list, std::ios::app);
        for (const std::string& w : whiteouts)
            if (std::find(existing.begin(), existing.end(), w) == existing.end())
                out << w << "\n";
    }
    if (!failed.empty())
        return FileHelper::FromFsPath(journalPath.filename()) + ": " + failed.front();
    fs::remove(journalPath, ec);
    std::string done = FileHelper::FromFsPath(journalPath.filename()) + ": an interrupted write-back was completed";
    for (const std::string& k : kept)
        done += "; " + k + kKeptNotEmpty;
    return done;
}
