#include "debugger/ttd/ttdrecordingfolders.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "common/filehelper.h"
#include "debugger/ttd/engine/ttdbytes.h"
#include "debugger/ttd/engine/ttdcontainer.h"
#include "debugger/ttd/engine/ttdrecordingwriter.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "platform/processinfo.h"

namespace ttd
{
    namespace fs = std::filesystem;

    std::string RecordingsRoot()
    {
        const std::string user = FileHelper::GetUserDataPath();
        return user.empty() ? std::string() : FileHelper::PathCombine(user, "ttd");
    }

    uint32_t ReadRecordingOwner(const std::string& folder)
    {
        std::ifstream in(FileHelper::ToFsPath(FileHelper::PathCombine(folder, kRecordingOwnerFile)));
        uint64_t pid = 0;
        if (!(in >> pid) || pid > UINT32_MAX)
            return 0;
        return static_cast<uint32_t>(pid);
    }

    bool WriteRecordingOwner(const std::string& folder)
    {
        std::ofstream out(FileHelper::ToFsPath(FileHelper::PathCombine(folder, kRecordingOwnerFile)), std::ios::trunc);
        out << platform::CurrentProcessId() << '\n';
        return static_cast<bool>(out);
    }

    namespace
    {
        /// The newest modification time in a folder, the folder itself included
        bool NewestWrite(const fs::path& folder, fs::file_time_type& newest)
        {
            std::error_code ec;
            newest = fs::last_write_time(folder, ec);
            if (ec)
                return false;
            for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                std::error_code timeError;
                const fs::file_time_type t = fs::last_write_time(it->path(), timeError);
                if (!timeError && t > newest)
                    newest = t;
            }
            return !ec;
        }
    }  // namespace

    void CleanCrashedRecordings(CleanupContext& context, const std::string& root, fs::file_time_type now,
                                std::chrono::seconds keep)
    {
        std::error_code ec;
        const fs::path rootPath = FileHelper::ToFsPath(root);
        if (root.empty() || !fs::is_directory(rootPath, ec))
            return;

        for (fs::directory_iterator it(rootPath, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && !context.StopRequested(); it.increment(ec))
        {
            std::error_code typeError;
            if (!it->is_directory(typeError))
                continue;   // a saved recording, or anything else that is not a recording folder
            const std::string folder = FileHelper::PathCombine(root, FileHelper::FromFsPath(it->path().filename()));

            const uint32_t owner = ReadRecordingOwner(folder);
            if (owner != 0 && platform::IsProcessAlive(owner))
                continue;

            fs::file_time_type newest;
            if (!NewestWrite(it->path(), newest))
            {
                context.Failed(folder, "cannot read its times");
                continue;
            }
            if (now - newest < keep)
                continue;   // kept a week, so it can still be opened or repaired

            std::string error;
            if (FileHelper::DeleteFolder(folder, &error))
                context.Removed(folder);
            else
                context.Failed(folder, error.empty() ? "not deleted" : error);
        }
        if (ec)
            context.Failed(root, ec.message());
    }

    std::unique_ptr<TTDRecordingFolder> TTDRecordingFolder::Create(const std::string& root, const std::string& name,
                                                                   std::time_t when, std::string& error)
    {
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &when);
#else
        localtime_r(&when, &local);
#endif
        char stamp[32];
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H%M%S", &local);
        std::string lower;
        for (char c : name)
        {
            const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
            if (c >= 'A' && c <= 'Z')
                lower += static_cast<char>(c - 'A' + 'a');
            else
                lower += keep ? c : '_';
        }
        const std::string base = std::string(stamp) + (lower.empty() ? "" : "-" + lower);
        if (!FileHelper::CreateFolders(root))
        {
            error = "cannot create " + root;
            return nullptr;
        }
        for (int n = 1; n < 1000; ++n)
        {
            const std::string path = FileHelper::PathCombine(root, n == 1 ? base : base + "-" + std::to_string(n));
            std::error_code ec;
            // create_directory is false (no error) when it exists: taken, try the next name
            if (!fs::create_directory(FileHelper::ToFsPath(path), ec))
            {
                if (ec)
                {
                    error = "cannot create " + path + ": " + ec.message();
                    return nullptr;
                }
                continue;
            }
            auto folder = std::unique_ptr<TTDRecordingFolder>(new TTDRecordingFolder());
            folder->_path = path;
            if (!WriteRecordingOwner(path))
            {
                error = "cannot write the owner file in " + path;
                FileHelper::DeleteFolder(path);
                return nullptr;
            }
            return folder;
        }
        error = "no free folder name for " + base;
        return nullptr;
    }

    bool ParseSegmentFileName(const std::string& name, uint32_t& number, uint32_t& line)
    {
        // "segment-" digits ["-b" digits] ".ttd", nothing else
        const std::string prefix = "segment-", suffix = ".ttd";
        if (name.size() <= prefix.size() + suffix.size() || name.compare(0, prefix.size(), prefix) != 0 ||
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0)
            return false;
        const std::string body = name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        auto digits = [](const std::string& text, uint32_t& value) {
            if (text.empty() || text.size() > 9)
                return false;
            value = 0;
            for (char c : text)
            {
                if (c < '0' || c > '9')
                    return false;
                value = value * 10 + static_cast<uint32_t>(c - '0');
            }
            return true;
        };
        const size_t branch = body.find("-b");
        line = 0;
        if (branch == std::string::npos)
            return digits(body, number);
        return digits(body.substr(0, branch), number) && digits(body.substr(branch + 2), line) && line != 0;
    }

    namespace
    {
        bool IsSegmentFileName(const std::string& name)
        {
            uint32_t number = 0, line = 0;
            return ParseSegmentFileName(name, number, line);
        }

        /// branches.tdx records (stream 0x0200, as the branch table's): 1 create, 2 source, 3 delete, 4 segment
        constexpr uint8_t kIndexCreate = 1;
        constexpr uint8_t kIndexSource = 2;
        constexpr uint8_t kIndexDelete = 3;
        constexpr uint8_t kIndexSegment = 4;

        std::vector<uint8_t> SourceRecord(const TTDBranchIndex::Source& src)
        {
            TTDByteWriter w;
            w.U8(kIndexSource);
            w.Str(src.path);
            w.Varint(src.size);
            for (uint8_t b : src.uuid)
                w.U8(b);
            return w.bytes;
        }

        /// A session file's size and header UUID
        bool ReadSourceIdentity(const std::string& path, TTDBranchIndex::Source& out, std::string* error)
        {
            TTDFileSource source(path);
            if (!source.Valid())
            {
                if (error)
                    *error = source.Error();
                return false;
            }
            TTDContainerReader reader;
            std::string why;
            if (!reader.Open(source, why))
            {
                if (error)
                    *error = why;
                return false;
            }
            out.size = source.Size();
            out.uuid = reader.Header().uuid;
            return true;
        }

        std::vector<uint8_t> SegmentRecord(const TTDBranchIndex::Segment& s)
        {
            TTDByteWriter w;
            w.U8(kIndexSegment);
            w.Str(s.file);
            w.Varint(s.line);
            w.Varint(s.firstFrame);
            w.Varint(s.frameSpan);
            w.Varint(s.parts);
            w.Varint(s.bytes);
            return std::move(w.bytes);
        }

        /// A closed (or cut) segment file's record from its index, and its first part's create record, if any
        bool DescribeSegment(const std::string& path, TTDBranchIndex::Segment& out, std::vector<uint8_t>* create)
        {
            TTDFileSource source(path);
            TTDContainerReader reader;
            std::string error;
            if (!source.Valid() || !reader.Open(source, error, TTDSessionFile::KnownStream) || reader.Parts().empty())
                return false;
            uint32_t number = 0;
            ParseSegmentFileName(FileHelper::FromFsPath(FileHelper::ToFsPath(path).filename()), number, out.line);
            out.file = FileHelper::FromFsPath(FileHelper::ToFsPath(path).filename());
            const TTDPartRef& first = reader.Parts().front();
            const TTDPartRef& last = reader.Parts().back();
            out.firstFrame = first.firstFrame;
            out.frameSpan = last.firstFrame + last.frameCount - first.firstFrame;
            out.parts = reader.Parts().size();
            out.bytes = source.Size();
            if (create)
            {
                create->clear();
                for (const TTDRecordRef& record : first.records)
                    if (record.streamId == sessionstream::kBranchTable)
                    {
                        if (!reader.ReadRecord(record, *create, &error))
                            create->clear();
                        break;
                    }
            }
            return true;
        }
    }  // namespace

    struct TTDRecordingFolder::IndexWriter
    {
        std::unique_ptr<TTDFileSink> sink;
        TTDContainerWriter writer;
    };

    TTDRecordingFolder::~TTDRecordingFolder() = default;

    uint32_t TTDRecordingFolder::NextNumber() const
    {
        uint32_t next = 0;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(FileHelper::ToFsPath(_path), ec))
        {
            uint32_t number = 0, line = 0;
            if (ParseSegmentFileName(FileHelper::FromFsPath(entry.path().filename()), number, line))
                next = std::max(next, number + 1);
        }
        return next;
    }

    std::unique_ptr<TTDRecordingFolder> TTDRecordingFolder::Open(const std::string& path)
    {
        if (path.empty())
            return nullptr;
        std::string folder = path;
        if (!FileHelper::IsFolder(path))
        {
            const fs::path file = FileHelper::ToFsPath(path);
            if (!IsSegmentFileName(FileHelper::FromFsPath(file.filename())))
                return nullptr;
            folder = FileHelper::FromFsPath(file.parent_path());
        }
        // The owner file marks a recording's folder (kept after its process ends)
        std::error_code ec;
        if (!fs::is_regular_file(FileHelper::ToFsPath(FileHelper::PathCombine(folder, kRecordingOwnerFile)), ec))
            return nullptr;
        auto out = std::unique_ptr<TTDRecordingFolder>(new TTDRecordingFolder());
        out->_path = folder;
        out->_nextNumber = out->NextNumber();
        out->FinishDeletions();
        return out;
    }

    std::string TTDRecordingFolder::SegmentPath(uint32_t index) const
    {
        return SegmentPath(index, 0);
    }

    std::string TTDRecordingFolder::SegmentPath(uint32_t number, uint32_t line) const
    {
        char name[48];
        if (line == 0)
            std::snprintf(name, sizeof(name), "segment-%04u.ttd", number);
        else
            std::snprintf(name, sizeof(name), "segment-%04u-b%u.ttd", number, line);
        return FileHelper::PathCombine(_path, name);
    }

    std::function<std::string(uint32_t)> TTDRecordingFolder::LinePaths(uint32_t line)
    {
        auto numbers = std::make_shared<std::vector<uint32_t>>();
        return [this, line, numbers](uint32_t n) {
            while (numbers->size() <= n)
                numbers->push_back(_nextNumber++);
            return SegmentPath((*numbers)[n], line);
        };
    }

    std::vector<std::string> TTDRecordingFolder::Segments() const
    {
        // Every segment file in the folder by its number: a black box deletes
        // its oldest ones as its window moves on, so the run may not start at 0
        std::set<uint32_t> deleted;
        if (HasBranchIndex())
        {
            TTDBranchIndex index;
            std::string error;
            if (ReadBranchIndex(BranchIndexPath(), index, error))
                deleted = index.deleted;
        }
        std::vector<std::pair<uint32_t, uint32_t>> numbers;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(FileHelper::ToFsPath(_path), ec))
        {
            const std::string name = FileHelper::FromFsPath(entry.path().filename());
            uint32_t number = 0, line = 0;
            if (ParseSegmentFileName(name, number, line) && !deleted.count(line))
                numbers.emplace_back(number, line);
        }
        std::sort(numbers.begin(), numbers.end());
        std::vector<std::string> out;
        for (const auto& [n, line] : numbers)
            out.push_back(SegmentPath(n, line));
        return out;
    }

    std::string TTDRecordingFolder::BranchIndexPath() const
    {
        return FileHelper::PathCombine(_path, kBranchIndexFile);
    }

    bool TTDRecordingFolder::HasBranchIndex() const
    {
        std::error_code ec;
        return _index || fs::is_regular_file(FileHelper::ToFsPath(BranchIndexPath()), ec);
    }

    bool TTDRecordingFolder::ReadBranchIndex(const std::string& path, TTDBranchIndex& out, std::string& error)
    {
        out = {};
        TTDFileSource source(path);
        if (!source.Valid())
        {
            error = path + ": " + source.Error();
            return false;
        }
        TTDContainerReader reader;
        if (!reader.Open(source, error, [](uint16_t id) { return id == sessionstream::kBranchTable; }))
            return false;
        // Each record a part of its own: a cut or damaged one ends the list, the complete ones before it count
        for (const TTDPartRef& part : reader.Parts())
        {
            if (part.damaged)
                break;
            bool ok = true;
            for (const TTDRecordRef& record : part.records)
            {
                std::vector<uint8_t> bytes;
                if (record.streamId != sessionstream::kBranchTable || !reader.ReadRecord(record, bytes, &error))
                {
                    ok = false;
                    break;
                }
                TTDByteReader r(bytes);
                uint8_t kind = 0;
                if (!r.U8(kind))
                {
                    ok = false;
                    break;
                }
                if (kind == kIndexCreate)
                    out.creates.push_back(std::move(bytes));
                else if (kind == kIndexDelete)
                {
                    uint32_t line = 0;
                    ok = r.VarintAs(line);
                    if (ok)
                        out.deleted.insert(line);
                }
                else if (kind == kIndexSource)
                {
                    TTDBranchIndex::Source src;
                    ok = r.Str(src.path) && r.Varint(src.size);
                    for (uint8_t& b : src.uuid)
                        ok = ok && r.U8(b);
                    if (ok)
                        out.source = std::move(src);
                }
                else if (kind == kIndexSegment)
                {
                    TTDBranchIndex::Segment seg;
                    ok = r.Str(seg.file) && r.VarintAs(seg.line) && r.Varint(seg.firstFrame) &&
                         r.Varint(seg.frameSpan) && r.Varint(seg.parts) && r.Varint(seg.bytes);
                    if (ok)
                        out.segments.push_back(std::move(seg));
                }
                // other kinds (meta updates, later) are skipped
                if (!ok)
                    break;
                ++out.records;
            }
            if (!ok)
                break;
        }
        return true;
    }

    TTDBranchIndex TTDRecordingFolder::IndexFromFiles() const
    {
        TTDBranchIndex out;
        std::set<uint32_t> created;
        for (const std::string& path : Segments())
        {
            TTDBranchIndex::Segment seg;
            std::vector<uint8_t> create;
            if (!DescribeSegment(path, seg, &create))
                continue;
            if (seg.line != 0 && !create.empty() && created.insert(seg.line).second)
            {
                out.creates.push_back(create);
                ++out.records;
            }
            out.segments.push_back(std::move(seg));
            ++out.records;
        }
        return out;
    }

    bool TTDRecordingFolder::OpenIndex(const TTDBranchIndex& content, std::string* error)
    {
        // Written in place: a cut while it is written leaves its complete records, and the files rebuild the rest
        const std::string path = BranchIndexPath();
        std::error_code ec;
        _index.reset();
        fs::remove(FileHelper::ToFsPath(path), ec);
        auto index = std::make_unique<IndexWriter>();
        index->sink = std::make_unique<TTDFileSink>(path);
        if (!index->sink->Valid())
        {
            if (error)
                *error = path + ": " + index->sink->Error();
            return false;
        }
        TTDContainerHeader header;
        header.streams.push_back({sessionstream::kBranchTable, 1, TTDStreamKind::Required, "branch-index"});
        std::string why;
        if (!index->writer.Begin(*index->sink, header, &why))
        {
            if (error)
                *error = why;
            return false;
        }
        _index = std::move(index);
        // The source (Q-11) and the delete records first (a cut keeps them), then in the files' order each branch's
        // create before its first segment
        if (content.source && !AppendIndexRecord(SourceRecord(*content.source), error))
            return false;
        for (uint32_t line : content.deleted)
        {
            TTDByteWriter w;
            w.U8(kIndexDelete);
            w.Varint(line);
            if (!AppendIndexRecord(w.bytes, error))
                return false;
        }
        size_t create = 0;
        std::set<uint32_t> seen;
        for (const TTDBranchIndex::Segment& seg : content.segments)
        {
            if (seg.line != 0 && seen.insert(seg.line).second && create < content.creates.size())
                if (!AppendIndexRecord(content.creates[create++], error))
                    return false;
            if (!AppendIndexRecord(SegmentRecord(seg), error))
                return false;
        }
        for (; create < content.creates.size(); ++create)
            if (!AppendIndexRecord(content.creates[create], error))
                return false;
        return true;
    }

    bool TTDRecordingFolder::AppendIndexRecord(const std::vector<uint8_t>& record, std::string* error)
    {
        if (!_index || !_index->writer.AddRecord(sessionstream::kBranchTable, record) ||
            !_index->writer.EndPart(TTDPartEnd{}, /*sync=*/true))
        {
            if (error)
                *error = "cannot write " + BranchIndexPath();
            return false;
        }
        return true;
    }
    bool TTDRecordingFolder::RebuildBranchIndex(std::string* error)
    {
        TTDBranchIndex content = IndexFromFiles();
        TTDBranchIndex old;
        std::string why;
        if (HasBranchIndex() && ReadBranchIndex(BranchIndexPath(), old, why))
        {
            content.deleted = old.deleted;
            content.source = old.source;
        }
        return OpenIndex(content, error);
    }

    bool TTDRecordingFolder::NoteBranchCreated(const std::vector<uint8_t>& createRecord, std::string* error)
    {
        if (!_index && !RebuildBranchIndex(error))
            return false;
        return AppendIndexRecord(createRecord, error);
    }

    bool TTDRecordingFolder::NoteSource(const std::string& path, std::string* error)
    {
        TTDBranchIndex::Source src;
        src.path = path;
        if (!fs::is_directory(FileHelper::ToFsPath(path)) && !ReadSourceIdentity(path, src, error))
            return false;
        if (!_index && !RebuildBranchIndex(error))
            return false;
        return AppendIndexRecord(SourceRecord(src), error);
    }

    bool TTDRecordingFolder::JoinList(std::vector<std::string>& out, std::string& error, int depth) const
    {
        out.clear();
        TTDBranchIndex index;
        std::string why;
        if (HasBranchIndex() && ReadBranchIndex(BranchIndexPath(), index, why) && index.source)
        {
            const TTDBranchIndex::Source& src = *index.source;
            if (depth > 16)
            {
                error = "the recording's sources go too deep";
                return false;
            }
            if (fs::is_directory(FileHelper::ToFsPath(src.path)))
            {
                const auto folder = Open(src.path);
                if (!folder || !folder->JoinList(out, error, depth + 1))
                {
                    if (!folder)
                        error = "the recording continues " + src.path + ", which is not a recording folder";
                    return false;
                }
            }
            else
            {
                TTDBranchIndex::Source now;
                std::string missing;
                if (!ReadSourceIdentity(src.path, now, &missing) || now.size != src.size || now.uuid != src.uuid)
                {
                    error = "the recording continues the session " + src.path + ", which is " +
                            (missing.empty() ? "changed" : "missing (" + missing + ")");
                    return false;
                }
                out.push_back(src.path);
            }
        }
        for (std::string& segment : Segments())
            out.push_back(std::move(segment));
        return true;
    }

    bool TTDRecordingFolder::NoteSegmentClosed(const std::string& path)
    {
        if (!_index)
            return true;   // no branch yet: the files are the index
        TTDBranchIndex::Segment seg;
        if (!DescribeSegment(path, seg, nullptr))
            return false;
        return AppendIndexRecord(SegmentRecord(seg), nullptr);
    }

    bool TTDRecordingFolder::DeleteLines(const std::vector<uint32_t>& lines, std::string* error)
    {
        if (lines.empty())
            return true;
        // The records first: from here on the files are not the recording's, whenever they go
        if (!_index && !RebuildBranchIndex(error))
            return false;
        for (uint32_t line : lines)
        {
            TTDByteWriter w;
            w.U8(kIndexDelete);
            w.Varint(line);
            if (!AppendIndexRecord(w.bytes, error))
                return false;
        }
        FinishDeletions();
        return true;
    }

    void TTDRecordingFolder::FinishDeletions()
    {
        if (!HasBranchIndex())
            return;
        TTDBranchIndex index;
        std::string why;
        if (!ReadBranchIndex(BranchIndexPath(), index, why) || index.deleted.empty())
            return;
        std::error_code ec;
        std::vector<fs::path> gone;
        for (const auto& entry : fs::directory_iterator(FileHelper::ToFsPath(_path), ec))
        {
            uint32_t number = 0, line = 0;
            if (ParseSegmentFileName(FileHelper::FromFsPath(entry.path().filename()), number, line) &&
                index.deleted.count(line))
                gone.push_back(entry.path());
        }
        for (const fs::path& file : gone)
            fs::remove(file, ec);
    }

    size_t TTDRecordingFolder::DropOldestSegments(size_t keep)
    {
        const std::vector<std::string> segments = Segments();
        size_t dropped = 0;
        for (size_t i = 0; i + keep < segments.size(); ++i)
        {
            std::error_code ec;
            if (std::filesystem::remove(FileHelper::ToFsPath(segments[i]), ec))
                ++dropped;
        }
        return dropped;
    }

    bool TTDRecordingFolder::SaveAs(const std::string& target, bool overwrite, std::string& error) const
    {
        std::vector<std::string> segments;
        if (!JoinList(segments, error, 0))
            return false;
        if (segments.empty())
        {
            error = "the recording has no segment yet";
            return false;
        }
        if (segments.size() > 1)
            return JoinSessionFiles(segments, target, overwrite, error);
        std::error_code ec;
        const auto options = overwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none;
        fs::copy_file(FileHelper::ToFsPath(segments.front()), FileHelper::ToFsPath(target), options, ec);
        if (ec)
        {
            error = "cannot write " + target + ": " + ec.message();
            return false;
        }
        return true;
    }

    bool TTDRecordingFolder::Discard(std::string* error)
    {
        _index.reset();
        return FileHelper::DeleteFolder(_path, error);
    }

    CleanupStep CrashedRecordingsCleanupStep()
    {
        CleanupStep step;
        step.name = kCrashedRecordingsCleanupStep;
        step.interval = std::chrono::hours(24 * 7);
        step.run = [](CleanupContext& context) {
            CleanCrashedRecordings(context, RecordingsRoot(), fs::file_time_type::clock::now(), kCrashedRecordingKeep);
        };
        return step;
    }
}  // namespace ttd
