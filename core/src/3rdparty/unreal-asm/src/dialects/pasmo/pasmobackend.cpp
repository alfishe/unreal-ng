#include "dialects/pasmo/pasmobackend.h"

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

// The displacement emulated without PHASE: the run address minus the address where the code is put
constexpr const char* kDelta = "__UNREALASM_D";

// Words pasmo reserves whatever their case (mnemonics, registers, conditions, operators, directives)
const std::set<std::string> kReserved = {
    "a", "b", "c", "d", "e", "h", "l", "i", "r", "af", "bc", "de", "hl", "sp", "ix", "iy", "ixh", "ixl", "iyh", "iyl",
    "nz", "z", "nc", "po", "pe", "p", "m",
    "and", "or", "xor", "not", "mod", "shl", "shr", "eq", "ne", "lt", "le", "gt", "ge", "high", "low", "nul", "defined",
    "org", "equ", "defl", "db", "defb", "defm", "defs", "ds", "dw", "defw", "end", "endif", "endm", "endp", "exitm", "if",
    "ifdef", "ifndef", "include", "incbin", "irp", "local", "macro", "proc", "public", "rept", "else",
};

/// A word pasmo keeps for itself: its reserved words and every Z80 mnemonic (a label RET or LD is refused)
bool Reserved(const std::string& name)
{
    const std::string lower = z80::Lower(name);
    return kReserved.count(lower) || z80::IsMnemonic(lower) || lower == "sll" || lower == "inf";
}

/// A label pasmo reads as written: a letter, "_", "?", "@" or "." first, then letters, digits and _ ? @ . ("$" would be
/// ignored inside a name, so a name holding one is renamed too)
bool IsValidLabel(const std::string& name)
{
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

struct Writer
{
    const BackendOptions& options;
    Diagnostics& diagnostics;
    uint32_t line = 0;
    std::map<std::string, std::string> renames{};
    std::vector<std::map<std::string, std::string>> localScopes{};
    bool inMacro = false;
    bool displaced = false;   // a displacement may be active: labels and $ go through __UNREALASM_D
    int trueValue = 0;
    std::string problem{};    // set when an expression has no pasmo form
    const std::set<std::string>* redefinable = nullptr;

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
        return Reserved(name) || !IsValidLabel(name) ? SafeName(name) : name;
    }

    std::string Hex(int64_t value, int digits) const
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%0*llX", std::max(digits, 1), static_cast<unsigned long long>(value & 0xFFFF));
        return std::string("#") + buffer;
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
        return std::string("(") + op + (a.kind == Expr::Kind::Binary ? "(" + inner + ")" : inner) + ")";
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
        trueValue = 0;
        const std::string out = Print(top);
        trueValue = 1;
        return "(-(" + out + "))";   // pasmo's true is #FFFF
    }

    std::string Print(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return Number(e);
            case Expr::Kind::Symbol: return Name(e.text);
            case Expr::Kind::Current: return displaced ? std::string("($+") + kDelta + ")" : "$";
            case Expr::Kind::CurrentPhysical: return "$";
            case Expr::Kind::CurrentPage:
                problem = "$$ (the current page)";
                return "0";
            case Expr::Kind::Group: return "(" + Print(e.args[0]) + ")";
            case Expr::Kind::Memory:
                problem = "a memory read while assembling ({...})";
                return "0";
            case Expr::Kind::Raw: return e.text;
            case Expr::Kind::Defined: return "(NOT (DEFINED " + Print(e.args[0]) + "))";   // ALASM ?label: 0 when defined
            case Expr::Kind::Unary:
                switch (e.op)
                {
                    case Op::Negate: return Unary("-", e.args[0]);
                    case Op::Plus: return Unary("+", e.args[0]);
                    case Op::Not: return Unary("NOT ", e.args[0]);
                    case Op::LogicalNot:
                        if (trueValue == 1)
                            return TrueIsOne(e);
                        return Unary("!", e.args[0]);
                    case Op::High: return Unary("HIGH ", e.args[0]);
                    case Op::Low: return Unary("LOW ", e.args[0]);
                    case Op::Exists: return Unary("DEFINED ", e.args[0]);
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
                    return Print(Grouped(Expr::Binary(Op::Or, Expr::Binary(right ? Op::Shr : Op::Shl, a, b),
                                                      Expr::Binary(right ? Op::Shl : Op::Shr, a, Grouped(Expr::Binary(Op::Sub, Expr::Number(16), b))))));
                }
                const bool comparison = e.op == Op::Equal || e.op == Op::NotEqual || e.op == Op::Less || e.op == Op::Greater ||
                                        e.op == Op::LessEqual || e.op == Op::GreaterEqual;
                if (comparison && trueValue == 1)
                    return TrueIsOne(e);
                const int p = Priority(e.op);
                std::string left = Print(e.args[0]);
                std::string right = Print(e.args[1]);
                if (e.args[0].kind == Expr::Kind::Binary && Priority(e.args[0].op) < p)
                    left = "(" + left + ")";
                if (e.args[1].kind == Expr::Kind::Binary && Priority(e.args[1].op) <= p)
                    right = "(" + right + ")";
                return left + Symbol(e.op) + right;
            }
        }
        return "0";
    }

    static std::string Quote(const std::string& text)
    {
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
        return {NotConverted(what + ": pasmo has no " + reason)};
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
                out = {"REPT " + std::to_string(count / static_cast<int64_t>(k)), "DB " + Operands(s.operands), "ENDM"};
            const size_t rest = static_cast<size_t>(count % static_cast<int64_t>(k));
            if (rest > 0)
                out.push_back("DB " + Operands(std::vector<Operand>(s.operands.begin(), s.operands.begin() + static_cast<std::ptrdiff_t>(rest))));
            if (out.empty())
                out.push_back("@; DS 0");
            return out;
        }
        const std::string count = "(" + Print(s.args[0]) + ")";
        std::vector<std::string> out = {"REPT " + count + "/" + std::to_string(k), "DB " + Operands(s.operands), "ENDM"};
        for (size_t r = 1; r < k; ++r)
        {
            out.push_back("IF " + count + " MOD " + std::to_string(k) + " GE " + std::to_string(r));
            out.push_back("DB " + Operand_(s.operands[r - 1], false));
            out.push_back("ENDIF");
        }
        return out;
    }

    std::vector<std::string> InstructionText(const Statement& s)
    {
        const std::string m = s.mnemonic;
        // pasmo knows neither IN F,(C) nor OUT (C),0: their bytes
        if (m == "in" && s.operands.size() == 2 && s.operands[0].kind == Operand::Kind::Register && s.operands[0].text == "f")
            return {"DB #ED,#70"};
        if (m == "out" && s.operands.size() == 2 && s.operands[1].kind == Operand::Kind::Immediate && s.operands[1].expr.kind == Expr::Kind::Number &&
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
            return {"DB #ED,#70"};
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
                if (s.args.size() > 1)
                    diagnostics.push_back({Severity::Warning, line, 0, "ORG with a page: pasmo has no pages; the page is left out"});
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
                    return Checked({"REPT " + Print(s.args[0]), "DB " + Operands(s.operands), "ENDM"}, "DS");
                }
                if (s.args.size() <= 2)
                    return Checked({"DS " + args()}, "DS");
                return Checked({"REPT " + Print(s.args[0]), "DB " + args(1), "ENDM"}, "DS");
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
                        return {NotConverted("INCBIN \"" + name + "\"," + args() + " (pasmo's INCBIN takes the whole file)")};
                    return {"INCBIN \"" + name + "\" ; unreal-asm: slice " + std::to_string(s.args[0].value) +
                            (s.args.size() > 1 ? "," + std::to_string(s.args[1].value) : std::string())};
                }
                const std::string incbin = "INCBIN \"" + name + "\"";
                if (s.params.empty() || s.params[0] != "sector-slack")
                    return {incbin};
                // The rest of the last sector next, then the address goes back (pasmo keeps the later bytes)
                return {incbin, "@__UNREALASM_INCBIN_P DEFL $", "INCBIN \"" + name + ".slack\"", "ORG __UNREALASM_INCBIN_P"};
            }
            case ir::DirectiveKind::If: return Checked({"IF " + Print(s.args[0])}, "IF");
            case ir::DirectiveKind::Else: return {"ELSE"};
            case ir::DirectiveKind::EndIf: return {"ENDIF"};
            case ir::DirectiveKind::EndMacro: return {"ENDM"};
            case ir::DirectiveKind::Repeat: return Checked({"REPT " + args()}, "REPT");
            case ir::DirectiveKind::EndRepeat: return {"ENDM"};
            case ir::DirectiveKind::While:
                // REPT with an exit: pasmo has no WHILE
                return Checked({"REPT 65535", "IF (" + args() + ") EQ 0", "EXITM", "ENDIF"}, "WHILE");
            case ir::DirectiveKind::EndWhile: return {"ENDM"};
            case ir::DirectiveKind::RepeatUntil: return {"REPT 65535"};
            case ir::DirectiveKind::UntilZero: return Checked({"IF (" + args() + ") EQ 0", "EXITM", "ENDIF", "ENDM"}, "UNTIL");
            case ir::DirectiveKind::Disp:
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
                displaced = false;
                return {"@" + std::string(kDelta) + " DEFL 0"};
            case ir::DirectiveKind::Display: return {"@; DISPLAY " + Operands(s.operands)};
            case ir::DirectiveKind::End: return {"END"};
            case ir::DirectiveKind::IfUsed:
                diagnostics.push_back({Severity::Warning, line, 0, "IFUSED " + s.text + ": pasmo has no IFUSED; the block is assembled"});
                return {"IF 1 ; unreal-asm: IFUSED " + s.text + " (pasmo has none)"};
            case ir::DirectiveKind::SaveBinary:
                diagnostics.push_back({Severity::Warning, line, 0, "SAVEBIN \"" + s.text + "\": pasmo writes one output file; kept as a comment"});
                return {"@; unreal-asm: SAVEBIN \"" + s.text + "\"," + args()};
            case ir::DirectiveKind::Main: return {"@; MAIN \"" + s.text + "\" (the project's main source)"};
            case ir::DirectiveKind::Run: return {NotConverted("RUN " + args() + " (code called while assembling)")};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
    }
};
}  // namespace

BackendResult PasmoBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "pasmo";
    result.document.codePage = encoding::CodePage::Cp866;   // texts are program bytes: the Spectrum code page
    Writer w{options, result.diagnostics};
    w.trueValue = program.trueValue;

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
        if (Reserved(name) || !IsValidLabel(name))
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
    if (program.displacementAcrossFiles)
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
            result.document.lines.push_back({"        ENDM", {}});
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
            if (!b.inMacro)
                texts.push_back("PROC");   // a local block outside a macro: PROC ... ENDP holds the LOCAL names
            if (!locals.empty())
                texts.push_back("LOCAL " + locals);
            w.localScopes.push_back(scope);
            if (!b.inMacro)
                w.localScopes.back()["__proc"] = "1";
        }

        // A label inside a possible displacement: its value is the run address
        bool defines = false;
        for (const Statement& s : l.statements)
            defines = defines || (s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Equ || s.directive == ir::DirectiveKind::Defl ||
                                                                           s.directive == ir::DirectiveKind::Macro));
        if (!label.empty() && !defines)
        {
            const bool redefined = redefinable.count(l.label) > 0;
            if (w.displaced)
                result.document.lines.push_back({label + (redefined ? " DEFL $+" : " EQU $+") + kDelta, {}});
            else if (redefined)
                result.document.lines.push_back({label + " DEFL $", {}});
            else
                result.document.lines.push_back({label, {}});
            label.clear();
        }

        for (const Statement& s : l.statements)
        {
            if (s.kind == Statement::Kind::Directive &&
                (s.directive == ir::DirectiveKind::LocalBlock || s.directive == ir::DirectiveKind::EndLocalBlock || s.directive == ir::DirectiveKind::Macro))
                continue;
            for (std::string t : w.StatementText(s, l.label))
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
    return result;
}
}  // namespace unrealasm::dialects
