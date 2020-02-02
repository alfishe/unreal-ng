#include "stdafx.h"
#include "common/logger.h"

#include "filehelper.h"
#include "common/stringhelper.h"

wstring FileHelper::GetExecutablePath()
{
	wstring result = filesystem::current_path().wstring();

	#ifdef _WIN32
		wchar_t buffer[MAX_PATH] = { L'\0' };
		GetModuleFileNameW(NULL, buffer, MAX_PATH);
		result = buffer;
	#else
		char buffer[PATH_MAX];
		ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
		buffer[count] = '\0';
		result = StringHelper::StringToWideString(buffer);
	#endif

	filesystem::path basePath = filesystem::canonical(result);
	result = basePath.parent_path().wstring();

	return result;
}

wstring FileHelper::PathCombine(wstring& path1, wstring& path2)
{
	filesystem::path basePath = path1;
	basePath = filesystem::canonical(basePath);	// Remove trailing path separator if exists
	basePath /= path2;							// Re-add trailing path separator

	wstring result = basePath.wstring();

	return result;
}

wstring FileHelper::PathCombine(wstring& path1, const char* path2)
{
	string strPath2 = path2;
	wstring pathPart2 = StringHelper::StringToWideString(strPath2);
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

string FileHelper::NormalizeFilePath(wstring wpath)
{
	string result = StringHelper::WideStringToString(wpath);

	return result;
}