#pragma once
#include "stdafx.h"

#include <string>

using namespace std;

class StringHelper
{
public:
	static uint8_t Hex(uint8_t val);
	static bool IsHex(uint8_t val);
	static int Compare(wstring& wstr1, wstring& wstr2);
	static int CompareCaseInsensitive(const char* str1, const char* str2, size_t len);

	static wstring StringToWideString(const string& str);
	static string WideStringToString(const wstring& wideString);

	string ReplaceAll(string& str, string from, string to);
	static wstring ReplaceAll(wstring& wstr, wstring wfrom, wstring wto);

    template<typename ... Args>
    static std::string Format(const std::string& format, Args ... args)
    {
        std::string result;

        // Precalculate resulting buffer size (adding +1 byte for trailing '\0')
        size_t size = snprintf(nullptr, 0, format.c_str(), args ...) + 1;
        if (size > 0)
        {
            std::unique_ptr<char[]> buf(new char[ size ]);
            snprintf(buf.get(), size, format.c_str(), args ...);

            // Create output string (without trailing '\0')
            result = std::string(buf.get(),buf.get() + size - 1);
        }

        return result;
    }
};