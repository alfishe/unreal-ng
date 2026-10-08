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
}  // namespace unrealasm::dialects::z80
