#include "dialects/common/chainwriter.h"

#include <algorithm>

#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
std::string ChainWriter::NumberText(const Expr& e) const
{
    const int64_t v = e.value & 0xFFFF;
    switch (e.spelling)
    {
        case ir::NumberSpelling::Hex: return "#" + z80::HexDigits(static_cast<uint64_t>(v), std::clamp(e.digits, 1, 4));
        case ir::NumberSpelling::Binary:
        {
            std::string bits;
            for (int k = std::clamp(e.digits, 1, 16) - 1; k >= 0; --k)
                bits.push_back(((v >> k) & 1) ? '1' : '0');
            return "%" + bits;
        }
        case ir::NumberSpelling::Character:
            if (e.text.size() == 1 && InQuotes(e.text[0]))
                return "\"" + e.text + "\"";
            return Number(v);
        default: return std::to_string(v);
    }
}

void ChainWriter::Multiplied(std::optional<int64_t> a, std::optional<int64_t> b)
{
    if (!MultiplicationAgrees(a, b))
        Problem("a multiplication " + Title() + " makes otherwise");
}

std::optional<std::string> ChainWriter::Atom(const Expr& e)
{
    const Expr& u = Unwrapped(e);
    if (u.kind == Expr::Kind::Number)
        return NumberText(u);
    if (u.kind == Expr::Kind::Symbol || u.kind == Expr::Kind::Current || u.kind == Expr::Kind::Raw)
        return SymbolText(u);
    if (const auto v = Value(u))
        return Number(*v);
    return std::nullopt;
}

bool ChainWriter::IsAtom(const Expr& e)
{
    const Expr& u = Unwrapped(e);
    return u.kind == Expr::Kind::Number || u.kind == Expr::Kind::Symbol || u.kind == Expr::Kind::Current ||
           (u.kind == Expr::Kind::Raw && u.text.size() >= 2);
}

std::optional<std::string> ChainWriter::Text(const Expr& e)
{
    switch (e.kind)
    {
        case Expr::Kind::Number: case Expr::Kind::Symbol: case Expr::Kind::Current: case Expr::Kind::Raw: return Atom(e);
        case Expr::Kind::Group: return Text(e.args[0]);
        case Expr::Kind::Unary:
        {
            const auto inner = Text(e.args[0]);
            switch (e.op)
            {
                case Op::Plus: return inner;
                case Op::Negate:
                    if (IsAtom(e.args[0]))
                        if (const auto a = Atom(e.args[0]))
                            return "0-" + *a;
                    Multiplied(Value(e.args[0]), 0xFFFF);
                    return inner && OperatorSymbol(Op::Mul) ? std::optional<std::string>(*inner + "*#FFFF") : std::nullopt;   // -x = x*#FFFF in 16 bits
                case Op::Not:
                    if (IsAtom(e.args[0]))
                        if (const auto a = Atom(e.args[0]))
                            return "#FFFF-" + *a;   // ~x = #FFFF-x
                    if (const char* x = OperatorSymbol(Op::Xor); x && inner)
                        return *inner + Guard(*inner, x) + x + "#FFFF";
                    Multiplied(Value(e.args[0]), 0xFFFF);
                    return inner && OperatorSymbol(Op::Mul) ? std::optional<std::string>(*inner + "*#FFFF+#FFFF") : std::nullopt;   // ~x = -x-1
                case Op::High: return inner ? std::optional<std::string>(HighOf(*inner)) : std::nullopt;
                case Op::Low:
                    if (const char* a = OperatorSymbol(Op::And); a && inner)
                        return *inner + Guard(*inner, a) + a + "255";
                    Multiplied(Value(e.args[0]), 256);
                    return inner && OperatorSymbol(Op::Mul) && OperatorSymbol(Op::Div) ? std::optional<std::string>(*inner + "*256/256") : std::nullopt;
                default: return std::nullopt;
            }
        }
        case Expr::Kind::Binary: return Binary(e);
        default: return std::nullopt;
    }
}

std::optional<std::string> ChainWriter::Binary(const Expr& e)
{
    const auto text = Chained(e);
    if (text && ExpressionWidth() && text->size() > ExpressionWidth() && Value(e))
        Problem("too long for " + Title() + "'s line");   // written as its value
    return text;
}

std::optional<std::string> ChainWriter::Chained(const Expr& e)
{
    const Expr& a = e.args[0];
    const Expr& b = Unwrapped(e.args[1]);
    const auto join = [&](const char* symbol, const Expr& left, const Expr& right) -> std::optional<std::string> {
        const auto l = Text(left);
        const auto r = Atom(right);
        if (!l || !r)
            return std::nullopt;
        if (e.op == Op::Mul)
            Multiplied(Value(left), Value(right));   // in the order written: the target's multiplier is on the right
        return *l + Guard(*l, symbol) + symbol + *r;
    };
    const char* symbol = OperatorSymbol(e.op);
    switch (e.op)
    {
        case Op::Add: case Op::Sub: case Op::Mul: case Op::Xor: case Op::And: case Op::Or:
        {
            if (!symbol)
                return std::nullopt;
            if (IsAtom(b))
                return join(symbol, a, b);
            // a+(c+d) = a+c+d, a-(c+d) = a-c-d ...; a*(c*d), a&(c&d) ... alike
            if (b.kind == Expr::Kind::Binary && ((e.op == Op::Add || e.op == Op::Sub) && (b.op == Op::Add || b.op == Op::Sub)))
            {
                const Op first = e.op;
                const Op second = e.op == Op::Add ? b.op : (b.op == Op::Add ? Op::Sub : Op::Add);
                return Text(Expr::Binary(second, Expr::Binary(first, a, b.args[0]), b.args[1]));
            }
            if (b.kind == Expr::Kind::Binary && b.op == e.op && e.op != Op::Add && e.op != Op::Sub)
                return Text(Expr::Binary(e.op, Expr::Binary(e.op, a, b.args[0]), b.args[1]));
            // the commuting operators: the atom goes to the right
            if (e.op != Op::Sub && IsAtom(a))
                return join(symbol, b, a);
            if (e.op == Op::Sub && IsAtom(a))
            {
                const auto r = Text(b);
                const auto l = Atom(a);
                Multiplied(Value(b), 0xFFFF);
                if (r && l && OperatorSymbol(Op::Mul))
                    return *r + "*#FFFF+" + *l;   // a-b = (b*#FFFF)+a
            }
            return join(symbol, a, b);   // the right side folded to its value, if known
        }
        case Op::Div: case Op::Mod:
            if (!symbol)
                return std::nullopt;
            if (!DivisionAgrees(a, b))
                Problem("a division " + Title() + " makes in its own 16-bit words");
            return join(symbol, a, b);
        case Op::Shl: case Op::Shr: case Op::ShrUnsigned:
        {
            const auto shift = Value(b);
            if (!shift || *shift < 0 || *shift > 15 || !OperatorSymbol(e.op == Op::Shl ? Op::Mul : Op::Div))
                return std::nullopt;
            if (e.op != Op::Shl && !ShiftAgrees(a))
                Problem("a right shift " + Title() + " would make as a division");
            if (e.op == Op::Shl)
                Multiplied(Value(a), int64_t{1} << *shift);
            const auto l = Text(a);
            return l ? std::optional<std::string>(*l + (e.op == Op::Shl ? "*" : "/") + Number(int64_t{1} << *shift)) : std::nullopt;
        }
        default: return std::nullopt;
    }
}
}  // namespace unrealasm::dialects
