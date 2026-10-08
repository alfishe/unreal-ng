#include "codecs/lasergenius/lasergeniuscodec.h"

#include <cctype>
#include <cstring>

namespace unrealasm::codecs
{
namespace
{
constexpr uint8_t kParagraphEnd = 0xF7;
constexpr uint8_t kCommentEnd = 0xF0;
constexpr uint8_t kLabel = 0xF1;
constexpr uint8_t kMacroParameter = 0xF2;
constexpr uint8_t kDirective = 0xF5;
constexpr uint8_t kName = 0xEC;
constexpr uint8_t kString = 0xED;
constexpr uint8_t kCharacter = 0xEB;
constexpr uint8_t kComment = 0xEE;       // after a statement
constexpr uint8_t kCommentLine = 0xEF;   // a sentence that is only a comment
constexpr uint8_t kMacroCall = 0x81;
constexpr uint8_t kOpenBracket = 0xCB;
constexpr uint8_t kCloseBracket = 0xD0;
constexpr uint8_t kLocation = 0xE9;      // $
constexpr uint8_t kStorage = 0xEA;       // .
constexpr uint8_t kFirstMnemonic = 0x2C;
constexpr uint8_t kDiskHeader = 0xAF;
constexpr size_t kDiskHeaderSize = 9;    // #AF, length, address, four characters of the name

// The editor's keyword table from #2C: the mnemonics and pseudo-ops, in its order
const char* const kMnemonics[] = {
    "CCF",  "CPL",  "DAA",  "DI",   "EI",   "EXX",  "HALT", "NOP",  "RLA",  "RLCA", "RRA",  "RRCA", "SCF",  "LDIR",
    "LDDR", "CPIR", "CPDR", "INIR", "INDR", "OTIR", "OTDR", "LDI",  "LDD",  "CPI",  "CPD",  "INI",  "IND",  "OUTI",
    "OUTD", "RLD",  "RRD",  "RETI", "RETN", "NEG",  "ELSE", "ENDC", "ENDM", "DEC",  "INC",  "JP",   "JR",   "CALL",
    "ADC",  "ADD",  "SBC",  "IN",   "OUT",  "EX",   "LD",   "ORG",  "AND",  "OR",   "XOR",  "SUB",  "CP",   "PUSH",
    "POP",  "DJNZ", "RET",  "BIT",  "SET",  "RES",  "RLC",  "RL",   "RRC",  "RR",   "SLA",  "SRA",  "SRL",  "IM",
    "RST",  "DB",   "DEFB", "DEFM", "DW",   "DEFW", "DL",   "DEFL", "EQU",  "DS",   "DEFS", "PUT",  "COND", "MACRO",
};
constexpr size_t kMnemonicCount = sizeof(kMnemonics) / sizeof(kMnemonics[0]);

// Operand tokens #05-#2B: registers, conditions, the indirect forms; the last five open a parenthesis an expression
// follows ((IX+ (IX- (IY+ (IY- and a memory operand)
const char* const kRegisters[] = {
    "BC", "DE", "HL", "SP", "AF'", "AF", "A", "B", "C", "D", "E", "H", "L", "IX", "IY", "NZ", "Z", "NC", "M", "PO",
    "PE", "P", "I", "R", "F", "ON", "OFF",
};
constexpr uint8_t kFirstRegister = 0x05;
constexpr uint8_t kLastRegister = 0x1F;
const char* const kIndirect[] = {"(BC)", "(DE)", "(HL)", "(SP)", "(IX)", "(IY)", "(C)", "(IX+", "(IX-", "(IY+", "(IY-", "("};
constexpr uint8_t kFirstIndirect = 0x20;
constexpr uint8_t kLastIndirect = 0x2B;
constexpr uint8_t kFirstOpening = 0x27;   // (IX+ ... (: an expression and a closing parenthesis follow

// A mnemonic with its first operand in one token, #82-#C3
struct Combined
{
    uint8_t mnemonic;
    uint8_t operand;
};
constexpr uint8_t kJp = 0x53, kJr = 0x54, kCall = 0x55, kAdc = 0x56, kAdd = 0x57, kSbc = 0x58, kIn = 0x59, kOut = 0x5A,
                  kEx = 0x5B, kLd = 0x5C;
constexpr uint8_t kRegA = 0x0B, kRegHl = 0x07, kZ = 0x15, kNz = 0x14, kC = 0x0D, kNc = 0x16, kM = 0x17, kP = 0x1A,
                  kPe = 0x19, kPo = 0x18;
const Combined kCombined[] = {
    {kJp, kZ},     {kJp, kNz},    {kJp, kC},     {kJp, kNc},    {kJp, kM},     {kJp, kP},     {kJp, kPe},
    {kJp, kPo},    {kJp, 0x22},   {kJp, 0x24},   {kJp, 0x25},   {kJr, kZ},     {kJr, kNz},    {kJr, kC},
    {kJr, kNc},    {kCall, kZ},   {kCall, kNz},  {kCall, kC},   {kCall, kNc},  {kCall, kM},   {kCall, kP},
    {kCall, kPe},  {kCall, kPo},  {kAdc, kRegA}, {kAdc, kRegHl}, {kAdd, kRegA}, {kAdd, kRegHl}, {kAdd, 0x12},
    {kAdd, 0x13},  {kSbc, kRegA}, {kSbc, kRegHl}, {kIn, 0x0B},   {kIn, 0x0C},   {kIn, 0x0D},   {kIn, 0x0E},
    {kIn, 0x0F},   {kIn, 0x10},   {kIn, 0x11},   {kOut, 0x26},  {kOut, 0x2B},  {kEx, 0x06},   {kEx, 0x0A},
    {kEx, 0x23},   {kLd, 0x20},   {kLd, 0x21},   {kLd, 0x22},   {kLd, 0x29},   {kLd, 0x2A},   {kLd, 0x27},
    {kLd, 0x28},   {kLd, 0x2B},   {kLd, 0x0B},   {kLd, 0x0C},   {kLd, 0x0D},   {kLd, 0x0E},   {kLd, 0x0F},
    {kLd, 0x10},   {kLd, 0x11},   {kLd, 0x05},   {kLd, 0x06},   {kLd, 0x07},   {kLd, 0x13},   {kLd, 0x12},
    {kLd, 0x1B},   {kLd, 0x1C},   {kLd, 0x08},
};
constexpr uint8_t kFirstCombined = 0x82;
constexpr size_t kCombinedCount = sizeof(kCombined) / sizeof(kCombined[0]);

const char* const kUnary[] = {"*", "!", "^", "-"};   // #C7-#CA
constexpr uint8_t kFirstUnary = 0xC7;
const char* const kBinary[] = {"+", "-", "*", "/", "%", "<=", ">=", "?=", "!=", "<<", ">>", "<", ">", "@<", "@>", "&&", "||", "&", "|", "^"};   // #D5-#E8
constexpr uint8_t kFirstBinary = 0xD5;
constexpr size_t kBinaryCount = sizeof(kBinary) / sizeof(kBinary[0]);

// Assembler directives "*name", #F5 and #82-#95
const char* const kDirectives[] = {"LIST", "LLIST", "COUNT", "SCREEN", "PRINTER", "INCLUDE", "OPENOUT", "CLOSEOUT", "MACLIST", "WHILE",
                                   "ENDW", "REPEAT", "UNTIL", "FORM", "REPORT", "TITLE", "PAUSE", "PRINT", "CODE", "PROMPTS"};
constexpr uint8_t kFirstDirective = 0x82;
constexpr size_t kDirectiveCount = sizeof(kDirectives) / sizeof(kDirectives[0]);

bool IsNameChar(uint8_t c)
{
    return std::isalnum(c) || c == '_' || c == '.' || c == '$';
}

std::string Upper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string Hex(unsigned value, int digits)
{
    static const char* const kDigits = "0123456789ABCDEF";
    std::string out(static_cast<size_t>(digits), '0');
    for (int k = digits - 1; k >= 0; --k, value >>= 4)
        out[static_cast<size_t>(k)] = kDigits[value & 15];
    return out;
}

std::string Radix(unsigned value, unsigned base, size_t minDigits)
{
    std::string out;
    do
    {
        out.insert(out.begin(), static_cast<char>('0' + value % base));
        value /= base;
    } while (value);
    while (out.size() < minDigits)
        out.insert(out.begin(), '0');
    return out;
}

/// Reads the tokens of a paragraph's statements (decoding)
class Reader
{
public:
    Reader(std::span<const uint8_t> bytes, size_t end) : _b(bytes), _end(end) {}

    bool Statement(size_t& pos, std::string& text)
    {
        _pos = pos;
        text.clear();
        if (!Has(1))
            return false;
        if (Peek() == kLabel)
        {
            ++_pos;
            std::string name;
            if (!Name(name))
                return false;
            text = name + ":";
        }
        std::string head;
        std::vector<std::string> operands;
        const uint8_t c = Has(1) ? Peek() : kParagraphEnd;
        if (c >= kFirstMnemonic && c < kFirstMnemonic + kMnemonicCount)
        {
            head = kMnemonics[c - kFirstMnemonic];
            ++_pos;
        }
        else if (c >= kFirstCombined && c < kFirstCombined + kCombinedCount)
        {
            const Combined& m = kCombined[c - kFirstCombined];
            head = kMnemonics[m.mnemonic - kFirstMnemonic];
            ++_pos;
            std::string first;
            if (!OperandAfter(m.operand, first))
                return false;
            operands.push_back(first);
        }
        else if (c == kDirective)
        {
            if (!Has(2) || _b[_pos + 1] < kFirstDirective || _b[_pos + 1] >= kFirstDirective + kDirectiveCount)
                return false;
            head = std::string("*") + kDirectives[_b[_pos + 1] - kFirstDirective];
            _pos += 2;
        }
        else if (c == kMacroCall)
        {
            ++_pos;
            std::string name;
            if (!Name(name))
                return false;
            head = "\\" + name;
        }
        if (!head.empty())
        {
            while (Has(1) && OperandStart(Peek()))
            {
                std::string o;
                if (!Operand(o))
                    return false;
                operands.push_back(o);
            }
            text += (text.empty() ? "" : " ") + head;
            for (size_t k = 0; k < operands.size(); ++k)
                text += (k ? "," : " ") + operands[k];
        }
        // A comment after the statement (#EE), or a sentence that is only a comment (#EF; after a statement it starts
        // the next one)
        if (Has(1) && (Peek() == kComment || (Peek() == kCommentLine && text.empty())))
        {
            const bool line = Peek() == kCommentLine;
            ++_pos;
            std::string comment;
            while (Has(1) && Peek() != kCommentEnd && Peek() != kParagraphEnd)
                comment.push_back(static_cast<char>(_b[_pos++]));
            if (!Has(1))
                return false;
            if (Peek() == kCommentEnd)
                ++_pos;
            if (line && !text.empty())
                return false;
            text += (text.empty() ? ";" : " ;") + encoding::ToUtf8(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(comment.data()), comment.size()), encoding::CodePage::ZxSpectrum);
        }
        if (_pos == pos)
            return false;   // no statement starts here
        pos = _pos;
        return true;
    }

private:
    bool Has(size_t n) const { return _pos + n <= _end; }
    uint8_t Peek() const { return _b[_pos]; }

    bool Name(std::string& out)
    {
        out.clear();
        while (Has(1) && Peek() < 0x80)
        {
            if (!IsNameChar(Peek()))
                return false;
            out.push_back(static_cast<char>(_b[_pos++]));
        }
        if (!Has(1) || !IsNameChar(Peek() & 0x7F))
            return false;
        out.push_back(static_cast<char>(_b[_pos++] & 0x7F));
        return true;
    }

    static bool OperandStart(uint8_t c)
    {
        return (c >= kFirstRegister && c <= kLastIndirect) || c == kName || c == kString || c == kCharacter || c == kMacroParameter ||
               c == kLocation || c == kStorage || c >= 0xF8 || (c >= kFirstUnary && c < kFirstUnary + 4) ||
               c == kOpenBracket;
    }

    bool OperandAfter(uint8_t reg, std::string& out)
    {
        if (reg >= kFirstRegister && reg <= kLastRegister)
        {
            out = kRegisters[reg - kFirstRegister];
            return true;
        }
        if (reg < kFirstIndirect || reg > kLastIndirect)
            return false;
        out = kIndirect[reg - kFirstIndirect];
        if (reg < kFirstOpening)
            return true;
        std::string e;
        if (!Expression(e))
            return false;
        out += e + ")";
        return true;
    }

    bool Operand(std::string& out)
    {
        const uint8_t c = Peek();
        if (c >= kFirstRegister && c <= kLastIndirect)
        {
            ++_pos;
            return OperandAfter(c, out);
        }
        return Expression(out);
    }

    bool Expression(std::string& out)
    {
        out.clear();
        for (;;)
        {
            while (Has(1) && ((Peek() >= kFirstUnary && Peek() < kFirstUnary + 4) || Peek() == kOpenBracket))
            {
                out += Peek() == kOpenBracket ? "[" : kUnary[Peek() - kFirstUnary];
                ++_pos;
            }
            std::string a;
            if (!Atom(a))
                return false;
            out += a;
            while (Has(1) && Peek() == kCloseBracket)
            {
                out += "]";
                ++_pos;
            }
            if (Has(1) && Peek() >= kFirstBinary && Peek() < kFirstBinary + kBinaryCount)
            {
                out += kBinary[Peek() - kFirstBinary];
                ++_pos;
                continue;
            }
            return true;
        }
    }

    bool Atom(std::string& out)
    {
        if (!Has(1))
            return false;
        const uint8_t c = _b[_pos++];
        auto byte = [&](unsigned& v) {
            if (!Has(1))
                return false;
            v = _b[_pos++];
            return true;
        };
        auto word = [&](unsigned& v) {
            if (!Has(2))
                return false;
            v = static_cast<unsigned>(_b[_pos] | (_b[_pos + 1] << 8));
            _pos += 2;
            return true;
        };
        unsigned v = 0;
        switch (c)
        {
        case kName:
            return Name(out);
        case kMacroParameter:
            if (!Name(out))
                return false;
            out = "\\" + out;
            return true;
        case kLocation:
            out = "$";
            return true;
        case kStorage:
            out = ".";
            return true;
        case kCharacter:
            if (!byte(v))
                return false;
            if (v == 0xFD)
            {
                if (!byte(v))
                    return false;
                out = "\"\\" + std::to_string(v) + "\"";
            }
            else if (v == '"' || v == '\\')
                out = std::string("\"\\") + static_cast<char>(v) + "\"";
            else if (v < 0x20 || v >= 0x80)
                return false;
            else
                out = std::string("\"") + static_cast<char>(v) + "\"";
            return true;
        case kString:
        {
            std::string s;
            while (Has(1) && Peek() != 0)
            {
                const uint8_t ch = _b[_pos++];
                if (ch == 0xFD)
                {
                    if (!byte(v))
                        return false;
                    s += "\\" + std::to_string(v);
                }
                else if (ch == '"' || ch == '\\')
                    s += std::string("\\") + static_cast<char>(ch);
                else if (ch < 0x20 || ch >= 0x80)
                    return false;
                else
                    s.push_back(static_cast<char>(ch));
            }
            if (!Has(1))
                return false;
            ++_pos;
            out = "\"" + s + "\"";
            return true;
        }
        case 0xFD:
            if (!byte(v))
                return false;
            out = std::to_string(v);
            return true;
        case 0xFC:   // a word form whose value fits a byte is written with four digits so it is read back as a word
            if (!word(v))
                return false;
            out = v < 256 ? Radix(v, 10, 4) : std::to_string(v);
            return true;
        case 0xFF:
            if (!byte(v))
                return false;
            out = "#" + Hex(v, 2);
            return true;
        case 0xFE:
            if (!word(v))
                return false;
            out = "#" + Hex(v, 4);
            return true;
        case 0xF9:
            if (!byte(v))
                return false;
            out = "%" + Radix(v, 2, 1);
            return true;
        case 0xF8:
            if (!word(v))
                return false;
            out = "%" + Radix(v, 2, 16);
            return true;
        case 0xFB:
            if (!byte(v))
                return false;
            out = "@" + Radix(v, 8, 1);
            return true;
        case 0xFA:
            if (!word(v))
                return false;
            out = "@" + Radix(v, 8, 4);
            return true;
        default:
            return false;
        }
    }

    std::span<const uint8_t> _b;
    size_t _end;
    size_t _pos = 0;
};

/// The end of a paragraph whose tokens are not all known: the next #F7, skipping the tokens whose length is known
size_t SkipRaw(std::span<const uint8_t> b, size_t pos, size_t end)
{
    while (pos < end && b[pos] != kParagraphEnd)
    {
        const uint8_t c = b[pos++];
        if (c == kName || c == kLabel || c == kMacroParameter || c == kMacroCall)
        {
            while (pos < end && b[pos] < 0x80)
                ++pos;
            ++pos;
        }
        else if (c == kString)
        {
            while (pos < end && b[pos] != 0)
                pos += b[pos] == 0xFD ? 2 : 1;
            ++pos;
        }
        else if (c == kComment || c == kCommentLine)
        {
            while (pos < end && b[pos] != kCommentEnd && b[pos] != kParagraphEnd)
                ++pos;
        }
        else if (c == 0xF8 || c == 0xFA || c == 0xFC || c == 0xFE)
            pos += 2;
        else if (c == 0xF9 || c == 0xFB || c == 0xFD || c == 0xFF || c == kDirective)
            ++pos;
        else if (c == kCharacter)
            pos += pos < end && b[pos] == 0xFD ? 2 : 1;
    }
    return pos;
}

std::string RawText(std::span<const uint8_t> bytes)
{
    std::string out = "{";
    for (size_t k = 0; k < bytes.size(); ++k)
        out += (k ? " " : "") + Hex(bytes[k], 2);
    return out + "}";
}

/// Writes the tokens of one statement's text (encoding)
class Writer
{
public:
    Writer(const std::string& text, std::vector<uint8_t>& out) : _s(text), _out(out) {}

    bool Statement(std::string& error)
    {
        Skip();
        // A paragraph the codec could not read, kept as its bytes
        if (Peek() == '{' && !_s.empty() && _s.back() == '}')
            return Raw(error);
        const size_t start = _out.size();
        if (Peek() == ';')
            return Comment(kCommentLine, error);
        // A label: a name and a colon
        {
            const size_t save = _pos;
            std::string name = Word();
            Skip();
            if (!name.empty() && Peek() == ':')
            {
                ++_pos;
                _out.push_back(kLabel);
                PutName(name);
            }
            else
                _pos = save;
        }
        Skip();
        if (AtEnd() || Peek() == ';')
            return Finish(start, error);
        if (Peek() == '*')
        {
            ++_pos;
            const std::string word = Upper(Word());
            for (size_t k = 0; k < kDirectiveCount; ++k)
                if (word == kDirectives[k])
                {
                    _out.push_back(kDirective);
                    _out.push_back(static_cast<uint8_t>(kFirstDirective + k));
                    return Operands(start, error);
                }
            error = "unknown directive *" + word;
            return false;
        }
        if (Peek() == '\\')
        {
            ++_pos;
            const std::string name = Word();
            if (name.empty())
            {
                error = "a macro name expected after \\";
                return false;
            }
            _out.push_back(kMacroCall);
            PutName(name);
            return Operands(start, error);
        }
        const std::string word = Upper(Word());
        size_t mnemonic = kMnemonicCount;
        for (size_t k = 0; k < kMnemonicCount; ++k)
            if (word == kMnemonics[k])
                mnemonic = k;
        if (mnemonic == kMnemonicCount)
        {
            error = "unknown mnemonic: " + word;
            return false;
        }
        const uint8_t token = static_cast<uint8_t>(kFirstMnemonic + mnemonic);
        // The first operand joins the mnemonic when the editor has a token for both
        Skip();
        const size_t save = _pos;
        uint8_t first = 0;
        if (!AtEnd() && Peek() != ';' && OperandHead(first))
        {
            for (size_t k = 0; k < kCombinedCount; ++k)
                if (kCombined[k].mnemonic == token && kCombined[k].operand == first)
                {
                    _out.push_back(static_cast<uint8_t>(kFirstCombined + k));
                    if (first >= kFirstOpening && first <= kLastIndirect && !Closed(error))
                        return false;
                    Skip();
                    if (Peek() == ',')
                        ++_pos;
                    return Operands(start, error);
                }
        }
        _pos = save;
        _out.push_back(token);
        return Operands(start, error);
    }

private:
    bool AtEnd() const { return _pos >= _s.size(); }
    char Peek() const { return AtEnd() ? '\0' : _s[_pos]; }
    void Skip()
    {
        while (!AtEnd() && (_s[_pos] == ' ' || _s[_pos] == '\t'))
            ++_pos;
    }
    std::string Word()
    {
        std::string w;
        while (!AtEnd() && IsNameChar(static_cast<uint8_t>(_s[_pos])))
            w.push_back(_s[_pos++]);
        return w;
    }
    void PutName(const std::string& name)
    {
        for (size_t k = 0; k < name.size(); ++k)
            _out.push_back(static_cast<uint8_t>(name[k] | (k + 1 == name.size() ? 0x80 : 0)));
    }

    bool Raw(std::string& error)
    {
        size_t k = _pos + 1;
        while (k + 1 < _s.size())
        {
            while (k < _s.size() && _s[k] == ' ')
                ++k;
            if (k + 1 >= _s.size())
                break;
            unsigned v = 0;
            for (int n = 0; n < 2; ++n)
            {
                const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(_s[k + static_cast<size_t>(n)])));
                if (!std::isxdigit(static_cast<unsigned char>(c)))
                {
                    error = "bad byte in a raw paragraph";
                    return false;
                }
                v = v * 16 + static_cast<unsigned>(c <= '9' ? c - '0' : c - 'A' + 10);
            }
            _out.push_back(static_cast<uint8_t>(v));
            k += 2;
        }
        return true;
    }

    bool Comment(uint8_t token, std::string& error)
    {
        ++_pos;   // ';'
        std::vector<uint8_t> bytes;
        if (!encoding::FromUtf8(std::string_view(_s).substr(_pos), encoding::CodePage::ZxSpectrum, bytes, error))
            return false;
        _out.push_back(token);
        for (const uint8_t b : bytes)
        {
            if (b == kCommentEnd || b == kParagraphEnd)
            {
                error = "a comment cannot hold this character";
                return false;
            }
            _out.push_back(b);
        }
        _out.push_back(kCommentEnd);
        _pos = _s.size();
        return true;
    }

    bool Finish(size_t start, std::string& error)
    {
        Skip();
        if (AtEnd())
            return true;
        if (Peek() == ';')
            return Comment(_out.size() == start ? kCommentLine : kComment, error);
        error = "unexpected text: " + _s.substr(_pos);
        return false;
    }

    bool Operands(size_t start, std::string& error)
    {
        bool first = true;
        for (;;)
        {
            Skip();
            if (AtEnd() || Peek() == ';')
                return Finish(start, error);
            if (!first)
            {
                if (Peek() != ',')
                {
                    error = "',' expected: " + _s.substr(_pos);
                    return false;
                }
                ++_pos;
                Skip();
            }
            first = false;
            if (!Operand(error))
                return false;
        }
    }

    /// The register or indirect token an operand starts with; false (position kept) when it is an expression
    bool OperandHead(uint8_t& token)
    {
        const size_t save = _pos;
        if (Peek() == '(')
        {
            ++_pos;
            Skip();
            const std::string w = Upper(Word());
            Skip();
            for (uint8_t k = 0; k < 7; ++k)
            {
                std::string inner(kIndirect[k] + 1);
                inner.pop_back();
                if (w == inner && Peek() == ')')
                {
                    ++_pos;
                    token = static_cast<uint8_t>(kFirstIndirect + k);
                    return true;
                }
            }
            if ((w == "IX" || w == "IY") && (Peek() == '+' || Peek() == '-'))
            {
                token = static_cast<uint8_t>(w == "IX" ? (Peek() == '+' ? 0x27 : 0x28) : (Peek() == '+' ? 0x29 : 0x2A));
                ++_pos;
                return true;
            }
            _pos = save + 1;
            token = kLastIndirect;   // a memory operand
            return true;
        }
        std::string w = Upper(Word());
        if (w == "AF" && Peek() == '\'')
        {
            ++_pos;
            w = "AF'";
        }
        for (uint8_t k = 0; k <= kLastRegister - kFirstRegister; ++k)
            if (w == kRegisters[k])
            {
                token = static_cast<uint8_t>(kFirstRegister + k);
                return true;
            }
        _pos = save;
        return false;
    }

    /// The expression after an opening token and its closing parenthesis
    bool Closed(std::string& error)
    {
        if (!Expression(error))
            return false;
        Skip();
        if (Peek() != ')')
        {
            error = "')' expected";
            return false;
        }
        ++_pos;
        return true;
    }

    bool Operand(std::string& error)
    {
        uint8_t token = 0;
        if (OperandHead(token))
        {
            _out.push_back(token);
            if (token >= kFirstOpening && token <= kLastIndirect)
                return Closed(error);
            return true;
        }
        return Expression(error);
    }

    bool Expression(std::string& error)
    {
        for (;;)
        {
            Skip();
            for (;;)
            {
                const char c = Peek();
                if (c == '[')
                    _out.push_back(kOpenBracket);
                else if (c == '-' || c == '!' || c == '^' || c == '*')
                {
                    // "*" alone at an operand's start is "contents of"; the others are the unary operators
                    for (uint8_t k = 0; k < 4; ++k)
                        if (c == kUnary[k][0])
                            _out.push_back(static_cast<uint8_t>(kFirstUnary + k));
                }
                else
                    break;
                ++_pos;
                Skip();
            }
            if (!Atom(error))
                return false;
            Skip();
            while (Peek() == ']')
            {
                _out.push_back(kCloseBracket);
                ++_pos;
                Skip();
            }
            size_t best = kBinaryCount;
            size_t bestLength = 0;
            for (size_t k = 0; k < kBinaryCount; ++k)
            {
                const size_t n = std::strlen(kBinary[k]);
                if (n > bestLength && _s.compare(_pos, n, kBinary[k]) == 0)
                {
                    best = k;
                    bestLength = n;
                }
            }
            if (best == kBinaryCount)
                return true;
            _out.push_back(static_cast<uint8_t>(kFirstBinary + best));
            _pos += bestLength;
        }
    }

    void Number(unsigned v, uint8_t byteToken, bool word)
    {
        if (!word && v < 256)
        {
            _out.push_back(byteToken);
            _out.push_back(static_cast<uint8_t>(v));
        }
        else
        {
            _out.push_back(static_cast<uint8_t>(byteToken - 1));
            _out.push_back(static_cast<uint8_t>(v & 0xFF));
            _out.push_back(static_cast<uint8_t>(v >> 8));
        }
    }

    bool Digits(unsigned base, size_t& count, unsigned& v, std::string& error)
    {
        count = 0;
        v = 0;
        while (!AtEnd())
        {
            const int c = std::toupper(static_cast<unsigned char>(Peek()));
            const int d = std::isdigit(c) ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : 99;
            if (d >= static_cast<int>(base))
                break;
            v = v * base + static_cast<unsigned>(d);
            if (v > 0xFFFF)
            {
                error = "number too large";
                return false;
            }
            ++count;
            ++_pos;
        }
        if (!count)
        {
            error = "digits expected";
            return false;
        }
        return true;
    }

    /// A character constant or string: "..." with \n (a decimal code), \" and \\ escapes
    bool Quoted(std::string& error)
    {
        ++_pos;
        std::vector<int> chars;   // >= 0 a character, < 0 an escape -(code + 1)
        while (!AtEnd() && Peek() != '"')
        {
            if (Peek() == '\\' && _pos + 1 < _s.size())
            {
                ++_pos;
                if (std::isdigit(static_cast<unsigned char>(Peek())))
                {
                    unsigned v = 0;
                    while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek())))
                        v = v * 10 + static_cast<unsigned>(Peek() - '0'), ++_pos;
                    if (v > 255)
                    {
                        error = "character code above 255";
                        return false;
                    }
                    chars.push_back(-static_cast<int>(v) - 1);
                    continue;
                }
            }
            const unsigned char c = static_cast<unsigned char>(_s[_pos++]);
            if (c < 0x20 || c >= 0x80)
            {
                error = "character not in the editor's set";
                return false;
            }
            chars.push_back(c);
        }
        if (AtEnd())
        {
            error = "unclosed string";
            return false;
        }
        ++_pos;
        if (chars.size() == 1)
        {
            _out.push_back(kCharacter);
            if (chars[0] < 0)
            {
                _out.push_back(0xFD);
                _out.push_back(static_cast<uint8_t>(-chars[0] - 1));
            }
            else
                _out.push_back(static_cast<uint8_t>(chars[0]));
            return true;
        }
        _out.push_back(kString);
        for (const int c : chars)
        {
            if (c < 0)
            {
                _out.push_back(0xFD);
                _out.push_back(static_cast<uint8_t>(-c - 1));
            }
            else
                _out.push_back(static_cast<uint8_t>(c));
        }
        _out.push_back(0);
        return true;
    }

    bool Atom(std::string& error)
    {
        const char c = Peek();
        size_t count = 0;
        unsigned v = 0;
        if (c == '"')
            return Quoted(error);
        if (c == '#')
        {
            ++_pos;
            if (!Digits(16, count, v, error))
                return false;
            Number(v, 0xFF, count > 2);
            return true;
        }
        if (c == '%')
        {
            ++_pos;
            if (!Digits(2, count, v, error))
                return false;
            Number(v, 0xF9, count > 8);
            return true;
        }
        if (c == '@')
        {
            ++_pos;
            if (!Digits(8, count, v, error))
                return false;
            Number(v, 0xFB, count > 3);
            return true;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            // Decimal, or hex with an H suffix
            size_t end = _pos;
            while (end < _s.size() && std::isxdigit(static_cast<unsigned char>(_s[end])))
                ++end;
            if (end < _s.size() && _s[end] == 'H')
            {
                if (!Digits(16, count, v, error))
                    return false;
                ++_pos;   // H
                Number(v, 0xFF, count > 2 && !(count == 3 && _s[_pos - 4] == '0'));
                return true;
            }
            if (!Digits(10, count, v, error))
                return false;
            Number(v, 0xFD, count > 3);
            return true;
        }
        if (c == '\\')
        {
            ++_pos;
            const std::string name = Word();
            if (name.empty())
            {
                error = "a parameter name expected after \\";
                return false;
            }
            _out.push_back(kMacroParameter);
            PutName(name);
            return true;
        }
        if ((c == '$' || c == '.') && (_pos + 1 >= _s.size() || !IsNameChar(static_cast<uint8_t>(_s[_pos + 1]))))
        {
            ++_pos;
            _out.push_back(c == '$' ? kLocation : kStorage);
            return true;
        }
        const std::string name = Word();
        if (name.empty())
        {
            error = "an operand expected: " + _s.substr(_pos);
            return false;
        }
        _out.push_back(kName);
        PutName(name);
        return true;
    }

    const std::string& _s;
    std::vector<uint8_t>& _out;
    size_t _pos = 0;
};

/// Where the text starts and ends in a file: after the disk header when there is one
bool Locate(std::span<const uint8_t> bytes, size_t& start, size_t& end)
{
    if (bytes.size() >= kDiskHeaderSize && bytes[0] == kDiskHeader)
    {
        const size_t length = bytes[1] | (bytes[2] << 8);
        if (kDiskHeaderSize + length <= bytes.size())
        {
            start = kDiskHeaderSize;
            end = kDiskHeaderSize + length;
            return true;
        }
    }
    start = 0;
    end = bytes.size();
    return false;
}
}  // namespace

LaserGeniusCodec::LaserGeniusCodec() : _info{"lasergenius", "Laser Genius source (Oasis Software)", "lasergenius", CodecFamily::Tokenized, {}} {}

bool LaserGeniusCodec::ReadParagraphs(std::span<const uint8_t> bytes, size_t pos, size_t end, std::vector<Paragraph>& out, size_t& used, bool& truncated, int& rawCount)
{
    out.clear();
    truncated = false;
    rawCount = 0;
    const size_t begin = pos;
    int previous = -1;
    while (pos + 2 <= end)
    {
        const uint16_t number = static_cast<uint16_t>(bytes[pos] | (bytes[pos + 1] << 8));
        if (number == 0xFFFF)
            break;   // the end marker of the text in memory
        if (static_cast<int>(number) <= previous)
            return false;
        Paragraph p;
        p.number = number;
        size_t at = pos + 2;
        bool ok = true;
        Reader reader(bytes, end);
        while (at < end && bytes[at] != kParagraphEnd)
        {
            Statement s;
            s.offset = at;
            if (!reader.Statement(at, s.text))
            {
                ok = false;
                break;
            }
            s.length = at - s.offset;
            p.statements.push_back(std::move(s));
        }
        if (!ok)
        {
            // Tokens this codec does not know (Phoenix): the paragraph kept as its bytes
            const size_t stop = SkipRaw(bytes, pos + 2, end);
            Statement s;
            s.offset = pos + 2;
            s.length = stop - s.offset;
            s.raw = true;
            s.text = RawText(bytes.subspan(s.offset, s.length));
            p.statements.assign(1, std::move(s));
            at = stop;
            ++rawCount;
        }
        if (at >= end)
        {
            truncated = true;   // a paragraph without its end: the tape lost the last block
            break;
        }
        previous = number;
        out.push_back(std::move(p));
        pos = at + 1;
    }
    used = pos - begin;
    return !out.empty();
}

int LaserGeniusCodec::Detect(std::span<const uint8_t> bytes, const CatalogHints& hints) const
{
    size_t start = 0, end = 0;
    const bool disk = Locate(bytes, start, end);
    std::vector<Paragraph> paragraphs;
    size_t used = 0;
    bool truncated = false;
    int raw = 0;
    if (!ReadParagraphs(bytes, start, end, paragraphs, used, truncated, raw))
        return 0;
    const size_t rest = end - start - used;
    if (!truncated && rest > 2 && !(rest <= 256 && hints.type))
        return 0;
    // Paragraphs the codec cannot read (Phoenix) still have their numbers and ends: a file of many of them is a Laser
    // Genius file when the paragraphs fill it exactly
    if (raw * 4 > static_cast<int>(paragraphs.size()))
        return !truncated && paragraphs.size() >= 5 ? 75 : 0;
    int score = paragraphs.size() >= 3 ? 90 : 60;
    if (disk)
        score += 5;
    if (hints.type == 'L')
        score += 5;
    return score > 100 ? 100 : score;
}

DecodeResult LaserGeniusCodec::Decode(std::span<const uint8_t> bytes, const DecodeOptions& options) const
{
    DecodeResult result;
    result.document.format = _info.id;
    result.document.dialect = _info.dialect;
    result.document.codePage = encoding::CodePage::ZxSpectrum;
    result.document.name = options.catalog.name;
    size_t start = 0, end = 0;
    Locate(bytes, start, end);
    std::vector<Paragraph> paragraphs;
    size_t used = 0;
    bool truncated = false;
    int raw = 0;
    if (!ReadParagraphs(bytes, start, end, paragraphs, used, truncated, raw))
    {
        result.diagnostics.push_back({Severity::Error, 0, 0, "not a Laser Genius source: no paragraph (line number, statements, #F7) at the start"});
        return result;
    }
    for (const Paragraph& p : paragraphs)
    {
        if (p.statements.empty())
        {
            SourceLine line;
            line.number = p.number;
            line.attrs.codec = _info.id;
            result.document.lines.push_back(std::move(line));
            continue;
        }
        for (size_t k = 0; k < p.statements.size(); ++k)
        {
            const Statement& s = p.statements[k];
            SourceLine line;
            line.text = s.text;
            line.number = k == 0 ? p.number : -1;
            line.attrs.codec = _info.id;
            line.attrs.bytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(s.offset), bytes.begin() + static_cast<std::ptrdiff_t>(s.offset + s.length));
            if (s.raw)
                result.diagnostics.push_back({Severity::Warning, static_cast<uint32_t>(result.document.lines.size() + 1), 0,
                                              "paragraph " + std::to_string(p.number) + ": tokens the codec does not know (Phoenix?), kept as bytes"});
            result.document.lines.push_back(std::move(line));
        }
    }
    if (truncated)
        result.diagnostics.push_back({Severity::Warning, 0, 0, "the text ends inside a paragraph (a lost tape block?): the last paragraph is dropped"});
    // The file's header and what follows the text, so encoding gives the same bytes
    const size_t tail = start + used;
    result.document.attrs.codec = _info.id;
    result.document.attrs.bytes.push_back(static_cast<uint8_t>(start));
    result.document.attrs.bytes.insert(result.document.attrs.bytes.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(start));
    result.document.attrs.bytes.insert(result.document.attrs.bytes.end(), bytes.begin() + static_cast<std::ptrdiff_t>(tail), bytes.end());
    result.ok = true;
    return result;
}

bool LaserGeniusCodec::EncodeStatement(const std::string& text, std::vector<uint8_t>& out, std::string& error)
{
    Writer writer(text, out);
    return writer.Statement(error);
}

EncodeResult LaserGeniusCodec::Encode(const SourceDocument& document, const EncodeOptions& options) const
{
    (void)options;
    EncodeResult result;
    std::vector<uint8_t> body;
    // The numbers kept when the first line has one; a document of plain lines gets one paragraph a line, 10, 20, ...
    const bool numbered = !document.lines.empty() && document.lines.front().number >= 0;
    bool open = false;
    int previous = -1;
    uint32_t index = 0;
    for (const SourceLine& line : document.lines)
    {
        ++index;
        int number = numbered ? line.number : static_cast<int>(index * 10);
        if (number >= 0)
        {
            if (number <= previous || number > 0xFFFE)
            {
                result.diagnostics.push_back({Severity::Error, index, 0, "paragraph numbers must increase (0-65534)"});
                return result;
            }
            if (open)
                body.push_back(kParagraphEnd);
            body.push_back(static_cast<uint8_t>(number & 0xFF));
            body.push_back(static_cast<uint8_t>(number >> 8));
            previous = number;
            open = true;
        }
        // The tokens kept beside the line while they still read as its text
        if (line.attrs.codec == _info.id && !line.attrs.bytes.empty())
        {
            std::vector<uint8_t> probe{0, 0};
            probe.insert(probe.end(), line.attrs.bytes.begin(), line.attrs.bytes.end());
            probe.push_back(kParagraphEnd);
            std::vector<Paragraph> p;
            size_t used = 0;
            bool truncated = false;
            int raw = 0;
            if (ReadParagraphs(probe, 0, probe.size(), p, used, truncated, raw) && p.size() == 1 && p[0].statements.size() == 1 &&
                p[0].statements[0].text == line.text)
            {
                body.insert(body.end(), line.attrs.bytes.begin(), line.attrs.bytes.end());
                continue;
            }
        }
        if (line.text.find_first_not_of(" \t") == std::string::npos)
            continue;
        std::string error;
        if (!EncodeStatement(line.text, body, error))
        {
            result.diagnostics.push_back({Severity::Error, index, 0, error});
            return result;
        }
    }
    if (open)
        body.push_back(kParagraphEnd);
    // The disk header (length updated) and the bytes after the text, as decoded
    const std::vector<uint8_t>& kept = document.attrs.bytes;
    if (document.attrs.codec == _info.id && !kept.empty() && kept[0] <= kept.size() - 1)
    {
        const size_t header = kept[0];
        if (header == kDiskHeaderSize)
        {
            result.bytes.assign(kept.begin() + 1, kept.begin() + 1 + static_cast<std::ptrdiff_t>(header));
            result.bytes[1] = static_cast<uint8_t>(body.size() & 0xFF);
            result.bytes[2] = static_cast<uint8_t>(body.size() >> 8);
        }
        result.bytes.insert(result.bytes.end(), body.begin(), body.end());
        result.bytes.insert(result.bytes.end(), kept.begin() + 1 + static_cast<std::ptrdiff_t>(header), kept.end());
    }
    else
        result.bytes = std::move(body);
    result.ok = true;
    return result;
}
}  // namespace unrealasm::codecs
