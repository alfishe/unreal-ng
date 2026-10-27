#pragma once

// Encodings (decision D-13): code page tables, exact conversion to and from UTF-8, and universal detectors for the
// code page, the line ends and text vs binary. They do not depend on the codecs, so any code can use them (the
// emulator's file viewers and text import, other libraries).
//
// Exactness: every byte of every single-byte code page converts to one Unicode code point and back to the same
// byte. Bytes a code page leaves undefined (CP1251 #98) map to the private-use code point U+F700 + byte, so they
// survive a round trip too.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace unrealasm::encoding
{
enum class CodePage : uint8_t
{
    Ascii,       ///< 7-bit; high bytes are not text
    Utf8,
    Cp866,       ///< DOS / alternative Cyrillic: the code page of most Russian ZX sources edited on the Spectrum
    Koi8r,
    Cp1251,      ///< Windows Cyrillic
    ZxSpectrum,  ///< the Spectrum character set: ASCII with ↑ £ © at #5E #60 #7F, block graphics #80-#8F
};

enum class LineEnd : uint8_t
{
    None,   ///< no line break in the input
    Lf,
    CrLf,
    Cr,
    Mixed,
};

/// "ascii", "utf-8", "cp866", "koi8-r", "cp1251", "zx-spectrum"
std::string_view CodePageName(CodePage codePage);
/// The inverse of CodePageName (case-insensitive; also "866", "koi8r", "1251", "windows-1251", "utf8"); false when
/// unknown
bool ParseCodePage(std::string_view name, CodePage& out);
std::string_view LineEndName(LineEnd lineEnd);
/// The bytes of one line break ("\n", "\r\n", "\r"; empty for None / Mixed)
std::string_view LineEndBytes(LineEnd lineEnd);

/// The Unicode code point of one byte in a single-byte code page (Ascii: bytes >= #80 map to U+F700 + byte)
char32_t ByteToCodePoint(uint8_t byte, CodePage codePage);
/// The byte of a code point in a single-byte code page; false when the code page has no such character
bool CodePointToByte(char32_t codePoint, CodePage codePage, uint8_t& out);

/// Bytes in a code page -> UTF-8. Utf8 input is validated: invalid sequences become U+F700 + byte per byte (so they
/// round-trip); `invalid` (when given) counts them
std::string ToUtf8(std::span<const uint8_t> bytes, CodePage codePage, size_t* invalid = nullptr);
/// UTF-8 -> bytes in a code page; false with the first unmappable character in `error` (out holds '?' for every
/// unmappable character)
bool FromUtf8(std::string_view text, CodePage codePage, std::vector<uint8_t>& out, std::string& error);

/// One candidate code page with its confidence
struct CodePageGuess
{
    CodePage codePage = CodePage::Ascii;
    int confidence = 0;  ///< 0..100
};

/// Ranks code pages for a text by its high-half bytes: UTF-8 validity, and for the Cyrillic single-byte pages how
/// much the decoded letters look like Russian (letter frequencies, case, letters vs pseudo-graphics). Pure ASCII
/// ranks Ascii first with confidence 100. Few high bytes give low confidence instead of a guess
class CodePageDetector
{
public:
    std::vector<CodePageGuess> Rank(std::span<const uint8_t> bytes) const;
    /// The best guess (Ascii for empty input)
    CodePageGuess Best(std::span<const uint8_t> bytes) const;
};

class LineEndDetector
{
public:
    LineEnd Detect(std::span<const uint8_t> bytes) const;
};

/// 0..100: how much the bytes look like text (NUL and control bytes other than tab, line breaks, form feed and
/// escape lower the score)
class TextBinaryDetector
{
public:
    int TextScore(std::span<const uint8_t> bytes) const;
};
}  // namespace unrealasm::encoding
