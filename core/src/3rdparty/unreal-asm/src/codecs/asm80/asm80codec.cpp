#include "codecs/asm80/asm80codec.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <string_view>

namespace unrealasm::codecs
{
namespace
{
std::string Upper(std::string_view word)
{
    std::string out(word);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

/// The key a line in column 0 starting with '*' gives (ASM80's decode_star): "" when it is none of ASM80's
std::string StarKey(std::string_view line)
{
    if (line.size() < 2 || line[0] != '*')
        return {};
    const std::string rest = Upper(line.substr(1, 3));
    auto ends = [&](size_t k) { return k >= line.size() || static_cast<unsigned char>(line[k]) <= ' ' || line[k] == ';'; };
    if (rest.rfind("Z80", 0) == 0 && ends(4))
        return "*Z80";
    if (rest.rfind("GA", 0) == 0 && ends(3))
        return "*GA";
    const char k = rest[0];
    if ((k == 'L' || k == 'M' || k == 'C' || k == 'D') && line.size() > 2 && (line[2] == '+' || line[2] == '-') && ends(3))
        return std::string("*") + k;
    if ((k == 'F' || k == 'B') && line.size() > 2 && (line[2] == ' ' || line[2] == '\t'))
        return std::string("*") + k;
    if (k == 'P' && line.size() > 2 && std::isxdigit(static_cast<unsigned char>(line[2])))
        return "*P";
    if ((k == 'O' || k == '$' || k == 'E' || k == 'G' || k == 'S') && ends(2))
        return std::string("*") + k;
    return {};
}
}  // namespace

Asm80Codec::Asm80Codec() : TextCodec(CodecInfo{"asm80", "ASM80 / Asm80Win source (text)", "asm80", CodecFamily::Text, {}}) {}

int Asm80Codec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    if (!TextGate(bytes))
        return 0;
    std::set<std::string> found;
    size_t i = 0;
    while (i < bytes.size())
    {
        size_t end = i;
        while (end < bytes.size() && bytes[end] != '\n' && bytes[end] != '\r')
            ++end;
        const std::string_view line(reinterpret_cast<const char*>(bytes.data() + i), end - i);
        const std::string key = StarKey(line);
        if (!key.empty())
            found.insert(key);
        // The words of the line before a ';' comment: ENDD, DEFR, INF and MAC are ASM80's; =0..=9 its macro parameters
        const std::string code = Upper(line.substr(0, std::min(line.find(';'), line.size())));
        size_t pos = 0;
        bool first = true;
        while (pos < code.size())
        {
            while (pos < code.size() && (code[pos] == ' ' || code[pos] == '\t' || code[pos] == ','))
                ++pos;
            const size_t from = pos;
            while (pos < code.size() && code[pos] != ' ' && code[pos] != '\t' && code[pos] != ',')
                ++pos;
            const std::string word = code.substr(from, pos - from);
            const bool label = first && from == 0;
            first = false;
            if (label || word.empty())
                continue;
            if (word == "ENDD" || word == "DEFR" || word == "INF" || word == "MAC")
                found.insert(word);
            if (word.size() >= 2 && word.find('=') != std::string::npos)
            {
                const size_t eq = word.find('=');
                if (eq + 1 < word.size() && std::isdigit(static_cast<unsigned char>(word[eq + 1])) &&
                    (eq + 2 == word.size() || !std::isalnum(static_cast<unsigned char>(word[eq + 2]))))
                    found.insert("=n");
            }
        }
        i = end + 1;
    }
    if (found.empty())
        return 0;
    int score = std::min(95, 66 + 10 * static_cast<int>(found.size() - 1));
    if (Upper(hints.extension) == "A80")
        score = std::min(100, score + 5);
    return score;
}

DecodeResult Asm80Codec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "asm80";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
