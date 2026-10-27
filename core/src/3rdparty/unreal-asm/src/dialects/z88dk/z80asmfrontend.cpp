#include "dialects/z88dk/z80asmfrontend.h"

#include "dialects/common/textfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Op;
using namespace text;

const TextDialect& Z80asm()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "z80asm";
        ExprRules& r = d.rules;
        // C priorities, lowest first: || && | ^ & == != < <= > >= << >> + - * / %; the unary operators bind tightest
        r.binary = {
            {"||", Op::LogicalOr, 1}, {"&&", Op::LogicalAnd, 2}, {"|", Op::Or, 3},        {"^", Op::Xor, 4},          {"&", Op::And, 5},
            {"==", Op::Equal, 6},     {"!=", Op::NotEqual, 6},   {"<", Op::Less, 7},      {"<=", Op::LessEqual, 7},   {">", Op::Greater, 7},
            {">=", Op::GreaterEqual, 7}, {"<<", Op::Shl, 8},     {">>", Op::Shr, 8},      {"+", Op::Add, 9},          {"-", Op::Sub, 9},
            {"*", Op::Mul, 10},       {"/", Op::Div, 10},        {"%", Op::Mod, 10},
        };
        r.unary = {{"-", Op::Negate, 11}, {"+", Op::Plus, 11}, {"~", Op::Not, 11}, {"!", Op::LogicalNot, 11}};
        r.dollarHex = r.percentBinary = r.atBinary = r.zeroB = true;
        r.suffixOctalDecimal = false;
        r.doubleQuoteIsNumber = false;
        r.dotInNames = r.atInNames = r.questionInNames = false;
        r.currentWord = "asmpc";
        d.labelStyle = LabelStyle::ColonOrDot;
        d.hashPrefixedCommands = true;
        d.statementSeparator = '\\';
        r.escapesInSingleQuotes = true;
        d.macroParamsSpaceSeparated = true;
        d.expressionBits = 32;
        d.unsignedArithmetic = false;
        d.trueValue = 1;
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        for (const char* n : {"equ", "="})
            add(n, DirectiveKind::Equ);
        add("defl", DirectiveKind::Defl, Special::DefineList);
        add("defc", DirectiveKind::Equ, Special::DefineList);
        add("dc", DirectiveKind::Equ, Special::DefineList);
        add("define", DirectiveKind::Equ, Special::DefineSymbols);
        add("defgroup", DirectiveKind::Equ, Special::Group);
        add("defvars", DirectiveKind::Equ, Special::Vars);
        add("c_line", DirectiveKind::Other, Special::Ignore);
        add("line", DirectiveKind::Other, Special::Ignore);
        for (const char* n : {"db", "defb", "defm", "dm", "byte"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"dw", "defw", "word"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"ds", "defs"})
            add(n, DirectiveKind::Ds);
        add("end", DirectiveKind::End);
        add("include", DirectiveKind::Include);
        for (const char* n : {"incbin", "binary"})
            add(n, DirectiveKind::Incbin);
        add("if", DirectiveKind::If);
        add("ifdef", DirectiveKind::If, Special::IfDef);
        add("ifndef", DirectiveKind::If, Special::IfNdef);
        add("elif", DirectiveKind::Else, Special::ElseIf);
        add("else", DirectiveKind::Else);
        add("endif", DirectiveKind::EndIf);
        add("macro", DirectiveKind::Macro, Special::Macro);
        for (const char* n : {"endm", "endr"})
            add(n, DirectiveKind::EndMacro, Special::EndBlock);
        add("rept", DirectiveKind::Repeat, Special::Repeat);
        add("local", DirectiveKind::LocalBlock, Special::Local);
        // z80asm's linker, listing and preprocessing directives: kept as text
        for (const char* n : {"section", "module", "public", "extern", "global", "xdef", "xref", "xlib", "lib", "undef", "align", "assert",
                              "lstoff", "lston", "reptc", "repti", "exitm", "setfloat", "float"})
            add(n, DirectiveKind::Other, Special::Text);
        d.reserved = {"asmpc"};
        return d;
    }();
    return dialect;
}
}  // namespace

FrontendResult Z80asmFrontend::Parse(const SourceDocument& source) const
{
    return ParseText(source, Z80asm());
}
}  // namespace unrealasm::dialects
