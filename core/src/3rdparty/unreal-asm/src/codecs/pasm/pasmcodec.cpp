#include "codecs/pasm/pasmcodec.h"

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

/// The words of a line before its comment, the label (a word in column 0) left out; quoted text skipped
std::vector<std::string> Words(std::string_view line)
{
    std::vector<std::string> out;
    size_t pos = 0;
    bool first = true;
    while (pos < line.size() && line[pos] != ';')
    {
        const bool atStart = pos == 0;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t' || line[pos] == ','))
            ++pos;
        if (pos >= line.size() || line[pos] == ';')
            break;
        if (line[pos] == '\'')
        {
            const size_t close = line.find('\'', pos + 1);
            pos = close == std::string_view::npos ? line.size() : close + 1;
            first = false;
            continue;
        }
        const size_t from = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t' && line[pos] != ',' && line[pos] != ';')
            ++pos;
        if (!(first && atStart && from == 0))
            out.push_back(Upper(line.substr(from, pos - from)));
        first = false;
    }
    return out;
}
}  // namespace

PasmCodec::PasmCodec() : TextCodec(CodecInfo{"pasm", "Power Assembler source (text, CR LF)", "pasm", CodecFamily::Text, {}}) {}

int PasmCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    if (!TextGate(bytes))
        return 0;
    std::set<std::string> found;
    size_t crlf = 0, lf = 0;
    size_t i = 0;
    while (i < bytes.size())
    {
        size_t end = i;
        while (end < bytes.size() && bytes[end] != '\n' && bytes[end] != '\r')
            ++end;
        if (end + 1 < bytes.size() && bytes[end] == '\r' && bytes[end + 1] == '\n')
            ++crlf;
        else if (end < bytes.size())
            ++lf;
        const std::vector<std::string> words = Words(std::string_view(reinterpret_cast<const char*>(bytes.data() + i), end - i));
        for (size_t k = 0; k < words.size(); ++k)
        {
            const std::string& w = words[k];
            if (k > 0 && w == "DUP" && (words[0] == "DB" || words[0] == "DW"))
                found.insert("DUP");
            if (k == 0 && (w == "ITXT" || w == "IBIN"))
                found.insert(w);
            if (k == 0 && w == "ENT" && words.size() == 1)
                found.insert("ENT");
            if (k == 0 && w == "SLI")
                found.insert("SLI");
        }
        i = end + 1;
        if (i < bytes.size() && bytes[i - 1] == '\r' && bytes[i] == '\n')
            ++i;
    }
    // ENT alone and SLI are sjasmplus' too: PASM's own DUP items or ITXT / IBIN must be there
    if (crlf < lf || !(found.count("DUP") || found.count("ITXT") || found.count("IBIN")))
        return 0;
    int score = std::min(95, 66 + 10 * static_cast<int>(found.size() - 1));
    if (hints.type != 0 && hints.type != 'C')
        score = std::min(score, 40);
    return score;
}

DecodeResult PasmCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeOptions own = options;
    own.dialect = "pasm";
    DecodeResult result = TextCodec::Decode(bytes, own);
    result.document.format = Info().id;
    return result;
}
}  // namespace unrealasm::codecs
