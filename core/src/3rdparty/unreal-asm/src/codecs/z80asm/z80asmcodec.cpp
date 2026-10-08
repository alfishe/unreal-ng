#include "codecs/z80asm/z80asmcodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string>

namespace unrealasm::codecs
{
namespace
{
// Directives z80asm has and the other Spectrum assemblers lack (z80asm directive list)
constexpr std::array<std::string_view, 12> kKeywords = {"SECTION", "PUBLIC", "EXTERN", "DEFC", "DEFVARS", "DEFGROUP", "XDEF", "XREF", "XLIB", "ASMPC", "C_LINE", "GLOBAL"};

std::string Upper(std::string_view word)
{
    std::string out(word);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}
}  // namespace

Z80asmCodec::Z80asmCodec() : TextCodec(CodecInfo{"z80asm", "z88dk z80asm source (text)", "z80asm", CodecFamily::Text, {}}) {}

int Z80asmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    if (!TextGate(bytes))
        return 0;
    std::set<std::string_view> found;
    size_t i = 0;
    while (i < bytes.size())
    {
        size_t end = i;
        while (end < bytes.size() && bytes[end] != '\n' && bytes[end] != '\r')
            ++end;
        size_t pos = i;
        for (int word = 0; word < 2 && pos < end; ++word)
        {
            while (pos < end && (bytes[pos] == ' ' || bytes[pos] == '\t'))
                ++pos;
            const size_t wordStart = pos;
            while (pos < end && (std::isalnum(bytes[pos]) || bytes[pos] == '_' || bytes[pos] == ':'))
                ++pos;
            if (pos == wordStart)
                break;
            const std::string token = Upper(std::string_view(reinterpret_cast<const char*>(bytes.data() + wordStart), pos - wordStart));
            for (std::string_view keyword : kKeywords)
                if (token == keyword)
                    found.insert(keyword);
        }
        i = end + 1;
    }
    if (found.empty())
        return 0;
    int score = std::min(85, 60 + 8 * static_cast<int>(found.size() - 1));
    if (Upper(hints.extension) == "ASM" || Upper(hints.extension) == "INC")
        score += 2;
    return score;
}

DecodeResult Z80asmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "z80asm";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
