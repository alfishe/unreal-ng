#include "dialects/common/classicbackend.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <iterator>
#include <map>
#include <set>

#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

// pasmo's table of precedence (pasmodoc "Operators"): the unary operators sit below the comparisons and HIGH / LOW
// at the bottom, so they are always written in parentheses here
int Priority(Op op)
{
    switch (op)
    {
        case Op::Mul: case Op::Div: case Op::Mod: case Op::Shl: case Op::Shr: case Op::ShrUnsigned: return 8;
        case Op::Add: case Op::Sub: return 7;
        case Op::Equal: case Op::NotEqual: case Op::Less: case Op::Greater: case Op::LessEqual: case Op::GreaterEqual: return 6;
        case Op::And: return 4;
        case Op::Or: case Op::Xor: return 3;
        case Op::LogicalAnd: return 2;
        case Op::LogicalOr: return 1;
        default: return 10;
    }
}

const char* Symbol(Op op)
{
    switch (op)
    {
        case Op::Add: return "+";
        case Op::Sub: return "-";
        case Op::Mul: return "*";
        case Op::Div: return "/";
        case Op::Mod: return " MOD ";
        case Op::And: return " AND ";
        case Op::Or: return " OR ";
        case Op::Xor: return " XOR ";
        case Op::Shl: return " SHL ";
        case Op::Shr: case Op::ShrUnsigned: return " SHR ";   // 16-bit unsigned words: both fill with zeros
        case Op::Equal: return " EQ ";
        case Op::NotEqual: return " NE ";
        case Op::Less: return " LT ";
        case Op::Greater: return " GT ";
        case Op::LessEqual: return " LE ";
        case Op::GreaterEqual: return " GE ";
        case Op::LogicalAnd: return " && ";
        case Op::LogicalOr: return " || ";
        default: return "?";
    }
}

// z80asm (z88dk): C's priorities
int PriorityC(Op op)
{
    switch (op)
    {
        case Op::Mul: case Op::Div: case Op::Mod: return 9;
        case Op::Add: case Op::Sub: return 8;
        case Op::Shl: case Op::Shr: case Op::ShrUnsigned: return 7;
        case Op::Less: case Op::Greater: case Op::LessEqual: case Op::GreaterEqual: return 6;
        case Op::Equal: case Op::NotEqual: return 5;
        case Op::And: return 4;
        case Op::Xor: return 3;
        case Op::Or: return 2;
        case Op::LogicalAnd: return 1;
        case Op::LogicalOr: return 0;
        default: return 10;
    }
}

const char* SymbolC(Op op)
{
    switch (op)
    {
        case Op::Add: return "+";
        case Op::Sub: return "-";
        case Op::Mul: return "*";
        case Op::Div: return "/";
        case Op::Mod: return "%";
        case Op::And: return "&";
        case Op::Or: return "|";
        case Op::Xor: return "^";
        case Op::Shl: return "<<";
        case Op::Shr: case Op::ShrUnsigned: return ">>";
        case Op::Equal: return "==";
        case Op::NotEqual: return "!=";
        case Op::Less: return "<";
        case Op::Greater: return ">";
        case Op::LessEqual: return "<=";
        case Op::GreaterEqual: return ">=";
        case Op::LogicalAnd: return "&&";
        case Op::LogicalOr: return "||";
        default: return "?";
    }
}

// The displacement emulated without PHASE: the run address minus the address where the code is put
constexpr const char* kDelta = "__UNREALASM_D";

// Words pasmo (and z80asm) reserve whatever their case (mnemonics, registers, conditions, operators, directives)
const std::set<std::string> kReserved = {
    "a", "b", "c", "d", "e", "h", "l", "i", "r", "af", "bc", "de", "hl", "sp", "ix", "iy", "ixh", "ixl", "iyh", "iyl",
    "nz", "z", "nc", "po", "pe", "p", "m",
    "and", "or", "xor", "not", "mod", "shl", "shr", "eq", "ne", "lt", "le", "gt", "ge", "high", "low", "nul", "defined",
    "org", "equ", "defl", "db", "defb", "defm", "defs", "ds", "dw", "defw", "end", "endif", "endm", "endp", "exitm", "if",
    "ifdef", "ifndef", "include", "incbin", "irp", "local", "macro", "proc", "public", "rept", "else",
};

// z80asm's keywords (src/z80asm keyword.def and scan_def.h of z88dk 2.3): the instructions, registers and directives
// of every CPU it assembles for; a label of such a name is refused or, worse, read as the keyword
const std::set<std::string> kZ80asmKeywords = {
    "a", "abc", "abs", "aci", "acos", "acosh", "adc", "add", "ade", "adi", "af", "ahl", "aix", "aiy", "altd", "ana",
    "and", "ani", "arhl", "asin", "asinh", "asmpc", "asp", "assume", "atan", "atan2", "atanh", "b", "bc", "bcde",
    "binary", "bit", "bool", "brlc", "bsla", "bsra", "bsrf", "bsrl", "c", "c_c", "c_eq", "c_ge", "c_geu", "c_gt",
    "c_gtu", "c_le", "c_leu", "c_line", "c_lo", "c_lt", "c_ltu", "c_lz", "c_m", "c_nc", "c_ne", "c_nv", "c_nz", "c_p",
    "c_pe", "c_po", "c_v", "c_z", "call", "cbm", "cbrt", "cc", "ccf", "ceil", "ceq", "cge", "cgt", "cle", "clo", "clr",
    "clt", "clz", "cm", "cma", "cmc", "cmp", "cnc", "cne", "cnv", "cnz", "convc", "convd", "copy", "copyr", "cos",
    "cosh", "cp", "cpd", "cpdr", "cpe", "cpi", "cpir", "cpl", "cpo", "cv", "cz", "d", "daa", "dad", "db", "dcr", "dcx",
    "de", "dec", "defb", "defc", "define", "defl", "defp", "defq", "defs", "defw", "dehl", "di", "div", "divs", "djnz",
    "dp", "dq", "ds", "dsub", "dw", "dwjnz", "e", "ei", "eir", "elif", "elifdef", "elifndef", "else", "endif", "endm",
    "endr", "eq", "equ", "ex", "exitm", "exp", "exp2", "extern", "exx", "f", "flag", "float", "floor", "fmod",
    "fsyscall", "ge", "geu", "global", "gt", "gtu", "h", "halt", "hl", "hld", "hli", "hlt", "htr", "hypot", "i", "ibox",
    "idet", "if", "ifdef", "ifndef", "iir", "il", "im", "in", "in0", "inc", "incbin", "include", "ind", "ind2", "ind2r",
    "indm", "indmr", "indr", "indrx", "ini", "ini2", "ini2r", "inim", "inimr", "inir", "inirx", "inr", "inx", "ioe",
    "ioi", "ip", "ipres", "ipset", "is", "ix", "ixh", "ixl", "iy", "iyh", "iyl", "j_c", "j_eq", "j_ge", "j_geu", "j_gt",
    "j_gtu", "j_le", "j_leu", "j_lo", "j_lt", "j_ltu", "j_lz", "j_m", "j_nc", "j_ne", "j_nv", "j_nz", "j_p", "j_pe",
    "j_po", "j_v", "j_z", "jc", "jeq", "jge", "jgeu", "jgt", "jgtu", "jk", "jkhl", "jle", "jleu", "jlo", "jlt", "jltu",
    "jlz", "jm", "jmp", "jnc", "jne", "jnk", "jnv", "jnx5", "jnz", "jp", "jp3", "jpe", "jpo", "jr", "jre", "jv", "jx5",
    "jz", "k", "l", "ld", "lda", "ldax", "ldd", "lddr", "lddrx", "lddsr", "lddx", "ldf", "ldh", "ldhi", "ldhl", "ldi",
    "ldir", "ldirx", "ldisr", "ldix", "ldl", "ldp", "ldpirx", "ldrx", "ldsi", "ldws", "le", "lea", "leu", "lhld",
    "lhlde", "lhlx", "lib", "lil", "line", "lirx", "lis", "lo", "local", "log", "log10", "log2", "lprx", "lsddr",
    "lsdr", "lsidr", "lsir", "lt", "ltu", "lxi", "lxpc", "lz", "m", "macro", "mb", "mirr", "mirror", "mlt", "mmu",
    "mmu0", "mmu1", "mmu2", "mmu3", "mmu4", "mmu5", "mmu6", "mmu7", "mov", "mul", "muls", "mulu", "mulub", "muluw",
    "mvi", "nc", "ne", "neg", "nextreg", "nk", "nop", "nreg", "nv", "nx5", "nz", "or", "ora", "org", "ori", "otd2r",
    "otdm", "otdmr", "otdr", "otdrx", "oti2r", "otib", "otim", "otimr", "otir", "otirx", "out", "out0", "outd", "outd2",
    "outi", "outi2", "outinb", "ovrst8", "p", "pbc", "pchl", "pde", "pe", "pea", "phl", "pi", "pix", "pixelad",
    "pixeldn", "piy", "po", "pop", "pow", "pp", "psp", "psw", "public", "push", "pw", "px", "pxad", "pxdn", "py", "pz",
    "r", "r_c", "r_eq", "r_ge", "r_geu", "r_gt", "r_gtu", "r_le", "r_leu", "r_lo", "r_lt", "r_ltu", "r_lz", "r_m",
    "r_nc", "r_ne", "r_nv", "r_nz", "r_p", "r_pe", "r_po", "r_v", "r_z", "ral", "rar", "rc", "rdel", "rdmode", "rept",
    "reptc", "repti", "req", "res", "ret", "ret3", "reti", "retn", "retn3", "rge", "rgeu", "rgt", "rgtu", "rim", "rl",
    "rla", "rlb", "rlc", "rlca", "rld", "rlde", "rle", "rleu", "rlo", "rlt", "rltu", "rlz", "rm", "rnc", "rne", "rnv",
    "rnz", "round", "rp", "rpe", "rpo", "rr", "rra", "rrb", "rrc", "rrca", "rrd", "rrhl", "rsmix", "rst", "rstv", "rv",
    "rz", "s", "sbb", "sbc", "sbi", "sbox", "scf", "section", "set", "setae", "setfloat", "setsysp", "setusr",
    "setusrp", "shld", "shlde", "shlx", "sil", "sim", "sin", "sinh", "sis", "sla", "sli", "sll", "slp", "sls", "sp",
    "sphl", "sqrt", "sra", "sret", "srl", "sta", "stae", "stax", "stc", "stmix", "stop", "su", "sub", "sui", "sures",
    "swap", "swapnib", "syscall", "sysret", "tan", "tanh", "test", "tra", "trunc", "tst", "tstio", "uma", "ums",
    "undef", "v", "x", "x5", "xbc", "xchg", "xde", "xdef", "xhl", "xix", "xiy", "xlib", "xor", "xp", "xpc", "xra",
    "xref", "xri", "xsp", "xthl", "xy", "ybc", "yde", "yhl", "yix", "yiy", "yp", "ysp", "z", "zbc", "zde", "zhl", "zix",
    "ziy", "zp", "zsp"
};

/// A word the target keeps for itself: its reserved words and every Z80 mnemonic (a label RET or LD is refused)
bool Reserved(const std::string& name, bool z80asm)
{
    const std::string lower = z80::Lower(name);
    return kReserved.count(lower) || z80::IsMnemonic(lower) || lower == "sll" || lower == "inf" || (z80asm && kZ80asmKeywords.count(lower));
}

/// A label pasmo reads as written: a letter, "_", "?", "@" or "." first, then letters, digits and _ ? @ . ("$" would be
/// ignored inside a name, so a name holding one is renamed too)
bool IsValidLabel(const std::string& name, bool z80asm)
{
    if (z80asm)   // letters, digits and "_" ("." starts a label in z80asm's other syntax)
    {
        if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_'))
            return false;
        for (const char c : name)
            if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
                return false;
        return true;
    }
    if (name.empty() || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_' || name[0] == '?' || name[0] == '@' || name[0] == '.'))
        return false;
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '?' || c == '@' || c == '.'))
            return false;
    return true;
}

std::string SafeName(const std::string& name)
{
    std::string renamed = "L_" + name;
    for (char& c : renamed)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            c = '_';
    return renamed;
}

bool UsesCurrent(const Expr& e)
{
    if (e.kind == Expr::Kind::Current)
        return true;
    for (const Expr& a : e.args)
        if (UsesCurrent(a))
            return true;
    return false;
}

struct Writer
{
    const BackendOptions& options;
    Diagnostics& diagnostics;
    ClassicTarget target;
    uint32_t line = 0;
    std::map<std::string, std::string> renames{};
    std::vector<std::map<std::string, std::string>> localScopes{};
    bool inMacro = false;
    bool displaced = false;   // a displacement may be active: labels and $ go through __UNREALASM_D
    int trueValue = 0;
    std::string problem{};    // set when an expression has no pasmo form
    const std::set<std::string>* redefinable = nullptr;
    int wordBits = 0;              // 16: the source computes in 16-bit words (z80asm computes in 32 bits)
    bool unsignedWords = false;
    int sections = 0;
    bool phased = false;           // z80asm: a PHASE may be active
    std::string pcLabel{};         // the label standing for $ in the statement being written
    int pcLabels = 0;
    // The names the assembly defines outside a block that tests them (sjasmplus' "exist" answers from the whole source
    // in the same way): z80asm's tests for a defined name are decided from them
    std::set<std::string> definedNames{};
    std::set<std::string> ownNames{};   // the names this file defines
    std::map<std::string, uint32_t> firstDefinition{};   // the source line that first defines each name
    std::set<std::string> addressLabels{};   // names whose value is an address in this file (labels, EQU $+..., names built on them)
    const std::set<const Statement*>* virtualOrgs = nullptr;   // z80asm: ORGs followed by labels only
    std::string virtualAddress{};   // the address of such a region while it lasts

    bool UsesAddressLabel(const Expr& e) const
    {
        if (e.kind == Expr::Kind::Symbol && addressLabels.count(e.text))
            return true;
        for (const Expr& a : e.args)
            if (UsesAddressLabel(a))
                return true;
        return false;
    }

    bool Z80asm() const { return target == ClassicTarget::Z80asm; }
    const char* TargetName() const { return Z80asm() ? "z80asm" : "pasmo"; }
    int TargetTrue() const { return Z80asm() ? 1 : -1; }
    int Priority_(Op op) const { return Z80asm() ? PriorityC(op) : Priority(op); }
    const char* Symbol_(Op op) const { return Z80asm() ? SymbolC(op) : Symbol(op); }
    const char* RepeatEnd() const { return Z80asm() ? "ENDR" : "ENDM"; }
    /// The REPT a conditional loop runs in: z80asm expands every pass even after EXITM, so its bound is lower
    std::string LoopRept() const
    {
        return Z80asm() ? "REPT 1024               ; unreal-asm: a conditional loop, at most 1024 passes in z80asm" : "REPT 65535";
    }

    std::string Name(const std::string& name) const
    {
        if (inMacro && name.size() == 2 && name[0] == '\\')
            return std::string("_arg") + name[1];
        for (auto it = localScopes.rbegin(); it != localScopes.rend(); ++it)
        {
            const auto found = it->find(name);
            if (found != it->end())
                return found->second;
        }
        const auto found = renames.find(name);
        if (found != renames.end())
            return found->second;
        return Reserved(name, Z80asm()) || !IsValidLabel(name, Z80asm()) ? SafeName(name) : name;
    }

    std::string Hex(int64_t value, int digits) const
    {
        // The width is clamped to the buffer: gcc 13 rejects an unbounded one (-Werror=format-truncation)
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%0*llX", std::clamp(digits, 1, 30), static_cast<unsigned long long>(value & 0xFFFF));
        return std::string(Z80asm() ? "$" : "#") + buffer;
    }

    std::string Number(const Expr& e) const
    {
        switch (e.spelling)
        {
            case ir::NumberSpelling::Hex: return Hex(e.value, e.digits);
            case ir::NumberSpelling::Binary:
            {
                std::string bits;
                for (int k = std::max(e.digits, 1) - 1; k >= 0; --k)
                    bits.push_back(((e.value >> k) & 1) ? '1' : '0');
                return "%" + bits;
            }
            case ir::NumberSpelling::Character:
                if (e.text.size() == 1 && Z80asm())
                    return e.text == "'" ? "'\\''" : e.text == "\\" ? "'\\\\'" : "'" + e.text + "'";
                if (e.text.size() == 1)
                    return e.text == "'" ? "''''" : "'" + e.text + "'";
                return std::to_string(e.value & 0xFFFF);
            default:
                return e.value < 0 ? "(-" + std::to_string(-e.value) + ")" : std::to_string(e.value);
        }
    }

    static Expr Grouped(Expr e)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(std::move(e));
        return g;
    }

    std::string Unary(const char* op, const Expr& a)
    {
        const std::string inner = Print(a);
        if (Z80asm())   // C: unary operators bind first
            return std::string(op) + (a.kind == Expr::Kind::Binary || (a.kind == Expr::Kind::Unary && !inner.empty() && inner[0] == op[0]) ? "(" + inner + ")" : inner);
        return std::string("(") + op + (a.kind == Expr::Kind::Binary ? "(" + inner + ")" : inner) + ")";
    }

    static Expr Masked16(Expr e)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(Expr::Binary(Op::And, std::move(e), Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4)));
        return g;
    }

    std::string TrueIsOne(const Expr& e)
    {
        Expr top = e;
        for (Expr& a : top.args)
            if (a.kind == Expr::Kind::Binary || a.kind == Expr::Kind::Unary)
            {
                Expr printed = Expr::Make(Expr::Kind::Raw);
                printed.text = "(" + Print(a) + ")";
                a = std::move(printed);
            }
        const int source = trueValue;
        trueValue = 0;
        const std::string out = Print(top);
        trueValue = source;
        return "(-(" + out + "))";   // the target's true has the other sign (pasmo #FFFF, z80asm 1)
    }

    std::string Print(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return Number(e);
            case Expr::Kind::Symbol: return Name(e.text);
            case Expr::Kind::Current:
                if (!virtualAddress.empty())
                    return "(" + virtualAddress + ")";
                if (Z80asm() && phased && !inMacro)   // (a macro keeps $: the label would repeat at every call)
                {
                    // z80asm 2.3 adds the section's ORG to $ inside PHASE (labels come out right): a label put before
                    // the statement stands for it
                    if (pcLabel.empty())
                    {
                        std::string file = options.fileName.empty() ? std::string("main") : options.fileName;
                        for (char& c : file)
                            if (!std::isalnum(static_cast<unsigned char>(c)))
                                c = '_';
                        pcLabel = "__UNREALASM_PC_" + file + "_" + std::to_string(++pcLabels);
                    }
                    return pcLabel;
                }
                return displaced ? std::string("($+") + kDelta + ")" : "$";
            case Expr::Kind::CurrentPhysical:
                if (Z80asm())
                {
                    problem = "$$$ (where the code is put inside PHASE)";
                    return "0";
                }
                return "$";
            case Expr::Kind::CurrentPage:
                problem = "$$ (the current page)";
                return "0";
            case Expr::Kind::Group: return "(" + Print(e.args[0]) + ")";
            case Expr::Kind::Memory:
                problem = "a memory read while assembling ({...})";
                return "0";
            case Expr::Kind::Raw: return e.text;
            case Expr::Kind::Defined:   // ALASM ?label: 0 when defined
                if (Z80asm())
                    return e.args[0].kind == Expr::Kind::Symbol && definedNames.count(e.args[0].text) ? "0" : "65535";
                return "(NOT (DEFINED " + Print(e.args[0]) + "))";
            case Expr::Kind::Unary:
                switch (e.op)
                {
                    case Op::Negate: return Unary("-", e.args[0]);
                    case Op::Plus: return Unary("+", e.args[0]);
                    case Op::Not: return Unary("NOT ", e.args[0]);
                    case Op::LogicalNot:
                        if (trueValue != 0 && trueValue != TargetTrue())
                            return TrueIsOne(e);
                        return Unary("!", e.args[0]);
                    case Op::High:
                        if (Z80asm())
                            return Print(Grouped(Expr::Binary(Op::And, Expr::Binary(Op::Shr, Grouped(e.args[0]), Expr::Number(8)), Expr::Number(255))));
                        return Unary("HIGH ", e.args[0]);
                    case Op::Low:
                        if (Z80asm())
                            return Print(Grouped(Expr::Binary(Op::And, Grouped(e.args[0]), Expr::Number(255))));
                        return Unary("LOW ", e.args[0]);
                    case Op::Exists:
                        if (Z80asm())   // decided when converting: z80asm's IFDEF sees no labels
                            return e.args[0].kind == Expr::Kind::Symbol && definedNames.count(e.args[0].text) ? "1" : "0";
                        return Unary("DEFINED ", e.args[0]);
                    case Op::SwapBytes:
                        return Print(Grouped(Expr::Binary(Op::Or, Expr::Binary(Op::Shl, Grouped(e.args[0]), Expr::Number(8)),
                                                          Expr::Binary(Op::Shr, Grouped(e.args[0]), Expr::Number(8)))));
                    default: return Print(e.args[0]);
                }
            case Expr::Kind::Binary:
            {
                if (e.op == Op::RotateLeft16 || e.op == Op::RotateRight16)
                {
                    // 16-bit words: (a SHL b) OR (a SHR (16-b))
                    const bool right = e.op == Op::RotateRight16;
                    const Expr a = Grouped(e.args[0]);
                    const Expr b = Grouped(e.args[1]);
                    Expr rotated = Grouped(Expr::Binary(Op::Or, Expr::Binary(right ? Op::Shr : Op::Shl, Z80asm() ? Masked16(e.args[0]) : a, b),
                                                        Expr::Binary(right ? Op::Shl : Op::Shr, Z80asm() ? Masked16(e.args[0]) : a,
                                                                     Grouped(Expr::Binary(Op::Sub, Expr::Number(16), b)))));
                    return Print(Z80asm() ? Masked16(rotated) : rotated);   // z80asm computes in 32 bits
                }
                const bool comparison = e.op == Op::Equal || e.op == Op::NotEqual || e.op == Op::Less || e.op == Op::Greater ||
                                        e.op == Op::LessEqual || e.op == Op::GreaterEqual;
                if (comparison && trueValue != 0 && trueValue != TargetTrue())
                    return TrueIsOne(e);
                if (Z80asm() && wordBits == 16 && unsignedWords && (comparison || e.op == Op::Div || e.op == Op::Mod || e.op == Op::Shr))
                {
                    // z80asm computes in 32 bits: the 16-bit unsigned words of the source masked (a number or a
                    // comparison with 0 needs no mask)
                    auto masked = [](const Expr& a) {
                        const Expr& x = a.kind == Expr::Kind::Group ? a.args[0] : a;
                        return x.kind == Expr::Kind::Binary && x.op == Op::And && x.args[1].kind == Expr::Kind::Number && x.args[1].value == 0xFFFF;
                    };
                    auto mask = [&](const Expr& a) { return a.kind == Expr::Kind::Number || masked(a) ? a : Masked16(a); };
                    const bool zeroTest = comparison && ((e.args[1].kind == Expr::Kind::Number && e.args[1].value == 0) ||
                                                         (e.args[0].kind == Expr::Kind::Number && e.args[0].value == 0));
                    if (!zeroTest)
                    {
                        wordBits = 0;
                        const std::string out = Print(Expr::Binary(e.op, mask(e.args[0]), e.op == Op::Shr ? e.args[1] : mask(e.args[1])));
                        wordBits = 16;
                        return out;
                    }
                }
                const int p = Priority_(e.op);
                std::string left = Print(e.args[0]);
                std::string right = Print(e.args[1]);
                if (e.args[0].kind == Expr::Kind::Binary && Priority_(e.args[0].op) < p)
                    left = "(" + left + ")";
                if (e.args[1].kind == Expr::Kind::Binary && Priority_(e.args[1].op) <= p)
                    right = "(" + right + ")";
                return left + Symbol_(e.op) + right;
            }
        }
        return "0";
    }

    std::string Quote(const std::string& text) const
    {
        if (Z80asm())
        {
            // z80asm's double-quoted text reads C escapes: a backslash and a quote escaped
            std::string out = "\"";
            for (const char c : text)
                out += c == '"' ? std::string("\\\"") : c == '\\' ? std::string("\\\\") : std::string(1, c);
            return out + "\"";
        }
        // pasmo's single-quoted text takes every byte as is; a quote is written twice
        std::string out = "'";
        for (const char c : text)
            out += c == '\'' ? std::string("''") : std::string(1, c);
        return out + "'";
    }

    std::string Operand_(const Operand& o, bool instruction)
    {
        switch (o.kind)
        {
            case Operand::Kind::Register: return o.text == "af'" ? "AF'" : z80::Upper(o.text);
            case Operand::Kind::Condition: return z80::Upper(o.text);
            case Operand::Kind::Indirect: return "(" + z80::Upper(o.text) + ")";
            case Operand::Kind::Indexed:
            {
                if (Z80asm())   // 32-bit signed words: a negative symbol is a negative offset
                {
                    const std::string d = Print(o.expr);
                    return "(" + z80::Upper(o.text) + (d.empty() || d[0] == '+' || d[0] == '-' ? d : "+" + d) + ")";
                }
                // pasmo's words are unsigned: (IY+(-3)) would be +#FFFD, so a negated offset is written with "-"
                if (o.expr.kind == Expr::Kind::Unary && o.expr.op == Op::Negate)
                {
                    const std::string m = Print(o.expr.args[0]);
                    return "(" + z80::Upper(o.text) + "-" + (o.expr.args[0].kind == Expr::Kind::Binary ? "(" + m + ")" : m) + ")";
                }
                if (o.expr.kind == Expr::Kind::Number && o.expr.value < 0)
                    return "(" + z80::Upper(o.text) + "-" + std::to_string(-o.expr.value) + ")";
                const std::string d = Print(o.expr);
                if (o.expr.kind == Expr::Kind::Number)
                    return "(" + z80::Upper(o.text) + "+" + d + ")";
                // A symbol may hold a negative offset (-122 is #FF86 in pasmo's 16-bit words); after "+" pasmo takes
                // any byte, so the low byte is the offset
                return "(" + z80::Upper(o.text) + "+((" + d + ") AND #FF))";
            }
            case Operand::Kind::Memory: return "(" + Print(o.expr) + ")";
            case Operand::Kind::Immediate:
            {
                // pasmo reads an operand that starts with a parenthesis as memory: "0+" keeps it a value (pasmodoc "Bugs")
                const std::string v = Print(o.expr);
                return instruction && !v.empty() && v[0] == '(' ? "0+" + v : v;
            }
            case Operand::Kind::String: return Quote(o.text);
        }
        return {};
    }

    std::string Operands(const std::vector<Operand>& operands, bool instruction = false)
    {
        std::string out;
        for (const Operand& o : operands)
            out += (out.empty() ? "" : ",") + Operand_(o, instruction);
        return out;
    }

    std::string NotConverted(const std::string& what)
    {
        diagnostics.push_back({Severity::Warning, line, 0, "not converted: " + what});
        return "@; unreal-asm: not converted: " + what;
    }

    std::vector<std::string> Checked(std::vector<std::string> texts, const std::string& what)
    {
        if (problem.empty())
            return texts;
        const std::string reason = problem;
        problem.clear();
        return {NotConverted(what + ": " + TargetName() + " has no " + reason)};
    }

    /// DS count,pattern... of STORM: count bytes of the pattern repeated and cut
    std::vector<std::string> CyclicFill(const Statement& s)
    {
        const size_t k = s.operands.size();
        if (s.args[0].kind == Expr::Kind::Number)
        {
            const int64_t count = s.args[0].value;
            std::vector<std::string> out;
            if (count / static_cast<int64_t>(k) > 0)
                out = {"REPT " + std::to_string(count / static_cast<int64_t>(k)), "DB " + Operands(s.operands), RepeatEnd()};
            const size_t rest = static_cast<size_t>(count % static_cast<int64_t>(k));
            if (rest > 0)
                out.push_back("DB " + Operands(std::vector<Operand>(s.operands.begin(), s.operands.begin() + static_cast<std::ptrdiff_t>(rest))));
            if (out.empty())
                out.push_back("@; DS 0");
            return out;
        }
        const Expr count = Grouped(s.args[0]);
        std::vector<std::string> out = {"REPT " + Print(Expr::Binary(Op::Div, count, Expr::Number(static_cast<int64_t>(k)))), "DB " + Operands(s.operands), RepeatEnd()};
        for (size_t r = 1; r < k; ++r)
        {
            out.push_back("IF " + Print(Expr::Binary(Op::GreaterEqual, Expr::Binary(Op::Mod, count, Expr::Number(static_cast<int64_t>(k))),
                                                     Expr::Number(static_cast<int64_t>(r)))));
            out.push_back("DB " + Operand_(s.operands[r - 1], false));
            out.push_back("ENDIF");
        }
        return out;
    }

    std::vector<std::string> InstructionText(const Statement& s)
    {
        const std::string m = s.mnemonic;
        // pasmo knows neither IN F,(C) nor OUT (C),0: their bytes
        if (!Z80asm() && m == "in" && s.operands.size() == 2 && s.operands[0].kind == Operand::Kind::Register && s.operands[0].text == "f")
            return {"DB #ED,#70"};
        if (!Z80asm() && m == "out" && s.operands.size() == 2 && s.operands[1].kind == Operand::Kind::Immediate && s.operands[1].expr.kind == Expr::Kind::Number &&
            s.operands[1].expr.value == 0)
            return {"DB #ED,#71"};
        // CP 0,0 / INC A,B: one instruction per operand, as sjasmplus reads them (not RLC (IX+1),B, the undocumented copy)
        const bool indexed = std::any_of(s.operands.begin(), s.operands.end(), [](const Operand& o) { return o.kind == Operand::Kind::Indexed; });
        if (z80::SplitArity(m) == 1 && s.operands.size() > 1 && !indexed)
        {
            std::vector<std::string> out;
            for (const Operand& o : s.operands)
            {
                Statement part = s;
                part.operands.assign(1, o);
                for (std::string& t : InstructionText(part))
                    out.push_back(std::move(t));
            }
            return out;
        }
        std::string mnemonic = z80::Upper(m == "sli" ? std::string("sll") : m);
        if (mnemonic == "INF")
            return {Z80asm() ? "IN F,(C)" : "DB #ED,#70"};
        std::vector<Operand> ops = s.operands;
        // Inside an emulated displacement the code sits at its physical address: a relative jump goes to the
        // target's physical address
        if (displaced && (m == "jr" || m == "djnz") && !ops.empty() && ops.back().kind == Operand::Kind::Immediate)
        {
            Expr target = Expr::Binary(Op::Sub, Grouped(ops.back().expr), Expr::Symbol(kDelta));
            ops.back().expr = std::move(target);
        }
        const std::string text = Operands(ops, true);
        return Checked({mnemonic + (text.empty() ? "" : " " + text)}, mnemonic);
    }

    std::vector<std::string> StatementText(const Statement& s, const std::string& label)
    {
        switch (s.kind)
        {
            case Statement::Kind::Raw: return {NotConverted("unparsed line: " + s.text)};
            case Statement::Kind::MacroCall:
            {
                std::vector<std::string> params = s.params;
                if (s.operands.size() == s.params.size())
                    for (size_t k = 0; k < params.size(); ++k)
                        params[k] = Operand_(s.operands[k], false);
                std::string args;
                for (size_t k = 0; k < params.size(); ++k)
                    args += (k ? "," : "") + params[k];
                return Checked({Name(s.mnemonic) + (args.empty() ? "" : " " + args)}, s.mnemonic);
            }
            case Statement::Kind::Instruction: return InstructionText(s);
            case Statement::Kind::Directive: break;
        }
        auto args = [&](size_t from = 0) {
            std::string out;
            for (size_t k = from; k < s.args.size(); ++k)
                out += (out.empty() ? "" : ",") + Print(s.args[k]);
            return out;
        };
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                virtualAddress.clear();
                if (s.args.size() > 1)
                    diagnostics.push_back({Severity::Warning, line, 0, std::string("ORG with a page: ") + TargetName() + " has no pages; the page is left out"});
                if (Z80asm() && (UsesCurrent(s.args[0]) || UsesAddressLabel(s.args[0])))
                {
                    // $ and the labels count from the section's start in z80asm (a SECTION's ORG must be a constant):
                    // an ORG computed from them moves on within the section by DEFS (a move back is z80asm's error)
                    const Expr& a = s.args[0];
                    if (a.kind == Expr::Kind::Current)
                        return {"@; ORG $"};   // no move
                    if (virtualOrgs && virtualOrgs->count(&s))
                    {
                        // Nothing but labels and EQUs up to the next ORG: the labels get the address, no code moves
                        virtualAddress = Print(Masked16(a));
                        return {"@; ORG " + virtualAddress + " (only labels follow: written as EQU)"};
                    }
                    if (a.kind == Expr::Kind::Binary && a.op == Op::Add && a.args[0].kind == Expr::Kind::Current && a.args[1].kind == Expr::Kind::Number)
                        return Checked({"DEFS " + Print(a.args[1])}, "ORG");
                    return Checked({"DEFS " + Print(Expr::Binary(Op::Sub, Grouped(Masked16(a)), Expr::Make(Expr::Kind::Current)))}, "ORG");
                }
                if (Z80asm())
                {
                    phased = false;
                    // z80asm takes one ORG per section: every ORG opens one (each section's binary is written apart,
                    // its address is __<section>_head in the map)
                    std::string file = options.fileName.empty() ? std::string("main") : options.fileName;
                    for (char& c : file)
                        if (!std::isalnum(static_cast<unsigned char>(c)))
                            c = '_';
                    return Checked({"SECTION s_" + file + "_" + std::to_string(++sections), "ORG " + Print(Masked16(s.args[0]))}, "ORG");
                }
                return Checked({"ORG " + Print(s.args[0])}, "ORG");
            case ir::DirectiveKind::Equ:
                if (redefinable && redefinable->count(label))
                    return Checked({"= " + args()}, "DEFL");
                return Checked({"EQU " + args()}, "EQU");
            case ir::DirectiveKind::Defl:
                if (redefinable && !redefinable->count(label))
                    return Checked({"EQU " + args()}, "EQU");   // assigned once
                return Checked({"= " + args()}, "DEFL");
            case ir::DirectiveKind::Db: return Checked({"DB " + Operands(s.operands)}, "DB");
            case ir::DirectiveKind::Dw: return Checked({"DW " + Operands(s.operands)}, "DW");
            case ir::DirectiveKind::Ds:
                if (s.operands.size() > 1 && !s.params.empty() && s.params[0] == "cyclic")
                    return Checked(CyclicFill(s), "DS");
                if (!s.operands.empty())
                {
                    if (s.operands.size() == 1 && s.operands[0].kind == Operand::Kind::Immediate)
                        return Checked({"DS " + Print(s.args[0]) + "," + Operands(s.operands)}, "DS");
                    return Checked({"REPT " + Print(s.args[0]), "DB " + Operands(s.operands), RepeatEnd()}, "DS");
                }
                if (s.args.size() <= 2)
                    return Checked({"DS " + args()}, "DS");
                return Checked({"REPT " + Print(s.args[0]), "DB " + args(1), RepeatEnd()}, "DS");
            case ir::DirectiveKind::Include:
            {
                std::string name = s.text;
                const size_t colon = name.find(':');
                if (colon != std::string::npos)
                    name = name.substr(colon + 1);
                if (!s.params.empty() && s.params[0] == "verbatim")
                    return {"INCLUDE \"" + name + "\""};
                return {"INCLUDE \"" + name + ".asm\""};
            }
            case ir::DirectiveKind::Incbin:
            {
                std::string name = s.text;
                const size_t colon = name.find(':');
                if (colon != std::string::npos)
                    name = name.substr(colon + 1);
                if (!s.args.empty())
                {
                    // pasmo's INCBIN takes the whole file: a part given by numbers is cut out by whoever writes the
                    // files (zxasm convert reads the marker)
                    const bool numbers = std::all_of(s.args.begin(), s.args.end(), [](const Expr& a) { return a.kind == Expr::Kind::Number; });
                    if (!numbers || s.args.size() > 2)
                        return {NotConverted("INCBIN \"" + name + "\"," + args() + " (" + TargetName() + "'s INCBIN takes the whole file)")};
                    return {std::string(Z80asm() ? "BINARY" : "INCBIN") + " \"" + name + "\" ; unreal-asm: slice " + std::to_string(s.args[0].value) +
                            (s.args.size() > 1 ? "," + std::to_string(s.args[1].value) : std::string())};
                }
                const std::string incbin = std::string(Z80asm() ? "BINARY" : "INCBIN") + " \"" + name + "\"";
                if (s.params.empty() || s.params[0] != "sector-slack")
                    return {incbin};
                if (Z80asm())   // the tail written and the address moved back: z80asm has no ORG back within a section
                    return {incbin, NotConverted("the rest of the last sector of \"" + name + "\" (TASM copies it; z80asm cannot move back)")};
                // The rest of the last sector next, then the address goes back (pasmo keeps the later bytes)
                return {incbin, "@__UNREALASM_INCBIN_P DEFL $", "INCBIN \"" + name + ".slack\"", "ORG __UNREALASM_INCBIN_P"};
            }
            case ir::DirectiveKind::If:
                if (Z80asm())
                {
                    // A test whether a name this file never defines (the file that INCLUDEs it may): z80asm's own
                    // IFDEF, which sees labels and EQUs defined before it
                    const Expr& c = s.args[0];
                    auto name = [&](const Expr& e) -> const Expr* {
                        if (e.kind == Expr::Kind::Unary && e.op == Op::Exists && e.args[0].kind == Expr::Kind::Symbol)
                            return &e.args[0];
                        if (e.kind == Expr::Kind::Defined && e.args[0].kind == Expr::Kind::Symbol)
                            return &e.args[0];
                        return nullptr;
                    };
                    const Expr* inner = c.kind == Expr::Kind::Unary && c.op == Op::LogicalNot ? name(c.args[0]) : nullptr;
                    const Expr* zero = c.kind == Expr::Kind::Binary && c.op == Op::Equal && c.args[1].kind == Expr::Kind::Number && c.args[1].value == 0
                                           ? name(c.args[0].kind == Expr::Kind::Group ? c.args[0].args[0] : c.args[0])
                                           : nullptr;
                    const Expr* direct = name(c);
                    auto foreign = [&](const Expr* e) { return e && !ownNames.count(e->text); };
                    if (foreign(direct))   // exist X; ALASM ?X (0 when defined)
                        return {std::string(c.kind == Expr::Kind::Defined ? "IFNDEF " : "IFDEF ") + Name(direct->text)};
                    if (foreign(inner))   // !exist X
                        return {std::string(c.args[0].kind == Expr::Kind::Defined ? "IFDEF " : "IFNDEF ") + Name(inner->text)};
                    if (foreign(zero))   // (?X)==0, (exist X)==0
                    {
                        const Expr& tested = c.args[0].kind == Expr::Kind::Group ? c.args[0].args[0] : c.args[0];
                        return {std::string(tested.kind == Expr::Kind::Defined ? "IFDEF " : "IFNDEF ") + Name(zero->text)};
                    }
                    // z80asm decides IF on its first pass: a label defined further on reads as 0 there
                    std::function<void(const Expr&)> later = [&](const Expr& e) {
                        if (e.kind == Expr::Kind::Symbol)
                        {
                            const auto first = firstDefinition.find(e.text);
                            if (first != firstDefinition.end() && first->second > line)
                                diagnostics.push_back({Severity::Warning, line, 0, "IF depends on " + e.text +
                                                                                       ", defined further on: z80asm decides IF before it knows the label"});
                        }
                        for (const Expr& a : e.args)
                            later(a);
                    };
                    later(s.args[0]);
                }
                return Checked({"IF " + Print(s.args[0])}, "IF");
            case ir::DirectiveKind::Else: return {"ELSE"};
            case ir::DirectiveKind::EndIf: return {"ENDIF"};
            case ir::DirectiveKind::EndMacro: return {"ENDM"};
            case ir::DirectiveKind::Repeat: return Checked({"REPT " + args()}, "REPT");
            case ir::DirectiveKind::EndRepeat: return {RepeatEnd()};
            case ir::DirectiveKind::While:
                // REPT with an exit: neither has WHILE
                return Checked({LoopRept(), "IF " + Print(Expr::Binary(Op::Equal, Grouped(s.args[0]), Expr::Number(0))), "EXITM", "ENDIF"}, "WHILE");
            case ir::DirectiveKind::EndWhile: return {RepeatEnd()};
            case ir::DirectiveKind::RepeatUntil: return {LoopRept()};
            case ir::DirectiveKind::UntilZero:
                return Checked({"IF " + Print(Expr::Binary(Op::Equal, Grouped(s.args[0]), Expr::Number(0))), "EXITM", "ENDIF", RepeatEnd()}, "UNTIL");
            case ir::DirectiveKind::Disp:
                virtualAddress.clear();
                if (Z80asm())   // an address is a 16-bit word (z80asm computes in 32 bits)
                {
                    std::vector<std::string> out = Checked({"PHASE " + Print(Masked16(s.args[0]))}, "PHASE");
                    phased = true;
                    return out;
                }
                {
                // The code stays where it is put; the labels and $ get the difference to the run address
                const std::string address = Print(s.args[0]);
                const bool was = displaced;
                displaced = false;
                const std::string physical = "$";
                displaced = was;
                std::vector<std::string> out = Checked({"@" + std::string(kDelta) + " DEFL " + address + "-" + physical}, "DISP");
                displaced = true;
                return out;
            }
            case ir::DirectiveKind::Ent:
                if (Z80asm())
                {
                    phased = false;
                    return {"DEPHASE"};   // also where none is active
                }
                displaced = false;
                return {"@" + std::string(kDelta) + " DEFL 0"};
            case ir::DirectiveKind::Display: return {"@; DISPLAY " + Operands(s.operands)};
            case ir::DirectiveKind::End: return {"END"};
            case ir::DirectiveKind::IfUsed:
                diagnostics.push_back({Severity::Warning, line, 0, "IFUSED " + s.text + ": " + TargetName() + " has no IFUSED; the block is assembled"});
                return {"IF 1 ; unreal-asm: IFUSED " + s.text + " (" + TargetName() + " has none)"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Warning, line, 0, "SAVEBIN \"" + s.text + "\": " + TargetName() + " writes its own output; kept as a comment"});
                return {"@; unreal-asm: SAVEBIN \"" + s.text + "\"," + args()};
            case ir::DirectiveKind::Main: return {"@; MAIN \"" + s.text + "\" (the project's main source)"};
            case ir::DirectiveKind::Run: return {NotConverted("RUN " + args() + " (code called while assembling)")};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }
};
}  // namespace

BackendResult WriteClassic(const ir::Program& program, const BackendOptions& options, ClassicTarget target)
{
    BackendResult result;
    result.document.dialect = target == ClassicTarget::Z80asm ? "z88dk" : "pasmo";
    result.document.codePage = encoding::CodePage::Cp866;   // texts are program bytes: the Spectrum code page
    Writer w{options, result.diagnostics, target};
    w.trueValue = program.trueValue;
    w.wordBits = program.expressionBits;
    w.unsignedWords = program.unsignedArithmetic;
    w.phased = program.displacementAcrossFiles;   // a file may start inside a PHASE of the file that INCLUDEs it
    std::set<const Statement*> virtualOrgs;
    {
        std::function<void(const Expr&, std::set<std::string>&)> tested = [&](const Expr& e, std::set<std::string>& into) {
            if ((e.kind == Expr::Kind::Defined || (e.kind == Expr::Kind::Unary && e.op == Op::Exists)) && !e.args.empty() &&
                e.args[0].kind == Expr::Kind::Symbol)
                into.insert(e.args[0].text);
            for (const Expr& a : e.args)
                tested(a, into);
        };
        std::vector<std::set<std::string>> open;
        for (const ir::Line& l : program.lines)
        {
            bool inside = false;
            for (const auto& names : open)
                inside = inside || names.count(l.label);
            if (!l.label.empty() && !inside)
                w.definedNames.insert(l.label);
            for (const Statement& s : l.statements)
            {
                if (s.kind != Statement::Kind::Directive)
                    continue;
                if (s.directive == ir::DirectiveKind::If || s.directive == ir::DirectiveKind::IfUsed)
                {
                    std::set<std::string> names;
                    for (const Expr& a : s.args)
                        tested(a, names);
                    open.push_back(names);
                }
                else if (s.directive == ir::DirectiveKind::EndIf && !open.empty())
                    open.pop_back();
            }
        }
        // The other files of the assembly: a name they define more often than this file does
        std::map<std::string, int> own;
        for (const ir::Line& l : program.lines)
            if (!l.label.empty())
            {
                ++own[l.label];
                w.ownNames.insert(l.label);
                w.firstDefinition.emplace(l.label, l.sourceLine);
            }
        for (const auto& [name, count] : options.definitions)
            if (count > own[name])
                w.definedNames.insert(name);
        // z80asm: an ORG followed by nothing but labels and EQUs up to the next ORG / PHASE / end
        for (size_t k = 0; k < program.lines.size(); ++k)
            for (const Statement& s : program.lines[k].statements)
            {
                if (!(s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Org))
                    continue;
                bool onlyLabels = true, ended = false;
                for (size_t n = k + 1; n < program.lines.size() && onlyLabels && !ended; ++n)
                    for (const Statement& t : program.lines[n].statements)
                    {
                        if (t.kind == Statement::Kind::Directive &&
                            (t.directive == ir::DirectiveKind::Org || t.directive == ir::DirectiveKind::Disp || t.directive == ir::DirectiveKind::Ent))
                        {
                            ended = true;
                            break;
                        }
                        if (!(t.kind == Statement::Kind::Directive && (t.directive == ir::DirectiveKind::Equ || t.directive == ir::DirectiveKind::Defl)))
                        {
                            onlyLabels = false;
                            break;
                        }
                    }
                if (onlyLabels)
                    virtualOrgs.insert(&s);
            }
        w.virtualOrgs = &virtualOrgs;
        // Address labels: a label of a line that is no EQU / DEFL, and EQUs built on $ or on such labels
        for (const ir::Line& l : program.lines)
        {
            if (l.label.empty())
                continue;
            bool assignment = false;
            for (const Statement& s : l.statements)
                assignment = assignment || (s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Equ || s.directive == ir::DirectiveKind::Defl));
            if (!assignment)
                w.addressLabels.insert(l.label);
        }
        for (bool grown = true; grown;)
        {
            grown = false;
            for (const ir::Line& l : program.lines)
                for (const Statement& s : l.statements)
                    if (!l.label.empty() && !w.addressLabels.count(l.label) && s.kind == Statement::Kind::Directive &&
                        (s.directive == ir::DirectiveKind::Equ || s.directive == ir::DirectiveKind::Defl) && !s.args.empty() &&
                        (UsesCurrent(s.args[0]) || w.UsesAddressLabel(s.args[0])))
                    {
                        w.addressLabels.insert(l.label);
                        grown = true;
                    }
        }
    }

    // Names assigned with "=" and defined more than once: every definition of them is DEFL. One assignment is an EQU
    // (pasmo refuses a reference to a DEFL name before its definition; "src=$+1" is used before it stands)
    std::set<std::string> redefinable;
    std::map<std::string, int> definitions;
    std::map<std::string, int> callArguments;
    bool inMacroBody = false;
    for (const ir::Line& l : program.lines)
    {
        bool assigned = false;
        for (const Statement& s : l.statements)
        {
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
                inMacroBody = true;
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Defl && !l.label.empty())
                assigned = true;
            if (s.kind == Statement::Kind::MacroCall)
                callArguments[s.mnemonic] = std::max(callArguments[s.mnemonic], static_cast<int>(s.params.size()));
        }
        if (!l.label.empty())
            definitions[l.label] += inMacroBody ? 2 : 1;   // inside a macro: defined at every call
        if (assigned)
            redefinable.insert(l.label);
        for (const Statement& s : l.statements)
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndMacro)
                inMacroBody = false;
    }
    result.definitions = definitions;
    // In a project the other files count too (a name assigned in a source and in a file it INCLUDEs)
    for (auto it = redefinable.begin(); it != redefinable.end();)
    {
        const auto project = options.definitions.find(*it);
        const int count = project != options.definitions.end() ? std::max(project->second, definitions[*it]) : definitions[*it];
        it = count <= 1 ? redefinable.erase(it) : std::next(it);
    }
    w.redefinable = &redefinable;

    std::set<std::string> used;
    for (const ir::Line& l : program.lines)
        if (!l.label.empty())
            used.insert(l.label);
    for (const std::string& name : used)
        if (Reserved(name, w.Z80asm()) || !IsValidLabel(name, w.Z80asm()))
        {
            std::string renamed = SafeName(name);
            while (used.count(renamed))
                renamed += "_";
            w.renames[name] = renamed;
        }

    // Local blocks (ALASM LOCAL, TASM's macro bodies): the labels defined inside and used only inside are LOCAL
    struct Block
    {
        size_t first = 0, last = 0;
        std::vector<std::string> local;
        bool inMacro = false;
    };
    std::vector<Block> blocks;
    {
        std::vector<std::set<std::string>> defined;
        std::vector<size_t> open;
        bool macro = false;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            for (const Statement& s : l.statements)
            {
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
                    macro = true;
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::LocalBlock)
                {
                    open.push_back(blocks.size());
                    blocks.push_back({k, program.lines.size() - 1, {}, macro});
                    defined.emplace_back();
                }
            }
            if (!open.empty() && !l.label.empty() && !l.labelGlobal)
                defined[open.back()].insert(l.label);
            for (const Statement& s : l.statements)
            {
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndLocalBlock && !open.empty())
                {
                    blocks[open.back()].last = k;
                    open.pop_back();
                }
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndMacro)
                    macro = false;
            }
        }
        std::vector<std::set<std::string>> references(program.lines.size());
        std::function<void(const Expr&, std::set<std::string>&)> collect = [&](const Expr& e, std::set<std::string>& into) {
            if (e.kind == Expr::Kind::Symbol)
                into.insert(e.text);
            for (const Expr& a : e.args)
                collect(a, into);
        };
        for (size_t k = 0; k < program.lines.size(); ++k)
            for (const Statement& s : program.lines[k].statements)
            {
                for (const Expr& a : s.args)
                    collect(a, references[k]);
                for (const Operand& o : s.operands)
                    collect(o.expr, references[k]);
            }
        auto inDefiningBlock = [&](size_t k, const std::string& name) {
            for (size_t b = 0; b < blocks.size(); ++b)
                if (k >= blocks[b].first && k <= blocks[b].last && defined[b].count(name))
                    return true;
            return false;
        };
        for (size_t b = 0; b < blocks.size(); ++b)
            for (const std::string& name : defined[b])
            {
                bool outside = false;
                for (size_t k = 0; k < program.lines.size() && !outside; ++k)
                    if ((k < blocks[b].first || k > blocks[b].last) && references[k].count(name) && !inDefiningBlock(k, name))
                        outside = true;
                if (!outside)
                    blocks[b].local.push_back(name);
            }
    }

    // REPT bodies that define labels: pasmo's LOCAL does not work in REPT, so the body becomes a macro called n times
    std::map<size_t, size_t> repeatEnd;   // the line of a REPT with labels -> the line of its ENDM
    auto only = [](const ir::Line& l, ir::DirectiveKind kind) {
        return l.statements.size() == 1 && l.statements[0].kind == Statement::Kind::Directive && l.statements[0].directive == kind && l.label.empty();
    };
    for (size_t k = 0; k < program.lines.size(); ++k)
    {
        if (!only(program.lines[k], ir::DirectiveKind::Repeat))
            continue;
        int depth = 0;
        bool labels = false;
        for (size_t e = k + 1; e < program.lines.size(); ++e)
        {
            const ir::Line& l = program.lines[e];
            for (const Statement& s : l.statements)
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Repeat)
                    ++depth;
            if (only(l, ir::DirectiveKind::EndRepeat) && depth == 0)
            {
                if (labels)
                    repeatEnd[k] = e;
                break;
            }
            for (const Statement& s : l.statements)
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndRepeat)
                    --depth;
            labels = labels || !l.label.empty();
        }
    }
    std::map<size_t, std::string> pendingRepeat;   // line of the ENDM -> the REPT count and macro name to call

    // A file that may start inside a displacement (an INCLUDE inside PHASE): the delta exists from its first line
    if (program.displacementAcrossFiles && !w.Z80asm())
    {
        result.document.lines.push_back({"        IF !DEFINED " + std::string(kDelta), {}});
        result.document.lines.push_back({std::string(kDelta) + " DEFL 0", {}});
        result.document.lines.push_back({"        ENDIF", {}});
        w.displaced = true;
    }

    size_t nextBlock = 0;
    int repeatMacros = 0;
    for (size_t index = 0; index < program.lines.size(); ++index)
    {
        const ir::Line& l = program.lines[index];
        w.line = l.sourceLine;
        std::vector<std::string> texts;
        // A REPT with labels: its body becomes a macro
        if (repeatEnd.count(index))
        {
            const std::string name = "__UNREALASM_R" + std::to_string(++repeatMacros);
            std::set<std::string> defined;
            for (size_t k = index + 1; k < repeatEnd[index]; ++k)
                if (!program.lines[k].label.empty())
                    defined.insert(program.lines[k].label);
            std::string locals;
            std::map<std::string, std::string> scope;
            for (const std::string& n : defined)
            {
                locals += (locals.empty() ? "" : ",") + w.Name(n);
                scope[n] = w.Name(n);
            }
            result.document.lines.push_back({name + " MACRO", {}});
            result.document.lines.push_back({"        LOCAL " + locals, {}});
            pendingRepeat[repeatEnd[index]] = w.Print(l.statements[0].args[0]) + "|" + name;
            w.localScopes.push_back(scope);
            continue;
        }
        if (pendingRepeat.count(index))
        {
            const std::string spec = pendingRepeat[index];
            const size_t bar = spec.find('|');
            result.document.lines.push_back({"        ENDM", {}});
            result.document.lines.push_back({"        REPT " + spec.substr(0, bar), {}});
            result.document.lines.push_back({"        " + spec.substr(bar + 1), {}});
            result.document.lines.push_back({std::string("        ") + w.RepeatEnd(), {}});
            if (!w.localScopes.empty())
                w.localScopes.pop_back();
            continue;
        }

        bool openBlock = false, closeBlock = false, openMacro = false, closeMacro = false;
        std::string macroHeader;
        std::vector<std::string> namedParams;
        for (const Statement& s : l.statements)
        {
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::LocalBlock)
                openBlock = true;
            else if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndLocalBlock)
                closeBlock = true;
            else if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
            {
                openMacro = true;
                macroHeader = s.text;
                namedParams = s.params;
            }
            else if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndMacro)
                closeMacro = true;
        }
        std::string label = l.label.empty() ? std::string() : w.Name(l.label);

        if (openMacro)
        {
            std::string params;
            int count = 0;
            if (!namedParams.empty())
            {
                for (size_t k = 0; k < namedParams.size(); ++k)
                    params += (k ? "," : "") + namedParams[k];
                count = static_cast<int>(namedParams.size());
            }
            else
            {
                // \0..\9 -> _arg0.._arg9: as many as the body uses or the longest call passes
                int highest = -1;
                std::function<void(const Expr&)> scan = [&](const Expr& e) {
                    if (e.kind == Expr::Kind::Symbol && e.text.size() == 2 && e.text[0] == '\\')
                        highest = std::max(highest, e.text[1] - '0');
                    for (const Expr& a : e.args)
                        scan(a);
                };
                for (size_t k = index + 1; k < program.lines.size(); ++k)
                {
                    bool end = false;
                    for (const Statement& s : program.lines[k].statements)
                    {
                        end = end || (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndMacro);
                        for (const Expr& a : s.args)
                            scan(a);
                        for (const Operand& o : s.operands)
                            scan(o.expr);
                    }
                    if (end)
                        break;
                }
                const auto calls = callArguments.find(macroHeader);
                if (calls != callArguments.end())
                    highest = std::max(highest, calls->second - 1);
                for (int k = 0; k <= highest; ++k)
                    params += (k ? "," : "") + std::string("_arg") + std::to_string(k);
                count = highest + 1;
            }
            texts.push_back("@" + w.Name(macroHeader) + " MACRO" + (params.empty() ? "" : " " + params));
            result.macroParams[macroHeader] = count;
            w.inMacro = true;
        }
        if (openBlock)
        {
            const Block& b = nextBlock < blocks.size() ? blocks[nextBlock] : Block{};
            ++nextBlock;
            std::string locals;
            std::map<std::string, std::string> scope;
            for (const std::string& n : b.local)
            {
                locals += (locals.empty() ? "" : ",") + w.Name(n);
                scope[n] = w.Name(n);
            }
            if (!b.inMacro && w.Z80asm())
            {
                // z80asm has no PROC: the block's labels get its number
                for (auto& [n, renamed] : scope)
                    renamed = renamed + "__L" + std::to_string(nextBlock);
                w.localScopes.push_back(scope);
            }
            else
            {
                if (!b.inMacro)
                    texts.push_back("PROC");   // a local block outside a macro: PROC ... ENDP holds the LOCAL names
                if (!locals.empty())
                    texts.push_back("LOCAL " + locals);
                w.localScopes.push_back(scope);
                if (!b.inMacro)
                    w.localScopes.back()["__proc"] = "1";
            }
        }

        // A label inside a possible displacement: its value is the run address
        bool defines = false;
        for (const Statement& s : l.statements)
            defines = defines || (s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Equ || s.directive == ir::DirectiveKind::Defl ||
                                                                           s.directive == ir::DirectiveKind::Macro));
        if (!label.empty() && !defines)
        {
            const bool redefined = redefinable.count(l.label) > 0;
            if (!w.virtualAddress.empty())
                result.document.lines.push_back({label + " EQU " + w.virtualAddress, {}});
            else if (w.displaced)
                result.document.lines.push_back({label + (redefined ? " DEFL $+" : " EQU $+") + kDelta, {}});
            else if (redefined)
                result.document.lines.push_back({label + " DEFL $", {}});
            else
                result.document.lines.push_back({label + (w.Z80asm() ? ":" : ""), {}});   // z80asm: a label ends with ":"
            label.clear();
        }

        for (const Statement& s : l.statements)
        {
            if (s.kind == Statement::Kind::Directive &&
                (s.directive == ir::DirectiveKind::LocalBlock || s.directive == ir::DirectiveKind::EndLocalBlock || s.directive == ir::DirectiveKind::Macro))
                continue;
            w.pcLabel.clear();
            std::vector<std::string> written = w.StatementText(s, l.label);
            if (!w.pcLabel.empty())
                texts.push_back("@" + w.pcLabel + ":");
            for (std::string t : written)
            {
                if (w.inMacro && s.kind == Statement::Kind::MacroCall)
                    for (int d = 0; d <= 9; ++d)
                    {
                        const std::string from = std::string("\\") + static_cast<char>('0' + d);
                        for (size_t pos = t.find(from); pos != std::string::npos; pos = t.find(from, pos))
                            t.replace(pos, 2, "_arg" + std::to_string(d));
                    }
                texts.push_back(t);
            }
        }
        if (closeBlock && !w.localScopes.empty())
        {
            const bool proc = w.localScopes.back().count("__proc") > 0;
            w.localScopes.pop_back();
            if (proc)
                texts.push_back("ENDP");
        }
        if (closeMacro)
            w.inMacro = false;

        // Lay out: label in column 0, statements from column 8; "@" lines own their column; "= x" is label DEFL x
        std::string out;
        if (texts.empty())
            out = label;
        for (size_t k = 0; k < texts.size(); ++k)
        {
            std::string t = texts[k];
            if (t.rfind("= ", 0) == 0)
            {
                out = label + " DEFL " + t.substr(2);
                continue;
            }
            if (t.rfind("EQU ", 0) == 0)
            {
                out = label + " " + t;
                continue;
            }
            if (!t.empty() && t[0] == '@')
            {
                if (!out.empty())
                {
                    result.document.lines.push_back({out, {}});
                    out.clear();
                }
                result.document.lines.push_back({t.substr(1), {}});
                continue;
            }
            if (!out.empty())
                result.document.lines.push_back({out, {}});
            out = std::string(8, ' ') + t;
        }
        if (l.hasComment)
        {
            if (!out.empty())
                out += (out.size() < 32 ? std::string(32 - out.size(), ' ') : std::string(" "));
            out += ";" + l.comment;
        }
        result.document.lines.push_back({out, {}});
    }
    if (w.Z80asm())
    {
        // A PHASE this file opened and left open is closed at its end: without the DEPHASE z80asm 2.3 resolves a JR to
        // a label further on inside the PHASE wrongly
        bool open = false;
        for (const ir::Line& l : program.lines)
            for (const Statement& s : l.statements)
                if (s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Disp || s.directive == ir::DirectiveKind::Ent))
                    open = s.directive == ir::DirectiveKind::Disp;
        if (open)
            result.document.lines.push_back({"        DEPHASE                 ; unreal-asm: z80asm needs it for JR targets in the PHASE", {}});
    }
    return result;
}
}  // namespace unrealasm::dialects
