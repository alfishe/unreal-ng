#include "dialects/alasm/alasmfrontend.h"

#include <cctype>
#include <functional>
#include <map>
#include <set>

#include "dialects/common/macros.h"
#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

bool IsLabelChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '@';
}

struct Failure
{
    std::string reason;
};

/// The characters of a quoted text starting at t[start] (a quote): "" inside is one quote (ALASM's own sources write
/// CP """); the text may run to the end of the line. `end` is the offset after the closing quote, or t.size()
std::string Quoted(std::string_view t, size_t start, size_t& end)
{
    std::string chars;
    size_t j = start + 1;
    while (j < t.size())
    {
        if (t[j] == '"')
        {
            if (j + 1 < t.size() && t[j + 1] == '"')
            {
                chars.push_back('"');
                j += 2;
                continue;
            }
            end = j + 1;
            return chars;
        }
        chars.push_back(t[j++]);
    }
    end = t.size();
    return chars;
}

/// Expressions: left to right without priorities (ALASM help §5)
struct ExpressionParser
{
    std::string_view t;
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && t[i] == ' ')
            ++i;
    }
    char Peek() const { return i < t.size() ? t[i] : '\0'; }

    Expr Term()
    {
        Blanks();
        const char c = Peek();
        if (c == '-' || c == '+')
        {
            ++i;
            return Expr::Unary(c == '-' ? Op::Negate : Op::Plus, Term());
        }
        if (c == '\'')
        {
            ++i;
            return Expr::Unary(Op::High, Term());
        }
        if (c == '.')
        {
            ++i;
            return Expr::Unary(Op::Low, Term());
        }
        if (c == '?')
        {
            ++i;
            Expr e = Expr::Make(Expr::Kind::Defined);
            e.args.push_back(Term());
            return e;
        }
        if (c == '(')
        {
            ++i;
            Expr inner = Sequence();
            Blanks();
            if (Peek() != ')')
                throw Failure{"( without )"};
            ++i;
            Expr e = Expr::Make(Expr::Kind::Group);
            e.args.push_back(std::move(inner));
            return e;
        }
        if (c == '{')
        {
            ++i;
            Expr inner = Sequence();
            Blanks();
            if (Peek() != '}')
                throw Failure{"{ without }"};
            ++i;
            Expr e = Expr::Make(Expr::Kind::Memory);
            e.args.push_back(std::move(inner));
            return e;
        }
        if (c == '#')
        {
            size_t j = ++i;
            while (j < t.size() && std::isxdigit(static_cast<unsigned char>(t[j])))
                ++j;
            if (j == i)
                throw Failure{"# without hex digits"};
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, 16), ir::NumberSpelling::Hex, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (c == '%')
        {
            size_t j = ++i;
            while (j < t.size() && (t[j] == '0' || t[j] == '1'))
                ++j;
            if (j == i)
                throw Failure{"% without binary digits"};
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, 2), ir::NumberSpelling::Binary, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t j = i;
            while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j])))
                ++j;
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i))), ir::NumberSpelling::Decimal, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (c == '"')
        {
            // A character constant; ALASM lets it run to the end of the line ("CP ":"). Its value is a 16-bit word
            // (the last two characters)
            size_t end = 0;
            const std::string chars = Quoted(t, i, end);
            int64_t value = 0;
            for (const char ch : chars)
                value = ((value << 8) | static_cast<unsigned char>(ch)) & 0xFFFF;
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            i = end;
            return e;
        }
        if (c == '$')
        {
            ++i;
            if (Peek() == '$')
            {
                ++i;
                return Expr::Make(Expr::Kind::CurrentPage);
            }
            return Expr::Make(Expr::Kind::Current);
        }
        if (c == '\\')
        {
            // A macro parameter: \0..\9 become symbols the backends map; \C \N \S \P \R have no counterpart
            if (i + 1 < t.size() && std::isdigit(static_cast<unsigned char>(t[i + 1])))
            {
                Expr e = Expr::Symbol(std::string(t.substr(i, 2)));
                i += 2;
                return e;
            }
            throw Failure{"macro parameter operator \\" + std::string(1, i + 1 < t.size() ? t[i + 1] : ' ') + " has no counterpart"};
        }
        if (IsLabelChar(c))
        {
            size_t j = i;
            while (j < t.size() && IsLabelChar(t[j]))
                ++j;
            Expr e = Expr::Symbol(std::string(t.substr(i, j - i)));
            i = j;
            return e;
        }
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    static bool BinaryOp(char c, Op& op)
    {
        switch (c)
        {
            case '+': op = Op::Add; return true;
            case '-': op = Op::Sub; return true;
            case '*': op = Op::Mul; return true;
            case '/': op = Op::Div; return true;
            case '&': op = Op::And; return true;
            case '|': op = Op::Or; return true;
            case '!': op = Op::Xor; return true;
            case '>': op = Op::RotateRight16; return true;
            case '<': op = Op::RotateLeft16; return true;
            default: return false;
        }
    }

    Expr Sequence()
    {
        Expr acc = Term();
        while (true)
        {
            Blanks();
            const char c = Peek();
            Op op;
            if (c == '~')
            {
                ++i;
                acc = Expr::Unary(Op::Not, std::move(acc));   // inverts the result so far
                continue;
            }
            if (!BinaryOp(c, op))
                return acc;
            ++i;
            acc = Expr::Binary(op, std::move(acc), Term());
        }
    }

    Expr Whole()
    {
        Expr e = Sequence();
        Blanks();
        if (i != t.size())
            throw Failure{"unexpected text after an expression: " + std::string(t.substr(i))};
        return e;
    }
};

Expr ParseExpression(std::string_view text)
{
    ExpressionParser p{text};
    return p.Whole();
}

/// Splits operands at commas outside quotes, parentheses and braces; a quote may run to the end of the line
std::vector<std::string> SplitOperands(std::string_view text)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool quote = false;
    for (const char c : text)
    {
        if (quote)
        {
            cur.push_back(c);
            if (c == '"')
                quote = false;
            continue;
        }
        if (c == '"')
            quote = true;
        else if (c == '(' || c == '{')
            ++depth;
        else if ((c == ')' || c == '}') && depth > 0)
            --depth;
        else if (c == ',' && depth == 0)
        {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!cur.empty() || !out.empty())
        out.push_back(cur);
    for (std::string& s : out)
    {
        while (!s.empty() && s.front() == ' ')
            s.erase(s.begin());
        while (!s.empty() && s.back() == ' ')
            s.pop_back();
    }
    return out;
}

Operand ParseOperand(const std::string& text, bool conditionAllowed)
{
    Operand o;
    const std::string lower = z80::Lower(text);
    if (conditionAllowed && z80::IsCondition(lower) && text == z80::Upper(text))
    {
        o.kind = Operand::Kind::Condition;
        o.text = lower;
        return o;
    }
    const std::string reg = z80::NormalizeRegister(lower);
    if (text == z80::Upper(text) && z80::IsRegister(reg) && reg != "f")
    {
        o.kind = Operand::Kind::Register;
        o.text = reg;
        return o;
    }
    if (text.size() >= 3 && text.front() == '(' && text.back() == ')')
    {
        const std::string inner = text.substr(1, text.size() - 2);
        const std::string innerLower = z80::Lower(inner);
        if (inner == z80::Upper(inner) && (innerLower == "hl" || innerLower == "bc" || innerLower == "de" || innerLower == "sp" ||
                                           innerLower == "c" || innerLower == "ix" || innerLower == "iy"))
        {
            o.kind = Operand::Kind::Indirect;
            o.text = innerLower;
            return o;
        }
        if ((inner.rfind("IX", 0) == 0 || inner.rfind("IY", 0) == 0) && inner.size() > 2 && (inner[2] == '+' || inner[2] == '-'))
        {
            o.kind = Operand::Kind::Indexed;
            o.text = z80::Lower(inner.substr(0, 2));
            o.expr = ParseExpression(inner.substr(2));   // the sign is part of the displacement expression
            return o;
        }
        // A whole operand in parentheses is a memory reference only when the parentheses enclose all of it
        int depth = 0;
        bool whole = true;
        for (size_t k = 0; k < text.size(); ++k)
        {
            if (text[k] == '(')
                ++depth;
            else if (text[k] == ')' && --depth == 0 && k + 1 != text.size())
                whole = false;
        }
        if (whole)
        {
            o.kind = Operand::Kind::Memory;
            o.expr = ParseExpression(inner);
            return o;
        }
    }
    o.kind = Operand::Kind::Immediate;
    o.expr = ParseExpression(text);
    return o;
}

Operand StringOperand(const std::string& text)
{
    Operand o;
    o.kind = Operand::Kind::String;
    size_t end = 0;
    o.text = Quoted(text, 0, end);
    return o;
}

struct Directive
{
    std::string_view name;
    ir::DirectiveKind kind;
};

constexpr Directive kDirectives[] = {
    {"ORG", ir::DirectiveKind::Org},       {"EQU", ir::DirectiveKind::Equ},         {"DB", ir::DirectiveKind::Db},
    {"DEFB", ir::DirectiveKind::Db},       {"DEFM", ir::DirectiveKind::Db},         {"DW", ir::DirectiveKind::Dw},
    {"DEFW", ir::DirectiveKind::Dw},       {"DS", ir::DirectiveKind::Ds},           {"DEFS", ir::DirectiveKind::Ds},
    {"DD", ir::DirectiveKind::Db},         {"INCLUDE", ir::DirectiveKind::Include}, {"INCBIN", ir::DirectiveKind::Incbin},
    {"IF0", ir::DirectiveKind::If},        {"IF", ir::DirectiveKind::If},           {"IFN", ir::DirectiveKind::If},
    {"ELSE", ir::DirectiveKind::Else},     {"ENDIF", ir::DirectiveKind::EndIf},     {"MACRO", ir::DirectiveKind::Macro},
    {"ENDM", ir::DirectiveKind::EndMacro}, {"DUP", ir::DirectiveKind::Repeat},      {"EDUP", ir::DirectiveKind::EndRepeat},
    {"REPEAT", ir::DirectiveKind::RepeatUntil}, {"UNTIL0", ir::DirectiveKind::UntilZero}, {"UNTIL", ir::DirectiveKind::UntilZero},
    {"DISP", ir::DirectiveKind::Disp},     {"ENT", ir::DirectiveKind::Ent},         {"LOCAL", ir::DirectiveKind::LocalBlock},
    {"ENDL", ir::DirectiveKind::EndLocalBlock}, {"DISPLAY", ir::DirectiveKind::Display}, {"MAIN", ir::DirectiveKind::Main},
    {"RUN", ir::DirectiveKind::Run},       {"ERASE", ir::DirectiveKind::Other},     {"STOP", ir::DirectiveKind::Other},
};

std::string FileName(const std::string& operand)
{
    std::string name = operand;
    if (!name.empty() && name.front() == '"')
        name = name.substr(1, name.find('"', 1) == std::string::npos ? std::string::npos : name.find('"', 1) - 1);
    while (!name.empty() && name.back() == ' ')   // TR-DOS names are blank padded: "PARTS "
        name.pop_back();
    return name;
}

bool IsKeyword(const std::string& word)
{
    for (const Directive& d : kDirectives)
        if (word == d.name)
            return true;
    if (word == "EXA" || word == "EXD" || word == "JZ" || word == "JNZ" || word == "JC" || word == "JNC" || word == "INF")
        return true;
    return word == z80::Upper(word) && z80::IsMnemonic(z80::Lower(word));
}

Statement ParseStatement(const std::string& word, const std::string& rest, Diagnostics& diagnostics, uint32_t line)
{
    Statement s;
    const std::vector<std::string> ops = SplitOperands(rest);
    for (const Directive& d : kDirectives)
    {
        if (word != d.name)
            continue;
        s.kind = Statement::Kind::Directive;
        s.directive = d.kind;
        s.text = word;
        switch (d.kind)
        {
            case ir::DirectiveKind::Db:
                // DD with a quoted text: the code older ALASM versions show as DEFM (the codec spells it as 5.07 does)
                if (word == "DD" && !ops.empty() && !ops[0].empty() && ops[0].front() == '"')
                {
                    for (const std::string& op : ops)
                        s.operands.push_back(!op.empty() && op.front() == '"' ? StringOperand(op) : ParseOperand(op, false));
                    break;
                }
                if (word == "DD")
                {
                    // DD [#]hexbytes[,hexbytes]: pairs of hex digits
                    for (std::string op : ops)
                    {
                        if (!op.empty() && op.front() == '#')
                            op.erase(op.begin());
                        for (size_t k = 0; k + 1 < op.size() + 1; k += 2)
                        {
                            const std::string pair = op.substr(k, 2);
                            Operand o;
                            o.kind = Operand::Kind::Immediate;
                            o.expr = Expr::Number(std::stoll(pair, nullptr, 16), ir::NumberSpelling::Hex, 2);
                            s.operands.push_back(o);
                        }
                    }
                    break;
                }
                [[fallthrough]];
            case ir::DirectiveKind::Dw:
            case ir::DirectiveKind::Display:
                for (const std::string& op : ops)
                {
                    if (d.kind == ir::DirectiveKind::Display && op.size() == 2 && op[0] == '/')
                    {
                        Operand key;   // a format key (/D /H /A /L /T): it applies to what follows
                        key.kind = Operand::Kind::Immediate;
                        key.expr = Expr::Make(Expr::Kind::Raw);
                        key.expr.text = op;
                        s.operands.push_back(key);
                        continue;
                    }
                    // A string (ALASM lets the last one run to the end of the line); in DW only a 1-2 character constant
                    size_t close = 0;
                    if (!op.empty() && op.front() == '"')
                        Quoted(op, 0, close);
                    if (!op.empty() && op.front() == '"' && close == op.size() && (d.kind != ir::DirectiveKind::Dw || op.size() > 4) &&
                        !(op.size() == 4 && op == "\"\"\"\""))
                        s.operands.push_back(StringOperand(op));
                    else
                        s.operands.push_back(ParseOperand(op, false));
                }
                break;
            case ir::DirectiveKind::Include:
            case ir::DirectiveKind::Incbin:
            case ir::DirectiveKind::Main:
                s.text = ops.empty() ? std::string() : FileName(ops[0]);
                if (d.kind == ir::DirectiveKind::Incbin && ops.size() > 1)
                    s.args.push_back(Expr::Number(0));   // ALASM gives the size only; the IR has [offset, length]
                for (size_t k = 1; k < ops.size(); ++k)
                    s.args.push_back(ParseExpression(ops[k]));
                break;
            case ir::DirectiveKind::Macro:
                s.text = ops.empty() ? std::string() : ops[0];
                break;
            case ir::DirectiveKind::If:
            {
                Expr cond = ops.empty() ? Expr::Number(0) : ParseExpression(rest);
                // IF0 (5.03 and later) and IF (the same code #D3 before 5.03; ALASM 4.2's help: "if the expression = 0,
                // body 1 is compiled") take the block when the expression is 0
                if (word == "IF0" || word == "IF")
                {
                    Expr group = Expr::Make(Expr::Kind::Group);
                    group.args.push_back(std::move(cond));
                    cond = Expr::Binary(Op::Equal, std::move(group), Expr::Number(0));
                }
                s.args.push_back(std::move(cond));
                break;
            }
            case ir::DirectiveKind::Other:
                s.text = word + (rest.empty() ? "" : " " + rest);
                diagnostics.push_back({Severity::Warning, line, 0, "ALASM 3.8 directive " + word + " kept as text"});
                break;
            default:
                for (const std::string& op : ops)
                    s.args.push_back(ParseExpression(op));
        }
        return s;
    }

    // Pseudo-instructions
    std::string mnemonic = z80::Lower(word);
    std::string operands = rest;
    if (word == "EXA")
        mnemonic = "ex", operands = "AF,AF'";
    else if (word == "EXD")
        mnemonic = "ex", operands = "DE,HL";
    else if (word == "JZ" || word == "JNZ" || word == "JC" || word == "JNC")
        mnemonic = "jr", operands = word.substr(1) + "," + rest;
    else if (word == "INF")
        mnemonic = "in", operands = "F,(C)";
    if (word != z80::Upper(word) || !z80::IsMnemonic(mnemonic))
    {
        // A macro call (ALASM keywords are capitals; anything else in the command place is a macro name)
        s.kind = Statement::Kind::MacroCall;
        s.mnemonic = word;
        s.params = ops;
        // Arguments that read as operands are converted like operands ('X is the high byte, not a character)
        for (const std::string& op : ops)
        {
            Operand o;
            try
            {
                o = (!op.empty() && op.front() == '"' && op.size() > 3) ? StringOperand(op) : ParseOperand(op, false);
            }
            catch (...)
            {
                o.kind = Operand::Kind::Immediate;
                o.expr = Expr::Make(Expr::Kind::Raw);
                o.expr.text = op;
            }
            s.operands.push_back(o);
        }
        return s;
    }
    s.kind = Statement::Kind::Instruction;
    s.mnemonic = mnemonic;
    const std::vector<std::string> parts = SplitOperands(operands);
    for (size_t k = 0; k < parts.size(); ++k)
    {
        if (mnemonic == "in" && parts[k] == "F")
        {
            Operand f;
            f.kind = Operand::Kind::Register;
            f.text = "f";
            s.operands.push_back(f);
            continue;
        }
        const bool condition = z80::TakesCondition(mnemonic) && k == 0 && (parts.size() >= 2 || mnemonic == "ret");
        const std::string& part = parts[k];
        if (!part.empty() && part.front() == '(')
        {
            // ALASM reads an operand that starts with "(" as memory up to the matching ")" and ignores the rest
            // (LD DE,(65536-46)*98/256 assembles as LD DE,(#FFD2), seen in ALASM 5.09)
            int depth = 0;
            size_t close = std::string::npos;
            for (size_t c = 0; c < part.size() && close == std::string::npos; ++c)
            {
                if (part[c] == '(')
                    ++depth;
                else if (part[c] == ')' && --depth == 0)
                    close = c;
            }
            if (close != std::string::npos && close + 1 < part.size())
            {
                diagnostics.push_back({Severity::Warning, line, 0,
                                       "ALASM reads " + part.substr(0, close + 1) + " as memory and ignores \"" + part.substr(close + 1) + "\""});
                s.operands.push_back(ParseOperand(part.substr(0, close + 1), condition));
                continue;
            }
        }
        s.operands.push_back(ParseOperand(part, condition));
    }
    return s;
}
/// One source line into IR lines (more than one when a macro call is expanded)
void ParseLine(const std::string& text, uint32_t number, std::set<std::string>& macros, const std::set<std::string>& referenced, FrontendResult& result)
{
    ir::Line line;
    line.sourceLine = number;
    std::string t = text;
    // The comment: from a ; outside a string
    bool quote = false;
    for (size_t k = 0; k < t.size(); ++k)
    {
        if (t[k] == '"')
            quote = !quote;
        else if (t[k] == ';' && !quote)
        {
            line.comment = t.substr(k + 1);
            line.hasComment = true;
            t.erase(k);
            break;
        }
    }
    while (!t.empty() && t.back() == ' ')
        t.pop_back();
    // "+" / "-" in column 0: assembled once (the help; checked in ALASM 5.09: "+       LD A,1" and "+LBL    LD D,4"
    // assemble, LBL being a label; a "-" read from the disk is a "+" again; an indented "+" is a syntax error). A
    // converted source assembles them every time
    if (t.size() > 1 && (t[0] == '+' || t[0] == '-') && !std::isdigit(static_cast<unsigned char>(t[1])))
    {
        if (t[1] == ' ')
            t[0] = ' ';
        else
            t.erase(0, 1);
        result.diagnostics.push_back({Severity::Info, number, 0, "an assemble-once (+/-) line: assembled every time after conversion"});
    }
    size_t i = 0;
    try
    {
        size_t wordEnd = 0;
        while (wordEnd < t.size() && t[wordEnd] != ' ' && t[wordEnd] != '=')
            ++wordEnd;
        // A keyword in column 0 is a command (ALASM stores it as a token), never a label; so is the name of a
        // macro defined above
        const std::string firstWord = t.substr(0, wordEnd);
        // The column does not matter to ALASM: an indented word is a label too when a command follows it, or when the
        // source refers to it (otherwise it is taken for a macro defined in another file)
        bool indentedLabel = false;
        if (!t.empty() && t[0] == ' ')
        {
            const size_t w0 = t.find_first_not_of(' ');
            size_t w1 = w0;
            while (w1 < t.size() && t[w1] != ' ' && t[w1] != '=')
                ++w1;
            const std::string word = t.substr(w0, w1 - w0);
            const size_t n0 = t.find_first_not_of(' ', w1);
            std::string next = n0 == std::string::npos ? std::string() : t.substr(n0, t.find(' ', n0) == std::string::npos ? std::string::npos : t.find(' ', n0) - n0);
            if (!word.empty() && !IsKeyword(word) && !macros.count(word) && word.find('=') == std::string::npos &&
                ((!next.empty() && IsKeyword(next)) || (next.empty() && referenced.count(word))) &&
                (std::isalpha(static_cast<unsigned char>(word[0])) || word[0] == '_' || word[0] == '@'))
            {
                indentedLabel = true;
                t = t.substr(w0);
            }
        }
        const size_t afterFirst = t.find_first_not_of(' ', wordEnd);
        const bool definesName = afterFirst != std::string::npos && (t[afterFirst] == '=' || t.compare(afterFirst, 4, "EQU ") == 0);
        if (!t.empty() && t[0] != ' ' && (indentedLabel || definesName || (!IsKeyword(firstWord) && !macros.count(firstWord))))
        {
            while (i < t.size() && t[i] != ' ' && t[i] != '=')
                ++i;
            line.label = t.substr(0, i);
            if (line.label.size() > 1 && line.label.back() == ':')
                line.label.pop_back();   // DATA: is DATA
            // "@" is part of the name (ALASM help, LOCAL: labels starting with @ are global); sjasmplus reads
            // "@name" as the global label too
            line.labelGlobal = !line.label.empty() && line.label.front() == '@';
            if (i < t.size() && t[i] == '=')
            {
                Statement s;
                s.kind = Statement::Kind::Directive;
                s.directive = ir::DirectiveKind::Defl;
                s.args.push_back(ParseExpression(std::string_view(t).substr(i + 1)));
                line.statements.push_back(std::move(s));
                result.program.lines.push_back(std::move(line));
                return;
            }
        }
        while (i < t.size() && t[i] == ' ')
            ++i;
        if (i < t.size())
        {
            size_t j = i;
            while (j < t.size() && t[j] != ' ')
                ++j;
            const std::string word = t.substr(i, j - i);
            const size_t equals = word.find('=');
            if (equals != std::string::npos && equals > 0 && word.find('"') == std::string::npos)
            {
                // label=expression further right on the line
                if (line.label.empty())
                    line.label = word.substr(0, equals);
                Statement s;
                s.kind = Statement::Kind::Directive;
                s.directive = ir::DirectiveKind::Defl;
                s.args.push_back(ParseExpression(std::string_view(t).substr(i + equals + 1)));
                line.statements.push_back(std::move(s));
                result.program.lines.push_back(std::move(line));
                return;
            }
            while (j < t.size() && t[j] == ' ')
                ++j;
            Statement s = ParseStatement(word, t.substr(j), result.diagnostics, number);
            if (s.kind == Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
                macros.insert(s.text);
            // Several operand groups for one instruction: one statement each; CALL A,B / JP A,B without a condition is
            // one jump per address (sjasmplus reads them so too)
            const bool jumps = s.kind == Statement::Kind::Instruction &&
                               (s.mnemonic == "call" || s.mnemonic == "jp" || s.mnemonic == "jr" || s.mnemonic == "djnz") && s.operands.size() > 1 &&
                               s.operands[0].kind != Operand::Kind::Condition;
            const int arity = jumps ? 1 : s.kind == Statement::Kind::Instruction ? z80::SplitArity(s.mnemonic) : 0;
            if (arity > 0 && static_cast<int>(s.operands.size()) > arity && s.operands.size() % arity == 0 &&
                !(s.mnemonic == "ex" && s.operands.size() == 2))
            {
                for (size_t k = 0; k < s.operands.size(); k += static_cast<size_t>(arity))
                {
                    Statement part = s;
                    part.operands.assign(s.operands.begin() + static_cast<std::ptrdiff_t>(k), s.operands.begin() + static_cast<std::ptrdiff_t>(k + static_cast<size_t>(arity)));
                    line.statements.push_back(std::move(part));
                }
            }
            else
                line.statements.push_back(std::move(s));
        }
    }
    catch (const Failure& f)
    {
        Statement raw;
        raw.kind = Statement::Kind::Raw;
        raw.text = text;
        line.statements.assign(1, raw);
        line.label.clear();
        line.hasComment = false;
        result.diagnostics.push_back({Severity::Warning, number, 0, "not parsed (" + f.reason + "): kept as text"});
    }
    catch (const std::exception&)
    {
        Statement raw;
        raw.kind = Statement::Kind::Raw;
        raw.text = text;
        line.statements.assign(1, raw);
        line.label.clear();
        line.hasComment = false;
        result.diagnostics.push_back({Severity::Warning, number, 0, "not parsed (a number out of range): kept as text"});
    }
    result.program.lines.push_back(std::move(line));
}

}  // namespace

/// ALASM 4.4x writes macro parameters as :0..:9 (its own SAVEOBJ 2.1): inside a macro they become \0..\9, outside
/// strings and comments
std::string ColonParameters(const std::string& line)
{
    std::string out = line;
    bool quote = false;
    for (size_t k = 0; k + 1 < out.size(); ++k)
    {
        if (out[k] == '"')
            quote = !quote;
        else if (!quote && out[k] == ';')
            break;
        else if (!quote && out[k] == ':' && std::isdigit(static_cast<unsigned char>(out[k + 1])))
            out[k] = '\\';
    }
    return out;
}

FrontendResult AlasmFrontend::Parse(const SourceDocument& original) const
{
    SourceDocument source = original;
    {
        bool inMacro = false;
        for (SourceLine& l : source.lines)
        {
            const size_t first = l.text.find_first_not_of(' ');
            const std::string rest = first == std::string::npos ? std::string() : l.text.substr(first);
            if (rest.rfind("MACRO ", 0) == 0)
                inMacro = true;
            else if (rest.rfind("ENDM", 0) == 0)
                inMacro = false;
            else if (inMacro)
                l.text = ColonParameters(l.text);
        }
    }
    FrontendResult result;
    result.program.dialect = "alasm";
    result.program.expressionBits = 16;          // ALASM help §5: 16-bit integers
    result.program.unsignedArithmetic = true;    // (65536-46)*98/256 = #EE in ALASM, not -17
    std::set<std::string> macros;
    // Names the source refers to (words after the first one of a line, outside comments and strings)
    std::set<std::string> referenced;
    for (const SourceLine& l : source.lines)
    {
        std::string t = l.text;
        bool quote = false;
        for (size_t k = 0; k < t.size(); ++k)
        {
            if (t[k] == '"')
                quote = !quote;
            else if (t[k] == ';' && !quote)
            {
                t.erase(k);
                break;
            }
            else if (quote)
                t[k] = ' ';
        }
        const size_t w0 = t.find_first_not_of(' ');
        if (w0 == std::string::npos)
            continue;
        size_t k = t.find(' ', w0);
        while (k != std::string::npos && k < t.size())
        {
            while (k < t.size() && !(std::isalpha(static_cast<unsigned char>(t[k])) || t[k] == '_' || t[k] == '@'))
                ++k;
            size_t e = k;
            while (e < t.size() && (std::isalnum(static_cast<unsigned char>(t[e])) || t[e] == '_' || t[e] == '@'))
                ++e;
            if (e > k)
                referenced.insert(t.substr(k, e - k));
            k = e;
        }
    }

    // Macro bodies (their source lines) and the ones expanded at their calls
    std::map<std::string, std::vector<std::string>> bodies;
    {
        std::string current;
        for (const SourceLine& l : source.lines)
        {
            const std::string& t = l.text;
            const size_t first = t.find_first_not_of(' ');
            const std::string rest = first == std::string::npos ? std::string() : t.substr(first);
            if (rest.rfind("MACRO ", 0) == 0)
            {
                current = rest.substr(6);
                current = current.substr(0, current.find_first_of(" ;"));
                bodies[current].clear();
            }
            else if (rest.rfind("ENDM", 0) == 0 && (rest.size() == 4 || rest[4] == ' ' || rest[4] == ';'))
                current.clear();
            else if (!current.empty())
                bodies[current].push_back(t);
        }
    }
    std::set<std::string> expanded;
    for (const auto& [name, body] : bodies)
        if (NeedsExpansion(body))
        {
            expanded.insert(name);
            result.diagnostics.push_back({Severity::Info, 0, 0, "macro " + name + " glues or walks its parameters: expanded at its calls"});
        }

    std::function<void(const std::string&, uint32_t, int)> emit = [&](const std::string& text, uint32_t number, int depth) {
        const size_t before = result.program.lines.size();
        ParseLine(text, number, macros, referenced, result);
        if (result.program.lines.size() != before + 1 || depth > 16)
            return;
        ir::Line& line = result.program.lines.back();
        if (line.statements.size() != 1 || line.statements[0].kind != Statement::Kind::MacroCall || !expanded.count(line.statements[0].mnemonic))
            return;
        // Replace the call by the body with the arguments put in (a label on the call stays on its own line)
        MacroArguments args;
        for (size_t n = 0; n < line.statements[0].params.size(); ++n)
            args.text += (n ? "," : "") + line.statements[0].params[n];
        const std::string name = line.statements[0].mnemonic;
        ir::Line call = std::move(line);
        result.program.lines.pop_back();
        ir::Line head;
        head.sourceLine = number;
        head.label = call.label;
        head.comment = " macro " + name + " expanded" + (call.hasComment ? ";" + call.comment : std::string());
        head.hasComment = true;
        result.program.lines.push_back(std::move(head));
        for (const std::string& bodyLine : bodies[name])
            emit(Substitute(bodyLine, args), number, depth + 1);
    };

    uint32_t number = 0;
    std::string skipping;   // inside the definition of an expanded macro: written as comments
    for (const SourceLine& sourceLine : source.lines)
    {
        ++number;
        const std::string& t = sourceLine.text;
        const size_t first = t.find_first_not_of(' ');
        const std::string rest = first == std::string::npos ? std::string() : t.substr(first);
        if (skipping.empty() && rest.rfind("MACRO ", 0) == 0)
        {
            std::string name = rest.substr(6);
            name = name.substr(0, name.find_first_of(" ;"));
            if (expanded.count(name))
                skipping = name;
        }
        if (!skipping.empty())
        {
            ir::Line comment;
            comment.sourceLine = number;
            comment.comment = " (expanded at its calls) " + t;
            comment.hasComment = true;
            result.program.lines.push_back(std::move(comment));
            if (rest.rfind("ENDM", 0) == 0)
                skipping.clear();
            macros.insert(skipping.empty() ? std::string() : skipping);
            continue;
        }
        emit(t, number, 0);
    }
    return result;
}
}  // namespace unrealasm::dialects
