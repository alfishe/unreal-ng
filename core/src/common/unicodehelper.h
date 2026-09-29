#pragma once

/// @file unicodehelper.h
/// @brief Exact Unicode conversions that do not depend on the platform's
/// wchar_t: UTF-8 <-> code points <-> UTF-16, and the 8-bit code pages
/// Spectrum FAT software uses for short names: CP866 (Russian DOS, NedoOS's
/// OEM code page) and CP1251 (Windows Cyrillic).
///
/// StringHelper::StringToWideString / utf8util convert to the platform's wide
/// strings (UTF-16 on Windows, UTF-32 elsewhere) for OS calls. Media formats
/// need fixed encodings instead: FAT long names are UTF-16 on every host, FAT
/// short names are bytes of an OEM code page.

#include <cstdint>
#include <string>

/// 8-bit code pages for FAT short names
enum class CodePage : uint8_t
{
    Cp866,   ///< IBM 866, Russian DOS (the default)
    Cp1251,  ///< Windows-1251, Cyrillic
};

class UnicodeHelper
{
public:
    static constexpr char32_t kReplacement = U'�';

    /// UTF-8 -> code points. Invalid or overlong sequences and surrogates
    /// become U+FFFD, one per bad byte run
    static std::u32string DecodeUtf8(const std::string& text);
    static std::string EncodeUtf8(const std::u32string& codepoints);
    static void AppendUtf8(std::string& out, char32_t codepoint);

    /// Code points -> UTF-16 (surrogate pairs above U+FFFF)
    static std::u16string ToUtf16(const std::u32string& codepoints);
    /// UTF-16 -> code points; unpaired surrogates become U+FFFD
    static std::u32string FromUtf16(const std::u16string& units);

    /// Code point -> byte of `page`; false when the page has no such character
    static bool ToCodePage(CodePage page, char32_t codepoint, uint8_t& byte);
    /// Byte of `page` -> code point (U+FFFD for a byte the page leaves undefined)
    static char32_t FromCodePage(CodePage page, uint8_t byte);

    static const char* CodePageName(CodePage page);  ///< "cp866" | "cp1251"
    /// "cp866" / "866" / "cp1251" / "1251" / "windows-1251" (case-insensitive)
    static bool ParseCodePage(const std::string& text, CodePage& page);

    /// Upper case for the letters these code pages hold: ASCII a-z, Cyrillic
    /// а-я (U+0430-U+044F), the extended Cyrillic ѐ-џ (U+0450-U+045F) and ґ.
    /// Anything else is returned unchanged
    static char32_t ToUpper(char32_t codepoint);
};
