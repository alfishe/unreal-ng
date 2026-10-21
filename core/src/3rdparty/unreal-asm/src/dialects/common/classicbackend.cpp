#include "dialects/common/classicbackend.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>

#include "dialects/common/definitions.h"
#include "dialects/common/z80.h"
#include "dialects/sjasmplus/sjasmplusfrontend.h"
#include "dialects/sjasmplus/sjasmpluslower.h"

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
        case Op::Mod: return " % ";   // "7%100" is 7 and the binary number %100 to z80asm
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
    std::set<std::string> parameters{};   // the names of the parameters of the macro being written
    // EQU names whose values the numbers give (a 16-bit source's value beyond #FFFF wrapped, as its assembler does)
    std::map<std::string, int64_t> constants{};
    bool sjasmplus = false;   // the source is sjasmplus': its repeated and fake instructions
    bool alasm = false;       // the source is ALASM's: a macro argument is put in as text
    bool displaced = false;   // a displacement may be active: labels and $ go through __UNREALASM_D
    int trueValue = 0;
    std::string problem{};    // set when an expression has no pasmo form
    const std::set<std::string>* redefinable = nullptr;
    int wordBits = 0;              // 16: the source computes in 16-bit words (z80asm computes in 32 bits)
    bool unsignedWords = false;
    int sections = 0;
    bool phased = false;           // z80asm: a PHASE may be active
    int loops = 0;                 // the REPT / WHILE blocks the line is in (the layout's line address is their first pass)
    bool lastStatement = false;    // the statement being written ends its line (the next line's address follows it)
    std::string pcLabel{};         // the label standing for $ in the statement being written
    bool deflText = false;         // z80asm: writing a DEFL, a text macro ($ in it would be the $ where the name is used)
    int pcLabels = 0;
    // z80asm: DEFL names assigned from each other (their text would double at every assignment): each value an EQU of
    // its own, the name standing for it
    std::set<std::string> growing{};
    int values = 0;
    // z80asm: DEFL names whose text is numbers only ($ outside macros and loops written as the layout's address): an
    // ORG in a macro computed from them is a constant, a section of its own per expansion
    std::set<std::string> numericNames{};
    std::string deflName{};   // the name of the DEFL being written
    int macroSections = 0;
    // The macro's own section: its ORG and the bytes written in it so far (nullopt: not counted, an instruction or a
    // conditional), so that ORG $+n skips to a section of its own instead of writing zeros over the others
    std::string macroOrg{};
    std::optional<int64_t> macroBytes{};
    // The names the assembly defines outside a block that tests them (sjasmplus' "exist" answers from the whole source
    // in the same way): z80asm's tests for a defined name are decided from them
    std::set<std::string> definedNames{};
    std::set<std::string> ownNames{};   // the names this file defines
    std::map<std::string, uint32_t> firstDefinition{};   // the source line that first defines each name
    std::set<std::string> addressLabels{};   // names whose value is an address in this file (labels, EQU $+..., names built on them)
    const std::set<const Statement*>* virtualOrgs = nullptr;   // z80asm: ORGs followed by labels only
    std::string virtualAddress{};   // the address of such a region while it lasts
    // sjasmplus' temporary labels ("1" ... JR 1B / LD HL,1_F): a name of each one's own, the references by their line
    std::map<std::string, std::vector<std::pair<size_t, std::string>>> temporaries{};
    size_t lineIndex = 0;

    static bool IsTemporary(const std::string& label)
    {
        return !label.empty() && label.find_first_not_of("0123456789") == std::string::npos;
    }

    /// The name of the temporary label a reference (1B, 1F, 1_B, 1_F) finds from this line
    std::optional<std::string> Temporary(const std::string& reference) const
    {
        if (reference.size() < 2)
            return std::nullopt;
        std::string number = reference.substr(0, reference.size() - 1);
        if (number.size() > 1 && number.back() == '_')
            number.pop_back();
        const char way = static_cast<char>(std::tolower(static_cast<unsigned char>(reference.back())));
        const auto found = temporaries.find(number);
        if (!IsTemporary(number) || found == temporaries.end() || (way != 'b' && way != 'f'))
            return std::nullopt;
        std::optional<std::string> pick;
        for (const auto& [at, name] : found->second)
        {
            if (way == 'b' && at <= lineIndex)
                pick = name;
            if (way == 'f' && at > lineIndex)
                return name;
        }
        return pick;
    }

    bool UsesAddressLabel(const Expr& e) const
    {
        if (e.kind == Expr::Kind::Symbol && addressLabels.count(e.text))
            return true;
        for (const Expr& a : e.args)
            if (UsesAddressLabel(a))
                return true;
        return false;
    }

    /// An address label under an operation other than + and - (HIGH, a shift, AND): z80asm's "computed" type
    bool AddressBeyondSum(const Expr& e) const
    {
        const bool sum = e.kind == Expr::Kind::Group ||
                         ((e.kind == Expr::Kind::Binary || e.kind == Expr::Kind::Unary) &&
                          (e.op == Op::Add || e.op == Op::Sub || e.op == Op::Negate || e.op == Op::Plus));
        for (const Expr& a : e.args)
            if ((!sum && UsesAddressLabel(a)) || AddressBeyondSum(a))
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
        if (IsTemporary(name))
            if (const auto found = temporaries.find(name); found != temporaries.end())
                for (const auto& [at, given] : found->second)
                    if (at == lineIndex)
                        return given;
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
        return std::string(Z80asm() ? "$" : "#") + z80::HexDigits(static_cast<uint64_t>(value & 0xFFFF), digits);
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
                if (e.text.size() == 1 && (static_cast<unsigned char>(e.text[0]) < 0x20 || static_cast<unsigned char>(e.text[0]) >= 0x7F))
                    return std::to_string(e.value & 0xFF);   // a control character cannot stand in a quoted constant
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

    static Expr Masked8(Expr e)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(Expr::Binary(Op::And, std::move(e), Expr::Number(0xFF, ir::NumberSpelling::Hex, 2)));
        return g;
    }

    std::optional<int64_t> Known(const Expr& e) const
    {
        return z80::ConstantValue(e, [&](const std::string& name) {
            const auto found = constants.find(Name(name));
            return found == constants.end() ? std::nullopt : std::optional<int64_t>(found->second);
        });
    }

    /// $ at the start of a source line (the one being written by default), as the layout gives it (a sjasmplus
    /// source's line, or the source line of another dialect through its conversion); none in a macro or a loop, whose
    /// lines have an address per pass
    std::optional<int64_t> LineAddress(uint32_t at = 0) const
    {
        if (inMacro || loops > 0)
            return std::nullopt;
        if (at == 0)
            at = line;
        std::string file;
        for (const char c : options.fileName)
            file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        for (const char* prefix : {"__UNREALASM_S", "__UNREALASM_L"})
            for (const std::string& f : {file, std::string()})
                if (const auto found = options.labelValues.find(prefix + f + "_" + std::to_string(at)); found != options.labelValues.end())
                    return found->second.value;
        return std::nullopt;
    }

    /// z80asm takes one ORG per section: every ORG opens one (each section's binary is written apart, its address is
    /// __<section>_head in the map)
    std::string NextSection()
    {
        macroOrg.clear();
        return "s_" + SectionFile() + "_" + std::to_string(++sections);
    }

    /// The file's name as a part of a section name
    std::string SectionFile() const
    {
        std::string file = options.fileName.empty() ? std::string("main") : options.fileName;
        for (char& c : file)
            if (!std::isalnum(static_cast<unsigned char>(c)))
                c = '_';
        return file;
    }

    /// The bytes a statement adds to the macro's section (nullopt: not known here)
    std::optional<int64_t> MacroBytes(const Statement& s) const
    {
        if (s.kind != Statement::Kind::Directive)
            return std::nullopt;
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
            case ir::DirectiveKind::Equ:
            case ir::DirectiveKind::Defl:
            case ir::DirectiveKind::Display: return 0;
            case ir::DirectiveKind::Db:
            {
                int64_t n = 0;
                for (const Operand& o : s.operands)
                    n += o.kind == Operand::Kind::String ? static_cast<int64_t>(o.text.size()) : 1;
                return n;
            }
            case ir::DirectiveKind::Dw: return 2 * static_cast<int64_t>(s.operands.size());
            case ir::DirectiveKind::Ds:
                if (s.args.size() == 1 && s.operands.empty())
                    return Known(s.args[0]);
                return std::nullopt;
            default: return std::nullopt;
        }
    }

    /// An expression of numbers, constants and numeric DEFL names (no address but theirs)
    bool Numeric(const Expr& e) const
    {
        if (e.kind == Expr::Kind::Symbol)
            return numericNames.count(e.text) || (!addressLabels.count(e.text) && !(redefinable && redefinable->count(e.text)) && !growing.count(e.text));
        if (e.kind == Expr::Kind::Current || e.kind == Expr::Kind::CurrentPhysical)
            return false;
        for (const Expr& a : e.args)
            if (!Numeric(a))
                return false;
        return true;
    }

    /// The value of an expression of numbers, EQU constants and $ (at `here`)
    std::optional<int64_t> KnownAt(const Expr& e, int64_t here) const
    {
        if (e.kind == Expr::Kind::Current)
            return here;
        if (e.args.empty())
            return Known(e);
        Expr copy = e;
        for (Expr& a : copy.args)
        {
            const auto v = KnownAt(a, here);
            if (!v)
                return std::nullopt;
            a = Expr::Number(*v, ir::NumberSpelling::Decimal, 0);
        }
        return Known(copy);
    }

    /// A 16-bit source's value beyond #FFFF (X EQU #FFFF+2 is 1 in GENS): masked to its word where z80asm's 32 bits
    /// would keep it (pasmo's words are 16-bit)
    bool BeyondWord(const Expr& e) const
    {
        const auto v = Known(e);
        return Z80asm() && wordBits == 16 && v && *v > 0xFFFF;
    }

    Expr Word(const Expr& e) const { return BeyondWord(e) ? Masked16(e) : e; }

    /// a / b or a MOD b of a source with 16-bit two's complement words and signed division (GENS: #8000/2 = #C000,
    /// 60000/2 = -2768; the remainder takes the dividend's sign)
    std::string SignedDivision(const Expr& e)
    {
        const auto positive = [](const Expr& a) { return a.kind == Expr::Kind::Number && a.value >= 0 && a.value <= 0x7FFF; };
        const std::string op = Symbol_(e.op);
        if (Z80asm())
        {
            // 32-bit signed words: each operand widened from 16 bits ((x & #FFFF) ^ #8000) - #8000
            const auto widen = [&](const Expr& a) {
                if (positive(a))
                    return Print(a);
                return Print(Grouped(Expr::Binary(Op::Sub, Grouped(Expr::Binary(Op::Xor, Masked16(a), Expr::Number(0x8000, ir::NumberSpelling::Hex, 4))),
                                                  Expr::Number(0x8000, ir::NumberSpelling::Hex, 4))));
            };
            return "(" + widen(e.args[0]) + op + widen(e.args[1]) + ")";
        }
        // pasmo's 16-bit unsigned words: the magnitudes divided, the sign put back; s = 0-(x SHR 15) is #FFFF for a
        // negative word, (x XOR s)-s its magnitude or its negation (a number's magnitude and sign are written known)
        const Expr& x = e.args[0];
        const Expr& y = e.args[1];
        const auto word = [](const Expr& n) { return n.value & 0xFFFF; };
        if (x.kind == Expr::Kind::Number && y.kind == Expr::Kind::Number && word(y) != 0)
        {
            // both known: the value (C's division and remainder truncate towards 0, as GENS')
            const int64_t a = word(x) >= 0x8000 ? word(x) - 0x10000 : word(x);
            const int64_t b = word(y) >= 0x8000 ? word(y) - 0x10000 : word(y);
            return std::to_string((e.op == Op::Div ? a / b : a % b) & 0xFFFF);
        }
        const std::string a = Print(Grouped(x));
        const std::string b = Print(Grouped(y));
        const std::string sub = Symbol_(Op::Sub);
        const std::string xr = Symbol_(Op::Xor);
        const auto sign = [&](const std::string& s) { return "(0" + sub + "(" + s + Symbol_(Op::Shr) + "15))"; };
        const auto apply = [&](const std::string& s, const std::string& negative) { return "((" + s + xr + negative + ")" + sub + negative + ")"; };
        const auto magnitude = [&](const Expr& n, const std::string& text) {
            if (n.kind == Expr::Kind::Number)
                return std::to_string(word(n) >= 0x8000 ? 0x10000 - word(n) : word(n));
            return apply(text, sign(text));
        };
        const auto negative = [&](const Expr& n) { return n.kind == Expr::Kind::Number && word(n) >= 0x8000; };
        const std::string quotient = "(" + magnitude(x, a) + op + magnitude(y, b) + ")";
        if (e.op == Op::Mod)
        {
            if (x.kind == Expr::Kind::Number)
                return negative(x) ? "(0" + sub + quotient + ")" : quotient;
            return apply(quotient, sign(a));
        }
        // the quotient's sign: the operands' signs differ (a known operand gives it, or turns it)
        std::string differ;
        if (x.kind == Expr::Kind::Number || y.kind == Expr::Kind::Number)
        {
            const Expr& known = x.kind == Expr::Kind::Number ? x : y;
            const std::string& other = x.kind == Expr::Kind::Number ? b : a;
            differ = negative(known) ? sign("(" + other + xr + "65535)") : sign(other);
        }
        else
            differ = sign("(" + a + xr + b + ")");
        return apply(quotient, differ);
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

    static bool Mentions(const Expr& e, const std::function<bool(const std::string&)>& name)
    {
        if (e.kind == Expr::Kind::Symbol && name(e.text))
            return true;
        return std::any_of(e.args.begin(), e.args.end(), [&](const Expr& a) { return Mentions(a, name); });
    }

    /// A label of its own for the address here: __UNREALASM_PC_<file>_<n>
    std::string NewPcLabel()
    {
        std::string file = options.fileName.empty() ? std::string("main") : options.fileName;
        for (char& c : file)
            if (!std::isalnum(static_cast<unsigned char>(c)))
                c = '_';
        return "__UNREALASM_PC_" + file + "_" + std::to_string(++pcLabels);
    }

    std::string Print(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return Number(e);
            case Expr::Kind::Symbol:
                // sjasmplus' built-in names of the relocation table: the target has none, the layout gives their values
                if (e.text == "relocate_count" || e.text == "relocate_size")
                    if (const auto v = options.labelValues.find(e.text); v != options.labelValues.end())
                        return std::to_string(v->second.value);
                return Name(e.text);
            case Expr::Kind::Current:
                if (!virtualAddress.empty())
                    return "(" + virtualAddress + ")";
                if (Z80asm() && deflText && numericNames.count(deflName))
                    if (const auto here = LineAddress())
                        return Hex(*here, 4);
                if (Z80asm() && ((phased && !inMacro) || deflText))   // (a macro keeps $ in PHASE: the label would repeat)
                {
                    // z80asm 2.3 adds the section's ORG to $ inside PHASE (labels come out right): a label put before
                    // the statement stands for it
                    if (pcLabel.empty())
                        pcLabel = NewPcLabel();
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
                problem = "memory reads while assembling ({...})";
                return "0";
            case Expr::Kind::Raw:
                if (const auto temporary = Temporary(e.text))
                    return *temporary;
                return e.text;
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
                if (e.op == Op::CyclicLeft16 || e.op == Op::CyclicRight16)
                {
                    // 16-bit words: (a SHL b) OR (a SHR (16-b))
                    const bool right = e.op == Op::CyclicRight16;
                    const Expr a = Grouped(e.args[0]);
                    const Expr b = Grouped(e.args[1]);
                    Expr rotated = Grouped(Expr::Binary(Op::Or, Expr::Binary(right ? Op::Shr : Op::Shl, Z80asm() ? Masked16(e.args[0]) : a, b),
                                                        Expr::Binary(right ? Op::Shl : Op::Shr, Z80asm() ? Masked16(e.args[0]) : a,
                                                                     Grouped(Expr::Binary(Op::Sub, Expr::Number(16), b)))));
                    return Print(Z80asm() ? Masked16(rotated) : rotated);   // z80asm computes in 32 bits
                }
                const bool comparison = e.op == Op::Equal || e.op == Op::NotEqual || e.op == Op::Less || e.op == Op::Greater ||
                                        e.op == Op::LessEqual || e.op == Op::GreaterEqual;
                const bool logical = e.op == Op::LogicalAnd || e.op == Op::LogicalOr;
                if ((comparison || logical) && trueValue != 0 && trueValue != TargetTrue())
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
                if (wordBits == 16 && !unsignedWords && (e.op == Op::Div || e.op == Op::Mod))
                    return SignedDivision(e);
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
        // A control character cannot stand inside a quoted string in a source line (LF, BEL ...): it goes as a number
        // between the quoted runs, in the list the caller joins with commas
        std::string out;
        std::string run;
        const auto flush = [&] {
            if (!run.empty())
                out += (out.empty() ? "" : ",") + QuoteRun(run);
            run.clear();
        };
        for (const char c : text)
        {
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
            {
                flush();
                out += (out.empty() ? "" : ",") + std::to_string(static_cast<unsigned char>(c));
            }
            else
                run += c;
        }
        flush();
        return out.empty() ? QuoteRun("") : out;
    }

    std::string QuoteRun(const std::string& text) const
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
                    const std::string offset = d.empty() || d[0] == '+' || d[0] == '-' ? d : "+" + d;
                    // z80asm reads (IX+(D)), (IX+(D)-K) and (IX-(D-K)) as syntax errors: a displacement with parentheses
                    // goes between zeros, (IX+0+(D)-K+0)
                    if (d.find('(') != std::string::npos)
                        return "(" + z80::Upper(o.text) + "+0" + offset + "+0)";
                    return "(" + z80::Upper(o.text) + offset + ")";
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
                // (also a macro parameter it starts with: its argument may be in parentheses)
                const std::string v = Print(o.expr);
                const std::string first = v.substr(0, v.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_"));
                // ALASM puts the argument's text in: a parameter that is the whole operand may be a register or memory
                const bool wholeArgument = alasm && v == first && parameters.count(first);
                return instruction && !v.empty() && (v[0] == '(' || (parameters.count(first) && !wholeArgument)) ? "0+" + v : v;
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

    /// sjasmplus' RELOCATE_START / RELOCATE_END (comments) and RELOCATE_TABLE [offset]: the words the layout found
    std::optional<std::vector<std::string>> Relocation(const std::string& text)
    {
        const std::string word = z80::Upper(text.substr(0, text.find_first_of(" \t")));
        const size_t at = text.find_first_not_of(" \t", word.size());
        const std::string rest = at == std::string::npos ? std::string() : text.substr(at);
        if (word == "RELOCATE_START" || word == "RELOCATE_END")
            return std::vector<std::string>{"@; " + text + " (the table comes from the layout)"};
        if (word != "RELOCATE_TABLE")
            return std::nullopt;
        std::string offset;
        if (!rest.empty())
        {
            // one expression (sjasmplus reports RELOCATE_TABLE @@ / $1000,123 as errors)
            const FrontendResult parsed = SjasmplusFrontend().Parse(SourceDocument::FromText("        DW " + rest, "sjasmplus"));
            std::function<bool(const Expr&)> named = [&](const Expr& e) {
                if (e.kind == Expr::Kind::Symbol)
                {
                    const size_t at = e.text.find_first_not_of("@.");
                    return at != std::string::npos && (std::isalpha(static_cast<unsigned char>(e.text[at])) || e.text[at] == '_');
                }
                return std::all_of(e.args.begin(), e.args.end(), [&](const Expr& a) { return named(a); });
            };
            const bool one = parsed.program.lines.size() == 1 && parsed.program.lines[0].statements.size() == 1 &&
                             parsed.program.lines[0].statements[0].operands.size() == 1 &&
                             parsed.program.lines[0].statements[0].operands[0].kind == Operand::Kind::Immediate &&
                             parsed.program.lines[0].statements[0].operands[0].expr.kind != Expr::Kind::Raw &&
                             named(parsed.program.lines[0].statements[0].operands[0].expr);
            if (!one || std::any_of(parsed.diagnostics.begin(), parsed.diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::Error; }))
                return std::vector<std::string>{NotConverted(text)};
            offset = "-(" + Print(parsed.program.lines[0].statements[0].operands[0].expr) + ")";
        }
        std::vector<std::string> out;
        for (size_t k = 0;; ++k)
        {
            const auto found = options.labelValues.find("__UNREALASM_RELOC_" + std::to_string(k));
            if (found == options.labelValues.end())
                break;
            out.push_back("DW " + Hex(found->second.value & 0xFFFF, 4) + offset);
        }
        diagnostics.push_back({Severity::Info, line, 0, "RELOCATE_TABLE written as its " + std::to_string(out.size()) + " word(s)"});
        if (out.empty())
            out.push_back("@; " + text + " (no relocation data)");
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

    /// The Z80N instructions: z80asm (-mz80n) reads them by name, in its own spellings (SWAPNIB, MIRROR A, MUL D,E, PIXELDN
    /// with no operand); pasmo has no Z80N, so it gets the bytes. LDIRSCALE (ED B6) is unknown to both
    std::optional<std::vector<std::string>> Z80nText(const Statement& s)
    {
        const std::string& m = s.mnemonic;
        const auto& ops = s.operands;
        const auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
        const auto value = [&](size_t k) { return k < ops.size() && ops[k].kind == Operand::Kind::Immediate; };
        int code = -1;
        char layout = 'n';   // n none, b byte, w word (low first), W word (high first), B two bytes
        std::string text;    // the z80asm spelling
        if (m == "swapnib") code = 0x23, text = "SWAPNIB";
        else if (m == "mirror") code = 0x24, text = "MIRROR A";
        else if (m == "test") code = 0x27, layout = 'b', text = "TEST ";
        else if (m == "bsla") code = 0x28, text = "BSLA DE,B";
        else if (m == "bsra") code = 0x29, text = "BSRA DE,B";
        else if (m == "bsrl") code = 0x2A, text = "BSRL DE,B";
        else if (m == "bsrf") code = 0x2B, text = "BSRF DE,B";
        else if (m == "brlc") code = 0x2C, text = "BRLC DE,B";
        else if (m == "mul") code = 0x30, text = "MUL D,E";
        else if (m == "add" && ops.size() == 2 && reg(1, "a") && (reg(0, "hl") || reg(0, "de") || reg(0, "bc")))
            code = reg(0, "hl") ? 0x31 : reg(0, "de") ? 0x32 : 0x33, text = "ADD " + z80::Upper(ops[0].text) + ",A";
        else if (m == "add" && ops.size() == 2 && value(1) && (reg(0, "hl") || reg(0, "de") || reg(0, "bc")))
            code = reg(0, "hl") ? 0x34 : reg(0, "de") ? 0x35 : 0x36, layout = 'w', text = "ADD " + z80::Upper(ops[0].text) + ",";
        else if (m == "push" && ops.size() == 1 && value(0)) code = 0x8A, layout = 'W', text = "PUSH ";
        else if (m == "outinb") code = 0x90, text = "OUTINB";
        else if (m == "nextreg" && ops.size() == 2 && value(0) && value(1)) code = 0x91, layout = 'B', text = "NEXTREG ";
        else if (m == "nextreg" && ops.size() == 2 && value(0) && reg(1, "a")) code = 0x92, layout = 'b', text = "NEXTREG ";
        else if (m == "pixeldn") code = 0x93, text = "PIXELDN";
        else if (m == "pixelad") code = 0x94, text = "PIXELAD";
        else if (m == "setae") code = 0x95, text = "SETAE";
        else if (m == "jp" && ops.size() == 1 && ops[0].kind == Operand::Kind::Indirect && ops[0].text == "c") code = 0x98, text = "JP (C)";
        else if (m == "ldix") code = 0xA4, text = "LDIX";
        else if (m == "ldws") code = 0xA5, text = "LDWS";
        else if (m == "lddx") code = 0xAC, text = "LDDX";
        else if (m == "ldirscale") code = 0xB6;
        else if (m == "ldirx") code = 0xB4, text = "LDIRX";
        else if (m == "ldpirx") code = 0xB7, text = "LDPIRX";
        else if (m == "lddrx") code = 0xBC, text = "LDDRX";
        if (code < 0)
            return std::nullopt;
        // The operand spellings sjasmplus takes besides these (SWAPNIB A, PIXELDN HL, MUL DE) mean the same
        if (Z80asm() && code != 0xB6)
        {
            if (layout == 'b' && code == 0x27)
                text += Print(ops[0].expr);
            else if (layout == 'w' || layout == 'W')
                text += Print(ops[ops.size() - 1].expr);
            else if (layout == 'B' || (layout == 'b' && code == 0x92))
                text += Print(ops[0].expr) + (layout == 'B' ? "," + Print(ops[1].expr) : ",A");
            return std::vector<std::string>{text};
        }
        const std::string head = "DB 237," + std::to_string(code);
        if (layout == 'n')
            return std::vector<std::string>{head};
        if (layout == 'b' && code == 0x27)
            return std::vector<std::string>{head + "," + Print(Masked8(ops[0].expr))};
        if (layout == 'b' || layout == 'B')
            return std::vector<std::string>{head + "," + Print(Masked8(ops[0].expr)) + (layout == 'B' ? "," + Print(Masked8(ops[1].expr)) : "")};
        // the low byte with AND: in pasmo &255 is a hexadecimal number
        const std::string word = Print(Masked16(ops[ops.size() - 1].expr));
        if (layout == 'W')
            return std::vector<std::string>{head + ",(" + word + ")/256,(" + word + ") AND 255"};
        return std::vector<std::string>{head + ",(" + word + ") AND 255,(" + word + ")/256"};
    }

    std::vector<std::string> InstructionText(const Statement& s)
    {
        const std::string m = s.mnemonic;
        if (auto next = Z80nText(s))
            return *next;
        // sjasmplus repeats an instruction for its operands (LD A,1,B,2; PUSH AF,BC) and has fake ones (LD BC,DE)
        if (sjasmplus)
            for (const auto& parts : {SjasmplusRepeated(s), SjasmplusFake(s)})
                if (parts.size() > 1)
                {
                    std::vector<std::string> out;
                    for (const Statement& one : parts)
                        for (std::string& t : InstructionText(one))
                            out.push_back(std::move(t));
                    return out;
                }
        // pasmo knows neither IN F,(C) nor OUT (C),0: their bytes
        if (!Z80asm() && m == "in" && s.operands.size() == 2 && s.operands[0].kind == Operand::Kind::Register && s.operands[0].text == "f")
            return {"DB #ED,#70"};
        if (!Z80asm() && m == "out" && s.operands.size() == 2 && s.operands[1].kind == Operand::Kind::Immediate && s.operands[1].expr.kind == Expr::Kind::Number &&
            s.operands[1].expr.value == 0)
            return {"DB #ED,#71"};
        // RLC (IX+d),B / SET b,(IX+d),C: the undocumented copy to a register; pasmo has no spelling, its bytes
        if (!Z80asm())
        {
            static const std::map<std::string, int> kShifts = {{"rlc", 0x00}, {"rrc", 0x08}, {"rl", 0x10}, {"rr", 0x18},
                                                               {"sla", 0x20}, {"sra", 0x28}, {"sll", 0x30}, {"sli", 0x30}, {"srl", 0x38}};
            static const std::map<std::string, int> kRegisters = {{"b", 0}, {"c", 1}, {"d", 2}, {"e", 3}, {"h", 4}, {"l", 5}, {"a", 7}};
            const auto& ops = s.operands;
            const bool bitOp = m == "res" || m == "set";
            const size_t at = bitOp ? 1 : 0;
            const bool index = ops.size() == at + 2 && (ops[at].kind == Operand::Kind::Indexed ||
                                                         (ops[at].kind == Operand::Kind::Indirect && (ops[at].text == "ix" || ops[at].text == "iy")));
            if ((bitOp || kShifts.count(m)) && index && ops[at + 1].kind == Operand::Kind::Register && kRegisters.count(ops[at + 1].text))
            {
                const int r = kRegisters.at(ops[at + 1].text);
                const std::string d = ops[at].kind == Operand::Kind::Indexed ? Print(Masked8(ops[at].expr)) : "0";   // (IX): no offset
                const std::string code = bitOp ? "#" + z80::HexDigits(static_cast<uint64_t>((m == "res" ? 0x80 : 0xC0) | r), 2) + "+((" +
                                                     Print(ops[0].expr) + ") AND 7)*8"
                                               : std::to_string(kShifts.at(m) | r);
                return Checked({"DB " + std::string(ops[at].text == "ix" ? "#DD" : "#FD") + ",#CB," + d + "," + code}, z80::Upper(m));
            }
        }
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
            Expr shifted = Expr::Binary(Op::Sub, Grouped(ops.back().expr), Expr::Symbol(kDelta));
            ops.back().expr = std::move(shifted);
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
        // name = value (written as DEFL): z80asm puts the value's text where the name is used, so a $ in it is a label
        // put here (pcLabel)
        auto deflOf = [&]() {
            deflText = Z80asm();
            deflName = label;
            std::string text = args();
            deflText = false;
            // z80asm puts the text where the name is used, without parentheses (N DEFL N-1 makes P-N P-4-1)
            if (Z80asm() && s.args.size() == 1 && (s.args[0].kind == Expr::Kind::Binary || s.args[0].kind == Expr::Kind::Unary))
                text = "(" + text + ")";
            if (Z80asm() && growing.count(label))
            {
                std::string file = options.fileName.empty() ? std::string("main") : options.fileName;
                for (char& c : file)
                    if (!std::isalnum(static_cast<unsigned char>(c)))
                        c = '_';
                const std::string value = "__UNREALASM_V_" + file + "_" + std::to_string(++values);
                std::vector<std::string> out;
                if (inMacro)
                    out.push_back("LOCAL " + value);   // a value of each expansion
                out.push_back("@" + value + " EQU " + text);
                out.push_back("= " + value);
                return Checked(out, "DEFL");
            }
            return Checked({"= " + text}, "DEFL");
        };
        switch (s.directive)
        {
            case ir::DirectiveKind::Org:
                virtualAddress.clear();
                if (s.args.size() > 1)
                    diagnostics.push_back({Severity::Warning, line, 0, std::string("ORG with a page: ") + TargetName() + " has no pages; the page is left out"});
                // In a macro, from numbers and names whose text is numbers: a constant at every expansion, so a section of
                // its own (LOCAL: one per expansion), after the ones it may write over in the map
                if (Z80asm() && inMacro && !phased && Numeric(s.args[0]))
                {
                    const std::string section = "s_m_" + SectionFile() + "_" + std::to_string(++macroSections);
                    macroOrg = Print(Masked16(s.args[0]));
                    macroBytes = 0;
                    return Checked({"LOCAL " + section, "SECTION " + section, "ORG " + macroOrg}, "ORG");
                }
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
                    {
                        if (inMacro && macroBytes && !macroOrg.empty())
                        {
                            // A skip in the macro's section: the next section starts past it (DEFS would write zeros)
                            *macroBytes += a.args[1].value;
                            const std::string section = "s_m_" + SectionFile() + "_" + std::to_string(++macroSections);
                            return Checked({"LOCAL " + section, "SECTION " + section, "ORG (" + macroOrg + "+" + std::to_string(*macroBytes) + "&$FFFF)"}, "ORG");
                        }
                        return Checked({"DEFS " + Print(a.args[1])}, "ORG");
                    }
                    // A move back to an address the layout gives (ORG $-2: the next bytes write over the last ones; a
                    // name the converter does not know, when the ORG ends its line: the next line's address): a section
                    // of its own at that address, after the one it writes over in the map
                    if (const auto here = LineAddress(); here && !phased)
                        if (auto to = KnownAt(a, *here) ? KnownAt(a, *here) : lastStatement ? LineAddress(line + 1) : std::nullopt;
                            to && (*to & 0xFFFF) < *here)
                            return {"SECTION " + NextSection(), "ORG " + Hex(*to, 4) + "             ; unreal-asm: ORG " + Print(a) + " (the layout's address)"};
                    if (a.kind == Expr::Kind::Binary && a.op == Op::Sub && a.args[0].kind == Expr::Kind::Current && a.args[1].kind == Expr::Kind::Number &&
                        a.args[1].value > 0)
                        diagnostics.push_back({Severity::Warning, line, 0, "ORG " + Print(a) + ": z80asm cannot move back in a section (its DEFS is an error)"});
                    else if (Mentions(a, [&](const std::string& n) { return redefinable && redefinable->count(n) > 0; }))
                        diagnostics.push_back({Severity::Warning, line, 0,
                                               "ORG " + Print(a) + " by DEFS: z80asm moves only forward in a section, from its start; a redefinable name "
                                               "holding a number or an address before $ comes out wrong"});
                    return Checked({"DEFS " + Print(Expr::Binary(Op::Sub, Grouped(Masked16(a)), Expr::Make(Expr::Kind::Current)))}, "ORG");
                }
                if (Z80asm())
                {
                    phased = false;
                    return Checked({"SECTION " + NextSection(), "ORG " + Print(Masked16(s.args[0]))}, "ORG");
                }
                return Checked({"ORG " + Print(s.args[0])}, "ORG");
            case ir::DirectiveKind::Equ:
                if (redefinable && redefinable->count(label))
                    return deflOf();
                // z80asm 2.3 stops on an assertion (expr1.c: type != TYPE_COMPUTED) when an expression with an address
                // uses a name made of an address beyond a sum: the name is the layout's value
                if (Z80asm() && s.args.size() == 1 && AddressBeyondSum(s.args[0]))
                    if (const auto v = options.labelValues.find(label); v != options.labelValues.end() && !v->second.redefinable)
                        return {"EQU " + (v->second.value >= 0 && v->second.value <= 0xFFFF ? Hex(v->second.value, 4) : std::to_string(v->second.value)) +
                                "             ; unreal-asm: EQU " + Print(s.args[0]) + " (the layout's value)"};
                if (s.args.size() == 1)
                {
                    if (const auto v = Known(s.args[0]))
                        constants[label] = BeyondWord(s.args[0]) ? *v & 0xFFFF : *v;
                    return Checked({"EQU " + Print(Word(s.args[0]))}, "EQU");
                }
                return Checked({"EQU " + args()}, "EQU");
            case ir::DirectiveKind::Defl:
                if (redefinable && !redefinable->count(label))
                    return Checked({"EQU " + args()}, "EQU");   // assigned once
                return deflOf();
            case ir::DirectiveKind::Db: return Checked({"DB " + Operands(s.operands)}, "DB");
            case ir::DirectiveKind::Dw: return Checked({"DW " + Operands(s.operands)}, "DW");
            case ir::DirectiveKind::Ds:
                // z80asm's $ and labels count from the section's start in DEFS (DS #6060-$ is #6000 too many): a count
                // computed from them is the one the layout gives
                if (Z80asm() && !s.args.empty() && (UsesCurrent(s.args[0]) || UsesAddressLabel(s.args[0])))
                    if (const auto here = LineAddress())
                    {
                        std::optional<int64_t> count = KnownAt(s.args[0], *here);
                        if (!count && lastStatement)
                            if (const auto next = LineAddress(line + 1))
                                count = (*next - *here) & 0xFFFF;
                        if (count)
                        {
                            Statement counted = s;
                            counted.args[0] = Expr::Number(*count);
                            std::vector<std::string> out = StatementText(counted, label);
                            if (!out.empty() && out.back().find(';') == std::string::npos)
                                out.back() += "             ; unreal-asm: DS " + Print(s.args[0]) + " (the layout's count)";
                            return out;
                        }
                    }
                if (s.operands.size() > 1 && !s.params.empty() && s.params[0] == "cyclic")
                    return Checked(CyclicFill(s), "DS");
                if (!s.operands.empty())
                {
                    if (s.operands.size() == 1 && s.operands[0].kind == Operand::Kind::Immediate)
                        return Checked({"DS " + Print(Word(s.args[0])) + "," + Operands(s.operands)}, "DS");
                    return Checked({"REPT " + Print(Word(s.args[0])), "DB " + Operands(s.operands), RepeatEnd()}, "DS");
                }
                if (s.args.empty())
                    return Checked({"DS " + args()}, "DS");
                if (s.args.size() <= 2)
                    return Checked({"DS " + Print(Word(s.args[0])) + (s.args.size() > 1 ? "," + Print(s.args[1]) : "")}, "DS");
                return Checked({"REPT " + Print(Word(s.args[0])), "DB " + args(1), RepeatEnd()}, "DS");
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
                if (Z80asm())
                {
                    // z80asm has no ORG back within a section: the tail in a section of its own at the file's end (the
                    // map's s_tail_ sections first, the code after them writes over them), then the code goes on in a
                    // new section from there
                    std::optional<int64_t> end;
                    if (const auto here = LineAddress(); here && options.fileSize)
                        if (const auto size = options.fileSize(name))
                            end = *here + static_cast<int64_t>(*size);
                    if (!end && lastStatement)
                        end = LineAddress(line + 1);
                    if (!end || phased)
                        return {incbin, NotConverted("the rest of the last sector of \"" + name + "\" (TASM copies it; z80asm cannot move back)")};
                    std::string tail = NextSection();
                    tail.insert(2, "tail_");
                    return {incbin, "SECTION " + tail, "ORG " + Hex(*end, 4), "BINARY \"" + name + ".slack\"", "SECTION " + NextSection(), "ORG " + Hex(*end, 4)};
                }
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
                {
                    // a condition the target cannot compute: the block is assembled (its ELSE / ENDIF stay paired)
                    std::vector<std::string> out = Checked({"IF " + Print(Word(s.args[0]))}, "IF");
                    if (out.size() == 1 && out[0].rfind("@; unreal-asm: not converted", 0) == 0)
                        out.push_back("IF 1");
                    return out;
                }
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
                    // z80asm's PHASE takes a constant: an address computed from $ or labels (ZX-ASM's return to the
                    // outer PHASE) is the one the layout gives the next line
                    if (UsesCurrent(s.args[0]) || UsesAddressLabel(s.args[0]))
                        if (const auto at = lastStatement ? LineAddress(line + 1) : std::nullopt)
                        {
                            phased = true;
                            return {"PHASE " + Hex(*at & 0xFFFF, 4) + "             ; unreal-asm: PHASE " + Print(s.args[0]) + " (the layout's address)"};
                        }
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
            case ir::DirectiveKind::End: return {Z80asm() ? "@; END" : "END"};
            case ir::DirectiveKind::IfUsed:
                diagnostics.push_back({Severity::Warning, line, 0, "IFUSED " + s.text + ": " + TargetName() + " has no IFUSED; the block is assembled"});
                return {"IF 1 ; unreal-asm: IFUSED " + s.text + " (" + TargetName() + " has none)"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Warning, line, 0, "SAVEBIN \"" + s.text + "\": " + TargetName() + " writes its own output; kept as a comment"});
                return {"@; unreal-asm: SAVEBIN \"" + s.text + "\"," + args()};
            case ir::DirectiveKind::Main: return {"@; MAIN \"" + s.text + "\" (the project's main source)"};
            case ir::DirectiveKind::Run: return {NotConverted("RUN " + args() + " (code called while assembling)")};
            default:
                // OPT --zxnext / DEVICE ZXSPECTRUMNEXT: the Z80N is on in the target by other means (z80asm -mz80n; pasmo gets the bytes)
                if (z80::EnablesZ80n(s.text))
                    return {Z80asm() ? "; Z80N source: assemble with z80asm -mz80n" : "; Z80N source: its instructions are written as bytes"};
                // a MODULE's mark (sjasmpluslower.h): its names are full already, its line keeps it as a comment
                if (s.text.rfind(kModuleMark, 0) == 0 || s.text == kEndModuleMark)
                    return {};
                if (auto table = Relocation(s.text))
                    return *table;
                return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }
};

/// An expression's value as the layout gives its names (fixed ones only); none when a name is unknown
std::optional<int64_t> LayoutValue(const Expr& e, const std::map<std::string, SourceLabel>& values)
{
    switch (e.kind)
    {
        case Expr::Kind::Number: return e.value;
        case Expr::Kind::Symbol:
        {
            const auto v = values.find(e.text);
            return v != values.end() && !v->second.redefinable ? std::optional<int64_t>(v->second.value) : std::nullopt;
        }
        case Expr::Kind::Group: return LayoutValue(e.args[0], values);
        case Expr::Kind::Unary:
        {
            const auto a = LayoutValue(e.args[0], values);
            if (!a)
                return std::nullopt;
            switch (e.op)
            {
                case Op::Negate: return -*a;
                case Op::Plus: return *a;
                case Op::Not: return ~*a;
                case Op::High: return (*a >> 8) & 0xFF;
                case Op::Low: return *a & 0xFF;
                case Op::LogicalNot: return *a == 0;
                default: return std::nullopt;
            }
        }
        case Expr::Kind::Binary:
        {
            const auto a = LayoutValue(e.args[0], values), b = LayoutValue(e.args[1], values);
            if (!a || !b)
                return std::nullopt;
            switch (e.op)
            {
                case Op::Add: return *a + *b;
                case Op::Sub: return *a - *b;
                case Op::Mul: return *a * *b;
                case Op::Div: return *b ? std::optional<int64_t>(*a / *b) : std::nullopt;
                case Op::Mod: return *b ? std::optional<int64_t>(*a % *b) : std::nullopt;
                case Op::And: return *a & *b;
                case Op::Or: return *a | *b;
                case Op::Xor: return *a ^ *b;
                case Op::Shl: return *b >= 0 && *b < 32 ? std::optional<int64_t>(*a << *b) : std::nullopt;
                case Op::Shr: return *b >= 0 && *b < 32 ? std::optional<int64_t>(*a >> *b) : std::nullopt;
                case Op::Equal: return *a == *b;
                case Op::NotEqual: return *a != *b;
                case Op::Less: return *a < *b;
                case Op::Greater: return *a > *b;
                case Op::LessEqual: return *a <= *b;
                case Op::GreaterEqual: return *a >= *b;
                case Op::LogicalAnd: return *a != 0 && *b != 0;
                case Op::LogicalOr: return *a != 0 || *b != 0;
                default: return std::nullopt;
            }
        }
        default: return std::nullopt;
    }
}

}  // namespace

/// Local labels (.loop) and qualified names (start.loop) exist in sjasmplus and its relatives; pasmo and z80asm have neither.
/// Each becomes a plain name: a leading period takes the last ordinary label of the source before it, every other period
/// is written as two underscores (macro bodies keep theirs: the backends make those unique per expansion)
void RewriteDotNames(ir::Program& program)
{
    std::string scope = "L";
    bool inMacro = false;
    const bool full = program.dialect == "sjasmplus";   // sjasmplus' @name is a full name (the lowered names have it)
    const auto rename = [&](const std::string& given) -> std::string {
        const std::string name = full && given.size() > 1 && given[0] == '@' ? given.substr(1) : given;
        if (name.empty() || name.find('.') == std::string::npos)
            return name;
        std::string out = name[0] == '.' ? scope + "__" + name.substr(1) : name;
        for (size_t k = 0; k < out.size(); ++k)
            if (out[k] == '.')
                out.replace(k, 1, "__");
        return out;
    };
    std::function<void(Expr&)> walk = [&](Expr& e) {
        if (e.kind == Expr::Kind::Symbol && !inMacro)
            e.text = rename(e.text);
        for (Expr& a : e.args)
            walk(a);
    };
    for (ir::Line& line : program.lines)
    {
        for (Statement& s : line.statements)
            if (s.kind == Statement::Kind::Directive)
            {
                if (s.directive == ir::DirectiveKind::Macro)
                    inMacro = true;
                else if (s.directive == ir::DirectiveKind::EndMacro)
                    inMacro = false;
            }
        if (!line.label.empty() && !inMacro)
        {
            // sjasmplus' !label leaves the parent of the .labels after it as it is
            const bool keepsParent = full && line.label.size() > 1 && line.label[0] == '!';
            if (keepsParent)
                line.label.erase(0, 1);
            else if (line.label[0] != '.')
                scope = rename(line.label);
            line.label = rename(line.label);
        }
        for (Statement& s : line.statements)
        {
            for (Expr& e : s.args)
                walk(e);
            for (Operand& o : s.operands)
                walk(o.expr);
        }
    }
}

BackendResult WriteClassic(const ir::Program& input, const BackendOptions& options, ClassicTarget target)
{
    BackendResult result;
    // sjasmplus' MODULE, STRUCT, DZ ... in words pasmo and z80asm have (sjasmpluslower.h)
    ir::Program program = input.dialect == "sjasmplus" ? LowerSjasmplus(input, result.diagnostics) : input;
    RewriteDotNames(program);
    result.document.dialect = target == ClassicTarget::Z80asm ? "z88dk" : "pasmo";
    result.document.codePage = encoding::CodePage::Cp866;   // texts are program bytes: the Spectrum code page
    Writer w{options, result.diagnostics, target};
    w.trueValue = program.trueValue;
    w.sjasmplus = program.dialect == "sjasmplus";
    w.alasm = program.dialect == "alasm";
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
    std::map<std::string, int> callArguments;
    for (const ir::Line& l : program.lines)
        for (const Statement& s : l.statements)
        {
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Defl && !l.label.empty())
                redefinable.insert(l.label);
            if (s.kind == Statement::Kind::MacroCall)
                callArguments[s.mnemonic] = std::max(callArguments[s.mnemonic], static_cast<int>(s.params.size()));
        }
    // an IF on names defined before it (or in another file: RAR's rarLZ4 tests fastCPIQ of RARB33), which the layout
    // knows, counts its taken branch only (one on a name defined later is decided below, its branch left out still
    // giving the value the pass before left)
    std::map<std::string, size_t> firstDefinition;
    for (size_t k = 0; k < program.lines.size(); ++k)
        if (!program.lines[k].label.empty())
            firstDefinition.emplace(program.lines[k].label, k);
    std::function<bool(const Expr&, size_t)> definedBefore = [&](const Expr& e, size_t at) {
        if (e.kind == Expr::Kind::Symbol)
        {
            const auto first = firstDefinition.find(e.text);
            return first == firstDefinition.end() || first->second < at;
        }
        return std::all_of(e.args.begin(), e.args.end(), [&](const Expr& a) { return definedBefore(a, at); });
    };
    std::map<std::string, int> definitions = CountDefinitions(program, [&](const Expr& c, size_t at) -> std::optional<bool> {
        if (!definedBefore(c, at))
            return std::nullopt;
        const auto v = LayoutValue(c, options.labelValues);
        return v ? std::optional<bool>(*v != 0) : std::nullopt;
    });
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

    for (size_t index = 0; index < program.lines.size(); ++index)
        if (Writer::IsTemporary(program.lines[index].label))
        {
            auto& defined = w.temporaries[program.lines[index].label];
            defined.push_back({index, "__T" + program.lines[index].label + "_" + std::to_string(defined.size() + 1)});
        }
    // A source of two passes (TASM's `.IF PASS` / `PASS EQU 1` / `.ENDIF`, a name its pass 1 sets for pass 2): an IF
    // outside a macro on a name defined only after it is decided from the layout (the target's IF needs its names
    // defined before), the branch not taken left out. So is z80asm's IF on an address (its labels count from their
    // section's start there: `IF high maintree&#40` is 0). A redefinable name that branch assigns, and no line before it,
    // starts with the value the pass before left (the layout's)
    std::set<size_t> leftOut;
    std::map<size_t, std::vector<std::string>> decidedLines;
    {
        std::map<std::string, size_t> firstIndex;
        for (size_t k = 0; k < program.lines.size(); ++k)
            if (!program.lines[k].label.empty())
                firstIndex.emplace(program.lines[k].label, k);
        const auto is = [](const ir::Line& l, ir::DirectiveKind kind) {
            return std::any_of(l.statements.begin(), l.statements.end(),
                               [kind](const Statement& s) { return s.kind == Statement::Kind::Directive && s.directive == kind; });
        };
        std::function<void(const Expr&, std::set<std::string>&)> names = [&](const Expr& e, std::set<std::string>& into) {
            if (e.kind == Expr::Kind::Symbol)
                into.insert(e.text);
            for (const Expr& a : e.args)
                names(a, into);
        };
        bool inMacro = false;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            inMacro = (inMacro || is(l, ir::DirectiveKind::Macro)) && !is(l, ir::DirectiveKind::EndMacro);
            if (inMacro || leftOut.count(k) || !only(l, ir::DirectiveKind::If) || l.statements[0].args.empty())
                continue;
            std::set<std::string> tested;
            names(l.statements[0].args[0], tested);
            bool later = false, known = !tested.empty();
            for (const std::string& n : tested)
            {
                const auto first = firstIndex.find(n);
                later = later || first == firstIndex.end() || first->second > k || (w.Z80asm() && w.addressLabels.count(n));
                const auto v = options.labelValues.find(n);
                known = known && v != options.labelValues.end() && !v->second.redefinable && !redefinable.count(n);
            }
            if (!later || !known)
                continue;
            // the condition's value as the layout gives its names (any true is not 0)
            const auto value = LayoutValue(l.statements[0].args[0], options.labelValues);
            // its ELSE and ENDIF; no block opens or closes in it (their lines are matched by index)
            size_t elseAt = 0, endAt = 0;
            bool plain = value.has_value();
            int depth = 0;
            for (size_t e = k + 1; e < program.lines.size() && !endAt && plain; ++e)
            {
                const ir::Line& b = program.lines[e];
                for (const ir::DirectiveKind kind : {ir::DirectiveKind::Macro, ir::DirectiveKind::EndMacro, ir::DirectiveKind::LocalBlock,
                                                     ir::DirectiveKind::EndLocalBlock, ir::DirectiveKind::Repeat, ir::DirectiveKind::EndRepeat})
                    plain = plain && !is(b, kind);
                if (is(b, ir::DirectiveKind::If) || is(b, ir::DirectiveKind::IfUsed))
                    ++depth;
                else if (only(b, ir::DirectiveKind::EndIf) && depth == 0)
                    endAt = e;
                else if (is(b, ir::DirectiveKind::EndIf))
                    --depth;
                else if (only(b, ir::DirectiveKind::Else) && depth == 0)
                    elseAt = e;
            }
            if (!plain || !endAt)
                continue;
            const bool taken = *value != 0;
            const size_t from = taken ? (elseAt ? elseAt : endAt) : k, to = taken ? endAt : (elseAt ? elseAt : endAt);
            std::vector<std::string>& lines = decidedLines[k];
            lines.push_back(std::string("; unreal-asm: IF ") + (taken ? "taken" : "left out") + " (the layout gives its names)");
            for (size_t e = from; e <= to; ++e)
            {
                const ir::Line& b = program.lines[e];
                if (!taken && e > k && e < to && !b.label.empty() && redefinable.count(b.label) && firstIndex[b.label] == e)
                    if (const auto v = options.labelValues.find(b.label); v != options.labelValues.end())
                        lines.push_back(w.Name(b.label) + " DEFL " + std::to_string(v->second.value) + "   ; the value the pass before left");
                leftOut.insert(e);
            }
            leftOut.insert(k);
            leftOut.insert(endAt);
            if (elseAt)
                leftOut.insert(elseAt);
        }
    }

    // z80asm's DEFL is a text macro (its preprocessor puts the expression's text in place of the name): two names
    // assigned from each other (X DEFL X+Y/41, Y DEFL Y-X/41) would double the text at every assignment, so each of
    // their values is an EQU of its own
    if (w.Z80asm())
    {
        std::map<std::string, std::set<std::string>> uses;
        std::function<void(const Expr&, std::set<std::string>&)> names = [&](const Expr& e, std::set<std::string>& into) {
            if (e.kind == Expr::Kind::Symbol)
                into.insert(e.text);
            for (const Expr& a : e.args)
                names(a, into);
        };
        for (const ir::Line& l : program.lines)
            if (!l.label.empty() && redefinable.count(l.label))
                for (const Statement& s : l.statements)
                    if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Defl && !s.args.empty())
                        names(s.args[0], uses[l.label]);
        for (const auto& [name, used] : uses)
            for (const std::string& other : used)
                if (other < name && uses.count(other) && uses[other].count(name))
                {
                    w.growing.insert(name);
                    w.growing.insert(other);
                }
        // Names whose every assignment is numbers, constants and such names, $ only outside macros and loops
        std::set<std::string> candidates;
        std::map<std::string, std::vector<std::pair<const Expr*, bool>>> assignments;   // the expression, $ allowed there
        int macroDepth = 0, loopDepth = 0;
        for (const ir::Line& l : program.lines)
            for (const Statement& s : l.statements)
            {
                if (s.kind != Statement::Kind::Directive)
                    continue;
                if (s.directive == ir::DirectiveKind::Macro)
                    ++macroDepth;
                else if (s.directive == ir::DirectiveKind::EndMacro)
                    --macroDepth;
                else if (s.directive == ir::DirectiveKind::Repeat || s.directive == ir::DirectiveKind::While || s.directive == ir::DirectiveKind::RepeatUntil)
                    ++loopDepth;
                else if (s.directive == ir::DirectiveKind::EndRepeat || s.directive == ir::DirectiveKind::EndWhile || s.directive == ir::DirectiveKind::UntilZero)
                    --loopDepth;
                else if (!l.label.empty() && redefinable.count(l.label) && !w.growing.count(l.label) && !s.args.empty() &&
                         (s.directive == ir::DirectiveKind::Defl || s.directive == ir::DirectiveKind::Equ))
                {
                    candidates.insert(l.label);
                    assignments[l.label].push_back({&s.args[0], macroDepth == 0 && loopDepth == 0});
                }
            }
        for (const ir::Line& l : program.lines)   // a label of a line: an address in its section
            if (!l.label.empty() && std::none_of(l.statements.begin(), l.statements.end(), [](const Statement& s) {
                    return s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Defl || s.directive == ir::DirectiveKind::Equ);
                }))
                candidates.erase(l.label);
        w.numericNames = candidates;
        for (bool shrunk = true; shrunk;)
        {
            shrunk = false;
            for (const std::string& name : candidates)
            {
                if (!w.numericNames.count(name))
                    continue;
                for (const auto& [e, top] : assignments[name])
                {
                    std::function<bool(const Expr&)> numeric = [&](const Expr& x) {
                        if (x.kind == Expr::Kind::Current)
                            return top;
                        if (x.kind == Expr::Kind::Symbol)
                            return w.numericNames.count(x.text) > 0 || (!w.addressLabels.count(x.text) && !redefinable.count(x.text));
                        return std::all_of(x.args.begin(), x.args.end(), numeric);
                    };
                    if (!numeric(*e))
                    {
                        w.numericNames.erase(name);
                        shrunk = true;
                        break;
                    }
                }
            }
        }
    }

    size_t nextBlock = 0;
    int repeatMacros = 0;
    for (size_t index = 0; index < program.lines.size(); ++index)
    {
        const ir::Line& l = program.lines[index];
        w.line = l.sourceLine;
        w.lineIndex = index;
        for (const Statement& s : l.statements)
            if (s.kind == Statement::Kind::Directive)
            {
                if (s.directive == ir::DirectiveKind::Repeat || s.directive == ir::DirectiveKind::While || s.directive == ir::DirectiveKind::RepeatUntil)
                    ++w.loops;
                if (s.directive == ir::DirectiveKind::EndRepeat || s.directive == ir::DirectiveKind::EndWhile || s.directive == ir::DirectiveKind::UntilZero)
                    --w.loops;
            }
        if (const auto decided = decidedLines.find(index); decided != decidedLines.end())
            for (const std::string& text : decided->second)
                result.document.lines.push_back({text, {}});
        if (leftOut.count(index))
            continue;
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
                    params += (k ? "," : "") + w.Name(namedParams[k]);   // as the body spells it (a: L_a)
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
            w.parameters.clear();
            for (size_t from = 0; from < params.size();)
            {
                const size_t comma = std::min(params.find(',', from), params.size());
                w.parameters.insert(params.substr(from, comma - from));
                from = comma + 1;
            }
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
            else if (redefined && w.Z80asm())
            {
                // z80asm's DEFL is a text macro: the name stands for a label here, not for the $ where it is used
                const std::string here = w.NewPcLabel();
                if (w.inMacro)
                    result.document.lines.push_back({"        LOCAL " + here, {}});
                result.document.lines.push_back({here + ":", {}});
                result.document.lines.push_back({label + " DEFL " + here, {}});
            }
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
            w.lastStatement = &s == &l.statements.back();
            std::vector<std::string> written = w.StatementText(s, l.label);
            if (w.inMacro && w.macroBytes && !(s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Org))
            {
                const auto bytes = w.MacroBytes(s);
                w.macroBytes = bytes ? std::optional<int64_t>(*w.macroBytes + *bytes) : std::nullopt;
            }
            if (!w.pcLabel.empty() && w.inMacro)
                texts.push_back("LOCAL " + w.pcLabel);   // a label of each expansion
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
        {
            w.inMacro = false;
            w.macroOrg.clear();
            w.parameters.clear();
        }

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
