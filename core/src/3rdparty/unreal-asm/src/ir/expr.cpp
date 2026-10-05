#include "unrealasm/ir.h"

namespace unrealasm::ir
{
Expr Expr::Number(int64_t value, NumberSpelling spelling, int digits)
{
    Expr e;
    e.kind = Kind::Number;
    e.value = value;
    e.spelling = spelling;
    e.digits = digits;
    return e;
}

Expr Expr::Symbol(std::string name)
{
    Expr e;
    e.kind = Kind::Symbol;
    e.text = std::move(name);
    return e;
}

Expr Expr::Make(Kind kind)
{
    Expr e;
    e.kind = kind;
    return e;
}

Expr Expr::Unary(Op op, Expr a)
{
    Expr e;
    e.kind = Kind::Unary;
    e.op = op;
    e.args.push_back(std::move(a));
    return e;
}

Expr Expr::Binary(Op op, Expr a, Expr b)
{
    Expr e;
    e.kind = Kind::Binary;
    e.op = op;
    e.args.push_back(std::move(a));
    e.args.push_back(std::move(b));
    return e;
}
}  // namespace unrealasm::ir
