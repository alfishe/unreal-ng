#include "dialects/pasmo/pasmofrontend.h"

#include <algorithm>

#include "dialects/common/textfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Op;
using namespace text;

const TextDialect& Pasmo()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "pasmo";
        ExprRules& r = d.rules;
        // Priorities, lowest first (the manual's table, read bottom up): ? · HIGH LOW · || · && · OR | XOR · AND & · unary ·
        // comparisons · + - · * / MOD SHL SHR
        r.binary = {
            {"||", Op::LogicalOr, 2},   {"&&", Op::LogicalAnd, 3}, {"or", Op::Or, 4},         {"|", Op::Or, 4},          {"xor", Op::Xor, 4},
            {"and", Op::And, 5},        {"&", Op::And, 5},         {"eq", Op::Equal, 7},     {"=", Op::Equal, 7},       {"ne", Op::NotEqual, 7},
            {"!=", Op::NotEqual, 7},    {"lt", Op::Less, 7},       {"<", Op::Less, 7},       {"le", Op::LessEqual, 7},  {"<=", Op::LessEqual, 7},
            {"gt", Op::Greater, 7},     {">", Op::Greater, 7},     {"ge", Op::GreaterEqual, 7}, {">=", Op::GreaterEqual, 7},
            {"+", Op::Add, 8},          {"-", Op::Sub, 8},         {"*", Op::Mul, 9},        {"/", Op::Div, 9},         {"mod", Op::Mod, 9},
            {"%", Op::Mod, 9},          {"shl", Op::Shl, 9},       {"<<", Op::Shl, 9},       {"shr", Op::Shr, 9},       {">>", Op::Shr, 9},
        };
        r.unary = {
            {"not", Op::Not, 6}, {"~", Op::Not, 6}, {"!", Op::LogicalNot, 6}, {"+", Op::Plus, 6}, {"-", Op::Negate, 6},
            {"high", Op::High, 1}, {"low", Op::Low, 1},
        };
        r.dollarHex = r.hashHex = r.ampersandBase = r.percentBinary = true;
        r.dollarInDigits = true;
        r.dollarInNames = true;   // a $ inside an identifier is ignored (call$msg is callmsg)
        r.definedOperator = true;
        r.bracketIndirect = true;
        d.reserved = {"nul", "defined"};
        d.lineNumbers = true;
        d.nameFilter = [](std::string name) {
            name.erase(std::remove(name.begin(), name.end(), '$'), name.end());
            return name;
        };
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        add("equ", DirectiveKind::Equ);
        add("defl", DirectiveKind::Defl);
        for (const char* n : {"db", "defb", "defm"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"dw", "defw"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"ds", "defs"})
            add(n, DirectiveKind::Ds);
        add("end", DirectiveKind::End);
        add("include", DirectiveKind::Include);
        add("incbin", DirectiveKind::Incbin);
        add("if", DirectiveKind::If);
        add("ifdef", DirectiveKind::If, Special::IfDef);
        add("ifndef", DirectiveKind::If, Special::IfNdef);
        add("else", DirectiveKind::Else);
        add("endif", DirectiveKind::EndIf);
        add("macro", DirectiveKind::Macro, Special::Macro);
        add("endm", DirectiveKind::EndMacro, Special::EndBlock);
        add("rept", DirectiveKind::Repeat, Special::Repeat);
        add("proc", DirectiveKind::LocalBlock, Special::Proc);
        add("endp", DirectiveKind::EndLocalBlock, Special::EndProc);
        add("local", DirectiveKind::LocalBlock, Special::Local);
        // Directives pasmo has and the IR does not: kept as text
        for (const char* n : {"public", "irp", "exitm", ".shift", ".error", ".warning"})
            add(n, DirectiveKind::Other, Special::Text);
        return d;
    }();
    return dialect;
}
}  // namespace

FrontendResult PasmoFrontend::Parse(const SourceDocument& source) const
{
    return ParseText(source, Pasmo());
}
}  // namespace unrealasm::dialects
