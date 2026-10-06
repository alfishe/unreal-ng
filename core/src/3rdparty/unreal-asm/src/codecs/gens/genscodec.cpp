#include "codecs/gens/genscodec.h"

#include <algorithm>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr uint8_t kTab = 0x09;
constexpr uint8_t kCr = 0x0D;
constexpr int kMaxNumber = 32767;
constexpr int kRow = 26;                       // 32 columns minus the 6 of the number margin
constexpr int kStops[] = {7, 12, 21, 25};      // TAB stops inside each row (GENS3 / GENS4, research-gens.md §4)

struct Record
{
    int number = 0;
    std::span<const uint8_t> text;
};

struct Walk
{
    std::vector<Record> lines;
    bool marker = false;      // GENS1's #00 #00 after the last line
    size_t end = 0;           // where the records (and the marker) end; the rest is the tail
};

/// The line records from the start while they parse (research-gens.md §6)
Walk WalkRecords(std::span<const uint8_t> b)
{
    Walk w;
    size_t p = 0;
    int previous = 0;
    while (p + 3 <= b.size())
    {
        const int n = b[p] | (b[p + 1] << 8);
        if (n <= previous || n > kMaxNumber)
            break;
        size_t e = p + 2;
        while (e < b.size() && b[e] != kCr && (b[e] >= 0x20 || b[e] == kTab))
            ++e;
        if (e >= b.size() || b[e] != kCr)
            break;
        w.lines.push_back({n, b.subspan(p + 2, e - p - 2)});
        previous = n;
        p = e + 1;
    }
    if (p + 2 <= b.size() && b[p] == 0 && b[p + 1] == 0 && !w.lines.empty())
    {
        w.marker = true;
        p += 2;
    }
    w.end = p;
    return w;
}

/// The bytes of a line's text the editor would store for it (version "1": as typed, "2": compressed); false with the
/// reason when the text has a character GENS cannot hold
bool Canonical(const std::string& text, const std::string& version, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    if (!encoding::FromUtf8(text, encoding::CodePage::ZxSpectrum, out, error))
        return false;
    for (const uint8_t c : out)
        if (c < 0x20 && c != kTab)
        {
            error = "a control character GENS cannot store in a line";
            return false;
        }
    if (version != "1")
        out = GensCodec::Compress(out);
    return true;
}
}  // namespace

GensCodec::GensCodec()
    : _info{"gens",
            "GENS (HiSoft Devpac) source (numbered lines)",
            "gens",
            CodecFamily::Tokenized,
            {{"1", "GENS1 (Devpac 1983)"}, {"2", "GENS2 / GENS3 / GENS4"}}}
{
}

std::vector<uint8_t> GensCodec::Compress(std::span<const uint8_t> text)
{
    std::vector<uint8_t> b(text.begin(), text.end());
    if (!b.empty() && (b[0] == ';' || b[0] == '*'))
        return b;
    size_t p = 0;
    for (int run = 0; run < 2; ++run)
    {
        while (p < b.size() && b[p] != ' ')
            ++p;
        if (p >= b.size())
            break;
        size_t q = p;
        while (q < b.size() && b[q] == ' ')
            ++q;
        if (q >= b.size())
        {
            b.erase(b.begin() + static_cast<std::ptrdiff_t>(p), b.end());   // blanks up to the line end: dropped
            break;
        }
        b.erase(b.begin() + static_cast<std::ptrdiff_t>(p) + 1, b.begin() + static_cast<std::ptrdiff_t>(q));
        b[p++] = kTab;
    }
    return b;
}

std::string GensCodec::Expand(std::span<const uint8_t> text)
{
    std::string out;
    int column = 0;
    for (const uint8_t c : text)
    {
        if (c == kTab)
        {
            const int row = column / kRow;
            const int inRow = column % kRow;
            const int* next = std::find_if(std::begin(kStops), std::end(kStops), [inRow](int s) { return s > inRow; });
            const int target = next != std::end(kStops) ? row * kRow + *next : (row + 1) * kRow + kStops[0];
            out.append(static_cast<size_t>(target - column), ' ');
            column = target;
            continue;
        }
        utf8::Append(out, encoding::ByteToCodePoint(c, encoding::CodePage::ZxSpectrum));
        ++column;
    }
    return out;
}

int GensCodec::LineNumber(const SourceLine& line)
{
    if (line.attrs.codec != "gens" || line.attrs.bytes.size() < 2)
        return 0;
    return line.attrs.bytes[0] | (line.attrs.bytes[1] << 8);
}

int GensCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    const Walk w = WalkRecords(bytes);
    if (w.lines.empty())
        return 0;
    int score = 10;
    if (w.end == bytes.size())
        score = w.lines.size() >= 3 ? 85 + (w.marker ? 5 : 0) : 50;
    else if (w.marker)
        score = 60;
    else if (w.lines.size() >= 3 && w.end * 10 >= bytes.size() * 9)
        score = 40;
    // TR-DOS ports save type C (one renamed file in the corpus is A)
    if (hints.type != 0 && hints.type != 'C' && hints.type != 'A')
        score = std::min(score, 30);
    return score;
}

DecodeResult GensCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::ZxSpectrum;
    document.lineEnd = encoding::LineEnd::Cr;
    const Walk w = WalkRecords(bytes);
    document.subversion = !options.subversion.empty() ? options.subversion : w.marker ? "1" : "2";
    result.subversions = {document.subversion};
    if (w.lines.empty())
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "no GENS line record (number, text, #0D) at the start"});
        return result;
    }
    if (w.end != bytes.size())
        result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(w.lines.size() + 1), w.end,
                                      "bytes after the last line record (kept as the file's tail)"});
    for (const Record& r : w.lines)
    {
        SourceLine line;
        line.text = Expand(r.text);
        line.attrs.codec = _info.id;
        line.attrs.bytes = {static_cast<uint8_t>(r.number & 0xFF), static_cast<uint8_t>(r.number >> 8)};
        std::vector<uint8_t> canonical;
        std::string error;
        if (!Canonical(line.text, document.subversion, canonical, error) || !std::equal(canonical.begin(), canonical.end(), r.text.begin(), r.text.end()))
            line.attrs.bytes.insert(line.attrs.bytes.end(), r.text.begin(), r.text.end());
        document.lines.push_back(std::move(line));
    }
    // File attributes: 1 when the end marker was there, then the bytes after the records
    std::vector<uint8_t> attrs{static_cast<uint8_t>(w.marker ? 1 : 0)};
    attrs.insert(attrs.end(), bytes.begin() + static_cast<std::ptrdiff_t>(w.end), bytes.end());
    document.attrs = {_info.id, attrs};
    result.ok = true;
    return result;
}

EncodeResult GensCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const bool sameFormat = document.format == _info.id;
    const std::string version = !options.subversion.empty() ? options.subversion
                                : sameFormat && !document.subversion.empty() ? document.subversion : std::string("2");
    const bool keep = sameFormat && document.subversion == version;

    // The stored numbers when every line has one and they increase, else GENS's own renumbering 10, 20, ...
    std::vector<int> numbers;
    int previous = 0;
    for (const SourceLine& line : document.lines)
    {
        const int n = LineNumber(line);
        if (n <= previous || n > kMaxNumber)
            break;
        numbers.push_back(n);
        previous = n;
    }
    if (numbers.size() != document.lines.size())
    {
        if (document.lines.size() > static_cast<size_t>(kMaxNumber))
        {
            result.diagnostics.push_back({Severity::Error, 0, 0, "more lines than GENS numbers (32767)"});
            return result;
        }
        const int step = document.lines.size() * 10 <= static_cast<size_t>(kMaxNumber) ? 10 : 1;
        numbers.clear();
        for (size_t i = 0; i < document.lines.size(); ++i)
            numbers.push_back(static_cast<int>(i + 1) * step);
        if (!document.lines.empty())
            result.diagnostics.push_back({Severity::Info, 0, 0, "lines numbered " + std::to_string(step) + ", " + std::to_string(2 * step) + ", ..."});
    }

    std::vector<uint8_t>& out = result.bytes;
    std::vector<uint8_t> body;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        const auto& a = line.attrs.bytes;
        if (keep && line.attrs.codec == _info.id && a.size() > 2 && Expand(std::span<const uint8_t>(a).subspan(2)) == line.text)
            body.assign(a.begin() + 2, a.end());
        else
        {
            std::string error;
            if (!Canonical(line.text, version, body, error))
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), out.size(), error});
                continue;
            }
        }
        out.push_back(static_cast<uint8_t>(numbers[i] & 0xFF));
        out.push_back(static_cast<uint8_t>(numbers[i] >> 8));
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(kCr);
    }
    const bool attrs = keep && document.attrs.codec == _info.id && !document.attrs.bytes.empty();
    if (attrs ? document.attrs.bytes[0] == 1 : version == "1")
        out.insert(out.end(), {uint8_t(0), uint8_t(0)});
    if (attrs)
        out.insert(out.end(), document.attrs.bytes.begin() + 1, document.attrs.bytes.end());
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
