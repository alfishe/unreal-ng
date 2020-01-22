#include "stdafx.h"

#include "stringhelper.h"
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


		}
		while (len-- && *ptr1++ && *ptr2++);

		result = *ptr1 - *ptr2;
	}

	return result;
}

wstring StringHelper::StringToWideString(const string& str)
{
	size_t len = str.length() + 1;
	wstring result = wstring(len, 0);

	#ifdef _WIN32
		// Optimized Windows API conversion
		int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
		result.resize(size_needed, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), (LPWSTR)result.c_str(), size_needed);
	#else
		// Generic cross-platform conversion
		wstring_convert<codecvt_utf8_utf16<wchar_t>> converter;
		result = converter.from_bytes(str);
	#endif

	return result;
}

string StringHelper::WideStringToString(const wstring& wstr)
{
	string result;

#ifdef _WIN32
	// Optimized Windows API conversion
	int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	result = string(size, 0);
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0],(int) wstr.size(), (LPSTR)result.c_str(), size, NULL, NULL);
#else
	// Generic cross-platform conversion
	wstring_convert<codecvt_utf8<wchar_t>> converter;
	result = converter.to_bytes(wstr);
#endif

	return result;
}

string StringHelper::ReplaceAll(string& str, string from, string to)
{
	string result = str;

	if (!from.empty())
	{
		size_t start_pos = 0;
		while ((start_pos = str.find(from, start_pos)) != string::npos)
		{
			str.replace(start_pos, from.length(), to);
			start_pos += to.length();
		}
	}

	return result;
}

wstring StringHelper::ReplaceAll(wstring& wstr, wstring wfrom, wstring wto)
{
	wstring result = wstr;

	if (!wfrom.empty())
	{
		size_t start_pos = 0;
		while ((start_pos = wstr.find(wfrom, start_pos)) != string::npos)
		{
			wstr.replace(start_pos, wfrom.length(), wto);
			start_pos += wto.length();
		}
	}

	return result;
}