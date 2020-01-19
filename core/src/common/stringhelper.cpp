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

wstring StringHelper::StringToWideString(string& str)
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
		size_t size = 0;
		_locale_t lc = _create_locale(LC_ALL, "en_US.UTF-8");
		errno_t retval = _mbstowcs_s_l(&size, &result[0], len, &str[0], _TRUNCATE, lc);
		_free_locale(lc);
		result.resize(size - 1);
	#endif

	return result;
}

string StringHelper::WideStringToString(wstring& wstr)
{
	string result;

#ifdef _WIN32
	// Optimized Windows API conversion
	int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
	result = string(size, 0);
	WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &wstr[0],(int) wstr.size(), (LPSTR)result.c_str(), size, NULL, NULL);
#else
	// Generic cross-platform conversion
	size_t size = 0;
	_locale_t lc = _create_locale(LC_ALL, "en_US.UTF-8");
	errno_t err = _wcstombs_s_l(&size, NULL, 0, &wstr[0], _TRUNCATE, lc);
	result = string(size, 0);
	err = _wcstombs_s_l(&size, &result[0], size, &wstr[0], _TRUNCATE, lc);
	_free_locale(lc);
	result.resize(size - 1);
#endif

	return result;
}