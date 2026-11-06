#include "zesaruxcondition.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace zrcp
{
namespace
{
std::string toUpperAscii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trimString(const std::string& s)
{
    size_t start = 0;
    size_t end = s.size();
    while (start < end && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

/// The tokens of the ZEsarUX condition subset (see header), shared by the evaluator and the translation
class Lexer
{
public:
    explicit Lexer(const std::string& text)
        : _s(text)
    {
    }

    static bool isIdentChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '\'';
    }

protected:
    const std::string& _s;
    size_t _pos = 0;
    bool _failed = false;
    std::string _error;

    // The whole text was one expression: false with the reason otherwise
    bool finish(std::string& error)
    {
        skipWs();
        if (_failed)
        {
            error = _error;
            return false;
        }
        if (_pos < _s.size())
        {
            error = "unexpected text at offset " + std::to_string(_pos);
            return false;
        }
        return true;
    }

    void fail(const std::string& message)
    {
        if (!_failed)
        {
            _failed = true;
            _error = message;
        }
    }

    void skipWs()
    {
        while (_pos < _s.size() && std::isspace(static_cast<unsigned char>(_s[_pos])))
            ++_pos;
    }

    // Case-insensitive keyword match. The keyword must be followed by a
    // non-identifier character so "AND" does not match the prefix of "ANDY".
    bool matchKeyword(const char* keyword)
    {
        skipWs();
        const size_t len = std::strlen(keyword);
        if (_pos + len > _s.size())
            return false;
        for (size_t i = 0; i < len; ++i)
        {
            char a = static_cast<char>(std::toupper(static_cast<unsigned char>(_s[_pos + i])));
            char b = static_cast<char>(std::toupper(static_cast<unsigned char>(keyword[i])));
            if (a != b)
                return false;
        }
        if (_pos + len < _s.size() && isIdentChar(_s[_pos + len]))
            return false;
        _pos += len;
        return true;
    }

    // Exact operator match (no identifier-boundary check - operators may be
    // directly followed by digits, e.g. "SP>=32768").
    bool matchOper(const char* op)
    {
        skipWs();
        const size_t len = std::strlen(op);
        if (_pos + len > _s.size() || _s.compare(_pos, len, op) != 0)
            return false;
        _pos += len;
        return true;
    }

    // Scans an identifier ([A-Za-z][A-Za-z0-9']*, apostrophes included for AF')
    std::string parseName()
    {
        skipWs();
        size_t start = _pos;
        if (_pos >= _s.size() || !std::isalpha(static_cast<unsigned char>(_s[_pos])))
            return {};
        while (_pos < _s.size() && isIdentChar(_s[_pos]))
            ++_pos;
        return _s.substr(start, _pos - start);
    }

    /// ZEsarUX's flag names (expression_parser.c): FS FZ FP FV FH FN FC, each 0 or 1. Checked before a number:
    /// "FH" would read as the hex number Fh
    static int flagBit(const std::string& upperName)
    {
        static const struct { const char* name; int bit; } kFlags[] = {
            {"FS", 7}, {"FZ", 6}, {"FH", 4}, {"FP", 2}, {"FV", 2}, {"FN", 1}, {"FC", 0}};
        for (const auto& flag : kFlags)
            if (upperName == flag.name)
                return flag.bit;
        return -1;
    }

    /// The flag name at the cursor (consumed), or -1 (nothing consumed)
    int matchFlagName()
    {
        skipWs();
        size_t end = _pos;
        while (end < _s.size() && isIdentChar(_s[end]))
            ++end;
        if (end - _pos != 2)
            return -1;
        std::string word = _s.substr(_pos, 2);
        for (char& c : word)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        const int bit = flagBit(word);
        if (bit >= 0)
            _pos = end;
        return bit;
    }

    // number := hexDigits[h|H] | 0x hexDigits | decimal
    bool parseNumber(long long& value)
    {
        skipWs();
        if (_pos >= _s.size() || !std::isxdigit(static_cast<unsigned char>(_s[_pos])))
            return false;

        const size_t start = _pos;

        if (_s[_pos] == '0' && _pos + 1 < _s.size() && (_s[_pos + 1] == 'x' || _s[_pos + 1] == 'X'))
        {
            _pos += 2;
            const size_t digitsStart = _pos;
            while (_pos < _s.size() && std::isxdigit(static_cast<unsigned char>(_s[_pos])))
                ++_pos;
            if (_pos == digitsStart)
            {
                fail("hex digits expected after 0x");
                return false;
            }
            value = std::strtoll(_s.substr(digitsStart, _pos - digitsStart).c_str(), nullptr, 16);
            return true;
        }

        // Scan hex-wide digits: the token is hex when an h/H suffix follows
        while (_pos < _s.size() && std::isxdigit(static_cast<unsigned char>(_s[_pos])))
            ++_pos;

        if (_pos < _s.size() && (_s[_pos] == 'h' || _s[_pos] == 'H'))
        {
            value = std::strtoll(_s.substr(start, _pos - start).c_str(), nullptr, 16);
            ++_pos;
            return true;
        }

        // Plain decimal (digits only). A token that starts with a letter is
        // not a number - rewind so parseName can retry from the token start
        // ("B" or "AF'" are registers, not hex digits).
        if (!std::isdigit(static_cast<unsigned char>(_s[start])))
        {
            _pos = start;
            return false;
        }
        size_t end = start;
        while (end < _pos && std::isdigit(static_cast<unsigned char>(_s[end])))
            ++end;
        if (end == start)
        {
            fail("malformed number");
            return false;
        }
        value = std::strtoll(_s.substr(start, end - start).c_str(), nullptr, 10);
        _pos = end;
        return true;
    }
};

/// Recursive-descent evaluator over the ZEsarUX condition subset (see header).
class Parser : public Lexer
{
public:
    Parser(const std::string& text, const ConditionContext& ctx)
        : Lexer(text), _ctx(ctx)
    {
    }

    bool parse(long long& result, std::string& error)
    {
        skipWs();
        result = parseOr();
        return finish(error);
    }

private:
    const ConditionContext& _ctx;

    // ZEsarUX expression_parser.c: on the 128K / +2 / +2A / +3 (and Pentagon) RAM is the bank paged at C000h and
    // ROM the page in slot 0 (& 127: the ROM number, a RAM page on a +2A all-RAM map); on every other machine both
    // are 0. Here: the 4-slot DZRP map (slot 0: 8 = ROM 0, 9 = ROM 1, 0x80 + page another ROM, else RAM)
    long long pagingName(bool rom) const
    {
        if (!_ctx.slots || _ctx.slots->size() != 4)
            return 0;
        if (!rom)
            return (*_ctx.slots)[3];
        const uint8_t bank = (*_ctx.slots)[0];
        if (bank == 8 || bank == 9)
            return bank - 8;
        return bank & 0x7F;
    }

    bool lookupName(const std::string& name, long long& value) const
    {
        const dzrp::IDebugInterface::Registers& r = *_ctx.regs;
        if (name == "PC") value = r.pc;
        else if (name == "SP") value = r.sp;
        else if (name == "AF") value = r.af;
        else if (name == "BC") value = r.bc;
        else if (name == "DE") value = r.de;
        else if (name == "HL") value = r.hl;
        else if (name == "IX") value = r.ix;
        else if (name == "IY") value = r.iy;
        else if (name == "AF'") value = r.af2;
        else if (name == "BC'") value = r.bc2;
        else if (name == "HL'") value = r.hl2;
        else if (name == "DE'") value = r.de2;
        else if (name == "I") value = r.i;
        else if (name == "R") value = r.r;
        else if (name == "IM") value = r.im;
        else if (name == "A") value = (r.af >> 8) & 0xFF;
        else if (name == "F") value = r.af & 0xFF;
        else if (name == "B") value = (r.bc >> 8) & 0xFF;
        else if (name == "C") value = r.bc & 0xFF;
        else if (name == "D") value = (r.de >> 8) & 0xFF;
        else if (name == "E") value = r.de & 0xFF;
        else if (name == "H") value = (r.hl >> 8) & 0xFF;
        else if (name == "L") value = r.hl & 0xFF;
        else if (name == "IXH") value = (r.ix >> 8) & 0xFF;
        else if (name == "IXL") value = r.ix & 0xFF;
        else if (name == "IYH") value = (r.iy >> 8) & 0xFF;
        else if (name == "IYL") value = (r.iy & 0xFF);
        else if (name == "ROM") value = pagingName(true);
        else if (name == "RAM") value = pagingName(false);
        else
            return false;
        return true;
    }

    long long parsePrimary()
    {
        skipWs();
        if (_pos >= _s.size())
        {
            fail("operand expected");
            return 0;
        }

        const char c = _s[_pos];
        if (c == '(')
        {
            ++_pos;
            const long long value = parseOr();
            skipWs();
            if (_pos < _s.size() && _s[_pos] == ')')
                ++_pos;
            else
                fail("')' expected");
            return value;
        }
        if (c == '-')
        {
            ++_pos;
            return -parsePrimary();
        }
        if (c == '+')
        {
            ++_pos;
            return parsePrimary();
        }

        if (const int bit = matchFlagName(); bit >= 0)
            return (_ctx.regs->af >> bit) & 1;

        long long number;
        if (parseNumber(number))
            return number;

        const std::string name = parseName();
        if (name.empty())
        {
            fail("operand expected at offset " + std::to_string(_pos));
            return 0;
        }

        skipWs();
        if (_pos < _s.size() && _s[_pos] == '(')
        {
            ++_pos;
            const long long addr = parseOr();
            skipWs();
            if (_pos < _s.size() && _s[_pos] == ')')
                ++_pos;
            else
                fail("')' expected");

            if (!_ctx.readMem)
            {
                fail("no memory reader available");
                return 0;
            }
            const uint16_t a = static_cast<uint16_t>(addr & 0xFFFF);
            const std::string function = toUpperAscii(name);
            if (function == "PEEK" || function == "PEEKB")
                return _ctx.readMem(a);
            if (function == "PEEKW")
                return static_cast<long long>(_ctx.readMem(a)) |
                       (static_cast<long long>(_ctx.readMem(static_cast<uint16_t>(a + 1))) << 8);

            fail("unknown function " + name);
            return 0;
        }

        long long value = 0;
        if (lookupName(toUpperAscii(name), value))
            return value;

        fail("unknown identifier " + name);
        return 0;
    }

    long long parseTerm()
    {
        long long value = parsePrimary();
        while (true)
        {
            skipWs();
            // ZEsarUX's calculation operators below + and -: * / & | ^ (one level, left to right)
            if (matchOper("*"))
                value *= parsePrimary();
            else if (matchOper("&"))
                value &= parsePrimary();
            else if (matchOper("|"))
                value |= parsePrimary();
            else if (matchOper("^"))
                value ^= parsePrimary();
            else if (matchOper("/"))
            {
                const long long divisor = parsePrimary();
                if (divisor == 0)
                {
                    fail("division by zero");
                    return value;
                }
                value /= divisor;
            }
            else
                return value;
        }
    }

    long long parseSum()
    {
        long long value = parseTerm();
        while (true)
        {
            skipWs();
            if (matchOper("+"))
                value += parseTerm();
            else if (matchOper("-"))
                value -= parseTerm();
            else
                return value;
        }
    }

    long long parseCmp()
    {
        const long long left = parseSum();

        int op = 0;  // 1:= 2:<> 3:< 4:> 5:<= 6:>=
        if (matchOper("<="))
            op = 5;
        else if (matchOper(">="))
            op = 6;
        else if (matchOper("<>"))
            op = 2;
        else if (matchOper("!="))
            op = 2;
        else if (matchOper("="))
            op = 1;
        else if (matchOper("<"))
            op = 3;
        else if (matchOper(">"))
            op = 4;

        if (op == 0)
            return left;  // bare value: truth handled by the caller (!= 0)

        const long long right = parseSum();
        switch (op)
        {
            case 1: return left == right ? 1 : 0;
            case 2: return left != right ? 1 : 0;
            case 3: return left < right ? 1 : 0;
            case 4: return left > right ? 1 : 0;
            case 5: return left <= right ? 1 : 0;
            default: return left >= right ? 1 : 0;
        }
    }

    long long parseNot()
    {
        if (matchKeyword("NOT"))
            return parseNot() == 0 ? 1 : 0;
        return parseCmp();
    }

    long long parseAnd()
    {
        long long value = parseNot();
        while (matchKeyword("AND"))
        {
            // Evaluate the right side unconditionally: short-circuiting would
            // leave its text unconsumed and fail the whole parse
            const long long right = parseNot();
            value = (value != 0 && right != 0) ? 1 : 0;
        }
        return value;
    }

    long long parseOr()
    {
        long long value = parseAnd();
        while (true)
        {
            if (matchKeyword("OR"))
            {
                const long long right = parseAnd();
                value = (value != 0 || right != 0) ? 1 : 0;
            }
            else if (matchKeyword("XOR"))
            {
                const long long right = parseAnd();
                value = ((value != 0) != (right != 0)) ? 1 : 0;
            }
            else
                return value;
        }
    }
};

/// The same grammar written out in the core condition language (breakpointcondition.h): every operation in
/// brackets, so neither dialect's precedence matters, numbers as #hex. Refuses what the core would answer
/// differently from the evaluator above (see translateToCore)
class Translator : public Lexer
{
public:
    explicit Translator(const std::string& text)
        : Lexer(text)
    {
    }

    bool translate(std::string& out, std::string& error)
    {
        skipWs();
        out = parseOr().text;
        return finish(error);
    }

private:
    struct Term
    {
        std::string text;
        bool isNumber = false;  // a literal (a divisor must be one)
        long long value = 0;
    };

    static std::string hex(long long value)
    {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "#%llX", static_cast<unsigned long long>(value));
        return buffer;
    }

    static Term binary(const Term& left, const char* op, const Term& right)
    {
        return Term{"(" + left.text + " " + op + " " + right.text + ")"};
    }

    // The registers lookupName knows, under their core names (identical); ROM and RAM have no core operand
    static bool coreRegister(const std::string& name)
    {
        static const char* const kNames[] = {"PC", "SP", "AF", "BC", "DE", "HL", "IX", "IY", "AF'", "BC'", "HL'",
                                             "DE'", "I", "R", "IM", "A", "F", "B", "C", "D", "E", "H", "L",
                                             "IXH", "IXL", "IYH", "IYL"};
        for (const char* known : kNames)
            if (name == known)
                return true;
        return false;
    }

    Term parsePrimary()
    {
        skipWs();
        if (_pos >= _s.size())
        {
            fail("operand expected");
            return {};
        }

        const char c = _s[_pos];
        if (c == '(')
        {
            ++_pos;
            Term inner = parseOr();
            skipWs();
            if (_pos < _s.size() && _s[_pos] == ')')
                ++_pos;
            else
                fail("')' expected");
            inner.isNumber = false;
            return inner;
        }
        if (c == '-')
        {
            ++_pos;
            return Term{"(-" + parsePrimary().text + ")"};
        }
        if (c == '+')
        {
            ++_pos;
            Term operand = parsePrimary();
            operand.isNumber = false;
            return operand;
        }

        if (const int bit = matchFlagName(); bit >= 0)
            return Term{"((F & " + hex(1LL << bit) + ") != 0)"};

        long long number;
        if (parseNumber(number))
            return Term{hex(number), true, number};

        const std::string name = toUpperAscii(parseName());
        if (name.empty())
        {
            fail("operand expected at offset " + std::to_string(_pos));
            return {};
        }

        skipWs();
        if (_pos < _s.size() && _s[_pos] == '(')
        {
            ++_pos;
            const Term address = parseOr();
            skipWs();
            if (_pos < _s.size() && _s[_pos] == ')')
                ++_pos;
            else
                fail("')' expected");
            if (name == "PEEK" || name == "PEEKB")
                return Term{"M(" + address.text + ")"};
            if (name == "PEEKW")
                return Term{"W(" + address.text + ")"};
            fail("unknown function " + name);
            return {};
        }

        if (name == "ROM" || name == "RAM")
        {
            fail(name + " (the paging: the bank at C000h, the ROM in slot 0) has no core operand");
            return {};
        }
        if (!coreRegister(name))
        {
            fail("unknown identifier " + name);
            return {};
        }
        return Term{name};
    }

    Term parseTerm()
    {
        Term value = parsePrimary();
        while (true)
        {
            skipWs();
            if (matchOper("*"))
                value = binary(value, "*", parsePrimary());
            else if (matchOper("&"))
                value = binary(value, "&", parsePrimary());
            else if (matchOper("|"))
                value = binary(value, "|", parsePrimary());
            else if (matchOper("^"))
                value = binary(value, "^", parsePrimary());
            else if (matchOper("/"))
            {
                // The evaluator stops on a zero divisor, the core keeps the dividend: only a constant one is the same
                const Term divisor = parsePrimary();
                if (!divisor.isNumber || divisor.value == 0)
                {
                    fail("a divisor other than a nonzero number");
                    return value;
                }
                value = binary(value, "/", divisor);
            }
            else
                return value;
        }
    }

    Term parseSum()
    {
        Term value = parseTerm();
        while (true)
        {
            skipWs();
            if (matchOper("+"))
                value = binary(value, "+", parseTerm());
            else if (matchOper("-"))
                value = binary(value, "-", parseTerm());
            else
                return value;
        }
    }

    Term parseCmp()
    {
        const Term left = parseSum();

        const char* op = nullptr;
        if (matchOper("<="))
            op = "<=";
        else if (matchOper(">="))
            op = ">=";
        else if (matchOper("<>") || matchOper("!="))
            op = "!=";
        else if (matchOper("="))
            op = "==";
        else if (matchOper("<"))
            op = "<";
        else if (matchOper(">"))
            op = ">";

        if (!op)
            return left;
        return binary(left, op, parseSum());
    }

    Term parseNot()
    {
        if (matchKeyword("NOT"))
            return Term{"(!" + parseNot().text + ")"};
        return parseCmp();
    }

    Term parseAnd()
    {
        Term value = parseNot();
        while (matchKeyword("AND"))
            value = binary(value, "&&", parseNot());
        return value;
    }

    Term parseOr()
    {
        Term value = parseAnd();
        while (true)
        {
            if (matchKeyword("OR"))
                value = binary(value, "||", parseAnd());
            else if (matchKeyword("XOR"))
            {
                // Logical: the truths differ
                const Term right = parseAnd();
                value = Term{"((!" + value.text + ") != (!" + right.text + "))"};
            }
            else
                return value;
        }
    }
};

// Strips leading whitespace and an optional leading AND separator from the
// condition remainder left after removing the PC literal.
std::string trimLeadingAnd(std::string s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    s = s.substr(start);

    const std::string upper = toUpperAscii(s);
    if (upper.compare(0, 3, "AND") == 0)
    {
        size_t after = 3;
        while (after < s.size() && std::isspace(static_cast<unsigned char>(s[after])))
            ++after;
        s = s.substr(after);
    }
    return s;
}
} // namespace

bool ConditionEvaluator::evaluate(const std::string& condition, const ConditionContext& ctx,
                                  std::string* errorOut)
{
    if (condition.empty() || !ctx.regs)
    {
        if (errorOut)
            *errorOut = "empty condition";
        return true;
    }

    Parser parser(condition, ctx);
    long long result = 0;
    std::string error;
    if (!parser.parse(result, error))
    {
        // Conservative: an unparseable condition stops execution. A silently
        // skipped breakpoint is worse than a spurious stop.
        if (errorOut)
            *errorOut = error;
        return true;
    }
    return result != 0;
}

bool translateToCore(const std::string& condition, std::string& coreOut, std::string& reasonOut)
{
    coreOut.clear();
    if (condition.empty())
    {
        reasonOut = "empty condition";
        return false;
    }
    Translator translator(condition);
    std::string text;
    if (!translator.translate(text, reasonOut))
        return false;
    coreOut = text;
    return true;
}

bool extractPcLiteral(const std::string& condition, uint16_t& addrOut, std::string& restOut)
{
    // DeZog emits "PC=0XXXXh ..." (hex literal with h suffix, lowercase
    // address part "08000h" for long addresses - leading digit is ignored).
    const std::string upper = toUpperAscii(condition);

    size_t pos = upper.find("PC=");
    while (pos != std::string::npos)
    {
        // Not part of a longer identifier
        if (pos == 0 || !Lexer::isIdentChar(condition[pos - 1]))
        {
            const size_t digitsStart = pos + 3;
            size_t digitsEnd = digitsStart;
            while (digitsEnd < condition.size() &&
                   std::isxdigit(static_cast<unsigned char>(condition[digitsEnd])))
                ++digitsEnd;
            if (digitsEnd > digitsStart && digitsEnd < condition.size() &&
                std::toupper(static_cast<unsigned char>(condition[digitsEnd])) == 'H')
            {
                const unsigned long value =
                    std::strtoul(condition.substr(digitsStart, digitsEnd - digitsStart).c_str(),
                                 nullptr, 16);
                addrOut = static_cast<uint16_t>(value & 0xFFFF);
                // DeZog's shape: the literal first, then "AND ..." or nothing - the rest is what stays to check. A
                // literal anywhere else ("B=5 AND PC=8003h", "(PC=8003h OR ...)") gives the address only: the whole
                // text is checked, as ZEsarUX evaluates it (PC holds at the address)
                const std::string before = trimString(condition.substr(0, pos));
                const std::string after = trimLeadingAnd(condition.substr(digitsEnd + 1));
                const std::string afterUpper = toUpperAscii(trimString(condition.substr(digitsEnd + 1)));
                const bool leading = before.empty() &&
                                     (afterUpper.empty() || (afterUpper.compare(0, 3, "AND") == 0 &&
                                                             (afterUpper.size() == 3 || !Lexer::isIdentChar(afterUpper[3]))));
                restOut = leading ? after : trimString(condition);
                return true;
            }
        }
        pos = upper.find("PC=", pos + 1);
    }
    return false;
}
} // namespace zrcp
