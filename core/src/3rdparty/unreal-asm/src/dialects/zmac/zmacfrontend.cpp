#include "dialects/zmac/zmacfrontend.h"

#include "dialects/common/textfrontend.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Op;
using namespace text;

const TextDialect& Zmac()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "zmac";
        ExprRules& r = d.rules;
        // C priorities with the word synonyms: || && | or ^ xor & and == = eq != <> ne < lt <= le > gt >= ge << shl >> shr + - * / % mod
        r.binary = {
            {"||", Op::LogicalOr, 1}, {"&&", Op::LogicalAnd, 2}, {"|", Op::Or, 3},       {"or", Op::Or, 3},         {"^", Op::Xor, 4},
            {"xor", Op::Xor, 4},      {"&", Op::And, 5},         {"and", Op::And, 5},    {"==", Op::Equal, 6},      {"=", Op::Equal, 6},
            {"eq", Op::Equal, 6},     {"!=", Op::NotEqual, 6},   {"<>", Op::NotEqual, 6}, {"ne", Op::NotEqual, 6},   {"<", Op::Less, 7},
            {"lt", Op::Less, 7},      {"<=", Op::LessEqual, 7},  {"le", Op::LessEqual, 7}, {">", Op::Greater, 7},    {"gt", Op::Greater, 7},
            {">=", Op::GreaterEqual, 7}, {"ge", Op::GreaterEqual, 7}, {"<<", Op::Shl, 8}, {"shl", Op::Shl, 8},      {">>", Op::Shr, 8},
            {"shr", Op::Shr, 8},      {"+", Op::Add, 9},         {"-", Op::Sub, 9},      {"*", Op::Mul, 10},        {"/", Op::Div, 10},
            {"%", Op::Mod, 10},       {"mod", Op::Mod, 10},
        };
        r.unary = {{"!", Op::LogicalNot, 11}, {"~", Op::Not, 11}, {"not", Op::Not, 11}, {"+", Op::Plus, 11}, {"-", Op::Negate, 11},
                   {"low", Op::Low, 11},      {"high", Op::High, 11}};
        r.dollarHex = true;
        r.twoCharsLowFirst = true;
        r.dotInNames = true;
        r.questionInNames = r.atInNames = true;
        d.labelStyle = LabelStyle::ColonOrColumnZero;
        d.dotCommands = true;
        d.statementSeparator = '\\';
        d.expressionBits = 16;
        d.unsignedArithmetic = false;
        d.trueValue = 1;
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        add("equ", DirectiveKind::Equ);
        for (const char* n : {"defl", "aset", "="})
            add(n, DirectiveKind::Defl);
        for (const char* n : {"defb", "db", "ascii", "byte", "defm", "dm", "text"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"defw", "dw", "word"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"defs", "ds", "block", "rmem"})
            add(n, DirectiveKind::Ds);
        add("end", DirectiveKind::End);
        for (const char* n : {"include", "read", "import", "maclib"})
            add(n, DirectiveKind::Include);
        add("incbin", DirectiveKind::Incbin);
        for (const char* n : {"if", "cond"})
            add(n, DirectiveKind::If);
        add("ifdef", DirectiveKind::If, Special::IfDef);
        add("ifndef", DirectiveKind::If, Special::IfNdef);
        add("ifeq", DirectiveKind::If, Special::IfEqual);
        add("ifne", DirectiveKind::If, Special::IfNotEqual);
        add("iflt", DirectiveKind::If, Special::IfLess);
        add("ifgt", DirectiveKind::If, Special::IfGreater);
        add("else", DirectiveKind::Else);
        for (const char* n : {"endif", "endc"})
            add(n, DirectiveKind::EndIf);
        add("macro", DirectiveKind::Macro, Special::Macro);
        add("endm", DirectiveKind::EndMacro, Special::EndBlock);
        add("rept", DirectiveKind::Repeat, Special::Repeat);
        add("local", DirectiveKind::LocalBlock, Special::Local);
        add("phase", DirectiveKind::Disp);
        add("dephase", DirectiveKind::Ent);
        for (const char* n : {"z80", "8080", "z180", "list", "nolist", "title", "page", "name", "eject", "jperror", "jrpromote", "aseg", "cseg", "dseg"})
            add(n, DirectiveKind::Other, Special::Ignore);
        for (const char* n : {"public", "extern", "ext", "extrn", "global", "entry", "assert", "exitm", "def3", "d3", "defd", "dword", "dc", "common", "comment"})
            add(n, DirectiveKind::Other, Special::Text);
        return d;
    }();
    return dialect;
}
}  // namespace

FrontendResult ZmacFrontend::Parse(const SourceDocument& source) const
{
    return ParseText(source, Zmac());
}
}  // namespace unrealasm::dialects
