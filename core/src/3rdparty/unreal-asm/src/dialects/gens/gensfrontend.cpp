#include "dialects/gens/gensfrontend.h"

#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <string_view>

#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

struct Failure
{
    std::string reason;
};

constexpr size_t kSignificant = 6;   // GENS keeps the first 6 characters of a name

/// A–z in the manual's sense: the letters and [ \ ] ^ _ (between Z and a in ASCII)
bool IsLabelStart(char c)
{
    return (c >= 'A' && c <= 'z') && c != '`';
}

/// Then 0-9, $ and # too
bool IsLabelChar(char c)
{
    return IsLabelStart(c) || std::isdigit(static_cast<unsigned char>(c)) || c == '$' || c == '#';
}

std::string Trim(std::string_view text)
{
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t'))
        --b;
    return std::string(text.substr(a, b - a));
}

/// The Z80 words GENS knows (Devpac 4.1 manual, appendix 2): in capitals only
const std::set<std::string> kMnemonics = {
    "ADC", "ADD",  "AND",  "BIT",  "CALL", "CCF",  "CP",   "CPD",  "CPDR", "CPI", "CPIR", "CPL",  "DAA",  "DEC",  "DI",   "DJNZ", "EI",
    "EX",  "EXX",  "HALT", "IM",   "IN",   "INC",  "IND",  "INDR", "INI",  "INIR", "JP",  "JR",   "LD",   "LDD",  "LDDR", "LDI",  "LDIR",
    "NEG", "NOP",  "OR",   "OTDR", "OTIR", "OUT",  "OUTD", "OUTI", "POP",  "PUSH", "RES", "RET",  "RETI", "RETN", "RL",   "RLA",  "RLC",
    "RLCA", "RLD", "RR",   "RRA",  "RRC",  "RRCA", "RRD",  "RST",  "SBC",  "SCF",  "SET", "SLA",  "SRA",  "SRL",  "SUB",  "XOR"};
/// Words after which the rest of the line is a comment (no operand)
const std::set<std::string> kNoOperand = {"CCF", "CPD",  "CPDR", "CPI",  "CPIR", "CPL",  "DAA",  "DI",   "EI",   "EXX",  "HALT", "IND",
                                          "INDR", "INI", "INIR", "LDD",  "LDDR", "LDI",  "LDIR", "NEG",  "NOP",  "OTDR", "OTIR", "OUTD",
                                          "OUTI", "RETI", "RETN", "RLA", "RLCA", "RLD",  "RRA",  "RRCA", "RRD",  "SCF",  "ELSE", "END",
                                          "ENDM", "MAC"};
const std::set<std::string> kRegisters = {"A", "B", "C", "D", "E", "H", "L", "I", "R", "AF", "AF'", "BC", "DE", "HL", "IX", "IY", "SP"};
const std::set<std::string> kConditions = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};

std::string ParamName(int n)
{
    return "_g" + std::to_string(n);
}

bool IsParam(const std::string& name)
{
    return name.size() > 2 && name.rfind("_g", 0) == 0 && name.find_first_not_of("0123456789", 2) == std::string::npos;
}

/// Expressions: terms joined by operators, strictly left to right (no priorities); numbers are taken modulo 65536
struct ExpressionParser
{
    std::string_view t;
    bool inMacro = false;
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
            ++i;
    }

    Expr Term()
    {
        Blanks();
        if (i >= t.size())
            throw Failure{"an operator without its term"};
        const char c = t[i];
        if (c == '-' || c == '+')
        {
            ++i;
            Expr inner = Term();
            return c == '-' ? Expr::Unary(Op::Negate, std::move(inner)) : inner;
        }
        if (c == '#' || c == '%' || std::isdigit(static_cast<unsigned char>(c)))
        {
            const int base = c == '#' ? 16 : c == '%' ? 2 : 10;
            if (base != 10)
                ++i;
            const size_t from = i;
            uint64_t value = 0;
            while (i < t.size())
            {
                const char d = t[i];
                const int digit = std::isdigit(static_cast<unsigned char>(d)) ? d - '0' : (d >= 'A' && d <= 'F') ? d - 'A' + 10 : (d >= 'a' && d <= 'f') ? d - 'a' + 10 : 99;
                if (digit >= base)
                    break;
                value = (value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit)) & 0xFFFF;
                ++i;
            }
            if (i == from)
                throw Failure{std::string(1, c) + " without digits"};
            return Expr::Number(static_cast<int64_t>(value), base == 16 ? ir::NumberSpelling::Hex : base == 2 ? ir::NumberSpelling::Binary : ir::NumberSpelling::Decimal,
                                static_cast<int>(i - from));
        }
        if (c == '"')
        {
            // "c": exactly one character between the quotes ("""" is the quote itself)
            if (i + 2 >= t.size() || t[i + 2] != '"')
                throw Failure{"a character constant is one character in quotes"};
            Expr e = Expr::Number(static_cast<unsigned char>(t[i + 1]), ir::NumberSpelling::Character, 1);
            e.text = std::string(1, t[i + 1]);
            i += 3;
            return e;
        }
        if (c == '$')
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (c == '=' && inMacro && i + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 1])))
        {
            ++i;
            int n = 0;
            while (i < t.size() && std::isdigit(static_cast<unsigned char>(t[i])))
                n = n * 10 + (t[i++] - '0');
            if (n > 31)
                throw Failure{"macro parameters are =0 to =31"};
            return Expr::Symbol(ParamName(n));
        }
        if (IsLabelStart(c))
        {
            const size_t from = i;
            while (i < t.size() && IsLabelChar(t[i]))
                ++i;
            return Expr::Symbol(std::string(t.substr(from, i - from)));
        }
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    Expr Whole()
    {
        Expr left = Term();
        while (true)
        {
            Blanks();
            if (i >= t.size())
                return left;
            Op op;
            switch (t[i])
            {
                case '+': op = Op::Add; break;
                case '-': op = Op::Sub; break;
                case '*': op = Op::Mul; break;
                case '/': op = Op::Div; break;
                case '?': op = Op::Mod; break;
                case '&': op = Op::And; break;
                case '@': op = Op::Or; break;
                case '!': op = Op::Xor; break;
                default: throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
            }
            ++i;
            left = Expr::Binary(op, std::move(left), Term());
        }
    }
};

/// Operands split at commas outside character constants and parentheses
std::vector<std::string> SplitOperands(std::string_view text)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (c == '"' && k + 2 < text.size() && text[k + 2] == '"')
        {
            current.append(text.substr(k, 3));
            k += 2;
            continue;
        }
        if (c == '(')
            ++depth;
        else if (c == ')' && depth > 0)
            --depth;
        if (c == ',' && depth == 0)
        {
            out.push_back(Trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    if (!Trim(current).empty() || !out.empty())
        out.push_back(Trim(current));
    return out;
}

/// The end of the operand field: a ';' outside character constants
size_t CommentStart(std::string_view text)
{
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (text[k] == '"' && k + 2 < text.size() && text[k + 2] == '"')
        {
            k += 2;
            continue;
        }
        if (text[k] == ';')
            return k;
    }
    return std::string_view::npos;
}

struct LineParser
{
    Diagnostics& diagnostics;
    bool inMacro = false;
    bool inCondition = false;
    bool orgSeen = false;
    uint32_t number = 0;

    Expr Parse(std::string_view text) const
    {
        ExpressionParser p{text, inMacro};
        return p.Whole();
    }

    Operand InstructionOperand(const std::string& text, bool conditionAllowed) const
    {
        Operand o;
        if (conditionAllowed && kConditions.count(text))
        {
            o.kind = Operand::Kind::Condition;
            o.text = z80::Lower(text);
            return o;
        }
        if (kRegisters.count(text))
        {
            o.kind = Operand::Kind::Register;
            o.text = z80::Lower(text);
            return o;
        }
        if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
        {
            // In parentheses: memory (the manual, 2.5), or a register pair / an index register
            const std::string inner = Trim(std::string_view(text).substr(1, text.size() - 2));
            if (inner == "HL" || inner == "BC" || inner == "DE" || inner == "SP" || inner == "C" || inner == "IX" || inner == "IY")
            {
                o.kind = Operand::Kind::Indirect;
                o.text = z80::Lower(inner);
                return o;
            }
            if ((inner.rfind("IX", 0) == 0 || inner.rfind("IY", 0) == 0) && inner.size() > 2)
            {
                const std::string displacement = Trim(std::string_view(inner).substr(2));
                if (!displacement.empty() && (displacement[0] == '+' || displacement[0] == '-'))
                {
                    o.kind = Operand::Kind::Indexed;
                    o.text = z80::Lower(inner.substr(0, 2));
                    o.expr = Parse(displacement);
                    return o;
                }
            }
            o.kind = Operand::Kind::Memory;
            o.expr = Parse(inner);
            return o;
        }
        o.kind = Operand::Kind::Immediate;
        o.expr = Parse(text);
        return o;
    }

    /// $ in the k-th item of DEFB / DEFW is the address of that item (GENS advances the location counter per item)
    static void ShiftCurrent(Expr& e, int64_t by)
    {
        if (e.kind == Expr::Kind::Current && by != 0)
        {
            e = Expr::Binary(Op::Add, Expr::Make(Expr::Kind::Current), Expr::Number(by));
            return;
        }
        for (Expr& a : e.args)
            ShiftCurrent(a, by);
    }

    void Comment(ir::Line& line, std::string_view text) const
    {
        std::string c = Trim(text);
        if (!c.empty() && c[0] == ';')
            c.erase(0, 1);
        if (!c.empty())
        {
            line.comment = c;
            line.hasComment = true;
        }
    }

    Statement Directive(DirectiveKind kind) const
    {
        Statement s;
        s.kind = Statement::Kind::Directive;
        s.directive = kind;
        return s;
    }

    void ParseLine(const std::string& text, ir::Line& line, const std::set<std::string>& macros)
    {
        if (text.empty())
            return;
        if (text[0] == ';')
        {
            line.comment = text.substr(1);
            line.hasComment = true;
            return;
        }
        if (text[0] == '*')
        {
            // *F name: include a file (a drive "n:" in front names a microdrive); the other commands shape the listing
            if (text.size() > 1 && text[1] == 'F')
            {
                std::string name = Trim(std::string_view(text).substr(2));
                const size_t colon = name.find(':');
                if (colon != std::string::npos && colon <= 1)
                    name = name.substr(colon + 1);
                Statement s = Directive(DirectiveKind::Include);
                s.text = name;
                line.statements.push_back(std::move(s));
                return;
            }
            line.comment = text;
            line.hasComment = true;
            return;
        }
        size_t k = 0;
        if (text[0] != ' ' && text[0] != '\t')
        {
            while (k < text.size() && IsLabelChar(text[k]))
                ++k;
            line.label = text.substr(0, k);
            if (k < text.size() && text[k] == ':')
                ++k;   // checked on GENS4: "LAB1: LD A,1" defines LAB1
            if (line.label.empty() || (k < text.size() && text[k] != ' ' && text[k] != '\t' && text[k] != ';'))
                throw Failure{"a label holds 0-9 $ A-z only and starts with a letter"};
        }
        while (k < text.size() && (text[k] == ' ' || text[k] == '\t'))
            ++k;
        if (k >= text.size() || text[k] == ';')
        {
            Comment(line, std::string_view(text).substr(k));
            return;
        }
        size_t end = k;
        while (end < text.size() && text[end] != ' ' && text[end] != '\t' && text[end] != ';')
            ++end;
        const std::string word = text.substr(k, end - k);
        std::string rest = Trim(std::string_view(text).substr(end));
        std::string upper = z80::Upper(word);
        if (upper != word && (kMnemonics.count(upper) || upper == "ORG" || upper == "EQU" || upper.rfind("DEF", 0) == 0))
            diagnostics.push_back({Severity::Warning, number, 0, "GENS takes " + word + " in capitals only (*ERROR* 02): converted as " + upper});
        if (macros.count(word))
            upper.clear();   // a macro may take any name

        if (kNoOperand.count(upper))
        {
            Comment(line, rest);
            if (upper == "MAC")
            {
                Statement s = Directive(DirectiveKind::Macro);
                s.text = line.label;
                line.label.clear();
                line.statements.push_back(std::move(s));
                inMacro = true;
            }
            else if (upper == "ENDM")
            {
                line.statements.push_back(Directive(DirectiveKind::EndMacro));
                inMacro = false;
            }
            else if (upper == "ELSE")
                line.statements.push_back(Directive(DirectiveKind::Else));
            else if (upper == "END")
            {
                // END turns the assembly on: the end of an IF; without one it does nothing
                if (inCondition)
                    line.statements.push_back(Directive(DirectiveKind::EndIf));
                inCondition = false;
            }
            else
            {
                Statement s;
                s.mnemonic = z80::Lower(upper);
                line.statements.push_back(std::move(s));
            }
            return;
        }
        if (upper == "DEFM")
        {
            // The first character delimits the text; the end of the line ends it too
            Statement s = Directive(DirectiveKind::Db);
            if (!rest.empty())
            {
                const char delimiter = rest[0];
                const size_t close = rest.find(delimiter, 1);
                Operand o;
                o.kind = Operand::Kind::String;
                o.text = rest.substr(1, close == std::string::npos ? std::string::npos : close - 1);
                s.operands.push_back(std::move(o));
                if (close != std::string::npos)
                    Comment(line, std::string_view(rest).substr(close + 1));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        const size_t comment = CommentStart(rest);
        if (comment != std::string::npos)
        {
            Comment(line, std::string_view(rest).substr(comment));
            rest = Trim(std::string_view(rest).substr(0, comment));
        }
        const std::vector<std::string> ops = SplitOperands(rest);
        auto one = [&]() -> const std::string& {
            if (ops.size() != 1)
                throw Failure{word + " takes one operand"};
            return ops[0];
        };
        if (upper == "ORG" || upper == "EQU" || upper == "DEFS" || upper == "IF")
        {
            Statement s = Directive(upper == "ORG" ? DirectiveKind::Org : upper == "EQU" ? DirectiveKind::Equ : upper == "DEFS" ? DirectiveKind::Ds : DirectiveKind::If);
            s.args.push_back(Parse(one()));
            if (upper == "IF")
                inCondition = true;
            if (upper == "ORG")
            {
                if (!orgSeen && s.args[0].kind == Expr::Kind::Current)
                    diagnostics.push_back({Severity::Warning, number, 0,
                                           "ORG $ before any ORG: GENS starts where its text and symbol table end (#8B87 in one GENS4 session), sjasmplus at 0"});
                orgSeen = true;
            }
            line.statements.push_back(std::move(s));
            return;
        }
        if (upper == "ENT")
        {
            // The run address of the editor's R command: no code
            const std::string note = "ENT " + one() + " (GENS' run address)";
            line.comment = line.hasComment ? note + "; " + line.comment : note;
            line.hasComment = true;
            return;
        }
        if (upper == "DEFB" || upper == "DEFW")
        {
            const bool words = upper == "DEFW";
            Statement s = Directive(words ? DirectiveKind::Dw : DirectiveKind::Db);
            for (size_t n = 0; n < ops.size(); ++n)
            {
                Operand o;
                o.kind = Operand::Kind::Immediate;
                o.expr = Parse(ops[n]);
                ShiftCurrent(o.expr, static_cast<int64_t>(n) * (words ? 2 : 1));
                s.operands.push_back(std::move(o));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        if (kMnemonics.count(upper))
        {
            Statement s;
            s.mnemonic = z80::Lower(upper);
            for (size_t n = 0; n < ops.size(); ++n)
            {
                const bool condition = n == 0 && (upper == "JP" || upper == "JR" || upper == "CALL" || upper == "RET") && (ops.size() > 1 || upper == "RET");
                s.operands.push_back(InstructionOperand(ops[n], condition));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        // Any other word in the mnemonic field calls a macro; its arguments are values (checked: 2*=0 with 1+1 is 4),
        // passed as +(expression) so the macro's text keeps them whole
        Statement s;
        s.kind = Statement::Kind::MacroCall;
        s.mnemonic = word;
        for (const std::string& op : ops)
        {
            s.params.push_back(op);
            Operand o;
            o.kind = Operand::Kind::Immediate;
            o.expr = Parse(op);
            if (o.expr.kind == Expr::Kind::Binary || o.expr.kind == Expr::Kind::Unary)
                o.expr = Expr::Unary(Op::Plus, std::move(o.expr));
            s.operands.push_back(std::move(o));
        }
        line.statements.push_back(std::move(s));
    }
};

/// Names whose first 6 characters match are one name to GENS: every spelling becomes the defining one
void Canonical(ir::Program& program, const std::map<std::string, std::string>& spelling)
{
    auto name = [&](const std::string& n) {
        const auto found = spelling.find(n.substr(0, kSignificant));
        return found == spelling.end() ? n : found->second;
    };
    std::function<void(Expr&)> walk = [&](Expr& e) {
        if (e.kind == Expr::Kind::Symbol && !IsParam(e.text))
            e.text = name(e.text);
        for (Expr& a : e.args)
            walk(a);
    };
    for (ir::Line& l : program.lines)
    {
        if (!l.label.empty())
            l.label = name(l.label);
        for (Statement& s : l.statements)
        {
            for (Expr& a : s.args)
                walk(a);
            for (Operand& o : s.operands)
                walk(o.expr);
            if (s.kind == Statement::Kind::MacroCall || (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro))
            {
                std::string& macroName = s.kind == Statement::Kind::MacroCall ? s.mnemonic : s.text;
                macroName = name(macroName);
            }
        }
    }
}

/// The Spectrum's character set puts an up arrow at #5E (the documents hold it as U+2191) where ASCII has '^', a
/// label character for GENS
std::string Ascii(const std::string& text)
{
    std::string out;
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (text.compare(k, 3, "\xE2\x86\x91") == 0)
        {
            out.push_back('^');
            k += 2;
            continue;
        }
        out.push_back(text[k]);
    }
    return out;
}

/// The labels a source defines in column 0 (MAC names included), by their first 6 characters
void CollectDefinitions(const SourceDocument& source, std::map<std::string, std::string>& spelling)
{
    for (const SourceLine& l : source.lines)
    {
        const std::string t = Ascii(l.text);
        if (t.empty() || t[0] == ';' || t[0] == '*' || t[0] == ' ' || t[0] == '\t' || !IsLabelStart(t[0]))
            continue;
        size_t k = 0;
        while (k < t.size() && IsLabelChar(t[k]))
            ++k;
        spelling.emplace(t.substr(0, std::min(k, kSignificant)), t.substr(0, k));
    }
}
}  // namespace

FrontendResult GensFrontend::Parse(const SourceDocument& source) const
{
    return ParseInProject(source, {});
}

FrontendResult GensFrontend::ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const
{
    FrontendResult result;
    result.program.dialect = "gens";
    result.program.expressionBits = 16;          // 16-bit words: 70016 is 4480
    result.program.unsignedArithmetic = false;   // two's complement: #8000/2 is #C000 (checked on GENS4)

    // Macro names first (a call may come before the backend sees the definition only in another file)
    std::set<std::string> macros;
    for (const SourceLine& l : source.lines)
    {
        const std::string t = Ascii(l.text);
        const size_t blank = t.find_first_of(" \t");
        if (blank != std::string::npos && blank > 0 && t[0] != ';' && t[0] != '*' && Trim(std::string_view(t).substr(blank)).rfind("MAC", 0) == 0)
        {
            const std::string after = Trim(std::string_view(t).substr(blank));
            if (after.size() == 3 || after[3] == ' ' || after[3] == '\t' || after[3] == ';')
                macros.insert(t.substr(0, t.find_first_of(" \t:")));
        }
    }

    LineParser p{result.diagnostics};
    uint32_t index = 0;
    for (const SourceLine& sourceLine : source.lines)
    {
        ++index;
        ir::Line line;
        line.sourceLine = index;
        p.number = index;
        try
        {
            p.ParseLine(Ascii(sourceLine.text), line, macros);
        }
        catch (const Failure& f)
        {
            line = ir::Line{};
            line.sourceLine = index;
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = sourceLine.text;
            line.statements.push_back(std::move(raw));
            result.diagnostics.push_back({Severity::Warning, index, 0, "not parsed (" + f.reason + "): " + sourceLine.text});
        }
        result.program.lines.push_back(std::move(line));
    }

    // Macro parameters: =0..=n become _g0.._gn (the highest one the body uses decides how many)
    for (size_t k = 0; k < result.program.lines.size(); ++k)
        for (Statement& s : result.program.lines[k].statements)
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro)
            {
                int highest = -1;
                std::function<void(const Expr&)> scan = [&](const Expr& e) {
                    if (e.kind == Expr::Kind::Symbol && IsParam(e.text))
                        highest = std::max(highest, std::stoi(e.text.substr(2)));
                    for (const Expr& a : e.args)
                        scan(a);
                };
                for (size_t b = k + 1; b < result.program.lines.size(); ++b)
                {
                    bool end = false;
                    for (const Statement& t : result.program.lines[b].statements)
                    {
                        end = end || (t.kind == Statement::Kind::Directive && t.directive == DirectiveKind::EndMacro);
                        for (const Expr& a : t.args)
                            scan(a);
                        for (const Operand& o : t.operands)
                            scan(o.expr);
                    }
                    if (end)
                        break;
                }
                for (int n = 0; n <= highest; ++n)
                    s.params.push_back(ParamName(n));
            }

    std::map<std::string, std::string> spelling;
    CollectDefinitions(source, spelling);
    for (const SourceDocument* other : project)
        if (other)
            CollectDefinitions(*other, spelling);
    Canonical(result.program, spelling);
    return result;
}
}  // namespace unrealasm::dialects
