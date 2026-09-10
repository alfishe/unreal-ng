#include "stdafx.h"

#include "filehelper.h"

#include "common/stringhelper.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>  // _getdcwd
#else
#include <climits>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

/// region <Internal helpers>

namespace
{
#ifdef _WIN32
    /// UTF-8 -> UTF-16 for Win32/CRT wide APIs
    inline std::wstring W(const std::string& utf8)
    {
        return StringHelper::StringToWideString(utf8);
    }

    /// UTF-16 -> UTF-8
    inline std::string U8(const std::wstring& wide)
    {
        return StringHelper::WideStringToString(wide);
    }
#endif

    inline bool IsSep(char c)
    {
        return c == '/' || c == '\\';
    }

    /// "X:" prefix (Windows-only callers; POSIX treats "X:" as an ordinary component)
    [[maybe_unused]] inline bool HasDriveLetter(const std::string& p)
    {
        return p.size() >= 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':';
    }

    [[maybe_unused]] bool EqualsIgnoreCase(const std::string& a, const char* b)
    {
        size_t n = std::strlen(b);
        if (a.size() != n)
            return false;
        for (size_t i = 0; i < n; i++)
        {
            if (std::toupper(static_cast<unsigned char>(a[i])) != std::toupper(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    }

    /// Root of a path (native separators already applied).
    /// root       - text that is kept verbatim and never touched by "." / ".." processing
    ///              Windows: "C:", "\\server\share", "\\?\C:", "\\?\UNC\server\share", "" (rooted without drive)
    ///              POSIX:   "/" for a path starting with exactly "//" (POSIX keeps it significant), "" otherwise
    /// hasRootDir - a separator follows the root (the path is anchored, ".." cannot climb above the root)
    /// bodyStart  - offset where the regular components start
    struct PathRoot
    {
        std::string root;
        bool hasRootDir = false;
        size_t bodyStart = 0;
    };

    PathRoot SplitRoot(const std::string& p)
    {
        PathRoot r;
        const size_t n = p.size();

#ifdef _WIN32
        const char sep = FileHelper::GetPathSeparator();
        if (n >= 2 && p[0] == sep && p[1] == sep)
        {
            // UNC or Win32 device path. Root components: "server\share", "?\C:", ".\device", "?\UNC\server\share".
            // Extra separators between root components are collapsed.
            size_t pos = 2;
            while (pos < n && p[pos] == sep)
                pos++;

            std::vector<std::string> comps;
            size_t wanted = 2;
            while (comps.size() < wanted && pos < n)
            {
                size_t start = pos;
                while (pos < n && p[pos] != sep)
                    pos++;
                comps.push_back(p.substr(start, pos - start));

                if (comps.size() == 2 && (comps[0] == "?" || comps[0] == ".") && EqualsIgnoreCase(comps[1], "UNC"))
                    wanted = 4;

                if (comps.size() < wanted)
                {
                    while (pos < n && p[pos] == sep)
                        pos++;
                }
            }

            r.root.assign(2, sep);
            for (size_t k = 0; k < comps.size(); k++)
            {
                if (k > 0)
                    r.root += sep;
                r.root += comps[k];
            }
            r.hasRootDir = pos < n && p[pos] == sep;
            r.bodyStart = pos;
            return r;
        }

        if (HasDriveLetter(p))
        {
            r.root = p.substr(0, 2);
            r.hasRootDir = n > 2 && p[2] == sep;
            r.bodyStart = 2;
            return r;
        }

        if (n >= 1 && p[0] == sep)
        {
            // Rooted on the current drive
            r.hasRootDir = true;
            r.bodyStart = 0;
            return r;
        }
#else
        if (n >= 2 && p[0] == '/' && p[1] == '/' && (n == 2 || p[2] != '/'))
        {
            // POSIX: exactly two leading slashes are implementation-defined and must be preserved;
            // three or more are equivalent to one (handled by the branch below)
            r.root = "/";
            r.hasRootDir = true;
            r.bodyStart = 1;
            return r;
        }

        if (n >= 1 && p[0] == '/')
        {
            r.hasRootDir = true;
            r.bodyStart = 0;
            return r;
        }
#endif

        return r;  // relative
    }

    std::string CurrentDirectory()
    {
        std::error_code ec;
        fs::path cwd = fs::current_path(ec);
        if (ec)
            return std::string();
#ifdef _WIN32
        return U8(cwd.wstring());
#else
        return cwd.string();
#endif
    }

    /// Resolve symlinks / on-disk letter case of an existing path. Returns empty string on failure.
    std::string CanonicalExistingPath(const std::string& nativePath)
    {
        std::string result;

#ifdef _WIN32
        // Handle-based resolution works identically for drive paths, UNC shares and \\?\ prefixed paths
        // on both MSVC and MinGW (libstdc++'s std::filesystem::canonical mangles UNC paths).
        HANDLE handle = CreateFileW(W(nativePath).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return result;

        std::vector<wchar_t> buffer(32768);
        DWORD length = GetFinalPathNameByHandleW(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
                                                 FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        CloseHandle(handle);

        if (length == 0 || length >= buffer.size())
            return result;

        result = U8(std::wstring(buffer.data(), length));

        // Strip the extended-length prefix: "\\?\UNC\server\share\x" -> "\\server\share\x", "\\?\C:\x" -> "C:\x"
        static const std::string uncPrefix = "\\\\?\\UNC\\";
        static const std::string devPrefix = "\\\\?\\";
        if (result.compare(0, uncPrefix.size(), uncPrefix) == 0)
            result = "\\\\" + result.substr(uncPrefix.size());
        else if (result.compare(0, devPrefix.size(), devPrefix) == 0)
            result = result.substr(devPrefix.size());
#else
        char* resolved = realpath(nativePath.c_str(), nullptr);
        if (resolved != nullptr)
        {
            result = resolved;
            free(resolved);
        }
#endif

        return result;
    }
}  // namespace

/// endregion </Internal helpers>

char FileHelper::GetPathSeparator()
{
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

//
// Returns path for executable file
//
std::string FileHelper::GetExecutablePath()
{
    std::filesystem::path exePath;

#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768);
    DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size())
        return {};  // error
    return U8(std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().wstring());

#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // get the size needed
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};  // error
    exePath = std::filesystem::path(buffer.data()).lexically_normal();

#elif defined(__linux__)
    std::vector<char> buffer(4096);
    ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length == -1)
        return {};          // error
    buffer[length] = '\0';  // readlink doesn't null-terminate
    exePath = std::filesystem::path(buffer.data());
#else
    #error "Unsupported platform"
#endif

    return exePath.parent_path().string();
}

std::string FileHelper::GetResourcesPath()
{
    std::string resourcesPath;

#if defined(_WIN32)
    // On Windows, resources are in the same directory as the executable
    resourcesPath = GetExecutablePath();

#elif defined(__APPLE__)
    // On macOS, check if we're inside an app bundle
    std::string execPath = GetExecutablePath();

    // Check if we're in an app bundle (path contains .app/Contents/MacOS)
    if (execPath.find(".app/Contents/MacOS") != std::string::npos)
    {
        // We're in an app bundle, resources should be in Contents/Resources
        fs::path path(execPath);
        path = path.parent_path();  // Move from MacOS to Contents
        path /= "Resources";        // Move to Resources directory
        resourcesPath = path.string();
    }
    else
    {
        // Not in an app bundle, use executable directory
        resourcesPath = execPath;
    }

#elif defined(__linux__)
    // On Linux, resources are in the same directory as the executable
    resourcesPath = GetExecutablePath();
#else
#error "Unsupported platform"
#endif

    return resourcesPath;
}

std::string FileHelper::ExpandPath(const std::string& path)
{
    // Only a leading "~" that is the whole path or is followed by a separator refers to the home directory
    if (path.empty() || path[0] != '~' || (path.size() > 1 && !IsSep(path[1])))
        return path;

#ifdef _WIN32
    // Wide environment - the narrow getenv() would return the ANSI rendering of a non-ASCII user name
    const wchar_t* home = _wgetenv(L"USERPROFILE");
    std::string homeDir = home ? U8(home) : "";
    if (homeDir.empty())
    {
        const wchar_t* drive = _wgetenv(L"HOMEDRIVE");
        const wchar_t* dir = _wgetenv(L"HOMEPATH");
        if (drive && dir)
            homeDir = U8(std::wstring(drive) + dir);
    }
#else
    const char* home = getenv("HOME");
    std::string homeDir = home ? home : "";
#endif

    if (homeDir.empty())
        return path;

    while (homeDir.size() > 1 && IsSep(homeDir.back()))
        homeDir.pop_back();

    return homeDir + path.substr(1);
}

std::string FileHelper::NormalizePath(const std::string& path, char separator)
{
    if (separator == '\0')
        separator = GetPathSeparator();

    std::string result = path;

    std::replace(result.begin(), result.end(), '/', separator);
    std::replace(result.begin(), result.end(), '\\', separator);

    return result;
}

std::string FileHelper::NormalizePath(const std::string& path)
{
    return NormalizePath(path, GetPathSeparator());
}

std::string FileHelper::LexicallyNormalPath(const std::string& path)
{
    if (path.empty())
        return path;

    const char sep = GetPathSeparator();
    const std::string p = NormalizePath(path, sep);
    const size_t n = p.size();
    const PathRoot root = SplitRoot(p);

    // Split the body into components (empty ones = duplicated separators are dropped)
    std::vector<std::string> comps;
    size_t start = root.bodyStart;
    for (size_t i = root.bodyStart; i <= n; i++)
    {
        if (i == n || p[i] == sep)
        {
            if (i > start)
                comps.push_back(p.substr(start, i - start));
            start = i + 1;
        }
    }

    // Fold "." and ".." (std::filesystem::path::lexically_normal semantics)
    bool trailing = p.back() == sep;
    std::vector<std::string> out;
    for (size_t k = 0; k < comps.size(); k++)
    {
        const std::string& c = comps[k];
        const bool last = (k + 1 == comps.size());

        if (c == ".")
        {
            if (last)
                trailing = true;
            continue;
        }

        if (c == "..")
        {
            if (!out.empty() && out.back() != "..")
            {
                out.pop_back();
                if (last)
                    trailing = true;
            }
            else if (root.hasRootDir)
            {
                // Cannot climb above the root - drop it
                if (last)
                    trailing = true;
            }
            else
            {
                out.push_back(c);
            }
            continue;
        }

        out.push_back(c);
    }

    if (!out.empty() && out.back() == "..")
        trailing = false;

    std::string result = root.root;
    if (root.hasRootDir)
        result += sep;
    for (size_t k = 0; k < out.size(); k++)
    {
        if (k > 0)
            result += sep;
        result += out[k];
    }
    if (trailing && !out.empty())
        result += sep;

    if (result.empty())
        result = ".";

    return result;
}

bool FileHelper::IsUNCPath(const std::string& path)
{
    return path.size() >= 3 && IsSep(path[0]) && IsSep(path[1]) && !IsSep(path[2]);
}

bool FileHelper::IsAbsolutePath(const std::string& path)
{
#ifdef _WIN32
    // Anything starting with two separators lives in the UNC / device namespace and never depends on the cwd
    if (path.size() >= 2 && IsSep(path[0]) && IsSep(path[1]))
        return true;
    return HasDriveLetter(path) && path.size() > 2 && IsSep(path[2]);
#else
    // POSIX (macOS/Linux). Native rule: absolute means '/'-rooted.
    //
    // Additionally, '\'-rooted spellings (\opt\unreal, \\server\share, \\?\C:\...)
    // are classified absolute here as well. This is safe and consistent because
    // FileHelper's own contract already aliases '\' to '/' on every platform:
    // NormalizePath() unconditionally rewrites every '\' to the native separator,
    // so a '\'-rooted string always denotes the same location as its '/'-rooted
    // normalization — never a cwd-relative POSIX filename that merely starts
    // with a backslash (such names are not representable through FileHelper).
    return !path.empty() && (path[0] == '/' || path[0] == '\\');
#endif
}

std::string FileHelper::AbsolutePath(const std::string& path, bool resolveSymlinks)
{
    if (path.empty())
        return path;

    const char sep = GetPathSeparator();
    std::string result = NormalizePath(ExpandPath(path), sep);

    if (!IsAbsolutePath(result))
    {
#ifdef _WIN32
        if (HasDriveLetter(result))
        {
            // Drive-relative ("C:dir\file") - relative to the current directory of that drive
            int drive = std::toupper(static_cast<unsigned char>(result[0])) - 'A' + 1;
            wchar_t* driveCwd = _wgetdcwd(drive, nullptr, 0);
            std::string base = driveCwd ? U8(driveCwd) : result.substr(0, 2) + sep;
            free(driveCwd);

            std::string rest = result.substr(2);
            result = rest.empty() ? base : base + sep + rest;
        }
        else if (result[0] == sep)
        {
            // Rooted without drive ("\dir\file") - on the current drive
            std::error_code ec;
            result = fs::current_path(ec).root_name().string() + result;
        }
        else
        {
            result = CurrentDirectory() + sep + result;
        }
#else
        result = CurrentDirectory() + sep + result;
#endif
    }

    result = LexicallyNormalPath(result);

    if (resolveSymlinks && (FileExists(result) || FolderExists(result)))
    {
        std::string canonical = CanonicalExistingPath(result);
        if (!canonical.empty())
            result = NormalizePath(canonical, sep);
    }

    return result;
}

std::string FileHelper::PathCombine(const std::string& path1, const std::string& path2)
{
    if (path1.empty())
        return path2;
    if (path2.empty())
        return path1;

    const char sep = GetPathSeparator();
    std::string first = NormalizePath(path1, sep);
    std::string second = NormalizePath(path2, sep);

    bool firstHasSep = first.back() == sep;
    bool secondHasSep = second.front() == sep;

    if (firstHasSep && secondHasSep)
        return first + second.substr(1);
    if (firstHasSep || secondHasSep)
        return first + second;
    return first + sep + second;
}

std::string FileHelper::PathCombine(const std::string& path1, const char* path2)
{
    return PathCombine(path1, std::string(path2 ? path2 : ""));
}

bool FileHelper::IsFile(const std::string& path)
{
    return FileExists(path);
}

bool FileHelper::IsFolder(const std::string& path)
{
    return FolderExists(path);
}

bool FileHelper::FileExists(const std::string& path)
{
    if (path.empty())
        return false;

    std::string nativePath = NormalizePath(path);

#ifdef _WIN32
    // Win32 API accepts every path shape (drive, UNC share root, \\?\ prefix) on MSVC and MinGW alike
    DWORD attrs = GetFileAttributesW(W(nativePath).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
    std::error_code ec;
    return fs::is_regular_file(nativePath, ec) && !ec;
#endif
}

bool FileHelper::FolderExists(const std::string& path)
{
    if (path.empty())
        return false;

    std::string nativePath = NormalizePath(path);

#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(W(nativePath).c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    std::error_code ec;
    return fs::is_directory(nativePath, ec) && !ec;
#endif
}

size_t FileHelper::GetFileSize(const std::string& path)
{
    size_t result = static_cast<size_t>(-1);

    if (path.empty())
        return result;

    std::string nativePath = NormalizePath(path);

#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (GetFileAttributesExW(W(nativePath).c_str(), GetFileExInfoStandard, &data) &&
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
    {
        result = (static_cast<size_t>(data.nFileSizeHigh) << 32) | static_cast<size_t>(data.nFileSizeLow);
    }
#else
    std::error_code ec;
    uintmax_t size = fs::file_size(nativePath, ec);
    if (!ec)
        result = static_cast<size_t>(size);
#endif

    return result;
}

size_t FileHelper::GetFileSize(FILE* file)
{
    size_t result = -1;

    if (file != nullptr)
    {
        // Remember current position
        fpos_t position;
        fgetpos(file, &position);

        // Determine EOF position = file size
        rewind(file);
        fseek(file, 0, SEEK_END);
        result = ftell(file);

        // Restore position
        fsetpos(file, &position);
    }

    return result;
}

std::string FileHelper::GetFileExtension(const std::string& path)
{
    // Work on the last component only, so roots ("C:", "\\server\share", "\\?\") and folders with dots are ignored
    size_t lastSep = path.find_last_of("/\\");
    std::string name = (lastSep == std::string::npos) ? path : path.substr(lastSep + 1);

    if (name == "." || name == "..")
        return std::string();

    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0)  // no extension, or dot-file without extension (".bashrc")
        return std::string();

    return name.substr(dot + 1);
}

std::string FileHelper::PrintablePath(const std::string& path)
{
    return path;
}

FILE* FileHelper::OpenExistingFile(const std::string& path, const char* mode)
{
    FILE* result = nullptr;

    if (FileExists(path))
    {
        result = OpenFile(path, mode);
    }

    return result;
}

FILE* FileHelper::OpenFile(const std::string& path, const char* mode)
{
    if (path.empty())
        return nullptr;

    std::string nativePath = NormalizePath(path);
#ifdef _WIN32
    // _wfopen: the narrow fopen() would interpret the UTF-8 bytes in the ANSI code page
    return _wfopen(W(nativePath).c_str(), W(mode ? mode : "rb").c_str());
#else
    return fopen(nativePath.c_str(), mode);
#endif
}

std::filesystem::path FileHelper::ToFsPath(const std::string& utf8Path)
{
#ifdef _WIN32
    return std::filesystem::path(W(utf8Path));
#else
    return std::filesystem::path(utf8Path);
#endif
}

void FileHelper::CloseFile(FILE* file)
{
    if (file != nullptr)
    {
        fclose(file);
    }
}

/// Load file content to buffer provided (up to @refitem size bytes)
/// @param file File handle
/// @param buffer Buffer
/// @param size Buffer size
/// @return Number of bytes loaded from file to the buffer
size_t FileHelper::ReadFileToBuffer(FILE* file, uint8_t* buffer, size_t size)
{
    size_t result = 0;

    if (file == nullptr || buffer == nullptr || size == 0)
    {
        return result;
    }

    result = fread(buffer, 1, size, file);

    return result;
}

/// Load file content to buffer provided (up to @refitem size bytes)
/// @param filePath File path
/// @param buffer Buffer
/// @param size Buffer size
/// @return Number of bytes loaded from file to the buffer
size_t FileHelper::ReadFileToBuffer(const std::string& filePath, uint8_t* buffer, size_t size)
{
    size_t result = 0;

    FILE* file = FileHelper::OpenExistingFile(filePath, "rb");
    if (file != nullptr)
    {
        result = FileHelper::ReadFileToBuffer(file, buffer, size);

        FileHelper::CloseFile(file);
    }

    return result;
}

bool FileHelper::SaveBufferToFile(FILE* file, uint8_t* buffer, size_t size)
{
    bool result = false;

    if (file == nullptr || buffer == nullptr || size == 0)
    {
        return result;
    }

    size_t bytesWritten = fwrite(buffer, 1, size, file);

    if (bytesWritten == size)
    {
        result = true;
    }

    return result;
}

bool FileHelper::SaveBufferToFile(const std::string& filePath, uint8_t* buffer, size_t size)
{
    bool result = false;

    if (buffer == nullptr || size == 0)
    {
        return result;
    }

    FILE* file = OpenFile(filePath, "wb");
    if (file != nullptr)
    {
        result = SaveBufferToFile(file, buffer, size);
        fclose(file);
    }

    return result;
}
