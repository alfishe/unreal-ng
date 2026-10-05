#include "codecs/sjasmplus/sjasmpluscodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string>

namespace unrealasm::codecs
{
namespace
{
// Directives that exist in sjasmplus and not in the classic Spectrum assemblers (sjasmplus documentation)
constexpr std::array<std::string_view, 24> kSjasmplusKeywords = {
    "DEVICE", "SLOT", "MMU", "SAVEBIN", "SAVESNA", "SAVETAP", "SAVENEX", "SAVETRD", "EMPTYTRD", "EMPTYTAP",
    "SAVEDEV", "LABELSLIST", "CSPECTMAP", "OUTPUT", "OUTEND", "MODULE", "ENDMODULE", "DEFINE", "UNDEFINE",
    "LUA", "ENDLUA", "INCLUDELUA", "STRUCT", "OPT",
};

std::string Upper(std::string_view word)
{
    std::string out(word);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}
}  // namespace

SjasmplusCodec::SjasmplusCodec() : TextCodec(CodecInfo{"sjasmplus", "sjasmplus source (text)", "sjasmplus", CodecFamily::Text}) {}

int SjasmplusCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    if (!TextGate(bytes))
        return 0;
    // Distinct sjasmplus-only keywords used as the first or second word of a line (before any ';' comment)
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
            while (pos < end && (std::isalnum(bytes[pos]) || bytes[pos] == '_' || bytes[pos] == '.' || bytes[pos] == ':'))
                ++pos;
            if (pos == wordStart)
                break;
            std::string token = Upper(std::string_view(reinterpret_cast<const char*>(bytes.data() + wordStart), pos - wordStart));
            if (!token.empty() && token.back() == ':')
                continue;   // a label
            for (std::string_view keyword : kSjasmplusKeywords)
                if (token == keyword)
                    found.insert(keyword);
        }
        i = end + 1;
    }
    int score = found.empty() ? 30 : std::min(95, 66 + 10 * static_cast<int>(found.size() - 1) + (found.size() >= 2 ? 10 : 0));
    const std::string ext = Upper(hints.extension);
    if (!found.empty() && (ext == "ASM" || ext == "A80" || ext == "Z80" || ext == "S"))
        score = std::min(100, score + 4);
    return score;
}

DecodeResult SjasmplusCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "sjasmplus";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
