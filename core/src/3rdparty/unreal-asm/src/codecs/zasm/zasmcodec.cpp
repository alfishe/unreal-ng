#include "codecs/zasm/zasmcodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string>

namespace unrealasm::codecs
{
namespace
{
// zasm directives that start with a hash and that the other Spectrum assemblers lack
constexpr std::array<std::string_view, 8> kKeywords = {"#TARGET", "#CODE", "#DATA", "#LOCAL", "#ENDLOCAL", "#INSERT", "#CFLAGS", "#CHARSET"};

std::string Upper(std::string_view word)
{
    std::string out(word);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}
}  // namespace

ZasmCodec::ZasmCodec() : TextCodec(CodecInfo{"zasm", "zasm source (text)", "zasm", CodecFamily::Text, {}}) {}

int ZasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
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
        while (pos < end && (bytes[pos] == ' ' || bytes[pos] == '\t'))
            ++pos;
        if (pos < end && bytes[pos] == '#')
        {
            size_t stop = pos + 1;
            while (stop < end && std::isalnum(bytes[stop]))
                ++stop;
            const std::string token = Upper(std::string_view(reinterpret_cast<const char*>(bytes.data() + pos), stop - pos));
            for (std::string_view keyword : kKeywords)
                if (token == keyword)
                    found.insert(keyword);
        }
        i = end + 1;
    }
    if (found.empty())
        return 0;
    int score = std::min(88, 66 + 8 * static_cast<int>(found.size() - 1));
    const std::string ext = Upper(hints.extension);
    if (ext == "ASM" || ext == "S")
        score += 2;
    return score;
}

DecodeResult ZasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "zasm";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
