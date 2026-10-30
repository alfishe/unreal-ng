#include "upload_sweep.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <system_error>

#include <platform/processinfo.h>

namespace fs = std::filesystem;

namespace api::v1
{
void SweepUploadSessions(const std::string& uploadsDir)
{
    std::error_code ec;
    for (fs::directory_iterator it(fs::path(uploadsDir), fs::directory_options::skip_permission_denied, ec), end;
         !ec && it != end; it.increment(ec))
    {
        const std::string name = it->path().filename().string();
        const size_t dash = name.find('-');
        if (dash == 0 || dash == std::string::npos ||
            !std::all_of(name.begin(), name.begin() + dash, [](unsigned char c) { return std::isdigit(c) != 0; }))
            continue;
        const unsigned long long owner = std::strtoull(name.substr(0, dash).c_str(), nullptr, 10);
        if (owner == 0 || owner > UINT32_MAX || owner == platform::CurrentProcessId() ||
            platform::IsProcessAlive(static_cast<uint32_t>(owner)))
            continue;
        std::error_code removeError;
        fs::remove_all(it->path(), removeError);
    }
}
} // namespace api::v1
