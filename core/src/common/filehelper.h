#pragma once
#ifndef _INCLUDED_FILEHELPER_H_
#define _INCLUDED_FILEHELPER_H_

#include "stdafx.h"

#include <filesystem>
#include <string>

/// Path and file helpers.
///
/// Encoding: every std::string path is UTF-8 on every platform. On Windows FileHelper converts to UTF-16 at the
/// Win32/CRT boundary (so non-ASCII paths work regardless of the ANSI code page); on POSIX UTF-8 is native.
/// Use ToFsPath() whenever a std::filesystem::path has to be built from a UTF-8 string elsewhere.
///
/// All path-manipulating functions accept any mix of '/' and '\' separators on every platform and
/// understand every path shape the host OS does, without ever corrupting it:
///   - Windows drive paths          C:\dir\file.ext        C:/dir/file.ext       C:relative
///   - Windows rooted (no drive)    \dir\file.ext          /dir/file.ext
///   - UNC / network shares         \\server\share\file    //server/share/file   //172.16.17.10/Macintosh HD/x
///   - Win32 device / long paths    \\?\C:\dir\file        \\?\UNC\server\share\file
///   - POSIX absolute               /opt/dir/file          //host/dir (leading double slash preserved)
///   - Relative                     dir/file               ./file    ../file
///   - Home-relative                ~    ~/file    ~\file
/// Results always use the platform's native separator (see GetPathSeparator()).
class FileHelper
{
public:
    static char GetPathSeparator();

    /// std::filesystem::path for a UTF-8 string (wide on Windows, as-is on POSIX). Separators are left untouched.
    static std::filesystem::path ToFsPath(const std::string& utf8Path);

    static std::string GetExecutablePath();
    static std::string GetResourcesPath();

    /// Expand a leading "~" ("~", "~/x", "~\x") to the user's home directory. Everything else is returned untouched.
    static std::string ExpandPath(const std::string& path);

    /// Replace every '/' and '\' with the given separator ('\0' = native). Pure character substitution.
    static std::string NormalizePath(const std::string& path, char separator);
    static std::string NormalizePath(const std::string& path);

    /// Lexical normalization with native separators: collapses repeated separators, removes "." components,
    /// resolves ".." components (never above the root) and keeps the root (drive letter, UNC \\server\share,
    /// \\?\ prefix, leading "/" or "//") intact. No filesystem access, no corruption of unknown shapes.
    static std::string LexicallyNormalPath(const std::string& path);

    /// True for paths that do not depend on the current directory:
    /// Windows - "X:\...", "X:/...", "\\server\...", "//server/..."; POSIX - anything starting with '/'.
    static bool IsAbsolutePath(const std::string& path);

    /// True for network-share style paths: two leading separators followed by a non-separator
    /// ("\\server\share", "//server/share", "\\?\...", ...). Purely syntactic, works on every platform.
    static bool IsUNCPath(const std::string& path);

    /// Resolve a path to an absolute, lexically normalized, native-separator form.
    /// Relative paths are resolved against the current directory, "~" is expanded.
    /// When resolveSymlinks is true and the path exists, symlinks are resolved and (on Windows) the on-disk
    /// letter case is applied. UNC paths are never rewritten into drive paths or stripped of their "\\" prefix.
    static std::string AbsolutePath(const std::string& path, bool resolveSymlinks = true);

    /// Join two path fragments with exactly one native separator between them (separators normalized).
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
    FileHelper();  // Disable direct object creation by making constructor private. Only static method calls allowed.
};

#endif  // _INCLUDED_FILEHELPER_H_
