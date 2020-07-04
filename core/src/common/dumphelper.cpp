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
std::string DumpHelper::HesDumpBuffer(uint8_t* buffer, size_t size)
{
    std::string result;

    // Preallocate memory for output string
    const int bufferSize = size * 3; // Byte of data printed as hex with spaces between and line feeds takes x3 more space
    result.resize(bufferSize);

    DumpHelper::HexDumpBuffer(result.data(), result.length(), buffer, size);

    return result;
}

///
/// \param outBuffer
/// \param outSize
/// \param buffer
/// \param size
void DumpHelper::HexDumpBuffer(char* outBuffer, size_t outSize, uint8_t* buffer, size_t size)
{
    int outPos = 0;
    int symbolsPrinted = 0;

    for (int lines = 0; lines < size / width; lines++)
    {
        for (int column = 0; column < width; column++)
        {
            symbolsPrinted = snprintf(outBuffer + outPos, outSize - outPos, "%02X", buffer[lines * width + column]);

            if (symbolsPrinted > 0)
            {
                outPos += symbolsPrinted;

                if (column >= 0 && column < width - 1 && outPos < outSize)
                {
                    *(outBuffer + outPos) = ' ';
                    outPos++;
                }
            }
        }

        if (outPos < outSize)
        {
            *(outBuffer + outPos) = '\n';
            outPos++;
        }
    }
}