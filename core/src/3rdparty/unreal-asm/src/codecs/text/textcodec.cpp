#include "codecs/text/textcodec.h"

namespace unrealasm::codecs
{
using encoding::CodePage;
using encoding::LineEnd;

namespace
{
// Line attribute byte (only on lines whose break differs from the document's dominant one)
enum LineBreak : uint8_t { kBreakNone = 0, kBreakLf = 1, kBreakCrLf = 2, kBreakCr = 3 };
// File attribute bytes: [0] = UTF-8 byte-order mark, [1] = the last line ends with a break
constexpr size_t kAttrBom = 0;
constexpr size_t kAttrFinalBreak = 1;

LineBreak ToBreak(LineEnd lineEnd)
{
    switch (lineEnd)
    {
        case LineEnd::CrLf: return kBreakCrLf;
        case LineEnd::Cr: return kBreakCr;
        default: return kBreakLf;
    }
}

std::string_view BreakBytes(uint8_t lineBreak)
{
    switch (lineBreak)
    {
        case kBreakLf: return "\n";
        case kBreakCrLf: return "\r\n";
        case kBreakCr: return "\r";
        default: return "";
    }
}
}  // namespace

TextCodec::TextCodec() : _info{"text", "Text source (any code page and line end)", "", CodecFamily::Text} {}

int TextCodec::TextGate(std::span<const uint8_t> bytes)
{
    return encoding::TextBinaryDetector().TextScore(bytes) >= 90 ? 1 : 0;
}

int TextCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints&) const
{
    // A text file is always readable as text; dialect codecs that recognize their own keywords score higher
    return TextGate(bytes) ? 61 : 0;
}

DecodeResult TextCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = !options.dialect.empty() ? options.dialect : _info.dialect;

    std::vector<uint8_t> attrsFile(2, 0);
    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
    {
        attrsFile[kAttrBom] = 1;
        bytes = bytes.subspan(3);
    }

    if (options.codePage)
        document.codePage = *options.codePage;
    else if (attrsFile[kAttrBom])
        document.codePage = CodePage::Utf8;
    else
    {
        const auto ranked = encoding::CodePageDetector().Rank(bytes);
        document.codePage = ranked.front().codePage;
        if (ranked.front().codePage != CodePage::Ascii && ranked.front().confidence < 50)
            result.diagnostics.push_back({Severity::Info, 0, 0,
                                          "code page guessed with low confidence: " +
                                              std::string(encoding::CodePageName(ranked.front().codePage)) + " (" +
                                              std::to_string(ranked.front().confidence) + ")"});
    }
    if (document.codePage == CodePage::Ascii)
        document.codePage = CodePage::Utf8;   // ASCII is valid UTF-8; high bytes then survive as invalid bytes

    // Dominant line end; a mixed file takes the most frequent break, the others go to line attributes
    size_t lf = 0, crlf = 0, cr = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        if (bytes[i] == '\r')
        {
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n')
                ++crlf, ++i;
            else
                ++cr;
        }
        else if (bytes[i] == '\n')
            ++lf;
    }
    document.lineEnd = (crlf >= lf && crlf >= cr && crlf) ? LineEnd::CrLf : (cr > lf && cr) ? LineEnd::Cr : LineEnd::Lf;
    const uint8_t dominant = ToBreak(document.lineEnd);

    size_t invalidTotal = 0;
    size_t start = 0;
    uint32_t lineNumber = 0;
    while (start < bytes.size())
    {
        size_t end = start;
        while (end < bytes.size() && bytes[end] != '\n' && bytes[end] != '\r')
            ++end;
        uint8_t lineBreak = kBreakNone;
        size_t next = end;
        if (end < bytes.size())
        {
            if (bytes[end] == '\r' && end + 1 < bytes.size() && bytes[end + 1] == '\n')
                lineBreak = kBreakCrLf, next = end + 2;
            else if (bytes[end] == '\r')
                lineBreak = kBreakCr, next = end + 1;
            else
                lineBreak = kBreakLf, next = end + 1;
        }
        ++lineNumber;
        size_t invalid = 0;
        SourceLine line;
        line.text = encoding::ToUtf8(bytes.subspan(start, end - start), document.codePage, &invalid);
        if (invalid)
        {
            invalidTotal += invalid;
            result.diagnostics.push_back({Severity::Warning, lineNumber, start,
                                          std::to_string(invalid) + " byte(s) not valid in " +
                                              std::string(encoding::CodePageName(document.codePage)) + " (kept)"});
        }
        if (lineBreak != kBreakNone && lineBreak != dominant)
            line.attrs = {kLayoutAttrs, {lineBreak}};
        attrsFile[kAttrFinalBreak] = lineBreak != kBreakNone ? 1 : 0;
        document.lines.push_back(std::move(line));
        start = next;
    }
    document.attrs = {kLayoutAttrs, attrsFile};
    (void)invalidTotal;
    result.ok = true;
    return result;
}

EncodeResult TextCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const CodePage codePage = options.codePage ? *options.codePage : document.codePage;
    LineEnd lineEnd = options.lineEnd ? *options.lineEnd : document.lineEnd;
    if (lineEnd == LineEnd::Mixed || lineEnd == LineEnd::None)
        lineEnd = LineEnd::Lf;
    const uint8_t dominant = ToBreak(lineEnd);
    // The layout of a decoded text document is reused unless the caller forces a line end
    const bool keepLayout = !options.lineEnd && document.attrs.codec == kLayoutAttrs;
    bool bom = false;
    bool finalBreak = true;
    if (document.attrs.codec == kLayoutAttrs && document.attrs.bytes.size() >= 2)
    {
        bom = document.attrs.bytes[kAttrBom] != 0 && codePage == CodePage::Utf8;
        finalBreak = document.attrs.bytes[kAttrFinalBreak] != 0;
    }
    if (bom)
        result.bytes.insert(result.bytes.end(), {0xEF, 0xBB, 0xBF});

    std::vector<uint8_t> lineBytes;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        std::string error;
        if (!encoding::FromUtf8(line.text, codePage, lineBytes, error))
            result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), result.bytes.size(), error});
        result.bytes.insert(result.bytes.end(), lineBytes.begin(), lineBytes.end());
        const bool last = i + 1 == document.lines.size();
        if (last && !finalBreak)
            break;
        uint8_t lineBreak = dominant;
        if (keepLayout && line.attrs.codec == kLayoutAttrs && !line.attrs.bytes.empty())
            lineBreak = line.attrs.bytes[0];
        const std::string_view brk = BreakBytes(lineBreak);
        result.bytes.insert(result.bytes.end(), brk.begin(), brk.end());
    }
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
