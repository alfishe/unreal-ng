#include "dialects/common/textfrontend.h"

#include <algorithm>
#include <cctype>

#include "dialects/common/z80.h"

namespace unrealasm::dialects::text
{
using ir::DirectiveKind;
using ir::Expr;
using ir::Op;
using ir::Statement;

std::string DefaultFileName(const std::string& rest)
{
    const std::string t = Trim(rest);
    if (t.size() >= 2 && (t[0] == '"' || t[0] == '\''))
    {
        const size_t close = t.find(t[0], 1);
        return t.substr(1, close == std::string::npos ? std::string::npos : close - 1);
    }
    return t.substr(0, t.find_first_of(" \t"));
}

namespace
{
struct Block
{
    explicit Block(char blockKind, bool localOpen = false) : kind(blockKind), local(localOpen) {}

    char kind;           // 'M' macro, 'R' rept / irp, 'I' if, 'P' proc
    bool local;          // a LocalBlock is open in it (LOCAL, or the PROC itself)
    std::string loopVar; // REPT n,var,init,step
    std::string loopStep;
    int extraIfs = 0;    // ELIF branches opened inside an IF block: closed by its ENDIF
};

Statement Directive(DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

struct Engine
{
    const TextDialect& d;
    FrontendResult result;
    std::set<std::string> macros;
    std::vector<Block> blocks;
    uint32_t number = 0;
    bool z80n;
    int localScope = 0;   // numeric N$ labels: bumped by every ordinary label
    std::vector<std::string> macroParams;   // the parameters of the macro being defined, tags removed

    Expr varsEnd = Expr::Number(0);   ///< where the last DEFVARS block ended
    std::string varsEndName;

    /// DEFGROUP / DEFVARS blocks: the items of the lines up to the closing brace
    struct Group
    {
        enum Kind { None, Constants, Memory } kind = None;
        bool open = false;       // the brace has been seen
        bool haveNext = false;
        std::string previous;    // the last name defined
        Expr next;               // the value of the next name
    } group;

    Engine(const TextDialect& dialect, bool nextMode) : d(dialect), z80n(nextMode) {}

    Expr Parse(std::string_view text)
    {
        return ParseExpression(text, d.rules, number, result.diagnostics);
    }

    std::string Filter(std::string name) const { return d.nameFilter ? d.nameFilter(std::move(name)) : name; }

    const Command* Find(const std::string& lower) const
    {
        const auto found = d.commands.find(lower);
        return found == d.commands.end() ? nullptr : &found->second;
    }

    bool IsMnemonic(const std::string& lower) const { return z80::IsMnemonic(lower) || (z80n && z80::IsZ80nMnemonic(lower)); }

    bool Reserved(const std::string& word) const
    {
        const std::string lower = Lower(word);
        return IsMnemonic(lower) || z80::IsRegister(z80::NormalizeRegister(lower)) || z80::IsCondition(lower) || Find(lower) || d.reserved.count(lower);
    }

    Statement Other(const std::string& command, const std::string& rest) const
    {
        Statement s = Directive(DirectiveKind::Other);
        s.text = Upper(command) + (rest.empty() ? "" : " " + rest);
        return s;
    }

    /// Inside a macro body: TAGname -> name for the macro's own parameters
    std::string StripParamTags(const std::string& text) const
    {
        if (macroParams.empty() || d.macroParamTags.empty())
            return text;
        std::string out;
        for (size_t k = 0; k < text.size();)
        {
            if (d.macroParamTags.find(text[k]) != std::string::npos)
            {
                for (const std::string& p : macroParams)
                    if (text.compare(k + 1, p.size(), p) == 0 &&
                        (k + 1 + p.size() >= text.size() || !IsNameChar(text[k + 1 + p.size()], d.rules)))
                    {
                        out += p;
                        k += 1 + p.size();
                        goto next;
                    }
            }
            out += text[k++];
        next:;
        }
        return out;
    }

    /// N$ -> a name unique to the current stretch between ordinary labels; strings are left alone
    std::string RewriteNumericLocals(const std::string& text) const
    {
        const std::string mask = Mask(text, d.rules.escapesInSingleQuotes);
        std::string out;
        for (size_t k = 0; k < text.size();)
        {
            if (mask[k] == text[k] && std::isdigit(static_cast<unsigned char>(text[k])) && (k == 0 || !(std::isalnum(static_cast<unsigned char>(text[k - 1])) || text[k - 1] == '_')))
            {
                size_t e = k;
                while (e < text.size() && std::isdigit(static_cast<unsigned char>(text[e])))
                    ++e;
                if (e < text.size() && text[e] == '$' && mask[e] == '$' && (e + 1 >= text.size() || !(std::isalnum(static_cast<unsigned char>(text[e + 1])) || text[e + 1] == '_')))
                {
                    out += "__loc" + std::to_string(localScope) + "_" + text.substr(k, e - k);
                    k = e + 1;
                    continue;
                }
            }
            out += text[k++];
        }
        return out;
    }

    /// The items of a DEFGROUP / DEFVARS line; the closing brace ends the block
    void GroupItems(std::string items, std::vector<ir::Line>& out)
    {
        const auto define = [&](const std::string& name, Expr value) {
            ir::Line one;
            one.label = Filter(name);
            one.sourceLine = number;
            Statement equ = Directive(DirectiveKind::Equ);
            equ.args.push_back(std::move(value));
            one.statements.push_back(std::move(equ));
            out.push_back(std::move(one));
        };
        if (const std::string head = Lower(Trim(items)); head.rfind("c_line", 0) == 0 || head.rfind("line ", 0) == 0)
            return;   // debug information inside a block
        const size_t open = items.find('{');
        if (open != std::string::npos)
        {
            group.open = true;
            items.erase(open, 1);
        }
        else if (!Trim(items).empty() || group.open)
            group.open = true;
        const size_t close = items.find('}');
        bool closes = false;
        if (close != std::string::npos)
        {
            closes = true;
            items.erase(close);
        }
        if (group.kind == Group::Constants)
        {
            for (const std::string& item : Split(items, ',', d.rules.escapesInSingleQuotes))
            {
                if (item.empty())
                    continue;
                const size_t eq = item.find('=');
                const std::string name = Trim(std::string_view(item).substr(0, eq));
                Expr value;
                if (eq != std::string::npos)
                    value = Parse(Trim(std::string_view(item).substr(eq + 1)));
                else if (group.haveNext)
                    value = Expr::Binary(Op::Add, Expr::Symbol(Filter(group.previous)), Expr::Number(1));
                else
                    value = Expr::Number(0);
                define(name, std::move(value));
                group.previous = name;
                group.haveNext = true;
            }
        }
        else
        {
            const std::string t = Trim(items);
            if (!t.empty())
            {
                // name [ds.b|ds.w|ds.p|ds.q count]
                const size_t blank = t.find_first_of(" \t");
                const std::string name = t.substr(0, blank);
                define(name, group.next);
                if (blank != std::string::npos)
                {
                    const std::string spec = Trim(std::string_view(t).substr(blank));
                    const size_t gap = spec.find_first_of(" \t");
                    const std::string kind = Lower(spec.substr(0, gap));
                    const int size = kind == "ds.b" ? 1 : kind == "ds.w" ? 2 : kind == "ds.p" ? 3 : kind == "ds.q" ? 4 : 0;
                    if (size && gap != std::string::npos)
                        group.next = Expr::Binary(Op::Add, Expr::Symbol(Filter(name)),
                                                  Expr::Binary(Op::Mul, Expr::Number(size), Parse(Trim(std::string_view(spec).substr(gap)))));
                    else if (!size)
                        result.diagnostics.push_back({Severity::Error, number, 0, "DEFVARS item: expected name ds.b|w|p|q count"});
                }
            }
        }
        if (closes)
        {
            if (group.kind == Group::Memory)
            {
                varsEnd = group.next;
                varsEndName = "set";
            }
            group = Group();
        }
    }

    void Command_(const std::string& command, const std::string& rest, ir::Line& line, bool labelBeforeKeyword, std::vector<ir::Line>& before)
    {
        std::string lower = Lower(command);
        if (d.hashPrefixedCommands && lower.size() > 1 && lower[0] == '#' && Find(lower.substr(1)))
            lower.erase(0, 1);
        if (d.dotCommands && lower.size() > 1 && lower[0] == '.' && !Find(lower) && Find(lower.substr(1)))
            lower.erase(0, 1);
        const Command* c = Find(lower);
        if (!c && d.setIsDefl && lower == "set" && labelBeforeKeyword && !line.label.empty() && Split(rest, ',', d.rules.escapesInSingleQuotes).size() == 1)
        {
            Statement defl = Directive(DirectiveKind::Defl);
            defl.args.push_back(Parse(rest));
            line.statements.push_back(std::move(defl));
            return;
        }
        if (!c)
        {
            Statement s;
            if (IsMnemonic(lower))
            {
                s.kind = Statement::Kind::Instruction;
                s.mnemonic = lower;
                const std::vector<std::string> ops = Split(rest, ',', d.rules.escapesInSingleQuotes);
                for (size_t k = 0; k < ops.size(); ++k)
                {
                    const bool condition = k == 0 && z80::TakesCondition(lower) && (ops.size() > 1 || lower == "ret");
                    s.operands.push_back(InstructionOperand(ops[k], condition, lower == "in" || lower == "out", d.rules, number, result.diagnostics));
                }
            }
            else
            {
                s.kind = Statement::Kind::MacroCall;
                s.mnemonic = command;
                s.params = Split(rest, ',', d.rules.escapesInSingleQuotes);
            }
            std::vector<Statement> expanded;
            if (s.kind == Statement::Kind::Instruction && d.expandInstruction && d.expandInstruction(s, expanded))
            {
                for (Statement& one : expanded)
                    line.statements.push_back(std::move(one));
                return;
            }
            line.statements.push_back(std::move(s));
            return;
        }
        Statement s = Directive(c->kind);
        switch (c->special)
        {
            case Special::Text:
                line.statements.push_back(Other(command, rest));
                if (lower == "irp" || lower == "irpc" || lower == "repti" || lower == "reptc")
                    blocks.push_back(Block('R'));
                return;
            case Special::Proc:
                s.directive = DirectiveKind::LocalBlock;
                blocks.push_back(Block('P', true));
                break;
            case Special::EndProc:
                s.directive = DirectiveKind::EndLocalBlock;
                if (!blocks.empty() && blocks.back().kind == 'P')
                    blocks.pop_back();
                break;
            case Special::Local:
            {
                size_t at = blocks.size();
                while (at > 0 && blocks[at - 1].kind == 'I')
                    --at;
                if (at == 0 || blocks[at - 1].local)
                    return;
                blocks[at - 1].local = true;
                s.directive = DirectiveKind::LocalBlock;
                break;
            }
            case Special::Macro:
            {
                std::string macroRest = rest;
                if (d.macroParenParams)
                {
                    // MACRO name (a, b) -> MACRO name, a, b
                    const size_t open = macroRest.find('(');
                    if (open != std::string::npos)
                    {
                        const size_t close = macroRest.rfind(')');
                        macroRest = Trim(std::string_view(macroRest).substr(0, open)) + "," +
                                    macroRest.substr(open + 1, (close == std::string::npos ? macroRest.size() : close) - open - 1);
                    }
                }
                std::vector<std::string> parts = Split(macroRest, ',', d.rules.escapesInSingleQuotes);
                if (labelBeforeKeyword && d.macroNameFirst && !line.label.empty())
                {
                    s.text = line.label;   // name MACRO params
                    line.label.clear();
                }
                else
                {
                    if (parts.empty() || !d.macroKeywordFirst)
                    {
                        result.diagnostics.push_back({Severity::Error, number, 0, "MACRO without a name"});
                        return;
                    }
                    s.text = parts[0];
                    parts.erase(parts.begin());
                    if (d.macroParamsSpaceSeparated)
                    {
                        // the name is the first word; the rest of that item is the first parameters
                        const size_t blank = s.text.find_first_of(" \t");
                        if (blank != std::string::npos)
                        {
                            parts.insert(parts.begin(), Trim(std::string_view(s.text).substr(blank)));
                            s.text = s.text.substr(0, blank);
                        }
                    }
                }
                for (std::string& p : parts)
                {
                    if (!d.macroParamTags.empty() && !p.empty() && d.macroParamTags.find(p[0]) != std::string::npos)
                        p.erase(0, 1);
                    if (d.macroParamsSpaceSeparated)
                    {
                        // MACRO name a b c: blanks separate as well
                        size_t from = 0;
                        while (from < p.size())
                        {
                            const size_t blank = p.find_first_of(" \t", from);
                            const std::string word = p.substr(from, blank == std::string::npos ? std::string::npos : blank - from);
                            if (!word.empty())
                                s.params.push_back(word);
                            if (blank == std::string::npos)
                                break;
                            from = p.find_first_not_of(" \t", blank);
                            if (from == std::string::npos)
                                break;
                        }
                    }
                    else if (!p.empty())
                        s.params.push_back(p);
                }
                macros.insert(s.text);
                macroParams = s.params;
                blocks.push_back(Block('M'));
                if (d.macroLocalLabels)
                {
                    blocks.back().local = true;
                    line.statements.push_back(std::move(s));
                    line.statements.push_back(Directive(DirectiveKind::LocalBlock));
                    return;
                }
                break;
            }
            case Special::EndBlock:
                // ends the macro / REPT block and every IF open inside it
                while (!blocks.empty() && blocks.back().kind == 'I')
                {
                    line.statements.push_back(Directive(DirectiveKind::EndIf));
                    blocks.pop_back();
                }
                if (!blocks.empty() && blocks.back().kind == 'R')
                {
                    s.directive = DirectiveKind::EndRepeat;
                    if (!blocks.back().loopVar.empty())
                    {
                        // the loop variable steps at the end of every pass
                        ir::Line step;
                        step.label = blocks.back().loopVar;
                        step.sourceLine = number;
                        Statement defl = Directive(DirectiveKind::Defl);
                        defl.args.push_back(Expr::Binary(Op::Add, Expr::Symbol(blocks.back().loopVar), Parse(blocks.back().loopStep)));
                        step.statements.push_back(std::move(defl));
                        before.push_back(std::move(step));
                    }
                }
                if (!blocks.empty() && blocks.back().local)
                    line.statements.push_back(Directive(DirectiveKind::EndLocalBlock));
                if (!blocks.empty() && blocks.back().kind == 'M')
                    macroParams.clear();
                if (!blocks.empty())
                    blocks.pop_back();
                break;
            case Special::Repeat:
            {
                const std::vector<std::string> parts = Split(rest, ',', d.rules.escapesInSingleQuotes);
                Block block('R');
                if (parts.size() > 1)
                {
                    // REPT n, var [, init [, step]]: var counts from init (0) by step (1)
                    block.loopVar = Filter(parts[1]);
                    block.loopStep = parts.size() > 3 && !parts[3].empty() ? parts[3] : "1";
                    ir::Line init;
                    init.label = block.loopVar;
                    init.sourceLine = number;
                    Statement defl = Directive(DirectiveKind::Defl);
                    defl.args.push_back(Parse(parts.size() > 2 && !parts[2].empty() ? parts[2] : "0"));
                    init.statements.push_back(std::move(defl));
                    before.push_back(std::move(init));
                }
                s.args.push_back(Parse(parts.empty() ? rest : parts[0]));
                blocks.push_back(block);
                break;
            }
            case Special::ElseIf:
            {
                // ELSE and a nested IF; the ENDIF of the chain closes both
                s.directive = DirectiveKind::Else;
                line.statements.push_back(std::move(s));
                Statement nested = Directive(DirectiveKind::If);
                nested.args.push_back(Parse(rest));
                line.statements.push_back(std::move(nested));
                for (size_t k = blocks.size(); k > 0; --k)
                    if (blocks[k - 1].kind == 'I')
                    {
                        ++blocks[k - 1].extraIfs;
                        break;
                    }
                return;
            }
            case Special::DefineList:
            {
                const std::vector<std::string> items = Split(rest, ',', d.rules.escapesInSingleQuotes);
                if (c->kind == DirectiveKind::Defl && (items.empty() || items[0].find('=') == std::string::npos))
                {
                    // DEFL expr: the label of the line takes the value
                    s.args.push_back(Parse(rest));
                    break;
                }
                for (const std::string& item : items)
                {
                    const size_t eq = item.find('=');
                    if (eq == std::string::npos)
                    {
                        result.diagnostics.push_back({Severity::Error, number, 0, "DEFC needs name = value"});
                        continue;
                    }
                    ir::Line one;
                    one.label = Filter(Trim(std::string_view(item).substr(0, eq)));
                    one.sourceLine = number;
                    Statement def = Directive(c->kind);
                    def.args.push_back(Parse(Trim(std::string_view(item).substr(eq + 1))));
                    one.statements.push_back(std::move(def));
                    before.push_back(std::move(one));
                }
                return;
            }
            case Special::DefineSymbols:
            {
                const std::vector<std::string> names = Split(rest, ',', d.rules.escapesInSingleQuotes);
                const auto plain = [&](const std::string& n) {
                    return !n.empty() && IsNameStart(n[0], d.rules) && std::all_of(n.begin(), n.end(), [&](char ch) { return IsNameChar(ch, d.rules); });
                };
                if (names.size() == 1 && !plain(names[0]))
                {
                    // #define NAME value: a constant when the value is an expression; a macro with parameters or text stays text
                    const size_t blank = names[0].find_first_of(" \t");
                    const std::string name = names[0].substr(0, blank);
                    if (Find(Lower(name)) || (d.dotCommands && Find(Lower(name).substr(name[0] == '.' ? 1 : 0))))
                        return;   // #define equ .equ: an alias of a pseudo instruction, which the dialect knows anyway
                    if (blank != std::string::npos && plain(name))
                    {
                        Expr value = Parse(Trim(std::string_view(names[0]).substr(blank)));
                        if (value.kind != Expr::Kind::Raw)
                        {
                            ir::Line one;
                            one.label = Filter(name);
                            one.sourceLine = number;
                            Statement def = Directive(DirectiveKind::Equ);
                            def.args.push_back(std::move(value));
                            one.statements.push_back(std::move(def));
                            before.push_back(std::move(one));
                            return;
                        }
                    }
                    line.statements.push_back(Other(command, rest));
                    return;
                }
                for (const std::string& name : names)
                {
                    ir::Line one;
                    one.label = Filter(name);
                    one.sourceLine = number;
                    Statement def = Directive(DirectiveKind::Equ);
                    def.args.push_back(Expr::Number(1));
                    one.statements.push_back(std::move(def));
                    before.push_back(std::move(one));
                }
                return;
            }
            case Special::Group:
            case Special::Vars:
            {
                group = Group();
                group.kind = c->special == Special::Group ? Group::Constants : Group::Memory;
                std::string items = rest;
                if (group.kind == Group::Memory)
                {
                    const size_t brace = rest.find('{');
                    group.next = Parse(Trim(std::string_view(rest).substr(0, brace)));
                    group.haveNext = true;
                    // -1: continue from where the last block ended (0 for the first)
                    if (group.next.kind == Expr::Kind::Unary && group.next.op == Op::Negate && group.next.args[0].kind == Expr::Kind::Number &&
                        group.next.args[0].value == 1)
                        group.next = varsEnd.kind == Expr::Kind::Number && varsEnd.value == 0 && varsEndName.empty() ? Expr::Number(0) : varsEnd;
                    items = brace == std::string::npos ? "" : rest.substr(brace);
                }
                GroupItems(items, before);
                return;
            }
            case Special::IfNot:
                s.args.push_back(Expr::Unary(Op::LogicalNot, Expr::Make(Expr::Kind::Group)));
                s.args[0].args[0].args.push_back(Parse(rest));
                blocks.push_back(Block('I'));
                break;
            case Special::IfEqual:
            case Special::IfNotEqual:
            case Special::IfLess:
            case Special::IfGreater:
            {
                const std::vector<std::string> parts = Split(rest, ',', d.rules.escapesInSingleQuotes);
                if (parts.size() != 2)
                {
                    result.diagnostics.push_back({Severity::Error, number, 0, "IFxx needs two expressions"});
                    return;
                }
                const Op op = c->special == Special::IfEqual ? Op::Equal : c->special == Special::IfNotEqual ? Op::NotEqual
                              : c->special == Special::IfLess ? Op::Less : Op::Greater;
                s.directive = DirectiveKind::If;
                s.args.push_back(Expr::Binary(op, Parse(parts[0]), Parse(parts[1])));
                blocks.push_back(Block('I'));
                break;
            }
            case Special::Ignore: return;
            case Special::Z80n:
                z80n = true;
                result.program.z80n = true;
                return;
            case Special::HexBytes:
            {
                s.directive = DirectiveKind::Db;
                for (const std::string& item : Split(rest, ',', d.rules.escapesInSingleQuotes))
                {
                    std::string digits = item;
                    if (digits.size() >= 2 && (digits[0] == '"' || digits[0] == '\''))
                        digits = digits.substr(1, digits.size() - 2);
                    for (size_t k = 0; k + 1 < digits.size(); k += 2)
                    {
                        ir::Operand byte;
                        byte.kind = ir::Operand::Kind::Immediate;
                        byte.expr = Expr::Number(std::stoi(digits.substr(k, 2), nullptr, 16), ir::NumberSpelling::Hex, 2);
                        s.operands.push_back(std::move(byte));
                    }
                }
                break;
            }
            case Special::RawBlock:
                blocks.push_back(Block('S'));
                line.statements.push_back(Other(command, rest));
                return;
            case Special::AsciiZ:
            {
                s.directive = DirectiveKind::Db;
                s.operands = DataOperands(rest, d.rules, number, result.diagnostics);
                ir::Operand zero;
                zero.kind = ir::Operand::Kind::Immediate;
                zero.expr = Expr::Number(0);
                s.operands.push_back(std::move(zero));
                break;
            }
            case Special::Segment:
            {
                // #CODE name, start, size: the segment starts at the address (a '*' or none: right after the previous one)
                const std::vector<std::string> parts = Split(rest, ',', d.rules.escapesInSingleQuotes);
                if (parts.size() > 1 && parts[1] != "*" && !parts[1].empty())
                {
                    Statement org = Directive(DirectiveKind::Org);
                    org.args.push_back(Parse(parts[1]));
                    line.statements.push_back(std::move(org));
                }
                return;
            }
            case Special::IfDef:
            case Special::IfNdef:
            {
                Expr exists = Expr::Unary(Op::Exists, Expr::Symbol(Filter(Trim(rest))));
                s.args.push_back(c->special == Special::IfDef ? std::move(exists) : Expr::Unary(Op::LogicalNot, std::move(exists)));
                blocks.push_back(Block('I'));
                break;
            }
            default:
                switch (c->kind)
                {
                    case DirectiveKind::If:
                        s.args.push_back(Parse(rest));
                        blocks.push_back(Block('I'));
                        break;
                    case DirectiveKind::EndIf:
                        if (!blocks.empty() && blocks.back().kind == 'I')
                        {
                            for (int k = 0; k < blocks.back().extraIfs; ++k)
                                line.statements.push_back(Directive(DirectiveKind::EndIf));
                            blocks.pop_back();
                        }
                        break;
                    case DirectiveKind::Include:
                    case DirectiveKind::Incbin:
                        s.text = d.fileName ? d.fileName(rest) : DefaultFileName(rest);
                        s.params.push_back("verbatim");   // a real file name: the backends write it as it is
                        if (c->kind == DirectiveKind::Incbin)
                        {
                            const std::vector<std::string> parts = Split(rest, ',', d.rules.escapesInSingleQuotes);
                            for (size_t k = 1; k < parts.size(); ++k)
                                s.args.push_back(Parse(parts[k]));
                        }
                        break;
                    case DirectiveKind::Db:
                    case DirectiveKind::Dw:
                        s.operands = DataOperands(rest, d.rules, number, result.diagnostics);
                        if (c->kind == DirectiveKind::Dw)
                            for (ir::Operand& o : s.operands)
                                if (o.kind == ir::Operand::Kind::String && o.text.size() == 2)
                                {
                                    // DW 'LH': a two-character constant is one word, not two bytes of text
                                    int64_t value = 0;
                                    if (d.rules.twoCharsLowFirst)
                                        value = static_cast<unsigned char>(o.text[0]) | (static_cast<unsigned char>(o.text[1]) << 8);
                                    else
                                        value = (static_cast<unsigned char>(o.text[0]) << 8) | static_cast<unsigned char>(o.text[1]);
                                    Expr word = Expr::Number(value, ir::NumberSpelling::Character, 2);
                                    word.text = o.text;
                                    o.kind = ir::Operand::Kind::Immediate;
                                    o.text.clear();
                                    o.expr = std::move(word);
                                }
                        break;
                    case DirectiveKind::End:
                        if (!blocks.empty() && blocks.back().kind == 'S')
                        {
                            blocks.pop_back();   // END of a STRUCT / ENUM
                            line.statements.push_back(Other(command, rest));
                            return;
                        }
                        if (!Trim(rest).empty())
                            s.args.push_back(Parse(rest));
                        break;
                    case DirectiveKind::Else: break;
                    default:
                        for (const std::string& a : Split(rest, ',', d.rules.escapesInSingleQuotes))
                            if (!a.empty())
                                s.args.push_back(Parse(a));
                        break;
                }
                break;
        }
        line.statements.push_back(std::move(s));
    }
};

std::string FirstWord(const std::string& text, size_t from, size_t& end)
{
    size_t k = from;
    while (k < text.size() && text[k] != ' ' && text[k] != '\t' && text[k] != ':' && text[k] != ',')
        ++k;
    end = k;
    return text.substr(from, k - from);
}
}  // namespace

FrontendResult ParseText(const SourceDocument& source, const TextDialect& dialect)
{
    Engine e(dialect, source.z80n || dialect.z80nAlways);
    e.result.program.dialect = dialect.id;
    e.result.program.expressionBits = dialect.expressionBits;
    e.result.program.unsignedArithmetic = dialect.unsignedArithmetic;
    e.result.program.trueValue = dialect.trueValue;
    e.result.program.z80n = e.z80n;
    bool ended = false;
    for (const SourceLine& sourceLine : source.lines)
    {
        ++e.number;
        ir::Line line;
        line.sourceLine = e.number;
        std::string text = sourceLine.text;
        const std::string mask = Mask(text, dialect.rules.escapesInSingleQuotes);
        size_t semicolon = mask.find(';');
        if (dialect.slashComments)
        {
            const size_t slashes = mask.find("//");
            if (slashes != std::string::npos && (semicolon == std::string::npos || slashes < semicolon))
                semicolon = slashes - (0);   // the comment text starts after the two slashes: handled below
        }
        if (semicolon != std::string::npos)
        {
            line.hasComment = true;
            const size_t skip = dialect.slashComments && text.compare(semicolon, 2, "//") == 0 ? 2 : 1;
            line.comment = text.substr(semicolon + skip);
            text = text.substr(0, semicolon);
        }
        if (ended)
        {
            // after END: ignored; kept as a comment so nothing is lost
            line.hasComment = true;
            line.comment = " " + sourceLine.text;
            e.result.program.lines.push_back(std::move(line));
            continue;
        }
        if (e.group.kind != Engine::Group::None)
        {
            std::vector<ir::Line> items;
            e.GroupItems(text, items);
            for (ir::Line& one : items)
                e.result.program.lines.push_back(std::move(one));
            e.result.program.lines.push_back(std::move(line));
            continue;
        }
        if (dialect.shebangLine && e.number == 1 && text.rfind("#!", 0) == 0)
        {
            if (text.find("--z80n") != std::string::npos)
            {
                e.z80n = true;
                e.result.program.z80n = true;
            }
            line.hasComment = true;
            line.comment = text.substr(2);
            e.result.program.lines.push_back(std::move(line));
            continue;
        }
        if (dialect.lineNumbers)
        {
            size_t at = 0;
            while (at < text.size() && std::isdigit(static_cast<unsigned char>(text[at])))
                ++at;
            if (at > 0 && at < text.size() && (text[at] == ' ' || text[at] == '\t'))
                text = std::string(at, ' ') + text.substr(at);
        }
        if (!e.blocks.empty() && e.blocks.back().kind == 'S' && Lower(Trim(text)) != "end")
        {
            // inside a STRUCT / ENUM: the line is kept as text
            line.hasComment = true;
            line.comment = " " + sourceLine.text;
            e.result.program.lines.push_back(std::move(line));
            continue;
        }
        text = e.StripParamTags(text);
        if (dialect.numericLocals)
        {
            // an ordinary label (a name in column 1 that is not a command) starts a new stretch
            size_t end = 0;
            const std::string first = text.empty() || text[0] == ' ' || text[0] == '\t' ? "" : FirstWord(text, 0, end);
            const bool numeric = !first.empty() && std::isdigit(static_cast<unsigned char>(first[0])) && first.size() > 1 && first.back() == '$';
            if (!first.empty() && !numeric && IsNameStart(first[0], dialect.rules) && !e.Reserved(first) && first[0] != '#')
                ++e.localScope;
            text = e.RewriteNumericLocals(text);
        }
        if (dialect.commands.count("="))
        {
            // NAME=expr written without blanks: NAME = expr
            size_t p = text.find_first_not_of(" \t");
            const size_t nameStart = p;
            while (p != std::string::npos && p < text.size() && IsNameChar(text[p], dialect.rules))
                ++p;
            if (nameStart != std::string::npos && p > nameStart && p < text.size())
            {
                size_t q = p;
                while (q < text.size() && (text[q] == ' ' || text[q] == '\t'))
                    ++q;
                if (q < text.size() && text[q] == '=' && (q + 1 >= text.size() || text[q + 1] != '='))
                    text = text.substr(0, p) + " = " + text.substr(q + 1);
            }
        }
        size_t k = 0;
        bool labelBeforeKeyword = false;
        if (!text.empty())
        {
            size_t start = 0;
            const bool columnZero = text[0] != ' ' && text[0] != '\t';
            if (!columnZero && dialect.labelStyle != LabelStyle::ColumnZero)
            {
                start = text.find_first_not_of(" \t");
                if (start == std::string::npos)
                    start = text.size();
            }
            if (start < text.size())
            {
                size_t end = 0;
                const std::string word = FirstWord(text, start, end);
                const bool colon = end < text.size() && text[end] == ':';
                const bool dot = word.size() > 1 && word[0] == '.' && dialect.labelStyle == LabelStyle::ColonOrDot && columnZero;
                const std::string name = dot ? word.substr(1) : word;
                bool isLabel = false;
                switch (dialect.labelStyle)
                {
                    case LabelStyle::ColumnZero: isLabel = columnZero; break;
                    case LabelStyle::ColonOrDot: isLabel = colon || dot; break;
                    case LabelStyle::ColonOrColumnZero: isLabel = colon || columnZero; break;
                }
                bool assignedBare = false;
                if (!isLabel && columnZero && dialect.labelStyle != LabelStyle::ColumnZero && !name.empty() && IsNameStart(name[0], dialect.rules) &&
                    !e.Reserved(name))
                {
                    // NAME EQU expr / NAME = expr / NAME DEFL expr / NAME MACRO ...: the name is a label though it has no colon
                    size_t next = text.find_first_not_of(" \t", end);
                    std::string follower;
                    if (next != std::string::npos)
                    {
                        if (text[next] == '=' && (next + 1 >= text.size() || text[next + 1] != '='))
                            follower = "=";
                        else
                            follower = Lower(FirstWord(text, next, next));
                    }
                    assignedBare = follower == "=" || follower == "equ" || follower == "defl" || follower == "macro";
                }
                if ((isLabel || assignedBare) && !name.empty() && IsNameStart(name[0], dialect.rules) && (colon || dot || assignedBare || !e.Reserved(name)))
                {
                    line.label = e.Filter(name);
                    labelBeforeKeyword = true;
                    k = end + (colon ? 1 : 0);
                    if (colon && k < text.size() && text[k] == ':')
                        ++k;   // NAME:: (a global label in a local context)
                }
            }
        }
        const std::string body = Trim(std::string_view(text).substr(k));
        std::vector<ir::Line> before;
        if (!body.empty())
        {
            size_t end = 0;
            const std::vector<std::string> pieces = dialect.statementSeparator ? Split(body, dialect.statementSeparator, dialect.rules.escapesInSingleQuotes)
                                                                                : std::vector<std::string>{body};
            for (const std::string& piece : pieces)
            {
                if (piece.empty())
                    continue;
                std::string command = FirstWord(piece, 0, end);
                std::string rest = Trim(std::string_view(piece).substr(end));
                if (!command.empty() && command[0] == '=' && command.size() > 1)
                {
                    // =expr without a blank
                    rest = Trim(std::string_view(piece).substr(1));
                    command = "=";
                }
                e.Command_(command, rest, line, labelBeforeKeyword, before);
                // END ends the source unless it closed a STRUCT / ENUM block (kept as text)
                if (!line.statements.empty() && line.statements.back().kind == Statement::Kind::Directive &&
                    line.statements.back().directive == DirectiveKind::End)
                    ended = true;
            }
        }
        for (ir::Line& extra : before)
            e.result.program.lines.push_back(std::move(extra));
        e.result.program.lines.push_back(std::move(line));
    }
    return std::move(e.result);
}
}  // namespace unrealasm::dialects::text
