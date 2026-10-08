#include "codecs/xas/xascodec.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr char32_t kRawBase = 0xF700;   // a byte the font gives no character: U+F700 + byte
constexpr size_t kHeader = 36;
constexpr uint8_t kSentinel = 0x01;
constexpr uint8_t kNormal = 0x0D;
constexpr uint8_t kFirstText = 0x0E;    // bytes below end a line
constexpr uint8_t kDb = 0xC8;

// The token table, the same codes in every version (research-xas.md §4): #80-#C7 commands
constexpr std::array<std::string_view, 72> kCommands = {
    "LDIR", "LDDR", "LDI", "LDD", "CPIR", "CPDR", "CPI", "CPD", "INIR", "INDR", "INI", "IND", "OUTI", "OTIR", "OUTD", "OTDR",
    "RETI", "RETN", "NEG", "RLD", "RRD", "PUSH", "POP", "ADD", "SUB", "ADC", "SBC", "AND", "OR", "XOR", "CP", "INC",
    "DEC", "BIT", "RES", "SET", "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLI", "SRL", "LD", "EX", "IN", "OUT",
    "IM", "RST", "DJNZ", "JP", "JR", "CALL", "RET", "EXX", "CPL", "DAA", "RLCA", "RRCA", "RLA", "RRA", "NOP", "HALT",
    "DI", "EI", "SCF", "CCF", "ORG", "ENT", "EQU", "WORK",
};
// #D0-#EE registers, conditions
constexpr std::array<std::string_view, 31> kRegisters = {
    "BC", "DE", "HL", "IX", "IY", "SP", "AF", "(C)", "B", "C", "D", "E", "H", "L", "(HL)", "A",
    "(BC)", "(DE)", "HX", "LX", "HY", "LY", "I", "R", "NZ", "Z", "NC", "PO", "PE", "P", "M",
};

struct Version
{
    const char* id;
    const char* title;
    std::array<std::string_view, 8> c8;          // #C8-#CF
    std::array<std::string_view, 8> ef;          // #EF-#F6 ("" past the table)
    size_t width;                                // bytes per screen row (the display only)
    int threshold;                               // a label shorter than this puts the command at tab1
    int tab1;
    int tab2;
    bool lower;                                  // text outside strings shown in lower case
    bool f3;                                     // commands at #F3+
    bool pushPop;                                // PUSH / POP take a list
    bool stringGoesOn;                           // a letter or digit after the closing quote continues the string
    bool commaSkip;                              // ", " drops the comma
    std::string_view template_;                  // the title of a new text
};

const std::array<Version, 6> kVersions = {{
    {"4.18", "XAS 4.18", {"DEFB", "DEFW", "DEFM", "DEFS", "!ASSM", "!CONT", "LOADTEXT", "LOADCODE"},
     {"!ON", "!OFF", "(SP)", "AF'", "", "", "", ""}, 43, 8, 8, 14, true, false, false, false, false, "XAS by Max Petrov (HPM) 3.091"},
    {"5.05", "XAS 5.05 / 5.05SE", {"DEFB", "DEFW", "DEFM", "DEFS", "!ASSM", "!CONT", "LOADTEXT", "LOADCODE"},
     {"!ON", "!OFF", "(SP)", "AF'", "", "", "", ""}, 43, 8, 8, 14, true, false, true, true, false, "XAS by Max Petrov (HPM) 5.05 "},
    {"7.43", "XAS 7.43 / 7.447", {"DB", "DW", "DM", "DS", "!ASSM", "!CONT", "LTEXT", "LCODE"},
     {"!ON", "!OFF", "(SP)", "AF'", "USEL", "IFNZ", "IFZ", "MAKE"}, 43, 8, 8, 14, true, true, true, true, true, "by Max Petrov & Creator v7.44"},
    {"7.43c", "XAS 7.43c (64 columns)", {"DB", "DW", "DM", "DS", "!ASSM", "!CONT", "LTEXT", "LCODE"},
     {"!ON", "!OFF", "(SP)", "AF'", "USEL", "IFNZ", "IFZ", "MAKE"}, 65, 8, 15, 23, true, true, true, true, true, "by Max Petrov & Creator v7.43"},
    {"9.07m", "XAS 9.07m (Mythos)", {"DB", "DW", "DM", "DS", ".ASM", ".END", "LTXT", "LCOD"},
     {".ON", ".OFF", "(SP)", "AF'", "USEL", "", "", ""}, 65, 14, 14, 22, false, true, true, true, false, "XAS 9.07 ReCompiled by Mythos"},
    {"9.10", "XAS 9.10 (STS)", {"DB", "DW", "DM", "DS", "!ASSM", "!CONT", "LTEXT", "LCODE"},
     {"!ON", "!OFF", "(SP)", "AF'", "USEL", "", "", ""}, 65, 14, 14, 22, false, true, true, true, false, "XAS by Max Petrov,64sm by STS"},
}};

const Version& FindVersion(const std::string& id)
{
    for (const Version& v : kVersions)
        if (id == v.id)
            return v;
    return kVersions[2];   // 7.43
}

/// The table of a version: index = code - #80 (built once per version)
const std::vector<std::string_view>& Table(const Version& v)
{
    static const std::array<std::vector<std::string_view>, kVersions.size()> tables = [] {
        std::array<std::vector<std::string_view>, kVersions.size()> out;
        for (size_t k = 0; k < kVersions.size(); ++k)
        {
            std::vector<std::string_view>& t = out[k];
            t.assign(kCommands.begin(), kCommands.end());
            t.insert(t.end(), kVersions[k].c8.begin(), kVersions[k].c8.end());
            t.insert(t.end(), kRegisters.begin(), kRegisters.end());
            for (const std::string_view name : kVersions[k].ef)
                if (!name.empty())
                    t.push_back(name);
        }
        return out;
    }();
    return tables[static_cast<size_t>(&v - kVersions.data())];
}

/// The codes of a version's table by the first character of the name (table order kept, so the first match wins as
/// in a scan of the whole table)
const std::array<std::vector<uint8_t>, 256>& ByFirst(const Version& v)
{
    static const std::array<std::array<std::vector<uint8_t>, 256>, kVersions.size()> buckets = [] {
        std::array<std::array<std::vector<uint8_t>, 256>, kVersions.size()> out;
        for (size_t k = 0; k < kVersions.size(); ++k)
        {
            const std::vector<std::string_view>& t = Table(kVersions[k]);
            for (size_t i = 0; i < t.size(); ++i)
                if (!t[i].empty())
                    out[k][static_cast<uint8_t>(t[i][0])].push_back(static_cast<uint8_t>(i));
        }
        return out;
    }();
    return buckets[static_cast<size_t>(&v - kVersions.data())];
}

// The editor's font: Russian letters that do not look like Latin ones at #10-#1F and #7B-#7E (research-xas.md §6)
constexpr char32_t kFontLow[16] = {U'Д', U'Ж', U'И', U'Й', U'Л', U'П', U'У', U'Ф', U'Ц', U'Ч', U'Ы', U'Ь', U'Э', U'Ю', U'Я', U'Ъ'};

char32_t Glyph(uint8_t b)
{
    if (b >= 0x10 && b <= 0x1F)
        return kFontLow[b - 0x10];
    switch (b)
    {
        case 0x5E: return U'↑';
        case 0x60: return U'£';
        case 0x7B: return U'Ш';
        case 0x7C: return U'Щ';
        case 0x7D: return U'Б';
        case 0x7E: return U'Г';
        case 0x7F: return U'©';
        default: break;
    }
    if (b >= 0x20 && b < 0x7F)
        return b;
    return kRawBase + b;
}

bool GlyphByte(char32_t c, uint8_t& out)
{
    // The font read backwards once: the lowest byte showing each character, sorted by character
    static const std::vector<std::pair<char32_t, uint8_t>> reverse = [] {
        std::vector<std::pair<char32_t, uint8_t>> r;
        for (int b = 0x10; b < 0x100; ++b)
            r.emplace_back(Glyph(static_cast<uint8_t>(b)), static_cast<uint8_t>(b));
        std::stable_sort(r.begin(), r.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        r.erase(std::unique(r.begin(), r.end(), [](const auto& x, const auto& y) { return x.first == y.first; }), r.end());
        return r;
    }();
    // ASCII, the common case, from a direct table (-1 = not in the font)
    static const std::array<int16_t, 128> ascii = [] {
        std::array<int16_t, 128> a;
        a.fill(-1);
        for (const auto& [glyph, byte] : reverse)
            if (glyph < 0x80)
                a[glyph] = byte;
        return a;
    }();
    if (c < 0x80 && ascii[c] >= 0)
    {
        out = static_cast<uint8_t>(ascii[c]);
        return true;
    }
    const auto found = std::lower_bound(reverse.begin(), reverse.end(), c, [](const auto& e, char32_t v) { return e.first < v; });
    if (found != reverse.end() && found->first == c)
    {
        out = found->second;
        return true;
    }
    if (c < 0x10)
    {
        out = static_cast<uint8_t>(c);
        return true;
    }
    return false;
}

/// XAS's row formatter (7.447 #8B22) for one stored line, without the row cut: the shown bytes (< #100 font codes and
/// token names)
std::string Format(std::span<const uint8_t> line, const Version& v)
{
    const std::vector<std::string_view>& table = Table(v);
    std::string out;
    int col = 0;
    int fields = 3;
    uint8_t mask = v.lower ? 0x20 : 0;
    bool afterDb = false;
    size_t i = 0;
    const size_t n = line.size();
    auto glyph = [&](uint8_t b) {
        utf8::Append(out, Glyph(b));
        ++col;
    };
    auto token = [&](uint8_t t) {
        afterDb = t == kDb;
        const size_t k = t - 0x80u;
        if (k < table.size())
            for (const char c : table[k])
                glyph(static_cast<uint8_t>(c));
        else
            glyph(t);   // past the table: XAS prints whatever follows it
    };
    auto separator = [&] {
        if (--fields == 0)
        {
            fields = 1;
            glyph(',');
            return;
        }
        const int target = col < v.threshold ? v.tab1 : v.tab2;
        for (int k = std::max(target - col, 1); k > 0; --k)
            glyph(' ');
    };
    auto ends = [&](size_t k) { return k >= n || line[k] < kFirstText; };
    while (!ends(i))
    {
        uint8_t a = line[i];
        if (a < 0x80)
        {
            if (a == ';')   // a comment: the rest as it is
            {
                while (!ends(i))
                    glyph(line[i++]);
                break;
            }
            if (a == '"')
                mask ^= v.lower ? 0x20 : 0;
            if (fields != 3)
                separator();
        }
        else
            separator();
        // one item
        for (;;)
        {
            a = line[i];
            if (a >= 0x80)
            {
                token(a);
                ++i;
                break;
            }
            if (a == '(' && i + 1 < n && line[i + 1] >= 0x80)   // "(" + register: copied up to ")"
            {
                if (!(i > 0 && line[i - 1] >= 0x80))
                    glyph(',');
                glyph('(');
                token(line[++i]);
                ++i;
                for (;;)
                {
                    if (ends(i))
                        return out;
                    a = line[i++];
                    glyph(a);
                    if (a == ')')
                        break;
                }
                break;
            }
            if (a >= 'A' && a <= 'Z' && !afterDb)
                a |= mask;
            glyph(a);
            ++i;
            if (ends(i))
                return out;
            if (line[i] == ';' || line[i] >= 0x80)
                break;
        }
    }
    return out;
}

/// XAS's line packer (7.447 #889A, 4.18 #87E8, 5.05 #87EB, 7.43 #887E, 9.10 #87F2), research-xas.md §5
class Packer
{
public:
    Packer(std::span<const uint8_t> src, const Version& v) : _src(src), _v(v), _table(Table(v)), _byFirst(ByFirst(v)) {}

    bool Run(std::vector<uint8_t>& out, std::string& error)
    {
        const State s = Pack();
        out = std::move(_out);
        error = _error;
        return s != State::Error;
    }

private:
    enum class State
    {
        Go,
        Done,
        Error,
    };

    uint8_t At(size_t k) const { return k < _src.size() ? _src[k] : kNormal; }

    size_t Skip(size_t k) const
    {
        while (k < _src.size() && (_src[k] == ' ' || _src[k] == ','))
            ++k;
        return k;
    }

    State Fail(const char* reason)
    {
        _error = reason;
        return State::Error;
    }

    State Comment(size_t k)
    {
        while (k < _src.size())
            _out.push_back(_src[k++]);
        while (!_out.empty() && _out.back() == ' ')
            _out.pop_back();
        return State::Done;
    }

    /// A comma is stored only in DB / DW / DS (and PUSH / POP from 5.05) operand lists
    State Comma(size_t& k)
    {
        if (_limit == 3)
            return State::Go;
        if (_v.commaSkip && (At(k + 1) == ' ' || At(k + 1) == ','))
        {
            ++k;
            return State::Go;
        }
        _out.push_back(',');
        _texts = 0;
        return State::Go;
    }

    /// A text operand up to a blank, a comma, a ';' or the end: capitals, '.' as '#'
    State Text(size_t& k)
    {
        for (;;)
        {
            uint8_t c = At(k);
            if (c == ';')
                return Comment(k);
            if (c < kFirstText || c == ' ')
                return State::Go;
            if (c == ',')
                return Comma(k);
            if (c == '.')
                c = '#';
            if (c >= 'a' && c <= 'z')
                c &= 0xDF;
            _out.push_back(c);
            ++k;
        }
    }

    /// A quoted string, at least one character; from 5.05 a letter or digit after the closing quote goes on
    State String(size_t& k)
    {
        _out.push_back('"');
        ++k;
        for (;;)
        {
            uint8_t c = At(k);
            if (c < kFirstText)
                return Fail("unclosed string");
            _out.push_back(c);
            ++k;
            if (At(k) != '"')
                continue;
            ++k;
            _out.push_back('"');
            c = At(k);
            if (_v.stringGoesOn)
            {
                if (c == ';')
                    return Text(k);
                if (c >= 0x30)
                    continue;
            }
            if (c == ',')
                return Comma(k);
            return Text(k);
        }
    }

    /// One text item; "(IX" / "(IY" before + - ) is "(" and the register and does not count as a text item
    State Item(size_t& k)
    {
        const uint8_t c = At(k);
        if (c == ';')
            return Comment(k);
        const int xy = (At(k + 2) & 0xDF) - 'X';
        if (c == '(' && (At(k + 1) & 0xDF) == 'I' && (xy == 0 || xy == 1) && (At(k + 3) == '-' || At(k + 3) == ')' || At(k + 3) == '+'))
        {
            _out.push_back('(');
            _out.push_back(static_cast<uint8_t>(0xD3 + xy));
            k += 3;
        }
        else
        {
            if (_texts)
                return Fail("two text items in a row");
            _texts = 1;
        }
        if (At(k) == '"')
            return String(k);
        return Text(k);
    }

    /// A keyword at k (either case) followed by ',' ' ' ';' or the line end: its code, else 0
    uint8_t Match(size_t k, size_t& next) const
    {
        if (k >= _src.size())
            return 0;
        uint8_t first = _src[k];
        if (first >= 'a' && first <= 'z')
            first &= 0xDF;
        for (const uint8_t t : _byFirst[first])
        {
            const std::string_view name = _table[t];
            if (k + name.size() > _src.size())
                continue;
            bool same = true;
            for (size_t j = 0; j < name.size() && same; ++j)
            {
                uint8_t c = _src[k + j];
                if (c >= 'a' && c <= 'z')
                    c &= 0xDF;
                same = c == static_cast<uint8_t>(name[j]);
            }
            if (!same)
                continue;
            const uint8_t after = At(k + name.size());
            if (after == ',' || after == ' ' || after == ';' || after < kFirstText)
            {
                next = k + name.size();
                return static_cast<uint8_t>(0x80 + t);
            }
        }
        return 0;
    }

    State Pack()
    {
        size_t i = 0;
        size_t next = 0;
        // label and command
        for (;;)
        {
            i = Skip(i);
            if (i >= _src.size())
                return State::Done;
            const uint8_t t = Match(i, next);
            if (!t)
            {
                const State s = Item(i);
                if (s != State::Go)
                    return s;
                continue;
            }
            if (t >= 0xD0 && !(_v.f3 && t >= 0xF3))
                return Fail("a register in command position (XAS inserts the implied command first)");
            _out.push_back(t);
            i = next;
            if (t == 0xC8 || t == 0xC9 || t == 0xCB || (_v.pushPop && (t == 0x95 || t == 0x96)))
                _limit = t;
            break;
        }
        // operands
        _texts = 0;
        int count = 0;
        for (;;)
        {
            i = Skip(i);
            if (i >= _src.size())
                return State::Done;
            const uint8_t t = Match(i, next);
            if (t)
            {
                if (t < 0xD0)
                    return Fail("a command in operand position");
                _out.push_back(t);
                i = next;
                continue;
            }
            const State s = Item(i);
            if (s != State::Go)
                return s;
            if (++count == _limit)
                return Fail("too many text operands");
        }
    }

    std::span<const uint8_t> _src;
    const Version& _v;
    const std::vector<std::string_view>& _table;
    const std::array<std::vector<uint8_t>, 256>& _byFirst;
    std::vector<uint8_t> _out;
    std::string _error;
    int _limit = 3;
    int _texts = 0;
};

struct Layout
{
    size_t header = kHeader;   // 35 when the file lost its first title byte
    size_t end = 0;            // the #00 that ends the text (the size when there is none)
    bool terminated = false;
};

Layout Measure(std::span<const uint8_t> b)
{
    Layout l;
    l.header = b.size() > 35 && b[35] == kSentinel ? 36 : b.size() > 34 && b[34] == kSentinel ? 35 : 36;
    l.end = std::min(l.header, b.size());
    while (l.end < b.size() && b[l.end] != 0)
        ++l.end;
    l.terminated = l.end < b.size();
    return l;
}

bool IsEnd(uint8_t c)
{
    return c == 0x0D || c == 0x0C || c == 0x09;
}

/// The version whose editor most likely wrote the file: the title if it is still the template, else 7.43 when the text
/// uses #F3-#F6 (7.x only) or the title names Creator, else 4.18 (research-xas.md §8)
std::string GuessVersion(std::span<const uint8_t> b, const Layout& l)
{
    std::string title(b.begin(), b.begin() + static_cast<std::ptrdiff_t>(std::min(l.header - 7, b.size())));
    std::replace(title.begin(), title.end(), '\xC9', 'x');   // "MaÉ": one title has bit 7 set on a letter
    while (!title.empty() && title.back() == ' ')
        title.pop_back();
    for (const Version& v : kVersions)
    {
        std::string_view t = v.template_;
        while (!t.empty() && t.back() == ' ')
            t.remove_suffix(1);
        t = t.substr(t.size() > 20 ? t.size() - 20 : 0);
        if (title.size() >= t.size() && title.compare(title.size() - t.size(), t.size(), t) == 0)
            return v.id;
    }
    uint8_t top = 0;
    for (size_t i = l.header; i < l.end; ++i)
        top = std::max(top, b[i]);
    if (top >= 0xF3 || title.find("Creator") != std::string::npos)
        return "7.43";
    return "4.18";
}

std::vector<uint8_t> WithSlack(std::span<const uint8_t> bytes, const CatalogHints& hints)
{
    std::vector<uint8_t> all(bytes.begin(), bytes.end());
    all.insert(all.end(), hints.slack.begin(), hints.slack.end());
    return all;
}
}  // namespace

XasCodec::XasCodec() : _info{"xas", "XAS source (tokenized, TR-DOS type X)", "xas", CodecFamily::Tokenized, {}}
{
    for (const Version& v : kVersions)
        _info.subversions.push_back({v.id, v.title});
}

std::string XasCodec::DecodeBody(std::span<const uint8_t> body, const std::string& version)
{
    return Format(body, FindVersion(version));
}

bool XasCodec::EncodeBody(const std::string& text, const std::string& version, std::vector<uint8_t>& body, std::string& error)
{
    std::vector<uint8_t> src;
    src.reserve(text.size());
    const std::span<const uint8_t> utf(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    for (size_t p = 0; p < utf.size();)
    {
        char32_t c = 0;
        p += utf8::DecodeOne(utf.subspan(p), c);
        uint8_t b = 0;
        if (!GlyphByte(c, b))
        {
            error = "a character XAS's font does not have";
            return false;
        }
        src.push_back(b);
    }
    return Packer(src, FindVersion(version)).Run(body, error);
}

int XasCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    const std::vector<uint8_t> all = WithSlack(bytes, hints);
    const Layout l = Measure(all);
    bool text = all.size() > l.header && all[l.header - 1] == kSentinel && l.terminated;
    if (text)
        for (size_t i = l.header; i < l.end && text; ++i)
            text = all[i] >= kFirstText || IsEnd(all[i]);
    if (text && l.end > l.header)
        text = IsEnd(all[l.end - 1]);
    const bool type = hints.type == 'X' || hints.type == 'x';
    const bool start = hints.start == 0x5341 || hints.start == 0x5361;   // "AS" (a source), "aS" (XMACROS)
    if (!text)
        return 0;
    if (type && start)
        return 95;
    if (type || start)
        return 85;
    return hints.type != 0 ? 20 : 60;
}

DecodeResult XasCodec::Decode(std::span<const uint8_t> input, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::ZxSpectrum;
    document.lineEnd = encoding::LineEnd::Cr;
    const std::vector<uint8_t> b = WithSlack(input, options.catalog);
    if (b.size() < kHeader - 1)
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "shorter than XAS's header"});
        return result;
    }
    const Layout l = Measure(b);
    document.subversion = !options.subversion.empty() ? options.subversion : GuessVersion(b, l);
    result.subversions = {document.subversion};
    const Version& v = FindVersion(document.subversion);
    if (!l.terminated)
        result.diagnostics.push_back({Severity::Warning, 0, l.end, "no #00 ends the text"});
    size_t start = l.header;
    for (size_t i = l.header; i < l.end; ++i)
    {
        if (b[i] >= kFirstText)
            continue;
        const std::span<const uint8_t> stored(b.data() + start, i - start);
        SourceLine line;
        line.text = Format(stored, v);
        std::vector<uint8_t> packed;
        std::string error;
        const bool canonical = EncodeBody(line.text, document.subversion, packed, error) && std::equal(packed.begin(), packed.end(), stored.begin(), stored.end());
        if (b[i] != kNormal || !canonical)
        {
            line.attrs.codec = _info.id;
            line.attrs.bytes.push_back(b[i]);
            if (!canonical)
                line.attrs.bytes.insert(line.attrs.bytes.end(), stored.begin(), stored.end());
        }
        document.lines.push_back(std::move(line));
        start = i + 1;
    }
    // File attributes: the header length and bytes, the bytes after the last line end (2-byte count), then from the
    // #00 on (with the sector slack)
    std::vector<uint8_t> attrs{static_cast<uint8_t>(l.header)};
    attrs.insert(attrs.end(), b.begin(), b.begin() + static_cast<std::ptrdiff_t>(l.header));
    const size_t rest = l.end - start;
    attrs.push_back(static_cast<uint8_t>(rest & 0xFF));
    attrs.push_back(static_cast<uint8_t>(rest >> 8));
    attrs.insert(attrs.end(), b.begin() + static_cast<std::ptrdiff_t>(start), b.end());
    document.attrs = {_info.id, attrs};
    result.ok = true;
    return result;
}

EncodeResult XasCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    EncodeResult result;
    const bool sameFormat = document.format == _info.id;
    const std::string version = !options.subversion.empty() ? options.subversion
                                : sameFormat && !document.subversion.empty() ? document.subversion : std::string("7.43");
    const bool keep = sameFormat && document.subversion == version;
    const auto& a = document.attrs.bytes;
    const bool attrs = sameFormat && document.attrs.codec == _info.id && !a.empty() && a.size() >= 1u + a[0] + 2u;
    std::vector<uint8_t>& out = result.bytes;
    if (attrs)
        out.assign(a.begin() + 1, a.begin() + 1 + a[0]);
    else
    {
        // A new text: the version's title, the cursor on the first line (#C023), a clear editor state
        const std::string_view title = FindVersion(version).template_;
        out.assign(title.begin(), title.end());
        out.resize(29, ' ');
        out.insert(out.end(), {0x23, 0xC0, 0, 0, 0, 0, kSentinel});
    }
    std::vector<uint8_t> body;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        const bool own = line.attrs.codec == _info.id && !line.attrs.bytes.empty();
        const uint8_t end = own && IsEnd(line.attrs.bytes[0]) ? line.attrs.bytes[0] : kNormal;
        const auto& lb = line.attrs.bytes;
        if (keep && own && lb.size() > 1 && Format(std::span<const uint8_t>(lb).subspan(1), FindVersion(version)) == line.text)
            body.assign(lb.begin() + 1, lb.end());
        else
        {
            std::string error;
            if (!EncodeBody(line.text, version, body, error))
            {
                result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), out.size(), error});
                continue;
            }
        }
        out.insert(out.end(), body.begin(), body.end());
        out.push_back(end);
    }
    if (attrs)
        out.insert(out.end(), a.begin() + 1 + a[0] + 2, a.end());
    else
        out.push_back(0);
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
