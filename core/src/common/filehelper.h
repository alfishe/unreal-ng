#pragma once
#ifndef _INCLUDED_FILEHELPER_H_
#define _INCLUDED_FILEHELPER_H_

#include "stdafx.h"

#include <string>

class FileHelper
{
public:
    static char GetPathSeparator();

    static std::string GetExecutablePath();
    static std::string GetResourcesPath();
    static std::string NormalizePath(const std::string& path, char separator);
    static std::string NormalizePath(const std::string& path);
    static std::string AbsolutePath(const std::string& path, bool resolveSymlinks = false);
    static std::string PathCombine(const std::string& path1, const std::string& path2);
    static std::string PathCombine(const std::string& path1, const char* path2);

    static bool IsFile(const std::string& path);
    static bool IsFolder(const std::string& path);
    static bool FileExists(const std::string& path);
    static bool FolderExists(const std::string& path);

    static size_t GetFileSize(const std::string& path);
    static size_t GetFileSize(FILE* file);
    static std::string GetFileExtension(const std::string& path);

    static std::string PrintablePath(const std::string& path);

    static FILE* OpenExistingFile(const std::string& path, const char* mode = "rb");
    static FILE* OpenFile(const std::string& path, const char* mode = "rb");
    static void CloseFile(FILE* file);

    static size_t ReadFileToBuffer(FILE* file, uint8_t* buffer, size_t size);
    static size_t ReadFileToBuffer(const std::string& filePath, uint8_t* buffer, size_t size);
    static bool SaveBufferToFile(FILE* file, uint8_t* buffer, size_t size);
    static bool SaveBufferToFile(const std::string& filePath, uint8_t* buffer, size_t size);

private:
	FileHelper();	// Disable direct object creation by making constructor private. Only static method calls allowed.
};

#endif // _INCLUDED_FILEHELPER_H_