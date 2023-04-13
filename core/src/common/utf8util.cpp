#include "stdafx.h"

#include "utf8util.h"

#include "common/stringhelper.h"

namespace utf8
{
	std::wstring utf8::decode(const std::string &str)
	{
        std::wstring result = StringHelper::StringToWideString(str);
		return result;
	}

    std::string utf8::encode(const std::wstring &wstr)
	{
        std::string result = StringHelper::WideStringToString(wstr);
		return result;
	}
}