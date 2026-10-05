#pragma once

// Minimal UTF-8 helpers (private to the library)

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace unrealasm::utf8
{
/// Decodes one code point; returns its length in bytes, 0 when the bytes are not valid UTF-8 (overlong forms,
/// surrogates and values above U+10FFFF are invalid)
inline size_t DecodeOne(std::span<const uint8_t> bytes, char32_t& out)
{
    if (bytes.empty())
        return 0;
    const uint8_t b0 = bytes[0];
    if (b0 < 0x80)
    {
        out = b0;
        return 1;
    }
    size_t length = 0;
    char32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0)
    {
        length = 2;
        cp = b0 & 0x1F;
    }
    else if ((b0 & 0xF0) == 0xE0)
    {
        length = 3;
        cp = b0 & 0x0F;
    }
    else if ((b0 & 0xF8) == 0xF0)
    {
        length = 4;
        cp = b0 & 0x07;
    }
    else
        return 0;
    if (bytes.size() < length)
        return 0;
    for (size_t i = 1; i < length; ++i)
    {
        if ((bytes[i] & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (bytes[i] & 0x3F);
    }
    static constexpr char32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMin[length] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    out = cp;
    return length;
}

inline void AppendBytes(std::vector<uint8_t>& out, char32_t cp)
{
    if (cp < 0x80)
        out.push_back(static_cast<uint8_t>(cp));
    else if (cp < 0x800)
    {
        out.push_back(static_cast<uint8_t>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<uint8_t>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<uint8_t>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<uint8_t>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<uint8_t>(0x80 | (cp & 0x3F)));
    }
}

inline void Append(std::string& out, char32_t cp)
{
    if (cp < 0x80)
        out.push_back(static_cast<char>(cp));
    else if (cp < 0x800)
    {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (cp < 0x10000)
    {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}
}  // namespace unrealasm::utf8
