#include "unrealasm/encoding.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <unordered_map>

#include "encoding/codepagetables.h"
#include "encoding/utf8.h"

namespace unrealasm::encoding
{
namespace
{
constexpr char32_t kUndefinedBase = 0xF700;   // U+F700 + byte: a byte with no character, kept for the round trip

const char32_t* HighTable(CodePage codePage)
{
    switch (codePage)
    {
        case CodePage::Cp866: return tables::kCp866High;
        case CodePage::Koi8r: return tables::kKoi8rHigh;
        case CodePage::Cp1251: return tables::kCp1251High;
        case CodePage::ZxSpectrum: return tables::kZxSpectrumHigh;
        default: return nullptr;
    }
}

/// Code point -> byte for every single-byte code page, built once (a function-local static: thread-safe)
const std::unordered_map<char32_t, uint8_t>& ReverseTable(CodePage codePage)
{
    static const std::array<std::unordered_map<char32_t, uint8_t>, 6> tables = [] {
        std::array<std::unordered_map<char32_t, uint8_t>, 6> built;
        for (size_t page = 0; page < built.size(); ++page)
            for (int b = 0; b < 256; ++b)
                built[page].emplace(ByteToCodePoint(static_cast<uint8_t>(b), static_cast<CodePage>(page)),
                                    static_cast<uint8_t>(b));
        return built;
    }();
    return tables[static_cast<size_t>(codePage)];
}

std::string Lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
}  // namespace

std::string_view CodePageName(CodePage codePage)
{
    switch (codePage)
    {
        case CodePage::Ascii: return "ascii";
        case CodePage::Utf8: return "utf-8";
        case CodePage::Cp866: return "cp866";
        case CodePage::Koi8r: return "koi8-r";
        case CodePage::Cp1251: return "cp1251";
        case CodePage::ZxSpectrum: return "zx-spectrum";
    }
    return "ascii";
}

bool ParseCodePage(std::string_view name, CodePage& out)
{
    const std::string n = Lower(name);
    if (n == "ascii" || n == "us-ascii")
        out = CodePage::Ascii;
    else if (n == "utf-8" || n == "utf8")
        out = CodePage::Utf8;
    else if (n == "cp866" || n == "866" || n == "ibm866" || n == "dos")
        out = CodePage::Cp866;
    else if (n == "koi8-r" || n == "koi8r" || n == "koi8")
        out = CodePage::Koi8r;
    else if (n == "cp1251" || n == "1251" || n == "windows-1251" || n == "win1251")
        out = CodePage::Cp1251;
    else if (n == "zx-spectrum" || n == "zx" || n == "spectrum")
        out = CodePage::ZxSpectrum;
    else
        return false;
    return true;
}

std::string_view LineEndName(LineEnd lineEnd)
{
    switch (lineEnd)
    {
        case LineEnd::None: return "none";
        case LineEnd::Lf: return "lf";
        case LineEnd::CrLf: return "crlf";
        case LineEnd::Cr: return "cr";
        case LineEnd::Mixed: return "mixed";
    }
    return "none";
}

std::string_view LineEndBytes(LineEnd lineEnd)
{
    switch (lineEnd)
    {
        case LineEnd::Lf: return "\n";
        case LineEnd::CrLf: return "\r\n";
        case LineEnd::Cr: return "\r";
        default: return "";
    }
}

char32_t ByteToCodePoint(uint8_t byte, CodePage codePage)
{
    if (codePage == CodePage::ZxSpectrum)
    {
        if (byte == 0x5E)
            return 0x2191;   // ↑
        if (byte == 0x60)
            return 0x00A3;   // £
        if (byte == 0x7F)
            return 0x00A9;   // ©
    }
    if (byte < 0x80)
        return byte;
    if (const char32_t* table = HighTable(codePage))
        return table[byte - 0x80];
    return kUndefinedBase + byte;   // Ascii / Utf8 used as a single-byte page: high bytes are not characters
}

bool CodePointToByte(char32_t codePoint, CodePage codePage, uint8_t& out)
{
    if (codePage == CodePage::Ascii || codePage == CodePage::Utf8)
    {
        if (codePoint < 0x80)
        {
            out = static_cast<uint8_t>(codePoint);
            return true;
        }
        if (codePoint >= kUndefinedBase + 0x80 && codePoint <= kUndefinedBase + 0xFF)
        {
            out = static_cast<uint8_t>(codePoint - kUndefinedBase);
            return true;
        }
        return false;
    }
    // ASCII is itself in every single-byte page, except the three ZX Spectrum characters at #5E #60 #7F
    if (codePoint < 0x80 && !(codePage == CodePage::ZxSpectrum && (codePoint == 0x5E || codePoint == 0x60 || codePoint == 0x7F)))
    {
        out = static_cast<uint8_t>(codePoint);
        return true;
    }
    const auto& map = ReverseTable(codePage);
    const auto it = map.find(codePoint);
    if (it == map.end())
        return false;
    out = it->second;
    return true;
}

std::string ToUtf8(std::span<const uint8_t> bytes, CodePage codePage, size_t* invalid)
{
    std::string out;
    out.reserve(bytes.size() + bytes.size() / 2);
    size_t bad = 0;
    if (codePage == CodePage::Utf8)
    {
        size_t i = 0;
        while (i < bytes.size())
        {
            char32_t cp = 0;
            const size_t length = utf8::DecodeOne(bytes.subspan(i), cp);
            if (length == 0)
            {
                utf8::Append(out, kUndefinedBase + bytes[i]);
                ++bad;
                ++i;
                continue;
            }
            utf8::Append(out, cp);
            i += length;
        }
    }
    else
    {
        for (uint8_t b : bytes)
        {
            const char32_t cp = ByteToCodePoint(b, codePage);
            if (cp >= kUndefinedBase + 0x80 && cp <= kUndefinedBase + 0xFF)
                ++bad;
            utf8::Append(out, cp);
        }
    }
    if (invalid)
        *invalid = bad;
    return out;
}

bool FromUtf8(std::string_view text, CodePage codePage, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    out.reserve(text.size());
    bool ok = true;
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    std::span<const uint8_t> bytes(data, text.size());
    size_t i = 0;
    const bool spectrum = codePage == CodePage::ZxSpectrum;
    while (i < bytes.size())
    {
        // ASCII is itself in every page (but for the three ZX Spectrum characters): no decoding, no table
        const uint8_t first = bytes[i];
        if (first < 0x80 && !(spectrum && (first == 0x5E || first == 0x60 || first == 0x7F)))
        {
            out.push_back(first);
            ++i;
            continue;
        }
        char32_t cp = 0;
        size_t length = utf8::DecodeOne(bytes.subspan(i), cp);
        if (length == 0)
        {
            if (ok)
                error = "invalid UTF-8 at byte " + std::to_string(i);
            ok = false;
            out.push_back('?');
            ++i;
            continue;
        }
        i += length;
        if (codePage == CodePage::Utf8)
        {
            if (cp >= kUndefinedBase + 0x80 && cp <= kUndefinedBase + 0xFF)
                out.push_back(static_cast<uint8_t>(cp - kUndefinedBase));   // an invalid byte kept on decode
            else
                utf8::AppendBytes(out, cp);
            continue;
        }
        uint8_t b = 0;
        if (CodePointToByte(cp, codePage, b))
        {
            out.push_back(b);
            continue;
        }
        if (ok)
        {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "U+%04X has no byte in ", static_cast<unsigned>(cp));
            error = std::string(buffer) + std::string(CodePageName(codePage));
        }
        ok = false;
        out.push_back('?');
    }
    return ok;
}
}  // namespace unrealasm::encoding
