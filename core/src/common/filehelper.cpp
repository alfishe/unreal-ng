#include "stdafx.h"

#include "filehelper.h"
#include <algorithm>
#include <filesystem>

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
    string result;

    #if defined _WIN32
        char buffer[MAX_PATH] = { '\0' };
        GetModuleFileNameA(NULL, buffer, MAX_PATH - 1);
        result = buffer;
    #endif

    #ifdef __linux__
        char buffer[PATH_MAX]  = { '\0' };
        ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX - 1);
        buffer[count] = '\0';
        result = buffer;
    #endif

    #ifdef __APPLE__
        char buffer[PATH_MAX] = { '\0' };
        uint32_t size = PATH_MAX - 1;
        if (_NSGetExecutablePath(buffer, &size) == 0)
        {
            size = strnlen(buffer, size);
			buffer[size] = '\0';
            result = buffer;
        }
    #endif

    Pathie::Path executablePath(result);
    result = executablePath.parent().str();



    Pathie::Path exepath = Pathie::Path::exe();
    result = exepath.parent().str();

	return result;
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
        fs::path absolutePath = fs::path(path);

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

        if (resolveSymlinks)
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
        //throw std::runtime_error("Failed to get absolute path and resolve symlinks: " + std::string(e.what()));
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
#else // Unix-like systems (Linux and macOS)
    char buffer[PATH_MAX];
    char* resolved = realpath(path.c_str(), buffer);
    if (resolved != nullptr)
    {
        result = std::string(resolved);
        result = NormalizePath(result);
    }
    else if (errno == ENOENT) // Path doesn't exist, but might be valid
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
        if (path[0] == '/') // Absolute path
        {
            result = path;
        }
        else // Relative path, convert to absolute
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
#ifdef _WIN32
    Pathie::Path basePath = Pathie::Path::from_native(StringHelper::StringToWideString(path1));
#endif // _WIN32

#ifndef _WIN32
    Pathie::Path basePath = Pathie::Path::from_native(path1);
#endif

    basePath.expand();
    basePath /= path2;

    std::string result = basePath.str();
	result = NormalizePath(result);

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
	bool result = false;
	Pathie::Path basePath(path);

	if (basePath.exists() && basePath.is_file())
	{
		result = true;
	}

	return result;
}

bool FileHelper::FolderExists(const std::string& path)
{
	bool result = false;
    Pathie::Path basePath(path);

	if (basePath.exists() && basePath.is_directory())
	{
		result = true;
	}

	return result;
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
    std::string result;

    Pathie::Path filepath(path);
    result = filepath.extension();

    return result;
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