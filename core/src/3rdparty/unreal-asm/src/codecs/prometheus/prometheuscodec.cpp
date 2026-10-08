#include "codecs/prometheus/prometheuscodec.h"

#include <algorithm>
#include <cctype>
#include <map>

#include "codecs/prometheus/prometheustable.h"

namespace unrealasm::codecs
{
namespace
{
using prometheus::kRecords;
using prometheus::Record;
using Symbol = PrometheusCodec::Symbol;

constexpr uint8_t kPseudo = 0x30;     // DD+FD together: a pseudo-instruction's information byte
constexpr uint8_t kLabelFlag = 0x08;
constexpr uint8_t kSeparator = 0x1F;  // between the displacement and the value of LD (IX+d),n
constexpr size_t kLabelField = 9;     // PROMETHEUS' editor fields
constexpr size_t kMnemonicField = 5;
constexpr size_t kMaxPayload = 0x3F;  // the marker's six bits

std::string Lower(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t'))
        ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t'))
        --b;
    return s.substr(a, b - a);
}

/// The FD (IY) spelling of a DD record's line
std::string IyForm(std::string_view line)
{
    std::string out(line);
    for (size_t k = 0; k + 1 < out.size(); ++k)
    {
        if (out[k] == 'i' && out[k + 1] == 'x')
            out[k + 1] = 'y';
        else if ((out[k] == 'h' || out[k] == 'l') && out[k + 1] == 'x')
            out[k + 1] = 'y';
    }
    return out;
}

/// The table record for an opcode and information byte (FD looked up as DD); null when there is none
const Record* Find(uint8_t opcode, uint8_t info, bool& iy)
{
    uint8_t prefix = info & 0xF0;
    const uint8_t storage = info & 0x07;
    iy = false;
    if ((prefix & kPseudo) == 0x10)
    {
        iy = true;
        prefix = static_cast<uint8_t>((prefix & ~0x10) | 0x20);
    }
    for (const Record& r : kRecords)
        if (r.opcode == opcode && r.prefix == prefix && r.storage == storage)
            return &r;
    return nullptr;
}

std::string Mnemonic(std::string_view line)
{
    const size_t blank = line.find(' ');
    return std::string(line.substr(0, blank));
}

std::string OperandTemplate(std::string_view line)
{
    const size_t blank = line.find(' ');
    return blank == std::string_view::npos ? std::string() : std::string(line.substr(blank + 1));
}

/// A line in PROMETHEUS' fields: the label padded to 9 columns, the mnemonic to 5 when operands follow
std::string Compose(const std::string& label, const std::string& mnemonic, const std::string& operands)
{
    if (mnemonic.empty())
        return label;
    std::string out = label;
    out.append(out.size() < kLabelField ? kLabelField - out.size() : 1, ' ');
    out += mnemonic;
    if (!operands.empty())
    {
        out.append(mnemonic.size() < kMnemonicField ? kMnemonicField - mnemonic.size() : 1, ' ');
        out += operands;
    }
    return out;
}

/// Splits at commas outside quotes and parentheses
std::vector<std::string> SplitOperands(const std::string& text)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    char quoted = 0;
    for (const char c : text)
    {
        if ((c == '"' || c == '\'') && (!quoted || quoted == c))
            quoted = quoted ? 0 : c;
        else if (!quoted && c == '(')
            ++depth;
        else if (!quoted && c == ')' && depth > 0)
            --depth;
        if (c == ',' && depth == 0 && !quoted)
        {
            out.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!Trim(current).empty() || !out.empty())
        out.push_back(Trim(current));
    return out;
}

/// The ordinal of a name (1-based), added at the end when new
uint16_t Ordinal(std::vector<Symbol>& symbols, const std::string& name)
{
    for (size_t k = 0; k < symbols.size(); ++k)
        if (symbols[k].name == name)
            return static_cast<uint16_t>(k + 1);
    symbols.push_back({name, 0, false, false});
    return static_cast<uint16_t>(symbols.size());
}

void PutOrdinal(std::vector<uint8_t>& out, uint16_t ordinal)
{
    out.push_back(static_cast<uint8_t>(0x80 | (ordinal >> 8)));
    out.push_back(static_cast<uint8_t>(ordinal & 0xFF));
}

/// An operand's characters as PROMETHEUS stores them: names (a letter, then letters and digits) as ordinals, hex
/// digits in capitals, quoted text as it is
bool EncodeExpression(const std::string& text, std::vector<Symbol>& symbols, std::vector<uint8_t>& out, std::string& error)
{
    for (size_t k = 0; k < text.size();)
    {
        const unsigned char c = static_cast<unsigned char>(text[k]);
        if (c >= 0x80)
        {
            error = "a character PROMETHEUS cannot store (above #7F)";
            return false;
        }
        if (c == '"' || c == '\'')
        {
            const size_t close = text.find(static_cast<char>(c), k + 1);
            const size_t end = close == std::string::npos ? text.size() : close + 1;
            for (size_t n = k; n < end; ++n)
            {
                if (static_cast<unsigned char>(text[n]) >= 0x80)
                {
                    error = "a character PROMETHEUS cannot store (above #7F)";
                    return false;
                }
                out.push_back(static_cast<uint8_t>(text[n]));
            }
            k = end;
            continue;
        }
        if (c == '#')
        {
            out.push_back('#');
            ++k;
            while (k < text.size() && std::isxdigit(static_cast<unsigned char>(text[k])))
                out.push_back(static_cast<uint8_t>(std::toupper(static_cast<unsigned char>(text[k++]))));
            continue;
        }
        if (std::isalpha(c))
        {
            size_t end = k;
            while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
                ++end;
            PutOrdinal(out, Ordinal(symbols, Upper(text.substr(k, end - k))));
            k = end;
            continue;
        }
        if (c != ' ')
            out.push_back(c);
        ++k;
    }
    return true;
}

bool Terminate(std::vector<uint8_t>& out, std::string& error)
{
    const size_t payload = out.size() - 2;
    if (payload > kMaxPayload)
    {
        error = "the line is too long for a PROMETHEUS record (63 bytes after the header)";
        return false;
    }
    out.push_back(static_cast<uint8_t>(0xC0 | payload));
    return true;
}

/// How a template operand matches a written operand: 3 a fixed word, 2 an indexed operand, 1 an expression, 0 no match
int Match(const std::string& pattern, const std::string& operand, bool& iy)
{
    const std::string lower = Lower(operand);
    std::string compact;
    for (const char c : lower)
        if (c != ' ')
            compact.push_back(c);
    auto indexed = [&](const std::string& p) { return p.find("(ix+d)") != std::string::npos; };
    if (pattern == "N")
        return compact.empty() || compact[0] == '(' ? 0 : 1;
    if (pattern == "(N)")
        return compact.size() >= 2 && compact.front() == '(' && compact.back() == ')' && compact.rfind("(ix", 0) != 0 && compact.rfind("(iy", 0) != 0 ? 1 : 0;
    if (indexed(pattern))
    {
        if (compact.size() < 5 || compact[0] != '(' || compact[1] != 'i' || (compact[2] != 'x' && compact[2] != 'y') || (compact[3] != '+' && compact[3] != '-') ||
            compact.back() != ')')
            return 0;
        iy = compact[2] == 'y';
        return 2;
    }
    // A fixed word; an index register's IY spelling matches the DD record
    if (compact == pattern)
        return 3;
    if (IyForm(pattern) == compact && IyForm(pattern) != pattern)
    {
        iy = true;
        return 3;
    }
    return 0;
}
}  // namespace

PrometheusCodec::PrometheusCodec() : _info{"prometheus", "PROMETHEUS source (Proxima)", "prometheus", CodecFamily::Tokenized, {}} {}

bool PrometheusCodec::ReadSymbols(std::span<const uint8_t> bytes, std::vector<Symbol>& out, size_t& used)
{
    out.clear();
    if (bytes.size() < 2)
        return false;
    const size_t count = bytes[0] | (bytes[1] << 8);
    const size_t vectors = 2 + 2 * count;
    if (bytes.size() < vectors)
        return false;
    const size_t anchor = vectors + 2;
    size_t end = vectors;
    for (size_t k = 0; k < count; ++k)
    {
        const uint16_t v = static_cast<uint16_t>(bytes[2 + 2 * k] | (bytes[3 + 2 * k] << 8));
        const size_t at = anchor + (v & 0x3FFF);
        if (at < 2 || at >= bytes.size())
            return false;
        Symbol s;
        s.value = static_cast<uint16_t>(bytes[at - 2] | (bytes[at - 1] << 8));
        s.defined = (v & 0x4000) != 0;
        s.locked = (v & 0x8000) != 0;
        size_t n = at;
        while (n < bytes.size() && bytes[n] < 0x80)
            s.name.push_back(static_cast<char>(bytes[n++]));
        if (n >= bytes.size())
            return false;
        s.name.push_back(static_cast<char>(bytes[n] & 0x7F));
        end = std::max(end, n + 1);
        for (const char c : s.name)
            if (static_cast<unsigned char>(c) < 0x20)
                return false;
        out.push_back(std::move(s));
    }
    used = end;
    return true;
}

bool PrometheusCodec::DecodeRecord(std::span<const uint8_t> bytes, size_t pos, const std::vector<Symbol>& symbols, std::string& text, size_t& length)
{
    if (pos + 2 > bytes.size())
        return false;
    const uint8_t opcode = bytes[pos], info = bytes[pos + 1];
    bool iy = false;
    const Record* record = Find(opcode, info, iy);
    if (!record)
        return false;
    const bool hasLabel = (info & kLabelFlag) != 0;
    const uint8_t storage = info & 0x07;
    std::vector<uint8_t> payload;
    if (!hasLabel && storage == 0)
        length = 2;
    else
    {
        size_t k = pos + 2;
        while (k < bytes.size() && bytes[k] < 0xC0)
        {
            if (bytes[k] >= 0x80)
            {
                if (k + 1 >= bytes.size())
                    return false;
                payload.push_back(bytes[k++]);
            }
            payload.push_back(bytes[k++]);
        }
        if (k >= bytes.size() || (bytes[k] & 0x3F) != payload.size())
            return false;
        length = k + 1 - pos;
    }
    auto name = [&](size_t at, std::string& out) {
        const size_t ordinal = ((payload[at] & 0x3F) << 8) | payload[at + 1];
        if (ordinal == 0 || ordinal > symbols.size())
            return false;
        out = symbols[ordinal - 1].name;
        return true;
    };
    size_t at = 0;
    std::string label;
    if (hasLabel)
    {
        if (payload.size() < 2 || payload[0] < 0x80)
            return false;
        if (!name(0, label))
            return false;
        at = 2;
    }
    // The operand's characters, names resolved; class 5 holds two parts split by #1F
    std::vector<std::string> parts(1);
    for (; at < payload.size(); ++at)
    {
        if (payload[at] >= 0x80)
        {
            std::string n;
            if (!name(at, n))
                return false;
            parts.back() += n;
            ++at;
        }
        else if (payload[at] == kSeparator && storage == 5)
            parts.emplace_back();
        else
            parts.back().push_back(static_cast<char>(payload[at]));
    }
    const std::string line = iy ? IyForm(record->line) : std::string(record->line);
    if (record->prefix == kPseudo && record->opcode == 0x01)   // comment
    {
        text = parts[0];
        return true;
    }
    if (record->prefix == kPseudo && record->opcode == 0x00)   // empty line (a label alone)
    {
        text = label;
        return true;
    }
    const std::string mnemonic = Mnemonic(line);
    std::string operands = OperandTemplate(line);
    if (record->prefix == kPseudo)
        operands = parts[0];   // ent / equ / org / put N, defb / defm / defs / defw: the characters as written
    else if (storage == 4 || storage == 5)
    {
        const std::string displacement = parts[0];
        const std::string sign = !displacement.empty() && displacement[0] == '-' ? "" : "+";
        const std::string reg = iy ? "(iy+d)" : "(ix+d)";
        const size_t p = operands.find(reg);
        if (p == std::string::npos)
            return false;
        operands.replace(p, reg.size(), "(" + reg.substr(1, 2) + sign + displacement + ")");
        if (storage == 5)
        {
            if (parts.size() != 2)
                return false;
            const size_t n = operands.rfind('N');
            if (n == std::string::npos)
                return false;
            operands.replace(n, 1, parts[1]);
        }
    }
    else if (storage != 0)
    {
        const size_t n = operands.find('N');
        if (n == std::string::npos)
            return false;
        operands.replace(n, 1, parts[0]);
    }
    text = Compose(label, mnemonic, operands);
    return true;
}

bool PrometheusCodec::FindSourceLength(std::span<const uint8_t> bytes, size_t& length)
{
    // The first record boundary after which two bytes and a symbol table fill the rest exactly; the records up to it
    // must decode against that table
    std::vector<size_t> boundaries{0};
    size_t pos = 0;
    while (pos + 2 <= bytes.size())
    {
        const uint8_t info = bytes[pos + 1];
        size_t next = pos + 2;
        if ((info & kLabelFlag) || (info & 0x07))
        {
            while (next < bytes.size() && bytes[next] < 0xC0)
                next += bytes[next] >= 0x80 ? 2 : 1;
            if (next >= bytes.size())
                break;
            ++next;
        }
        bool iy = false;
        if (!Find(bytes[pos], info, iy))
            break;
        pos = next;
        boundaries.push_back(pos);
    }
    // A save's two middle bytes are the records' checksum and #FF: such a boundary is tried first
    std::vector<size_t> checked, others;
    uint8_t checksum = 0xFF;
    size_t done = 0;
    for (const size_t b : boundaries)
    {
        for (; done < b; ++done)
            checksum ^= bytes[done];
        (b + 1 < bytes.size() && bytes[b] == checksum && bytes[b + 1] == 0xFF ? checked : others).push_back(b);
    }
    checked.insert(checked.end(), others.begin(), others.end());
    for (const size_t b : checked)
    {
        if (b + 4 > bytes.size())
            continue;
        std::vector<Symbol> symbols;
        size_t used = 0;
        if (!ReadSymbols(bytes.subspan(b + 2), symbols, used) || b + 2 + used != bytes.size())
            continue;
        size_t p = 0, n = 0;
        std::string text;
        bool ok = true;
        while (p < b && ok)
        {
            ok = DecodeRecord(bytes, p, symbols, text, n);
            p += n;
        }
        if (ok && p == b)
        {
            length = b;
            return true;
        }
    }
    return false;
}

int PrometheusCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    (void)hints;
    size_t length = 0;
    if (bytes.size() < 6 || !FindSourceLength(bytes, length))
        return 0;
    // Records and a table alone could be chance (zeros read as NOPs and an empty table): a save also has the records'
    // checksum in the two middle bytes
    uint8_t checksum = 0xFF;
    for (size_t k = 0; k < length; ++k)
        checksum ^= bytes[k];
    if (bytes[length] != checksum || bytes[length + 1] != 0xFF)
        return 0;
    return length >= 4 ? 92 : 70;
}

DecodeResult PrometheusCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    result.document.format = _info.id;
    result.document.dialect = _info.dialect;
    result.document.codePage = encoding::CodePage::Ascii;
    result.document.name = options.catalog.name;
    size_t length = 0;
    if (!FindSourceLength(bytes, length))
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "not a PROMETHEUS source save: no records followed by two bytes and a symbol table"});
        return result;
    }
    std::vector<Symbol> symbols;
    size_t used = 0;
    ReadSymbols(bytes.subspan(length + 2), symbols, used);
    size_t pos = 0;
    while (pos < length)
    {
        SourceLine line;
        size_t n = 0;
        DecodeRecord(bytes, pos, symbols, line.text, n);
        line.attrs.codec = _info.id;
        line.attrs.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(pos), bytes.begin() + static_cast<std::ptrdiff_t>(pos + n));
        result.document.lines.push_back(std::move(line));
        pos += n;
    }
    result.document.attrs.codec = _info.id;
    result.document.attrs.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(length), bytes.end());   // the two bytes + the table
    result.ok = true;
    return result;
}

bool PrometheusCodec::EncodeLine(const std::string& text, std::vector<Symbol>& symbols, std::vector<uint8_t>& out, std::string& error)
{
    out.clear();
    if (Trim(text).empty())
    {
        out = {0x00, kPseudo};
        return true;
    }
    if (text[0] == ';')
    {
        out = {0x01, 0x37};
        for (const char c : text)
        {
            if (static_cast<unsigned char>(c) >= 0x80)
            {
                error = "a character PROMETHEUS cannot store (above #7F)";
                return false;
            }
            out.push_back(static_cast<uint8_t>(c));
        }
        return Terminate(out, error);
    }
    size_t k = 0;
    std::string label;
    if (text[0] != ' ' && text[0] != '\t')
    {
        while (k < text.size() && text[k] != ' ' && text[k] != '\t')
            ++k;
        label = Upper(text.substr(0, k));
    }
    const std::string rest = Trim(text.substr(k));
    const size_t blank = rest.find_first_of(" \t");
    const std::string mnemonic = Lower(rest.substr(0, blank));
    const std::string operands = blank == std::string::npos ? std::string() : Trim(rest.substr(blank));
    auto start = [&](uint8_t opcode, uint8_t info) {
        out = {opcode, static_cast<uint8_t>(info | (label.empty() ? 0 : kLabelFlag))};
        if (!label.empty())
            PutOrdinal(out, Ordinal(symbols, label));
    };
    if (mnemonic.empty())
    {
        start(0x00, kPseudo);
        return Terminate(out, error);
    }
    static const std::map<std::string, uint8_t> kPseudoOps = {{"ent", 2}, {"equ", 3}, {"org", 4}, {"put", 5}, {"defb", 6}, {"defm", 7}, {"defs", 8}, {"defw", 9}};
    const auto pseudo = kPseudoOps.find(mnemonic);
    if (pseudo != kPseudoOps.end())
    {
        start(pseudo->second, 0x37);
        if (!EncodeExpression(operands, symbols, out, error))
            return false;
        return Terminate(out, error);
    }
    // The instruction record whose operands match: fixed words first, then placeholders
    const std::vector<std::string> written = SplitOperands(operands);
    const Record* best = nullptr;
    int bestScore = -1;
    bool bestIy = false;
    for (const Record& r : kRecords)
    {
        if (r.prefix == kPseudo || Mnemonic(r.line) != mnemonic)
            continue;
        const std::string tmpl = OperandTemplate(r.line);
        const std::vector<std::string> pattern = tmpl.empty() ? std::vector<std::string>{} : SplitOperands(tmpl);
        if (pattern.size() != written.size())
            continue;
        int score = 0;
        bool iy = false, ok = true;
        for (size_t n = 0; n < pattern.size() && ok; ++n)
        {
            bool thisIy = false;
            const int m = Match(pattern[n], written[n], thisIy);
            ok = m > 0;
            score += m;
            iy = iy || thisIy;
        }
        if (ok && score > bestScore)
        {
            best = &r;
            bestScore = score;
            bestIy = iy;
        }
    }
    if (!best)
    {
        error = "no PROMETHEUS instruction matches: " + rest;
        return false;
    }
    const uint8_t prefix = bestIy ? static_cast<uint8_t>((best->prefix & ~0x20) | 0x10) : best->prefix;
    start(best->opcode, static_cast<uint8_t>(prefix | best->storage));
    const std::string tmpl = OperandTemplate(best->line);
    const std::vector<std::string> pattern = tmpl.empty() ? std::vector<std::string>{} : SplitOperands(tmpl);
    bool first = true;
    for (size_t n = 0; n < pattern.size(); ++n)
    {
        std::string expr;
        if (pattern[n] == "N")
            expr = written[n];
        else if (pattern[n] == "(N)")
            expr = Trim(written[n].substr(1, written[n].size() - 2));
        else if (pattern[n].find("(ix+d)") != std::string::npos)
        {
            std::string compact;
            for (const char c : written[n])
                if (c != ' ')
                    compact.push_back(c);
            expr = compact.substr(3, compact.size() - 4);   // "+5" / "-3"
            if (!expr.empty() && expr[0] == '+')
                expr.erase(0, 1);
        }
        else
            continue;
        if (!first)
            out.push_back(kSeparator);
        first = false;
        if (!EncodeExpression(expr, symbols, out, error))
            return false;
    }
    if (best->storage == 0 && label.empty())
        return true;   // a two-byte record
    return Terminate(out, error);
}

std::vector<uint8_t> PrometheusCodec::WriteSymbols(const std::vector<Symbol>& symbols)
{
    const size_t count = symbols.size();
    std::vector<size_t> order(count);
    for (size_t k = 0; k < count; ++k)
        order[k] = k;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return symbols[a].name < symbols[b].name; });
    std::vector<uint8_t> out{static_cast<uint8_t>(count & 0xFF), static_cast<uint8_t>(count >> 8)};
    out.resize(2 + 2 * count);
    std::vector<uint8_t> records;
    std::vector<size_t> offsets(count);
    for (const size_t k : order)
    {
        const Symbol& s = symbols[k];
        records.push_back(static_cast<uint8_t>(s.value & 0xFF));
        records.push_back(static_cast<uint8_t>(s.value >> 8));
        offsets[k] = records.size() - 2;   // the anchor is two bytes into the records: the first name is at offset 0
        for (size_t n = 0; n < s.name.size(); ++n)
            records.push_back(static_cast<uint8_t>(s.name[n] | (n + 1 == s.name.size() ? 0x80 : 0)));
    }
    for (size_t k = 0; k < count; ++k)
    {
        const uint16_t v = static_cast<uint16_t>(offsets[k] | (symbols[k].defined ? 0x4000 : 0) | (symbols[k].locked ? 0x8000 : 0));
        out[2 + 2 * k] = static_cast<uint8_t>(v & 0xFF);
        out[3 + 2 * k] = static_cast<uint8_t>(v >> 8);
    }
    out.insert(out.end(), records.begin(), records.end());
    return out;
}

EncodeResult PrometheusCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    (void)options;
    EncodeResult result;
    std::vector<Symbol> symbols;
    std::vector<uint8_t> table;
    const bool kept = document.attrs.codec == _info.id && document.attrs.bytes.size() >= 4;
    if (kept)
    {
        size_t used = 0;
        const std::span<const uint8_t> tail(document.attrs.bytes);
        if (ReadSymbols(tail.subspan(2), symbols, used))
            table.assign(tail.begin() + 2, tail.end());
    }
    const size_t knownSymbols = symbols.size();
    std::vector<uint8_t> body;
    uint32_t number = 0;
    for (const SourceLine& line : document.lines)
    {
        ++number;
        // The record kept beside the line, when it still reads as the line's text against the symbols so far
        if (line.attrs.codec == _info.id && !line.attrs.bytes.empty())
        {
            std::string text;
            size_t n = 0;
            if (DecodeRecord(line.attrs.bytes, 0, symbols, text, n) && n == line.attrs.bytes.size() && text == line.text)
            {
                body.insert(body.end(), line.attrs.bytes.begin(), line.attrs.bytes.end());
                continue;
            }
        }
        std::vector<uint8_t> record;
        std::string error;
        if (!EncodeLine(line.text, symbols, record, error))
        {
            result.diagnostics.push_back({Severity::Error, number, 0, error});
            return result;
        }
        body.insert(body.end(), record.begin(), record.end());
    }
    if (table.empty() || symbols.size() != knownSymbols)
        table = WriteSymbols(symbols);
    // The two bytes the chained tape save leaves between the parts: the checksum of the first part (flag #FF and the
    // records, XOR) and the next part's flag #FF (every save of the corpus); LOAD checks them
    uint8_t checksum = 0xFF;
    for (const uint8_t b : body)
        checksum ^= b;
    result.bytes = std::move(body);
    result.bytes.push_back(checksum);
    result.bytes.push_back(0xFF);
    result.bytes.insert(result.bytes.end(), table.begin(), table.end());
    result.ok = true;
    return result;
}
}  // namespace unrealasm::codecs
