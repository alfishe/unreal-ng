#include "stdafx.h"

#include "common/logger.h"

#include "dumphelper.h"

#include <cstdio>
#include "common/filehelper.h"
#include "common/stringhelper.h"

int DumpHelper::width = DumpHelper::defaultWidth;

void DumpHelper::SaveHexDumpToFile(std::wstring& filePath, uint8_t* buffer, size_t size)
{
    // Important! Not a unicode path! Rework needed to support
    string path = StringHelper::WideStringToString(filePath);

    DumpHelper::SaveHexDumpToFile(path, buffer, size);
}

/// Saves hex dump of the buffer to specified file
/// Note!: whole text buffer is pre-allocated. Streaming method should be used for real large blocks of memory to be dumped
/// @param filePath Path to the file
/// @param buffer Memory buffer with data to make dump from
/// @param size Size of data buffer
void DumpHelper::SaveHexDumpToFile(std::string& filePath, uint8_t* buffer, size_t size)
{
    if (buffer == nullptr || size == 0)
        return;

    const int outSize = size * 3; // Byte of data printed as hex with spaces between and line feeds takes x3 more space
    char* outBuffer = new char[outSize];
    if (outBuffer != nullptr)
    {
        // Print hex dump to memory buffer
        DumpHelper::HexDumpBuffer(outBuffer, outSize, buffer, size);

        // Save the whole buffer to a specified file
        FileHelper::SaveBufferToFile(filePath, (uint8_t *)outBuffer, outSize);

        delete[] outBuffer;
    }
}

/// @brief Converts a buffer to a hex dump string with optional delimiter and prefix.
/// @param buffer Pointer to the buffer to dump.
/// @param size Size of the buffer in bytes.
/// @param delimiter String to insert between bytes (default: " ").
/// @param prefix String to prepend to each byte (default: "").
/// @return Hexadecimal string representation of the buffer.
std::string DumpHelper::HexDumpBuffer(uint8_t* buffer, size_t size, const std::string& delimiter, const std::string& prefix)
{
    std::string result;

    // Preallocate memory for the output string
    const size_t bufferSize = size * (2 + delimiter.size() + prefix.size());
    result.resize(bufferSize);

    size_t realDumpLen = DumpHelper::HexDumpBuffer(result.data(), result.length(), buffer, size, delimiter, prefix);
    if (realDumpLen <= bufferSize)
    {
        result.resize(realDumpLen);
    }

    return result;
}

/// @brief Writes a hex dump of a buffer into an output buffer with optional delimiter and prefix.
/// @param outBuffer Output buffer to write the hex dump string.
/// @param outSize Size of the output buffer.
/// @param buffer Pointer to the buffer to dump.
/// @param size Size of the buffer in bytes.
/// @param delimiter String to insert between bytes (default: " ").
/// @param prefix String to prepend to each byte (default: "").
/// @return Number of characters written to outBuffer (excluding null terminator).
size_t DumpHelper::HexDumpBuffer(char* outBuffer, size_t outSize, uint8_t* buffer, size_t size, const std::string& delimiter, const std::string& prefix)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t outPos = 0;
    const int lineWidth = width;
    const bool delimiterEnabled = !delimiter.empty();
    const bool prefixEnabled = !prefix.empty();

    for (size_t i = 0; i < size; ++i)
    {
        // Add prefix if needed
        if (prefixEnabled)
        {
            size_t prefixLen = prefix.size();
            if (outPos + prefixLen >= outSize) break;
            memcpy(outBuffer + outPos, prefix.data(), prefixLen);
            outPos += prefixLen;
        }

        // Add hex byte
        if (outPos + 2 >= outSize) break;
        outBuffer[outPos++] = hex[(buffer[i] >> 4) & 0xF];
        outBuffer[outPos++] = hex[buffer[i] & 0xF];

        // Add a delimiter if needed (not after the last byte in line or last byte overall)
        bool lastInLine = ((i + 1) % lineWidth == 0);
        bool lastByte = (i == size - 1);
        if (!lastInLine && !lastByte && delimiterEnabled)
        {
            size_t delimLen = delimiter.size();
            if (outPos + delimLen >= outSize) break;
            memcpy(outBuffer + outPos, delimiter.data(), delimLen);
            outPos += delimLen;
        }

        // Add a newline character at the end of the line (except the last line)
        if (lastInLine && !lastByte)
        {
            if (outPos + 1 >= outSize) break;
            outBuffer[outPos++] = '\n';
        }
    }

    return outPos;
}