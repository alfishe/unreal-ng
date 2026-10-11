#include "dialects/common/retrowriter.h"

#include <algorithm>
#include <cctype>

#include "dialects/common/z80.h"
#include "unrealasm/registry.h"

namespace unrealasm::dialects
{
namespace
{
// sjasmplus directives kept as text that make no bytes: dropped with a note
const std::set<std::string> kNoBytes = {
    "DEVICE", "SAVEBIN", "SAVESNA", "SAVETAP", "SAVEHOB", "SAVETRD", "SAVENEX", "SAVECPCSNA", "SAVECDT", "SAVEDEV",
    "EMPTYTAP", "EMPTYTRD", "OUTPUT", "OUTEND", "LABELSLIST", "CSPECTMAP", "BPLIST", "SETBP", "SETBREAKPOINT",
    "EXPORT", "TAPOUT", "TAPEND", "SHELLEXEC", "ASSERT", "SIZE", "DEFDEVICE", "DISPLAY", "OPT",
};
}  // namespace

RetroWriter::RetroWriter(const ir::Program& p, const BackendOptions& o, BackendResult& r)
    : program(p), options(o), result(r), diagnostics(r.diagnostics)
{
}

// ---- helpers ----

bool RetroWriter::IsTemporary(const std::string& label)
{
    return !label.empty() && label.find_first_not_of("0123456789") == std::string::npos;
}

bool RetroWriter::IsDirective(const Statement& s, ir::DirectiveKind kind)
{
    return s.kind == Statement::Kind::Directive && s.directive == kind;
}

const ir::Expr& RetroWriter::Unwrapped(const Expr& e)
{
    return e.kind == Expr::Kind::Group ? Unwrapped(e.args[0]) : e;
}

bool RetroWriter::UsesCurrent(const Expr& e)
{
    if (e.kind == Expr::Kind::Current)
        return true;
    return std::any_of(e.args.begin(), e.args.end(), [](const Expr& a) { return UsesCurrent(a); });
}

ir::Expr RetroWriter::ShiftCurrent(const Expr& e, int64_t offset)
{
    if (e.kind == Expr::Kind::Current)
    {
        Expr g = Expr::Make(Expr::Kind::Group);
        g.args.push_back(Expr::Binary(Op::Sub, e, Expr::Number(offset)));
        return g;
    }
    Expr out = e;
    for (Expr& a : out.args)
        a = ShiftCurrent(a, offset);
    return out;
}

// ---- names ----

std::string RetroWriter::Sanitized(const std::string& name) const
{
    std::string out;
    for (const char c : name)
        out.push_back(std::isalnum(static_cast<unsigned char>(c)) || c == '_' ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_');
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
        out = "L" + out;
    return out;
}

std::string RetroWriter::Key(const std::string& name) const
{
    return z80::Upper(name.substr(0, Significant()));
}

std::string RetroWriter::FullName(const std::string& text) const
{
    if (!text.empty() && text[0] == '@')
        return text.substr(1);
    if (!text.empty() && text[0] == '.')
        return parent + text;
    return text;
}

std::optional<std::string> RetroWriter::Temporary(const std::string& reference) const
{
    if (reference.size() < 2 || !IsTemporary(reference.substr(0, reference.size() - 1)))
        return std::nullopt;
    const char way = static_cast<char>(std::tolower(static_cast<unsigned char>(reference.back())));
    const auto found = temporaries.find(reference.substr(0, reference.size() - 1));
    if (found == temporaries.end() || (way != 'b' && way != 'f'))
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

std::string RetroWriter::Name(const std::string& text) const
{
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
        if (const auto local = it->find(text); local != it->end())
            return Shown(local->second);
    const std::string full = FullName(text);
    const auto found = names.find(full);
    return Shown(found != names.end() ? found->second : Sanitized(full).substr(0, Significant()));
}

bool RetroWriter::InScope(const std::string& text) const
{
    return std::any_of(scopes.begin(), scopes.end(), [&](const auto& scope) { return scope.count(text) > 0; });
}

std::string RetroWriter::Give(const std::string& wanted)
{
    const std::string base = Sanitized(wanted);
    std::string name = base.substr(0, Significant());
    for (int n = 1; taken.count(Key(name)) || Reserved(z80::Upper(name)); ++n)
    {
        const std::string suffix = std::to_string(n);
        name = base.substr(0, Significant() - suffix.size()) + suffix;
    }
    taken.insert(Key(name));
    return name;
}

// ---- values ----

int64_t RetroWriter::Wrap(int64_t v) const
{
    if (program.expressionBits == 16)
    {
        v &= 0xFFFF;
        return !program.unsignedArithmetic && v >= 0x8000 ? v - 0x10000 : v;
    }
    return static_cast<int32_t>(static_cast<uint32_t>(v & 0xFFFFFFFF));
}

const SourceLabel* RetroWriter::Lookup(const std::string& text) const
{
    if (text.empty() || InScope(text))
        return nullptr;   // a label of a macro expansion: no layout value
    const auto found = options.labelValues.find(FullName(text));
    return found == options.labelValues.end() ? nullptr : &found->second;
}

std::optional<int64_t> RetroWriter::Value(const Expr& e) const
{
    switch (e.kind)
    {
        case Expr::Kind::Number: return e.value;
        case Expr::Kind::Symbol:
        {
            if (redefinable.count(FullName(e.text)) && !InScope(e.text))
            {
                const auto v = deflValues.find(FullName(e.text));
                return v == deflValues.end() ? std::nullopt : std::optional<int64_t>(v->second);
            }
            const SourceLabel* l = Lookup(e.text);
            if (!l || l->redefinable)
                return std::nullopt;
            return l->value;
        }
        case Expr::Kind::Current: return displaced ? std::nullopt : here;
        case Expr::Kind::Group: return Value(e.args[0]);
        case Expr::Kind::Unary:
        {
            if (e.op == Op::Exists)
            {
                if (options.labelValues.empty() || e.args[0].kind != Expr::Kind::Symbol)
                    return std::nullopt;
                return options.labelValues.count(FullName(e.args[0].text)) ? True() : 0;
            }
            const auto a = Value(e.args[0]);
            if (!a)
                return std::nullopt;
            switch (e.op)
            {
                case Op::Negate: return Wrap(-*a);
                case Op::Plus: return *a;
                case Op::Not: return Wrap(~*a);
                case Op::High: return (*a >> 8) & 0xFF;
                case Op::Low: return *a & 0xFF;
                case Op::LogicalNot: return *a == 0 ? True() : 0;
                case Op::SwapBytes: return ((*a & 0xFF) << 8) | ((*a >> 8) & 0xFF);
                default: return std::nullopt;
            }
        }
        case Expr::Kind::Binary:
        {
            const auto a = Value(e.args[0]);
            const auto b = Value(e.args[1]);
            if (!a || !b)
                return std::nullopt;
            const bool unsignedWords = program.expressionBits == 16 && program.unsignedArithmetic;
            switch (e.op)
            {
                case Op::Add: return Wrap(*a + *b);
                case Op::Sub: return Wrap(*a - *b);
                case Op::Mul: return Wrap(*a * *b);
                case Op::Div: return *b == 0 ? std::nullopt : std::optional<int64_t>(Wrap(*a / *b));
                case Op::Mod: return *b == 0 ? std::nullopt : std::optional<int64_t>(Wrap(*a % *b));
                case Op::And: return Wrap(*a & *b);
                case Op::Or: return Wrap(*a | *b);
                case Op::Xor: return Wrap(*a ^ *b);
                case Op::Shl: return *b < 0 || *b > 63 ? 0 : Wrap(*a << *b);
                case Op::Shr: return *b < 0 || *b > 63 ? (*a < 0 && !unsignedWords ? -1 : 0) : Wrap(*a >> *b);
                case Op::ShrUnsigned:
                    return *b < 0 || *b > 31 ? 0 : Wrap(static_cast<int64_t>((static_cast<uint64_t>(*a) & 0xFFFFFFFF) >> *b));
                case Op::RotateLeft16: case Op::RotateRight16:
                {
                    const uint32_t v = static_cast<uint32_t>(*a & 0xFFFF);
                    const int n = static_cast<int>(*b & 15);
                    const uint32_t r = e.op == Op::RotateLeft16 ? (v << n) | (v >> (16 - n)) : (v >> n) | (v << (16 - n));
                    return r & 0xFFFF;
                }
                case Op::Equal: return *a == *b ? True() : 0;
                case Op::NotEqual: return *a != *b ? True() : 0;
                case Op::Less: return *a < *b ? True() : 0;
                case Op::Greater: return *a > *b ? True() : 0;
                case Op::LessEqual: return *a <= *b ? True() : 0;
                case Op::GreaterEqual: return *a >= *b ? True() : 0;
                case Op::LogicalAnd: return *a != 0 && *b != 0 ? True() : 0;
                case Op::LogicalOr: return *a != 0 || *b != 0 ? True() : 0;
                default: return std::nullopt;
            }
        }
        default: return std::nullopt;
    }
}

std::string RetroWriter::LineLabel(bool withFile) const
{
    std::string file;
    if (withFile)
        for (const char c : options.fileName)
            file.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
    return parent + ".__UNREALASM_L" + file + "_" + std::to_string(line);
}

bool RetroWriter::Later(const std::string& text) const
{
    const SourceLabel* l = Lookup(text);
    if (!l || l->redefinable)
        return false;
    if (!l->file.empty() && !options.fileName.empty() && l->file != options.fileName)
        return true;
    return l->line > line;
}

bool RetroWriter::InWord(const Expr& e) const
{
    if (program.expressionBits == 16 && program.unsignedArithmetic)
        return true;
    const auto v = Value(e);
    return v && *v >= 0 && *v <= 0xFFFF;
}

// ---- expressions and operands ----

std::string RetroWriter::Number(int64_t value) const
{
    const int64_t v = value & 0xFFFF;
    return v < 256 ? std::to_string(v) : "#" + z80::HexDigits(static_cast<uint64_t>(v), 4);
}

void RetroWriter::Problem(const std::string& why)
{
    if (problem.empty())
        problem = why;
}

std::optional<std::string> RetroWriter::SymbolText(const Expr& u)
{
    switch (u.kind)
    {
        case Expr::Kind::Symbol:
            if (redefinable.count(FullName(u.text)) && !InScope(u.text))
            {
                // the target defines a name once: a name assigned again and again stands for its value here, or for
                // the name its definition in force got
                if (const auto v = Value(u))
                    return Number(*v);
                if (const auto n = deflNames.find(FullName(u.text)); n != deflNames.end())
                    return Shown(n->second);
                Problem("the value of " + u.text + " (assigned with = / DEFL) is not known here");
                return Name(u.text);
            }
            if (strict && Later(u.text))
            {
                if (const auto v = Value(u))
                    return Number(*v);
                Problem("the label " + u.text + " is defined further down");
            }
            return Name(u.text);
        case Expr::Kind::Current: return std::string("$");
        case Expr::Kind::Raw:
            if (const auto temporary = Temporary(u.text))
                return Shown(*temporary);
            return std::nullopt;
        default: return std::nullopt;
    }
}

std::string RetroWriter::Expression(const Expr& e)
{
    problem.clear();
    const auto text = Text(e);
    if (text && problem.empty())
        return *text;
    if (const auto value = Value(e))
    {
        diagnostics.push_back({Severity::Info, line, 0, "written as its value " + Number(*value) + (problem.empty() ? std::string() : ": " + problem)});
        return Number(*value);
    }
    diagnostics.push_back({Severity::Warning, line, 0, Title() + " cannot compute this expression" + (problem.empty() ? std::string() : " (" + problem + ")") + ": written as 0"});
    return text ? *text : "0";
}

std::string RetroWriter::Known(const Expr& e)
{
    strict = true;
    const std::string text = Expression(e);
    strict = false;
    return text;
}

std::string RetroWriter::HexByte(int value) const
{
    return "#" + z80::HexDigits(static_cast<uint64_t>(value & 0xFF), 2);
}

std::string RetroWriter::RegisterText(const std::string& r) const
{
    if (r == "ixh") return "HX";
    if (r == "ixl") return "LX";
    if (r == "iyh") return "HY";
    if (r == "iyl") return "LY";
    return r == "af'" ? "AF" : z80::Upper(r);   // EX AF,AF is EX AF,AF'
}

std::string RetroWriter::OperandText(const Operand& o)
{
    switch (o.kind)
    {
        case Operand::Kind::Register: return RegisterText(o.text);
        case Operand::Kind::Condition: return z80::Upper(o.text);
        case Operand::Kind::Indirect: return "(" + z80::Upper(o.text) + ")";
        case Operand::Kind::Indexed:
        {
            const auto d = Value(o.expr);
            if (d && *d < 0 && *d >= -128)
                return "(" + z80::Upper(o.text) + "-" + std::to_string(-*d) + ")";
            return "(" + z80::Upper(o.text) + "+" + Expression(o.expr) + ")";
        }
        case Operand::Kind::Memory: return "(" + Expression(o.expr) + ")";
        case Operand::Kind::Immediate: return Expression(o.expr);
        case Operand::Kind::String:
        {
            std::vector<std::string> items, lines;
            PutString(o.text, items, lines, [] {});
            std::string out;
            for (const std::string& item : items)
                out += (out.empty() ? "" : ",") + item;
            return out;
        }
    }
    return {};
}

std::string RetroWriter::Operands(const std::vector<Operand>& operands)
{
    std::string out;
    for (const Operand& o : operands)
        out += (out.empty() ? "" : ",") + OperandText(o);
    return out;
}

std::string RetroWriter::NotConverted(const std::string& what)
{
    diagnostics.push_back({Severity::Warning, line, 0, "not converted: " + what});
    return ";unreal-asm: not converted: " + what;
}

std::vector<std::string> RetroWriter::DataLines(const std::string& keyword, const std::vector<std::string>& items, size_t width) const
{
    std::vector<std::string> out;
    std::string current;
    for (const std::string& item : items)
    {
        if (!current.empty() && current.size() + item.size() > width)
        {
            out.push_back(keyword + " " + current);
            current.clear();
        }
        current += (current.empty() ? "" : ",") + item;
    }
    if (!current.empty())
        out.push_back(keyword + " " + current);
    return out;
}

std::vector<std::string> RetroWriter::Data(const Statement& s, bool words)
{
    const bool current = s.operands.size() > 1 && std::any_of(s.operands.begin(), s.operands.end(), [](const Operand& o) { return UsesCurrent(o.expr); });
    std::vector<std::string> items;
    std::vector<std::string> out;
    const auto flush = [&]() {
        for (const std::string& l : DataLines(words ? Dw() : Db(), items, DataWidth()))
            out.push_back(l);
        items.clear();
    };
    int64_t offset = 0;
    for (const Operand& o : s.operands)
    {
        if (o.kind == Operand::Kind::String)
        {
            if (words)
            {
                // a string of one or two characters in DW is a word (sjasmplus 'AB' = #4142)
                if (o.text.size() > 2)
                {
                    items.push_back(NotConverted("a string of more than two characters in DW"));
                    continue;
                }
                int64_t value = 0;
                for (const char c : o.text)
                    value = ((value << 8) | static_cast<unsigned char>(c)) & 0xFFFF;
                items.push_back(Number(value));
                offset += 2;
                continue;
            }
            if (o.text.size() <= 2)
                for (const char c : o.text)
                    items.push_back(std::to_string(static_cast<unsigned char>(c)));
            else
                PutString(o.text, items, out, flush);
            offset += static_cast<int64_t>(o.text.size());
            continue;
        }
        Operand item = o;
        if (current && offset > 0)
            item.expr = ShiftCurrent(o.expr, offset);
        if ((words || !CharactersInData()) && item.kind == Operand::Kind::Immediate && item.expr.kind == Expr::Kind::Number && item.expr.spelling == ir::NumberSpelling::Character)
            item.expr.spelling = ir::NumberSpelling::Decimal;
        items.push_back(OperandText(item));
        offset += words ? 2 : 1;
        if (current)
            flush();   // one item a line: $ is then the item's address in both assemblers once shifted
    }
    flush();
    return out;
}

std::string RetroWriter::Bytes(const std::vector<std::string>& items, const std::string& instruction) const
{
    std::string out = Db() + " ";
    for (size_t k = 0; k < items.size(); ++k)
        out += (k ? "," : "") + items[k];
    return out + CommentAfter() + instruction;
}

std::vector<std::string> RetroWriter::InstructionText(const Statement& s)
{
    if (auto own = OwnInstruction(s))
        return *own;
    const std::string& m = s.mnemonic;
    const auto& ops = s.operands;
    auto reg = [&](size_t k, const char* name) { return k < ops.size() && ops[k].kind == Operand::Kind::Register && ops[k].text == name; };
    // The undocumented copies (RLC (IX+d),B; SET 3,(IY+d),A): their bytes
    {
        static const std::map<std::string, int> kShifts = {{"rlc", 0x00}, {"rrc", 0x08}, {"rl", 0x10}, {"rr", 0x18},
                                                            {"sla", 0x20}, {"sra", 0x28}, {"sll", 0x30}, {"sli", 0x30}, {"srl", 0x38}};
        static const std::map<std::string, int> kRegisters = {{"b", 0}, {"c", 1}, {"d", 2}, {"e", 3}, {"h", 4}, {"l", 5}, {"a", 7}};
        const auto shift = kShifts.find(m);
        const bool bitOp = (m == "res" || m == "set") && ops.size() == 3;
        const size_t at = bitOp ? 1 : 0;
        if ((shift != kShifts.end() && ops.size() == 2) || bitOp)
            if (ops[at].kind == Operand::Kind::Indexed && ops[at + 1].kind == Operand::Kind::Register && kRegisters.count(ops[at + 1].text))
            {
                int code = kRegisters.at(ops[at + 1].text);
                if (bitOp)
                {
                    const auto bit = Value(ops[0].expr);
                    if (!bit)
                        return {NotConverted("an undocumented copy with a bit known only when assembling")};
                    code |= (m == "res" ? 0x80 : 0xC0) | static_cast<int>((*bit & 7) << 3);
                }
                else
                    code |= shift->second;
                const auto d = Value(ops[at].expr);
                return {Bytes({HexByte(ops[at].text == "ix" ? 0xDD : 0xFD), HexByte(0xCB), d ? Number(*d & 0xFF) : Expression(ops[at].expr), Number(code)}, z80::Upper(m) + " " + Operands(ops))};
            }
    }
    if (program.dialect == "sjasmplus")
    {
        // sjasmplus repeats these for each operand (PUSH AF,BC; SUB A,B is SUB A : SUB B)
        const bool pairFirst = !ops.empty() && ops[0].kind == Operand::Kind::Register && (ops[0].text == "hl" || ops[0].text == "ix" || ops[0].text == "iy");
        if (ops.size() > 1 && (m == "push" || m == "pop" || m == "inc" || m == "dec" ||
                               ((m == "sub" || m == "and" || m == "xor" || m == "or" || m == "cp") && !pairFirst)))
        {
            std::vector<std::string> out;
            for (const Operand& o : ops)
            {
                Statement one = s;
                one.operands.assign(1, o);
                for (const std::string& t : InstructionText(one))
                    out.push_back(t);
            }
            return out;
        }
        // and its fake instructions, written out as it assembles them
        auto halves = [](const std::string& r) -> std::pair<std::string, std::string> {
            if (r == "bc") return {"b", "c"};
            if (r == "de") return {"d", "e"};
            if (r == "hl") return {"h", "l"};
            if (r == "ix") return {"ixh", "ixl"};
            if (r == "iy") return {"iyh", "iyl"};
            return {};
        };
        auto regOp = [](const std::string& r) {
            Operand o;
            o.kind = Operand::Kind::Register;
            o.text = r;
            return o;
        };
        auto one = [&](const char* mnemonic, std::vector<Operand> operands) {
            Statement t = s;
            t.mnemonic = mnemonic;
            t.operands = std::move(operands);
            return InstructionText(t);
        };
        auto join = [](std::vector<std::string> a, const std::vector<std::string>& b) {
            a.insert(a.end(), b.begin(), b.end());
            return a;
        };
        const bool pair0 = ops.size() == 2 && ops[0].kind == Operand::Kind::Register && !halves(ops[0].text).first.empty();
        const bool pair1 = ops.size() == 2 && ops[1].kind == Operand::Kind::Register && !halves(ops[1].text).first.empty();
        if (m == "sub" && pair0 && ops[0].text == "hl" && pair1)
            return join(one("or", {regOp("a")}), one("sbc", {ops[0], ops[1]}));
        if (m == "ld" && pair0 && pair1)
        {
            const auto d = halves(ops[0].text), v = halves(ops[1].text);
            return join(one("ld", {regOp(d.first), regOp(v.first)}), one("ld", {regOp(d.second), regOp(v.second)}));
        }
        if (m == "ld" && pair0 && ops[1].kind == Operand::Kind::Indexed)
        {
            Operand next = ops[1];
            next.expr = Expr::Binary(Op::Add, ops[1].expr, Expr::Number(1));
            const auto d = halves(ops[0].text);
            return join(one("ld", {regOp(d.second), ops[1]}), one("ld", {regOp(d.first), next}));
        }
        if (m == "ld" && pair1 && ops[0].kind == Operand::Kind::Indexed)
        {
            Operand next = ops[0];
            next.expr = Expr::Binary(Op::Add, ops[0].expr, Expr::Number(1));
            const auto v = halves(ops[1].text);
            return join(one("ld", {ops[0], regOp(v.second)}), one("ld", {next, regOp(v.first)}));
        }
    }
    if (program.dialect != "sjasmplus" && ops.size() == 2 && reg(0, "a") && (m == "and" || m == "or" || m == "xor" || m == "cp" || m == "sub"))
    {
        Statement t = s;
        t.operands.erase(t.operands.begin());
        return InstructionText(t);
    }
    if (ops.size() == 1 && (m == "add" || m == "adc" || m == "sbc") && ops[0].kind != Operand::Kind::Condition)
        return {z80::Upper(m) + " A," + OperandText(ops[0])};
    const std::string text = Operands(ops);
    return {z80::Upper(m) + (text.empty() ? "" : " " + text)};
}

// ---- conditions ----

std::vector<std::string> RetroWriter::OpenCondition(const Expr& condition)
{
    Condition c;
    c.parentEmits = Emitting();
    if (!c.parentEmits)
    {
        conditions.push_back(c);
        return {};
    }
    strict = true;
    const auto value = Value(condition);
    strict = false;
    if (value)
    {
        c.taken = *value != 0;
        conditions.push_back(c);
        return {};
    }
    c.decided = false;
    if (!nativeBlock)
        if (auto lines = OpenNative(condition, c))
        {
            c.native = true;
            nativeBlock = true;
            conditions.push_back(c);
            return *lines;
        }
    conditions.push_back(c);
    return {NotConverted(std::string("a condition known only when assembling") +
                         (nativeBlock ? " inside a block of " + Title() : " (" + Title() + " has no condition for it)") + ": its block is written as taken")};
}

std::vector<std::string> RetroWriter::ElseCondition()
{
    if (conditions.empty())
        return {NotConverted("ELSE without IF")};
    Condition& c = conditions.back();
    if (c.decided)
    {
        c.taken = !c.taken;
        return {};
    }
    if (!c.native)
        return {};
    return ElseNative(c);
}

std::vector<std::string> RetroWriter::CloseCondition()
{
    if (conditions.empty())
        return {NotConverted("ENDIF without IF")};
    const Condition c = conditions.back();
    conditions.pop_back();
    if (c.native)
    {
        nativeBlock = false;
        return CloseNative(c);
    }
    return {};
}

std::vector<std::string> RetroWriter::StatementText(const Statement& s, const std::string& label)
{
    // The conditions first: a block not taken writes nothing
    if (s.kind == Statement::Kind::Directive)
    {
        if (s.directive == ir::DirectiveKind::If)
            return OpenCondition(s.args[0]);
        if (s.directive == ir::DirectiveKind::Else)
            return ElseCondition();
        if (s.directive == ir::DirectiveKind::EndIf)
            return CloseCondition();
        if (s.directive == ir::DirectiveKind::Other)
        {
            const std::string word = z80::Upper(s.text.substr(0, s.text.find_first_of(" \t")));
            const size_t at = s.text.find_first_not_of(" \t", word.size());
            const std::string rest = at == std::string::npos ? std::string() : s.text.substr(at);
            const std::string flag = rest.substr(0, rest.find_first_of(" \t"));
            if (word == "IFDEF" || word == "IFNDEF")
            {
                // sjasmplus' DEFINE flags, tested in the order of the text: decided here
                Condition c;
                c.parentEmits = Emitting();
                c.taken = defines.count(flag) == (word == "IFDEF" ? 1u : 0u);
                conditions.push_back(c);
                return {};
            }
            if (!Emitting())
                return {};
            if (word == "DEFINE" || word == "DEFINE+" || word == "UNDEFINE")
            {
                if (word == "UNDEFINE")
                    defines.erase(flag);
                else
                    defines.insert(flag);
                if (word != "UNDEFINE" && flag.size() < rest.size())
                    diagnostics.push_back({Severity::Warning, line, 0, "a DEFINE with a value (text put in where its name stands): not converted"});
                return {";" + s.text};
            }
            if (kNoBytes.count(word))
            {
                diagnostics.push_back({Severity::Info, line, 0, word + " dropped (it makes no bytes)"});
                return {";" + s.text};
            }
            return {NotConverted(s.text)};
        }
    }
    if (!Emitting())
        return {};
    switch (s.kind)
    {
        case Statement::Kind::Raw: return {NotConverted("unparsed line: " + s.text)};
        case Statement::Kind::Instruction: return InstructionText(s);
        case Statement::Kind::MacroCall: return {NotConverted("a call of an unknown macro: " + s.mnemonic)};
        case Statement::Kind::Directive: break;
    }
    switch (s.directive)
    {
        case ir::DirectiveKind::Equ:
        case ir::DirectiveKind::Defl:
            if (label.empty())
                return {NotConverted("an assignment without a name")};
            if (redefinable.count(FullName(label)) && !InScope(label))
            {
                // its uses get the value it has then
                strict = true;
                const auto v = Value(s.args[0]);
                strict = false;
                if (!v)
                {
                    // a value the target computes ($ in it): this definition gets a name of its own
                    const std::string text = Known(s.args[0]);
                    deflValues.erase(FullName(label));
                    assignedName = Give(FullName(label));
                    deflNames[FullName(label)] = assignedName;
                    return {"EQU " + text};
                }
                deflValues[FullName(label)] = *v;
                deflNames.erase(FullName(label));
                return {";" + label + "=" + Number(*v)};
            }
            return {"EQU " + Known(s.args[0])};
        case ir::DirectiveKind::Db: return Data(s, false);
        case ir::DirectiveKind::Dw: return Data(s, true);
        case ir::DirectiveKind::Display: return {};
        default: return DirectiveText(s, label);
    }
}

// ---- the walk ----

size_t RetroWriter::Matching(const std::vector<ir::Line>& lines, size_t from, ir::DirectiveKind open, ir::DirectiveKind close)
{
    int depth = 0;
    for (size_t k = from; k < lines.size(); ++k)
        for (const Statement& s : lines[k].statements)
        {
            if (IsDirective(s, open))
                ++depth;
            if (IsDirective(s, close) && --depth == 0)
                return k;
        }
    return lines.size();
}

void RetroWriter::Range(const std::vector<ir::Line>& lines, size_t begin, size_t end, size_t index)
{
    for (size_t k = begin; k < end; ++k)
    {
        const ir::Line& l = lines[k];
        const size_t at = index == SIZE_MAX ? k : index;
        const Statement* first = l.statements.empty() ? nullptr : &l.statements.front();
        if (first && IsDirective(*first, ir::DirectiveKind::Macro))
        {
            k = Matching(lines, k, ir::DirectiveKind::Macro, ir::DirectiveKind::EndMacro);   // written where it is used
            continue;
        }
        if (Emitting() && first && IsDirective(*first, ir::DirectiveKind::While))
        {
            // WHILE: unrolled while its condition, with the names' values as they stand, holds
            const size_t e = Matching(lines, k, ir::DirectiveKind::While, ir::DirectiveKind::EndWhile);
            line = l.sourceLine;
            for (int pass = 0;; ++pass)
            {
                strict = true;
                const auto go = Value(first->args[0]);
                strict = false;
                if (!go || pass > 4096)
                {
                    Emit({NotConverted(!go ? "a WHILE whose condition is known only when assembling" : "a WHILE of more than 4096 passes")}, l.sourceLine);
                    break;
                }
                if (*go == 0)
                    break;
                Range(lines, k + 1, e, at);
            }
            k = e;
            continue;
        }
        if (Emitting() && first && IsDirective(*first, ir::DirectiveKind::Repeat) && l.label.empty())
        {
            const size_t e = Matching(lines, k, ir::DirectiveKind::Repeat, ir::DirectiveKind::EndRepeat);
            const RepeatForm form = nativeBlock ? RepeatForm::Unrolled : Repeat(lines, k, e);
            if (form != RepeatForm::Block)
            {
                line = l.sourceLine;
                strict = true;
                const auto count = Value(first->args[0]);
                strict = false;
                if (!count || *count < 0 || *count > 4096)
                    Emit({NotConverted("a repeat " + Title() + " cannot hold, of a count known only when assembling")}, l.sourceLine);
                else if (form == RepeatForm::Line && *count > 0)
                {
                    // the body's one line, repeated by the target
                    const std::string label = RepeatedLabel(*count);
                    WriteLine(lines[k + 1], at, &label);
                }
                else
                    for (int64_t pass = 0; pass < *count; ++pass)
                        Range(lines, k + 1, e, at);
                k = e;
                continue;
            }
        }
        if (Emitting() && first && first->kind == Statement::Kind::MacroCall)
        {
            const auto def = macros.find(z80::Lower(first->mnemonic));
            if (def != macros.end() && nesting < 16)
            {
                if (!l.label.empty())
                {
                    ir::Line alone = l;
                    alone.statements.clear();
                    WriteLine(alone, at);
                }
                Expand(def->second, *first, at, l.sourceLine);
                continue;
            }
        }
        WriteLine(l, at);
    }
}

RetroWriter::Operand RetroWriter::Argument(const std::string& text) const
{
    Operand o;
    o.kind = Operand::Kind::Immediate;
    o.expr = Expr::Make(Expr::Kind::Raw);
    o.expr.text = text;
    const IFrontend* frontend = DialectRegistry::Builtin().Frontend(program.dialect);
    if (!frontend || text.empty())
        return o;
    FrontendResult parsed = frontend->Parse(SourceDocument::FromText("        INC " + text, program.dialect));
    if (parsed.program.lines.empty() || parsed.program.lines[0].statements.size() != 1)
        return o;
    const Statement& s = parsed.program.lines[0].statements[0];
    if (s.kind != Statement::Kind::Instruction || s.operands.size() != 1)
        return o;
    return s.operands[0];
}

namespace
{
ir::Expr Substituted(const ir::Expr& e, const std::map<std::string, ir::Operand>& args)
{
    if (e.kind == ir::Expr::Kind::Symbol)
        if (const auto a = args.find(e.text); a != args.end())
        {
            ir::Expr g = ir::Expr::Make(ir::Expr::Kind::Group);
            g.args.push_back(a->second.expr);
            return g;
        }
    ir::Expr out = e;
    for (ir::Expr& a : out.args)
        a = Substituted(a, args);
    return out;
}

ir::Operand Substituted(const ir::Operand& o, const std::map<std::string, ir::Operand>& args)
{
    if (o.kind == ir::Operand::Kind::Immediate && o.expr.kind == ir::Expr::Kind::Symbol)
        if (const auto a = args.find(o.expr.text); a != args.end())
            return a->second;   // a register, an indirection ... as the call gives it
    ir::Operand out = o;
    out.expr = Substituted(o.expr, args);
    return out;
}
}  // namespace

void RetroWriter::Expand(const MacroDef& def, const Statement& call, size_t index, uint32_t callLine)
{
    std::map<std::string, Operand> args;
    for (size_t k = 0; k < def.params.size(); ++k)
        args[def.params[k]] = Argument(k < call.params.size() ? call.params[k] : std::string());
    // The body with the arguments in, its labels one name per expansion
    ++expansions;
    std::map<std::string, std::string> scope;
    std::vector<ir::Line> body;
    for (size_t n = def.begin + 1; n < def.end; ++n)
    {
        ir::Line copy = program.lines[n];
        if (!copy.label.empty() && !scope.count(copy.label))
            scope[copy.label] = Give("M" + std::to_string(expansions) + (copy.label[0] == '.' ? copy.label.substr(1) : copy.label));
        for (Statement& s : copy.statements)
        {
            for (Operand& o : s.operands)
                o = Substituted(o, args);
            for (Expr& a : s.args)
                a = Substituted(a, args);
        }
        copy.sourceLine = callLine;
        body.push_back(std::move(copy));
    }
    scopes.push_back(std::move(scope));
    ++nesting;
    Range(body, 0, body.size(), index);
    --nesting;
    scopes.pop_back();
}

void RetroWriter::Emit(const std::vector<std::string>& texts, uint32_t origin)
{
    for (const std::string& t : texts)
    {
        result.document.lines.push_back({t, {}});
        result.document.lines.back().origin = origin;
    }
}

void RetroWriter::WriteLine(const ir::Line& l, size_t k, const std::string* repeatLabel)
{
    line = l.sourceLine;
    lineIndex = k;
    std::vector<std::string> texts;
    const bool emittingBefore = Emitting();

    // The label and the parent of sjasmplus' local labels
    std::string label;
    bool assigns = false;
    for (const Statement& s : l.statements)
        assigns = assigns || IsDirective(s, ir::DirectiveKind::Equ) || IsDirective(s, ir::DirectiveKind::Defl);
    if (!l.label.empty())
    {
        if (IsTemporary(l.label))
        {
            for (const auto& [at, name] : temporaries[l.label])
                if (at == k)
                    label = Shown(name);
        }
        else
        {
            if (scopes.empty() && l.label[0] != '.' && l.label[0] != '@')
                parent = l.label;
            else if (scopes.empty() && l.label[0] == '@')
                parent = l.label.substr(1);
            label = Name(l.label);
        }
        if (scopes.empty())
            result.labels.push_back({l.sourceLine, l.label, label, false, false});
    }
    here.reset();
    if (scopes.empty() && !l.label.empty() && !assigns)
    {
        if (const SourceLabel* known = Lookup(l.label))
            here = known->value;
    }
    else if (scopes.empty() && l.label.empty())
    {
        auto found = options.labelValues.find(LineLabel(true));
        if (found == options.labelValues.end())
            found = options.labelValues.find(LineLabel(false));
        if (found != options.labelValues.end())
            here = found->second.value;
    }

    assignedName.clear();
    for (const Statement& s : l.statements)
    {
        for (std::string& t : StatementText(s, l.label))
            texts.push_back(t);
        // $ moves past what the statement puts down (sjasmplus: several statements a line)
        if (s.kind != Statement::Kind::Directive || IsDirective(s, ir::DirectiveKind::Db) || IsDirective(s, ir::DirectiveKind::Dw) ||
            IsDirective(s, ir::DirectiveKind::Ds) || IsDirective(s, ir::DirectiveKind::Incbin))
            here.reset();
    }
    if (!emittingBefore && !Emitting())
        return;   // inside a block not taken: nothing of the line exists
    if (!emittingBefore || (!l.label.empty() && redefinable.count(FullName(l.label)) && !InScope(l.label)))
        label = Shown(assignedName);   // a name assigned with = / DEFL is no label of the target, or one per definition
    if (repeatLabel)
        label = *repeatLabel;
    const std::vector<std::string> out = Layout(label, texts, l.statements.empty(), l.hasComment, l.comment);
    if (!out.empty())
        Emit(out, l.sourceLine);
}

void RetroWriter::Run()
{
    // The macros, and the names assigned more than once (DEFL / "="): those are written as their values
    {
        std::string parentName;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            for (const Statement& s : l.statements)
                if (IsDirective(s, ir::DirectiveKind::Macro))
                {
                    MacroDef def;
                    def.begin = k;
                    def.end = Matching(program.lines, k, ir::DirectiveKind::Macro, ir::DirectiveKind::EndMacro);
                    def.params = s.params;
                    macros[z80::Lower(s.text)] = def;
                }
            if (l.label.empty() || IsTemporary(l.label))
                continue;
            const std::string full = l.label[0] == '@' ? l.label.substr(1) : l.label[0] == '.' ? parentName + l.label : l.label;
            if (l.label[0] != '.')
                parentName = full;
            for (const Statement& s : l.statements)
                if (IsDirective(s, ir::DirectiveKind::Defl))
                    redefinable.insert(full);
        }
    }

    // Names: every label under a name the target keeps apart (sjasmplus' main.loop -> MAIN_LO, a keyword renamed);
    // temporary labels numbered; the labels of a macro body get theirs per expansion
    {
        std::string parentName;
        std::map<std::string, int> temporaryCount;
        bool inMacro = false;
        for (size_t k = 0; k < program.lines.size(); ++k)
        {
            const ir::Line& l = program.lines[k];
            for (const Statement& s : l.statements)
                inMacro = inMacro || IsDirective(s, ir::DirectiveKind::Macro);
            if (!l.label.empty() && !inMacro)
            {
                if (IsTemporary(l.label))
                    temporaries[l.label].push_back({k, Give("T" + l.label + "_" + std::to_string(++temporaryCount[l.label]))});
                else
                {
                    const std::string full = l.label[0] == '@' ? l.label.substr(1) : l.label[0] == '.' ? parentName + l.label : l.label;
                    if (l.label[0] != '.')
                        parentName = full;
                    if (!names.count(full) && !redefinable.count(full))
                    {
                        const std::string name = Give(full);
                        if (z80::Upper(name) != z80::Upper(full))
                            diagnostics.push_back({Severity::Info, 0, 0, "label " + full + " written as " + Shown(name)});
                        names[full] = name;
                    }
                }
            }
            for (const Statement& s : l.statements)
                if (IsDirective(s, ir::DirectiveKind::EndMacro))
                    inMacro = false;
        }
    }

    Range(program.lines, 0, program.lines.size(), SIZE_MAX);
}
}  // namespace unrealasm::dialects
