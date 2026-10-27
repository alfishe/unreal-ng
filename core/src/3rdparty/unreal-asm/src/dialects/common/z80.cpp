#include "dialects/common/z80.h"

#include <algorithm>
#include <iterator>

namespace unrealasm::dialects::z80
{
namespace
{
constexpr std::string_view kMnemonics[] = {
    "adc", "add", "and", "bit", "call", "ccf", "cp", "cpd", "cpdr", "cpi", "cpir", "cpl", "daa", "dec", "di", "djnz", "ei", "ex",
    "exx", "halt", "im", "in", "inc", "ind", "indr", "ini", "inir", "jp", "jr", "ld", "ldd", "lddr", "ldi", "ldir", "neg", "nop",
    "or", "otdr", "otir", "out", "outd", "outi", "pop", "push", "res", "ret", "reti", "retn", "rl", "rla", "rlc", "rlca", "rld",
    "rr", "rra", "rrc", "rrca", "rrd", "rst", "sbc", "scf", "set", "sla", "sli", "sll", "sra", "srl", "sub", "xor", "inf",
};
// The Z80N additions that have a mnemonic of their own (ADD / PUSH / JP forms of the Next reuse the Z80 names); they
// are read only in the Next mode, since a classic source may well use TEST or MIRROR as a label or macro
constexpr std::string_view kZ80nMnemonics[] = {
    "swapnib", "mirror", "test", "bsla", "bsra", "bsrl", "bsrf", "brlc", "mul", "nextreg", "pixeldn", "pixelad", "setae", "outinb", "ldix",
    "ldws", "lddx", "ldirx", "ldpirx", "lddrx", "ldirscale",
};
constexpr std::string_view kRegisters[] = {
    "a", "b", "c", "d", "e", "h", "l", "i", "r", "af", "af'", "bc", "de", "hl", "sp", "ix", "iy", "ixh", "ixl", "iyh", "iyl", "f",
};
constexpr std::string_view kConditions[] = {"nz", "z", "nc", "c", "po", "pe", "p", "m"};
}  // namespace

std::string Lower(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c); });
    return out;
}

std::string HexDigits(uint64_t value, int digits)
{
    static const char kDigits[] = "0123456789ABCDEF";
    std::string out;
    do
    {
        out.insert(out.begin(), kDigits[value & 0xF]);
        value >>= 4;
    } while (value);
    if (static_cast<int>(out.size()) < digits)
        out.insert(out.begin(), static_cast<size_t>(digits) - out.size(), '0');
    return out;
}

std::string Upper(std::string_view text)
{
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c); });
    return out;
}

bool IsMnemonic(std::string_view lower)
{
    return !lower.empty() && std::find(std::begin(kMnemonics), std::end(kMnemonics), lower) != std::end(kMnemonics);
}

bool IsZ80nMnemonic(std::string_view lower)
{
    return !lower.empty() && std::find(std::begin(kZ80nMnemonics), std::end(kZ80nMnemonics), lower) != std::end(kZ80nMnemonics);
}

bool EnablesZ80n(std::string_view directive)
{
    const std::string lower = Lower(directive);
    const size_t word = lower.find_first_not_of(" \t");
    if (word == std::string::npos)
        return false;
    if (lower.compare(word, 6, "device") == 0)
        return lower.find("zxspectrumnext", word) != std::string::npos;
    return lower.compare(word, 3, "opt") == 0 && lower.find("--zxnext", word) != std::string::npos;
}

std::string NormalizeRegister(std::string_view lower)
{
    if (lower == "hx" || lower == "xh")
        return "ixh";
    if (lower == "lx" || lower == "xl")
        return "ixl";
    if (lower == "hy" || lower == "yh")
        return "iyh";
    if (lower == "ly" || lower == "yl")
        return "iyl";
    return std::string(lower);
}

bool IsRegister(std::string_view normalized)
{
    return std::find(std::begin(kRegisters), std::end(kRegisters), normalized) != std::end(kRegisters);
}

bool IsCondition(std::string_view lower)
{
    return std::find(std::begin(kConditions), std::end(kConditions), lower) != std::end(kConditions);
}

int SplitArity(std::string_view m)
{
    if (m == "ld" || m == "ex" || m == "add" || m == "adc" || m == "sbc" || m == "in" || m == "out" || m == "bit" || m == "res" || m == "set")
        return 2;
    if (m == "push" || m == "pop" || m == "inc" || m == "dec" || m == "sub" || m == "and" || m == "or" || m == "xor" || m == "cp" || m == "rl" ||
        m == "rr" || m == "rlc" || m == "rrc" || m == "sla" || m == "sra" || m == "sli" || m == "sll" || m == "srl")
        return 1;
    return 0;
}

bool TakesCondition(std::string_view m)
{
    return m == "jp" || m == "jr" || m == "call" || m == "ret";
}

std::optional<Z80nForm> Z80nEncoding(const ir::Statement& s)
{
    if (s.kind != ir::Statement::Kind::Instruction)
        return std::nullopt;
    const std::string m = Lower(s.mnemonic);
    const auto& ops = s.operands;
    const auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == ir::Operand::Kind::Register && ops[k].text == name; };
    const auto value = [&](size_t k) { return k < ops.size() && ops[k].kind == ir::Operand::Kind::Immediate; };
    const auto form = [](int code, char layout = 'n') { return std::optional<Z80nForm>(Z80nForm{code, layout}); };
    if (m == "swapnib") return form(0x23);
    if (m == "mirror") return form(0x24);
    if (m == "test" && value(0)) return form(0x27, 'b');
    if (m == "bsla") return form(0x28);
    if (m == "bsra") return form(0x29);
    if (m == "bsrl") return form(0x2A);
    if (m == "bsrf") return form(0x2B);
    if (m == "brlc") return form(0x2C);
    if (m == "mul") return form(0x30);
    if (m == "add" && ops.size() == 2 && reg(1, "a") && (reg(0, "hl") || reg(0, "de") || reg(0, "bc")))
        return form(reg(0, "hl") ? 0x31 : reg(0, "de") ? 0x32 : 0x33);
    if (m == "add" && ops.size() == 2 && value(1) && (reg(0, "hl") || reg(0, "de") || reg(0, "bc")))
        return form(reg(0, "hl") ? 0x34 : reg(0, "de") ? 0x35 : 0x36, 'w');
    if (m == "push" && ops.size() == 1 && value(0)) return form(0x8A, 'W');
    if (m == "outinb") return form(0x90);
    if (m == "nextreg" && ops.size() == 2 && value(0) && value(1)) return form(0x91, 'B');
    if (m == "nextreg" && ops.size() == 2 && value(0) && reg(1, "a")) return form(0x92, 'b');
    if (m == "pixeldn") return form(0x93);
    if (m == "pixelad") return form(0x94);
    if (m == "setae") return form(0x95);
    if (m == "jp" && ops.size() == 1 && ops[0].kind == ir::Operand::Kind::Indirect && ops[0].text == "c") return form(0x98);
    if (m == "ldix") return form(0xA4);
    if (m == "ldws") return form(0xA5);
    if (m == "lddx") return form(0xAC);
    if (m == "ldirx") return form(0xB4);
    if (m == "ldirscale") return form(0xB6);
    if (m == "ldpirx") return form(0xB7);
    if (m == "lddrx") return form(0xBC);
    return std::nullopt;
}

uint32_t StopAtCarryProduct(uint32_t multiplicand, uint32_t multiplier)
{
    const uint32_t x = multiplicand & 0xFFFF, y = multiplier & 0xFFFF;
    uint32_t r = 0;
    bool started = false;
    for (int bit = 15; bit >= 0 && r <= 0xFFFF; --bit)
    {
        const bool set = (y >> bit) & 1;
        started = started || set;
        if (!started)
            continue;
        r *= 2;
        if (set && r <= 0xFFFF)
            r += x;
    }
    return r & 0xFFFF;
}

std::optional<int64_t> ConstantValue(const ir::Expr& e, const std::function<std::optional<int64_t>(const std::string&)>& symbol)
{
    using Kind = ir::Expr::Kind;
    using ir::Op;
    switch (e.kind)
    {
        case Kind::Number: return e.value;
        case Kind::Group: return ConstantValue(e.args[0], symbol);
        case Kind::Symbol: return symbol(e.text);
        case Kind::Unary:
        {
            const auto a = ConstantValue(e.args[0], symbol);
            if (!a)
                return std::nullopt;
            switch (e.op)
            {
                case Op::Negate: return static_cast<int32_t>(0u - static_cast<uint32_t>(*a));
                case Op::Plus: return *a;
                case Op::Not: return static_cast<int32_t>(~static_cast<uint32_t>(*a));
                default: return std::nullopt;
            }
        }
        case Kind::Binary:
        {
            const auto a = ConstantValue(e.args[0], symbol);
            const auto b = a ? ConstantValue(e.args[1], symbol) : std::nullopt;
            if (!b)
                return std::nullopt;
            const uint32_t x = static_cast<uint32_t>(*a), y = static_cast<uint32_t>(*b);
            switch (e.op)
            {
                case Op::Add: return static_cast<int32_t>(x + y);
                case Op::Sub: return static_cast<int32_t>(x - y);
                case Op::Mul: return static_cast<int32_t>(x * y);
                case Op::And: return static_cast<int32_t>(x & y);
                case Op::Or: return static_cast<int32_t>(x | y);
                case Op::Xor: return static_cast<int32_t>(x ^ y);
                case Op::Shl: return *b >= 0 && *b <= 31 ? std::optional<int64_t>(static_cast<int32_t>(x << y)) : std::nullopt;
                default: return std::nullopt;
            }
        }
        default: return std::nullopt;
    }
}
}  // namespace unrealasm::dialects::z80
