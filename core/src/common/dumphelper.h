#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
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

    /// Check if byte buffer filled with desired value
    /// @param buffer Byte buffer
    /// @param size Buffer size
    /// @param value Value to check against
    /// @return Whether buffer is filled with the value or not
    static bool IsFilledWith(const uint8_t* buffer, size_t size, uint8_t value)
    {
        bool result = std::all_of(buffer, buffer + size, [value](uint8_t val) { return val == value; });

        return result;
    }

    /// Compares two uint8_t buffers and generates a diff report
    ///
    /// Performs byte-by-byte comparison of two memory buffers and generates a
    /// human-readable report showing offset locations and differing values in hex format.
    /// Output is limited to first 20 differences to prevent excessive output for large buffers.
    ///
    /// @param buffer1     Pointer to first buffer to compare
    /// @param buffer2     Pointer to second buffer to compare
    /// @param size     Size of buffers in bytes
    /// @return         Formatted string containing:
    ///                 - Header with buffer size
    ///                 - List of differing bytes (offset + values)
    ///                 - Summary with total difference count
    ///                 - Special message if buffers are identical
    ///
    /// @note Example output format:
    ///   Buffer comparison (256 bytes):
    ///     Offset 0x0010: 0xAB != 0xCD
    ///     Offset 0x0020: 0x12 != 0x34
    ///   Found 2 differing bytes
    ///
    /// @warning No bounds checking performed - caller must ensure
    ///          both buffers have at least 'size' bytes allocated
    ///
    /// @see For block-based comparison of large buffers
    /// @see For hex dump output options
    /// @see For similarity percentage calculations
    static std::string DumpBufferDifferences(const uint8_t* buffer1, const uint8_t* buffer2, size_t size)
    {
        std::stringstream ss;
        bool has_diff = false;
        const size_t max_diffs_to_show = 256; // Limit output for large buffers

        ss << "Buffer comparison (" << size << " bytes):\n";

        size_t diff_count = 0;
        for (size_t i = 0; i < size; ++i)
        {
            if (buffer1[i] != buffer2[i])
            {
                if (diff_count < max_diffs_to_show)
                {
                    ss << "  Offset 0x" << std::hex << std::setw(4) << std::setfill('0') << i << ": 0x" << std::setw(2)
                       << static_cast<int>(buffer1[i]) << " != 0x" << std::setw(2) << static_cast<int>(buffer2[i]) << std::dec
                       << "\n";
                }
                diff_count++;
                has_diff = true;
            }
        }

        if (!has_diff)
        {
            ss << "  Buffers are identical\n";
        }
        else
        {
            ss << "  Found " << diff_count << " differing bytes";
            if (diff_count > max_diffs_to_show)
            {
                ss << " (showing first " << max_diffs_to_show << ")";
            }
            ss << "\n";
        }

        return ss.str();
    }
};
