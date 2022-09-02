#include "stdafx.h"

#include "filehelper.h"
#include <algorithm>

const char FileHelper::GetPathSeparator()
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
        char buffer[PATH_MAX] = { '\0' };;
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

string FileHelper::NormalizePath(std::string& path, char separator)
{
	if (separator == L'\0')
		separator = GetPathSeparator();

	string result = path;

	replace(result.begin(), result.end(), '/', separator);
	replace(result.begin(), result.end(), '\\', separator);

	return result;
}

string FileHelper::NormalizePath(std::string& path)
{
	static char systemSeparator = GetPathSeparator();

	string result = NormalizePath(path, systemSeparator);

	return result;
}

std::string FileHelper::AbsolutePath(std::string& path)
{
    std::string result;

#if defined _WIN32
#endif

#ifdef __linux__
    char buffer[PATH_MAX];
    realpath(path.c_str(), buffer);
    result = std::string(buffer);
#endif

#ifdef __APPLE__
    char buffer[PATH_MAX];
    realpath(path.c_str(), buffer);
    result = std::string(buffer);
#endif

    return result;
}

string FileHelper::PathCombine(std::string& path1, string& path2)
{
#ifdef _WIN32
    Pathie::Path basePath = Pathie::Path::from_native(StringHelper::StringToWideString(path1));
#endif // _WIN32

#ifndef _WIN32
    Pathie::Path basePath = Pathie::Path::from_native(path1);
#endif

    basePath.expand();
    basePath /= path2;

	string result = basePath.str();
	result = NormalizePath(result);

	return result;
}

string FileHelper::PathCombine(std::string& path1, const char* path2)
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

size_t FileHelper::GetFileSize(std::string& path)
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

string FileHelper::PrintablePath(std::string path)
{
	string result = path;

	return result;
}

FILE* FileHelper::OpenFile(std::string& path, const char* mode)
{
    FILE* result = nullptr;

    if (FileExists(path))
    {
        result = fopen(path.c_str(), mode);
    }

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
size_t FileHelper::ReadFileToBuffer(std::string& filePath, uint8_t* buffer, size_t size)
{
    size_t result = 0;

    FILE* file = FileHelper::OpenFile(filePath, "r");
    if (file != nullptr)
    {
        result = FileHelper::ReadFileToBuffer(file, buffer, size);

        FileHelper::CloseFile(file);
    }

    return result;
}

bool FileHelper::SaveBufferToFile(std::string& filePath, uint8_t* buffer, size_t size)
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