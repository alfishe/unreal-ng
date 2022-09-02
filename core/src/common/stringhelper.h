#pragma once
#include "stdafx.h"

#include <string>

/// region <Helper formatters>
class ThousandsDelimiterPunct : public std::numpunct<char>
{
protected:
    virtual char do_thousands_sep() const { return ','; }
    virtual std::string do_grouping() const { return "\03"; }
};
/// endregion </Helper formatters>

class StringHelper
{
public:
	static uint8_t Hex(uint8_t val);
	static bool IsHex(uint8_t val);
	static int Compare(std::wstring& wstr1, std::wstring& wstr2);
    static int Compare(std::string& str1, std::string& wstr2);
	static int CompareCaseInsensitive(const char* str1, const char* str2, size_t len);

	static std::wstring StringToWideString(const std::string& str);
	static std::string WideStringToString(const std::wstring& wideString);

	string ReplaceAll(std::string& str, std::string from, std::string to);
	static std::wstring ReplaceAll(std::wstring& wstr, std::wstring wfrom, std::wstring wto);

    static std::string_view LTrim(std::string_view str);
    static std::string_view RTrim(std::string_view str);
    static std::string_view Trim(std::string_view str);

    static std::string ToUpper(const std::string& str);
    static std::string ToLower(const std::string& str);

    template <typename T>
    static std::string ToHex(T n)
    {
        std::stringstream ss;

        ss << std::setfill ('0') << std::setw(sizeof(T) * 2) << std::hex;

        // char, unsigned char and derived types (like int8_t and uint8_t are not by default treated as numbers by stringstream
        if (typeid(T) == typeid(char) || typeid(T) == typeid(unsigned char) || sizeof(T) == 1)
        {
            ss << static_cast<int>(n & 0xFF);
        }
        else
        {
            ss << n;
        }

        std::string result = ss.str();

        return result;
    }

    template <typename T>
    static std::string ToHexWithPrefix(T n, const char* prefix = "0x")
    {
        std::stringstream ss;

        ss << prefix << ToHex(n);

        std::string result = ss.str();

        return result;
    }

    /// Format string using std::string primitives only
    /// @tparam Args Variadic argument(s)
    /// @param format Format string as std::string object reference
    /// @param args Format arguments
    /// @return Formatted string as std::string object
    template<typename ... Args>
    static std::string Format(const std::string& format, Args ... args)
    {
        std::string result;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#pragma clang diagnostic ignored "-Wformat-security"

        // Pre-calculate resulting buffer size (adding +1 byte for trailing '\0')
        size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1;
        if (size > 0)
        {
            std::unique_ptr<char[]> buf(new char[size]);
            snprintf(buf.get(), size, format.c_str(), args ...);

            // Create output string (without trailing '\0')
            result = std::string(buf.get(),buf.get() + size - 1);
        }

#pragma clang diagnostic pop

        return result;
    }

    static std::string FormatWithThousandsDelimiter(int64_t n);

    /// Format any integer number to bit quads
    /// int8  => b'0110'1111
    /// int16 => b'0110'1111'0000'1111
    /// \tparam T
    /// \param n
    /// \return
    template <typename T>
    static std::string FormatBinary(T n)
    {
        size_t size = sizeof(n) * 8;

        std::stringstream ss;

        ss << "b";

        for (int i = 0; i < size; i++)
        {
            if ((i % 4) == 0)
            {
                ss << '\'';
            }

            if ((n >> (size - 1 - i)) & 1)
            {
                ss << '1';
            }
            else
            {
                ss << '0';
            }
        }

        std::string result = ss.str();

        return result;
    }
};