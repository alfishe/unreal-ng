#pragma once

#include <fstream>

namespace utf8
{

class utf8
{
public:
	static std::wstring decode(const std::string &str);
	static std::string encode(const std::wstring &wstr);
};
		
class ifstream : public std::ifstream
{
public:
#ifdef _MSC_VER
	ifstream(const std::string& _Str, ios_base::openmode _Mode = ios_base::in, int _Prot = (int)ios_base::_Openprot) : std::ifstream(utf8::decode(_Str), _Mode, _Prot) { }
	ifstream() : std::ifstream() { }
	void open(const std::string& _Str, ios_base::openmode _Mode = ios_base::in, int _Prot = (int)ios_base::_Openprot) { std::ifstream::open(utf8::decode(_Str), _Mode, _Prot); }
#else
	// Unknown GCC/CLang alternatives
#endif
};

class ofstream : public std::ofstream
{
public:
#ifdef _MSC_VER
	ofstream(const std::string& _Str, ios_base::openmode _Mode = ios_base::in, int _Prot = (int)ios_base::_Openprot) : std::ofstream(utf8::decode(_Str), _Mode, _Prot) { }
	ofstream() : std::ofstream() { }
	void open(const std::string& _Str, ios_base::openmode _Mode = ios_base::in, int _Prot = (int)ios_base::_Openprot) { std::ofstream::open(utf8::decode(_Str), _Mode, _Prot); }
#else
	// Unknown GCC/CLang alternatives
#endif
};

} // namespace utf8