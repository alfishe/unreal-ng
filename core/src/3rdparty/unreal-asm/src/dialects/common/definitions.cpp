#include "dialects/common/definitions.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace unrealasm::dialects
{
std::map<std::string, int> CountDefinitions(const ir::Program& program, const std::function<std::optional<bool>(const ir::Expr&, size_t)>& decide)
{
    std::map<std::string, int> definitions;
    bool inMacroBody = false;
    // Per open IF: the most each branch so far defined, and the current branch's; the condition when known
    struct Branches
    {
        std::map<std::string, int> most, current;
        std::optional<bool> condition;
        bool inElse = false;
    };
    std::vector<Branches> ifs;
    const auto close = [](Branches& b) {
        if (!b.condition || *b.condition != b.inElse)   // a branch not taken defines nothing
            for (const auto& [name, count] : b.current)
                b.most[name] = std::max(b.most[name], count);
        b.current.clear();
    };
    const auto end = [&] {
        close(ifs.back());
        const std::map<std::string, int> most = std::move(ifs.back().most);
        ifs.pop_back();
        for (const auto& [name, count] : most)
            (ifs.empty() ? definitions : ifs.back().current)[name] += count;
    };
    for (size_t index = 0; index < program.lines.size(); ++index)
    {
        const ir::Line& l = program.lines[index];
        for (const ir::Statement& s : l.statements)
            if (s.kind == ir::Statement::Kind::Directive && s.directive == ir::DirectiveKind::Macro)
                inMacroBody = true;
        if (!l.label.empty())
            (ifs.empty() ? definitions : ifs.back().current)[l.label] += inMacroBody ? 2 : 1;
        for (const ir::Statement& s : l.statements)
        {
            if (s.kind != ir::Statement::Kind::Directive)
                continue;
            if (s.directive == ir::DirectiveKind::EndMacro)
                inMacroBody = false;
            else if (s.directive == ir::DirectiveKind::If || s.directive == ir::DirectiveKind::IfUsed)
            {
                ifs.emplace_back();
                if (decide && !inMacroBody && s.directive == ir::DirectiveKind::If && !s.args.empty())
                    ifs.back().condition = decide(s.args[0], index);
            }
            else if (s.directive == ir::DirectiveKind::Else && !ifs.empty())
            {
                close(ifs.back());
                ifs.back().inElse = true;
            }
            else if (s.directive == ir::DirectiveKind::EndIf && !ifs.empty())
                end();
        }
    }
    while (!ifs.empty())   // an IF left open: its branches as they are
        end();
    return definitions;
}
}  // namespace unrealasm::dialects
