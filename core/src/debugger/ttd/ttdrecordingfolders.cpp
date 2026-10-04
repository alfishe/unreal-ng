#include "debugger/ttd/ttdrecordingfolders.h"

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
