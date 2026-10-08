#include "dialects/odin/odinfrontend.h"

#include "dialects/common/textfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Op;
using namespace text;

const TextDialect& Odin()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "odin";
        ExprRules& r = d.rules;
        r.binary = {
            {"||", Op::LogicalOr, 1}, {"&&", Op::LogicalAnd, 2}, {"|", Op::Or, 3},        {"^", Op::Xor, 4},          {"&", Op::And, 5},
            {"==", Op::Equal, 6},     {"!=", Op::NotEqual, 6},   {"<", Op::Less, 7},      {"<=", Op::LessEqual, 7},   {">", Op::Greater, 7},
            {">=", Op::GreaterEqual, 7}, {"<<", Op::Shl, 8},     {">>", Op::Shr, 8},      {"+", Op::Add, 9},          {"-", Op::Sub, 9},
            {"*", Op::Mul, 10},       {"/", Op::Div, 10},        {"%", Op::Mod, 10},      {"mod", Op::Mod, 10},
        };
        r.unary = {{"-", Op::Negate, 11}, {"+", Op::Plus, 11}, {"~", Op::Not, 11}, {"!", Op::LogicalNot, 11}};
        r.dollarHex = r.percentBinary = true;
        r.zeroX = r.suffixes = true;
        r.suffixOctalDecimal = false;
        d.labelStyle = LabelStyle::ColonOrColumnZero;
        d.z80nAlways = true;
        d.expressionBits = 16;
        d.unsignedArithmetic = false;
        d.trueValue = 1;
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        add("equ", DirectiveKind::Equ);
        for (const char* n : {"db", "defb"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"dw", "defw"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"ds", "defs"})
            add(n, DirectiveKind::Ds);
        for (const char* n : {"dz", "defz"})
            add(n, DirectiveKind::Db, Special::AsciiZ);
        for (const char* n : {"load", "include"})
            add(n, DirectiveKind::Include);
        for (const char* n : {"bin", "incbin"})
            add(n, DirectiveKind::Incbin);
        for (const char* n : {"opt", "save", "tab", "ent", "endt", "entb", "entc", "ents", "entw", "entz", "dc", "defc"})
            add(n, DirectiveKind::Other, Special::Text);
        return d;
    }();
    return dialect;
}
}  // namespace

FrontendResult OdinFrontend::Parse(const SourceDocument& source) const
{
    return ParseText(source, Odin());
}
}  // namespace unrealasm::dialects
