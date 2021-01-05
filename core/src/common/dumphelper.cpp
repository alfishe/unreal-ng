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
/// \param filePath Path to the file
/// \param buffer Memory buffer with data to make dump from
/// \param size Size of data buffer
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

        // Save the whole buffer to specified file
        FileHelper::SaveBufferToFile(filePath, (uint8_t *)outBuffer, outSize);

        delete[] outBuffer;
    }
}

///
/// \param buffer
/// \param size
/// \return
std::string DumpHelper::HexDumpBuffer(uint8_t* buffer, size_t size, const std::string& delimiter, const std::string& prefix)
{
    std::string result;

    // Preallocate memory for output string
    const int bufferSize = size * (2 + delimiter.size() + prefix.size()); // Byte of data printed as hex with spaces between and line feeds takes more 2 x symbols per each data byte + delimiters + prefixes
    result.resize(bufferSize);

    size_t realDumpLen = DumpHelper::HexDumpBuffer(result.data(), result.length(), buffer, size, delimiter, prefix);
    if (realDumpLen <= bufferSize)
    {
        result.resize(realDumpLen);
    }

    return result;
}

///
/// \param outBuffer
/// \param outSize
/// \param buffer
/// \param size
/// \param delimiter
/// \param prefix
/// \return
size_t DumpHelper::HexDumpBuffer(char* outBuffer, size_t outSize, uint8_t* buffer, size_t size, const std::string& delimiter, const std::string& prefix)
{
    size_t result = 0;

    int outPos = 0;
    int symbolsPrinted = 0;
    int fullLines = size / width;
    int remainder = size % width;
    bool delimiterEnabled = delimiter.size() > 0;
    bool prefixEnabled = prefix.size() > 0;

    if (remainder > 0)
    {
        fullLines += 1;
    }

    // Full lines
    for (int line = 0; line < fullLines; line++)
    {
        bool lastLine = line == fullLines - 1;

        for (int column = 0; column < width; column++)
        {
            bool lastColumn = (column == width - 1) || (remainder > 0 && lastLine && column == remainder - 1);

            // Stop on data end
            if (lastLine && remainder > 0 && column == remainder)
                break;

            if (prefixEnabled)
            {
                symbolsPrinted = snprintf(outBuffer + outPos, outSize - outPos, "%s%02X", prefix.c_str(), buffer[line * width + column]);
            }
            else
            {
                symbolsPrinted = snprintf(outBuffer + outPos, outSize - outPos, "%02X", buffer[line * width + column]);
            }

            if (symbolsPrinted > 0)
            {
                outPos += symbolsPrinted;

                if (delimiterEnabled && !lastColumn && outPos < outSize)
                {
                    symbolsPrinted = snprintf(outBuffer + outPos, outSize - outPos, "%s", delimiter.c_str());
                    outPos += symbolsPrinted;
                }
            }
        }

        if (!lastLine && outPos < outSize)
        {
            *(outBuffer + outPos) = '\n';
            outPos++;
        }

        result = outPos;
    }

    return result;
}