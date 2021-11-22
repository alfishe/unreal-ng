#include "stdafx.h"

#include "filehelper.h"
#include "common/stringhelper.h"
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

	// Important note: std::filesystem::path::make_preferred() does not convert windows separators to linux preferred so we have to care about that (if configs are created with backslashes)
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

bool FileHelper::IsFile(std::string& path)
{
	bool result = FileExists(path);

	return result;
}

bool FileHelper::IsFolder(std::string& path)
{
	bool result = FolderExists(path);

	return result;
}

bool FileHelper::FileExists(std::string& path)
{
	bool result = false;
	Pathie::Path basePath(path);

	if (basePath.exists() && basePath.is_file())
	{
		result = true;
	}

	return result;
}

bool FileHelper::FolderExists(std::string& path)
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

size_t FileHelper::ReadFileToBuffer(FILE* file, uint8_t* buffer, size_t size)
{
    size_t result = 0;

    if (file == nullptr || buffer == nullptr || size == 0)
    {
        return result;
    }

    result = fread(buffer, size, 1, file);

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