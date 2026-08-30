#include "zesaruxcondition.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace zrcp
{
namespace
{
/// Recursive-descent parser over the ZEsarUX condition subset (see header).
class Parser
{
public:
    Parser(const std::string& text, const ConditionContext& ctx)
        : _s(text), _ctx(ctx)
    {
    }

    bool parse(long long& result, std::string& error)
    {
        skipWs();
        result = parseOr();
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

    static bool isIdentChar(char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '\'';
    }

private:
    const std::string& _s;
    const ConditionContext& _ctx;
    size_t _pos = 0;
    bool _failed = false;
    std::string _error;

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

    // Bank visible at the breakpoint address; ROM: 0/1 or -1 (not ROM-backed),
    // RAM: bank number or -1. Mirrors ZEsarUX "ROM=1"/"RAM=5" semantics.
    long long bankAtBreakpoint(bool rom) const
    {
        if (!_ctx.slots || _ctx.slots->empty())
            return -1;

        size_t slot = 0;
        if (_ctx.breakpointAddr >= 0xC000)
            slot = 3;
        else if (_ctx.breakpointAddr >= 0x8000)
            slot = 2;
        else if (_ctx.breakpointAddr >= 0x4000)
            slot = 1;
        if (slot >= _ctx.slots->size())
            slot = _ctx.slots->size() - 1;

        const uint8_t bank = (*_ctx.slots)[slot];
        const bool isRom = bank >= 8;  // dzrp bank encoding: 8 = ROM0, 9 = ROM1
        if (rom)
            return isRom ? (bank - 8) : -1;
        return isRom ? -1 : bank;
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
        else if (name == "ROM") value = bankAtBreakpoint(true);
        else if (name == "RAM") value = bankAtBreakpoint(false);
        else
            return false;
        return true;
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
            if (name == "PEEK" || name == "PEEKB")
                return _ctx.readMem(a);
            if (name == "PEEKW")
                return static_cast<long long>(_ctx.readMem(a)) |
                       (static_cast<long long>(_ctx.readMem(static_cast<uint16_t>(a + 1))) << 8);

            fail("unknown function " + name);
            return 0;
        }

        long long value = 0;
        if (lookupName(name, value))
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
            if (matchOper("*"))
                value *= parsePrimary();
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
        while (matchKeyword("OR"))
        {
            const long long right = parseAnd();
            value = (value != 0 || right != 0) ? 1 : 0;
        }
        return value;
    }
};

std::string toUpperAscii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

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

bool extractPcLiteral(const std::string& condition, uint16_t& addrOut, std::string& restOut)
{
    // DeZog emits "PC=0XXXXh ..." (hex literal with h suffix, lowercase
    // address part "08000h" for long addresses - leading digit is ignored).
    const std::string upper = toUpperAscii(condition);

    size_t pos = upper.find("PC=");
    while (pos != std::string::npos)
    {
        // Not part of a longer identifier
        if (pos == 0 || !Parser::isIdentChar(condition[pos - 1]))
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
                restOut = trimLeadingAnd(condition.substr(0, pos) + condition.substr(digitsEnd + 1));
                return true;
            }
        }
        pos = upper.find("PC=", pos + 1);
    }
    return false;
}
} // namespace zrcp
