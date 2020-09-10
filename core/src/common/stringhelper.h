#pragma once
#include "stdafx.h"

#include <string>

using namespace std;

/// region <Helper fprmatters>
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
	static int Compare(wstring& wstr1, wstring& wstr2);
    static int Compare(string& str1, string& wstr2);
	static int CompareCaseInsensitive(const char* str1, const char* str2, size_t len);

	static wstring StringToWideString(const string& str);
	static string WideStringToString(const wstring& wideString);

	string ReplaceAll(string& str, string from, string to);
	static wstring ReplaceAll(wstring& wstr, wstring wfrom, wstring wto);

    static string_view LTrim(string_view str);
    static string_view RTrim(string_view str);
    static string_view Trim(string_view str);

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
            std::unique_ptr<char[]> buf(new char[ size ]);
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