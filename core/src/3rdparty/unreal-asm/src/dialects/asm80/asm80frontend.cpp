#include "dialects/asm80/asm80frontend.h"

#include <algorithm>
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

constexpr size_t kSignificant = 16;   // ASM80 2.01+ keeps the first 16 characters of a name

/// A label starts with A-z in ASCII order (letters and [ \ ] ^ _ `: ASM80's label() takes *pt - 'A' <= 'z' - 'A')
bool IsLabelStart(char c)
{
    return c >= 'A' && c <= 'z';
}

/// Then letters, digits, $ _ # (ASM80's SYMBOL)
bool IsLabelChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '$' || c == '_' || c == '#';
}

std::string Trim(std::string_view text)
{
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r'))
        --b;
    return std::string(text.substr(a, b - a));
}

/// ASM80's command table (com[] of Asm80win.cpp): the Z80 words, INF (IN F,(C)) and SLI
const std::set<std::string> kMnemonics = {
    "ADC",  "ADD",  "AND",  "BIT",  "CALL", "CCF",  "CP",   "CPD",  "CPDR", "CPI",  "CPIR", "CPL",  "DAA",  "DEC",  "DI",   "DJNZ",
    "EI",   "EX",   "EXX",  "HALT", "IM",   "IN",   "INC",  "IND",  "INDR", "INF",  "INI",  "INIR", "JP",   "JR",   "LD",   "LDD",
    "LDDR", "LDI",  "LDIR", "NEG",  "NOP",  "OR",   "OTDR", "OTIR", "OUT",  "OUTD", "OUTI", "POP",  "PUSH", "RES",  "RET",  "RETI",
    "RETN", "RL",   "RLA",  "RLC",  "RLCA", "RLD",  "RR",   "RRA",  "RRC",  "RRCA", "RRD",  "RST",  "SBC",  "SCF",  "SET",  "SLA",
    "SLI",  "SRA",  "SRL",  "SUB",  "XOR"};
const std::set<std::string> kDirectives = {"DEFB", "DEFW", "DEFM", "DEFR", "DEFS", "DISP", "ELSE", "ENDD", "ENDIF",
                                           "ENDM", "ENT",  "EQU",  "IF",   "MAC",  "ORG",  "TIME"};
const std::set<std::string> kRegisters = {"A", "B", "C", "D", "E", "H", "L", "I", "R", "AF", "AF'", "BC", "DE", "HL", "IX", "IY", "SP"};
const std::set<std::string> kConditions = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
/// Names ASM80 warns about as labels ("the label's name is a register's or a flag's"): written as a symbol they are an
/// undefined label, so the expression is 0 (asm80win.exe: "sub hl,de" assembles to SUB 0)
const std::set<std::string> kRegisterNames = {"af", "bc", "de", "hl", "ix", "iy", "sp", "nc", "nz", "pe", "po", "p", "m", "z"};

std::string ParamName(int n)
{
    return "_m" + std::to_string(n);
}

bool IsParam(const std::string& name)
{
    return name.size() == 3 && name.rfind("_m", 0) == 0 && std::isdigit(static_cast<unsigned char>(name[2]));
}

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

/// The low 16 bits of an expression: ASM80 keeps values in 16-bit words where it stores them
Expr Low16(Expr e)
{
    if (e.kind == Expr::Kind::Number || e.kind == Expr::Kind::Symbol || e.kind == Expr::Kind::Current)
        return e;
    return Grouped(Expr::Binary(Op::And, Grouped(std::move(e)), Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4)));
}

/// value() of Asm80win.cpp: an optional sign, then terms joined by operators of equal priority, left to right; the
/// terms are unsigned 16-bit, the accumulator 32-bit; a product is cut to 16 bits (res = (unsigned short)d), so it is
/// masked where a later division or remainder would see the difference
struct ExpressionParser
{
    std::string_view t;
    bool inMacro = false;
    size_t i = 0;
    bool registerName = false;   ///< a register / flag name used as a symbol: the expression is 0

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
            // "c": one character between double quotes (num() of Asm80win.cpp)
            if (i + 2 >= t.size() || t[i + 2] != '"')
                throw Failure{"a character constant is one character in double quotes"};
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
        if (c == '=' && i + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 1])))
        {
            if (!inMacro)
                throw Failure{"=n outside a macro"};
            const int n = t[i + 1] - '0';
            i += 2;
            return Expr::Symbol(ParamName(n));
        }
        if (IsLabelStart(c))
        {
            const size_t from = i;
            ++i;
            while (i < t.size() && IsLabelChar(t[i]))
                ++i;
            std::string name(t.substr(from, i - from));
            if (kRegisterNames.count(z80::Lower(name)))
                registerName = true;
            return Expr::Symbol(std::move(name));
        }
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    Expr Whole()
    {
        Blanks();
        bool negate = false;
        if (i < t.size() && (t[i] == '-' || t[i] == '+'))
        {
            negate = t[i] == '-';
            ++i;
        }
        std::vector<Expr> terms;
        std::vector<Op> ops;
        terms.push_back(Term());
        while (true)
        {
            Blanks();
            if (i >= t.size())
                break;
            Op op;
            switch (t[i])
            {
                case '+': op = Op::Add; break;
                case '-': op = Op::Sub; break;
                case '*': op = Op::Mul; break;
                case '/': op = Op::Div; break;
                case '%': op = Op::Mod; break;
                case '&': op = Op::And; break;
                case '|': op = Op::Or; break;
                case '^': op = Op::Xor; break;
                default: throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
            }
            ++i;
            ops.push_back(op);
            terms.push_back(Term());
        }
        Expr left = negate ? Expr::Unary(Op::Negate, std::move(terms[0])) : std::move(terms[0]);
        for (size_t k = 0; k < ops.size(); ++k)
        {
            left = Expr::Binary(ops[k], std::move(left), std::move(terms[k + 1]));
            if (ops[k] == Op::Mul && std::any_of(ops.begin() + static_cast<std::ptrdiff_t>(k) + 1, ops.end(), [](Op o) { return o == Op::Div || o == Op::Mod; }))
                left = Low16(std::move(left));
        }
        return left;
    }
};

/// Operands split at commas outside character constants, strings and parentheses
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

/// Where the comment starts: a ';' outside character constants and DEFM strings
size_t CommentStart(std::string_view text)
{
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (text[k] == '"')
        {
            const size_t close = text.find('"', k + 1);
            if (close == std::string_view::npos)
                return std::string_view::npos;
            k = close;
            continue;
        }
        if (text[k] == ';')
            return k;
    }
    return std::string_view::npos;
}

/// The name a *F / *B file has in the project: no drive, no directory, in lower case (DOS names); *F without its
/// extension (.a80 / .sym)
std::string FileName(std::string text, bool source)
{
    text = Trim(text);
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        text = text.substr(1, text.size() - 2);
    const size_t slash = text.find_last_of("\\/:");
    if (slash != std::string::npos)
        text = text.substr(slash + 1);
    text = z80::Lower(text);
    if (source)
    {
        const size_t dot = text.rfind('.');
        if (dot != std::string::npos && dot > 0)
        {
            const std::string ext = z80::Upper(text.substr(dot + 1));
            if (ext == "A80" || ext == "SYM")
                text = text.substr(0, dot);
        }
    }
    return text;
}

struct LineParser
{
    Diagnostics& diagnostics;
    bool inMacro = false;
    bool displaced = false;
    int page = -1;   ///< *Pn: the page at #C000 for the ORGs that follow (-1: none named)
    uint32_t number = 0;
    std::string fileTag;   ///< the document's name made a label part: labels the frontend adds are unique per project

    void Warn(const std::string& message) const { diagnostics.push_back({Severity::Warning, number, 0, message}); }

    Expr Parse(std::string_view text) const
    {
        ExpressionParser p{text, inMacro};
        Expr e = p.Whole();
        if (p.registerName)
        {
            Warn("a register name used as a label in '" + std::string(text) + "': undefined, ASM80 takes the expression as 0");
            return Expr::Number(0);
        }
        return e;
    }

    Operand InstructionOperand(const std::string& text, bool conditionAllowed) const
    {
        Operand o;
        const std::string upper = z80::Upper(text);
        if (conditionAllowed && kConditions.count(upper))
        {
            o.kind = Operand::Kind::Condition;
            o.text = z80::Lower(upper);
            return o;
        }
        if (kRegisters.count(upper))
        {
            o.kind = Operand::Kind::Register;
            o.text = z80::Lower(upper);
            return o;
        }
        if (text.size() >= 2 && text.front() == '(' && text.back() == ')')
        {
            const std::string inner = Trim(std::string_view(text).substr(1, text.size() - 2));
            const std::string upperInner = z80::Upper(inner);
            if (upperInner == "HL" || upperInner == "BC" || upperInner == "DE" || upperInner == "SP" || upperInner == "C" || upperInner == "IX" ||
                upperInner == "IY")
            {
                o.kind = Operand::Kind::Indirect;
                o.text = z80::Lower(upperInner);
                return o;
            }
            if ((upperInner.rfind("IX", 0) == 0 || upperInner.rfind("IY", 0) == 0) && inner.size() > 2)
            {
                const std::string displacement = Trim(std::string_view(inner).substr(2));
                if (!displacement.empty() && (displacement[0] == '+' || displacement[0] == '-'))
                {
                    // gIndex(): a sign, then one number or one label
                    o.kind = Operand::Kind::Indexed;
                    o.text = z80::Lower(upperInner.substr(0, 2));
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

    static void Comment(ir::Line& line, std::string_view text)
    {
        std::string c = Trim(text);
        if (!c.empty() && c[0] == ';')
            c.erase(0, 1);
        if (!c.empty())
        {
            line.comment = line.hasComment ? c + "; " + line.comment : c;
            line.hasComment = true;
        }
    }

    static Statement Directive(DirectiveKind kind)
    {
        Statement s;
        s.kind = Statement::Kind::Directive;
        s.directive = kind;
        return s;
    }

    void Key(const std::string& text, ir::Line& line)
    {
        // decode_star(): the letter right after '*'
        const char key = text.size() > 1 ? static_cast<char>(std::toupper(static_cast<unsigned char>(text[1]))) : 0;
        std::string rest = text.size() > 2 ? text.substr(2) : std::string();
        if (key == 'F' || key == 'B')
        {
            if (inMacro)
                throw Failure{"*F / *B inside a macro (ASM80: fatal error 47)"};
            // the name runs to a blank or ';' (*B: or ','); what follows the *F name is not read
            size_t k = 0;
            while (k < rest.size() && (rest[k] == ' ' || rest[k] == '\t'))
                ++k;
            size_t end = k;
            while (end < rest.size() && static_cast<unsigned char>(rest[end]) > ' ' && rest[end] != ';' && !(key == 'B' && rest[end] == ','))
                ++end;
            const std::string name = rest.substr(k, end - k);
            if (name.empty())
                throw Failure{std::string("*") + key + " without a file name"};
            if (key == 'F')
            {
                Statement s = Directive(DirectiveKind::Include);
                s.text = FileName(name, true);
                line.statements.push_back(std::move(s));
                const size_t comment = rest.find(';', end);
                if (comment != std::string::npos)
                    Comment(line, std::string_view(rest).substr(comment));
                return;
            }
            Statement s = Directive(DirectiveKind::Incbin);
            s.text = FileName(name, false);
            std::string tail = rest.substr(end);
            const size_t comment = CommentStart(tail);
            if (comment != std::string::npos)
            {
                Comment(line, std::string_view(tail).substr(comment));
                tail = tail.substr(0, comment);
            }
            tail = Trim(tail);
            if (!tail.empty() && tail[0] == ',')
            {
                const std::vector<std::string> args = SplitOperands(tail.substr(1));
                // *B name,start[,length]: an empty start is 0; without a length ASM80 reads the whole file's length
                // from start on and moves the address by the file's length (the bytes past the end stay as they were)
                const Expr start = args.empty() || args[0].empty() ? Expr::Number(0) : Parse(args[0]);
                s.args.push_back(start);
                if (args.size() > 1 && !args[1].empty())
                    s.args.push_back(Parse(args[1]));
                else if (!(start.kind == Expr::Kind::Number && start.value == 0))
                    Warn("*B with a start and no length: ASM80 moves the address by the whole file's length; sjasmplus by the bytes read");
            }
            line.statements.push_back(std::move(s));
            return;
        }
        if (key == 'P' && rest.size() > 0 && std::isxdigit(static_cast<unsigned char>(rest[0])))
        {
            size_t k = 0;
            int n = 0;
            while (k < rest.size() && std::isxdigit(static_cast<unsigned char>(rest[k])))
                n = n * 16 + (std::isdigit(static_cast<unsigned char>(rest[k])) ? rest[k] - '0' : std::toupper(static_cast<unsigned char>(rest[k])) - 'A' + 10), ++k;
            if (n > 31)
                throw Failure{"*P takes a page 0-1F"};
            page = n;
            line.comment = Trim(text);
            line.hasComment = true;
            return;
        }
        // *L *M *C *D *E *H *S *G *GA (listing, symbols), *$ *Tn *Z80 *O (the output files): no code
        line.comment = Trim(text);
        line.hasComment = true;
    }

    void ParseLine(const std::string& text, ir::Line& line, const std::set<std::string>& macros)
    {
        if (Trim(text).empty())
            return;
        if (text[0] == ';')
        {
            line.comment = text.substr(1);
            line.hasComment = true;
            return;
        }
        if (text[0] == '*')
        {
            Key(text, line);
            return;
        }
        size_t k = 0;
        if (text[0] != ' ' && text[0] != '\t')
        {
            if (!IsLabelStart(text[0]))
                throw Failure{"a label starts with a letter or one of [ \\ ] ^ _ `"};
            ++k;
            while (k < text.size() && IsLabelChar(text[k]))
                ++k;
            line.label = text.substr(0, k);
            if (k < text.size() && text[k] != ' ' && text[k] != '\t' && text[k] != ';')
                throw Failure{"a label holds letters, digits, $ _ # only (no ':')"};
            if (inMacro)
                throw Failure{"a label inside a macro (ASM80: error 36)"};
        }
        while (k < text.size() && (text[k] == ' ' || text[k] == '\t'))
            ++k;
        if (k >= text.size() || text[k] == ';')
        {
            Comment(line, std::string_view(text).substr(k));
            return;
        }
        size_t end = k;
        while (end < text.size() && static_cast<unsigned char>(text[end]) > ' ' && text[end] != ';')
            ++end;
        const std::string word = text.substr(k, end - k);
        std::string rest = text.substr(end);
        const std::string upper = z80::Upper(word);

        if (upper == "DEFM")
        {
            // DEFM "text": the characters up to the next '"'; the rest of the line is not read
            const std::string body = Trim(rest);
            if (body.empty() || body[0] != '"')
                throw Failure{"DEFM takes a text in double quotes"};
            const size_t close = body.find('"', 1);
            if (close == std::string::npos)
                throw Failure{"DEFM without its closing quote"};
            Statement s = Directive(DirectiveKind::Db);
            Operand o;
            o.kind = Operand::Kind::String;
            o.text = body.substr(1, close - 1);
            s.operands.push_back(std::move(o));
            line.statements.push_back(std::move(s));
            const std::string after = Trim(std::string_view(body).substr(close + 1));
            if (!after.empty() && after[0] != ';')
                Warn("text after DEFM's string is not read by ASM80: " + after);
            Comment(line, after.empty() || after[0] != ';' ? std::string_view() : std::string_view(after));
            return;
        }
        const size_t comment = CommentStart(rest);
        if (comment != std::string::npos)
        {
            Comment(line, std::string_view(rest).substr(comment));
            rest = rest.substr(0, comment);
        }
        rest = Trim(rest);
        const std::vector<std::string> ops = SplitOperands(rest);
        auto one = [&]() -> const std::string& {
            if (ops.empty() || ops[0].empty())
                throw Failure{word + " takes an operand"};
            if (ops.size() > 1)
                Warn(word + ": ASM80 reads one operand, the rest is not read: " + rest);
            return ops[0];
        };

        if (kDirectives.count(upper))   // a command wins over a macro of the same name (decode() comes first)
        {
            if (upper == "MAC")
            {
                if (line.label.empty())
                    throw Failure{"MAC without a name in column 0"};
                if (inMacro)
                    throw Failure{"a macro inside a macro"};
                Statement s = Directive(DirectiveKind::Macro);
                s.text = line.label;
                line.label.clear();
                line.statements.push_back(std::move(s));
                inMacro = true;
                return;
            }
            if (upper == "ENDM")
            {
                line.statements.push_back(Directive(DirectiveKind::EndMacro));
                inMacro = false;
                return;
            }
            if (upper == "ELSE" || upper == "ENDIF")
            {
                line.statements.push_back(Directive(upper == "ELSE" ? DirectiveKind::Else : DirectiveKind::EndIf));
                return;
            }
            if (upper == "ENDD")
            {
                line.statements.push_back(Directive(DirectiveKind::Ent));
                displaced = false;
                return;
            }
            if (upper == "IF")
            {
                // IF expr: assembled when expr is 0; IF a=b: when a equals b (16-bit values, _if())
                Statement s = Directive(DirectiveKind::If);
                const size_t eq = rest.find('=');
                const bool param = eq != std::string::npos && eq + 1 < rest.size() && std::isdigit(static_cast<unsigned char>(rest[eq + 1])) && inMacro &&
                                   (eq == 0 || std::string("+-*/%&|^ \t").find(rest[eq - 1]) != std::string::npos);
                if (eq != std::string::npos && !param)
                    s.args.push_back(Expr::Binary(Op::Equal, Low16(Parse(Trim(std::string_view(rest).substr(0, eq)))), Low16(Parse(Trim(std::string_view(rest).substr(eq + 1))))));
                else
                    s.args.push_back(Expr::Binary(Op::Equal, Low16(Parse(rest)), Expr::Number(0)));
                line.statements.push_back(std::move(s));
                return;
            }
            if (upper == "ORG")
            {
                if (displaced)
                    line.statements.push_back(Directive(DirectiveKind::Ent));   // ORG ends DISP (disp=0)
                displaced = false;
                Statement s = Directive(DirectiveKind::Org);
                s.args.push_back(Parse(one()));
                // below #C000 the page does not apply
                if (page >= 0 && !(s.args[0].kind == Expr::Kind::Number && s.args[0].value < 0xC000))
                    s.args.push_back(Expr::Number(page));
                line.statements.push_back(std::move(s));
                return;
            }
            if (upper == "DISP")
            {
                Statement s = Directive(DirectiveKind::Disp);
                s.args.push_back(Parse(one()));
                line.statements.push_back(std::move(s));
                displaced = true;
                return;
            }
            if (upper == "EQU")
            {
                if (line.label.empty())
                    throw Failure{"EQU without a label"};
                Statement s = Directive(DirectiveKind::Equ);
                s.args.push_back(Low16(Parse(one())));   // the label keeps an unsigned 16-bit value
                line.statements.push_back(std::move(s));
                return;
            }
            if (upper == "ENT")
            {
                Comment(line, "ENT " + one() + " (the run address of the output file)");
                return;
            }
            if (upper == "DEFS" || upper == "DEFR" || upper == "TIME")
            {
                Statement s = Directive(DirectiveKind::Ds);
                if (upper == "TIME")
                {
                    Warn("TIME: ASM80 writes 20 characters of the date and time it assembles at; zeros written");
                    s.args.push_back(Expr::Number(20));
                }
                else
                {
                    if (ops.empty() || ops[0].empty())
                        throw Failure{word + " takes a length"};
                    s.args.push_back(Parse(ops[0]));
                    if (upper == "DEFR")
                        Warn("DEFR: ASM80 writes random bytes (rand() seeded with the address); zeros written");
                    else if (ops.size() > 1 && !ops[1].empty())
                        s.args.push_back(Parse(ops[1]));
                }
                line.statements.push_back(std::move(s));
                return;
            }
            if (upper == "DEFB" || upper == "DEFW")
            {
                Statement s = Directive(upper == "DEFW" ? DirectiveKind::Dw : DirectiveKind::Db);
                for (const std::string& op : ops)
                {
                    Operand o;
                    o.kind = Operand::Kind::Immediate;
                    if (op == "?")
                    {
                        Warn(word + " ?: ASM80 writes a random value; 0 written");
                        o.expr = Expr::Number(0);
                    }
                    else
                        o.expr = Parse(op);
                    s.operands.push_back(std::move(o));
                }
                line.statements.push_back(std::move(s));
                return;
            }
        }
        if (kMnemonics.count(upper))
        {
            Statement s;
            if (upper == "INF")
            {
                // IN F,(C): the flags from port C, ED 70
                s.mnemonic = "in";
                Operand o;
                o.kind = Operand::Kind::Indirect;
                o.text = "c";
                s.operands.push_back(std::move(o));
                line.statements.push_back(std::move(s));
                return;
            }
            s.mnemonic = z80::Lower(upper);
            std::vector<std::string> list = ops;
            if ((upper == "AND" || upper == "CP" || upper == "OR" || upper == "SUB" || upper == "XOR") && list.size() > 1)
            {
                Warn(word + " takes one operand; ASM80 does not read the rest: " + rest);
                list.resize(1);
            }
            for (size_t n = 0; n < list.size(); ++n)
            {
                const bool condition = n == 0 && (upper == "JP" || upper == "JR" || upper == "CALL" || upper == "RET") && (list.size() > 1 || upper == "RET");
                Operand o = InstructionOperand(list[n], condition);
                // AND / CP / OR / SUB / XOR take A-L as registers (_t3com); another register's name is read as an
                // undefined label: the operand is 0 (asm80win.exe: "sub hl,de" is SUB 0)
                if ((upper == "AND" || upper == "CP" || upper == "OR" || upper == "SUB" || upper == "XOR") && o.kind == Operand::Kind::Register &&
                    o.text.size() > 1)
                {
                    Warn(word + " " + list[n] + ": ASM80 reads " + list[n] + " as an undefined label, the operand is 0");
                    o.kind = Operand::Kind::Immediate;
                    o.text.clear();
                    o.expr = Expr::Number(0);
                }
                // JR / DJNZ count the offset in 16 bits (short i = target - address - 2): a target past #FFFF wraps
                if ((upper == "JR" || upper == "DJNZ") && o.kind == Operand::Kind::Immediate &&
                    (o.expr.kind == Expr::Kind::Binary || o.expr.kind == Expr::Kind::Unary))
                    o.expr = Expr::Binary(Op::And, Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4), Grouped(std::move(o.expr)));
                if (upper == "EX" && n == 1 && o.kind == Operand::Kind::Register && o.text == "af")
                    o.text = "af'";   // EX AF,AF (any word starting with A after the comma)
                s.operands.push_back(std::move(o));
            }
            line.statements.push_back(std::move(s));
            return;
        }
        // Any other word calls a macro; its arguments are values computed at the call (decode_macro()), passed as
        // +(expression) so the macro's text keeps them whole
        if (!macros.count(word))
            Warn("'" + word + "' is no command and no macro defined so far");
        Statement s;
        s.kind = Statement::Kind::MacroCall;
        s.mnemonic = word;
        // $ in an argument is the address of the call (the value is taken there): the call line's label stands for it
        std::function<bool(const Expr&)> current = [&](const Expr& e) {
            if (e.kind == Expr::Kind::Current)
                return true;
            return std::any_of(e.args.begin(), e.args.end(), current);
        };
        std::function<void(Expr&, const std::string&)> replace = [&](Expr& e, const std::string& name) {
            if (e.kind == Expr::Kind::Current)
                e = Expr::Symbol(name);
            for (Expr& a : e.args)
                replace(a, name);
        };
        for (const std::string& op : ops)
        {
            s.params.push_back(op);
            Operand o;
            o.kind = Operand::Kind::Immediate;
            o.expr = Parse(op);
            if (current(o.expr))
            {
                if (line.label.empty())
                    line.label = "__ASM80_AT_" + fileTag + "_" + std::to_string(number);
                replace(o.expr, line.label);
            }
            if (o.expr.kind == Expr::Kind::Binary || o.expr.kind == Expr::Kind::Unary)
                o.expr = Expr::Unary(Op::Plus, std::move(o.expr));
            s.operands.push_back(std::move(o));
        }
        line.statements.push_back(std::move(s));
    }
};

/// Names whose first 16 characters match are one name to ASM80: every spelling becomes the defining one
void Canonical(ir::Program& program, const std::map<std::string, std::string>& spelling)
{
    auto name = [&](const std::string& n) {
        if (n.size() <= kSignificant)
            return n;
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
            if (s.kind == Statement::Kind::MacroCall)
                s.mnemonic = name(s.mnemonic);
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro)
                s.text = name(s.text);
        }
    }
}

/// The names a source defines in column 0 (MAC names included), by their first 16 characters
void CollectDefinitions(const SourceDocument& source, std::map<std::string, std::string>& spelling, std::set<std::string>* macros)
{
    for (const SourceLine& l : source.lines)
    {
        const std::string& t = l.text;
        if (t.empty() || !IsLabelStart(t[0]))
            continue;
        size_t k = 1;
        while (k < t.size() && IsLabelChar(t[k]))
            ++k;
        const std::string name = t.substr(0, k);
        if (name.size() > kSignificant)
            spelling.emplace(name.substr(0, kSignificant), name);
        if (macros)
        {
            const std::string after = Trim(std::string_view(t).substr(k));
            if (z80::Upper(after.substr(0, 3)) == "MAC" && (after.size() == 3 || after[3] == ' ' || after[3] == '\t' || after[3] == ';'))
                macros->insert(name);
        }
    }
}
}  // namespace

FrontendResult Asm80Frontend::Parse(const SourceDocument& source) const
{
    return ParseInProject(source, {});
}

FrontendResult Asm80Frontend::ParseInProject(const SourceDocument& source, const std::vector<const SourceDocument*>& project) const
{
    FrontendResult result;
    result.program.dialect = "asm80";
    result.program.expressionBits = 0;           // a 32-bit accumulator (long res), like the target
    result.program.unsignedArithmetic = false;   // signed division of the accumulator
    result.program.displacementAcrossFiles = true;   // DISP holds across *F

    std::map<std::string, std::string> spelling;
    std::set<std::string> macros;
    CollectDefinitions(source, spelling, &macros);
    for (const SourceDocument* other : project)
        if (other)
            CollectDefinitions(*other, spelling, &macros);

    LineParser p{result.diagnostics, false, false, -1, 0, {}};
    for (const char c : source.name)
        p.fileTag.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    uint32_t index = 0;
    for (const SourceLine& sourceLine : source.lines)
    {
        ++index;
        ir::Line line;
        line.sourceLine = index;
        p.number = index;
        try
        {
            p.ParseLine(sourceLine.text, line, macros);
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

    // Macro parameters: =0..=9 become _m0.._m9 (the highest one the body uses decides how many)
    for (size_t k = 0; k < result.program.lines.size(); ++k)
        for (Statement& s : result.program.lines[k].statements)
            if (s.kind == Statement::Kind::Directive && s.directive == DirectiveKind::Macro)
            {
                int highest = -1;
                std::function<void(const Expr&)> scan = [&](const Expr& e) {
                    if (e.kind == Expr::Kind::Symbol && IsParam(e.text))
                        highest = std::max(highest, e.text[2] - '0');
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

    Canonical(result.program, spelling);
    return result;
}
}  // namespace unrealasm::dialects
