#include "dialects/fantasm/fantasmfrontend.h"

#include "dialects/common/textfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Op;
using namespace text;

const TextDialect& Fantasm()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "fantasm";
        ExprRules& r = d.rules;
        // C priorities: || && | ^ & == != < <= > >= << >> + - * / %
        r.binary = {
            {"||", Op::LogicalOr, 1}, {"&&", Op::LogicalAnd, 2}, {"|", Op::Or, 3},        {"^", Op::Xor, 4},          {"&", Op::And, 5},
            {"==", Op::Equal, 6},     {"!=", Op::NotEqual, 6},   {"<", Op::Less, 7},      {"<=", Op::LessEqual, 7},   {">", Op::Greater, 7},
            {">=", Op::GreaterEqual, 7}, {"<<", Op::Shl, 8},     {">>", Op::Shr, 8},      {"+", Op::Add, 9},          {"-", Op::Sub, 9},
            {"*", Op::Mul, 10},       {"/", Op::Div, 10},        {"%", Op::Mod, 10},
        };
        r.unary = {{"-", Op::Negate, 11}, {"+", Op::Plus, 11}, {"~", Op::Not, 11}, {"!", Op::LogicalNot, 11}};
        r.dollarHex = r.percentBinary = true;
        r.suffixOctalDecimal = false;
        r.dotInNames = true;
        r.atInNames = r.questionInNames = false;
        r.currentWord = "asmpc";
        d.labelStyle = LabelStyle::ColonOrColumnZero;
        d.hashPrefixedCommands = true;
        d.slashComments = true;
        d.statementSeparator = ':';
        d.macroParamsSpaceSeparated = true;
        d.expressionBits = 16;
        d.unsignedArithmetic = false;
        d.trueValue = 1;
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        for (const char* n : {"equ", "="})
            add(n, DirectiveKind::Equ);
        for (const char* n : {"db", "byte"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"dw", "word"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"ds", "block"})
            add(n, DirectiveKind::Ds);
        for (const char* n : {"dh", "hex"})
            add(n, DirectiveKind::Db, Special::HexBytes);
        add("dz", DirectiveKind::Db, Special::AsciiZ);
        add("end", DirectiveKind::End);
        add("include", DirectiveKind::Include);
        for (const char* n : {"incbin", "binary"})
            add(n, DirectiveKind::Incbin);
        add("if", DirectiveKind::If);
        add("ifdef", DirectiveKind::If, Special::IfDef);
        add("ifndef", DirectiveKind::If, Special::IfNdef);
        add("else", DirectiveKind::Else);
        add("endif", DirectiveKind::EndIf);
        add("macro", DirectiveKind::Macro, Special::Macro);
        add("endm", DirectiveKind::EndMacro, Special::EndBlock);
        for (const char* n : {"struct", "enum"})
            add(n, DirectiveKind::Other, Special::RawBlock);
        for (const char* n : {"global", "!opt", "!message", "pragma"})
            add(n, DirectiveKind::Other, Special::Text);
        d.reserved = {"asmpc"};
        return d;
    }();
    return dialect;
}
}  // namespace

FrontendResult FantasmFrontend::Parse(const SourceDocument& source) const
{
    return ParseText(source, Fantasm());
}
}  // namespace unrealasm::dialects
