#pragma once
#include "stdafx.h"

#include <codecvt>
#include <locale>
#include <string>

using namespace std;

class StringHelper
{
public:
	static uint8_t Hex(uint8_t val);
	static bool IsHex(uint8_t val);

	static wstring StringToWideString(string& str);
	static string WideStringToString(wstring& wideString);
};