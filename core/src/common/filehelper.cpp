#include "stdafx.h"
#include "common/logger.h"

#include "filehelper.h"
#include "common/stringhelper.h"
#include <algorithm>

//
// Returns path for executable file
//
wstring FileHelper::GetExecutablePath()
{
	wstring result = filesystem::current_path().wstring();

	#ifdef _WIN32
		wchar_t buffer[MAX_PATH] = { L'\0' };
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		result = buffer;
    #endif

    #ifdef __linux__
		char buffer[PATH_MAX];
		ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
		buffer[count] = '\0';
		result = StringHelper::StringToWideString(buffer);
    #endif

    #ifdef __APPLE__
        char buffer[PATH_MAX];
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0)
        {
			buffer[size] = '\0';
            result = StringHelper::StringToWideString(buffer);
        }
    #endif

	filesystem::path basePath = filesystem::canonical(result);
	result = basePath.parent_path().wstring();

	return result;
}

wstring FileHelper::NormalizePath(wstring& path, wchar_t separator)
{
	if (separator == L'\0')
		separator = filesystem::path::preferred_separator;

	wstring result = path;

	// Important note: std::filesystem::path::make_preferred() does not convert windows separators to linux preferred so we have to care about that (if configs are created with backslashes)
	replace(result.begin(), result.end(), L'/', separator);
	replace(result.begin(), result.end(), L'\\', separator);

	return result;
}

wstring FileHelper::NormalizePath(wstring& path)
{
	static wchar_t preferred_separator = filesystem::path::preferred_separator;

	wstring result = NormalizePath(path, preferred_separator);

	return result;
}

wstring FileHelper::PathCombine(wstring& path1, wstring& path2)
{
	filesystem::path basePath = path1;
	basePath = filesystem::weakly_canonical(basePath);	// Remove trailing path separator if exists
	basePath /= path2;									// Re-add trailing path separator

	wstring result = basePath.wstring();
	result = NormalizePath(result);

	return result;
}

wstring FileHelper::PathCombine(wstring& path1, const char* path2)
{
	string strPath2 = path2;
	wstring pathPart2 = StringHelper::StringToWideString(strPath2);
	return PathCombine(path1, pathPart2);
}

wstring FileHelper::PathCombine(wstring& path1, const wchar_t* path2)
{
	wstring pathPart2 = path2;
	return PathCombine(path1, pathPart2);
}

bool FileHelper::IsFile(wstring& path)
{
	bool result = FileExists(path);

	return result;
}

bool FileHelper::IsFolder(wstring& path)
{
	bool result = FolderExists(path);

	return result;
}

bool FileHelper::FileExists(wstring& path)
{
	bool result = false;

	if (filesystem::exists(path) && filesystem::is_regular_file(path))
	{
		result = true;
	}

	return result;
}

bool FileHelper::FolderExists(wstring& path)
{
	bool result = false;

	if (filesystem::exists(path) && filesystem::is_directory(path))
	{
		result = true;
	}

	return result;
}

string FileHelper::PrintablePath(wstring wpath)
{
	string result = StringHelper::WideStringToString(wpath);

	return result;
}