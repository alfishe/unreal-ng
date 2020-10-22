#pragma once
#include "stdafx.h"

class DumpHelper
{
    /// region <Fields>
protected:
    static const int defaultWidth = 16; // 16 bytes in hex per line
    static int width;
    /// endregion </Fields>

public:
    static void SaveHexDumpToFile(std::wstring& filePath, uint8_t* buffer, size_t size);
    static void SaveHexDumpToFile(std::string& filePath, uint8_t* buffer, size_t size);

    static std::string HexDumpBuffer(uint8_t* buffer, size_t size, const std::string& delimiter = " ", const std::string& prefix = "");
    static void HexDumpBuffer(char* outBuffer, size_t outSize, uint8_t* buffer, size_t size, const std::string& delimiter = " ", const std::string& prefix = "");

};
