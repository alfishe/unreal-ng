#include "codecs/storm/stormcodec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

#include "encoding/utf8.h"

namespace unrealasm::codecs
{
namespace
{
constexpr char32_t kRawBase = 0xF700;   // a byte with no character: U+F700 + byte

// STORM's keyword table TBTK (the 1.3 sources, file DPC; the same 131 entries in every binary): code = index + #09
constexpr std::array<std::string_view, 131> kTokens = {
    "ORG", "EQU", "DI", "EI", "EXA", "NOP", "CCF", "SCF", "CPL", "DAA", "EXX", "RLA", "RRA", "RLCA", "RRCA", "HALT",
    "LDI", "LDD", "LDIR", "LDDR", "CPI", "CPD", "CPIR", "CPDR", "INI", "IND", "INIR", "INDR", "OUTI", "OUTD", "OTIR", "OTDR",
    "NEG", "RLD", "RRD", "INF", "RETI", "RETN", "", "B", "C", "D", "E", "H", "L", "(HL)", "A", "HX",
    "LX", "HY", "LY", "BC", "DE", "HL", "SP", "", "DEFB", "DEFW", "DEFS", "AF'", "XH", "XL", "YH", "YL",
    "", "LD", "INC", "DEC", "EX", "JR", "DJNZ", "JP", "CALL", "RET", "POP", "PUSH", "ADD", "ADC", "SUB", "SBC",
    "AND", "OR", "XOR", "CP", "IN", "OUT", "BIT", "RES", "SET", "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLI",
    "SRL", "IM", "RST", "DB", "DW", "DS", "IX", "IY", "(BC)", "(DE)", "I", "R", "AF", "(SP)", "(C)", "NZ",
    "Z", "NC", "C", "PO", "PE", "P", "M", "INCL", "INCB", "REPT", "ENDR", "IF", "IFU", "IFNU", "IFD", "IFND",
    "ELSE", "EIF", "ENDM",
};
constexpr uint8_t kFirstCode = 0x09;
constexpr uint8_t kExtended = 0x49;   // #49 n: INCL .. ENDM (code n + #80)
constexpr uint8_t kComment = 0x2F;
constexpr uint8_t kColumnZero = 0x06;
constexpr uint8_t kLocalLabel = 0x07;
constexpr uint8_t kString = 0xDC;
constexpr uint8_t kConditionC = 0x7B;

// Operators TBSIGN: infix 0..14, postfix 15..22
constexpr std::array<std::string_view, 23> kOperators = {
    "+", "-", "*", "/", "\\", "&", "!", "|", "<<", ">>", "<=", ">=", "<", ">", "=", "[", "]", "^", "`", "?", "~", "@", "'",
};
// Statement separators #2A-#2E as STORM shows them
constexpr std::array<std::string_view, 5> kSeparators = {":", " :", " : ", " :  ", "  : "};

std::string_view TokenName(uint8_t code)
{
    return code >= kFirstCode && static_cast<size_t>(code - kFirstCode) < kTokens.size() ? kTokens[code - kFirstCode] : std::string_view();
}

bool IsRegisterCode(uint8_t code)
{
    return (code >= 0x30 && code <= 0x3F) || (code >= 0x44 && code <= 0x48) || (code >= 0x6F && code <= 0x7F);
}

bool IsCommandCode(uint8_t code)
{
    return (code >= kFirstCode && code <= 0x2E) || (code >= 0x41 && code <= 0x43) || (code >= 0x4A && code <= 0x6E) || code >= 0x80;
}

int CodeOf(std::string_view name, bool registers)
{
    for (size_t i = 0; i < kTokens.size(); ++i)
    {
        const uint8_t code = static_cast<uint8_t>(i + kFirstCode);
        if (kTokens[i] == name && !kTokens[i].empty() && (registers ? IsRegisterCode(code) : IsCommandCode(code)))
            return code;
    }
    return -1;
}

void AppendByteChar(std::string& out, uint8_t b)
{
    if (b < 0x20)
        utf8::Append(out, kRawBase + b);
    else if (b < 0x80)
        out.push_back(static_cast<char>(b));
    else
        utf8::Append(out, encoding::ByteToCodePoint(b, encoding::CodePage::Cp866));
}

// ---------------------------------------------------------------------------------------------- decoding

struct Decoder
{
    std::span<const uint8_t> b;
    std::string o;
    bool clean = true;

    uint8_t At(size_t j)
    {
        if (j < b.size())
            return b[j];
        clean = false;
        return 0;
    }

    void Fill(size_t n)
    {
        while (o.size() < n)
            o.push_back(' ');
    }

    size_t Text(size_t j)   // text up to #00; #01-#1F = that many blanks
    {
        while (j < b.size() && b[j])
        {
            if (b[j] <= 0x1F)
                o.append(b[j], ' ');
            else
                AppendByteChar(o, b[j]);
            ++j;
        }
        if (j >= b.size())
            clean = false;
        return j + 1;
    }

    size_t Label(size_t j)
    {
        const uint8_t first = At(j);
        o.push_back(first == 0xDA ? '_' : first > 0xDA ? '=' : static_cast<char>(first - 0x7F));
        while (true)
        {
            const uint8_t cur = At(++j);
            const uint8_t c = cur & 0x3F;
            if (c <= 9)
                o.push_back(static_cast<char>('0' + c));
            else if (c == 0x0B)
                o.push_back('_');
            else if (c == 0x0A)
                ;   // the end of a one-character label
            else if (c < 0x26)
                o.push_back(static_cast<char>(c - 0x0C + 'A'));
            else
                o.push_back(static_cast<char>(c - 0x26 + 'a'));
            if (cur >= 0x80 || j + 1 >= b.size())
                break;
        }
        return j + 1;
    }

    size_t Expression(size_t j, uint8_t op, bool number, int depth = 0)
    {
        if (depth > 32)
        {
            clean = false;
            return b.size();
        }
        char buffer[24];
        while (j <= b.size())
        {
            if (number)
            {
                switch (op & 7)
                {
                    case 0:
                    {
                        o.push_back('(');
                        const uint8_t inner = At(j++);
                        j = Expression(j, inner, !(op & 0x10), depth + 1);
                        o.push_back(')');
                        break;
                    }
                    case 1: std::snprintf(buffer, sizeof(buffer), "#%02X", At(j)); o += buffer; j += 1; break;
                    case 2: o += std::to_string(At(j)); j += 1; break;
                    case 3:
                        if (At(j) < 0xC0)
                        {
                            const uint8_t x = At(j++);
                            if (x == 0)
                                o.push_back('$');
                            else
                                o += '=', o.push_back(static_cast<char>('0' + ((x - 1) & 7)));
                        }
                        else
                            j = Label(j);
                        break;
                    case 4:
                        o.push_back('"');
                        if (At(j + 1))
                            AppendByteChar(o, At(j + 1));
                        AppendByteChar(o, At(j));
                        o.push_back('"');
                        j += 2;
                        break;
                    case 5: std::snprintf(buffer, sizeof(buffer), "#%02X%02X", At(j + 1), At(j)); o += buffer; j += 2; break;
                    case 6: o += std::to_string(256 * At(j + 1) + At(j)); j += 2; break;
                    default:
                    {
                        auto bits = [&](uint8_t v) {
                            for (int k = 7; k >= 0; --k)
                                o.push_back(((v >> k) & 1) ? '1' : '0');
                        };
                        o.push_back('%');
                        if (At(j + 1))
                            bits(At(j + 1));
                        bits(At(j));
                        j += 2;
                    }
                }
                number = false;
                if (op & 8)
                    break;
                op = At(j++);
            }
            else
            {
                const uint8_t saved = op;
                if ((op & 0xF0) != 0xF0)
                {
                    o += kOperators[(op & 0xF0) >> 4];
                    number = true;
                }
                else
                {
                    o += kOperators[15 + (op & 7)];
                    number = false;
                    if (saved & 8)
                        break;
                    op = At(j++);
                }
            }
        }
        return j;
    }

    size_t Command(size_t j, bool indent)
    {
        if (indent)
            Fill(8);
        const uint8_t c = At(j);
        if (c >= kFirstCode && c < 0x2F)
            o += TokenName(c), ++j;
        else if (c > 0x2F && c < 0x40)
            o += "LD";
        else if (c >= 0x40 && c < 0x6F)
        {
            if (c == kExtended)
            {
                const std::string_view name = TokenName(static_cast<uint8_t>(At(j + 1) + 0x80));
                if (name.empty())
                    clean = false;
                o += name;
                j += 2;
            }
            else
            {
                if (TokenName(c).empty())
                    clean = false;
                o += TokenName(c);
                ++j;
            }
        }
        else if (c >= 0x6F && c < 0x75)
            o += "LD";
        else if (c == 0x75 || c == 0x76)
            o += "EX";
        else if (c == 0x77)
            o += "OUT";
        else if (c > 0x77 && c < 0x7C)
            o += "JR";
        else if (c >= 0x7C && c < 0x80)
            o += "JP";   // PO PE P M imply JP: STORM 1.3's binary has JP where its source shows these (reference decoders say CALL)
        else if (c >= 0x80 && c < kString)
            o += (c & 0x20) ? "LD" : "JR";
        else if (c == kString)
            o += "DB";
        else if (c > kString)
            o += "JR";
        return j;
    }

    void Line()
    {
        const size_t end = b.size();
        size_t i = 0;
        if (end == 0)
            return;
        if (b[0] == kComment)
        {
            o = ";";
            Text(1);
            return;
        }
        bool indent = true;
        const uint8_t b0 = b[0];
        if (b0 >= 0xC0 && b0 < kString)
        {
            if (At(1) & 0x40)
                i = Label(0);
        }
        else if (b0 == kColumnZero)
            o += '_', i = 1, indent = false;
        else if (b0 == kLocalLabel)
            o += '.' + std::to_string(At(1)), i = 2;
        bool done = i >= end;
        while (!done)
        {
            i = Command(i, indent);
            indent = false;
            char separator = ' ';
            while (true)
            {
                if (i >= end)
                {
                    done = true;
                    break;
                }
                if (b[i] == kComment)
                {
                    o.push_back(';');
                    i = Text(i + 1);
                    done = true;
                    break;
                }
                if (b[i] < 0x80)
                {
                    const uint8_t m = b[i] & 0x3F;
                    if (m >= 0x2A && m < 0x2F)
                    {
                        o += kSeparators[m - 0x2A];
                        ++i;
                        break;
                    }
                }
                o.push_back(separator);
                separator = ',';
                const uint8_t c = b[i];
                if (c < 0x80)
                {
                    if (TokenName(c).empty())
                        clean = false, o.push_back('?');
                    else
                        o += TokenName(c);
                    ++i;
                }
                else if (c < 0xC0)
                {
                    uint8_t e = b[i++];
                    const bool brackets = e & 0x20;
                    if (brackets)
                        o.push_back('(');
                    bool number = true;
                    if (!brackets)
                    {
                        number = !(e & 0x10);
                        if (!number)
                            e &= 0x1F;
                    }
                    if (brackets && (e & 0x10))
                    {
                        o += (e & 0x08) ? "IY" : "IX";
                        e &= 7;
                        if (!e)
                        {
                            o.push_back(')');
                            continue;
                        }
                        if ((e & 3) == 0)
                            e = At(i++);
                        else
                        {
                            if (e & 4)
                                e = (e & 3) | 0x10;
                            e |= 8;
                        }
                        number = false;
                    }
                    else if (!number)
                        e &= 0x1F;
                    i = Expression(i, e, number);
                    if (brackets)
                        o.push_back(')');
                }
                else if (c < kString)
                    i = Label(i);
                else if (c == kString)
                {
                    o.push_back('"');
                    i = Text(i + 1);
                    o.push_back('"');
                }
                else if (c <= 0xE6)
                    o.push_back(static_cast<char>(c - 0xAD)), ++i;
                else
                {
                    o.push_back('$');
                    if (c < 0xF3)
                        o += "-" + std::to_string(0xF3 - c);
                    else if (c > 0xF3)
                        o += "+" + std::to_string(c - 0xF3);
                    ++i;
                }
            }
        }
        if (i != end)
            clean = false;   // the line's bytes do not decode to exactly its length
    }
};

// ---------------------------------------------------------------------------------------------- encoding

struct Fail
{
    std::string reason;
};

struct Element
{
    enum Type
    {
        Number,
        Operator,
        Postfix,
        Sub
    } type = Number;
    uint8_t kind = 0;
    std::vector<uint8_t> payload;
    int index = 0;
    std::vector<Element> sub;
};

bool IsLabelStart(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsLabelChar(char c)
{
    return IsLabelStart(c) || (c >= '0' && c <= '9');
}

bool IsHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

struct Encoder
{
    const std::string& t;
    size_t i = 0;

    explicit Encoder(const std::string& text) : t(text) {}

    char Peek(size_t k = 0) const { return i + k < t.size() ? t[i + k] : '\0'; }
    bool StartsWith(std::string_view s, size_t at) const { return t.compare(at, s.size(), s) == 0; }

    // Text with #01-#1F blank runs; characters to CP866
    void Runs(std::string_view text, std::vector<uint8_t>& out) const
    {
        const auto* data = reinterpret_cast<const uint8_t*>(text.data());
        size_t k = 0;
        while (k < text.size())
        {
            if (text[k] == ' ')
            {
                size_t j = k;
                while (j < text.size() && text[j] == ' ' && j - k < 31)
                    ++j;
                out.push_back(static_cast<uint8_t>(j - k));
                k = j;
                continue;
            }
            char32_t cp = 0;
            const size_t length = utf8::DecodeOne(std::span<const uint8_t>(data + k, text.size() - k), cp);
            uint8_t byte = 0;
            if (length == 0)
                throw Fail{"invalid UTF-8"};
            if (cp >= kRawBase && cp <= kRawBase + 0xFF)
                byte = static_cast<uint8_t>(cp - kRawBase);
            else if (cp < 0x80)
                byte = static_cast<uint8_t>(cp);
            else if (!encoding::CodePointToByte(cp, encoding::CodePage::Cp866, byte))
                throw Fail{"a character STORM cannot hold (not in CP866)"};
            out.push_back(byte);
            k += length;
        }
    }

    static std::vector<std::string> SplitBlanks(std::string_view text)
    {
        std::vector<std::string> pieces;
        for (const char c : text)
        {
            if (c == ' ')
                pieces.emplace_back(" ");
            else if (pieces.empty() || pieces.back() == " ")
                pieces.emplace_back(1, c);
            else
                pieces.back().push_back(c);
        }
        return pieces;
    }

    static std::vector<uint8_t> LabelBytes(std::string_view name, bool definition)
    {
        const char first = name[0];
        if (!((first >= 'A' && first <= 'Z') || first == '_'))
            throw Fail{"a STORM label starts with a capital letter or _"};
        std::vector<uint8_t> out = {static_cast<uint8_t>(first == '_' ? 0xDA : first + 0x7F)};
        std::vector<uint8_t> codes;
        for (const char c : name.substr(1))
        {
            if (c >= '0' && c <= '9')
                codes.push_back(static_cast<uint8_t>(c - '0'));
            else if (c == '_')
                codes.push_back(0x0B);
            else if (c >= 'A' && c <= 'Z')
                codes.push_back(static_cast<uint8_t>(c - 'A' + 0x0C));
            else
                codes.push_back(static_cast<uint8_t>(c - 'a' + 0x26));
        }
        if (codes.empty())
            codes.push_back(0x0A);
        codes.back() |= 0x80;
        if (definition)
            codes.front() |= 0x40;
        out.insert(out.end(), codes.begin(), codes.end());
        return out;
    }

    std::string Label()
    {
        const size_t start = i;
        while (i < t.size() && IsLabelChar(t[i]))
            ++i;
        return t.substr(start, i - start);
    }

    Element Item()
    {
        Element e;
        if (Peek() == '#' && IsHex(Peek(1)))
        {
            size_t j = i + 1;
            while (j < t.size() && IsHex(t[j]))
                ++j;
            const unsigned v = static_cast<unsigned>(std::stoul(t.substr(i + 1, j - i - 1), nullptr, 16));
            e.kind = j - i - 1 <= 2 ? 1 : 5;
            e.payload = e.kind == 1 ? std::vector<uint8_t>{static_cast<uint8_t>(v)} : std::vector<uint8_t>{static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
            i = j;
            return e;
        }
        if (Peek() == '%' && (Peek(1) == '0' || Peek(1) == '1'))
        {
            size_t j = i + 1;
            unsigned v = 0;
            while (j < t.size() && (t[j] == '0' || t[j] == '1'))
                v = (v << 1) | static_cast<unsigned>(t[j++] - '0');
            e.kind = 7;
            e.payload = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
            i = j;
            return e;
        }
        if (Peek() >= '0' && Peek() <= '9')
        {
            size_t j = i;
            unsigned v = 0;
            while (j < t.size() && t[j] >= '0' && t[j] <= '9')
                v = v * 10 + static_cast<unsigned>(t[j++] - '0');
            e.kind = v < 256 ? 2 : 6;
            e.payload = v < 256 ? std::vector<uint8_t>{static_cast<uint8_t>(v)} : std::vector<uint8_t>{static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
            i = j;
            return e;
        }
        if (Peek() == '"')
        {
            const size_t close = t.find('"', i + 1);
            if (close != std::string::npos)
            {
                // A character constant: one or two characters, a blank is a blank here (not a run)
                std::vector<uint8_t> chars;
                for (const std::string& piece : SplitBlanks(std::string_view(t).substr(i + 1, close - i - 1)))
                {
                    if (piece == " ")
                        chars.push_back(' ');
                    else
                        Runs(piece, chars);
                }
                if (chars.size() >= 1 && chars.size() <= 2)
                {
                    e.kind = 4;
                    e.payload = {chars.back(), static_cast<uint8_t>(chars.size() == 2 ? chars[0] : 0)};
                    i = close + 1;
                    return e;
                }
            }
        }
        if (Peek() == '$')
        {
            ++i;
            e.kind = 3;
            e.payload = {0};
            return e;
        }
        if (Peek() == '=' && Peek(1) >= '0' && Peek(1) <= '7')
        {
            e.kind = 3;
            e.payload = {static_cast<uint8_t>(Peek(1) - '0' + 1)};
            i += 2;
            return e;
        }
        if (IsLabelStart(Peek()))
        {
            e.kind = 3;
            e.payload = LabelBytes(Label(), false);
            return e;
        }
        throw Fail{"an expression element STORM cannot read at column " + std::to_string(i + 1)};
    }

    std::vector<Element> Elements(std::string_view stop, bool expect = true)
    {
        std::vector<Element> elements;
        while (i < t.size() && stop.find(t[i]) == std::string_view::npos)
        {
            if (expect)
            {
                if ((Peek() == '+' || Peek() == '-') && elements.empty())
                {
                    Element op;
                    op.type = Element::Operator;
                    op.index = Peek() == '+' ? 0 : 1;
                    elements.push_back(op);
                    ++i;
                    continue;
                }
                if (Peek() == '(')
                {
                    ++i;
                    Element sub;
                    sub.type = Element::Sub;
                    sub.sub = Elements(")");
                    if (Peek() != ')')
                        throw Fail{"a ( without )"};
                    ++i;
                    elements.push_back(std::move(sub));
                    expect = false;
                    continue;
                }
                elements.push_back(Item());
                expect = false;
                continue;
            }
            int found = -1;
            size_t longest = 0;
            for (size_t k = 0; k < kOperators.size(); ++k)
                if (kOperators[k].size() > longest && StartsWith(kOperators[k], i))
                    found = static_cast<int>(k), longest = kOperators[k].size();
            if (found < 0)
                return elements;
            i += longest;
            Element op;
            op.type = found < 15 ? Element::Operator : Element::Postfix;
            op.index = found < 15 ? found : found - 15;
            elements.push_back(op);
            expect = found < 15;
        }
        return elements;
    }

    static uint8_t KindOf(const Element& e) { return e.type == Element::Sub ? 0 : e.kind; }

    void Put(std::vector<uint8_t>& out, uint8_t byte, const Element& e)
    {
        out.push_back(byte);
        if (e.type == Element::Sub)
        {
            // the inner stream starts with an operator when the byte that opens it has bit 4 set
            const std::vector<uint8_t> inner = (byte & 0x10) ? OperatorStream(e.sub) : Stream(e.sub, 0);
            out.insert(out.end(), inner.begin(), inner.end());
        }
        else
            out.insert(out.end(), e.payload.begin(), e.payload.end());
    }

    std::vector<uint8_t> Stream(const std::vector<Element>& els, uint8_t first)
    {
        std::vector<uint8_t> out;
        const size_t n = els.size();
        if (n == 0)
            throw Fail{"an empty expression"};
        size_t k = 0;
        if (els[0].type == Element::Operator)
        {
            if (els[0].index != 1 || n < 2 || els[1].type == Element::Operator || els[1].type == Element::Postfix)
                throw Fail{"an expression may start with a minus only"};
            Put(out, static_cast<uint8_t>(first | 0x10 | KindOf(els[1]) | (n == 2 ? 8 : 0)), els[1]);
            k = 2;
        }
        else
        {
            if (els[0].type == Element::Postfix)
                throw Fail{"an expression starts with a postfix operator"};
            Put(out, static_cast<uint8_t>(first | KindOf(els[0]) | (n == 1 ? 8 : 0)), els[0]);
            k = 1;
        }
        Continue(els, k, out);
        return out;
    }

    std::vector<uint8_t> OperatorStream(const std::vector<Element>& els)
    {
        if (els.empty() || (els[0].type != Element::Operator && els[0].type != Element::Postfix))
            throw Fail{"a sub-expression STORM stores with a leading operator"};
        std::vector<uint8_t> out;
        Continue(els, 0, out);
        return out;
    }

    void Continue(const std::vector<Element>& els, size_t k, std::vector<uint8_t>& out)
    {
        const size_t n = els.size();
        while (k < n)
        {
            const Element& e = els[k];
            if (e.type == Element::Operator)
            {
                if (k + 1 >= n || els[k + 1].type == Element::Operator || els[k + 1].type == Element::Postfix)
                    throw Fail{"an operator without an operand"};
                Put(out, static_cast<uint8_t>((e.index << 4) | KindOf(els[k + 1]) | (k + 1 == n - 1 ? 8 : 0)), els[k + 1]);
                k += 2;
            }
            else if (e.type == Element::Postfix)
            {
                out.push_back(static_cast<uint8_t>(0xF0 | e.index | (k == n - 1 ? 8 : 0)));
                ++k;
            }
            else
                throw Fail{"two operands without an operator"};
        }
    }

    bool OperandEndsAt(size_t at) const
    {
        return at >= t.size() || t[at] == ',' || t[at] == ':' || t[at] == ';' || StartsWith(" :", at) || StartsWith("  :", at);
    }

    bool WholeParenthesis(size_t at) const
    {
        int depth = 0;
        size_t j = at;
        for (; j < t.size(); ++j)
        {
            if (t[j] == '"')
            {
                const size_t close = t.find('"', j + 1);
                j = close == std::string::npos ? t.size() : close;
                if (j >= t.size())
                    break;
            }
            else if (t[j] == '(')
                ++depth;
            else if (t[j] == ')' && --depth == 0)
                break;
        }
        return OperandEndsAt(j + 1);
    }

    static bool EndsShortForm(char c)
    {
        return std::string_view("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_#%$(+-*/\\&!|<>=[]^`?~@'").find(c) == std::string_view::npos;
    }

    std::vector<uint8_t> Operand()
    {
        // Register and condition names, longest first
        size_t best = 0;
        int bestCode = -1;
        for (size_t k = 0; k < kTokens.size(); ++k)
        {
            const uint8_t code = static_cast<uint8_t>(k + kFirstCode);
            const std::string_view name = kTokens[k];
            if (name.empty() || !IsRegisterCode(code) || name.size() <= best || !StartsWith(name, i))
                continue;
            const size_t end = i + name.size();
            const char next = end < t.size() ? t[end] : '\0';
            if (std::isalnum(static_cast<unsigned char>(next)) || next == '_' || next == '\'')
                continue;
            if (CodeOf(name, true) != code)
                continue;   // a name listed twice (C): the first is the one written
            best = name.size();
            bestCode = code;
        }
        if (bestCode >= 0)
        {
            i += best;
            return {static_cast<uint8_t>(bestCode)};
        }
        if (StartsWith("(IX", i) || StartsWith("(IY", i))
        {
            const uint8_t iy = t[i + 2] == 'Y' ? 8 : 0;
            i += 3;
            if (Peek() == ')')
            {
                ++i;
                return {static_cast<uint8_t>(0xB0 | iy)};
            }
            std::vector<Element> els = Elements(")", false);
            if (Peek() != ')')
                throw Fail{"(IX / (IY without )"};
            ++i;
            if (els.size() == 2 && els[0].type == Element::Operator && els[0].index <= 1 && els[1].type == Element::Number && els[1].kind >= 1 &&
                els[1].kind <= 3)
            {
                std::vector<uint8_t> out = {static_cast<uint8_t>(0xB0 | iy | els[1].kind | (els[0].index == 1 ? 4 : 0))};
                out.insert(out.end(), els[1].payload.begin(), els[1].payload.end());
                return out;
            }
            std::vector<uint8_t> out = {static_cast<uint8_t>(0xB4 | iy)};
            const std::vector<uint8_t> rest = OperatorStream(els);
            out.insert(out.end(), rest.begin(), rest.end());
            return out;
        }
        if (Peek() == '"')
        {
            const size_t close = t.find('"', i + 1);
            if (close != std::string::npos && close - i - 1 > 2 && OperandEndsAt(close + 1))
            {
                std::vector<uint8_t> out = {kString};
                Runs(std::string_view(t).substr(i + 1, close - i - 1), out);
                out.push_back(0);
                i = close + 1;
                return out;
            }
        }
        if (Peek() == '(' && WholeParenthesis(i))
        {
            ++i;
            std::vector<Element> els = Elements(")");
            if (Peek() != ')')
                throw Fail{"( without )"};
            ++i;
            return Stream(els, 0xA0);
        }
        if (Peek() >= '0' && Peek() <= '9' && EndsShortForm(Peek(1)))
        {
            const uint8_t digit = static_cast<uint8_t>(Peek() - '0');
            ++i;
            return {static_cast<uint8_t>(0xDD + digit)};
        }
        if (Peek() == '$')
        {
            size_t j = i + 1;
            int offset = 0;
            if (j < t.size() && (t[j] == '+' || t[j] == '-') && j + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[j + 1])))
            {
                const int sign = t[j] == '-' ? -1 : 1;
                ++j;
                int v = 0;
                while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j])))
                    v = v * 10 + (t[j++] - '0');
                offset = sign * v;
            }
            if ((j >= t.size() || EndsShortForm(t[j])) && offset >= -12 && offset <= 12)
            {
                i = j;
                return {static_cast<uint8_t>(0xF3 + offset)};
            }
        }
        if (IsLabelStart(Peek()))
        {
            const size_t save = i;
            const std::string name = Label();
            if (OperandEndsAt(i))
                return LabelBytes(name, false);
            i = save;
        }
        const std::vector<Element> els = Elements(",:; ");
        if (els.empty())
            throw Fail{"an operand STORM cannot read at column " + std::to_string(i + 1)};
        return Stream(els, 0x80);
    }

    int SeparatorAt() const
    {
        int found = -1;
        size_t longest = 0;
        for (size_t k = 0; k < kSeparators.size(); ++k)
            if (kSeparators[k].size() > longest && StartsWith(kSeparators[k], i))
                found = static_cast<int>(k), longest = kSeparators[k].size();
        return found;
    }

    std::vector<uint8_t> Statement()
    {
        size_t j = i;
        while (j < t.size() && ((t[j] >= 'A' && t[j] <= 'Z') || (j > i && t[j] >= '0' && t[j] <= '9')))
            ++j;
        const std::string command = t.substr(i, j - i);
        const int code = command.empty() ? -1 : CodeOf(command, false);
        if (code < 0)
            throw Fail{"not a STORM command: " + command};
        i = j;
        std::vector<std::vector<uint8_t>> operands;
        if (Peek() == ' ' && SeparatorAt() < 0)
        {
            ++i;
            while (true)
            {
                if (operands.empty() && command == "JR" && Peek() == 'C' && OperandEndsAt(i + 1))
                {
                    ++i;
                    operands.push_back({kConditionC});   // JR C: the condition (elsewhere C is the register, #31)
                }
                else
                    operands.push_back(Operand());
                if (Peek() == ',')
                {
                    ++i;
                    continue;
                }
                break;
            }
        }
        std::vector<uint8_t> out;
        const int first = operands.empty() ? -1 : operands[0][0];
        bool implicit = false;
        if (first >= 0)
        {
            if (command == "LD")
                implicit = (first >= 0x30 && first < 0x40) || (first >= 0x6F && first < 0x75) || (first >= 0x80 && first < 0xC0 && (first & 0x20));
            else if (command == "EX")
                implicit = first == 0x75 || first == 0x76;
            else if (command == "OUT")
                implicit = first == 0x77;
            else if (command == "JR")
                implicit = (first >= 0x78 && first < 0x7B) || (first >= 0xC0 && first < kString) || (first >= 0x80 && first < 0xC0 && !(first & 0x20)) ||
                           first > kString;
            else if (command == "JP")
                implicit = first >= 0x7C && first < 0x80;
        }
        if (!implicit)
        {
            if (code >= 0x80)
                out = {kExtended, static_cast<uint8_t>(code - 0x80)};
            else
                out = {static_cast<uint8_t>(code)};
        }
        for (const auto& operand : operands)
            out.insert(out.end(), operand.begin(), operand.end());
        return out;
    }

    std::vector<uint8_t> Line()
    {
        std::vector<uint8_t> out;
        if (t.empty())
            return out;
        if (t[0] == ';')
        {
            out.push_back(kComment);
            Runs(std::string_view(t).substr(1), out);
            out.push_back(0);
            return out;
        }
        if (t[0] == '_' && t.size() > 1 && t[1] >= 'A' && t[1] <= 'Z')
        {
            size_t j = 1;
            while (j < t.size() && ((t[j] >= 'A' && t[j] <= 'Z') || (t[j] >= '0' && t[j] <= '9')))
                ++j;
            if (CodeOf(t.substr(1, j - 1), false) >= 0 && (j >= t.size() || t[j] == ' ' || t[j] == ':' || t[j] == ';'))
            {
                out.push_back(kColumnZero);   // a command in column 0
                i = 1;
            }
        }
        if (i == 0 && t[0] == '.')
        {
            size_t j = 1;
            unsigned v = 0;
            while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j])))
                v = v * 10 + static_cast<unsigned>(t[j++] - '0');
            out.push_back(kLocalLabel);
            out.push_back(static_cast<uint8_t>(v));
            i = j;
        }
        else if (i == 0 && t[0] != ' ')
        {
            if (!IsLabelStart(t[0]))
                throw Fail{"a line starts with a label, a blank or ;"};
            const std::vector<uint8_t> label = LabelBytes(Label(), true);
            out.insert(out.end(), label.begin(), label.end());
        }
        while (Peek() == ' ')
            ++i;
        while (i < t.size())
        {
            if (Peek() == ';')
            {
                out.push_back(kComment);
                Runs(std::string_view(t).substr(i + 1), out);
                out.push_back(0);
                break;
            }
            const std::vector<uint8_t> statement = Statement();
            out.insert(out.end(), statement.begin(), statement.end());
            if (i >= t.size() || Peek() == ';')
                continue;
            const int separator = SeparatorAt();
            if (separator < 0)
                throw Fail{"unexpected text at column " + std::to_string(i + 1)};
            out.push_back(static_cast<uint8_t>(0x2A + separator));
            i += kSeparators[separator].size();
        }
        return out;
    }
};

struct Version
{
    std::string_view id;
    std::string_view title;
    uint16_t start;   ///< where the text starts in memory: the catalog's start field of a saved source
};

constexpr std::array<Version, 2> kVersions = {{
    {"1.0", "STORM 1.0beta (text from #C003)", 0xC003},
    {"1.3", "STORM 1.2 / 1.3 / 1.3+ / 1.3i (text from #C00B)", 0xC00B},
}};
}  // namespace

StormCodec::StormCodec() : _info{"storm", "STORM source (tokenized)", "storm", CodecFamily::Tokenized, {}}
{
    for (const Version& v : kVersions)
        _info.subversions.push_back({std::string(v.id), std::string(v.title)});
}

std::vector<std::pair<size_t, size_t>> StormCodec::Lines(std::span<const uint8_t> bytes, size_t& rest, bool& framingOk)
{
    std::vector<std::pair<size_t, size_t>> lines;
    long p = static_cast<long>(bytes.size()) - 1;
    while (p >= 0)
    {
        const long length = bytes[static_cast<size_t>(p)] & 0x3F;
        if (p - length < 0)
            break;
        lines.emplace_back(static_cast<size_t>(p - length), static_cast<size_t>(length));
        p -= length + 1;
    }
    std::reverse(lines.begin(), lines.end());
    framingOk = p == -1;
    rest = static_cast<size_t>(p + 1);
    return lines;
}

std::string StormCodec::DecodeLine(std::span<const uint8_t> body, bool* clean)
{
    Decoder d{body, {}, true};
    d.Line();
    if (clean)
        *clean = d.clean;
    return d.o;
}

bool StormCodec::EncodeLine(const std::string& text, std::vector<uint8_t>& body, std::string& error)
{
    try
    {
        Encoder encoder(text);
        body = encoder.Line();
    }
    catch (const Fail& f)
    {
        error = f.reason;
        return false;
    }
    if (body.size() > 0x3F)
    {
        error = "the line is longer than a STORM line can hold (63 bytes)";
        return false;
    }
    return true;
}

int StormCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    size_t rest = 0;
    bool framingOk = false;
    const auto lines = Lines(bytes, rest, framingOk);
    const bool catalog = (hints.type == 'C' && (hints.start == 0xC00B || hints.start == 0xC003)) || (hints.type == 'R' && hints.start == 0xC00B);
    if (!framingOk || lines.empty())
        return catalog ? 30 : 0;
    for (const auto& [start, length] : lines)
    {
        bool clean = true;
        DecodeLine(bytes.subspan(start, length), &clean);
        if (!clean)
            return catalog ? 40 : 0;
    }
    if (catalog)
        return 95;
    // A catalog entry with another start is no STORM text (every one found starts at #C00B): zeros and other data
    // also walk back as STORM lines (a screen at #4000 reads as empty lines and CCF)
    if (hints.type != 0)
        return 20;
    return lines.size() >= 3 ? 70 : 40;
}

DecodeResult StormCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    SourceDocument& document = result.document;
    document.format = _info.id;
    document.dialect = _info.dialect;
    document.codePage = encoding::CodePage::Cp866;
    document.lineEnd = encoding::LineEnd::Lf;
    // The version only decides where the text starts in memory (the catalog's start); the lines are the same
    if (!options.subversion.empty())
        document.subversion = options.subversion, result.subversions = {options.subversion};
    else if (options.catalog.start == 0xC003)
        document.subversion = "1.0", result.subversions = {"1.0"};
    else if (options.catalog.start == 0xC00B)
        document.subversion = "1.3", result.subversions = {"1.3"};
    else
        document.subversion = "1.3", result.subversions = {"1.0", "1.3"};
    size_t rest = 0;
    bool framingOk = false;
    const auto lines = Lines(bytes, rest, framingOk);
    if (!framingOk)
        result.diagnostics.push_back({Severity::Warning, 0, 0, "the lines walked back from the end do not reach the start of the file"});
    for (size_t k = 0; k < lines.size(); ++k)
    {
        const auto [start, length] = lines[k];
        bool clean = true;
        SourceLine line;
        line.text = DecodeLine(bytes.subspan(start, length), &clean);
        line.attrs = {_info.id, std::vector<uint8_t>(bytes.begin() + static_cast<std::ptrdiff_t>(start),
                                                     bytes.begin() + static_cast<std::ptrdiff_t>(start + length + 1))};
        if (!clean)
            result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(k + 1), start, "a line STORM's layout does not fully explain"});
        document.lines.push_back(std::move(line));
    }
    document.attrs = {_info.id, std::vector<uint8_t>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(rest))};
    result.ok = true;
    return result;
}

EncodeResult StormCodec::Encode(const SourceDocument& document, const EncodeOptions&) const
{
    EncodeResult result;
    const bool own = document.format == _info.id;
    if (own && document.attrs.codec == _info.id)
        result.bytes = document.attrs.bytes;
    std::vector<uint8_t> body;
    for (size_t i = 0; i < document.lines.size(); ++i)
    {
        const SourceLine& line = document.lines[i];
        const auto& kept = line.attrs.bytes;
        if (own && line.attrs.codec == _info.id && !kept.empty() && DecodeLine(std::span<const uint8_t>(kept).first(kept.size() - 1)) == line.text)
        {
            result.bytes.insert(result.bytes.end(), kept.begin(), kept.end());
            continue;
        }
        std::string error;
        if (!EncodeLine(line.text, body, error))
        {
            result.diagnostics.push_back({Severity::Error, static_cast<uint32_t>(i + 1), result.bytes.size(), error});
            continue;
        }
        result.bytes.insert(result.bytes.end(), body.begin(), body.end());
        result.bytes.push_back(static_cast<uint8_t>(body.size()));
    }
    result.ok = !HasErrors(result.diagnostics);
    return result;
}
}  // namespace unrealasm::codecs
