#include "debugger/ttd/ttdrecordingfolders.h"

#include <cstdio>
#include <fstream>
#include <system_error>

#include "common/filehelper.h"
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

    std::string TTDRecordingFolder::SegmentPath(uint32_t index) const
    {
        char name[32];
        std::snprintf(name, sizeof(name), "segment-%04u.ttd", index);
        return FileHelper::PathCombine(_path, name);
    }

    std::vector<std::string> TTDRecordingFolder::Segments() const
    {
        std::vector<std::string> out;
        for (uint32_t i = 0;; ++i)
        {
            const std::string p = SegmentPath(i);
            if (!FileHelper::FileExists(p))
                return out;
            out.push_back(p);
        }
    }

    bool TTDRecordingFolder::SaveAs(const std::string& target, bool overwrite, std::string& error) const
    {
        const std::vector<std::string> segments = Segments();
        if (segments.empty())
        {
            error = "the recording has no segment yet";
            return false;
        }
        if (segments.size() > 1)
        {
            error = "joining several segments comes with the segment ring (Phase 4, Step 3)";
            return false;
        }
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
