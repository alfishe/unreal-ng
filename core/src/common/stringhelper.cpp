#include "stdafx.h"

#include "stringhelper.h"
#include <algorithm>
#include <cassert>

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
		}
		while (len1-- && *ptr1++ && *ptr2++);

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
        }
        while (len1-- && *ptr1++ && *ptr2++);

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
		}
		while (len-- && *ptr1++ && *ptr2++);

		result = *ptr1 - *ptr2;
	}

	return result;
}

std::wstring StringHelper::StringToWideString(const std::string& str)
{
	size_t len = str.length();
    std::wstring result = std::wstring(len, 0);

    // Generic cross-platform conversion
    std::copy(str.begin(), str.end(), result.begin());

	return result;
}

std::string StringHelper::WideStringToString(const std::wstring& wstr)
{
	string result;

	// Generic cross-platform conversion
    std::copy(wstr.begin(), wstr.end(), std::back_inserter(result));

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
    str.remove_prefix(std::distance(str.cbegin(), std::find_if(str.cbegin(), str.cend(),
                                                           [](int c) {return !std::isspace(c);})));

    return str;
}

std::string_view StringHelper::RTrim(std::string_view str)
{
    str.remove_suffix(std::distance(str.crbegin(), std::find_if(str.crbegin(), str.crend(),
                                                            [](int c) {return !std::isspace(c);})));

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
