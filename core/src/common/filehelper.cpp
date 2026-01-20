#include "stdafx.h"

#include "filehelper.h"
#include <algorithm>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

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
    std::vector<char> buffer(4096);
    DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size())
        return {};  // error
    exePath = std::filesystem::path(buffer.data());

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

std::string FileHelper::NormalizePath(const std::string& path, char separator)
{
    if (separator == L'\0')
        separator = GetPathSeparator();

    std::string result = path;

    replace(result.begin(), result.end(), '/', separator);
    replace(result.begin(), result.end(), '\\', separator);

    return result;
}

std::string FileHelper::NormalizePath(const std::string& path)
{
    const char systemSeparator = GetPathSeparator();
    std::string result = NormalizePath(path, systemSeparator);

    return result;
}

std::string FileHelper::AbsolutePath(const std::string& path, bool resolveSymlinks)
{
    std::string result = path;

    try
    {
        // On Unix/macOS, normalize backslashes to forward slashes first
        // This ensures paths like "\\opt\\path" are recognized as absolute paths
        std::string normalizedPath = path;
#ifndef _WIN32
        std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
#endif
        
        // Expand tilde to home directory (cross-platform)
        if (!normalizedPath.empty() && normalizedPath[0] == '~')
        {
#ifdef _WIN32
            const char* home = getenv("USERPROFILE");
            if (!home) home = getenv("HOMEPATH");
#else
            const char* home = getenv("HOME");
#endif
            if (home)
            {
                normalizedPath = std::string(home) + normalizedPath.substr(1);
            }
        }

        fs::path absolutePath = fs::path(normalizedPath);

        if (resolveSymlinks)
        {
            // If the input isn't already absolute, prepend current path
            if (!absolutePath.is_absolute())
            {
                absolutePath = fs::current_path() / absolutePath;
            }

            // Convert to absolute path and normalize - checks filesystem
            absolutePath = fs::absolute(absolutePath);
        }

        // Normalize the path (remove . and ..)
        absolutePath = absolutePath.lexically_normal();

        if (resolveSymlinks && fs::exists(absolutePath))
        {
            // For consistent comparison, make sure to resolve any remaining symlinks
            absolutePath = fs::canonical(absolutePath);
        }

        result = absolutePath.generic_string();
        result = NormalizePath(result);
    }
    catch (const std::exception& e)
    {
        // Handle error (e.g., path doesn't exist)
        // throw std::runtime_error("Failed to get absolute path and resolve symlinks: " + std::string(e.what()));
    }

    return result;

#if defined _WIN32
    char buffer[MAX_PATH];
    DWORD length = GetFullPathNameA(path.c_str(), MAX_PATH, buffer, nullptr);
    if (length != 0)
    {
        result = std::string(buffer, length);
        result = NormalizePath(result);
    }
    return result;
#else  // Unix-like systems (Linux and macOS)
    char buffer[PATH_MAX];
    char* resolved = realpath(path.c_str(), buffer);
    if (resolved != nullptr)
    {
        result = std::string(resolved);
        result = NormalizePath(result);
    }
    else if (errno == ENOENT)  // Path doesn't exist, but might be valid
    {
        // Try to resolve the parent directory
        size_t lastSlash = path.find_last_of('/');
        if (lastSlash != std::string::npos)
        {
            std::string parentDir = path.substr(0, lastSlash);
            char* resolvedParent = realpath(parentDir.c_str(), buffer);
            if (resolvedParent != nullptr)
            {
                result = std::string(resolvedParent) + path.substr(lastSlash);
                result = NormalizePath(result);
                return result;
            }
        }

        // If parent directory resolution fails, handle as before
        if (path[0] == '/')  // Absolute path
        {
            result = path;
        }
        else  // Relative path, convert to absolute
        {
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != nullptr)
            {
                result = std::string(cwd) + "/" + path;
            }
        }
        result = NormalizePath(result);
    }
#endif

    return result;
}

std::string FileHelper::PathCombine(const std::string& path1, const std::string& path2)
{
    std::string result;

    // Handle empty paths
    if (path1.empty())
        return path2;
    if (path2.empty())
        return path1;

    // Combine paths
    char separator = '/';  // Use forward slash as universal separator

    // Check if path1 ends with separator or path2 starts with one
    bool path1_has_sep = !path1.empty() && (path1.back() == '/' || path1.back() == '\\');
    bool path2_has_sep = !path2.empty() && (path2.front() == '/' || path2.front() == '\\');

    if (path1_has_sep && path2_has_sep)
    {
        result = path1 + path2.substr(1);
    }
    else if (path1_has_sep || path2_has_sep)
    {
        result = path1 + path2;
    }
    else
    {
        result = path1 + separator + path2;
    }

    // Normalize the path (convert to forward slashes, remove duplicates, etc.)
    result = NormalizePath(result);

#ifdef _WIN32
    // On Windows, we might want to handle drive letters and UNC paths
    if (result.size() >= 2 && result[1] == ':')
    {
        // Drive letter path - ensure proper formatting
        if (result.size() > 2 && result[2] != '/')
        {
            result.insert(2, 1, '/');
        }
    }
#endif

    return result;
}

std::string FileHelper::PathCombine(const std::string& path1, const char* path2)
{
    string pathPart2 = path2;
    return PathCombine(path1, pathPart2);
}

bool FileHelper::IsFile(const std::string& path)
{
    bool result = FileExists(path);

    return result;
}

bool FileHelper::IsFolder(const std::string& path)
{
    bool result = FolderExists(path);

    return result;
}

bool FileHelper::FileExists(const std::string& path)
{
    std::error_code ec;  // To avoid throwing exceptions
    return fs::is_regular_file(path, ec) && !ec;
}

bool FileHelper::FolderExists(const std::string& path)
{
    std::error_code ec;  // To avoid throwing exceptions
    return fs::is_directory(path, ec) && !ec;
}

size_t FileHelper::GetFileSize(const std::string& path)
{
    size_t result = -1;

    std::ifstream ifile;
    ifile.open(path);

    if (ifile.is_open())
    {
        ifile.seekg(0, std::ios_base::end);
        std::fpos pos = ifile.tellg();

        result = pos;

        ifile.close();
    }

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
    fs::path filepath(path);
    std::string ext = filepath.extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext = ext.substr(1);
    // Convert to lowercase if desired (common convention)
    // std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

std::string FileHelper::PrintablePath(const std::string& path)
{
    string result = path;

    return result;
}

FILE* FileHelper::OpenExistingFile(const std::string& path, const char* mode)
{
    FILE* result = nullptr;

    if (FileExists(path))
    {
        result = fopen(path.c_str(), mode);
    }

    return result;
}

FILE* FileHelper::OpenFile(const std::string& path, const char* mode)
{
    FILE* result = nullptr;

    result = fopen(path.c_str(), mode);

    return result;
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

    FILE* file = fopen(filePath.c_str(), "wb");
    if (file != nullptr)
    {
        fwrite(buffer, 1, size, file);
        fclose(file);

        result = true;
    }

    return result;
}