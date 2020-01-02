#include "stdafx.h"
#include "UTF8Util.h"
#include <codecvt>
#include <locale>

using namespace std;

namespace utf8 
{
	wstring utf8::decode(const string &str)
	{
		wstring_convert<codecvt_utf8<wchar_t>> conv;
		return conv.from_bytes(str);
	}

	string utf8::encode(const wstring &wstr)
	{
		wstring_convert<codecvt_utf8<wchar_t>> conv;
    	return conv.to_bytes(wstr);
	}

	string utf8::encode(const u16string &wstr)
	{
		#ifdef _MSC_VER
			std::wstring_convert<codecvt_utf8_utf16<int16_t>, int16_t> conv;
			auto p = reinterpret_cast<const int16_t *>(wstr.data());
			return conv.to_bytes(p, p + wstr.size());
		#else 
			wstring_convert<codecvt_utf8_utf16<char16_t>, char16_t> conv;
  			return conv.to_bytes(wstr);
		#endif
	}	
}