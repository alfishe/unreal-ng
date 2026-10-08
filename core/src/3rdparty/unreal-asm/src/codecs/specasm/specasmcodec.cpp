#include "codecs/specasm/specasmcodec.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace unrealasm::codecs
{
SpecasmCodec::SpecasmCodec() : TextCodec(CodecInfo{"specasm", "Specasm source (.s text)", "specasm", CodecFamily::Text, {}}) {}

int SpecasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    if (!TextGate(bytes))
        return 0;
    int aloneLabels = 0, markers = 0, dotEqu = 0, lines = 0;
    size_t i = 0;
    while (i < bytes.size())
    {
        size_t end = i;
        while (end < bytes.size() && bytes[end] != '\n' && bytes[end] != '\r')
            ++end;
        std::string line(reinterpret_cast<const char*>(bytes.data() + i), end - i);
        const size_t from = line.find_first_not_of(" \t");
        if (from != std::string::npos && line[from] != ';')
        {
            ++lines;
            line = line.substr(from);
            if (line[0] == '.')
            {
                size_t k = 1;
                while (k < line.size() && (std::isalnum(static_cast<unsigned char>(line[k])) || line[k] == '_'))
                    ++k;
                const std::string rest = k < line.size() ? line.substr(line.find_first_not_of(" \t", k) == std::string::npos ? line.size() : line.find_first_not_of(" \t", k)) : "";
                if (k > 1 && (rest.empty() || rest.compare(0, 3, "equ") == 0 || rest[0] == ';'))
                    ++aloneLabels;
                if (k > 1 && rest.compare(0, 3, "equ") == 0)
                    ++dotEqu;   // `.name equ expression`: sjasmplus writes `name equ`, a dot-label is its local label
            }
            // the `=` marker of an expression operand: after a blank, a comma or a bracket and glued to what follows (`ld a, =10*2`)
            const std::string code = line.substr(0, line.find(';'));
            for (size_t e = 1; e + 1 < code.size(); ++e)
                if (code[e] == '=' && (code[e - 1] == ' ' || code[e - 1] == ',' || code[e - 1] == '(') && code[e + 1] != ' ' && code[e + 1] != '=')
                {
                    ++markers;
                    break;
                }
        }
        i = end + 1;
    }
    if (lines == 0 || aloneLabels == 0)
        return 0;
    // `.Name` on a line of its own, and even `.name equ x`, are sjasmplus' local labels as well: only the `=expression` operand is Specasm's alone
    (void)dotEqu;
    if (markers == 0)
        return std::min(25, aloneLabels);
    int score = 70 + std::min(20, aloneLabels * 2);
    std::string ext(hints.extension);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (ext == "S")
        score += 5;
    return std::min(95, score);
}

DecodeResult SpecasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "specasm";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
