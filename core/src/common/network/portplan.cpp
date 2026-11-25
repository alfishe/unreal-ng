#include "common/network/portplan.h"

#include <filesystem>
#include <fstream>
#include <system_error>

#include "common/filehelper.h"
#include "common/network/netsockets.h"

namespace portplan
{

namespace
{
void SetError(std::string* error, const std::string& text)
{
    if (error)
        *error = text;
}
}  // namespace

bool IsValidPortBase(long long base)
{
    return base >= 1 && base + kLastOffset <= 65535;
}

int FindFreePortBase(const SearchRange& range, const CanBind& canBind, std::string* error)
{
    if (range.step <= kLastOffset || range.lastBase < range.firstBase || !IsValidPortBase(range.firstBase) ||
        !IsValidPortBase(range.lastBase) || !canBind)
    {
        SetError(error, "the port base search range is not valid");
        return 0;
    }

    const size_t count = static_cast<size_t>((range.lastBase - range.firstBase) / range.step) + 1;
    for (size_t i = 0; i < count; i++)
    {
        const size_t slot = (range.startSlot % count + i) % count;
        const int base = range.firstBase + static_cast<int>(slot) * range.step;

        bool free = true;
        for (size_t role = 0; role < kRoles.size() && free; role++)
            free = canBind(PortOf(base, role));
        if (free)
            return base;
    }

    SetError(error, "no free port base from " + std::to_string(range.firstBase) + " to " +
                        std::to_string(range.lastBase) + " (step " + std::to_string(range.step) + "): each of the " +
                        std::to_string(count) + " candidates has a port that is in use");
    return 0;
}

bool CanBindTcpPort(int port)
{
    if (port < 1 || port > 65535 || !netsock::Startup())
        return false;

    const netsock::Handle handle = netsock::OpenTcp();
    if (handle == netsock::kInvalid)
        return false;

    // SO_REUSEADDR as the servers set it: a port in TIME_WAIT is free for them, a listening one is not. On Windows
    // the option would let the bind succeed over a live listener, so the probe goes without it there
#ifdef _WIN32
    const bool reuse = false;
#else
    const bool reuse = true;
#endif
    const bool bound = netsock::Bind(handle, 0, static_cast<uint16_t>(port), reuse) == netsock::Result::Ok;
    netsock::Close(handle);
    return bound;
}

std::string PortsFileJson(int base, int64_t pid)
{
    std::string json = "{\"port_base\": " + std::to_string(base);
    for (size_t role = 0; role < kRoles.size(); role++)
        json += std::string(", \"") + kRoles[role].name + "\": " + std::to_string(PortOf(base, role));
    json += ", \"pid\": " + std::to_string(pid) + "}\n";
    return json;
}

bool WritePortsFile(const std::string& path, int base, int64_t pid, std::string* error)
{
    // Written beside and renamed over: whoever waits for the file never reads half of it
    const std::string temp = path + ".tmp" + std::to_string(pid);
    {
        std::ofstream out(FileHelper::ToFsPath(temp), std::ios::binary | std::ios::trunc);
        if (out)
            out << PortsFileJson(base, pid);
        out.flush();
        if (!out)
        {
            SetError(error, "cannot write " + temp);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(FileHelper::ToFsPath(temp), FileHelper::ToFsPath(path), ec);
    if (ec)
    {
        std::error_code ignored;
        std::filesystem::remove(FileHelper::ToFsPath(temp), ignored);
        SetError(error, "cannot write " + path + ": " + ec.message());
        return false;
    }
    return true;
}

}  // namespace portplan
