#include "dezogcondition.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>

namespace dzrp
{
namespace
{
constexpr int64_t kExact = int64_t{1} << 53;  // JavaScript numbers are exact integers below this
constexpr int64_t kInt32Min = -(int64_t{1} << 31);
constexpr int64_t kInt32Max = (int64_t{1} << 31) - 1;

/// A translated operand: its core text, what JavaScript makes of it and the values it can take
struct Value
{
    enum class Kind
    {
        Number,  ///< a JavaScript number
        Truth,   ///< a JavaScript boolean (0 / 1 in the core)
        Either   ///< && / || over numbers: one of the operands in JavaScript, 0 / 1 in the core. Only its truth agrees
    };
    std::string text;
    Kind kind = Kind::Number;
    int64_t lo = 0;
    int64_t hi = 0;
};

/// The registers as DeZog names them (Z80_REG, shadows with ') and their core text and width
struct Register
{
    const char* name;
    const char* core;
    int64_t max;
};
constexpr Register kRegisters[] = {
    {"PC", "PC", 0xFFFF},   {"SP", "SP", 0xFFFF},   {"AF", "AF", 0xFFFF},   {"BC", "BC", 0xFFFF},
    {"DE", "DE", 0xFFFF},   {"HL", "HL", 0xFFFF},   {"IX", "IX", 0xFFFF},   {"IY", "IY", 0xFFFF},
    {"AF'", "AF'", 0xFFFF}, {"BC'", "BC'", 0xFFFF}, {"DE'", "DE'", 0xFFFF}, {"HL'", "HL'", 0xFFFF},
    {"IR", "((I << #8) | R)", 0xFFFF},
    {"IM", "IM", 2},
    {"F", "F", 0xFF},       {"A", "A", 0xFF},       {"C", "C", 0xFF},       {"B", "B", 0xFF},
    {"E", "E", 0xFF},       {"D", "D", 0xFF},       {"L", "L", 0xFF},       {"H", "H", 0xFF},
    {"IXL", "IXL", 0xFF},   {"IXH", "IXH", 0xFF},   {"IYL", "IYL", 0xFF},   {"IYH", "IYH", 0xFF},
    {"F'", "F'", 0xFF},     {"A'", "A'", 0xFF},     {"C'", "C'", 0xFF},     {"B'", "B'", 0xFF},
    {"E'", "E'", 0xFF},     {"D'", "D'", 0xFF},     {"L'", "L'", 0xFF},     {"H'", "H'", 0xFF},
    {"R", "R", 0xFF},       {"I", "I", 0xFF},
};

bool IsWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

class Translator
{
public:
    explicit Translator(const std::string& text)
        : _s(text)
    {
    }

    bool translate(std::string& out, std::string& error)
    {
        const Value value = parseBinary(0);
        skipWs();
        if (!_failed && _pos < _s.size())
            fail("unexpected text at offset " + std::to_string(_pos));
        if (_failed)
        {
            error = _error;
            return false;
        }
        out = value.text;  // the top level only asks for the truth: any kind
        return true;
    }

private:
    const std::string& _s;
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

    static std::string hex(int64_t value)
    {
        char buffer[24];
        std::snprintf(buffer, sizeof(buffer), "#%llX", static_cast<unsigned long long>(value));
        return buffer;
    }

    // The binary operators by JavaScript precedence, low to high; '/' is recognised to be refused
    struct Operator
    {
        const char* text;
        int level;
    };
    static constexpr Operator kOperators[] = {
        // longest first where one is the prefix of another
        {"===", 5}, {"!==", 5}, {">>>", 7}, {"||", 0}, {"&&", 1}, {"==", 5}, {"!=", 5}, {"<=", 6}, {">=", 6},
        {"<<", 7},  {">>", 7},  {"**", 9},  {"|", 2},  {"^", 3},  {"&", 4},  {"<", 6},  {">", 6},  {"+", 8},
        {"-", 8},   {"*", 9},   {"%", 9},   {"/", 9},
    };
    static constexpr int kUnaryLevel = 10;

    // The binary operator at the cursor (not consumed), null when there is none
    const Operator* peekOperator()
    {
        skipWs();
        for (const Operator& op : kOperators)
        {
            const size_t len = std::char_traits<char>::length(op.text);
            if (_s.compare(_pos, len, op.text) == 0)
            {
                // "||=" / "&=" and the like are assignments; "++" / "--" increments (a syntax error after a value)
                const char next = _pos + len < _s.size() ? _s[_pos + len] : '\0';
                if (next == '=' && op.level != 5 && op.level != 6)
                    return nullptr;
                if ((op.text[0] == '+' || op.text[0] == '-') && next == op.text[0])
                    return nullptr;
                return &op;
            }
        }
        return nullptr;
    }

    Value parseBinary(int level)
    {
        if (level >= kUnaryLevel)
            return parseUnary();
        Value left = parseBinary(level + 1);
        while (!_failed)
        {
            const Operator* op = peekOperator();
            if (!op || op->level != level)
                return left;
            _pos += std::char_traits<char>::length(op->text);
            const Value right = parseBinary(level + 1);
            if (_failed)
                return left;
            left = combine(op->text, left, right);
        }
        return left;
    }

    Value parseUnary()
    {
        skipWs();
        if (_pos >= _s.size())
        {
            fail("operand expected");
            return {};
        }
        const char c = _s[_pos];
        if ((c == '-' || c == '+') && _pos + 1 < _s.size() && _s[_pos + 1] == c)
        {
            fail(std::string("'") + c + c + "' changes a variable");
            return {};
        }
        if (c == '!' || c == '~' || c == '-' || c == '+')
        {
            ++_pos;
            const Value operand = parseUnary();
            if (_failed)
                return {};
            return unary(c, operand);
        }
        if (c == '(')
        {
            ++_pos;
            Value inner = parseBinary(0);
            skipWs();
            if (_pos < _s.size() && _s[_pos] == ')')
                ++_pos;
            else
                fail("')' expected");
            return inner;
        }
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '$')
            return parseNumber();
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '@')
            return parseName();
        fail(std::string("'") + c + "' is not taken");
        return {};
    }

    // DeZog's tokens (Utility.replaceVarsWithValues), each value then masked with & 0xFFFF
    Value parseNumber()
    {
        const size_t start = _pos;
        int64_t value = 0;
        // JavaScript parses the digits into a double: the mask is exact only below 2^53
        auto digits = [&](size_t from, size_t to, int base) {
            int64_t v = 0;
            for (size_t i = from; i < to && v < kExact; ++i)
            {
                const char d = static_cast<char>(std::tolower(static_cast<unsigned char>(_s[i])));
                v = v * base + (std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : d - 'a' + 10);
            }
            if (v >= kExact)
                fail("a number beyond 2^53");
            return v;
        };
        auto hexRun = [&](size_t from) {
            size_t end = from;
            while (end < _s.size() && std::isxdigit(static_cast<unsigned char>(_s[end])))
                ++end;
            return end;
        };

        size_t end = 0;
        if (_s[start] == '$' || _s.compare(start, 2, "0x") == 0)
        {
            const size_t from = start + (_s[start] == '$' ? 1 : 2);
            end = hexRun(from);
            if (end == from)
            {
                fail("hex digits expected at offset " + std::to_string(start));
                return {};
            }
            value = digits(from, end, 16);
        }
        else
        {
            end = hexRun(start);
            const bool decimal = std::all_of(_s.begin() + static_cast<std::ptrdiff_t>(start),
                                             _s.begin() + static_cast<std::ptrdiff_t>(end),
                                             [](char d) { return std::isdigit(static_cast<unsigned char>(d)) != 0; });
            const bool binary = end - start >= 2 && _s[end - 1] == 'b' &&
                                std::all_of(_s.begin() + static_cast<std::ptrdiff_t>(start),
                                            _s.begin() + static_cast<std::ptrdiff_t>(end - 1),
                                            [](char d) { return d == '0' || d == '1'; });
            if (end < _s.size() && _s[end] == 'h')
            {
                value = digits(start, end, 16);
                ++end;
            }
            else if (decimal)
                value = digits(start, end, 10);
            else if (binary)
                value = digits(start, end - 1, 2);
            else
            {
                fail("number '" + _s.substr(start, end - start) + "' is not taken");
                return {};
            }
        }
        if (_failed)
            return {};
        if (end < _s.size() && (IsWordChar(_s[end]) || _s[end] == '.'))
        {
            fail("number at offset " + std::to_string(start) + " runs into a name");
            return {};
        }
        _pos = end;
        value &= 0xFFFF;
        return Value{hex(value), Value::Kind::Number, value, value};
    }

    Value parseName()
    {
        const size_t start = _pos;
        ++_pos;
        while (_pos < _s.size() && (IsWordChar(_s[_pos]) || _s[_pos] == '.'))
            ++_pos;
        if (_pos < _s.size() && _s[_pos] == '\'')
            ++_pos;
        std::string name = _s.substr(start, _pos - start);
        for (char& ch : name)
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        for (const Register& reg : kRegisters)
            if (name == reg.name)
                return Value{reg.core, Value::Kind::Number, 0, reg.max};
        fail("'" + _s.substr(start, _pos - start) + "' is not a register (a label DeZog resolves)");
        return {};
    }

    bool numeric(const Value& v, const char* op)
    {
        if (v.kind == Value::Kind::Either)
            fail(std::string("&& / || used as a number (") + op + ")");
        return !_failed;
    }

    bool int32(const Value& v, const char* op)
    {
        if (v.lo < kInt32Min || v.hi > kInt32Max)
            fail(std::string("an operand of ") + op + " beyond 32 bits");
        return !_failed;
    }

    bool exact(int64_t lo, int64_t hi)
    {
        if (lo <= -kExact || hi >= kExact)
            fail("a value beyond 2^53");
        return !_failed;
    }

    Value unary(char op, const Value& v)
    {
        switch (op)
        {
            case '!': return Value{"(!" + v.text + ")", Value::Kind::Truth, 0, 1};
            case '+':
                if (!numeric(v, "+"))
                    return {};
                return Value{v.text, Value::Kind::Number, v.lo, v.hi};
            case '-':
                if (!numeric(v, "-"))
                    return {};
                return Value{"(-" + v.text + ")", Value::Kind::Number, -v.hi, -v.lo};
            default:  // '~'
                if (!numeric(v, "~") || !int32(v, "~"))
                    return {};
                return Value{"(~" + v.text + ")", Value::Kind::Number, -v.hi - 1, -v.lo - 1};
        }
    }

    static int64_t bitsCeiling(int64_t v)
    {
        int64_t mask = 0;
        while (mask < v)
            mask = (mask << 1) | 1;
        return mask;
    }

    Value combine(const std::string& op, const Value& a, const Value& b)
    {
        const std::string text = "(" + a.text + " " + (op == "===" ? "==" : op == "!==" ? "!=" : op == ">>>" ? ">>" : op) +
                                 " " + b.text + ")";
        if (op == "&&" || op == "||")
        {
            const bool truths = a.kind == Value::Kind::Truth && b.kind == Value::Kind::Truth;
            return Value{text, truths ? Value::Kind::Truth : Value::Kind::Either, 0, 1};
        }
        if (op == "/" || op == "**")
        {
            fail("'" + op + "' gives a fraction");
            return {};
        }
        if (!numeric(a, op.c_str()) || !numeric(b, op.c_str()))
            return {};

        if (op == "===" || op == "!==")
        {
            if (a.kind != b.kind)
                fail("strict equality between a truth and a number");
            return Value{text, Value::Kind::Truth, 0, 1};
        }
        if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=")
            return Value{text, Value::Kind::Truth, 0, 1};

        if (op == "+" || op == "-")
        {
            const int64_t lo = op == "+" ? a.lo + b.lo : a.lo - b.hi;
            const int64_t hi = op == "+" ? a.hi + b.hi : a.hi - b.lo;
            if (!exact(lo, hi))
                return {};
            return Value{text, Value::Kind::Number, lo, hi};
        }
        if (op == "*")
        {
            int64_t lo = 0;
            int64_t hi = 0;
            bool first = true;
            for (const int64_t x : {a.lo, a.hi})
                for (const int64_t y : {b.lo, b.hi})
                {
                    if (x != 0 && (y > kExact / (x < 0 ? -x : x) || y < -kExact / (x < 0 ? -x : x)))
                    {
                        fail("a value beyond 2^53");
                        return {};
                    }
                    const int64_t p = x * y;
                    lo = first ? p : std::min(lo, p);
                    hi = first ? p : std::max(hi, p);
                    first = false;
                }
            if (!exact(lo, hi))
                return {};
            return Value{text, Value::Kind::Number, lo, hi};
        }
        if (op == "%")
        {
            if (b.lo <= 0 && b.hi >= 0)
            {
                fail("a remainder by a value that can be zero");
                return {};
            }
            const int64_t m = std::max(b.lo < 0 ? -b.lo : b.lo, b.hi < 0 ? -b.hi : b.hi) - 1;
            const int64_t lo = a.lo >= 0 ? 0 : std::max(a.lo, -m);
            const int64_t hi = a.hi <= 0 ? 0 : std::min(a.hi, m);
            return Value{text, Value::Kind::Number, lo, hi};
        }
        if (op == "<<" || op == ">>" || op == ">>>")
        {
            if (!int32(a, op.c_str()) || !int32(b, op.c_str()))
                return {};
            if (b.lo < 0 || b.hi > 31)
            {
                fail("a shift count outside 0..31");
                return {};
            }
            if (op == "<<")
            {
                const int64_t lo = std::min(a.lo, a.lo * (int64_t{1} << b.hi));
                const int64_t hi = std::max(a.hi, a.hi * (int64_t{1} << b.hi));
                if (lo < kInt32Min || hi > kInt32Max)
                {
                    fail("a left shift beyond 32 bits");
                    return {};
                }
                return Value{text, Value::Kind::Number, lo, hi};
            }
            if (a.lo < 0)
            {
                fail("a right shift of a value that can be negative");
                return {};
            }
            return Value{text, Value::Kind::Number, 0, a.hi};
        }
        // & ^ |
        if (!int32(a, op.c_str()) || !int32(b, op.c_str()))
            return {};
        if (a.lo >= 0 && b.lo >= 0)
        {
            const int64_t hi = op == "&" ? std::min(a.hi, b.hi) : bitsCeiling(std::max(a.hi, b.hi));
            return Value{text, Value::Kind::Number, 0, hi};
        }
        return Value{text, Value::Kind::Number, kInt32Min, kInt32Max};
    }
};
} // namespace

bool translateCondition(const std::string& condition, std::string& coreOut, std::string& reasonOut)
{
    coreOut.clear();
    bool blank = true;
    for (const char c : condition)
        blank = blank && std::isspace(static_cast<unsigned char>(c));
    if (blank)
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
} // namespace dzrp
