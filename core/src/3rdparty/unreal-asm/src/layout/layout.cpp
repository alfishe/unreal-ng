#include "unrealasm/layout.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <memory>
#include <set>

#include "dialects/common/z80.h"
#include "dialects/sjasmplus/sjasmplusfrontend.h"
#include "layout/z80size.h"
#include "unrealasm/encoding.h"

namespace unrealasm::layout
{
namespace
{
using ir::DirectiveKind;
using ir::Expr;
using ir::Op;
using ir::Statement;
namespace z80 = dialects::z80;

/// One run of lines: a project file, or the lines of one macro expansion (parsed from the substituted text)
struct Chunk
{
    std::string file;                ///< the project file the lines come from
    std::vector<ir::Line> lines;
    std::vector<std::string> raw;    ///< each line's text (macro bodies are kept as text and substituted at a call)
    std::vector<uint32_t> fileLines; ///< each line's 1-based line in `file`
    encoding::CodePage codePage = encoding::CodePage::Utf8;   ///< the file's: a string takes its bytes in it
};

struct Macro
{
    std::vector<std::string> params;
    std::vector<std::string> body;
    std::vector<uint32_t> bodyLines;
    std::string file;
    encoding::CodePage codePage = encoding::CodePage::Utf8;
};

constexpr int kMaxNesting = 64;          // INCLUDE / macro depth
constexpr int64_t kMaxRepeats = 1 << 20;   // DUP / WHILE iterations
constexpr int64_t kMaxSteps = 1 << 23;     // lines laid out in one pass (a program that runs while assembling: ALASM's SNAKE)

std::string BaseName(std::string name)
{
    const size_t slash = name.find_last_of("/\\:");
    if (slash != std::string::npos)
        name.erase(0, slash + 1);
    if (name.size() > 4 && z80::Lower(name.substr(name.size() - 4)) == ".asm")
        name.resize(name.size() - 4);
    return z80::Lower(name);
}

int64_t Wrap32(int64_t v)
{
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(v)));
}

bool IsWordChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

/// A macro body line with the arguments put in: a parameter name is replaced where the characters around it are no
/// letters or digits ("mg_x" with x = 1 is "mg_1", as sjasmplus does; "_arg0" is a parameter as a whole), outside
/// strings; the longest name first
std::string Substitute(const std::string& line, const std::vector<std::string>& params, const std::vector<std::string>& args)
{
    std::vector<size_t> order(params.size());
    for (size_t k = 0; k < order.size(); ++k)
        order[k] = k;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return params[a].size() > params[b].size(); });
    std::string out;
    char quote = 0;
    for (size_t i = 0; i < line.size();)
    {
        const char c = line[i];
        if (quote)
        {
            out.push_back(c);
            if (c == quote)
                quote = 0;
            ++i;
            continue;
        }
        const bool afAfter = c == '\'' && i >= 2 && (line[i - 1] == 'f' || line[i - 1] == 'F') && (line[i - 2] == 'a' || line[i - 2] == 'A');
        if (c == '"' || (c == '\'' && !afAfter))
        {
            quote = c;
            out.push_back(c);
            ++i;
            continue;
        }
        if (c == ';')
        {
            out += line.substr(i);
            break;
        }
        bool replaced = false;
        if (i == 0 || !IsWordChar(line[i - 1]))
            for (const size_t p : order)
            {
                const std::string& name = params[p];
                if (name.empty() || line.compare(i, name.size(), name) != 0)
                    continue;
                if (i + name.size() < line.size() && IsWordChar(line[i + name.size()]))
                    continue;
                out += p < args.size() ? args[p] : std::string();
                i += name.size();
                replaced = true;
                break;
            }
        if (!replaced)
        {
            out.push_back(c);
            ++i;
        }
    }
    return out;
}

std::string Trim(const std::string& text)
{
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t'))
        ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t'))
        --b;
    return text.substr(a, b - a);
}

/// A macro argument as sjasmplus passes it: <text> without its brackets
std::string Argument(std::string text)
{
    text = Trim(text);
    if (text.size() >= 2 && text.front() == '<' && text.back() == '>')
        return text.substr(1, text.size() - 2);
    return text;
}

class Engine
{
public:
    Engine(const std::vector<ProjectFile>& files, const LayoutOptions& options) : _options(options)
    {
        const dialects::SjasmplusFrontend frontend;
        for (const ProjectFile& f : files)
        {
            Chunk chunk;
            chunk.file = f.name;
            chunk.codePage = f.document.codePage;
            FrontendResult parsed = frontend.Parse(f.document);
            chunk.lines = std::move(parsed.program.lines);
            for (size_t k = 0; k < chunk.lines.size(); ++k)
            {
                chunk.raw.push_back(k < f.document.lines.size() ? f.document.lines[k].text : std::string());
                chunk.fileLines.push_back(chunk.lines[k].sourceLine);
            }
            _byName[BaseName(f.name)] = _files.size();
            _files.push_back(std::move(chunk));
        }
    }

    LayoutResult Run(size_t main)
    {
        LayoutResult result;
        if (main >= _files.size())
        {
            result.diagnostics.push_back({Severity::Error, 0, 0, "no main file"});
            return result;
        }
        // As sjasmplus: the symbol table lives through the passes (a name defined in pass 1 is known later even where
        // no later pass defines it again, a name not known yet counts as 0), the passes repeat until nothing moves
        std::map<std::string, int64_t> last;
        bool stable = false;
        for (_pass = 1; _pass <= _options.maxPasses; ++_pass)
        {
            BeginPass();
            RunChunk(_files[main], 0);
            result.passes = _pass;
            const bool same = _symbols == last;
            last = _symbols;
            _previousTemps = _temps;
            if (same && !_unresolved && _pass > 1)
            {
                stable = true;
                break;
            }
            if (_steps > kMaxSteps)
                break;   // another pass would not end either
        }
        // One more pass with every value known collects the diagnostics and the labels
        _final = true;
        BeginPass();
        RunChunk(_files[main], 0);
        stable = stable && _symbols == last;
        result.labels = std::move(_labels);
        result.diagnostics = std::move(_diagnostics);
        if (!stable)
            result.diagnostics.push_back({Severity::Error, 0, 0, "labels still move after " + std::to_string(_options.maxPasses) + " passes"});
        result.ok = stable && !_unresolved && !HasErrors(result.diagnostics);
        return result;
    }

private:
    const LayoutOptions& _options;
    std::vector<Chunk> _files;
    std::map<std::string, size_t> _byName;

    int _pass = 0;
    bool _final = false;
    int64_t _pc = 0, _physical = 0;
    bool _displaced = false;
    int _page = -1;
    std::string _lastGlobal;
    std::vector<std::string> _modules;
    std::map<std::string, int64_t> _symbols;        // through all passes
    std::set<std::string> _definedThisPass;
    std::set<std::string> _used;                    // names some expression refers to (IFUSED), through all passes
    std::set<std::string> _defines;
    std::map<std::string, Macro> _macros;
    std::vector<std::pair<std::string, int64_t>> _temps, _previousTemps;
    bool _unresolved = false;
    bool _stopped = false;
    int64_t _steps = 0;
    int _expansions = 0;
    std::string _macroScope;   // "" outside a macro; else a prefix unique to the expansion (its local labels)

    // What DB / DW / DS wrote where (by the physical address), for {address} reads: -1 = not written, -2 = code (its
    // bytes are not built)
    std::vector<int16_t> _memory = std::vector<int16_t>(65536, -1);
    std::vector<Label> _labels;                     // through all passes, in the order first defined
    std::map<std::string, size_t> _labelIndex;
    std::vector<size_t> _pending;   // labels whose use the next laid-out statement decides
    // Final pass only
    Diagnostics _diagnostics;
    std::set<std::string> _reported;

    // Where the statement being laid out is (for diagnostics and labels)
    const Chunk* _chunk = nullptr;
    size_t _lineIndex = 0;

    void BeginPass()
    {
        _pc = _physical = 0;
        _displaced = false;
        _page = -1;
        _lastGlobal.clear();
        _modules.clear();
        _definedThisPass.clear();
        _defines.clear();
        _macros.clear();
        _temps.clear();
        _unresolved = false;
        _stopped = false;
        _steps = 0;
        _expansions = 0;
        _macroScope.clear();
        _pending.clear();
        _diagnostics.clear();
        _reported.clear();
    }

    uint32_t FileLine() const { return _chunk && _lineIndex < _chunk->fileLines.size() ? _chunk->fileLines[_lineIndex] : 0; }

    void Report(Severity severity, const std::string& message)
    {
        if (!_final)
            return;
        const std::string where = _chunk ? _chunk->file + ":" + std::to_string(FileLine()) + ": " : std::string();
        if (!_reported.insert(where + message).second)
            return;
        _diagnostics.push_back({severity, FileLine(), 0, where + message});
    }

    // ---- names ----

    std::string ModulePrefix() const
    {
        std::string prefix;
        for (const std::string& m : _modules)
            prefix += m + ".";
        return prefix;
    }

    /// The candidates a name in an expression may mean, most specific first
    std::vector<std::string> Candidates(const std::string& name) const
    {
        if (name.empty())
            return {};
        if (name[0] == '@')
            return {name.substr(1)};
        if (name[0] == '.')
        {
            std::vector<std::string> out;
            if (!_macroScope.empty())
                out.push_back(_macroScope + name);
            out.push_back(_lastGlobal + name);
            return out;
        }
        std::vector<std::string> out;
        for (size_t k = _modules.size(); k > 0; --k)
        {
            std::string prefix;
            for (size_t m = 0; m < k; ++m)
                prefix += _modules[m] + ".";
            out.push_back(prefix + name);
        }
        out.push_back(name);
        return out;
    }

    std::optional<int64_t> Lookup(const std::string& name)
    {
        const std::vector<std::string> candidates = Candidates(name);
        for (const std::string& c : candidates)
            if (const auto found = _symbols.find(c); found != _symbols.end())
            {
                _used.insert(c);
                return found->second;
            }
        if (!candidates.empty())
            _used.insert(candidates.back());
        _unresolved = true;
        Report(Severity::Error, "unknown symbol " + name);
        return 0;   // sjasmplus: an unknown name is 0 until a pass defines it
    }

    bool Defined(const std::string& name) const
    {
        for (const std::string& c : Candidates(name))
            if (_symbols.count(c))
                return true;
        return false;
    }

    /// 1B / 1F: the temporary label of that number before / after this point
    std::optional<int64_t> Temporary(const std::string& text)
    {
        const std::string number = text.substr(0, text.size() - 1);
        const char direction = static_cast<char>(std::tolower(static_cast<unsigned char>(text.back())));
        if (direction == 'b')
        {
            for (size_t k = _temps.size(); k > 0; --k)
                if (_temps[k - 1].first == number)
                    return _temps[k - 1].second;
        }
        else
        {
            for (size_t k = _temps.size(); k < _previousTemps.size(); ++k)
                if (_previousTemps[k].first == number)
                    return _previousTemps[k].second;
        }
        _unresolved = true;
        if (direction == 'b' || _pass > 1)
            Report(Severity::Error, "no temporary label " + text);
        return std::nullopt;
    }

    // ---- expressions (sjasmplus: 32-bit, C rules, true = -1) ----

    std::optional<int64_t> Eval(const Expr& e)
    {
        switch (e.kind)
        {
            case Expr::Kind::Number: return Wrap32(e.value);
            case Expr::Kind::Symbol: return Lookup(e.text);
            case Expr::Kind::Current: return _pc;
            case Expr::Kind::CurrentPage: return _page < 0 ? 0 : _page;
            case Expr::Kind::CurrentPhysical: return _physical;
            case Expr::Kind::Group: return e.args.empty() ? std::nullopt : Eval(e.args[0]);
            case Expr::Kind::Memory:
            {
                if (e.args.empty())
                    return std::nullopt;
                const auto at = Eval(e.args[0]);
                if (!at)
                    return std::nullopt;
                const int16_t low = _memory[static_cast<size_t>(*at & 0xFFFF)];
                const int16_t high = _memory[static_cast<size_t>((*at + 1) & 0xFFFF)];
                if (low < 0 || high < 0)
                    Report(Severity::Warning, "{..} reads bytes the source did not write as data (code or untouched memory): taken as 0");
                return (low < 0 ? 0 : low) | ((high < 0 ? 0 : high) << 8);
            }
            case Expr::Kind::Defined: return e.args.empty() ? std::nullopt : std::optional<int64_t>(0);
            case Expr::Kind::Raw:
            {
                const std::string& t = e.text;
                if (t.size() >= 2 && (t.back() == 'b' || t.back() == 'B' || t.back() == 'f' || t.back() == 'F') &&
                    t.find_first_not_of("0123456789") == t.size() - 1)
                    return Temporary(t);
                _unresolved = true;
                Report(Severity::Error, "expression not understood: " + t);
                return std::nullopt;
            }
            case Expr::Kind::Unary: return Unary(e);
            case Expr::Kind::Binary: return Binary(e);
        }
        return std::nullopt;
    }

    std::optional<int64_t> Unary(const Expr& e)
    {
        if (e.args.empty())
            return std::nullopt;
        if (e.op == Op::Exists)
        {
            const Expr& a = e.args[0].kind == Expr::Kind::Group && !e.args[0].args.empty() ? e.args[0].args[0] : e.args[0];
            return a.kind == Expr::Kind::Symbol && Defined(a.text) ? -1 : 0;
        }
        const auto v = Eval(e.args[0]);
        if (!v)
            return std::nullopt;
        switch (e.op)
        {
            case Op::Negate: return Wrap32(-*v);
            case Op::Plus: return *v;
            case Op::Not: return Wrap32(~*v);
            case Op::High: return (*v >> 8) & 0xFF;
            case Op::Low: return *v & 0xFF;
            case Op::LogicalNot: return *v == 0 ? -1 : 0;
            case Op::SwapBytes: return ((*v & 0xFF) << 8) | ((*v >> 8) & 0xFF);
            default: break;
        }
        Report(Severity::Error, "operator not laid out");
        return std::nullopt;
    }

    std::optional<int64_t> Binary(const Expr& e)
    {
        if (e.args.size() < 2)
            return std::nullopt;
        const auto a = Eval(e.args[0]);
        const auto b = Eval(e.args[1]);
        if (!a || !b)
            return std::nullopt;
        const int64_t x = *a, y = *b;
        const uint32_t ux = static_cast<uint32_t>(static_cast<uint64_t>(x));
        switch (e.op)
        {
            case Op::Add: return Wrap32(x + y);
            case Op::Sub: return Wrap32(x - y);
            case Op::Mul: return Wrap32(x * y);
            case Op::Div:
            case Op::Mod:
                if (y == 0)
                {
                    Report(Severity::Error, "division by zero");
                    return 0;
                }
                return Wrap32(e.op == Op::Div ? x / y : x % y);
            case Op::And: return Wrap32(x & y);
            case Op::Or: return Wrap32(x | y);
            case Op::Xor: return Wrap32(x ^ y);
            case Op::Shl: return y >= 32 || y < 0 ? 0 : Wrap32(static_cast<int64_t>(static_cast<uint64_t>(ux) << y));
            case Op::Shr: return y >= 32 || y < 0 ? (x < 0 ? -1 : 0) : Wrap32(x >> y);
            case Op::ShrUnsigned: return y >= 32 || y < 0 ? 0 : Wrap32(static_cast<int64_t>(ux >> y));
            case Op::RotateLeft16:
            case Op::RotateRight16:
            {
                const uint32_t w = ux & 0xFFFF;
                const int n = static_cast<int>(((y % 16) + 16) % 16);
                const uint32_t r = e.op == Op::RotateLeft16 ? (w << n | w >> (16 - n)) : (w >> n | w << (16 - n));
                return static_cast<int64_t>(r & 0xFFFF);
            }
            case Op::Equal: return x == y ? -1 : 0;
            case Op::NotEqual: return x != y ? -1 : 0;
            case Op::Less: return x < y ? -1 : 0;
            case Op::Greater: return x > y ? -1 : 0;
            case Op::LessEqual: return x <= y ? -1 : 0;
            case Op::GreaterEqual: return x >= y ? -1 : 0;
            case Op::LogicalAnd: return x && y ? -1 : 0;
            case Op::LogicalOr: return x || y ? -1 : 0;
            default: break;
        }
        Report(Severity::Error, "operator not laid out");
        return std::nullopt;
    }

    int64_t Value(const Expr& e)
    {
        const auto v = Eval(e);
        return v ? *v : 0;
    }

    // ---- output ----

    void Emit(int64_t bytes, LabelUse use)
    {
        for (size_t k : _pending)
            _labels[k].use = use;
        _pending.clear();
        _pc = Wrap32(_pc + bytes);
        _physical = Wrap32(_physical + bytes);
    }

    void Define(const std::string& written, int64_t value, LabelUse use)
    {
        std::string name = written;
        std::string parent;
        bool global = false;
        if (!name.empty() && name[0] == '@')
        {
            name.erase(0, 1);
            global = true;
        }
        else if (!name.empty() && name[0] == '.')
        {
            if (!_macroScope.empty())
            {
                _symbols[_macroScope + name] = value;   // private to the expansion: no label of the result
                return;
            }
            parent = _lastGlobal;
            name = _lastGlobal + name;
        }
        if (parent.empty())
        {
            if (!global)
                name = ModulePrefix() + name;
            _lastGlobal = name;
        }
        const bool redefinable = use == LabelUse::Defl;
        const bool again = !_definedThisPass.insert(name).second;
        if (again && !redefinable && _symbols[name] != value)
            Report(Severity::Error, "label " + name + " defined twice");
        _symbols[name] = value;
        if (again && redefinable)
        {
            _labels[_labelIndex[name]].value = value;   // DEFL: the last value
            return;
        }
        auto found = _labelIndex.find(name);
        if (found == _labelIndex.end())
        {
            found = _labelIndex.emplace(name, _labels.size()).first;
            _labels.emplace_back();
        }
        Label& l = _labels[found->second];
        l.name = name;
        l.value = value;
        l.use = use;
        l.page = use == LabelUse::Equ || use == LabelUse::Defl ? -1 : _page;
        l.parent = parent;
        l.module = _modules.empty() ? std::string() : ModulePrefix().substr(0, ModulePrefix().size() - 1);
        l.file = _chunk ? _chunk->file : std::string();
        l.line = FileLine();
        if (use == LabelUse::Unknown)
            _pending.push_back(found->second);
    }

    // ---- lines ----

    struct Condition
    {
        bool active;      // this branch is laid out
        bool taken;       // some branch of the IF was
        bool parent;      // the enclosing block is laid out
    };

    static bool Opens(const Statement& s)
    {
        if (s.kind != Statement::Kind::Directive)
            return false;
        if (s.directive == DirectiveKind::If)
            return true;
        if (s.directive == DirectiveKind::Other)
        {
            const std::string word = z80::Upper(Trim(s.text).substr(0, Trim(s.text).find_first_of(" \t")));
            return word == "IFDEF" || word == "IFNDEF" || word == "IFUSED" || word == "IFNUSED";
        }
        return false;
    }

    /// The index of the line closing the block opened at `from` (DUP / WHILE / MACRO), counting nested ones
    static size_t BlockEnd(const Chunk& chunk, size_t from, DirectiveKind open, DirectiveKind close)
    {
        int depth = 0;
        for (size_t k = from; k < chunk.lines.size(); ++k)
            for (const Statement& s : chunk.lines[k].statements)
            {
                if (s.kind != Statement::Kind::Directive)
                    continue;
                if (s.directive == open)
                    ++depth;
                else if (s.directive == close && --depth == 0)
                    return k;
            }
        return chunk.lines.size();
    }

    void RunChunk(const Chunk& chunk, int depth)
    {
        RunLines(chunk, 0, chunk.lines.size(), depth);
    }

    void RunLines(const Chunk& chunk, size_t begin, size_t end, int depth)
    {
        std::vector<Condition> conditions;
        for (size_t k = begin; k < end && !_stopped; ++k)
        {
            _chunk = &chunk;
            _lineIndex = k;
            if (++_steps > kMaxSteps)
            {
                _stopped = true;
                _unresolved = true;
                Report(Severity::Error, "more than " + std::to_string(kMaxSteps) + " lines in one pass: stopped (a loop that does not end?)");
                break;
            }
            const ir::Line& line = chunk.lines[k];
            const bool active = conditions.empty() || conditions.back().active;

            // Conditionals first: they decide whether the rest of the line counts
            bool handled = false;
            for (const Statement& s : line.statements)
            {
                if (s.kind != Statement::Kind::Directive)
                    continue;
                if (Opens(s))
                {
                    bool value = false;
                    if (active)
                        value = Test(s);
                    conditions.push_back({active && value, active && value, active});
                    handled = true;
                }
                else if (s.directive == DirectiveKind::Else && !conditions.empty())
                {
                    Condition& c = conditions.back();
                    c.active = c.parent && !c.taken;
                    c.taken = c.taken || c.active;
                    handled = true;
                }
                else if (s.directive == DirectiveKind::EndIf && !conditions.empty())
                {
                    conditions.pop_back();
                    handled = true;
                }
                else if (s.directive == DirectiveKind::Other && z80::Upper(Trim(s.text)).rfind("ELSEIF", 0) == 0 && !conditions.empty())
                {
                    Condition& c = conditions.back();
                    Statement test;
                    test.kind = Statement::Kind::Directive;
                    test.directive = DirectiveKind::If;
                    test.args.push_back(ParseText(Trim(s.text).substr(6)));
                    const bool value = c.parent && !c.taken && Test(test);
                    c.active = value;
                    c.taken = c.taken || value;
                    handled = true;
                }
            }
            if (handled)
            {
                // A label on the line counts under the state before it (sjasmplus: "name IF ..." defines name)
                if (active && !line.label.empty())
                    Define(line.label, _pc, LabelUse::Unknown);
                continue;
            }
            if (!active)
                continue;

            // Blocks: MACRO, DUP, WHILE take the lines up to their end
            const Statement* block = nullptr;
            for (const Statement& s : line.statements)
                if (s.kind == Statement::Kind::Directive &&
                    (s.directive == DirectiveKind::Macro || s.directive == DirectiveKind::Repeat || s.directive == DirectiveKind::While))
                    block = &s;
            if (block)
            {
                if (!line.label.empty() && block->directive != DirectiveKind::Macro)
                    Define(line.label, _pc, LabelUse::Unknown);
                const DirectiveKind close = block->directive == DirectiveKind::Macro    ? DirectiveKind::EndMacro
                                            : block->directive == DirectiveKind::Repeat ? DirectiveKind::EndRepeat
                                                                                        : DirectiveKind::EndWhile;
                const size_t last = BlockEnd(chunk, k, block->directive, close);
                if (last >= end)
                    Report(Severity::Error, "block without its end");
                const size_t stop = last < end ? last : end;
                if (block->directive == DirectiveKind::Macro)
                {
                    Macro m;
                    m.params = block->params;
                    for (std::string& p : m.params)
                        p = Trim(p);
                    m.file = chunk.file;
                    m.codePage = chunk.codePage;
                    for (size_t b = k + 1; b < stop; ++b)
                    {
                        m.body.push_back(chunk.raw[b]);
                        m.bodyLines.push_back(chunk.fileLines[b]);
                    }
                    _macros[block->text] = std::move(m);
                }
                else if (block->directive == DirectiveKind::Repeat)
                {
                    const int64_t count = block->args.empty() ? 0 : Value(block->args[0]);
                    if (count < 0 || count > kMaxRepeats)
                        Report(Severity::Error, "DUP count out of range");
                    for (int64_t n = 0; n < count && n < kMaxRepeats && !_stopped; ++n)
                        RunLines(chunk, k + 1, stop, depth);
                }
                else
                {
                    int64_t n = 0;
                    for (; n < kMaxRepeats && !_stopped; ++n)
                    {
                        _chunk = &chunk;
                        _lineIndex = k;
                        const auto v = block->args.empty() ? std::optional<int64_t>(0) : Eval(block->args[0]);
                        if (!v || *v == 0)
                            break;
                        RunLines(chunk, k + 1, stop, depth);
                    }
                    if (n == kMaxRepeats)
                        Report(Severity::Error, "WHILE does not end");
                }
                k = stop;
                continue;
            }
            RunLine(line, depth);
        }
        if (!conditions.empty() && end == chunk.lines.size())
            Report(Severity::Error, "IF without ENDIF");
    }

    bool Test(const Statement& s)
    {
        if (s.directive == DirectiveKind::If)
        {
            const auto v = s.args.empty() ? std::optional<int64_t>(0) : Eval(s.args[0]);
            return v && *v != 0;
        }
        const std::string text = Trim(s.text);
        const size_t blank = text.find_first_of(" \t");
        const std::string word = z80::Upper(text.substr(0, blank));
        const std::string name = blank == std::string::npos ? std::string() : Trim(text.substr(blank));
        if (word == "IFDEF")
            return _defines.count(name) != 0;   // DEFINE names only (labels: IFUSED, exist)
        if (word == "IFNDEF")
            return _defines.count(name) == 0;
        bool used = false;
        for (const std::string& c : Candidates(name))
            used = used || _used.count(c);
        return word == "IFUSED" ? used : !used;
    }

    Expr ParseText(const std::string& text)
    {
        // An expression the frontend did not parse (ELSEIF's): through a one-line document
        const dialects::SjasmplusFrontend frontend;
        const FrontendResult parsed = frontend.Parse(SourceDocument::FromText("        IF " + text, "sjasmplus"));
        if (!parsed.program.lines.empty() && !parsed.program.lines[0].statements.empty() && !parsed.program.lines[0].statements[0].args.empty())
            return parsed.program.lines[0].statements[0].args[0];
        Expr raw = Expr::Make(Expr::Kind::Raw);
        raw.text = text;
        return raw;
    }

    void RunLine(const ir::Line& line, int depth)
    {
        // The label: an EQU / DEFL value, a temporary label, or the address
        if (!line.label.empty())
        {
            const Statement* value = nullptr;
            for (const Statement& s : line.statements)
                if (s.kind == Statement::Kind::Directive && (s.directive == DirectiveKind::Equ || s.directive == DirectiveKind::Defl))
                    value = &s;
            if (value)
                Define(line.label, value->args.empty() ? 0 : Value(value->args[0]), value->directive == DirectiveKind::Equ ? LabelUse::Equ : LabelUse::Defl);
            else if (line.label.find_first_not_of("0123456789") == std::string::npos)
                _temps.emplace_back(line.label, _pc);
            else
                Define(line.label, _pc, LabelUse::Unknown);
        }
        for (const Statement& s : line.statements)
        {
            if (_stopped)
                return;
            RunStatement(s, depth);
        }
    }

    void RunStatement(const Statement& s, int depth)
    {
        switch (s.kind)
        {
            case Statement::Kind::Instruction:
            {
                if (const std::optional<Statement> call = MacroNamedLike(s))
                {
                    Expand(*call, depth);   // sjasmplus looks for a macro first (ALASM's "push" macros)
                    return;
                }
                std::string error;
                const int size = InstructionSize(s, error);
                if (size == 0)
                    Report(Severity::Error, error);
                Operands(s);
                Store(std::vector<int16_t>(static_cast<size_t>(size), -2));
                Emit(size, LabelUse::Code);
                return;
            }
            case Statement::Kind::MacroCall: Expand(s, depth); return;
            case Statement::Kind::Raw: Report(Severity::Error, "line not understood: " + s.text); return;
            case Statement::Kind::Directive: break;
        }
        switch (s.directive)
        {
            case DirectiveKind::Org:
                if (s.args.empty())
                    return;
                if (_displaced)
                    _physical = Value(s.args[0]);   // ORG in a DISP block moves where the code goes
                else
                    _pc = _physical = Value(s.args[0]);
                if (s.args.size() > 1)
                    _page = static_cast<int>(Value(s.args[1]));
                return;
            case DirectiveKind::Equ:
            case DirectiveKind::Defl: return;   // with the label
            case DirectiveKind::Db:
            {
                const std::vector<int16_t> bytes = DataBytes(s);
                Store(bytes);
                Emit(static_cast<int64_t>(bytes.size()), LabelUse::Data);
                return;
            }
            case DirectiveKind::Dw:
            {
                std::vector<int16_t> bytes;
                for (const ir::Operand& o : s.operands)
                {
                    const int64_t v = o.kind == ir::Operand::Kind::String ? 0 : Value(o.expr);
                    bytes.push_back(static_cast<int16_t>(v & 0xFF));
                    bytes.push_back(static_cast<int16_t>((v >> 8) & 0xFF));
                }
                Store(bytes);
                Emit(static_cast<int64_t>(bytes.size()), LabelUse::Data);
                return;
            }
            case DirectiveKind::Ds:
            {
                const int64_t count = s.args.empty() ? 0 : Value(s.args[0]);
                if (count < 0)
                    Report(Severity::Error, "DS with a negative count");
                const int64_t fill = s.args.size() > 1 ? Value(s.args[1]) & 0xFF : 0;
                if (count > 0 && count <= 0x10000)
                    Store(std::vector<int16_t>(static_cast<size_t>(count), static_cast<int16_t>(fill)));
                Emit(count < 0 ? 0 : count, LabelUse::Data);
                return;
            }
            case DirectiveKind::Include: Include(s, depth); return;
            case DirectiveKind::Incbin: Emit(Incbin(s.text, s.args), LabelUse::Data); return;
            case DirectiveKind::Disp:
                if (!_displaced)
                    _physical = _pc;
                _displaced = true;
                _pc = s.args.empty() ? _pc : Value(s.args[0]);
                return;
            case DirectiveKind::Ent:
                if (_displaced)
                    _pc = _physical;
                _displaced = false;
                return;
            case DirectiveKind::End: _stopped = true; return;
            case DirectiveKind::Other: Other(s); return;
            case DirectiveKind::EndMacro:
            case DirectiveKind::EndRepeat:
            case DirectiveKind::EndWhile:
                Report(Severity::Error, "block end without its start");
                return;
            default: return;   // DISPLAY, SAVEBIN and the like lay out nothing
        }
    }

    /// The operands' values are not needed for the sizes, but evaluating them marks the names used (IFUSED) and
    /// reports the unknown ones
    void Operands(const Statement& s)
    {
        for (const ir::Operand& o : s.operands)
            if (o.kind != ir::Operand::Kind::Register && o.kind != ir::Operand::Kind::Condition && o.kind != ir::Operand::Kind::Indirect &&
                o.kind != ir::Operand::Kind::String)
                (void)Eval(o.expr);
    }

    /// A string's bytes in the file's code page (sjasmplus reads the file as bytes): a character the code page lacks is
    /// the '?' the file is written with
    std::vector<uint8_t> TextBytes(const std::string& text) const
    {
        const encoding::CodePage codePage = _chunk ? _chunk->codePage : encoding::CodePage::Utf8;
        if (codePage == encoding::CodePage::Utf8)
            return std::vector<uint8_t>(text.begin(), text.end());
        std::vector<uint8_t> out;
        for (size_t i = 0; i < text.size();)
        {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            char32_t codePoint = length == 1 ? lead : lead & (0x7F >> length);
            for (size_t k = 1; k < length && i + k < text.size(); ++k)
                codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
            uint8_t byte = '?';
            if (!encoding::CodePointToByte(codePoint, codePage, byte))
                byte = '?';
            out.push_back(byte);
            i += length;
        }
        return out;
    }

    /// The bytes DB / DM write (their values; a string's characters)
    std::vector<int16_t> DataBytes(const Statement& s)
    {
        std::vector<int16_t> bytes;
        for (const ir::Operand& o : s.operands)
        {
            if (o.kind == ir::Operand::Kind::String)
            {
                for (const uint8_t b : TextBytes(o.text))
                    bytes.push_back(b);
                continue;
            }
            // "ab"+#80: the string's characters, the operator on the last one
            const Expr* left = &o.expr;
            while (left->kind == Expr::Kind::Binary && !left->args.empty())
                left = &left->args[0];
            if (left->kind == Expr::Kind::Number && left->spelling == ir::NumberSpelling::Character && TextBytes(left->text).size() > 1)
            {
                const std::vector<uint8_t> chars = TextBytes(left->text);
                for (size_t k = 0; k + 1 < chars.size(); ++k)
                    bytes.push_back(chars[k]);
                bytes.push_back(static_cast<int16_t>(Value(o.expr) & 0xFF));
            }
            else
                bytes.push_back(static_cast<int16_t>(Value(o.expr) & 0xFF));
        }
        return bytes;
    }

    void Store(const std::vector<int16_t>& bytes)
    {
        for (size_t k = 0; k < bytes.size(); ++k)
            _memory[static_cast<size_t>((_physical + static_cast<int64_t>(k)) & 0xFFFF)] = bytes[k];
    }

    /// An instruction whose mnemonic a macro takes: the call, its arguments from the line's text
    std::optional<Statement> MacroNamedLike(const Statement& s) const
    {
        if (!_chunk || _lineIndex >= _chunk->raw.size())
            return std::nullopt;
        const std::string& raw = _chunk->raw[_lineIndex];
        // The word in the command place as written (macro names keep their case)
        size_t at = 0;
        if (!raw.empty() && raw[0] != ' ' && raw[0] != '\t')
            while (at < raw.size() && raw[at] != ' ' && raw[at] != '\t')
                ++at;
        while (at < raw.size() && (raw[at] == ' ' || raw[at] == '\t'))
            ++at;
        size_t end = at;
        while (end < raw.size() && raw[end] != ' ' && raw[end] != '\t' && raw[end] != ';')
            ++end;
        const std::string word = raw.substr(at, end - at);
        if (z80::Lower(word) != s.mnemonic || !_macros.count(word))
            return std::nullopt;
        // The arguments: up to a ';' or ':' outside quotes and brackets, split at the commas there
        Statement call;
        call.kind = Statement::Kind::MacroCall;
        call.mnemonic = word;
        std::string current;
        char quote = 0;
        int depth = 0;
        size_t k = end;
        for (; k < raw.size(); ++k)
        {
            const char c = raw[k];
            if (quote)
            {
                if (c == quote)
                    quote = 0;
            }
            else if (c == '"' || c == '\'')
                quote = c;
            else if (c == '(' || c == '<' || c == '{')
                ++depth;
            else if ((c == ')' || c == '>' || c == '}') && depth > 0)
                --depth;
            else if ((c == ';' || c == ':') && depth == 0)
                break;
            else if (c == ',' && depth == 0)
            {
                call.params.push_back(Trim(current));
                current.clear();
                continue;
            }
            current.push_back(c);
        }
        if (!Trim(current).empty() || !call.params.empty())
            call.params.push_back(Trim(current));
        return call;
    }

    int64_t Incbin(const std::string& name, const std::vector<Expr>& args)
    {
        std::optional<uint64_t> size = _options.fileSize ? _options.fileSize(name) : std::nullopt;
        if (!size)
        {
            Report(Severity::Error, "INCBIN file not found: " + name);
            return 0;
        }
        int64_t offset = args.empty() ? 0 : Value(args[0]);
        int64_t total = static_cast<int64_t>(*size);
        if (offset < 0)
            offset += total;   // sjasmplus: a negative offset counts from the end
        int64_t length = total - offset;
        if (args.size() > 1)
        {
            const int64_t asked = Value(args[1]);
            length = asked < 0 ? length + asked : asked;
        }
        return length < 0 ? 0 : length;
    }

    void Include(const Statement& s, int depth)
    {
        const auto found = _byName.find(BaseName(s.text));
        if (found == _byName.end())
        {
            Report(Severity::Error, "INCLUDE file not in the project: " + s.text);
            return;
        }
        if (depth >= kMaxNesting)
        {
            Report(Severity::Error, "INCLUDE nested too deep");
            return;
        }
        const Chunk* back = _chunk;
        const size_t backLine = _lineIndex;
        RunChunk(_files[found->second], depth + 1);
        _chunk = back;
        _lineIndex = backLine;
    }

    void Expand(const Statement& s, int depth)
    {
        const auto found = _macros.find(s.mnemonic);
        if (found == _macros.end())
        {
            Report(Severity::Error, "unknown instruction or macro " + s.mnemonic);
            return;
        }
        if (depth >= kMaxNesting)
        {
            Report(Severity::Error, "macro nested too deep: " + s.mnemonic);
            return;
        }
        const Macro& m = found->second;
        std::vector<std::string> args;
        for (const std::string& p : s.params)
            args.push_back(Argument(p));
        std::string text;
        for (size_t k = 0; k < m.body.size(); ++k)
            text += (k ? "\n" : "") + Substitute(m.body[k], m.params, args);
        auto expansion = std::make_unique<Chunk>();
        expansion->file = m.file;
        expansion->codePage = m.codePage;
        const dialects::SjasmplusFrontend frontend;
        const SourceDocument document = SourceDocument::FromText(text, "sjasmplus");
        expansion->lines = frontend.Parse(document).program.lines;
        for (size_t k = 0; k < expansion->lines.size(); ++k)
        {
            expansion->raw.push_back(k < document.lines.size() ? document.lines[k].text : std::string());
            expansion->fileLines.push_back(k < m.bodyLines.size() ? m.bodyLines[k] : 0);
        }
        const std::string scope = _macroScope;
        _macroScope = ">" + s.mnemonic + std::to_string(++_expansions);
        const Chunk* back = _chunk;
        const size_t backLine = _lineIndex;
        RunChunk(*expansion, depth + 1);
        _chunk = back;
        _lineIndex = backLine;
        _macroScope = scope;
    }

    void Other(const Statement& s)
    {
        const std::string text = Trim(s.text);
        const size_t blank = text.find_first_of(" \t");
        const std::string word = z80::Upper(text.substr(0, blank));
        const std::string rest = blank == std::string::npos ? std::string() : Trim(text.substr(blank));
        if (word == "DEFINE" || word == "DEFINE+")
        {
            _defines.insert(Trim(rest.substr(0, rest.find_first_of(" \t"))));
            return;
        }
        if (word == "UNDEFINE")
        {
            _defines.erase(rest);
            return;
        }
        if (word == "MODULE")
        {
            _modules.push_back(rest);
            _lastGlobal.clear();
            return;
        }
        if (word == "ENDMODULE")
        {
            if (!_modules.empty())
                _modules.pop_back();
            return;
        }
        if (word == "ALIGN")
        {
            const int64_t boundary = rest.empty() ? 4 : Value(ParseText(rest.substr(0, rest.find(','))));
            if (boundary > 0)
                Emit((boundary - _pc % boundary) % boundary, LabelUse::Data);
            return;
        }
        if (word == "DZ" || word == "DC" || word == "ABYTE" || word == "ABYTEC" || word == "ABYTEZ" || word == "D24" || word == "DD" ||
            word == "DWORD")
        {
            Statement data;
            std::string items = rest;
            if (word.rfind("ABYTE", 0) == 0)
            {
                const size_t blankAfterOffset = items.find_first_of(" \t");
                items = blankAfterOffset == std::string::npos ? std::string() : Trim(items.substr(blankAfterOffset));
            }
            const dialects::SjasmplusFrontend frontend;
            const FrontendResult parsed = frontend.Parse(SourceDocument::FromText("        DB " + items, "sjasmplus"));
            if (!parsed.program.lines.empty() && !parsed.program.lines[0].statements.empty())
                data = parsed.program.lines[0].statements[0];
            int64_t bytes = static_cast<int64_t>(DataBytes(data).size());
            if (word == "D24")
                bytes = 3 * static_cast<int64_t>(data.operands.size());
            else if (word == "DD" || word == "DWORD")
                bytes = 4 * static_cast<int64_t>(data.operands.size());
            else if (word == "DZ" || word == "ABYTEZ")
                bytes += 1;
            Emit(bytes, LabelUse::Data);
            return;
        }
        if (word == "INSERT" || word == "BINARY")
        {
            const dialects::SjasmplusFrontend frontend;
            const FrontendResult parsed = frontend.Parse(SourceDocument::FromText("        INCBIN " + rest, "sjasmplus"));
            if (!parsed.program.lines.empty() && !parsed.program.lines[0].statements.empty())
                Emit(Incbin(parsed.program.lines[0].statements[0].text, parsed.program.lines[0].statements[0].args), LabelUse::Data);
            return;
        }
        static const std::set<std::string> kSized = {"DG", "DEFG", "DH", "DEFH", "STRUCT", "INCHOB", "INCTRD", "LUA", "INCLUDELUA", "DEFARRAY",
                                                     "RELOCATE_TABLE", "SAVENEX", "FPOS"};
        if (kSized.count(word))
            Report(Severity::Error, word + " is not laid out: the labels after it may be wrong");
    }
};
}  // namespace

LayoutResult Layout(const std::vector<ProjectFile>& files, size_t main, const LayoutOptions& options)
{
    Engine engine(files, options);
    return engine.Run(main);
}
}  // namespace unrealasm::layout
