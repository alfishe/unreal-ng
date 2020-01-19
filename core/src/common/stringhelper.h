#pragma once
#include "stdafx.h"

#include <string>

using namespace std;

class StringHelper
{
public:
	static uint8_t Hex(uint8_t val);
	static bool IsHex(uint8_t val);

	static wstring StringToWideString(const string& str);
	static string WideStringToString(const wstring& wideString);

	string ReplaceAll(string& str, string from, string to);
	static wstring ReplaceAll(wstring& wstr, wstring wfrom, wstring wto);
};