#include "stdafx.h"

#include "UTF8Util.h"

#include "common/stringhelper.h"

using namespace std;

namespace utf8 
{
	wstring utf8::decode(const string &str)
	{
		wstring result = StringHelper::StringToWideString(str);
		return result;
	}

	string utf8::encode(const wstring &wstr)
	{
		string result = StringHelper::WideStringToString(wstr);
		return result;
	}
}