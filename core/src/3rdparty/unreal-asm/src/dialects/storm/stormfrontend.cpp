#include "dialects/storm/stormfrontend.h"

#include <cctype>
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

struct Failure
{
    std::string reason;
};

bool IsLabelChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

Expr Grouped(Expr e)
{
    Expr g = Expr::Make(Expr::Kind::Group);
    g.args.push_back(std::move(e));
    return g;
}

/// STORM's operator table (help "ВЫРАЖЕНИЯ"): priorities 8 (high) to 2, equal priorities left to right; every unary
/// operator is postfix and acts on what the operators of higher priority before it built
struct OperatorInfo
{
    std::string_view text;
    int priority;
    bool postfix;
};

constexpr OperatorInfo kOperators[] = {
    // two characters first
    {"<<", 5, false}, {">>", 5, false}, {"<=", 2, false}, {">=", 2, false},
    {"*", 7, false},  {"/", 7, false},  {"\\", 7, false}, {"+", 4, false},  {"-", 4, false},  {"&", 3, false},
    {"!", 3, false},  {"|", 3, false},  {"<", 2, false},  {">", 2, false},  {"=", 2, false},
    {"[", 8, true},   {"]", 8, true},   {"^", 8, true},   {"`", 8, true},   {"'", 8, true},   {"~", 6, true},
    {"@", 2, true},
};

/// The IR of one STORM operator applied
Expr Apply(std::string_view op, Expr a, Expr b = {})
{
    if (op == "[")
        return Expr::Unary(Op::High, std::move(a));
    if (op == "]")
        return Expr::Unary(Op::Low, std::move(a));
    if (op == "^")   // round up to a multiple of 256: #AE18 -> #AF00
        return Grouped(Expr::Binary(Op::And, Grouped(Expr::Binary(Op::Add, std::move(a), Expr::Number(0xFF, ir::NumberSpelling::Hex, 2))),
                                    Expr::Number(0xFF00, ir::NumberSpelling::Hex, 4)));
    if (op == "`")   // round down: #AE18 -> #AE00
        return Grouped(Expr::Binary(Op::And, std::move(a), Expr::Number(0xFF00, ir::NumberSpelling::Hex, 4)));
    if (op == "'")   // times 256 in a 16-bit word
        return Grouped(Expr::Binary(Op::And, Expr::Binary(Op::Shl, std::move(a), Expr::Number(8)), Expr::Number(0xFFFF, ir::NumberSpelling::Hex, 4)));
    if (op == "~")
        return Expr::Unary(Op::Negate, std::move(a));
    if (op == "@")
        return Expr::Unary(Op::LogicalNot, std::move(a));
    Op o = Op::Add;
    if (op == "-") o = Op::Sub;
    else if (op == "*") o = Op::Mul;
    else if (op == "/") o = Op::Div;
    else if (op == "\\") o = Op::Mod;
    else if (op == "&") o = Op::And;
    else if (op == "!") o = Op::Or;
    else if (op == "|") o = Op::Xor;
    else if (op == "<<") o = Op::Shl;
    else if (op == ">>") o = Op::Shr;
    else if (op == "<=") o = Op::LessEqual;
    else if (op == ">=") o = Op::GreaterEqual;
    else if (op == "<") o = Op::Less;
    else if (op == ">") o = Op::Greater;
    else if (op == "=") o = Op::Equal;
    return Expr::Binary(o, std::move(a), std::move(b));
}

struct ExpressionParser
{
    std::string_view t;
    size_t i = 0;

    char Peek() const { return i < t.size() ? t[i] : '\0'; }
    void Blanks()
    {
        while (i < t.size() && t[i] == ' ')
            ++i;
    }

    const OperatorInfo* Operator()
    {
        Blanks();
        for (const OperatorInfo& o : kOperators)
            if (t.substr(i, o.text.size()) == o.text)
                return &o;
        return nullptr;
    }

    Expr Term()
    {
        Blanks();
        const char c = Peek();
        if (c == '-' || c == '+')
        {
            // a leading sign belongs to the first number (STORM stores it as a flag of that number)
            ++i;
            Expr inner = Term();
            return c == '-' ? Expr::Unary(Op::Negate, std::move(inner)) : inner;
        }
        if (c == '(')
        {
            ++i;
            Expr inner = Sequence();
            Blanks();
            if (Peek() != ')')
                throw Failure{"( without )"};
            ++i;
            return Grouped(std::move(inner));
        }
        if (c == '#' || c == '%')
        {
            const bool hex = c == '#';
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
            const Expr e = Expr::Number(std::stoll(std::string(t.substr(i, j - i))) & 0xFFFF, ir::NumberSpelling::Decimal, static_cast<int>(j - i));
            i = j;
            return e;
        }
        if (c == '"')
        {
            // "A" a byte, "AB" a word with the first character high
            const size_t close = t.find('"', i + 1);
            if (close == std::string_view::npos)
                throw Failure{"\" without \""};
            const std::string chars(t.substr(i + 1, close - i - 1));
            i = close + 1;
            int64_t value = 0;
            for (const char ch : chars)
                value = ((value << 8) | static_cast<unsigned char>(ch)) & 0xFFFF;
            Expr e = Expr::Number(value, ir::NumberSpelling::Character, static_cast<int>(chars.size()));
            e.text = chars;
            return e;
        }
        if (c == '$')
        {
            ++i;
            return Expr::Make(Expr::Kind::Current);
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
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

    /// Operator precedence by stacks: a binary operator or a postfix one first reduces the operators of the same or
    /// a higher priority on its left
    Expr Sequence()
    {
        std::vector<Expr> values;
        std::vector<const OperatorInfo*> ops;
        auto reduce = [&](int priority) {
            while (!ops.empty() && ops.back()->priority >= priority)
            {
                Expr b = std::move(values.back());
                values.pop_back();
                Expr a = std::move(values.back());
                values.pop_back();
                values.push_back(Apply(ops.back()->text, std::move(a), std::move(b)));
                ops.pop_back();
            }
        };
        values.push_back(Term());
        while (const OperatorInfo* o = Operator())
        {
            i += o->text.size();
            reduce(o->priority);
            if (o->postfix)
            {
                Expr a = std::move(values.back());
                values.back() = Apply(o->text, std::move(a));
                continue;
            }
            ops.push_back(o);
            values.push_back(Term());
        }
        reduce(0);
        return std::move(values.back());
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

std::string Trim(std::string s)
{
    while (!s.empty() && s.front() == ' ')
        s.erase(s.begin());
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

/// Splits at a separator outside quotes and parentheses
std::vector<std::string> Split(std::string_view text, char separator)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    bool quote = false;
    for (const char c : text)
    {
        if (c == '"')
            quote = !quote;
        else if (!quote && c == '(')
            ++depth;
        else if (!quote && c == ')' && depth > 0)
            --depth;
        else if (!quote && depth == 0 && c == separator)
        {
            out.push_back(Trim(cur));
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    if (!Trim(cur).empty() || !out.empty())
        out.push_back(Trim(cur));
    return out;
}

/// The operand wholly in parentheses: "(" and its matching ")" at the end (0+(x) and (x)+6 are values)
bool WhollyParenthesized(const std::string& text)
{
    if (text.size() < 2 || text.front() != '(' || text.back() != ')')
        return false;
    int depth = 0;
    bool quote = false;
    for (size_t k = 0; k < text.size(); ++k)
    {
        if (text[k] == '"')
            quote = !quote;
        else if (!quote && text[k] == '(')
            ++depth;
        else if (!quote && text[k] == ')' && --depth == 0)
            return k + 1 == text.size();
    }
    return false;
}

Operand ParseOperand(const std::string& text, bool conditionAllowed)
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
    if (WhollyParenthesized(text))
    {
        const std::string inner = Trim(text.substr(1, text.size() - 2));
        const std::string innerLower = z80::Lower(inner);
        if (innerLower == "hl" || innerLower == "bc" || innerLower == "de" || innerLower == "sp" || innerLower == "c" || innerLower == "ix" ||
            innerLower == "iy")
        {
            o.kind = Operand::Kind::Indirect;
            o.text = innerLower;
            return o;
        }
        if ((innerLower.rfind("ix", 0) == 0 || innerLower.rfind("iy", 0) == 0) && inner.size() > 2 && (inner[2] == '+' || inner[2] == '-'))
        {
            o.kind = Operand::Kind::Indexed;
            o.text = innerLower.substr(0, 2);
            o.expr = ParseExpression(std::string_view(inner).substr(2));
            return o;
        }
        o.kind = Operand::Kind::Memory;
        o.expr = ParseExpression(inner);
        return o;
    }
    o.kind = Operand::Kind::Immediate;
    o.expr = ParseExpression(text);
    return o;
}

Operand Register(const char* name)
{
    Operand o;
    o.kind = Operand::Kind::Register;
    o.text = name;
    return o;
}

Operand Indirect(const char* name)
{
    Operand o;
    o.kind = Operand::Kind::Indirect;
    o.text = name;
    return o;
}

Statement Instruction(std::string mnemonic, std::vector<Operand> operands)
{
    Statement s;
    s.kind = Statement::Kind::Instruction;
    s.mnemonic = std::move(mnemonic);
    s.operands = std::move(operands);
    return s;
}

/// "@AEDFC825": a text of hex digit pairs is those bytes (help: DB "@AEDFC825" = DB #AE,#DF,#C8,#25)
bool HexText(const std::string& chars, std::vector<Operand>& out)
{
    if (chars.size() < 3 || chars[0] != '@' || (chars.size() - 1) % 2 != 0)
        return false;
    for (size_t k = 1; k < chars.size(); ++k)
        if (!std::isxdigit(static_cast<unsigned char>(chars[k])))
            return false;
    for (size_t k = 1; k < chars.size(); k += 2)
    {
        Operand o;
        o.kind = Operand::Kind::Immediate;
        o.expr = Expr::Number(std::stoll(chars.substr(k, 2), nullptr, 16), ir::NumberSpelling::Hex, 2);
        out.push_back(std::move(o));
    }
    return true;
}

/// A quoted operand standing alone: its characters
bool QuotedText(const std::string& op, std::string& chars)
{
    if (op.size() < 2 || op.front() != '"' || op.back() != '"' || op.find('"', 1) != op.size() - 1)
        return false;
    chars = op.substr(1, op.size() - 2);
    return true;
}

/// DB items: texts (hex texts as their bytes) and byte values; with `bytes` every character is its own item (a DS
/// pattern)
std::vector<Operand> ByteOperands(const std::vector<std::string>& ops, bool bytes, Diagnostics& diagnostics, uint32_t line)
{
    std::vector<Operand> out;
    for (const std::string& op : ops)
    {
        std::string chars;
        if (QuotedText(op, chars))
        {
            if (HexText(chars, out))
                continue;
            if (!chars.empty() && chars[0] == '@')
                diagnostics.push_back({Severity::Warning, line, 0, "\"" + chars + "\" starts with @ but is no text of hex digit pairs: kept as characters"});
            if (bytes || chars.size() == 1)
            {
                // A one-character text is the character (STORM 1.3's help lists it among its bugs; 1.3 builds it right)
                for (const char ch : chars)
                {
                    Operand o;
                    o.kind = Operand::Kind::Immediate;
                    o.expr = Expr::Number(static_cast<unsigned char>(ch), ir::NumberSpelling::Character, 1);
                    o.expr.text = std::string(1, ch);
                    out.push_back(std::move(o));
                }
                continue;
            }
            Operand o;
            o.kind = Operand::Kind::String;
            o.text = chars;
            out.push_back(std::move(o));
            continue;
        }
        Operand o;
        o.kind = Operand::Kind::Immediate;
        o.expr = ParseExpression(op);
        out.push_back(std::move(o));
    }
    return out;
}

bool IsPair(const std::string& reg)
{
    return reg == "bc" || reg == "de" || reg == "hl" || reg == "ix" || reg == "iy";
}

const char* High(const std::string& pair)
{
    return pair == "bc" ? "b" : pair == "de" ? "d" : pair == "hl" ? "h" : pair == "ix" ? "ixh" : "iyh";
}

const char* Low(const std::string& pair)
{
    return pair == "bc" ? "c" : pair == "de" ? "e" : pair == "hl" ? "l" : pair == "ix" ? "ixl" : "iyl";
}

/// One instruction with any number of operands: the virtual instructions it stands for (help: ADD A,A,A,B,HL,DE is
/// ADD A,A / ADD A,B / ADD HL,DE; JR C,$+5,Z,$+20 two JRs; OUT B,A,(#FE) is OUT (C),B / OUT (C),A / OUT (#FE),A),
/// with STORM's built-in macros spelled out
std::vector<Statement> Instructions(const std::string& mnemonic, const std::vector<std::string>& texts)
{
    std::vector<Statement> out;
    if (mnemonic == "exa")
        return {Instruction("ex", {Register("af"), Register("af'")})};
    if (mnemonic == "inf")
        return {Instruction("in", {Register("f"), Indirect("c")})};
    if (texts.empty())
        return {Instruction(mnemonic, {})};

    const bool conditional = z80::TakesCondition(mnemonic);
    std::vector<Operand> ops;
    for (size_t k = 0; k < texts.size(); ++k)
        ops.push_back(ParseOperand(texts[k], false));
    // Conditions: JP / JR / CALL take one before their target, RET's operands are all conditions
    auto condition = [&](size_t k) {
        if (!conditional)
            return false;
        const std::string lower = z80::Lower(texts[k]);
        return z80::IsCondition(lower);
    };

    for (size_t k = 0; k < ops.size();)
    {
        const bool hasNext = k + 1 < ops.size();
        auto take = [&](size_t n) {
            std::vector<Operand> part(ops.begin() + static_cast<std::ptrdiff_t>(k), ops.begin() + static_cast<std::ptrdiff_t>(k + n));
            k += n;
            return part;
        };
        if (mnemonic == "ret")
        {
            out.push_back(Instruction(mnemonic, {ParseOperand(texts[k], true)}));
            ++k;
        }
        else if (conditional)
        {
            if (condition(k) && hasNext)
            {
                out.push_back(Instruction(mnemonic, {ParseOperand(texts[k], true), ops[k + 1]}));
                k += 2;
            }
            else
                out.push_back(Instruction(mnemonic, take(1)));
        }
        else if (mnemonic == "in")
        {
            const Operand& a = ops[k];
            const bool pair = hasNext && a.kind == Operand::Kind::Register &&
                              ((ops[k + 1].kind == Operand::Kind::Indirect && ops[k + 1].text == "c") || (a.text == "a" && ops[k + 1].kind == Operand::Kind::Memory));
            if (pair)
                out.push_back(Instruction(mnemonic, take(2)));
            else if (a.kind == Operand::Kind::Register)   // IN D = IN D,(C)
                out.push_back(Instruction(mnemonic, {take(1)[0], Indirect("c")}));
            else   // IN (#FE) = IN A,(#FE)
                out.push_back(Instruction(mnemonic, {Register("a"), take(1)[0]}));
        }
        else if (mnemonic == "out")
        {
            const Operand& a = ops[k];
            const bool pair = hasNext && ((a.kind == Operand::Kind::Indirect && a.text == "c" &&
                                           (ops[k + 1].kind == Operand::Kind::Register || ops[k + 1].kind == Operand::Kind::Immediate)) ||
                                          (a.kind == Operand::Kind::Memory && ops[k + 1].kind == Operand::Kind::Register && ops[k + 1].text == "a"));
            if (pair)
                out.push_back(Instruction(mnemonic, take(2)));
            else if (a.kind == Operand::Kind::Register)   // OUT B = OUT (C),B
                out.push_back(Instruction(mnemonic, {Indirect("c"), take(1)[0]}));
            else   // OUT (#FE) = OUT (#FE),A
                out.push_back(Instruction(mnemonic, {take(1)[0], Register("a")}));
        }
        else if (z80::SplitArity(mnemonic) == 1)
            out.push_back(Instruction(mnemonic, take(1)));   // RLC (IX+1),B is RLC (IX+1) / RLC B (STORM 1.3)
        else if (z80::SplitArity(mnemonic) == 2)
            out.push_back(Instruction(mnemonic, take(hasNext ? 2 : 1)));
        else
            out.push_back(Instruction(mnemonic, take(1)));
    }

    // Built-in macros
    std::vector<Statement> expanded;
    for (Statement& s : out)
    {
        const bool two = s.operands.size() == 2 && s.operands[0].kind == Operand::Kind::Register && s.operands[1].kind == Operand::Kind::Register;
        const std::string a = two ? s.operands[0].text : std::string(), b = two ? s.operands[1].text : std::string();
        if (s.mnemonic == "ld" && two && IsPair(a) && IsPair(b))
        {
            // LD HL,BC = LD H,B : LD L,C; LD BC,IX = LD B,HX : LD C,LX
            expanded.push_back(Instruction("ld", {Register(High(a)), Register(High(b))}));
            expanded.push_back(Instruction("ld", {Register(Low(a)), Register(Low(b))}));
        }
        else if (s.mnemonic == "ex" && two && a == "hl" && b == "de")
            expanded.push_back(Instruction("ex", {Register("de"), Register("hl")}));
        else if (s.mnemonic == "ex" && two && a == "af" && b == "af")
            expanded.push_back(Instruction("ex", {Register("af"), Register("af'")}));
        else if (s.mnemonic == "add" && two && a == "de" && b == "hl")
        {
            // ADD DE,HL = EX DE,HL : ADD HL,DE : EX DE,HL
            expanded.push_back(Instruction("ex", {Register("de"), Register("hl")}));
            expanded.push_back(Instruction("add", {Register("hl"), Register("de")}));
            expanded.push_back(Instruction("ex", {Register("de"), Register("hl")}));
        }
        else
            expanded.push_back(std::move(s));
    }
    return expanded;
}

/// An INCB / INCL name: a text or a label's name; the 9th character is the TR-DOS type ("C" when there is none)
std::string FileName(const std::string& op)
{
    std::string chars;
    std::string name = QuotedText(op, chars) ? chars : op;
    if (name.size() == 9)
    {
        std::string base = name.substr(0, 8);
        while (!base.empty() && base.back() == ' ')
            base.pop_back();
        return base + "." + name.substr(8);
    }
    while (!name.empty() && name.back() == ' ')
        name.pop_back();
    return name;
}

Statement Directive(ir::DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
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

/// DB / DW: $ is the address of each item (help: DB ABC-$,LOOP-$ = DB ABC-$ / DB LOOP-$), so a list that uses $
/// after its first item becomes one statement per item
void PushData(Statement s, std::vector<Statement>& out)
{
    bool later = false;
    for (size_t k = 1; k < s.operands.size(); ++k)
        later = later || (s.operands[k].kind != Operand::Kind::String && UsesCurrent(s.operands[k].expr));
    if (!later)
    {
        out.push_back(std::move(s));
        return;
    }
    for (const Operand& o : s.operands)
    {
        Statement part = s;
        part.operands.assign(1, o);
        out.push_back(std::move(part));
    }
}

struct LineParser
{
    FrontendResult& result;
    // ORG state: an ORG ends an active displacement first (sjasmplus' ORG would move only $); where a file starts it
    // is not known (INCL inside a displaced ORG): the end is then conditional
    enum class Phase { Unknown, Active, Off } phase = Phase::Unknown;
    int orgTemporaries = 0;

    void EndPhase(std::vector<Statement>& out)
    {
        if (phase == Phase::Off)
            return;
        Statement ent = Directive(ir::DirectiveKind::Ent);
        if (phase == Phase::Unknown)
            ent.text = "if-displaced";
        out.push_back(std::move(ent));
        phase = Phase::Off;
    }

    /// ORG run[,place]: the code runs at `run` and is put at `place`; $ in either is the run address so far
    void Org(std::vector<Expr> args, std::vector<ir::Line>& before, std::vector<Statement>& out, uint32_t number)
    {
        if (phase != Phase::Off)
            for (Expr& a : args)
                if (UsesCurrent(a))
                {
                    // $ is the displaced address: kept before the displacement ends
                    ir::Line keep;
                    keep.sourceLine = number;
                    keep.label = "__UNREALASM_ORG" + std::to_string(orgTemporaries++);
                    Statement defl = Directive(ir::DirectiveKind::Defl);
                    defl.args.push_back(std::move(a));
                    keep.statements.push_back(std::move(defl));
                    a = Expr::Symbol(keep.label);
                    before.push_back(std::move(keep));
                }
        EndPhase(out);
        Statement org = Directive(ir::DirectiveKind::Org);
        org.args.push_back(args.size() > 1 ? args[1] : args[0]);
        out.push_back(std::move(org));
        if (args.size() > 1)
        {
            Statement disp = Directive(ir::DirectiveKind::Disp);
            disp.args.push_back(args[0]);
            out.push_back(std::move(disp));
            phase = Phase::Active;
        }
    }

    /// One statement: a command and its operands
    void ParseStatement(const std::string& text, ir::Line& line, std::vector<ir::Line>& before, std::vector<Statement>& out)
    {
        const uint32_t number = line.sourceLine;
        size_t j = 0;
        while (j < text.size() && text[j] != ' ')
            ++j;
        std::string word = z80::Upper(text.substr(0, j));
        if (!word.empty() && word[0] == '_')   // _DB: the command stands in column 0 (40-character lines)
            word.erase(0, 1);
        const std::string rest = Trim(text.substr(j));
        const std::vector<std::string> ops = Split(rest, ',');

        if (word == "DB" || word == "DEFB")
        {
            Statement s = Directive(ir::DirectiveKind::Db);
            s.operands = ByteOperands(ops, false, result.diagnostics, number);
            PushData(std::move(s), out);
            return;
        }
        if (word == "DW" || word == "DEFW")
        {
            Statement s = Directive(ir::DirectiveKind::Dw);
            for (const std::string& op : ops)
            {
                Operand o;
                o.kind = Operand::Kind::Immediate;
                o.expr = ParseExpression(op);
                s.operands.push_back(std::move(o));
            }
            PushData(std::move(s), out);
            return;
        }
        if (word == "DS" || word == "DEFS")
        {
            // DS count[,pattern...]: count bytes, the pattern repeated and cut (DS 7,#AA,#BB is 7 bytes)
            if (ops.empty())
                throw Failure{"DS without a count"};
            Statement s = Directive(ir::DirectiveKind::Ds);
            s.args.push_back(ParseExpression(ops[0]));
            if (ops.size() > 1)
            {
                s.operands = ByteOperands(std::vector<std::string>(ops.begin() + 1, ops.end()), true, result.diagnostics, number);
                if (s.operands.size() > 1)
                    s.params = {"cyclic"};
            }
            out.push_back(std::move(s));
            return;
        }
        if (word == "EQU")
        {
            Statement s = Directive(ir::DirectiveKind::Equ);
            s.args.push_back(ParseExpression(rest));
            out.push_back(std::move(s));
            return;
        }
        if (word == "ORG")
        {
            std::vector<Expr> args;
            for (const std::string& op : ops)
                args.push_back(ParseExpression(op));
            if (args.empty() || args.size() > 2)
                throw Failure{"ORG takes one or two addresses"};
            Org(std::move(args), before, out, number);
            return;
        }
        if (word == "INCB" || word == "INCL")
        {
            // Several names: one file after the other (INCL does not nest)
            for (const std::string& op : ops)
            {
                Statement s = Directive(word == "INCB" ? ir::DirectiveKind::Incbin : ir::DirectiveKind::Include);
                s.text = FileName(op);
                out.push_back(std::move(s));
            }
            return;
        }
        if (word == "REPT" || word == "ENDR" || word == "IF" || word == "IFU" || word == "IFNU" || word == "IFD" || word == "IFND" ||
            word == "ELSE" || word == "EIF" || word == "ENDM")
        {
            // In STORM 1.3's keyword table, announced for a later version (help: macros, conditional assembly)
            Statement s = Directive(ir::DirectiveKind::Other);
            s.text = word + (rest.empty() ? "" : " " + rest);
            result.diagnostics.push_back({Severity::Warning, number, 0, "STORM keyword " + word + " (no meaning in 1.3) kept as text"});
            out.push_back(std::move(s));
            return;
        }
        const std::string mnemonic = z80::Lower(word);
        if (!z80::IsMnemonic(mnemonic) && mnemonic != "exa")
            throw Failure{"unknown command " + word};
        for (Statement& s : Instructions(mnemonic, ops))
            out.push_back(std::move(s));
    }

    void ParseLine(const std::string& text, uint32_t number)
    {
        ir::Line line;
        line.sourceLine = number;
        std::string t = text;
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
        for (char& c : t)
            if (c == '\t')
                c = ' ';
        while (!t.empty() && t.back() == ' ')
            t.pop_back();
        std::vector<ir::Line> before;
        try
        {
            size_t i = 0;
            int repeat = -1;
            if (!t.empty() && t[0] != ' ' && t[0] != '_')
            {
                while (i < t.size() && t[i] != ' ' && t[i] != ':')
                    ++i;
                const std::string field = t.substr(0, i);
                if (field[0] == '.')
                {
                    // .n: the line is assembled n times, 0 = 256
                    const std::string digits = field.substr(1);
                    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
                        throw Failure{"repeat count " + field};
                    repeat = std::stoi(digits);
                    if (repeat > 255)
                        throw Failure{"repeat count above 255"};
                }
                else
                    line.label = field;
            }
            std::vector<Statement> out;
            for (const std::string& statement : Split(std::string_view(t).substr(i), ':'))
                if (!statement.empty())
                    ParseStatement(statement, line, before, out);
            if (repeat >= 0 && !out.empty())
            {
                Statement rept = Directive(ir::DirectiveKind::Repeat);
                rept.args.push_back(Expr::Number(repeat == 0 ? 256 : repeat));
                out.insert(out.begin(), std::move(rept));
                out.push_back(Directive(ir::DirectiveKind::EndRepeat));
            }
            line.statements = std::move(out);
        }
        catch (const Failure& f)
        {
            before.clear();
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
            before.clear();
            Statement raw;
            raw.kind = Statement::Kind::Raw;
            raw.text = text;
            line.statements.assign(1, raw);
            line.label.clear();
            line.hasComment = false;
            result.diagnostics.push_back({Severity::Warning, number, 0, "not parsed (a number out of range): kept as text"});
        }
        for (ir::Line& b : before)
            result.program.lines.push_back(std::move(b));
        result.program.lines.push_back(std::move(line));
    }
};
}  // namespace

FrontendResult StormFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "storm";
    result.program.expressionBits = 16;   // "0-#20 = #FFE0": 16-bit words, carries ignored
    result.program.unsignedArithmetic = true;
    result.program.displacementAcrossFiles = true;
    result.program.trueValue = 1;         // comparisons and @ give 1
    LineParser parser{result};
    uint32_t number = 0;
    for (const SourceLine& line : source.lines)
        parser.ParseLine(line.text, ++number);
    return result;
}
}  // namespace unrealasm::dialects
