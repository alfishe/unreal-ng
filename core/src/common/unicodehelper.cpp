#include "stdafx.h"

#include "unicodehelper.h"

#include <algorithm>
#include <cctype>

namespace
{
    /// CP866 bytes #80-#FF as Unicode code points (IBM code page 866)
    constexpr char32_t kCp866High[128] = {
        // #80-#8F: А-П
        0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
        0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
        // #90-#9F: Р-Я
        0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
        0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
        // #A0-#AF: а-п
        0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
        0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
        // #B0-#BF: shades and box drawing
        0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
        0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
        // #C0-#CF: box drawing
        0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
        0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
        // #D0-#DF: box drawing and blocks
        0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
        0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
        // #E0-#EF: р-я
        0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
        0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
        // #F0-#FF: Ё ё Є є Ї ї Ў ў ° ∙ · √ № ¤ ■ NBSP
        0x0401, 0x0451, 0x0404, 0x0454, 0x0407, 0x0457, 0x040E, 0x045E,
        0x00B0, 0x2219, 0x00B7, 0x221A, 0x2116, 0x00A4, 0x25A0, 0x00A0,
    };

    /// Windows-1251 bytes #80-#FF as Unicode code points (#98 is undefined)
    constexpr char32_t kCp1251High[128] = {
        // #80-#8F
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
        0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
        // #90-#9F
        0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
        // #A0-#AF
        0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
        0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
        // #B0-#BF
        0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
        0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
        // #C0-#DF: А-Я
        0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
        0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
        0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
        0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
        // #E0-#FF: а-я
        0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
        0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
        0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
        0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
    };

    const char32_t* HighTable(CodePage page)
    {
        return page == CodePage::Cp1251 ? kCp1251High : kCp866High;
    }

    bool IsContinuation(unsigned char byte)
    {
        return (byte & 0xC0) == 0x80;
    }
}  // namespace

std::u32string UnicodeHelper::DecodeUtf8(const std::string& text)
{
    std::u32string result;
    result.reserve(text.size());

    const auto* s = reinterpret_cast<const unsigned char*>(text.data());
    const size_t n = text.size();
    size_t i = 0;
    while (i < n)
    {
        const unsigned char lead = s[i];
        char32_t cp = 0;
        size_t extra = 0;
        char32_t minimum = 0;
        if (lead < 0x80)
        {
            result.push_back(lead);
            i++;
            continue;
        }
        if ((lead & 0xE0) == 0xC0)
        {
            cp = lead & 0x1F;
            extra = 1;
            minimum = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            cp = lead & 0x0F;
            extra = 2;
            minimum = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            cp = lead & 0x07;
            extra = 3;
            minimum = 0x10000;
        }
        else
        {
            result.push_back(kReplacement);  // a stray continuation byte or an invalid lead
            i++;
            continue;
        }

        size_t k = 1;
        while (k <= extra && i + k < n && IsContinuation(s[i + k]))
        {
            cp = (cp << 6) | (s[i + k] & 0x3F);
            k++;
        }
        if (k <= extra || cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            result.push_back(kReplacement);  // truncated, overlong, out of range or a surrogate
            i += k;
            continue;
        }
        result.push_back(cp);
        i += k;
    }
    return result;
}

void UnicodeHelper::AppendUtf8(std::string& out, char32_t cp)
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = kReplacement;
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

std::string UnicodeHelper::EncodeUtf8(const std::u32string& codepoints)
{
    std::string result;
    result.reserve(codepoints.size());
    for (const char32_t cp : codepoints)
        AppendUtf8(result, cp);
    return result;
}

std::u16string UnicodeHelper::ToUtf16(const std::u32string& codepoints)
{
    std::u16string result;
    result.reserve(codepoints.size());
    for (char32_t cp : codepoints)
    {
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
            cp = kReplacement;
        if (cp < 0x10000)
            result.push_back(static_cast<char16_t>(cp));
        else
        {
            cp -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return result;
}

std::u32string UnicodeHelper::FromUtf16(const std::u16string& units)
{
    std::u32string result;
    result.reserve(units.size());
    for (size_t i = 0; i < units.size(); i++)
    {
        const char32_t u = units[i];
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF)
        {
            result.push_back(0x10000 + ((u - 0xD800) << 10) + (units[i + 1] - 0xDC00));
            i++;
        }
        else if (u >= 0xD800 && u <= 0xDFFF)
            result.push_back(kReplacement);
        else
            result.push_back(u);
    }
    return result;
}

bool UnicodeHelper::ToCodePage(CodePage page, char32_t codepoint, uint8_t& byte)
{
    if (codepoint < 0x80)
    {
        byte = static_cast<uint8_t>(codepoint);
        return true;
    }
    if (codepoint == kReplacement)
        return false;
    const char32_t* table = HighTable(page);
    for (size_t i = 0; i < 128; i++)
    {
        if (table[i] == codepoint)
        {
            byte = static_cast<uint8_t>(0x80 + i);
            return true;
        }
    }
    return false;
}

char32_t UnicodeHelper::FromCodePage(CodePage page, uint8_t byte)
{
    return byte < 0x80 ? static_cast<char32_t>(byte) : HighTable(page)[byte - 0x80];
}

const char* UnicodeHelper::CodePageName(CodePage page)
{
    return page == CodePage::Cp1251 ? "cp1251" : "cp866";
}

bool UnicodeHelper::ParseCodePage(const std::string& text, CodePage& page)
{
    std::string lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "cp866" || lower == "866" || lower == "ibm866")
        page = CodePage::Cp866;
    else if (lower == "cp1251" || lower == "1251" || lower == "windows-1251")
        page = CodePage::Cp1251;
    else
        return false;
    return true;
}

char32_t UnicodeHelper::ToUpper(char32_t cp)
{
    if (cp >= U'a' && cp <= U'z')
        return cp - 0x20;
    if (cp >= 0x0430 && cp <= 0x044F)  // а-я
        return cp - 0x20;
    if (cp >= 0x0450 && cp <= 0x045F)  // ѐ ё ђ ѓ є ѕ і ї ј љ њ ћ ќ ѝ ў џ
        return cp - 0x50;
    if (cp == 0x0491)  // ґ
        return 0x0490;
    return cp;
}
