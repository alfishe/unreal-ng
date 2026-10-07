#include "dialects/xas/xasfrontend.h"

#include <cctype>
#include <map>
#include <set>
#include <stdexcept>

#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::Expr;
using ir::Op;
using ir::Operand;
using ir::Statement;

struct Failure
{
    std::string reason;
};

/// XAS compares labels on their first 7 characters, without case (checked in 4.18 and 7.447)
constexpr size_t kSignificant = 7;

bool IsLabelChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '?' || c == '@';
}

std::string Key(const std::string& name)
{
    return z80::Lower(name.substr(0, kSignificant));
}

/// The end of a string starting at t[i] (a quote) and its characters: at least one character, so """ is the quote
/// itself; a letter or digit right after a closing quote continues the string (XAS 5.05 on: "AB"CD" is AB"CD)
size_t StringEnd(std::string_view t, size_t i, std::string* chars)
{
    size_t j = i + 1;
    if (j < t.size())
    {
        if (chars)
            chars->push_back(t[j]);
        ++j;
    }
    while (j < t.size())
    {
        if (t[j] == '"')
        {
            if (j + 1 < t.size() && std::isalnum(static_cast<unsigned char>(t[j + 1])))
            {
                if (chars)
                    chars->push_back('"');
                ++j;
                continue;
            }
            return j + 1;
        }
        if (chars)
            chars->push_back(t[j]);
        ++j;
    }
    return j;   // to the end of the line
}

struct ExpressionParser
{
    std::string_view t;
    const std::map<std::string, std::string>& names;   // significant key -> the defining spelling
    size_t i = 0;

    void Blanks()
    {
        while (i < t.size() && t[i] == ' ')
            ++i;
    }
    char Peek(size_t ahead = 0) const { return i + ahead < t.size() ? t[i + ahead] : '\0'; }

    Expr Term()
    {
        Blanks();
        const char c = Peek();
        if (c == '#' || c == '%' || (c == '.' && std::isxdigit(static_cast<unsigned char>(Peek(1)))))
        {
            // #FF and .FF are hex (the editor stores '.' as '#'), %101 binary
            const bool hex = c != '%';
            size_t j = ++i;
            while (j < t.size() && (hex ? std::isxdigit(static_cast<unsigned char>(t[j])) != 0 : (t[j] == '0' || t[j] == '1')))
                ++j;
            if (j == i)
                throw Failure{std::string(1, c) + " without digits"};
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i)), nullptr, hex ? 16 : 2) & 0xFFFF,
                                        hex ? ir::NumberSpelling::Hex : ir::NumberSpelling::Binary, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (std::isdigit(static_cast<unsigned char>(c)))
        {
            size_t j = i;
            while (j < t.size() && std::isdigit(static_cast<unsigned char>(t[j])))
                ++j;
            if (j < t.size() && IsLabelChar(t[j]))
                throw Failure{"a number with letters (XAS has no H suffix)"};
            // 16 bits: 65536 is 0 (XAS 7.447)
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i))) & 0xFFFF, ir::NumberSpelling::Decimal, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (c == '"')
        {
            std::string chars;
            i = StringEnd(t, i, &chars);
            int64_t value = 0;
            for (const char ch : chars)
                value = ((value << 8) | static_cast<unsigned char>(ch)) & 0xFFFF;   // "AB" = #4142
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            return e;
        }
        if (c == '$')
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (IsLabelChar(c))
        {
            size_t j = i;
            while (j < t.size() && IsLabelChar(t[j]))
                ++j;
            const std::string name(t.substr(i, j - i));
            i = j;
            const auto found = names.find(Key(name));
            return Expr::Symbol(found != names.end() ? found->second : name);
        }
        if (c == '-' || c == '(')
            throw Failure{c == '-' ? "XAS has no unary minus (write 0-n)" : "XAS has no parentheses in expressions"};
        throw Failure{std::string("unexpected '") + c + "' in an expression"};
    }

    Expr Sequence()
    {
        Expr acc = Term();
        while (true)
        {
            Blanks();
            const char c = Peek();
            const char next = static_cast<char>(std::toupper(static_cast<unsigned char>(Peek(1))));
            // Postfix operators act on the value so far (7.x): &L &H low / high byte, 'L 'R rotate the word by one bit
            if ((c == '&' && (next == 'L' || next == 'H')) || (c == '\'' && (next == 'L' || next == 'R')))
            {
                if (IsLabelChar(Peek(2)))
                    throw Failure{"unexpected text after " + std::string(1, c) + next};
                i += 2;
                if (c == '&')
                    acc = Expr::Unary(next == 'L' ? Op::Low : Op::High, std::move(acc));
                else
                    acc = Expr::Binary(next == 'L' ? Op::RotateLeft16 : Op::RotateRight16, std::move(acc), Expr::Number(1));
                continue;
            }
            Op op;
            switch (c)
            {
                case '+': op = Op::Add; break;
                case '-': op = Op::Sub; break;
                case '*': op = Op::Mul; break;
                case '/': op = Op::Div; break;
                case '!': op = Op::Xor; break;
                default: return acc;
            }
            ++i;
            Expr right = Term();
            if (op == Op::Div && right.kind == Expr::Kind::Number && right.value == 0)
                acc = Expr::Number(0);   // XAS divides by 0 without an error: 0 (7.447); sjasmplus would stop
            else
                acc = Expr::Binary(op, std::move(acc), std::move(right));
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

/// Operands split at commas outside strings and parentheses
std::vector<std::string> SplitOperands(std::string_view text)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (size_t k = 0; k < text.size(); ++k)
    {
        const char c = text[k];
        if (c == '"')
        {
            const size_t end = StringEnd(text, k, nullptr);
            cur += std::string(text.substr(k, end - k));
            k = end - 1;
            continue;
        }
        if (c == '(')
            ++depth;
        else if (c == ')' && depth > 0)
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

enum class Command
{
    None,
    Org,
    Equ,
    Db,
    Dw,
    Dm,
    Ds,
    Work,
    Ent,
    Assm,
    Cont,
    Ltext,
    Lcode,
    Usel,
    Ifnz,
    Ifz,
    Make,
};

Command CommandOf(const std::string& word)
{
    static const std::map<std::string, Command> kCommands = {
        {"ORG", Command::Org},       {"EQU", Command::Equ},         {"DB", Command::Db},        {"DEFB", Command::Db},
        {"DW", Command::Dw},         {"DEFW", Command::Dw},         {"DM", Command::Dm},        {"DEFM", Command::Dm},
        {"DS", Command::Ds},         {"DEFS", Command::Ds},         {"WORK", Command::Work},    {"ENT", Command::Ent},
        {"!ASSM", Command::Assm},    {".ASM", Command::Assm},       {"!CONT", Command::Cont},   {".END", Command::Cont},
        {"LTEXT", Command::Ltext},   {"LOADTEXT", Command::Ltext},  {"LTXT", Command::Ltext},   {"LCODE", Command::Lcode},
        {"LOADCODE", Command::Lcode}, {"LCOD", Command::Lcode},     {"USEL", Command::Usel},    {"IFNZ", Command::Ifnz},
        {"IFZ", Command::Ifz},       {"MAKE", Command::Make},
    };
    const auto found = kCommands.find(z80::Upper(word));
    return found == kCommands.end() ? Command::None : found->second;
}

bool IsKeyword(const std::string& word)
{
    return CommandOf(word) != Command::None || z80::IsMnemonic(z80::Lower(word));
}

std::string FileName(const std::string& operand)
{
    std::string name = operand;
    if (name.size() >= 2 && name.front() == '"')
        name = name.substr(1, name.back() == '"' ? name.size() - 2 : std::string::npos);
    while (!name.empty() && name.back() == ' ')   // TR-DOS names are blank padded
        name.pop_back();
    return name;
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

Statement Directive(ir::DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

struct Parser
{
    const std::map<std::string, std::string>& names;
    Diagnostics& diagnostics;
    uint32_t line = 0;

    Expr Parse(std::string_view text) const
    {
        ExpressionParser p{text, names};
        return p.Whole();
    }

    Operand ParseOperand(const std::string& text, bool conditionAllowed) const
    {
        Operand o;
        const std::string lower = z80::Lower(text);
        if (conditionAllowed && z80::IsCondition(lower))
        {
            o.kind = Operand::Kind::Condition;
            o.text = lower;
            return o;
        }
        const std::string reg = z80::NormalizeRegister(lower);
        if (z80::IsRegister(reg) && reg != "f")
        {
            o.kind = Operand::Kind::Register;
            o.text = reg;
            return o;
        }
        if (text.size() >= 3 && text.front() == '(' && text.back() == ')')
        {
            const std::string inner = text.substr(1, text.size() - 2);
            const std::string innerLower = z80::Lower(inner);
            if (innerLower == "hl" || innerLower == "bc" || innerLower == "de" || innerLower == "sp" || innerLower == "c" || innerLower == "ix" ||
                innerLower == "iy")
            {
                o.kind = Operand::Kind::Indirect;   // (IX) is (IX+0)
                o.text = innerLower;
                return o;
            }
            if ((innerLower.rfind("ix", 0) == 0 || innerLower.rfind("iy", 0) == 0) && inner.size() > 2 && (inner[2] == '+' || inner[2] == '-'))
            {
                o.kind = Operand::Kind::Indexed;
                o.text = innerLower.substr(0, 2);
                // (IX-2): the IR's displacement keeps its sign; XAS has no unary minus, 0-2 says the same
                o.expr = inner[2] == '-' ? Expr::Binary(Op::Sub, Expr::Number(0), Parse(std::string_view(inner).substr(3)))
                                         : Parse(std::string_view(inner).substr(3));
                return o;
            }
            o.kind = Operand::Kind::Memory;
            o.expr = Parse(inner);
            return o;
        }
        o.kind = Operand::Kind::Immediate;
        o.expr = Parse(text);
        return o;
    }

    /// DB / DM items: a string of any length but one is its characters (from 7.432 in DB), the others values
    std::vector<Operand> DataOperands(const std::vector<std::string>& ops) const
    {
        std::vector<Operand> out;
        for (const std::string& op : ops)
        {
            if (!op.empty() && op.front() == '"' && StringEnd(op, 0, nullptr) == op.size())
            {
                std::string chars;
                StringEnd(op, 0, &chars);
                if (chars.size() != 1)
                {
                    Operand o;
                    o.kind = Operand::Kind::String;
                    o.text = chars;
                    out.push_back(std::move(o));
                    continue;
                }
            }
            out.push_back(ParseOperand(op, false));
        }
        return out;
    }
};

/// The label, the command word and the rest of a line (the comment cut off)
struct Fields
{
    std::string label;
    std::string word;
    std::string rest;
    std::string comment;
    bool hasComment = false;
};

Fields Split(const std::string& text)
{
    Fields f;
    std::string t = text;
    for (size_t k = 0; k < t.size(); ++k)
    {
        if (t[k] == '"')
        {
            k = StringEnd(t, k, nullptr) - 1;
            continue;
        }
        if (t[k] == ';')
        {
            f.comment = t.substr(k + 1);
            f.hasComment = true;
            t.erase(k);
            break;
        }
    }
    for (char& c : t)
        if (c == '\t')
            c = ' ';
    while (!t.empty() && t.back() == ' ')
        t.pop_back();
    size_t i = 0;
    if (!t.empty() && t[0] != ' ' && t[0] != '"')
    {
        size_t end = t.find(' ');
        const std::string first = t.substr(0, end);
        if (!IsKeyword(first))
        {
            f.label = first;
            i = end == std::string::npos ? t.size() : end;
        }
    }
    while (i < t.size() && t[i] == ' ')
        ++i;
    if (i < t.size() && t[i] == '"')
    {
        f.word = "DM";   // a string in the command place is DEFM (7.43 help)
        f.rest = t.substr(i);
        return f;
    }
    size_t j = i;
    while (j < t.size() && t[j] != ' ')
        ++j;
    f.word = t.substr(i, j - i);
    while (j < t.size() && t[j] == ' ')
        ++j;
    f.rest = t.substr(j);
    return f;
}

struct LineParser
{
    const std::map<std::string, std::string>& names;
    FrontendResult& result;
    // The one block XAS keeps open: !ASSM n (a repeat), !ASSM !ON (nothing), !ASSM !OFF / IFNZ / IFZ (a condition)
    enum class Block { None, Repeat, Once, Condition } block = Block::None;
    bool displaced = false;   // WORK is active: ORG keeps its offset

    void Warn(uint32_t number, const std::string& message) { result.diagnostics.push_back({Severity::Warning, number, 0, message}); }

    void Open(ir::Line& line, Block kind, Statement s, uint32_t number)
    {
        if (block != Block::None)
        {
            // XAS keeps one block: a second opening is ignored and the next !CONT ends the first (7.447)
            Warn(number, "a block inside a block: XAS ignores it, the next !CONT ends the outer one");
            line.comment = " (ignored by XAS: a block inside a block)" + (line.hasComment ? ";" + line.comment : std::string());
            line.hasComment = true;
            return;
        }
        block = kind;
        if (kind != Block::Once)
            line.statements.push_back(std::move(s));
    }

    void EndWork(ir::Line& line)
    {
        if (displaced)
            line.statements.push_back(Directive(ir::DirectiveKind::Ent));
        displaced = false;
    }

    void ParseLine(const std::string& text, uint32_t number)
    {
        ir::Line line;
        line.sourceLine = number;
        Fields f = Split(text);
        line.comment = f.comment;
        line.hasComment = f.hasComment;
        const Parser p{names, result.diagnostics, number};
        try
        {
            if (!f.label.empty())
            {
                const auto found = names.find(Key(f.label));
                line.label = found != names.end() ? found->second : f.label;
            }
            if (!f.word.empty())
                ParseCommand(line, f, p, number);
        }
        catch (const Failure& e)
        {
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = text;
            line.statements.assign(1, raw);
            line.label.clear();
            line.hasComment = false;
            Warn(number, "not parsed (" + e.reason + "): kept as text");
        }
        catch (const std::exception&)
        {
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = text;
            line.statements.assign(1, raw);
            line.label.clear();
            line.hasComment = false;
            Warn(number, "not parsed (a number out of range): kept as text");
        }
        result.program.lines.push_back(std::move(line));
    }

    void ParseCommand(ir::Line& line, const Fields& f, const Parser& p, uint32_t number)
    {
        const std::vector<std::string> ops = SplitOperands(f.rest);
        switch (CommandOf(f.word))
        {
            case Command::Org:
            {
                if (ops.empty())
                    throw Failure{"ORG without an address"};
                const bool work = displaced;
                EndWork(line);
                Statement org = Directive(ir::DirectiveKind::Org);
                org.args.push_back(p.Parse(ops[0]));
                line.statements.push_back(org);
                if (work)
                {
                    // ORG under WORK moves both addresses: the displacement keeps its offset (7.447)
                    Statement disp = Directive(ir::DirectiveKind::Disp);
                    disp.args.push_back(Expr::Binary(Op::Add, p.Parse(ops[0]), Expr::Symbol("__xas_work")));
                    line.statements.push_back(std::move(disp));
                    displaced = true;
                }
                return;
            }
            case Command::Equ:
            {
                if (ops.empty())
                    throw Failure{"EQU without a value"};
                Statement s = Directive(ir::DirectiveKind::Equ);
                s.args.push_back(p.Parse(f.rest));
                line.statements.push_back(std::move(s));
                return;
            }
            case Command::Db:
            case Command::Dm:
            case Command::Dw:
            {
                Statement s = Directive(CommandOf(f.word) == Command::Dw ? ir::DirectiveKind::Dw : ir::DirectiveKind::Db);
                if (s.directive == ir::DirectiveKind::Db)
                    s.operands = p.DataOperands(ops);
                else
                    for (const std::string& op : ops)
                        s.operands.push_back(p.ParseOperand(op, false));
                // In XAS $ is the address of the item itself (DW 1,$ gives the address of the second word); sjasmplus'
                // $ is the statement's: a list using $ becomes one statement per item
                bool current = false;
                for (const Operand& o : s.operands)
                    current = current || UsesCurrent(o.expr);
                if (current && s.operands.size() > 1)
                {
                    for (const Operand& o : s.operands)
                    {
                        Statement part = s;
                        part.operands.assign(1, o);
                        line.statements.push_back(std::move(part));
                    }
                    return;
                }
                line.statements.push_back(std::move(s));
                return;
            }
            case Command::Ds:
            {
                // DS n fills zeros; DS n,w repeats the word w (low, high) and cuts it at n bytes (7.447)
                if (ops.empty())
                    throw Failure{"DS without a count"};
                Statement s = Directive(ir::DirectiveKind::Ds);
                s.args.push_back(p.Parse(ops[0]));
                if (ops.size() > 1)
                {
                    const Expr w = p.Parse(ops[1]);
                    Operand low, high;
                    low.expr = Expr::Unary(Op::Low, w);
                    high.expr = Expr::Unary(Op::High, w);
                    s.operands = {low, high};
                    s.params = {"cyclic"};
                    if (ops.size() > 2)
                        Warn(number, "DS: XAS uses the first fill word only");
                }
                else
                    s.args.push_back(Expr::Number(0));
                line.statements.push_back(std::move(s));
                return;
            }
            case Command::Work:
            {
                // WORK address: the labels count from it while the code stays where it is (DISP); without an
                // address the displacement ends
                EndWork(line);
                if (ops.empty())
                    return;
                ir::Line offset;
                offset.sourceLine = number;
                offset.label = "__xas_work";
                Statement defl = Directive(ir::DirectiveKind::Defl);
                defl.args.push_back(Expr::Binary(Op::Sub, p.Parse(ops[0]), Expr::Make(Expr::Kind::Current)));
                offset.statements.push_back(std::move(defl));
                if (!line.statements.empty())
                {
                    ir::Line before;
                    before.sourceLine = number;
                    before.statements = std::move(line.statements);
                    line.statements.clear();
                    result.program.lines.push_back(std::move(before));
                }
                result.program.lines.push_back(std::move(offset));
                Statement disp = Directive(ir::DirectiveKind::Disp);
                disp.args.push_back(p.Parse(ops[0]));
                line.statements.push_back(std::move(disp));
                displaced = true;
                return;
            }
            case Command::Ent:
                // The start address for Run: no bytes, nothing to assemble
                line.comment = " ENT (XAS: Run starts here)" + (line.hasComment ? ";" + line.comment : std::string());
                line.hasComment = true;
                return;
            case Command::Assm:
            {
                const std::string arg = z80::Upper(f.rest);
                if (arg == "!ON" || arg == ".ON")
                    Open(line, Block::Once, Statement{}, number);
                else if (arg == "!OFF" || arg == ".OFF")
                {
                    Statement s = Directive(ir::DirectiveKind::If);
                    s.args.push_back(Expr::Number(0));
                    Open(line, Block::Condition, std::move(s), number);
                }
                else
                {
                    Statement s = Directive(ir::DirectiveKind::Repeat);
                    s.args.push_back(p.Parse(f.rest));
                    Open(line, Block::Repeat, std::move(s), number);
                }
                return;
            }
            case Command::Ifnz:
            case Command::Ifz:
            {
                Statement s = Directive(ir::DirectiveKind::If);
                Expr g = Expr::Make(Expr::Kind::Group);
                g.args.push_back(p.Parse(f.rest));
                s.args.push_back(Expr::Binary(CommandOf(f.word) == Command::Ifz ? Op::Equal : Op::NotEqual, std::move(g), Expr::Number(0)));
                Open(line, Block::Condition, std::move(s), number);
                return;
            }
            case Command::Cont:
                if (block == Block::Repeat)
                    line.statements.push_back(Directive(ir::DirectiveKind::EndRepeat));
                else if (block == Block::Condition)
                    line.statements.push_back(Directive(ir::DirectiveKind::EndIf));
                else if (block == Block::None)
                {
                    line.comment = " !CONT (no block open)" + (line.hasComment ? ";" + line.comment : std::string());
                    line.hasComment = true;
                }
                block = Block::None;
                return;
            case Command::Ltext:
            case Command::Lcode:
            {
                Statement s = Directive(CommandOf(f.word) == Command::Ltext ? ir::DirectiveKind::Include : ir::DirectiveKind::Incbin);
                s.text = ops.empty() ? std::string() : FileName(ops[0]);
                line.statements.push_back(std::move(s));
                return;
            }
            case Command::Usel:
            case Command::Make:
            {
                Statement s = Directive(ir::DirectiveKind::Other);
                s.text = z80::Upper(f.word) + (f.rest.empty() ? "" : " " + f.rest);
                Warn(number, "XAS " + z80::Upper(f.word) + " kept as text (not converted)");
                line.statements.push_back(std::move(s));
                return;
            }
            case Command::None: break;
        }

        const std::string mnemonic = z80::Lower(f.word);
        if (!z80::IsMnemonic(mnemonic))
            throw Failure{"unknown command " + f.word};
        std::vector<Operand> operands;
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const bool condition = z80::TakesCondition(mnemonic) && k == 0 && (ops.size() >= 2 || mnemonic == "ret");
            Operand o = p.ParseOperand(ops[k], condition);
            // IN A,PORT / OUT PORT,A: the port without parentheses (7.43 help)
            if (o.kind == Operand::Kind::Immediate && ((mnemonic == "out" && k == 0) || (mnemonic == "in" && k == 1)))
                o.kind = Operand::Kind::Memory;
            operands.push_back(std::move(o));
        }
        // EX AF,AF is EX AF,AF'
        if (mnemonic == "ex" && operands.size() == 2 && operands[0].kind == Operand::Kind::Register && operands[0].text == "af" &&
            operands[1].kind == Operand::Kind::Register && operands[1].text == "af")
            operands[1].text = "af'";
        Statement s;
        s.kind = Statement::Kind::Instruction;
        s.mnemonic = mnemonic;
        // PUSH HL,IX: one instruction per register (5.05 on)
        if ((mnemonic == "push" || mnemonic == "pop") && operands.size() > 1)
        {
            for (const Operand& o : operands)
            {
                Statement part = s;
                part.operands.assign(1, o);
                line.statements.push_back(std::move(part));
            }
            return;
        }
        s.operands = std::move(operands);
        line.statements.push_back(std::move(s));
    }
};
}  // namespace

FrontendResult XasFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "xas";
    result.program.expressionBits = 16;   // unsigned 16-bit words (0-7/2 is #7FFC, 65535+2 is 1)
    result.program.unsignedArithmetic = true;

    // Labels: the first definition's spelling stands for every name with the same first 7 characters
    std::map<std::string, std::string> names;
    for (const SourceLine& l : source.lines)
    {
        const Fields f = Split(l.text);
        if (!f.label.empty())
            names.emplace(Key(f.label), f.label);
    }
    LineParser parser{names, result};
    uint32_t number = 0;
    for (const SourceLine& l : source.lines)
        parser.ParseLine(l.text, ++number);
    if (parser.block == LineParser::Block::Repeat || parser.block == LineParser::Block::Condition)
    {
        // XAS ends the text there; the block is closed so the conversion assembles
        ir::Line end;
        end.sourceLine = number;
        end.statements.push_back(Directive(parser.block == LineParser::Block::Repeat ? ir::DirectiveKind::EndRepeat : ir::DirectiveKind::EndIf));
        end.comment = " (the block ends with the file)";
        end.hasComment = true;
        result.program.lines.push_back(std::move(end));
        result.diagnostics.push_back({Severity::Warning, number, 0, "a block without its !CONT at the end of the file"});
    }
    return result;
}
}  // namespace unrealasm::dialects
