#include "stdafx.h"
#include "common/logger.h"

#include "filehelper.h"
#include "common/stringhelper.h"
#include <algorithm>

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
		wchar_t buffer[MAX_PATH] = { L'\0' };
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		result = buffer;
    #endif

    #ifdef __linux__
		char buffer[PATH_MAX];
		ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
		buffer[count] = '\0';
		result = buffer;
    #endif

    #ifdef __APPLE__
        char buffer[PATH_MAX];
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0)
        {
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

string FileHelper::NormalizePath(string& path, char separator)
{
	if (separator == L'\0')
		separator = GetPathSeparator();

	string result = path;

	// Important note: std::filesystem::path::make_preferred() does not convert windows separators to linux preferred so we have to care about that (if configs are created with backslashes)
	replace(result.begin(), result.end(), '/', separator);
	replace(result.begin(), result.end(), '\\', separator);

	return result;
}

string FileHelper::NormalizePath(string& path)
{
	static char systemSeparator = GetPathSeparator();

	string result = NormalizePath(path, systemSeparator);

	return result;
}

string FileHelper::PathCombine(string& path1, string& path2)
{
    Pathie::Path basePath = Pathie::Path::from_native(path1);
    basePath.expand();

    basePath /= path2;

	string result = basePath.str();
	result = NormalizePath(result);

	return result;
}

string FileHelper::PathCombine(string& path1, const char* path2)
{
	string pathPart2 = path2;
	return PathCombine(path1, pathPart2);
}

bool FileHelper::IsFile(string& path)
{
	bool result = FileExists(path);

	return result;
}

bool FileHelper::IsFolder(string& path)
{
	bool result = FolderExists(path);

	return result;
}

bool FileHelper::FileExists(string& path)
{
	bool result = false;
	Pathie::Path basePath(path);

	if (basePath.exists() && basePath.is_file())
	{
		result = true;
	}

	return result;
}

bool FileHelper::FolderExists(string& path)
{
	bool result = false;
    Pathie::Path basePath(path);

	if (basePath.exists() && basePath.is_directory())
	{
		result = true;
	}

	return result;
}

string FileHelper::PrintablePath(string path)
{
	string result = path;

	return result;
}

bool FileHelper::SaveBufferToFile(string& filePath, uint8_t* buffer, size_t size)
{
    bool result = false;

    FILE* file = fopen(filePath.c_str(), "wb");
    if (file != nullptr)
    {
        fwrite(buffer, 1, size, file);
        fclose(file);

        result = true;
    }

    return result;
}