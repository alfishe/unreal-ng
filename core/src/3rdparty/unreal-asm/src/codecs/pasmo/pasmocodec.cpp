#include "codecs/pasmo/pasmocodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <set>
#include <string>

namespace unrealasm::codecs
{
namespace
{
// Words pasmo has and the other Spectrum assemblers lack (pasmo manual)
constexpr std::array<std::string_view, 8> kPasmoKeywords = {"PROC", "ENDP", "IRP", "EXITM", ".SHIFT", ".ERROR", ".WARNING", "IFNDEF"};

std::string Upper(std::string_view word)
{
    std::string out(word);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}
}  // namespace

PasmoCodec::PasmoCodec() : TextCodec(CodecInfo{"pasmo", "pasmo source (text)", "pasmo", CodecFamily::Text, {}}) {}

int PasmoCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
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
            while (pos < end && (std::isalnum(bytes[pos]) || bytes[pos] == '_' || bytes[pos] == '.' || bytes[pos] == ':'))
                ++pos;
            if (pos == wordStart)
                break;
            const std::string token = Upper(std::string_view(reinterpret_cast<const char*>(bytes.data() + wordStart), pos - wordStart));
            for (std::string_view keyword : kPasmoKeywords)
                if (token == keyword)
                    found.insert(keyword);
        }
        i = end + 1;
    }
    if (found.empty())
        return 0;
    int score = std::min(80, 56 + 8 * static_cast<int>(found.size() - 1));
    if (Upper(hints.extension) == "ASM" || Upper(hints.extension) == "PAS")
        score += 3;
    return score;
}

DecodeResult PasmoCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "pasmo";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
