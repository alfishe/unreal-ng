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

    /// @brief Convert an integer to a hex string (zero-padded, no prefix).
    /// Fast path for 8/16/32/64-bit integers, falls back to stringstream for other types.
    /// @tparam T Integer type (uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, etc.)
    /// @param n Value to convert.
    /// @param upperCase If true, use uppercase hex digits (default false).
    /// @return std::string Hex string (zero-padded, no prefix).
    template <typename T>
    static std::string ToHex(T n, bool upperCase = false)
    {
        const char* hex = upperCase ? "0123456789ABCDEF" : "0123456789abcdef";
        if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
        {
            char buf[3];
            uint8_t val = static_cast<uint8_t>(n);
            buf[0] = hex[(val >> 4) & 0xF];
            buf[1] = hex[val & 0xF];
            buf[2] = 0;
            return std::string(buf);
        }
        else if constexpr (std::is_same_v<T, uint16_t> || std::is_same_v<T, int16_t>)
        {
            char buf[5];
            uint16_t val = static_cast<uint16_t>(n);
            buf[0] = hex[(val >> 12) & 0xF];
            buf[1] = hex[(val >> 8) & 0xF];
            buf[2] = hex[(val >> 4) & 0xF];
            buf[3] = hex[val & 0xF];
            buf[4] = 0;
            return std::string(buf);
        }
        else if constexpr (std::is_same_v<T, uint32_t> || std::is_same_v<T, int32_t>)
        {
            char buf[9];
            uint32_t val = static_cast<uint32_t>(n);
            buf[0] = hex[(val >> 28) & 0xF];
            buf[1] = hex[(val >> 24) & 0xF];
            buf[2] = hex[(val >> 20) & 0xF];
            buf[3] = hex[(val >> 16) & 0xF];
            buf[4] = hex[(val >> 12) & 0xF];
            buf[5] = hex[(val >> 8) & 0xF];
            buf[6] = hex[(val >> 4) & 0xF];
            buf[7] = hex[val & 0xF];
            buf[8] = 0;
            return std::string(buf);
        }
        else if constexpr (std::is_same_v<T, uint64_t> || std::is_same_v<T, int64_t>)
        {
            char buf[17];
            uint64_t val = static_cast<uint64_t>(n);
            buf[0]  = hex[(val >> 60) & 0xF];
            buf[1]  = hex[(val >> 56) & 0xF];
            buf[2]  = hex[(val >> 52) & 0xF];
            buf[3]  = hex[(val >> 48) & 0xF];
            buf[4]  = hex[(val >> 44) & 0xF];
            buf[5]  = hex[(val >> 40) & 0xF];
            buf[6]  = hex[(val >> 36) & 0xF];
            buf[7]  = hex[(val >> 32) & 0xF];
            buf[8]  = hex[(val >> 28) & 0xF];
            buf[9]  = hex[(val >> 24) & 0xF];
            buf[10] = hex[(val >> 20) & 0xF];
            buf[11] = hex[(val >> 16) & 0xF];
            buf[12] = hex[(val >> 12) & 0xF];
            buf[13] = hex[(val >> 8) & 0xF];
            buf[14] = hex[(val >> 4) & 0xF];
            buf[15] = hex[val & 0xF];
            buf[16] = 0;
            return std::string(buf);
        }
        else
        {
            // Slow but universal fallback
            std::stringstream ss;
            ss << std::setfill('0') << std::setw(sizeof(T) * 2) << std::hex;
            if (upperCase)
                ss << std::uppercase;
            ss << static_cast<typename std::make_unsigned<T>::type>(n);
            return ss.str();
        }
    }

    /// @brief Convert an integer to a hex string with a prefix (e.g., "#FF", "#1234").
    ///
    /// Fast path for 8/16/32/64-bit integers, falls back to stringstream for other types.
    ///
    /// @tparam T Integer type (uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, etc.)
    /// @param n Value to convert.
    /// @param prefix Prefix string (default "#").
    /// @param upperCase If true, use uppercase hex digits (default true).
    /// @return std::string Hex string with prefix.
    template <typename T>
    static std::string ToHexWithPrefix(T n, const char* prefix = "0x", bool upperCase = true)
    {
        // Use stringstream to properly handle multi-character prefixes
        // This ensures prefixes like "0x", "hex:", "addr:" work correctly
        std::stringstream ss;
        ss << prefix << ToHex(n, upperCase);
        return ss.str();
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
        // std::cout << "Converting std::string to const char*: " << s << std::endl;

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
        catch (...)
        {
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