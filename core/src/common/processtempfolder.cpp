#include "common/processtempfolder.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>

#include "common/cleanupmanager.h"
#include "common/filehelper.h"
#include "platform/processinfo.h"

namespace ProcessTempFolder
{
std::filesystem::path Make(const std::string& prefix)
{
    static std::atomic<uint32_t> counter{0};
    std::error_code ec;
    const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
    if (ec)
        return {};
    const std::string stem = prefix + std::to_string(platform::CurrentProcessId()) + "-";
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const std::filesystem::path folder = temp / (stem + std::to_string(counter++));
        if (std::filesystem::create_directories(folder, ec))
            return folder;
    }
    return {};
}

CleanupStep OrphanedFoldersCleanupStep(const std::string& name, const std::string& prefix)
{
    CleanupStep step;
    step.name = name;
    step.interval = std::chrono::hours(24);
    step.run = [prefix](CleanupContext& context) {
        std::error_code ec;
        const std::filesystem::path temp = std::filesystem::temp_directory_path(ec);
        if (ec)
            return;
        for (std::filesystem::directory_iterator it(temp, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             !ec && it != end && !context.StopRequested(); it.increment(ec))
        {
            const std::string entry = FileHelper::FromFsPath(it->path().filename());
            if (entry.rfind(prefix, 0) != 0)
                continue;
            const char* digits = entry.c_str() + prefix.size();
            char* stop = nullptr;
            const unsigned long owner = std::strtoul(digits, &stop, 10);
            if (stop == digits || *stop != '-' || owner == 0 || owner > UINT32_MAX ||
                platform::IsProcessAlive(static_cast<uint32_t>(owner)))
                continue;  // not one of these folders, or its process may still use it
            std::string error;
            const std::string folder = FileHelper::FromFsPath(it->path());
            if (FileHelper::DeleteFolder(folder, &error))
                context.Removed(folder);
            else
                context.Failed(folder, error.empty() ? "not deleted" : error);
        }
    };
    return step;
}
}  // namespace ProcessTempFolder
