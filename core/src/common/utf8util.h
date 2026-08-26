#pragma once

#include <filesystem>
#include <fstream>
#include <string>

/// UTF-8 aware helpers.
///
/// Every std::string path in the code base is UTF-8 (that is what Qt, Lua, Python and the web API hand over).
/// On Windows the narrow CRT/Win32 APIs interpret char strings in the ANSI code page, so anything non-ASCII
/// must cross to UTF-16 at the OS boundary. The stream wrappers below do that for iostreams; FileHelper does
/// it for FILE*/Win32. On POSIX UTF-8 is native and the wrappers are plain pass-throughs.
namespace utf8
{

class utf8
{
public:
	static std::wstring decode(const std::string &str);   // UTF-8 -> wide (UTF-16 on Windows, UTF-32 elsewhere)
	static std::string encode(const std::wstring &wstr);  // wide -> UTF-8

	/// std::filesystem::path for a UTF-8 string, correct on every platform/compiler
	static std::filesystem::path path(const std::string& utf8Path)
	{
#ifdef _WIN32
		return std::filesystem::path(decode(utf8Path));
#else
		return std::filesystem::path(utf8Path);
#endif
	}
};

class ifstream : public std::ifstream
{
public:
	ifstream() : std::ifstream() {}
	explicit ifstream(const std::string& utf8Path, std::ios_base::openmode mode = std::ios_base::in)
		: std::ifstream(utf8::path(utf8Path), mode) {}
	void open(const std::string& utf8Path, std::ios_base::openmode mode = std::ios_base::in)
	{
		std::ifstream::open(utf8::path(utf8Path), mode);
	}
};

class ofstream : public std::ofstream
{
public:
	ofstream() : std::ofstream() {}
	explicit ofstream(const std::string& utf8Path, std::ios_base::openmode mode = std::ios_base::out)
		: std::ofstream(utf8::path(utf8Path), mode) {}
	void open(const std::string& utf8Path, std::ios_base::openmode mode = std::ios_base::out)
	{
		std::ofstream::open(utf8::path(utf8Path), mode);
	}
};

} // namespace utf8
