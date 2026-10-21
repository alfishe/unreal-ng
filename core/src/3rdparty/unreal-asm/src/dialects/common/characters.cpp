#include "dialects/common/characters.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace unrealasm::dialects
{
namespace
{
using Bytes = std::vector<uint8_t>;

int64_t HighFirst(const Bytes& b)
{
    uint64_t v = 0;
    for (const uint8_t c : b)
        v = (v << 8) | c;
    return static_cast<int64_t>(v);
}

int64_t LowFirst(const Bytes& b)
{
    uint64_t v = 0;
    for (size_t k = b.size(); k > 0; --k)
        v = (v << 8) | b[k - 1];
    return static_cast<int64_t>(v);
}

/// The ways the frontends combine several characters: the first the high byte (TASM: the last two), the first the low
/// byte, ALASM's first in the low byte and last in the high one, the first or the last alone
const std::array<std::function<int64_t(const Bytes&)>, 6> kRules = {
    HighFirst,
    [](const Bytes& b) { return HighFirst(b) & 0xFFFF; },
    LowFirst,
    [](const Bytes& b) { return b.size() > 1 ? b.front() | (b.back() << 8) : b.front(); },
    [](const Bytes& b) { return static_cast<int64_t>(b.front()); },
    [](const Bytes& b) { return static_cast<int64_t>(b.back()); },
};

void Fix(ir::Expr& e, encoding::CodePage codePage)
{
    for (ir::Expr& a : e.args)
        Fix(a, codePage);
    if (e.kind != ir::Expr::Kind::Number || e.spelling != ir::NumberSpelling::Character || e.text.empty())
        return;
    const Bytes utf8(e.text.begin(), e.text.end());
    bool ascii = true;
    for (const uint8_t c : utf8)
        ascii = ascii && c < 0x80;
    if (ascii)
        return;
    Bytes bytes;
    std::string error;
    if (!encoding::FromUtf8(e.text, codePage, bytes, error) || bytes.empty())
        return;   // a character the code page lacks (or a part of one): the value as it is
    for (const auto& rule : kRules)
        if (rule(utf8) == e.value)
        {
            e.value = rule(bytes);
            return;
        }
}
}  // namespace

void CharactersInCodePage(ir::Program& program, encoding::CodePage codePage)
{
    if (codePage == encoding::CodePage::Utf8)
        return;
    for (ir::Line& line : program.lines)
        for (ir::Statement& s : line.statements)
        {
            for (ir::Expr& a : s.args)
                Fix(a, codePage);
            for (ir::Operand& o : s.operands)
                Fix(o.expr, codePage);
        }
}
}  // namespace unrealasm::dialects
