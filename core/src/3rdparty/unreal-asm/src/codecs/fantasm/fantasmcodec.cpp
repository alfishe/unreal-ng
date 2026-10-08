#include "codecs/fantasm/fantasmcodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string>

namespace unrealasm::codecs
{
namespace
{
constexpr std::array<std::string_view, 6> kKeywords = {"DH", "DZ", "HEX", "!OPT", "!MESSAGE", "#PRAGMA"};

std::string Upper(std::string_view word)
{
    std::string out(word);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}
}  // namespace

FantasmCodec::FantasmCodec() : TextCodec(CodecInfo{"fantasm", "FantASM source (text)", "fantasm", CodecFamily::Text, {}}) {}

int FantasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints&) const
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
        size_t stop = pos;
        while (stop < end && (std::isalnum(bytes[stop]) || bytes[stop] == '!' || bytes[stop] == '#'))
            ++stop;
        const std::string token = Upper(std::string_view(reinterpret_cast<const char*>(bytes.data() + pos), stop - pos));
        for (std::string_view keyword : kKeywords)
            if (token == keyword)
                found.insert(keyword);
        i = end + 1;
    }
    return found.empty() ? 0 : std::min(75, 55 + 8 * static_cast<int>(found.size() - 1));
}

DecodeResult FantasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "fantasm";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
