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

    static string ReplaceAll(std::string& str, const std::string& from, const std::string& to);
    static std::wstring ReplaceAll(std::wstring& wstr, std::wstring wfrom, std::wstring wto);

    static std::string_view LTrim(std::string_view str);
    static std::string_view RTrim(std::string_view str);
    static std::string_view Trim(std::string_view str);

    static std::string ToUpper(const std::string& str);
    static std::string ToLower(const std::string& str);

    template <typename T>
    static std::string ToHex(T n, bool upperCase = false)
    {
        std::stringstream ss;

        ss << std::setfill('0') << std::setw(sizeof(T) * 2) << std::hex;

        if (upperCase)
        {
            ss << std::uppercase;
        }

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
    static std::string ToHexWithPrefix(T n, const char* prefix = "0x", bool upperCase = true)
    {
        std::stringstream ss;

        ss << prefix << ToHex(n, upperCase);

        std::string result = ss.str();

        return result;
    }

    static std::string FormatWithThousandsDelimiter(int64_t n);
    static std::string FormatWithCustomThousandsDelimiter(int64_t n, char delimiter = ',');

    /// Format any integer number to bit quads
    /// int8  => b'0110'1111
    /// int16 => b'0110'1111'0000'1111
    /// @tparam T
    /// @param n
    /// @return
    /// @example std::string value = StringHelper::FormatBinary<uint8_t>(0xFF);
    template <typename T>
    static std::string FormatBinary(T n)
    {
        size_t size = sizeof(n) * 8;

        std::stringstream ss;

        ss << "b";

        for (unsigned i = 0; i < size; i++)
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

    /// region <Format>
public:
    /// Format string using std::string primitives only
    /// @tparam Args Variadic argument(s)
    /// @param format Format string as std::string object reference
    /// @param args Format arguments
    /// @return Formatted string as std::string object
    template<typename ... Args>
    static std::string Format(const std::string& format, Args ... args)
    {
        //std::cout << "Format called with format string: " << format << std::endl;

        return Format_Impl(format, ConvertArg(args)...);
    }

private:
    // Helper function to convert std::string to const char*
    static const char* ConvertArg(const std::string& s)
    {
        //std::cout << "Converting std::string to const char*: " << s << std::endl;

        return s.c_str();
    }

    // Pass-through for all other types
    template<typename T>
    static typename std::enable_if<!std::is_same<T, std::string>::value, T>::type
    ConvertArg(T arg)
    {
        /*
        if constexpr (std::is_same<T, int>::value)
            std::cout << "Integer passed through: " << arg << std::endl;
        else if constexpr (std::is_same<T, float>::value || std::is_same<T, double>::value)
            std::cout << "Float/double passed through: " << arg << std::endl;
        else if constexpr (std::is_same<T, const char*>::value)
            std::cout << "const char* passed through: " << arg << std::endl;
        else
            std::cout << "Other type passed through" << std::endl;
        */

        return arg;
    }

    /// Format string using std::string primitives only
    /// @tparam Args Variadic argument(s)
    /// @param format Format string as std::string object reference
    /// @param args Format arguments
    /// @return Formatted string as std::string object
    template<typename... Args>
    static std::string Format_Impl(const std::string& format, Args... args)
    {
        // Early return for empty format string
        if (format.empty()) {
            return "";
        }

        // Handle case with no arguments (just return the format string as-is)
        if constexpr (sizeof...(Args) == 0) {
            return format;
        }

        std::string result;

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wformat-nonliteral"
    #pragma clang diagnostic ignored "-Wformat-security"
#elif defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-nonliteral"
    #pragma GCC diagnostic ignored "-Wformat-security"
#endif

        // Calculate required buffer size with actual arguments
        size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
        if (size <= 1) {  // size <= 1 means either error or empty result
            return format;
        }

        try {
            std::unique_ptr<char[]> buf(new char[size]);
            int written = snprintf(buf.get(), size, format.c_str(), args...);

            if (written < 0 || static_cast<size_t>(written) >= size) {
                // Formatting error occurred
                return format;
            }

            result.assign(buf.get(), written);
        }
        catch (...) {
            // Memory allocation failed
            return format;
        }

#if defined(__clang__)
    #pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
    #pragma GCC diagnostic pop
#endif

        return result;
    }
    /// endregion </Format>
};