#include "basicextractor.h"

#include <sstream>

std::string BasicExtractor::extractBasic(uint8_t* data, size_t len)
{
    if (!data || len == 0)
    {
        return "";
    }

    std::stringstream ss;
    size_t offset = 0;

    // Iterate through the buffer, parsing line by line
    while (offset < len)
    {
        // Ensure we have at least 4 bytes for Line Number and Line Length
        if (offset + 4 > len)
            break;

        // 1. Parse Line Number (Big Endian)
        uint16_t lineNumber = (data[offset] << 8) | data[offset + 1];
        offset += 2;

        // 2. Parse Line Length (Little Endian)
        uint16_t lineLength = data[offset] | (data[offset + 1] << 8);
        offset += 2;

        // Check if line length is valid within buffer bounds
        // For robustness, if lineLength exceeds buffer, we clamp it to the end of the buffer.
        // This handles cases like EYEACHE2.B where length is FFFF (trick or malformed).
        size_t lineEnd = offset + lineLength;
        if (lineEnd > len)
        {
            lineEnd = len;
        }

        // Format line number
        ss << lineNumber;

        // Determine if we need a separator space
        bool needsSpace = true;
        if (offset < lineEnd)
        {
            uint8_t firstByte = data[offset];
            if (firstByte >= 0xA3)
            {
                size_t tokenIndex = firstByte - 0xA3;
                if (tokenIndex < sizeof(BasicTokens) / sizeof(BasicTokens[0]))
                {
                    const char* tokenStr = BasicTokens[tokenIndex];
                    if (tokenStr && tokenStr[0] == ' ')
                    {
                        needsSpace = false;
                    }
                }
            }
            else if (firstByte == ' ')
            {
                needsSpace = false;
            }
        }

        if (needsSpace)
        {
            ss << " ";
        }

        // Process line data (starts at current offset, which is past header)
        for (size_t i = offset; i < lineEnd; ++i)
        {
            uint8_t byte = data[i];

            if (byte == 0x0D)  // Newline
            {
                break;
            }

            if (byte >= 0xA3)  // Token
            {
                // Check bounds for token array
                // BasicTokens start at 0xA3. Array index = byte - 0xA3.
                size_t tokenIndex = byte - 0xA3;
                // BasicTokens has entries up to 0xFF. 0xFF - 0xA3 + 1 = 93 tokens.
                // Array size is implicit, but let's assume it covers 0xA3 to 0xFF.
                if (tokenIndex < sizeof(BasicTokens) / sizeof(BasicTokens[0]))
                {
                    ss << BasicTokens[tokenIndex];
                }
                else
                {
                    // Fallback for unknown token?
                    ss << "?";
                }
            }
            else if (byte >= 0x20 && byte <= 0x7E)  // Printable ASCII
            {
                ss << (char)byte;
                
                // If this is a closing quote and next byte is a token, add a space
                // This handles cases like: LOAD "filename"CODE where CODE is a token
                if (byte == 0x22 && i + 1 < lineEnd)  // 0x22 = quote character
                {
                    uint8_t nextByte = data[i + 1];
                    if (nextByte >= 0xA3 && nextByte != 0x0D)  // Next is a token
                    {
                        ss << " ";
                    }
                }
            }
            else if (byte == 0x0E)  // Hidden Number Marker
            {
                // Sequence is 0x0E followed by 5 bytes of binary data.
                // We typically skip this in a listing if ASCII digits are present.
                // Standard behavior: skip 5 bytes.
                if (i + 5 < lineEnd)
                {
                    i += 5;
                }
            }
            // Ignore other control codes for simple text extraction
        }

        ss << '\n';

        offset = lineEnd;
    }

    return ss.str();
}

#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"

std::string BasicExtractor::extractFromMemory(Memory* memory)
{
    if (!memory)
        return "";

    // PROG variable: Address of BASIC program
    uint8_t progL = memory->DirectReadFromZ80Memory(SystemVariables48k::PROG);
    uint8_t progH = memory->DirectReadFromZ80Memory(SystemVariables48k::PROG + 1);
    uint16_t progAddr = progL | (progH << 8);

    // VARS variable: Address of variables
    uint8_t varsL = memory->DirectReadFromZ80Memory(SystemVariables48k::VARS);
    uint8_t varsH = memory->DirectReadFromZ80Memory(SystemVariables48k::VARS + 1);
    uint16_t varsAddr = varsL | (varsH << 8);

    // Sanity / consistency checks
    if (varsAddr < progAddr)
        return "";

    // Safety cap to avoid huge allocations on garbage
    size_t length = varsAddr - progAddr;
    if (length > 0xC000)  // Max reasonable size (48k)
        return "";

    std::vector<uint8_t> buffer(length);
    for (size_t i = 0; i < length; ++i)
    {
        buffer[i] = memory->DirectReadFromZ80Memory(progAddr + i);
    }

    return extractBasic(buffer.data(), buffer.size());
}

void BasicExtractor::detokenize()
{
    // Not used in this implementation version, logic embedded in extractBasic
}

std::string BasicExtractor::printNumeric(NumericValue /*value*/)
{
    // Placeholder, as we are currently skipping internal numeric formats
    // relying on ASCII representation being present.
    return "";
}
