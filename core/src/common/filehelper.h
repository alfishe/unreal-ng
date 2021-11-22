#pragma once
#ifndef _INCLUDED_FILEHELPER_H_
#define _INCLUDED_FILEHELPER_H_

#include "stdafx.h"

#include "3rdparty/pathie-cpp/path.hpp"
#include <string>

using namespace std;
using namespace std::string_literals; // Enables s-suffix for std::string literals


class FileHelper
{
public:
    static const char GetPathSeparator();

	static std::string GetExecutablePath();
	static std::string NormalizePath(std::string& path, char separator);
	static std::string NormalizePath(std::string& path);
    static std::string AbsolutePath(std::string& path);
	static std::string PathCombine(std::string& path1, string& path2);
	static std::string PathCombine(std::string& path1, const char* path2);

	static bool IsFile(std::string& path);
	static bool IsFolder(std::string& path);
	static bool FileExists(std::string& path);
	static bool FolderExists(std::string& path);

	static size_t GetFileSize(std::string& path);
    static size_t GetFileSize(FILE* file);

	static std::string PrintablePath(std::string path);

	static FILE* OpenFile(std::string& path, const char* mode = "rb");
	static void CloseFile(FILE* file);
	static size_t ReadFileToBuffer(FILE* file, uint8_t* buffer, size_t size);

	static bool SaveBufferToFile(std::string& filePath, uint8_t* buffer, size_t size);

private:
	FileHelper();	// Disable direct object creation by making constructor private. Only static method calls allowed.
};

#endif // _INCLUDED_FILEHELPER_H_