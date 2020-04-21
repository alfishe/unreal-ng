#pragma once
#ifndef _INCLUDED_FILEHELPER_H_
#define _INCLUDED_FILEHELPER_H_

#include "stdafx.h"

#include <filesystem>
#include <string>

using namespace std;
using namespace std::filesystem;
using namespace std::string_literals; // Enables s-suffix for std::string literals


class FileHelper
{
public:
	static wstring GetExecutablePath();
	static wstring NormalizePath(wstring& path, wchar_t separator);
	static wstring NormalizePath(wstring& path);
	static wstring PathCombine(wstring& path1, wstring& path2);
	static wstring PathCombine(wstring& path1, const char* path2);
	static wstring PathCombine(wstring& path1, const wchar_t* path2);

	static bool IsFile(wstring& path);
	static bool IsFolder(wstring& path);
	static bool FileExists(wstring& path);
	static bool FolderExists(wstring& path);

	static string PrintablePath(wstring wpath);

private:
	FileHelper();	// Disable direct object creation by making constructor private. Only static method calls allowed.
};

#endif // _INCLUDED_FILEHELPER_H_