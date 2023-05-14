#pragma once
#include "stdafx.h"

#include "common/stringhelper.h"

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
    static size_t HexDumpBuffer(char* outBuffer, size_t outSize, uint8_t* buffer, size_t size, const std::string& delimiter = " ", const std::string& prefix = "");

    /// Dumps array of arbitrary numeric type, 'lineWidth' items per line
    /// @tparam T Type of the numeric values in the array
    /// @param buffer Pointer to the numeric array to dump
    /// @param size Number of elements in the array
    /// @param lineWidth Number of elements to print per line
    /// @return A string containing the dumped array
    template <typename T>
    static std::string HexDumpBuffer(const T* buffer, size_t size, size_t lineWidth)
    {
        std::stringstream ss;

        size_t i = 0;

        while (i < size)
        {
            // Print indexer on each line
            if (i % lineWidth == 0)
            {
                ss << "[" << std::setw(2) << i << "] ";
            }

            // Print each value
            ss << std::setw(sizeof(T) * 3) << buffer[i];

            if ((i + 1) % lineWidth == 0 || i + 1 == size)
            {
                // This line is populated
                ss << std::endl;
            }
            else
            {
                // We have more values to print on current line
                ss << ", ";
            }

            i++;
        }

        return ss.str();
    }
};
