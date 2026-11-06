#include "gdbagentexpr.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>

namespace
{
/// The opcodes taken (gdbsupport/ax.def); the operand bytes follow big-endian
enum : uint8_t
{
    OpAdd = 0x02, OpSub = 0x03, OpMul = 0x04, OpDivSigned = 0x05, OpDivUnsigned = 0x06, OpRemSigned = 0x07,
    OpRemUnsigned = 0x08, OpLsh = 0x09, OpRshSigned = 0x0A, OpRshUnsigned = 0x0B, OpLogNot = 0x0E,
    OpBitAnd = 0x0F, OpBitOr = 0x10, OpBitXor = 0x11, OpBitNot = 0x12, OpEqual = 0x13, OpLessSigned = 0x14,
    OpLessUnsigned = 0x15, OpExt = 0x16, OpRef8 = 0x17, OpRef16 = 0x18, OpRef32 = 0x19, OpIfGoto = 0x20,
    OpGoto = 0x21, OpConst8 = 0x22, OpConst16 = 0x23, OpConst32 = 0x24, OpConst64 = 0x25, OpReg = 0x26,
    OpEnd = 0x27, OpDup = 0x28, OpPop = 0x29, OpZeroExt = 0x2A, OpSwap = 0x2B, OpPick = 0x32, OpRot = 0x33
};

constexpr size_t kMaxSteps = 4096;
constexpr size_t kMaxText = 4096;

/// A value on the symbolic stack: its core text; a constant's value; a known upper bound when it can not be negative
struct Sym
{
    std::string text;
    std::optional<uint64_t> constant;
    std::optional<uint64_t> max;  ///< 0 <= value <= *max (below 2^63)
};

std::string Hex(uint64_t value)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "#%llX", static_cast<unsigned long long>(value));
    return buffer;
}

std::optional<Sym> Constant(uint64_t value)
{
    const int64_t s = static_cast<int64_t>(value);
    // The core lexer takes 15 hex digits (breakpointcondition.cpp ParseNumber): a negative one as -(magnitude)
    if (s >= 0 && value < (uint64_t{1} << 60))
        return Sym{Hex(value), value, value};
    if (s < 0 && s > -(int64_t{1} << 60))
        return Sym{"(-" + Hex(static_cast<uint64_t>(-s)) + ")", value, std::nullopt};
    return std::nullopt;
}

/// gdb's register number (the target description, gdbtarget_z80.cpp) as a core operand
std::optional<Sym> Register(uint16_t regnum)
{
    static const char* const kPairs[] = {"AF", "BC", "DE", "HL", "SP", "PC", "IX", "IY", "AF'", "BC'", "DE'", "HL'"};
    if (regnum < std::size(kPairs))
        return Sym{kPairs[regnum], std::nullopt, 0xFFFF};
    switch (regnum)
    {
        case 12: return Sym{"((I << #8) | R)", std::nullopt, 0xFFFF};
        case 13: return Sym{"(IFF1 | (IFF2 << #1))", std::nullopt, 3};
        case 14: return Sym{"IM", std::nullopt, 2};
        case 15: return Sym{"FD", std::nullopt, 0xFF};
        default: return std::nullopt;  // p1ffd, pfe: no core operand
    }
}

std::optional<uint64_t> Bounded(uint64_t a, uint64_t b, bool multiply)
{
    constexpr uint64_t kLimit = uint64_t{1} << 63;
    if (multiply ? (a != 0 && b >= kLimit / a) : (a + b >= kLimit))
        return std::nullopt;
    return multiply ? a * b : a + b;
}

uint64_t BitsCeiling(uint64_t v)
{
    uint64_t mask = 0;
    while (mask < v)
        mask = (mask << 1) | 1;
    return mask;
}

struct Path
{
    size_t pc = 0;
    std::vector<Sym> stack;
    std::string condition;  ///< the && of the branches taken, empty: none
};

class Executor
{
public:
    explicit Executor(const std::vector<uint8_t>& code)
        : _code(code)
    {
    }

    bool run(std::string& out, std::string& reason)
    {
        std::vector<Path> pending(1);
        size_t paths = 1;
        size_t steps = 0;
        std::string result;
        bool always = false;
        while (!pending.empty() && _error.empty())
        {
            Path path = std::move(pending.back());
            pending.pop_back();
            while (_error.empty())
            {
                if (++steps > kMaxSteps)
                {
                    _error = "too many steps";
                    break;
                }
                std::optional<Path> other;
                bool ended = false;
                Sym value;
                step(path, other, ended, value);
                if (!_error.empty())
                    break;
                if (other)
                {
                    if (++paths > GDBAgentExpression::kMaxPaths)
                    {
                        _error = "more than " + std::to_string(GDBAgentExpression::kMaxPaths) + " paths";
                        break;
                    }
                    pending.push_back(std::move(*other));
                }
                if (!ended)
                    continue;
                // This path's part: its branches && its result; a false constant adds nothing
                if (value.constant && *value.constant == 0)
                    break;
                std::string term = path.condition;
                if (!value.constant)
                    term = term.empty() ? value.text : "(" + term + " && " + value.text + ")";
                if (term.empty())
                    always = true;
                else
                    result = result.empty() ? term : "(" + result + " || " + term + ")";
                if (result.size() > kMaxText)
                    _error = "the condition grows beyond " + std::to_string(kMaxText) + " characters";
                break;
            }
        }
        if (!_error.empty())
        {
            reason = _error;
            return false;
        }
        out = always ? "1" : result.empty() ? "0" : result;
        return true;
    }

private:
    const std::vector<uint8_t>& _code;
    std::string _error;

    std::optional<uint64_t> operand(size_t at, size_t size)
    {
        if (at + size > _code.size())
        {
            _error = "an operand runs past the end";
            return std::nullopt;
        }
        uint64_t v = 0;
        for (size_t i = 0; i < size; ++i)
            v = (v << 8) | _code[at + i];
        return v;
    }

    bool need(const Path& path, size_t n)
    {
        if (path.stack.size() < n)
            _error = "the stack runs empty";
        return _error.empty();
    }

    static Sym pop(Path& path)
    {
        Sym v = std::move(path.stack.back());
        path.stack.pop_back();
        return v;
    }

    // A jump target: forward only (no loops), inside the code
    bool target(size_t from, uint64_t to)
    {
        if (to <= from || to >= _code.size())
            _error = "a jump that is not forward inside the code";
        return _error.empty();
    }

    static uint64_t ShiftCount(const Sym& s) { return *s.constant; }

    void binary(Path& path, uint8_t op)
    {
        if (!need(path, 2))
            return;
        const Sym b = pop(path);
        const Sym a = pop(path);
        auto push = [&](const char* text, std::optional<uint64_t> max) {
            path.stack.push_back(Sym{"(" + a.text + " " + text + " " + b.text + ")", std::nullopt, max});
        };
        const bool constantShift = b.constant && *b.constant <= 63;
        switch (op)
        {
            case OpAdd: push("+", a.max && b.max ? Bounded(*a.max, *b.max, false) : std::nullopt); return;
            case OpSub: push("-", std::nullopt); return;
            case OpMul: push("*", a.max && b.max ? Bounded(*a.max, *b.max, true) : std::nullopt); return;
            case OpDivSigned:
            case OpRemSigned:
            {
                // The agent fails on a zero divisor (the core keeps the dividend); INT64_MIN / -1 overflows
                const int64_t d = b.constant ? static_cast<int64_t>(*b.constant) : 0;
                if (!b.constant || d == 0 || d == -1)
                {
                    _error = "a signed division by anything but a constant other than 0 and -1";
                    return;
                }
                const uint64_t m = static_cast<uint64_t>(d < 0 ? -d : d);
                push(op == OpDivSigned ? "/" : "%",
                     a.max ? std::optional<uint64_t>(op == OpDivSigned ? *a.max : std::min(*a.max, m - 1)) : std::nullopt);
                if (d < 0 && op == OpDivSigned)
                    path.stack.back().max.reset();
                return;
            }
            case OpDivUnsigned:
            case OpRemUnsigned:
                if (!a.max || !b.constant || *b.constant == 0 || !b.max)
                {
                    _error = "an unsigned division of a value that can be negative or by a non-constant";
                    return;
                }
                push(op == OpDivUnsigned ? "/" : "%", op == OpDivUnsigned ? *a.max : std::min(*a.max, *b.constant - 1));
                return;
            case OpLsh:
                if (!constantShift)
                {
                    _error = "a shift by a count that is not a constant 0..63";
                    return;
                }
                push("<<", a.max && ShiftCount(b) < 63 && *a.max < (uint64_t{1} << (63 - ShiftCount(b)))
                               ? std::optional<uint64_t>(*a.max << ShiftCount(b))
                               : std::nullopt);
                return;
            case OpRshUnsigned:
            case OpRshSigned:
                // The core shifts the 64 bits logically: the same as signed for a value that is not negative
                if (!constantShift || (op == OpRshSigned && !a.max))
                {
                    _error = op == OpRshSigned ? "a signed right shift of a value that can be negative or by a non-constant"
                                               : "a shift by a count that is not a constant 0..63";
                    return;
                }
                push(">>", a.max ? std::optional<uint64_t>(*a.max >> ShiftCount(b)) : std::nullopt);
                return;
            case OpBitAnd:
                push("&", a.max && b.max ? std::optional<uint64_t>(std::min(*a.max, *b.max))
                          : a.max ? a.max : b.max);
                return;
            case OpBitOr:
            case OpBitXor:
                push(op == OpBitOr ? "|" : "^",
                     a.max && b.max ? std::optional<uint64_t>(BitsCeiling(std::max(*a.max, *b.max))) : std::nullopt);
                return;
            case OpEqual: push("==", 1); return;
            case OpLessSigned: push("<", 1); return;
            case OpLessUnsigned:
                if (!a.max || !b.max)
                {
                    _error = "an unsigned compare of a value that can be negative";
                    return;
                }
                push("<", 1);
                return;
        }
    }

    void step(Path& path, std::optional<Path>& other, bool& ended, Sym& value)
    {
        if (path.pc >= _code.size())
        {
            _error = "the code ends without 'end'";
            return;
        }
        const size_t at = path.pc;
        const uint8_t op = _code[at];
        path.pc = at + 1;
        switch (op)
        {
            case OpAdd: case OpSub: case OpMul: case OpDivSigned: case OpDivUnsigned: case OpRemSigned:
            case OpRemUnsigned: case OpLsh: case OpRshSigned: case OpRshUnsigned: case OpBitAnd: case OpBitOr:
            case OpBitXor: case OpEqual: case OpLessSigned: case OpLessUnsigned:
                binary(path, op);
                return;
            case OpLogNot:
            case OpBitNot:
            {
                if (!need(path, 1))
                    return;
                const Sym a = pop(path);
                path.stack.push_back(op == OpLogNot ? Sym{"(!" + a.text + ")", std::nullopt, 1}
                                                    : Sym{"(~" + a.text + ")", std::nullopt, std::nullopt});
                return;
            }
            case OpExt:
            case OpZeroExt:
            {
                const auto bits = operand(path.pc, 1);
                if (!bits || !need(path, 1))
                    return;
                path.pc += 1;
                if (*bits == 0 || *bits >= 64)
                    return;  // the 64 bits stay
                const uint64_t mask = (uint64_t{1} << *bits) - 1;
                Sym& a = path.stack.back();
                if (op == OpZeroExt)
                {
                    if (!a.max || *a.max > mask)
                        a = Sym{"(" + a.text + " & " + Hex(mask) + ")", std::nullopt, mask};
                    return;
                }
                // Sign extension: (x & mask) ^ s - s; nothing to do while the sign bit can not be set
                const uint64_t sign = uint64_t{1} << (*bits - 1);
                if (a.max && *a.max < sign)
                    return;
                a = Sym{"(((" + a.text + " & " + Hex(mask) + ") ^ " + Hex(sign) + ") - " + Hex(sign) + ")",
                        std::nullopt, std::nullopt};
                return;
            }
            case OpRef8:
            case OpRef16:
            case OpRef32:
            {
                if (!need(path, 1))
                    return;
                const Sym a = pop(path);
                if (op == OpRef8)
                    path.stack.push_back(Sym{"M(" + a.text + ")", std::nullopt, 0xFF});
                else if (op == OpRef16)
                    path.stack.push_back(Sym{"W(" + a.text + ")", std::nullopt, 0xFFFF});
                else  // little endian: two words
                    path.stack.push_back(Sym{"(W(" + a.text + ") | (W((" + a.text + " + #2)) << #10))", std::nullopt,
                                             0xFFFF'FFFF});
                return;
            }
            case OpConst8:
            case OpConst16:
            case OpConst32:
            case OpConst64:
            {
                const size_t size = op == OpConst8 ? 1 : op == OpConst16 ? 2 : op == OpConst32 ? 4 : 8;
                const auto v = operand(path.pc, size);
                if (!v)
                    return;
                path.pc += size;
                const auto c = Constant(*v);
                if (!c)
                {
                    _error = "a constant beyond 15 hex digits";
                    return;
                }
                path.stack.push_back(*c);
                return;
            }
            case OpReg:
            {
                const auto n = operand(path.pc, 2);
                if (!n)
                    return;
                path.pc += 2;
                const auto r = Register(static_cast<uint16_t>(*n));
                if (!r)
                {
                    _error = "register " + std::to_string(*n) + " has no core operand";
                    return;
                }
                path.stack.push_back(*r);
                return;
            }
            case OpIfGoto:
            case OpGoto:
            {
                const auto to = operand(path.pc, 2);
                if (!to || !target(at, *to))
                    return;
                path.pc += 2;
                if (op == OpGoto)
                {
                    path.pc = static_cast<size_t>(*to);
                    return;
                }
                if (!need(path, 1))
                    return;
                const Sym c = pop(path);
                if (c.constant)
                {
                    if (*c.constant != 0)
                        path.pc = static_cast<size_t>(*to);
                    return;
                }
                // Two paths: the jump taken when c holds, the next instruction when it does not
                Path taken = path;
                taken.pc = static_cast<size_t>(*to);
                taken.condition = taken.condition.empty() ? c.text : "(" + taken.condition + " && " + c.text + ")";
                const std::string notC = "(!" + c.text + ")";
                path.condition = path.condition.empty() ? notC : "(" + path.condition + " && " + notC + ")";
                other = std::move(taken);
                return;
            }
            case OpEnd:
                if (!need(path, 1))
                    return;
                value = path.stack.back();
                ended = true;
                return;
            case OpDup:
                if (!need(path, 1))
                    return;
                path.stack.push_back(path.stack.back());
                return;
            case OpPop:
                if (!need(path, 1))
                    return;
                path.stack.pop_back();
                return;
            case OpSwap:
                if (!need(path, 2))
                    return;
                std::swap(path.stack[path.stack.size() - 1], path.stack[path.stack.size() - 2]);
                return;
            case OpRot:
            {
                // a b c => c a b (c the top)
                if (!need(path, 3))
                    return;
                const size_t n = path.stack.size();
                Sym c = std::move(path.stack[n - 1]);
                path.stack[n - 1] = std::move(path.stack[n - 2]);
                path.stack[n - 2] = std::move(path.stack[n - 3]);
                path.stack[n - 3] = std::move(c);
                return;
            }
            case OpPick:
            {
                const auto n = operand(path.pc, 1);
                if (!n || !need(path, static_cast<size_t>(*n) + 1))
                    return;
                path.pc += 1;
                path.stack.push_back(path.stack[path.stack.size() - 1 - static_cast<size_t>(*n)]);
                return;
            }
            default:
            {
                char buffer[48];
                std::snprintf(buffer, sizeof(buffer), "opcode #%02X is not taken", op);
                _error = buffer;
                return;
            }
        }
    }
};

int HexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}
} // namespace

bool GDBAgentExpression::toCondition(const std::vector<uint8_t>& bytecode, std::string& out, std::string& reason)
{
    out.clear();
    if (bytecode.empty())
    {
        reason = "empty expression";
        return false;
    }
    Executor executor(bytecode);
    return executor.run(out, reason);
}

bool GDBAgentExpression::listToCondition(const std::string& list, std::string& out, std::string& reason)
{
    out.clear();
    std::string combined;
    size_t pos = 0;
    while (pos < list.size())
    {
        if (list[pos] != 'X')
        {
            reason = "a condition list entry that is not 'X len,bytes'";
            return false;
        }
        const size_t comma = list.find(',', pos);
        if (comma == std::string::npos || comma == pos + 1)
        {
            reason = "an expression without its length";
            return false;
        }
        size_t length = 0;
        for (size_t i = pos + 1; i < comma; ++i)
        {
            const int d = HexDigit(list[i]);
            if (d < 0 || length > 0xFFFF)
            {
                reason = "a bad expression length";
                return false;
            }
            length = length * 16 + static_cast<size_t>(d);
        }
        const size_t bytesAt = comma + 1;
        if (bytesAt + length * 2 > list.size())
        {
            reason = "an expression shorter than its length";
            return false;
        }
        std::vector<uint8_t> code(length);
        for (size_t i = 0; i < length; ++i)
        {
            const int hi = HexDigit(list[bytesAt + 2 * i]);
            const int lo = HexDigit(list[bytesAt + 2 * i + 1]);
            if (hi < 0 || lo < 0)
            {
                reason = "a bad expression byte";
                return false;
            }
            code[i] = static_cast<uint8_t>((hi << 4) | lo);
        }
        std::string one;
        if (!toCondition(code, one, reason))
            return false;
        // gdb stops when any of the conditions holds
        combined = combined.empty() ? one : "(" + combined + " || " + one + ")";
        pos = bytesAt + length * 2;
        if (pos < list.size())
        {
            if (list[pos] != ';')
            {
                reason = "text after an expression";
                return false;
            }
            ++pos;
        }
    }
    if (combined.empty())
    {
        reason = "no condition";
        return false;
    }
    out = combined;
    return true;
}
