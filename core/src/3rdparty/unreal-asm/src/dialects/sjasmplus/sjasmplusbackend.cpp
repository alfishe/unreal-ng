#include "dialects/sjasmplus/sjasmplusbackend.h"

#include <algorithm>
#include <functional>
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

constexpr int kUnaryPriority = 10;

int Priority(Op op)
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
        default: return kUnaryPriority;
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
        case Op::Mod: return "%";
        case Op::And: return "&";
        case Op::Or: return "|";
        case Op::Xor: return "^";
        case Op::Shl: return "<<";
        case Op::Shr: return ">>";
        case Op::ShrUnsigned: return ">>>";
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

// The DEFINE that says a displacement is active, for sources whose displacement continues across INCLUDE
constexpr const char* kDisplacementFlag = "__UNREALASM_DISP";

// Words sjasmplus reads as operators, keywords, registers or conditions whatever their case: a label with such a name
// is renamed (ALASM keeps lower-case "iy" or "b" as labels, sjasmplus would read the register)
const std::set<std::string> kReserved = {
    "high", "low", "and", "or", "xor", "mod", "shl", "shr", "not", "abs", "norel", "exist", "sizeof",
    "a", "b", "c", "d", "e", "h", "l", "i", "r", "f", "af", "bc", "de", "hl", "sp", "ix", "iy",
    "ixh", "ixl", "iyh", "iyl", "hx", "lx", "hy", "ly", "xh", "xl", "yh", "yl",
    "nz", "z", "nc", "po", "pe", "p", "m",
};

/// A label sjasmplus reads as written: a letter or "_" first (after a "." local prefix), then letters, digits and
/// _ . ? !. An "@" is sjasmplus' global prefix ("@X" is X): a source's @ (TASM's @VAL, ALASM's @label) gets renamed
bool IsValidLabel(const std::string& name)
{
    const size_t first = !name.empty() && name[0] == '.' ? 1 : 0;
    if (name.size() <= first || !(std::isalpha(static_cast<unsigned char>(name[first])) || name[first] == '_'))
        return false;
    for (const char c : name)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '?' || c == '!'))
            return false;
    return true;
}

/// The rename of a label sjasmplus would reject or read as something else; the same in every file of a project
std::string SafeName(const std::string& name)
{
    std::string renamed = "L_" + name;
    for (char& c : renamed)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_'))
            c = '_';
    return renamed;
}

/// A macro name sjasmplus accepts (TASM 4.12 names macros HL*8)
std::string MacroName(const std::string& name)
{
    return IsValidLabel(name) && name[0] != '.' ? name : SafeName(name);
}

struct Writer
{
    const BackendOptions& options;
    Diagnostics& diagnostics;
    uint32_t line = 0;
    std::map<std::string, std::string> renames;                  // global label renames (reserved words)
    std::vector<std::map<std::string, std::string>> localScopes;   // LOCAL blocks, innermost last
    bool inMacro = false;
    bool instructionOperand = false;
    int wordBits = 0;
    bool unsignedWords = false;
    std::map<std::string, int> macroParams;   // parameters each macro defined so far declares
    int repeatCounter = 0;
    std::vector<int> repeatStack;
    bool sameDialect = false;   // the program was parsed from sjasmplus: directives kept as text are written back
    bool displacementFlag = false;   // DISP / ENT also keep a DEFINE that says whether a displacement is active
    const std::set<std::string>* redefinable = nullptr;   // names some line assigns with "="
    int trueValue = 0;   // what the source's comparisons give when true (ir::Program::trueValue); sjasmplus gives -1
    std::set<std::string> ifUsedNames{};   // labels some file tests with IFUSED: their definitions set a DEFINE

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
        // A label defined in another file of the project gets the same rename there
        return kReserved.count(z80::Lower(name)) || !IsValidLabel(name) ? SafeName(name) : name;
    }

    std::string Hex(int64_t value, int digits) const
    {
        return std::string(options.hexDollar ? "$" : "#") + z80::HexDigits(static_cast<uint64_t>(value), digits);
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
                if (e.text.size() == 1 && e.text != "'")
                    return "'" + e.text + "'";
                if (e.text.size() == 1)
                    return "\"'\"";
                if (sameDialect && e.text.find('"') == std::string::npos && e.text.find('\\') == std::string::npos)
                    return "\"" + e.text + "\"";   // a sjasmplus source: its own constant, as written
                return std::to_string(e.value);   // several characters: the value ALASM / TASM compute (a 16-bit word)
            default: return std::to_string(e.value);
        }
    }

    /// An operand of a unary operator: binary expressions get parentheses (a nested unary only when its sign would
    /// touch the outer one: "- -x")
    std::string Atom(const Expr& e, char outer = '\0')
    {
        const std::string s = Print(e);
        bool simple = e.kind != Expr::Kind::Binary;
        if (e.kind == Expr::Kind::Unary && !s.empty() && (s[0] == outer || (outer == '!' && s[0] == '=')))
            simple = false;
        return simple ? s : "(" + s + ")";
    }

    static Expr Masked16(Expr e)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(Expr::Binary(Op::And, std::move(e), Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4)));
        return g;
    }

    static Expr Grouped(Expr e)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(std::move(e));
        return g;
    }

    std::string Print(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return Number(e);
            case Expr::Kind::Symbol: return Name(e.text);
            case Expr::Kind::Current: return "$";
            case Expr::Kind::CurrentPage: return "$$";
            case Expr::Kind::CurrentPhysical: return "$$$";
            case Expr::Kind::Group: return "(" + Print(e.args[0]) + ")";
            case Expr::Kind::Memory: return "{" + Print(e.args[0]) + "}";
            case Expr::Kind::Raw: return e.text;
            case Expr::Kind::Defined:
                // ALASM ?label: 0 when defined, #FFFF when not (sjasmplus IFDEF sees DEFINEs only, `exist` sees labels)
                return Print(Grouped(Expr::Binary(Op::Mul, Grouped(Expr::Unary(Op::LogicalNot, Expr::Unary(Op::Exists, e.args[0]))),
                                                  Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4))));
            case Expr::Kind::Unary:
                switch (e.op)
                {
                    case Op::Negate: return "-" + Atom(e.args[0], '-');
                    case Op::Plus: return "+" + Atom(e.args[0], '+');
                    case Op::Not: return "~" + Atom(e.args[0], '~');
                    case Op::LogicalNot:
                        if (trueValue == 1)
                            return "-" + TrueIsOne(e);
                        return "!" + Atom(e.args[0], '!');
                    case Op::High: return "high " + Atom(e.args[0]);
                    case Op::Low: return "low " + Atom(e.args[0]);
                    case Op::Exists: return "exist " + Atom(e.args[0]);
                    case Op::SwapBytes:
                    {
                        // ((x&#FF)<<8)|((x&#FFFF)>>8)
                        const Expr low = Expr::Binary(Op::And, e.args[0], Expr::Number(0xFF, ir::NumberSpelling::Hex, 2));
                        const Expr high = Expr::Binary(Op::And, e.args[0], Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4));
                        return Print(Grouped(Expr::Binary(Op::Or, Expr::Binary(Op::Shl, low, Expr::Number(8)), Expr::Binary(Op::Shr, high, Expr::Number(8)))));
                    }
                    default: return Atom(e.args[0]);
                }
            case Expr::Kind::Binary:
            {
                if (e.op == Op::RotateLeft16 || e.op == Op::RotateRight16)
                {
                    // ALASM's cyclic shifts of a 16-bit word, spelled out: ((a&#FFFF)<<b|(a&#FFFF)>>>(16-b))&#FFFF
                    const bool right = e.op == Op::RotateRight16;
                    const Expr a = Masked16(e.args[0]);
                    const Expr b = e.args[1].kind == Expr::Kind::Binary ? Grouped(e.args[1]) : e.args[1];
                    Expr back = Grouped(Expr::Binary(Op::Sub, Expr::Number(16), b));
                    Expr rotated = Expr::Binary(Op::Or, Expr::Binary(right ? Op::ShrUnsigned : Op::Shl, a, b),
                                                Expr::Binary(right ? Op::Shl : Op::ShrUnsigned, a, std::move(back)));
                    return Print(Masked16(Grouped(std::move(rotated))));
                }
                const bool comparison = e.op == Op::Equal || e.op == Op::NotEqual || e.op == Op::Less || e.op == Op::Greater ||
                                        e.op == Op::LessEqual || e.op == Op::GreaterEqual;
                if (comparison && trueValue == 1)
                    return "-" + TrueIsOne(e);   // sjasmplus' true is -1
                if ((comparison || e.op == Op::Mod || e.op == Op::Shr) && wordBits == 16 && unsignedWords)
                {
                    // 16-bit unsigned words (STORM: 0-1 is #FFFF, so #FFFF>>1 = #7FFF and 0-1>0): the operands masked
                    // (a number or a comparison with 0 needs no mask)
                    auto masked = [](const Expr& a) {
                        const Expr& x = a.kind == Expr::Kind::Group ? a.args[0] : a;
                        return x.kind == Expr::Kind::Binary && x.op == Op::And && x.args[1].kind == Expr::Kind::Number && x.args[1].value == 0xFFFF;
                    };
                    auto mask = [&](const Expr& a) { return a.kind == Expr::Kind::Number || masked(a) ? a : Masked16(a); };
                    const bool zeroTest = comparison && ((e.args[1].kind == Expr::Kind::Number && e.args[1].value == 0) ||
                                                         (e.args[0].kind == Expr::Kind::Number && e.args[0].value == 0));
                    if (!zeroTest)
                        return Joined(e.op, mask(e.args[0]), e.op == Op::Shr ? e.args[1] : mask(e.args[1]));
                }
                if (e.op == Op::Div && wordBits == 16 && unsignedWords)
                    return Joined(Op::Div, Masked16(e.args[0]), Masked16(e.args[1]));   // ALASM: unsigned 16-bit division
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
        return "?";
    }

    /// a op b with the operands printed under the same rules (an operation nested in a masked operand keeps its own
    /// 16-bit treatment), only this operator written as it is
    std::string Joined(Op op, const Expr& a, const Expr& b)
    {
        const int p = Priority(op);
        std::string left = Print(a);
        std::string right = Print(b);
        if (a.kind == Expr::Kind::Binary && Priority(a.op) < p)
            left = "(" + left + ")";
        if (b.kind == Expr::Kind::Binary && Priority(b.op) <= p)
            right = "(" + right + ")";
        return left + Symbol(op) + right;
    }

    /// A comparison or logical not of a source whose true is 1, printed as sjasmplus' (true -1) in parentheses
    std::string TrueIsOne(const Expr& e)
    {
        // The operands keep the source's convention (1<2<3 is (1<2)<3 = 1), only this operator is sjasmplus'
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
        return "(" + out + ")";
    }

    static std::string Quote(const std::string& text, bool& ok)
    {
        ok = true;
        if (text.find('\'') == std::string::npos)
            return "'" + text + "'";
        if (text.find('"') == std::string::npos && text.find('\\') == std::string::npos)
            return "\"" + text + "\"";
        ok = false;
        return {};
    }

    std::string Operand_(const Operand& o)
    {
        switch (o.kind)
        {
            case Operand::Kind::Register: return o.text == "af'" ? "AF'" : z80::Upper(o.text);
            case Operand::Kind::Condition: return z80::Upper(o.text);
            case Operand::Kind::Indirect: return "(" + z80::Upper(o.text) + ")";
            case Operand::Kind::Indexed:
            {
                const std::string d = Print(o.expr);
                return "(" + z80::Upper(o.text) + (d.empty() || d[0] == '+' || d[0] == '-' ? d : "+" + d) + ")";
            }
            case Operand::Kind::Memory: return "(" + Print(o.expr) + ")";
            case Operand::Kind::Immediate:
            {
                // sjasmplus reads an instruction operand wholly in parentheses as memory: a value written that way
                // gets a unary plus (+(..) stays a value)
                const std::string v = Print(o.expr);
                return instructionOperand && WhollyParenthesized(v) ? "+" + v : v;
            }
            case Operand::Kind::String:
            {
                bool ok = true;
                const std::string q = Quote(o.text, ok);
                if (ok)
                    return q;
                std::string bytes;
                for (const char c : o.text)
                    bytes += (bytes.empty() ? "" : ",") + std::to_string(static_cast<unsigned char>(c));
                return bytes;
            }
        }
        return {};
    }

    std::string Operands(const std::vector<Operand>& operands, bool instruction = false)
    {
        instructionOperand = instruction;
        std::string out;
        for (const Operand& o : operands)
            out += (out.empty() ? "" : ",") + Operand_(o);
        instructionOperand = false;
        return out;
    }

    static bool WhollyParenthesized(const std::string& v)
    {
        if (v.size() < 2 || v.front() != '(' || v.back() != ')')
            return false;
        int depth = 0;
        for (size_t k = 0; k < v.size(); ++k)
        {
            if (v[k] == '(')
                ++depth;
            else if (v[k] == ')' && --depth == 0 && k + 1 != v.size())
                return false;
        }
        return true;
    }

    std::string NotConverted(const std::string& what)
    {
        diagnostics.push_back({Severity::Warning, line, 0, "not converted: " + what});
        return "@; unreal-asm: not converted: " + what;   // from column 0 (the "@" mark)
    }

    /// The DEFINE that says a label tested with IFUSED is defined so far
    std::string DefinedFlag(const std::string& label) const
    {
        std::string flag = "__UNREALASM_DEF_" + Name(label);
        for (char& c : flag)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                c = '_';
        return flag;
    }

    /// DS count,pattern... of STORM: count bytes, the pattern repeated and cut (DS 7,1,2 = DB 1,2,1,2,1,2,1)
    std::vector<std::string> CyclicFill(const Statement& s)
    {
        const size_t k = s.operands.size();
        const std::vector<Operand> all = s.operands;
        if (s.args[0].kind == Expr::Kind::Number)
        {
            const int64_t count = s.args[0].value;
            std::vector<std::string> out;
            if (count / static_cast<int64_t>(k) > 0)
                out = {"DUP " + std::to_string(count / static_cast<int64_t>(k)), "DB " + Operands(all), "EDUP"};
            const size_t rest = static_cast<size_t>(count % static_cast<int64_t>(k));
            if (rest > 0)
                out.push_back("DB " + Operands(std::vector<Operand>(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(rest))));
            if (out.empty())
                out.push_back("; DS 0");
            return out;
        }
        // A count known only when assembling: whole patterns, then the first bytes of one more
        const std::string count = "(" + Print(s.args[0]) + ")";
        std::vector<std::string> out = {"DUP " + count + "/" + std::to_string(k), "DB " + Operands(all), "EDUP"};
        for (size_t r = 1; r < k; ++r)
        {
            out.push_back("IF " + count + "%" + std::to_string(k) + ">=" + std::to_string(r));
            out.push_back("DB " + Operand_(all[r - 1]));
            out.push_back("ENDIF");
        }
        return out;
    }

    /// One statement; may produce several output lines (DUP fills) and needs the label for EQU / =
    std::vector<std::string> StatementText(const Statement& s, const std::string& label)
    {
        switch (s.kind)
        {
            case Statement::Kind::Raw: return {NotConverted("unparsed line: " + s.text)};
            case Statement::Kind::MacroCall:
            {
                bool colon = false;   // sjasmplus would split the line there
                for (const std::string& p : s.params)
                    colon = colon || (p.find(':') != std::string::npos && p.find_first_of("\"'") == std::string::npos);
                if (colon)
                {
                    std::string text = s.mnemonic;
                    for (size_t k = 0; k < s.params.size(); ++k)
                        text += (k ? "," : " ") + s.params[k];
                    return {NotConverted("not a macro name: " + text)};
                }
                // ALASM lets a call give fewer arguments than the macro uses; sjasmplus needs them all (empty is fine)
                std::vector<std::string> params = s.params;
                if (s.operands.size() == s.params.size())
                    for (size_t k = 0; k < params.size(); ++k)
                        params[k] = Operand_(s.operands[k]);
                const auto declared = macroParams.find(s.mnemonic);
                if (declared != macroParams.end())
                    while (static_cast<int>(params.size()) < declared->second)
                        params.emplace_back();
                std::string args;
                for (size_t k = 0; k < params.size(); ++k)
                    args += (k ? "," : "") + params[k];
                return {MacroName(s.mnemonic) + (args.empty() ? "" : " " + args)};
            }
            case Statement::Kind::Instruction:
            {
                const std::string ops = Operands(s.operands, true);
                return {z80::Upper(s.mnemonic) + (ops.empty() ? "" : " " + ops)};
            }
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
                    diagnostics.push_back({Severity::Warning, line, 0, "ORG with a page: the source's page numbers follow its own memory layout; check the page for sjasmplus' DEVICE"});
                return {"ORG " + args()};
            case ir::DirectiveKind::Equ:
                if (redefinable && redefinable->count(label))
                    return {"= " + args()};   // reassigned elsewhere with "=": sjasmplus keeps EQU fixed
                return {"EQU " + args()};
            case ir::DirectiveKind::Defl: return {"= " + args()};
            case ir::DirectiveKind::Db: return {"DB " + Operands(s.operands)};
            case ir::DirectiveKind::Dw: return {"DW " + Operands(s.operands)};
            case ir::DirectiveKind::Ds:
                if (sameDialect)
                    return {"DS " + args()};   // sjasmplus' own DS (its fill is one value: DS 4,#AA,#55 is 4 bytes)
                // A fill sequence given as operands (strings too, TASM) or as arguments (ALASM): DUP when longer than a byte
                if (s.operands.size() > 1 && !s.params.empty() && s.params[0] == "cyclic")
                    return CyclicFill(s);
                if (!s.operands.empty())
                {
                    if (s.operands.size() == 1 && s.operands[0].kind == Operand::Kind::Immediate)
                        return {"DS " + Print(s.args[0]) + "," + Operands(s.operands)};
                    return {"DUP " + Print(s.args[0]), "DB " + Operands(s.operands), "EDUP"};
                }
                if (s.args.size() <= 2)
                    return {"DS " + args()};
                return {"DUP " + Print(s.args[0]), "DB " + args(1), "EDUP"};
            case ir::DirectiveKind::Include:
            {
                std::string name = s.text;
                const size_t colon = name.find(':');
                if (colon != std::string::npos)
                    name = name.substr(colon + 1);
                if (name.find_first_of("*?") != std::string::npos || name.empty())
                    diagnostics.push_back({Severity::Warning, line, 0, "INCLUDE with a wildcard: name the converted file"});
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
                const std::string incbin = "INCBIN \"" + name + "\"" + (s.args.empty() ? "" : "," + args());
                if (s.params.empty() || s.params[0] != "sector-slack")
                    return {incbin};
                // The rest of the last sector ("<file>.slack") is written next, then the address goes back
                const std::string slack = "INCBIN \"" + name + ".slack\"";
                // __UNREALASM_INCBIN_P is the physical address in both branches: whoever writes the .slack file may limit
                // its length to the end of memory (zxasm convert does)
                if (!displacementFlag)
                    return {incbin, "@__UNREALASM_INCBIN_D=$", "@__UNREALASM_INCBIN_P=$", slack, "ORG __UNREALASM_INCBIN_D"};
                return {incbin, "IFDEF " + std::string(kDisplacementFlag), "@__UNREALASM_INCBIN_D=$", "@__UNREALASM_INCBIN_P=$$$", slack, "ENT",
                        "ORG __UNREALASM_INCBIN_P", "DISP __UNREALASM_INCBIN_D", "ELSE", "@__UNREALASM_INCBIN_D=$", "@__UNREALASM_INCBIN_P=$", slack,
                        "ORG __UNREALASM_INCBIN_D", "ENDIF"};
            }
            case ir::DirectiveKind::If:
            {
                const Expr& c = s.args[0];
                // ALASM ?label: 0 when defined -> IFN ?x = IF !exist x, IF0 ?x = IF exist x
                if (c.kind == Expr::Kind::Defined && c.args[0].kind == Expr::Kind::Symbol)
                    return {"IF !exist " + Name(c.args[0].text)};
                if (c.kind == Expr::Kind::Binary && c.op == Op::Equal && c.args[0].kind == Expr::Kind::Group &&
                    c.args[0].args[0].kind == Expr::Kind::Defined && c.args[0].args[0].args[0].kind == Expr::Kind::Symbol &&
                    c.args[1].kind == Expr::Kind::Number && c.args[1].value == 0)
                    return {"IF exist " + Name(c.args[0].args[0].args[0].text)};
                return {"IF " + Print(c)};
            }
            case ir::DirectiveKind::Else: return {"ELSE"};
            case ir::DirectiveKind::EndIf: return {"ENDIF"};
            case ir::DirectiveKind::EndMacro: return {"ENDM"};
            case ir::DirectiveKind::Repeat: return {"DUP " + args()};
            case ir::DirectiveKind::EndRepeat: return {"EDUP"};
            case ir::DirectiveKind::While: return {"WHILE " + args()};
            case ir::DirectiveKind::EndWhile: return {"ENDW"};
            case ir::DirectiveKind::RepeatUntil:
            {
                const int n = ++repeatCounter;
                repeatStack.push_back(n);
                const std::string var = "__repeat" + std::to_string(n);
                return {"@" + var + "=1", "WHILE " + var};   // the "@" marks a line written from column 0
            }
            case ir::DirectiveKind::UntilZero:
            {
                const int n = repeatStack.empty() ? 0 : repeatStack.back();
                if (!repeatStack.empty())
                    repeatStack.pop_back();
                const std::string var = "__repeat" + std::to_string(n);
                return {"@" + var + "=(" + args() + ")!=0", "ENDW"};
            }
            case ir::DirectiveKind::Disp:
                if (displacementFlag)
                    return {"DISP " + args(), "DEFINE " + std::string(kDisplacementFlag)};
                return {"DISP " + args()};
            case ir::DirectiveKind::Ent:
                if (s.text == "if-displaced")
                    return {"IFDEF " + std::string(kDisplacementFlag), "ENT", "UNDEFINE " + std::string(kDisplacementFlag), "ENDIF"};
                if (displacementFlag)
                    return {"ENT", "UNDEFINE " + std::string(kDisplacementFlag)};
                return {"ENT"};
            case ir::DirectiveKind::Display:
                // a bare DISPLAY prints an empty line (ALASM); sjasmplus needs something to print
                return {"DISPLAY " + (s.operands.empty() ? std::string("' '") : Operands(s.operands))};
            case ir::DirectiveKind::End: return {"END"};
            case ir::DirectiveKind::IfUsed:
            {
                // ZX-ASM's IFUSED X: X used and not defined so far (a library routine the program defines itself, or
                // takes from a label file, stays out). sjasmplus' IFUSED only asks "used"; "defined so far" is the
                // DEFINE every definition of X sets. The answer goes to a redefinable label, so one name serves every block
                const bool negated = !s.params.empty() && s.params[0] == "not";
                return {"@__UNREALASM_IFU=0", "@        IFUSED " + Name(s.text), "@        IFNDEF " + DefinedFlag(s.text), "@__UNREALASM_IFU=1",
                        "@        ENDIF", "@        ENDIF", std::string(negated ? "IF !__UNREALASM_IFU" : "IF __UNREALASM_IFU")};
            }
            case ir::DirectiveKind::SaveBinary: return {"SAVEBIN \"" + s.text + "\"," + args()};
            case ir::DirectiveKind::Main: return {"@; ALASM MAIN \"" + s.text + "\" (the project's main source)"};
            case ir::DirectiveKind::Run: return {NotConverted("RUN " + args() + " (code called while assembling)")};
            case ir::DirectiveKind::Other:
                if (sameDialect)
                    return {s.text};
                return {NotConverted(s.text.empty() ? "a directive" : s.text)};
            default: return {NotConverted(s.text.empty() ? "a directive" : s.text)};
        }
        (void)label;
    }
};

}  // namespace

BackendResult SjasmplusBackend::Write(const ir::Program& program, const BackendOptions& options) const
{
    BackendResult result;
    result.document.dialect = "sjasmplus";
    result.document.codePage = encoding::CodePage::Cp866;   // strings are program bytes: keep the Spectrum code page
    Writer w{options, result.diagnostics, 0, {}, {}, false, false, program.expressionBits, program.unsignedArithmetic, options.macroParams, 0, {},
             program.dialect == "sjasmplus", program.displacementAcrossFiles};
    w.trueValue = program.trueValue;
    w.ifUsedNames = options.ifUsedNames;
    for (const ir::Line& l : program.lines)
        for (const Statement& s : l.statements)
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::IfUsed)
            {
                w.ifUsedNames.insert(s.text);
                result.ifUsedNames.insert(s.text);
            }

    // Macro calls may pass more arguments than the body uses (ALASM ignores the rest, sjasmplus refuses them): a macro
    // declares as many parameters as its longest call
    std::map<std::string, int> callArguments;
    // Names assigned with "=" somewhere: an EQU or an address label of the same name must be redefinable too
    std::set<std::string> redefinable;
    for (const ir::Line& l : program.lines)
        for (const Statement& s : l.statements)
        {
            if (s.kind == Statement::Kind::MacroCall)
                callArguments[s.mnemonic] = std::max(callArguments[s.mnemonic], static_cast<int>(s.params.size()));
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Defl && !l.label.empty())
                redefinable.insert(l.label);
        }
    w.redefinable = &redefinable;

    // Global labels sjasmplus would read as operators or reject
    std::set<std::string> used;
    for (const ir::Line& l : program.lines)
        if (!l.label.empty())
            used.insert(l.label);
    for (const std::string& name : used)
        if (kReserved.count(z80::Lower(name)) || !IsValidLabel(name))
        {
            std::string renamed = SafeName(name);
            while (used.count(renamed))
                renamed += "_";
            w.renames[name] = renamed;
            result.diagnostics.push_back({Severity::Info, 0, 0, "label " + name + " renamed to " + renamed});
        }

    // LOCAL blocks: the labels defined inside (not @labels) get a block suffix, unless used outside the block
    // (ALASM: a label defined in a block and used outside it is global)
    std::vector<std::map<std::string, std::string>> blockRenames;
    {
        struct Block
        {
            size_t first = 0, last = 0;
            std::set<std::string> defined;
        };
        std::vector<Block> blocks;
        std::vector<size_t> open;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            for (const Statement& s : l.statements)
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::LocalBlock)
                {
                    open.push_back(blocks.size());
                    blocks.push_back({k, program.lines.size() - 1, {}});
                }
            if (!open.empty() && !l.label.empty() && !l.labelGlobal)
                blocks[open.back()].defined.insert(l.label);
            for (const Statement& s : l.statements)
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndLocalBlock && !open.empty())
                {
                    blocks[open.back()].last = k;
                    open.pop_back();
                }
        }
        // symbols referenced on each line
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
        blockRenames.resize(blocks.size());
        // A reference counts as outside only when no block defining the same name holds it (a macro expanded twice
        // gives two blocks with the same label names)
        auto inDefiningBlock = [&](size_t k, const std::string& name) {
            for (const Block& other : blocks)
                if (k >= other.first && k <= other.last && other.defined.count(name))
                    return true;
            return false;
        };
        for (size_t b = 0; b < blocks.size(); ++b)
            for (const std::string& name : blocks[b].defined)
            {
                bool outside = false;
                for (size_t k = 0; k < program.lines.size() && !outside; ++k)
                    if ((k < blocks[b].first || k > blocks[b].last) && references[k].count(name) && !inDefiningBlock(k, name))
                        outside = true;
                if (!outside)
                    blockRenames[b][name] = name + "__L" + std::to_string(b + 1);
            }
    }

    // Pages in ORG and memory reads {..} need a sjasmplus device; the largest one keeps every ALASM page number valid
    {
        bool device = false, hasDevice = false;
        std::function<bool(const Expr&)> memory = [&](const Expr& e) {
            if (e.kind == Expr::Kind::Memory)
                return true;
            for (const Expr& a : e.args)
                if (memory(a))
                    return true;
            return false;
        };
        for (const ir::Line& l : program.lines)
            for (const Statement& s : l.statements)
            {
                if (s.kind == Statement::Kind::Directive &&
                    ((s.directive == ir::DirectiveKind::Org && s.args.size() > 1) || s.directive == ir::DirectiveKind::SaveBinary))
                    device = true;
                if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Other && z80::Upper(s.text).rfind("DEVICE", 0) == 0)
                    hasDevice = true;
                for (const Expr& a : s.args)
                    device = device || memory(a);
                for (const Operand& o : s.operands)
                    device = device || memory(o.expr);
            }
        if (device && !hasDevice)
        {
            result.document.lines.push_back({"        DEVICE ZXSPECTRUM4096   ; unreal-asm: ORG pages, {memory} reads and SAVEBIN need a device", {}});
            result.diagnostics.push_back({Severity::Info, 0, 0, "DEVICE ZXSPECTRUM4096 added for ORG pages / memory reads / SAVEBIN"});
        }
    }

    std::vector<size_t> blockOrder;   // block index by opening order
    size_t nextBlock = 0;
    for (const ir::Line& l : program.lines)
    {
        w.line = l.sourceLine;
        std::vector<std::string> texts;
        bool openBlock = false, closeBlock = false, openMacro = false, closeMacro = false;
        std::string macroHeader;
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
            }
            else if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndMacro)
                closeMacro = true;
        }
        if (openMacro)
            w.inMacro = true;   // a LOCAL block opened on the macro's own line belongs to the macro (TASM's DEFMAC)
        if (openBlock)
        {
            std::map<std::string, std::string> scope = nextBlock < blockRenames.size() ? blockRenames[nextBlock] : std::map<std::string, std::string>{};
            if (w.inMacro)
                for (auto& [name, renamed] : scope)
                    renamed = "." + name;   // inside a macro: sjasmplus' local labels, unique for every expansion
            w.localScopes.push_back(std::move(scope));
            ++nextBlock;
        }
        std::string label = l.label.empty() ? std::string() : w.Name(l.label);
        if (w.sameDialect && !l.label.empty() && l.label.find_first_not_of("0123456789") == std::string::npos)
            label = l.label;   // a sjasmplus temporary label (1, referred to as 1B / 1F)
        if (!l.label.empty())
        {
            bool local = false;
            for (const auto& scope : w.localScopes)
                local = local || scope.count(l.label);
            result.labels.push_back({l.sourceLine, l.label, label, local, w.inMacro});
        }
        if (!label.empty() && redefinable.count(l.label))
        {
            bool defines = false;
            for (const Statement& s : l.statements)
                defines = defines || (s.kind == Statement::Kind::Directive && (s.directive == ir::DirectiveKind::Equ || s.directive == ir::DirectiveKind::Defl));
            if (!defines)
            {
                // An address label of a name reassigned with "=" elsewhere: written as name=$ (redefinable)
                result.document.lines.push_back({label + "=$", {}});
                label.clear();
            }
        }
        std::vector<std::string> namedParams;
        for (const Statement& s : l.statements)
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
                namedParams = s.params;
        if (openMacro && !namedParams.empty())
        {
            // Named parameters (a sjasmplus source): written as they are
            std::string params;
            for (size_t k = 0; k < namedParams.size(); ++k)
                params += (k ? "," : "") + namedParams[k];
            texts.push_back("MACRO " + MacroName(macroHeader) + " " + params);
            w.macroParams[macroHeader] = static_cast<int>(namedParams.size());
            result.macroParams[macroHeader] = static_cast<int>(namedParams.size());
            w.inMacro = true;
        }
        else if (openMacro)
        {
            // Parameters: as many as the body uses (\0..\9 -> _arg0.._arg9)
            int highest = -1;
            for (size_t k = static_cast<size_t>(&l - program.lines.data()) + 1; k < program.lines.size(); ++k)
            {
                bool end = false;
                std::function<void(const Expr&)> scan = [&](const Expr& e) {
                    if (e.kind == Expr::Kind::Symbol && e.text.size() == 2 && e.text[0] == '\\')
                        highest = std::max(highest, e.text[1] - '0');
                    for (const Expr& a : e.args)
                        scan(a);
                };
                const std::string& bodyLabel = program.lines[k].label;
                for (size_t c = 0; c + 1 < bodyLabel.size(); ++c)
                    if (bodyLabel[c] == '\\' && std::isdigit(static_cast<unsigned char>(bodyLabel[c + 1])))
                    {
                        highest = std::max(highest, bodyLabel[c + 1] - '0');
                        result.diagnostics.push_back({Severity::Warning, program.lines[k].sourceLine, 0,
                                                      "a macro parameter glued to a name (" + bodyLabel + "): sjasmplus has no such concatenation"});
                    }
                for (const Statement& s : program.lines[k].statements)
                {
                    if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::EndMacro)
                        end = true;
                    for (const Expr& a : s.args)
                        scan(a);
                    for (const Operand& o : s.operands)
                        scan(o.expr);
                    for (const std::string& p : s.params)
                        for (size_t c = 0; c + 1 < p.size(); ++c)
                            if (p[c] == '\\' && std::isdigit(static_cast<unsigned char>(p[c + 1])))
                                highest = std::max(highest, p[c + 1] - '0');
                }
                if (end)
                    break;
            }
            const auto calls = callArguments.find(macroHeader);
            if (calls != callArguments.end())
                highest = std::max(highest, calls->second - 1);
            std::string params;
            for (int k = 0; k <= highest; ++k)
                params += (k ? "," : "") + std::string("_arg") + std::to_string(k);
            texts.push_back("MACRO " + MacroName(macroHeader) + (params.empty() ? "" : " " + params));
            w.macroParams[macroHeader] = highest + 1;
            result.macroParams[macroHeader] = highest + 1;
            w.inMacro = true;
        }
        for (const Statement& s : l.statements)
        {
            if (s.kind == Statement::Kind::Directive &&
                (s.directive == ir::DirectiveKind::LocalBlock || s.directive == ir::DirectiveKind::EndLocalBlock || s.directive == ir::DirectiveKind::Macro))
                continue;
            for (std::string t : w.StatementText(s, label))
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
        if (closeMacro)
            w.inMacro = false;
        if (closeBlock && !w.localScopes.empty())
            w.localScopes.pop_back();

        // Lay out: label in column 0, statements from column 8, " : " between them; lines starting with "@" (WHILE
        // counters) and "= expr" assignments need the label column themselves
        std::string out;
        if (texts.empty())
            out = label;
        for (size_t k = 0; k < texts.size(); ++k)
        {
            std::string t = texts[k];
            if (t.rfind("= ", 0) == 0)
            {
                out = label + "=" + t.substr(2);
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
            if (k == 0 || out.empty())
            {
                if (!out.empty())
                    result.document.lines.push_back({out, {}});
                out = (k == 0 ? label : std::string());
                out.append(out.size() < 8 ? 8 - out.size() : 1, ' ');
                out += t;
            }
            else
            {
                result.document.lines.push_back({out, {}});
                out = std::string(8, ' ') + t;
            }
        }
        if (l.hasComment)
        {
            if (!out.empty())
                out += (out.size() < 32 ? std::string(32 - out.size(), ' ') : std::string(" "));
            out += ";" + l.comment;
        }
        result.document.lines.push_back({out, {}});
        if (!l.label.empty() && w.ifUsedNames.count(l.label))
            result.document.lines.push_back({"        DEFINE " + w.DefinedFlag(l.label), {}});
    }
    (void)blockOrder;
    return result;
}
}  // namespace unrealasm::dialects
