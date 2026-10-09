#include "dialects/specasm/specasmfrontend.h"

#include <algorithm>
#include <cctype>

#include "dialects/common/textexpr.h"
#include "dialects/common/textfrontend.h"
#include "dialects/common/z80.h"

namespace unrealasm::dialects
{
namespace
{
using ir::DirectiveKind;
using ir::Expr;
using ir::Op;
using ir::Statement;
using namespace text;

const ExprRules& Rules()
{
    static const ExprRules rules = [] {
        ExprRules r;
        // Tightest first in the manual: unary · * / % · + - · << >> · & | ^ (one priority)
        r.binary = {
            {"&", Op::And, 1}, {"|", Op::Or, 1}, {"^", Op::Xor, 1}, {"<<", Op::Shl, 2}, {">>", Op::Shr, 2},
            {"+", Op::Add, 3}, {"-", Op::Sub, 3}, {"*", Op::Mul, 4}, {"/", Op::Div, 4},  {"%", Op::Mod, 4},
        };
        r.unary = {{"-", Op::Negate, 5}, {"~", Op::Not, 5}};
        r.dollarHex = true;
        r.zeroX = r.suffixes = false;
        r.dotInNames = r.atInNames = r.questionInNames = false;
        r.backslashEscapes = false;
        return r;
    }();
    return rules;
}

/// The text without the `=` expression markers outside strings
std::string DropMarkers(const std::string& text)
{
    const std::string mask = Mask(text);
    std::string out;
    for (size_t k = 0; k < text.size(); ++k)
        if (!(text[k] == '=' && mask[k] == '='))
            out += text[k];
    return Trim(out);
}

Statement Directive(DirectiveKind kind)
{
    Statement s;
    s.kind = Statement::Kind::Directive;
    s.directive = kind;
    return s;
}

ir::Operand Number(int64_t value)
{
    ir::Operand o;
    o.kind = ir::Operand::Kind::Immediate;
    o.expr = Expr::Number(value);
    return o;
}
}  // namespace

FrontendResult SpecasmFrontend::Parse(const SourceDocument& source) const
{
    FrontendResult result;
    result.program.dialect = "specasm";
    result.program.expressionBits = 16;
    result.program.unsignedArithmetic = true;
    result.program.z80n = true;
    uint32_t number = 0;
    const auto parse = [&](std::string_view t) { return ParseExpression(t, Rules(), number, result.diagnostics); };
    for (const SourceLine& sourceLine : source.lines)
    {
        ++number;
        ir::Line line;
        line.sourceLine = number;
        const std::string raw = Trim(sourceLine.text);
        const auto finish = [&] { result.program.lines.push_back(std::move(line)); };
        if (raw.empty())
        {
            finish();
            continue;
        }
        if (raw[0] == ';')
        {
            line.hasComment = true;
            line.comment = raw.substr(1);
            finish();
            continue;
        }
        // A string line: the first character says how it is written
        if (raw[0] == '"' || raw[0] == '\'' || raw[0] == '@' || raw[0] == '#')
        {
            const char q = raw[0];
            // closed by the same character (a comment may follow it), or running to the end of the line
            const size_t close = raw.find(q, 1);
            std::string body = close == std::string::npos ? raw.substr(1) : raw.substr(1, close - 1);
            if (close != std::string::npos)
            {
                const std::string after = Trim(std::string_view(raw).substr(close + 1));
                if (!after.empty() && after[0] == ';')
                {
                    line.hasComment = true;
                    line.comment = after.substr(1);
                }
            }
            Statement db = Directive(DirectiveKind::Db);
            if (q == '@' || q == '#')
                db.operands.push_back(Number(static_cast<int64_t>(body.size())));
            ir::Operand text;
            text.kind = ir::Operand::Kind::String;
            text.text = body;
            db.operands.push_back(std::move(text));
            line.statements.push_back(std::move(db));
            finish();
            continue;
        }
        std::string code = raw;
        const std::string mask = Mask(code);
        const size_t semicolon = mask.find(';');
        if (semicolon != std::string::npos)
        {
            line.hasComment = true;
            line.comment = code.substr(semicolon + 1);
            code = Trim(code.substr(0, semicolon));
        }
        if (code[0] == '.')
        {
            // .Name [equ expression]
            size_t end = 1;
            while (end < code.size() && IsNameChar(code[end], Rules()))
                ++end;
            line.label = code.substr(1, end - 1);
            const std::string rest = Trim(std::string_view(code).substr(end));
            if (!rest.empty())
            {
                if (Lower(rest.substr(0, 3)) == "equ")
                {
                    Statement equ = Directive(DirectiveKind::Equ);
                    equ.args.push_back(parse(DropMarkers(rest.substr(3))));
                    line.statements.push_back(std::move(equ));
                }
                else
                    result.diagnostics.push_back({Severity::Error, number, 0, "a label stands on a line of its own"});
            }
            finish();
            continue;
        }
        const size_t blank = code.find_first_of(" \t");
        const std::string command = Lower(code.substr(0, blank));
        const std::string rest = blank == std::string::npos ? std::string() : DropMarkers(code.substr(blank));
        if (command == "org" || command == "ds" || command == "align" || command == "include" || command == "incbin")
        {
            Statement s;
            if (command == "org")
            {
                s = Directive(DirectiveKind::Org);
                s.args.push_back(parse(rest));
            }
            else if (command == "ds")
            {
                s = Directive(DirectiveKind::Ds);
                for (const std::string& part : Split(rest, ','))
                    s.args.push_back(parse(part));
            }
            else if (command == "align")
            {
                // pad to the next multiple of n: DS (n - $ % n) % n
                s = Directive(DirectiveKind::Ds);
                const Expr n = parse(rest);
                s.args.push_back(Expr::Binary(Op::Mod, Expr::Binary(Op::Sub, n, Expr::Binary(Op::Mod, Expr::Make(Expr::Kind::Current), n)), n));
            }
            else
            {
                s = Directive(command == "include" ? DirectiveKind::Include : DirectiveKind::Incbin);
                s.text = DefaultFileName(rest);
                s.params.push_back("verbatim");
            }
            line.statements.push_back(std::move(s));
        }
        else if (command == "db" || command == "dw")
        {
            Statement s = Directive(command == "db" ? DirectiveKind::Db : DirectiveKind::Dw);
            s.operands = DataOperands(rest, Rules(), number, result.diagnostics);
            line.statements.push_back(std::move(s));
        }
        else if (command == "nbrk")
        {
            Statement s;
            s.kind = Statement::Kind::Instruction;
            s.mnemonic = "nextreg";
            s.operands = {Number(2), Number(8)};
            line.statements.push_back(std::move(s));
        }
        else if (z80::IsMnemonic(command) || z80::IsZ80nMnemonic(command))
        {
            Statement s;
            s.kind = Statement::Kind::Instruction;
            s.mnemonic = command;
            const std::vector<std::string> ops = Split(rest, ',');
            for (size_t k = 0; k < ops.size(); ++k)
            {
                const bool condition = k == 0 && z80::TakesCondition(command) && (ops.size() > 1 || command == "ret");
                s.operands.push_back(InstructionOperand(ops[k], condition, command == "in" || command == "out", Rules(), number, result.diagnostics));
            }
            line.statements.push_back(std::move(s));
        }
        else
        {
            Statement s;
            s.kind = Statement::Kind::Raw;
            s.text = code;
            line.statements.push_back(std::move(s));
        }
        finish();
    }
    return result;
}
}  // namespace unrealasm::dialects
