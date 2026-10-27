#pragma once

// Standard C++ stand-ins for the emulator's FileHelper, StringHelper and DumpHelper, under the same names inside
// namespace unrealdisk, so the format drivers taken over from the emulator read as they did there. Paths are UTF-8.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <type_traits>
#include <vector>

namespace unrealdisk
{
struct StringHelper
{
    /// printf-style formatting; std::string arguments are passed as C strings
    template <typename... Args>
    static std::string Format(const std::string& format, Args... args)
    {
        return Formatted(format, Convert(args)...);
    }

    static std::string ToLower(const std::string& text)
    {
        std::string out = text;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

private:
    static const char* Convert(const std::string& s) { return s.c_str(); }
    template <typename T>
    static typename std::enable_if<!std::is_same<T, std::string>::value, T>::type Convert(T value)
    {
        return value;
    }
    template <typename... Args>
    static std::string Formatted(const std::string& format, Args... args)
    {
        if constexpr (sizeof...(Args) == 0)
            return format;
        else
        {
            const int size = std::snprintf(nullptr, 0, format.c_str(), args...);
            if (size <= 0)
                return std::string();
            std::string out(static_cast<size_t>(size) + 1, '\0');
            std::snprintf(out.data(), out.size(), format.c_str(), args...);
            out.resize(static_cast<size_t>(size));
            return out;
        }
    }
};

struct FileHelper
{
    static std::filesystem::path Path(const std::string& utf8)
    {
        return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
    }

    static bool FileExists(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::is_regular_file(Path(path), ec) && !ec;
    }

    static size_t GetFileSize(const std::string& path)
    {
        std::error_code ec;
        const auto size = std::filesystem::file_size(Path(path), ec);
        return ec ? 0 : static_cast<size_t>(size);
    }

    /// The last component's extension without the dot ("" for none or a dot-file)
    static std::string GetFileExtension(const std::string& path)
    {
        const size_t sep = path.find_last_of("/\\");
        const std::string name = sep == std::string::npos ? path : path.substr(sep + 1);
        if (name == "." || name == "..")
            return std::string();
        const size_t dot = name.find_last_of('.');
        return dot == std::string::npos || dot == 0 ? std::string() : name.substr(dot + 1);
    }

    /// Up to `size` bytes of the file into `buffer`; the count read
    static size_t ReadFileToBuffer(const std::string& path, uint8_t* buffer, size_t size)
    {
        std::ifstream in(Path(path), std::ios::binary);
        if (!in)
            return 0;
        in.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
        return static_cast<size_t>(in.gcount());
    }

    static bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
    {
        std::ifstream in(Path(path), std::ios::binary);
        if (!in)
            return false;
        out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        return true;
    }

    /// A C stream on a UTF-8 path (Windows: _wfopen, as the narrow fopen reads the ANSI code page)
    static FILE* OpenFile(const std::string& path, const char* mode = "rb")
    {
#ifdef _WIN32
        const std::string m = mode ? mode : "rb";
        return _wfopen(Path(path).c_str(), std::wstring(m.begin(), m.end()).c_str());
#else
        return std::fopen(path.c_str(), mode ? mode : "rb");
#endif
    }

    static void CloseFile(FILE* file)
    {
        if (file)
            std::fclose(file);
    }

    static bool SaveBufferToFile(FILE* file, const uint8_t* buffer, size_t size)
    {
        return file && std::fwrite(buffer, 1, size, file) == size;
    }

    /// The whole file written (replaced); false when it cannot be
    static bool WriteFile(const std::string& path, const uint8_t* data, size_t size)
    {
        std::ofstream out(Path(path), std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        return static_cast<bool>(out);
    }
};

struct DumpHelper
{
    /// Bytes as hex, 16 per line
    static std::string HexDumpBuffer(const uint8_t* buffer, size_t size, const std::string& delimiter = " ", const std::string& prefix = "")
    {
        static const char digits[] = "0123456789ABCDEF";
        std::string out;
        for (size_t i = 0; i < size; i++)
        {
            if (i % 16 == 0)
                out += (i ? "\n" : "") + prefix;
            else
                out += delimiter;
            out += digits[buffer[i] >> 4];
            out += digits[buffer[i] & 0x0F];
        }
        return out;
    }
};
}  // namespace unrealdisk
