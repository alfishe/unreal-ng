#pragma once
#include "stdafx.h"

#include <filesystem>
#include <string>

using namespace std;
using namespace std::filesystem;

class FileHelper
{
public:
	static wstring GetExecutablePath();
	static wstring PathCombine(wstring& path1, wstring& path2);
	static wstring PathCombine(wstring& path1, const char* path2);

	static bool IsFile(wstring& path);
	static bool IsFolder(wstring& path);
	static bool FileExists(wstring& path);
	static bool FolderExists(wstring& path);

	static string NormalizeFilePath(wstring wpath);

private:
	FileHelper();	// Disable direct object creation by making constructor private. Only static method calls allowed.
};