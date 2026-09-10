#include "stringhelper.h"

#include <algorithm>
#include <cassert>

#include "stdafx.h"


uint8_t StringHelper::Hex(uint8_t val)
{
    uint8_t result = tolower(val);
    return (result < 'a') ? result - '0' : result - 'a' + 10;
}

bool StringHelper::IsHex(uint8_t val)
{
    return (isdigit(val) || (tolower(val) >= 'a' && tolower(val) <= 'f'));
}

int StringHelper::Compare(std::wstring& wstr1, std::wstring& wstr2)
{
    int result = -1;

    size_t len1 = wstr1.length();
    size_t len2 = wstr2.length();

    if (len1 == len2)
    {
        wchar_t* ptr1 = (wchar_t*)wstr1.c_str();
        wchar_t* ptr2 = (wchar_t*)wstr2.c_str();

        do
        {
            if (!(*ptr1 && *ptr2))
                break;

            if (*ptr1 != *ptr2)
                break;
        } while (len1-- && *ptr1++ && *ptr2++);

        result = *ptr1 - *ptr2;
    }
    else
    {
        result = (int)(len1 - len2);
    }

    return result;
}

int StringHelper::Compare(string& str1, string& str2)
{
    int result = -1;

    size_t len1 = str1.length();
    size_t len2 = str2.length();

    if (len1 == len2)
    {
        char* ptr1 = (char*)str1.c_str();
        char* ptr2 = (char*)str2.c_str();

        do
        {
            if (!(*ptr1 && *ptr2))
                break;

            if (*ptr1 != *ptr2)
                break;
        } while (len1-- && *ptr1++ && *ptr2++);

        result = *ptr1 - *ptr2;
    }
    else
    {
        result = (int)(len1 - len2);
    }

    return result;
}

int StringHelper::CompareCaseInsensitive(const char* str1, const char* str2, size_t len)
{
    int result = -1;

    if (str1 != nullptr && str2 != nullptr && len > 0)
    {
        char* ptr1 = (char*)str1;
        char* ptr2 = (char*)str2;

        do
        {
            if (!(*ptr1 && *ptr2))
                break;

            if (toupper(*ptr1) != toupper(*ptr2))
                break;
        } while (len-- && *ptr1++ && *ptr2++);

        result = *ptr1 - *ptr2;
    }

    return result;
}

/// UTF-8 -> wide. wchar_t is UTF-16 on Windows (surrogate pairs for U+10000..U+10FFFF) and UTF-32 elsewhere.
/// Malformed input bytes are mapped to U+FFFD so the conversion is total and never throws.
std::wstring StringHelper::StringToWideString(const std::string& str)
{
    std::wstring result;
    result.reserve(str.size());

    const unsigned char* s = reinterpret_cast<const unsigned char*>(str.data());
    const size_t n = str.size();
    size_t i = 0;

    while (i < n)
    {
        uint32_t cp;
        size_t extra;
        unsigned char c = s[i];

        if (c < 0x80)            { cp = c;        extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else                     { cp = 0xFFFD;   extra = 0; }  // stray continuation / invalid lead byte

        // Consume the continuation bytes that are actually present ("maximal subpart" rule: a truncated or
        // broken sequence becomes exactly one U+FFFD and decoding resumes at the first non-continuation byte)
        size_t got = 0;
        while (got < extra && i + 1 + got < n && (s[i + 1 + got] & 0xC0) == 0x80)
        {
            cp = (cp << 6) | (s[i + 1 + got] & 0x3F);
            got++;
        }

        if (got != extra)
        {
            cp = 0xFFFD;
            extra = got;
        }
        else if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
                 cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            cp = 0xFFFD;  // overlong form, out of range or encoded surrogate
        }

        i += 1 + extra;

        if (sizeof(wchar_t) == 2 && cp >= 0x10000)
        {
            cp -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 | (cp >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 | (cp & 0x3FF)));
        }
        else
        {
            result.push_back(static_cast<wchar_t>(cp));
        }
    }

    return result;
}

/// Wide -> UTF-8 (inverse of StringToWideString). Unpaired surrogates become U+FFFD.
std::string StringHelper::WideStringToString(const std::wstring& wstr)
{
    std::string result;
    result.reserve(wstr.size() * 3);

    const size_t n = wstr.size();
    for (size_t i = 0; i < n; i++)
    {
        uint32_t cp = static_cast<uint32_t>(wstr[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF)
        {
            // High surrogate - must be followed by a low surrogate
            if (i + 1 < n && static_cast<uint32_t>(wstr[i + 1]) >= 0xDC00 && static_cast<uint32_t>(wstr[i + 1]) <= 0xDFFF)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<uint32_t>(wstr[i + 1]) - 0xDC00);
                i++;
            }
            else
            {
                cp = 0xFFFD;
            }
        }
        else if (cp >= 0xD800 && cp <= 0xDFFF)
        {
            cp = 0xFFFD;  // lone low surrogate (or encoded surrogate on UTF-32 platforms)
        }
        else if (cp > 0x10FFFF)
        {
            cp = 0xFFFD;
        }

        if (cp < 0x80)
        {
            result.push_back(static_cast<char>(cp));
        }
        else if (cp < 0x800)
        {
            result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    return result;
}

std::string StringHelper::ReplaceAll(std::string& str, const std::string& from, const std::string& to)
{
    if (!from.empty())
    {
        std::string result;
        size_t pos = 0;
        size_t fromLen = from.length();
        size_t len = str.length();

        while (pos < len)
        {
            size_t matchPos = str.find(from, pos);
            if (matchPos == std::string::npos)
            {
                // No more matches, add the rest of the string
                result += str.substr(pos);
                break;
            }

            // Add the part before the match
            result += str.substr(pos, matchPos - pos);
            // Add the replacement
            result += to;
            // Move past this match
            pos = matchPos + fromLen;
        }

        // Update original string
        str = result;
    }

    return str;
}

std::wstring StringHelper::ReplaceAll(std::wstring& wstr, std::wstring wfrom, std::wstring wto)
{
    if (!wfrom.empty())
    {
        std::wstring result;
        size_t pos = 0;
        size_t fromLen = wfrom.length();
        size_t len = wstr.length();

        while (pos < len)
        {
            size_t matchPos = wstr.find(wfrom, pos);
            if (matchPos == std::wstring::npos)
            {
                // No more matches, add the rest of the string
                result += wstr.substr(pos);
                break;
            }

            // Add the part before the match
            result += wstr.substr(pos, matchPos - pos);
            // Add the replacement
            result += wto;
            // Move past this match
            pos = matchPos + fromLen;
        }

        // Update original string
        wstr = result;
    }

    return wstr;
}

std::string_view StringHelper::LTrim(std::string_view str)
{
    str.remove_prefix(
        std::distance(str.cbegin(), std::find_if(str.cbegin(), str.cend(), [](int c) { return !std::isspace(c); })));

    return str;
}

std::string_view StringHelper::RTrim(std::string_view str)
{
    str.remove_suffix(
        std::distance(str.crbegin(), std::find_if(str.crbegin(), str.crend(), [](int c) { return !std::isspace(c); })));

    return str;
}

std::string_view StringHelper::Trim(std::string_view str)
{
    return LTrim(RTrim(str));
}

std::string StringHelper::ToUpper(const string& str)
{
    std::string result;
    std::stringstream ss;

    for (std::string::size_type i = 0; i < str.length(); i++)
    {
        ss << static_cast<char>(std::toupper(str[i]));
    }

    result = ss.str();

    /* In-place conversion
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c){ return std::toupper(c); }
    );
    */

    return result;
}

std::string StringHelper::ToLower(const string& str)
{
    std::string result;
    std::stringstream ss;

    for (std::string::size_type i = 0; i < str.length(); i++)
    {
        ss << static_cast<char>(std::tolower(str[i]));
    }

    result = ss.str();

    /* In-place conversion
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c){ return std::tolower(c); }
    );
    */

    return result;
}

std::string StringHelper::FormatWithThousandsDelimiter(int64_t n)
{
    std::stringstream ss;
    ss.imbue(std::locale(std::locale::classic(), new ThousandsDelimiterPunct));
    ss << n;

    std::string result = ss.str();

    return result;
}

std::string StringHelper::FormatWithCustomThousandsDelimiter(int64_t n, char delimiter)
{
    // Create a custom locale with the specified thousands separator
    struct CustomDelimiterPunct : std::numpunct<char>
    {
        char m_delimiter;
        CustomDelimiterPunct(char delim) : m_delimiter(delim) {}
        char do_thousands_sep() const override
        {
            return m_delimiter;
        }
        std::string do_grouping() const override
        {
            return "\03";
        }
    };

    std::stringstream ss;
    ss.imbue(std::locale(std::locale::classic(), new CustomDelimiterPunct(delimiter)));
    ss << n;

    std::string result = ss.str();
    return result;
}
