#include "stdafx.h"

#include "hosttrash.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <system_error>

#include "common/filehelper.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace
{
    bool Fail(std::string* error, const std::string& text)
    {
        if (error)
            *error = text;
        return false;
    }

    /// RFC 2396 escaping of a path, '/' kept (the freedesktop `Path=` key)
    std::string PercentEncode(const std::string& utf8)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        for (const unsigned char c : utf8)
        {
            const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '-' ||
                               c == '_' || c == '.' || c == '~';
            if (plain)
            {
                out += static_cast<char>(c);
            }
            else
            {
                out += '%';
                out += hex[c >> 4];
                out += hex[c & 15];
            }
        }
        return out;
    }

#if !defined(_WIN32)
    bool Device(const fs::path& path, dev_t& device)
    {
        struct stat st{};
        if (::lstat(path.c_str(), &st) != 0)
            return false;
        device = st.st_dev;
        return true;
    }

    /// The top directory of the mount `path` is on: the highest ancestor on the same device
    fs::path TopDir(const fs::path& path)
    {
        dev_t device = 0;
        fs::path at = fs::absolute(path).parent_path();
        if (!Device(at, device))
            return {};
        for (;;)
        {
            const fs::path up = at.parent_path();
            dev_t upDevice = 0;
            if (up == at || !Device(up, upDevice) || upDevice != device)
                return at;
            at = up;
        }
    }

    /// `dir/name`, or `dir/name.2`, `dir/name.3` ... (also free in `alsoIn` with `alsoSuffix`)
    std::string FreeName(const fs::path& dir, const std::string& name, const fs::path& alsoIn = {}, const std::string& alsoSuffix = {})
    {
        std::error_code ec;
        std::string candidate = name;
        for (int n = 2; fs::exists(dir / candidate, ec) || (!alsoIn.empty() && fs::exists(alsoIn / (candidate + alsoSuffix), ec)); n++)
        {
            const fs::path p(name);
            candidate = p.stem().string() + "." + std::to_string(n) + p.extension().string();
        }
        return candidate;
    }

#if !defined(__APPLE__)
    std::string LocalTime()
    {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_r(&now, &local);
        char text[32];
        std::strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &local);
        return text;
    }
#endif
#endif
}  // namespace

std::string HostTrash::TrashInfo(const fs::path& original, const std::string& deletionDate)
{
    return "[Trash Info]\nPath=" + PercentEncode(FileHelper::FromFsPath(original)) + "\nDeletionDate=" + deletionDate + "\n";
}

fs::path HostTrash::TopDirTrash(const fs::path& topdir, unsigned uid)
{
    return topdir / (".Trash-" + std::to_string(uid));
}

bool HostTrash::Move(const fs::path& path, std::string* error)
{
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(path, ec)))
        return Fail(error, FileHelper::FromFsPath(path) + ": no such file");
    const fs::path absolute = fs::absolute(path, ec).lexically_normal();

#if defined(_WIN32)
    // A double-zero-terminated list of one path; FOF_ALLOWUNDO sends it to the Recycle Bin
    std::wstring from = absolute.wstring();
    from.push_back(L'\0');
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    const int rc = SHFileOperationW(&op);
    if (rc != 0 || op.fAnyOperationsAborted)
        return Fail(error, FileHelper::FromFsPath(absolute) + ": the Recycle Bin refused it (error " + std::to_string(rc) + ")");
    return true;
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (!home || !*home)
        return Fail(error, "no home folder: no trash");
    dev_t fileDevice = 0, homeDevice = 0;
    fs::path trash = fs::path(home) / ".Trash";
    if (Device(absolute.parent_path(), fileDevice) && Device(home, homeDevice) && fileDevice != homeDevice)
        trash = TopDir(absolute) / ".Trashes" / std::to_string(::getuid());  // another volume: its own trash
    fs::create_directories(trash, ec);
    if (ec)
        return Fail(error, FileHelper::FromFsPath(trash) + ": " + ec.message());
    const fs::path target = trash / FreeName(trash, absolute.filename().string());
    fs::rename(absolute, target, ec);
    if (ec)
        return Fail(error, FileHelper::FromFsPath(absolute) + " -> " + FileHelper::FromFsPath(target) + ": " + ec.message());
    return true;
#else
    // freedesktop.org Trash specification 1.0
    fs::path home;
    if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
        home = fs::path(data) / "Trash";
    else if (const char* h = std::getenv("HOME"); h && *h)
        home = fs::path(h) / ".local" / "share" / "Trash";
    fs::path trash = home;
    dev_t fileDevice = 0, homeDevice = 0;
    if (!home.empty())
    {
        fs::create_directories(home, ec);
        if (Device(absolute.parent_path(), fileDevice) && Device(home, homeDevice) && fileDevice != homeDevice)
            trash = TopDirTrash(TopDir(absolute), static_cast<unsigned>(::getuid()));
    }
    else
    {
        trash = TopDirTrash(TopDir(absolute), static_cast<unsigned>(::getuid()));
    }
    fs::create_directories(trash / "files", ec);
    fs::create_directories(trash / "info", ec);
    if (ec || trash.empty())
        return Fail(error, "no usable trash for " + FileHelper::FromFsPath(absolute) + (ec ? ": " + ec.message() : std::string()));
    if (trash != home)
        fs::permissions(trash, fs::perms::owner_all, fs::perm_options::replace, ec);

    const std::string name = FreeName(trash / "files", absolute.filename().string(), trash / "info", ".trashinfo");
    const fs::path info = trash / "info" / (name + ".trashinfo");
    {
        // The info file first: the spec's way of claiming the name
        std::ofstream out(info, std::ios::trunc);
        out << TrashInfo(absolute, LocalTime());
        if (!out)
            return Fail(error, FileHelper::FromFsPath(info) + ": cannot be written");
    }
    fs::rename(absolute, trash / "files" / name, ec);
    if (ec)
    {
        std::error_code ignored;
        fs::remove(info, ignored);
        return Fail(error, FileHelper::FromFsPath(absolute) + " -> " + FileHelper::FromFsPath(trash / "files" / name) + ": " + ec.message());
    }
    return true;
#endif
}
