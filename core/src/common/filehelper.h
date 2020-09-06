#pragma once
#ifndef _INCLUDED_FILEHELPER_H_
#define _INCLUDED_FILEHELPER_H_

#include "stdafx.h"

#include <filesystem>
#include <string>

using namespace std;
using namespace std::string_literals; // Enables s-suffix for std::string literals


class FileHelper
{
public:
    static char GetPathSeparator();

	static string GetExecutablePath();
	static string NormalizePath(string& path, char separator);
	static string NormalizePath(string& path);
	static string PathCombine(string& path1, string& path2);
	static string PathCombine(string& path1, const char* path2);

	static bool IsFile(string& path);
	static bool IsFolder(string& path);
	static bool FileExists(string& path);
	static bool FolderExists(string& path);

	static string PrintablePath(string wpath);

	static bool SaveBufferToFile(string& filePath, uint8_t* buffer, size_t size);

private:
	FileHelper();	// Disable direct object creation by making constructor private. Only static method calls allowed.
};

#endif // _INCLUDED_FILEHELPER_H_