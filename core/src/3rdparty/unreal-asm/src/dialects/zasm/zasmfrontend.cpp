#include "dialects/zasm/zasmfrontend.h"

#include "dialects/common/textfrontend.h"
#include "dialects/zasm/zasmcompound.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Op;
using namespace text;

const TextDialect& Zasm()
{
    static const TextDialect dialect = [] {
        TextDialect d;
        d.id = "zasm";
        ExprRules& r = d.rules;
        // The manual's list, tightest first: monadic · shifts · & | ^ (together) · * / % · + - · comparisons · && ||
        r.binary = {
            {"&&", Op::LogicalAnd, 3}, {"||", Op::LogicalOr, 3},
            {"==", Op::Equal, 4},      {"=", Op::Equal, 4},       {"eq", Op::Equal, 4},      {"!=", Op::NotEqual, 4},   {"<>", Op::NotEqual, 4},
            {"ne", Op::NotEqual, 4},   {"<", Op::Less, 4},        {"lt", Op::Less, 4},       {"<=", Op::LessEqual, 4},  {"le", Op::LessEqual, 4},
            {">", Op::Greater, 4},     {"gt", Op::Greater, 4},    {">=", Op::GreaterEqual, 4}, {"ge", Op::GreaterEqual, 4},
            {"+", Op::Add, 5},         {"-", Op::Sub, 5},
            {"*", Op::Mul, 6},         {"/", Op::Div, 6},         {"%", Op::Mod, 6},
            {"&", Op::And, 7},         {"and", Op::And, 7},       {"|", Op::Or, 7},          {"or", Op::Or, 7},         {"^", Op::Xor, 7},
            {"xor", Op::Xor, 7},       {"<<", Op::Shl, 8},        {"shl", Op::Shl, 8},       {">>", Op::Shr, 8},        {"shr", Op::Shr, 8},
        };
        r.unary = {{"-", Op::Negate, 9}, {"+", Op::Plus, 9}, {"~", Op::Not, 9}, {"!", Op::LogicalNot, 9}};
        r.dollarHex = r.ampersandBase = r.percentBinary = r.zeroB = true;
        r.dotInNames = r.atInNames = r.questionInNames = false;
        r.functions = {{"hi", Op::High}, {"lo", Op::Low}};
        r.hashImmediate = true;
        r.backslashEscapes = false;   // special characters in strings cannot be escaped
        r.stringLastCharOps = true;
        d.labelStyle = LabelStyle::ColumnZero;
        d.hashPrefixedCommands = true;
        d.dotCommands = true;
        d.setIsDefl = true;
        d.shebangLine = true;
        d.macroParamTags = "#&";
        d.statementSeparator = '\\';
        d.numericLocals = true;
        d.expandInstruction = ExpandZasmCompound;
        d.expressionBits = 16;
        d.unsignedArithmetic = false;
        d.trueValue = 1;
        const auto add = [&](const char* name, DirectiveKind kind, Special special = Special::None) { d.commands[name] = Command{kind, special}; };
        add("org", DirectiveKind::Org);
        for (const char* n : {"equ", "="})
            add(n, DirectiveKind::Equ);
        add("defl", DirectiveKind::Defl);
        for (const char* n : {"db", "defb", "byte"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"dm", "defm", "text", "ascii"})
            add(n, DirectiveKind::Db);
        for (const char* n : {"dw", "defw", "word"})
            add(n, DirectiveKind::Dw);
        for (const char* n : {"ds", "defs", "block"})
            add(n, DirectiveKind::Ds);
        add("end", DirectiveKind::End);
        add("include", DirectiveKind::Include);
        for (const char* n : {"incbin", "insert"})
            add(n, DirectiveKind::Incbin);
        add("if", DirectiveKind::If);
        add("elif", DirectiveKind::Else, Special::ElseIf);
        add("else", DirectiveKind::Else);
        add("endif", DirectiveKind::EndIf);
        add("macro", DirectiveKind::Macro, Special::Macro);
        for (const char* n : {"endm", "edup", "endr"})
            add(n, DirectiveKind::EndMacro, Special::EndBlock);
        for (const char* n : {"rept", "dup"})
            add(n, DirectiveKind::Repeat, Special::Repeat);
        add("local", DirectiveKind::LocalBlock, Special::Proc);
        add("endlocal", DirectiveKind::EndLocalBlock, Special::EndProc);
        add("define", DirectiveKind::Equ, Special::DefineSymbols);
        add("phase", DirectiveKind::Disp);
        add("dephase", DirectiveKind::Ent);
        add("code", DirectiveKind::Org, Special::Segment);
        add("asciz", DirectiveKind::Db, Special::AsciiZ);
        add("z80n", DirectiveKind::Other, Special::Z80n);
        for (const char* n : {"z80", "z180", "8080", "ixcbr2", "ixcbxh"})
            add(n, DirectiveKind::Other, Special::Ignore);
        for (const char* n : {"target", "cflags", "charset", "compress"})
            add(n, DirectiveKind::Other, Special::Text);
        for (const char* n : {"data", "assert", "align", "global", "globl", "#test", "list", "nolist", "area", "long", "error"})
            add(n, DirectiveKind::Other, Special::Text);
        return d;
    }();
    return dialect;
}
}  // namespace

FrontendResult ZasmFrontend::Parse(const SourceDocument& source) const
{
    return ParseText(source, Zasm());
}
}  // namespace unrealasm::dialects
