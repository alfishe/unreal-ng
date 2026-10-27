#include "dialects/rasm/rasmfrontend.h"

#include "dialects/common/textfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Op;
using namespace text;

const TextDialect& Rasm()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "rasm";
        ExprRules& r = d.rules;
        r.binary = {
            {"||", Op::LogicalOr, 1}, {"&&", Op::LogicalAnd, 2}, {"|", Op::Or, 3},       {"or", Op::Or, 3},         {"^", Op::Xor, 4},
            {"xor", Op::Xor, 4},      {"&", Op::And, 5},         {"and", Op::And, 5},    {"==", Op::Equal, 6},      {"!=", Op::NotEqual, 6},
            {"<>", Op::NotEqual, 6},  {"<", Op::Less, 7},        {"<=", Op::LessEqual, 7}, {">", Op::Greater, 7},   {">=", Op::GreaterEqual, 7},
            {"<<", Op::Shl, 8},       {">>", Op::Shr, 8},        {"+", Op::Add, 9},      {"-", Op::Sub, 9},         {"*", Op::Mul, 10},
            {"/", Op::Div, 10},       {"%%", Op::Mod, 10},       {"mod", Op::Mod, 10},
        };
        r.unary = {{"-", Op::Negate, 11}, {"+", Op::Plus, 11}, {"~", Op::Not, 11}, {"!", Op::LogicalNot, 11}};
        r.dollarHex = r.hashHex = r.percentBinary = r.zeroB = true;
        r.atOctal = true;
        r.suffixOctalDecimal = false;
        r.dotInNames = r.atInNames = r.questionInNames = true;
        r.functions = {{"hi", Op::High}, {"lo", Op::Low}};
        d.labelStyle = LabelStyle::ColonOrColumnZero;
        d.statementSeparator = ':';
        d.macroParenParams = true;
        d.macroLocalLabels = true;
        d.expressionBits = 32;
        d.unsignedArithmetic = false;
        d.trueValue = 1;
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        add("equ", DirectiveKind::Equ);
        add("=", DirectiveKind::Defl);
        for (const char* n : {"defb", "db", "defm", "dm", "byte", "text"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"defw", "dw", "word"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"defs", "ds"})
            add(n, DirectiveKind::Ds);
        add("end", DirectiveKind::End);
        add("include", DirectiveKind::Include);
        add("incbin", DirectiveKind::Incbin);
        add("if", DirectiveKind::If);
        add("ifnot", DirectiveKind::If, Special::IfNot);
        add("ifdef", DirectiveKind::If, Special::IfDef);
        add("ifndef", DirectiveKind::If, Special::IfNdef);
        add("else", DirectiveKind::Else);
        add("endif", DirectiveKind::EndIf);
        add("macro", DirectiveKind::Macro, Special::Macro);
        for (const char* n : {"mend", "endm"})
            add(n, DirectiveKind::EndMacro, Special::EndBlock);
        add("repeat", DirectiveKind::Repeat, Special::Repeat);
        for (const char* n : {"rend", "endr"})
            add(n, DirectiveKind::EndMacro, Special::EndBlock);
        for (const char* n : {"list", "nolist", "print", "break"})
            add(n, DirectiveKind::Other, Special::Ignore);
        for (const char* n : {"save", "bank", "bankset", "buildsna", "buildcpr", "snaset", "limit", "module", "write", "charset", "defr", "defi", "defb", "run",
                              "export", "section", "while", "wend", "until", "switch", "case", "default", "endswitch", "struct", "endstruct", "let", "enhance", "nocode",
                              "code", "lz4", "lz48", "lz49", "lzsa1", "lzsa2", "lzx7", "lzx0", "lzapu", "lzexo", "lzclose", "align", "fail", "stop", "assert", "undef",
                              "timer", "tickerstart", "tickerstop", "pushsection", "popsection"})
            if (!d.commands.count(n))
                add(n, DirectiveKind::Other, Special::Text);
        return d;
    }();
    return dialect;
}
}  // namespace

FrontendResult RasmFrontend::Parse(const SourceDocument& source) const
{
    return ParseText(source, Rasm());
}
}  // namespace unrealasm::dialects
