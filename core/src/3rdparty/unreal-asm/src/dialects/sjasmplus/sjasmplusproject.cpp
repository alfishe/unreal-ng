#include "dialects/sjasmplus/sjasmplusproject.h"

#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "dialects/common/z80.h"
#include "unrealasm/layout.h"

namespace unrealasm::dialects
{
namespace
{
/// The code of a line and the rest from its comment (";" outside quotes; AF' is a register, not a quote)
std::pair<std::string, std::string> SplitComment(const std::string& text)
{
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (quote)
        {
            if (c == quote)
                quote = 0;
            continue;
        }
        const bool shadowAf = c == '\'' && i >= 2 && std::toupper(static_cast<unsigned char>(text[i - 1])) == 'F' &&
                              std::toupper(static_cast<unsigned char>(text[i - 2])) == 'A';
        if ((c == '"' || c == '\'') && !shadowAf)
            quote = c;
        else if (c == ';')
            return {text.substr(0, i), text.substr(i)};
    }
    return {text, std::string()};
}

/// Splits at a separator outside quotes, parentheses and braces
std::vector<std::string> SplitTop(const std::string& text, char separator)
{
    std::vector<std::string> out(1);
    char quote = 0;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (quote)
        {
            if (c == quote)
                quote = 0;
        }
        else if ((c == '"' || c == '\'') && !(c == '\'' && i >= 2 && std::toupper(static_cast<unsigned char>(text[i - 1])) == 'F' &&
                                                std::toupper(static_cast<unsigned char>(text[i - 2])) == 'A'))
            quote = c;
        else if (c == '(' || c == '{' || c == '[')
            ++depth;
        else if ((c == ')' || c == '}' || c == ']') && depth > 0)
            --depth;
        else if (c == separator && depth == 0)
        {
            out.emplace_back();
            continue;
        }
        out.back().push_back(c);
    }
    return out;
}

std::string Trim(const std::string& text)
{
    const size_t a = text.find_first_not_of(" \t");
    if (a == std::string::npos)
        return std::string();
    const size_t b = text.find_last_not_of(" \t");
    return text.substr(a, b - a + 1);
}

/// "(...)" whose first parenthesis closes at its end
bool Parenthesized(const std::string& e)
{
    if (e.size() < 2 || e.front() != '(' || e.back() != ')')
        return false;
    int depth = 0;
    for (size_t i = 0; i < e.size(); ++i)
    {
        depth += e[i] == '(';
        depth -= e[i] == ')';
        if (depth == 0 && i + 1 < e.size())
            return false;
    }
    return true;
}

std::string Low(std::string e)
{
    e = Trim(e);
    if (e.size() > 1 && e[0] == '+' && Parenthesized(e.substr(1)))
        e = e.substr(1);
    bool simple = !e.empty();
    for (const char c : e)
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '.' && c != '#' && c != '$' && c != '@')
            simple = false;
    return simple || Parenthesized(e) ? "low " + e : "low (" + e + ")";
}

/// The statements of a line's code: the label (column 0, up to the first blank) apart, the rest split at ':'
struct Statements
{
    std::string label;                 ///< with the blanks after it
    std::vector<std::string> parts;    ///< each statement's text, blanks kept
};

Statements SplitStatements(const std::string& code)
{
    Statements s;
    size_t at = 0;
    if (!code.empty() && code[0] != ' ' && code[0] != '\t')
        while (at < code.size() && code[at] != ' ' && code[at] != '\t')
            ++at;
    s.label = code.substr(0, at);
    s.parts = SplitTop(code.substr(at), ':');
    return s;
}

/// Wraps operand `operand` of the statement text in low (an IN / OUT port inside its parentheses); false: not found
bool WrapOperand(std::string& statement, size_t operand)
{
    const size_t start = statement.find_first_not_of(" \t");
    if (start == std::string::npos)
        return false;
    size_t end = statement.find_first_of(" \t", start);
    if (end == std::string::npos)
        return false;
    const std::string mnemonic = z80::Lower(statement.substr(start, end - start));
    const size_t opsAt = statement.find_first_not_of(" \t", end);
    if (opsAt == std::string::npos)
        return false;
    const std::string tail = statement.substr(statement.find_last_not_of(" \t") + 1);
    std::vector<std::string> ops = SplitTop(Trim(statement.substr(opsAt)), ',');
    if (operand >= ops.size())
        return false;
    std::string op = Trim(ops[operand]);
    if ((mnemonic == "in" || mnemonic == "out") && Parenthesized(op))
        op = "(" + Low(op.substr(1, op.size() - 2)) + ")";
    else
        op = Low(op);
    ops[operand] = op;
    std::string joined;
    for (size_t k = 0; k < ops.size(); ++k)
        joined += (k ? "," : "") + Trim(ops[k]);
    statement = statement.substr(0, opsAt) + joined + tail;
    return true;
}

/// A number as sjasmplus writes it (#C000, $C000, 0xC000, C000h, 49152); none for an expression
std::optional<unsigned long> PlainNumber(const std::string& text)
{
    std::string a = Trim(text);
    int base = 10;
    if (!a.empty() && (a[0] == '#' || a[0] == '$'))
    {
        a = a.substr(1);
        base = 16;
    }
    else if (a.size() > 2 && a[0] == '0' && (a[1] == 'x' || a[1] == 'X'))
    {
        a = a.substr(2);
        base = 16;
    }
    else if (!a.empty() && (a.back() == 'h' || a.back() == 'H'))
    {
        a.pop_back();
        base = 16;
    }
    if (a.empty() || a.size() > 8)
        return std::nullopt;
    for (const char c : a)
        if (!std::isxdigit(static_cast<unsigned char>(c)) || (base == 10 && !std::isdigit(static_cast<unsigned char>(c))))
            return std::nullopt;
    return std::stoul(a, nullptr, base);
}

/// ORG address,page: the address is a number in #C000-#FFFF (the page's own slot)
bool OrgInPageSlot(const std::string& address)
{
    const std::optional<unsigned long> v = PlainNumber(address);
    return v && *v >= 0xC000 && *v <= 0xFFFF;
}

/// The page operands of the ORGs of the files (ORG address,page), lower case; a number also as its decimal value
std::set<std::string> OrgPages(const std::vector<ProjectFile>& files)
{
    std::set<std::string> pages;
    for (const ProjectFile& f : files)
        for (const SourceLine& line : f.document.lines)
        {
            const Statements s = SplitStatements(SplitComment(line.text).first);
            for (const std::string& part : s.parts)
            {
                const std::string statement = Trim(part);
                if (statement.size() < 4 || z80::Lower(statement.substr(0, 4)) != "org ")
                    continue;
                const std::vector<std::string> args = SplitTop(statement.substr(4), ',');
                if (args.size() < 2)
                    continue;
                pages.insert(z80::Lower(Trim(args[1])));
                if (const std::optional<unsigned long> v = PlainNumber(args[1]))
                    pages.insert(std::to_string(*v));
            }
        }
    return pages;
}

/// The page an ObjTab entry names. The object saver writes its byte to port #7FFD; the conversion keeps an ORG's page
/// as a sjasmplus page of that number (ORG #C000,pgzx), so a value the project's ORGs may name stays as it is (an
/// expression, when some ORG has a page: pgtunnelzx=pgeffects). Any other value is the RAM page the port maps to #C000,
/// where a source assembles without an ORG page: bits 0-2, and 6-7 as pages 8-31 on the Pentagon 512 / 1024 (sts7's
/// #10 = page 0)
std::string ObjectPage(const std::string& value, const std::set<std::string>& orgPages)
{
    const std::optional<unsigned long> v = PlainNumber(value);
    if (orgPages.count(z80::Lower(Trim(value))) || (v && orgPages.count(std::to_string(*v))) || (!v && !orgPages.empty()))
        return value;
    if (v)
        return std::to_string((*v & 7) | ((*v & 0xC0) >> 3));
    return "((" + value + ")&7)|((" + value + ")>>3&#18)";
}

void MarkOrgPages(SourceDocument& document)
{
    for (SourceLine& line : document.lines)
    {
        const auto [code, comment] = SplitComment(line.text);
        const Statements s = SplitStatements(code);
        if (s.parts.size() != 1)
            continue;
        const std::string statement = Trim(s.parts[0]);
        if (statement.size() < 4 || z80::Lower(statement.substr(0, 4)) != "org ")
            continue;
        const std::vector<std::string> args = SplitTop(statement.substr(4), ',');
        if (args.size() < 2 || OrgInPageSlot(args[0]) || comment.find("orgpage-ok") != std::string::npos)
            continue;
        std::string padded = code;
        while (padded.size() < 40)
            padded.push_back(' ');
        line.text = comment.empty() ? padded + "; orgpage-ok: the page goes to #C000, as in the source's assembler"
                                    : padded + "; orgpage-ok " + comment.substr(1);
    }
}

/// The object saver's table after the label ObjTab: 9-character name and type, address, length, page, start
void SaveObjects(SourceDocument& document, Diagnostics& diagnostics, const std::string& file, const std::set<std::string>& orgPages)
{
    std::vector<std::string> items;
    bool inTable = false;
    for (const SourceLine& line : document.lines)
    {
        const std::string code = SplitComment(line.text).first;
        Statements s = SplitStatements(code);
        if (!inTable)
        {
            if (z80::Lower(Trim(s.label)) != "objtab")
                continue;
            inTable = true;
        }
        if (Trim(code).empty() || (Trim(s.label).size() && s.parts.size() == 1 && Trim(s.parts[0]).empty()))
            continue;
        if (s.parts.size() != 1)
            break;
        const std::string statement = Trim(s.parts[0]);
        const std::string word = z80::Lower(statement.substr(0, statement.find_first_of(" \t")));
        if (word != "db" && word != "dw")
            break;
        for (const std::string& item : SplitTop(Trim(statement.substr(2)), ','))
            items.push_back(Trim(item));
    }
    if (!inTable)
        return;
    std::vector<std::string> block;
    for (size_t k = 0; k + 5 <= items.size(); k += 5)
    {
        const std::string& quoted = items[k];
        if (quoted.size() != 11 || (quoted.front() != '\'' && quoted.front() != '"') || quoted.back() != quoted.front())
        {
            diagnostics.push_back({Severity::Warning, 0, 0, file + ": ObjTab entry " + std::to_string(k / 5 + 1) + " is not a 9-character name and type: no SAVEBIN"});
            return;
        }
        std::string name = Trim(quoted.substr(1, 8));
        const char type = quoted[9];
        // As ALASM's INCBIN names a file: the type letter after a dot unless it is C
        const std::string host = type == 'C' ? name : name + "." + std::string(1, type);
        block.push_back("        PAGE " + ObjectPage(items[k + 3], orgPages));
        block.push_back("        SAVEBIN \"" + host + "\"," + items[k + 1] + "," + items[k + 2]);
    }
    if (block.empty())
        return;
    bool device = false;
    for (const SourceLine& line : document.lines)
    {
        const std::string code = z80::Lower(Trim(SplitComment(line.text).first));
        device = device || code.rfind("device ", 0) == 0;
    }
    if (!device)
        document.lines.insert(document.lines.begin(), SourceLine{"        DEVICE ZXSPECTRUM4096   ; unreal-asm: SAVEBIN needs a device", {}, -1, 0});
    document.lines.push_back({"", {}, -1, 0});
    document.lines.push_back({"; unreal-asm: the objects of ObjTab, which the object saver writes to disk when the source runs in its assembler", {}, -1, 0});
    document.lines.push_back({"        SLOT 3", {}, -1, 0});
    for (const std::string& l : block)
        document.lines.push_back({l, {}, -1, 0});
}
}  // namespace

void FinishSjasmplusProject(std::vector<ProjectFile>& files, const std::vector<bool>& included, const BackendOptions& options,
                            Diagnostics& diagnostics)
{
    // The byte operands out of range, from the layout of each main source with what it INCLUDEs
    layout::LayoutOptions layoutOptions;
    if (options.fileSize)
        layoutOptions.fileSize = options.fileSize;
    else if (options.dataFiles)
        layoutOptions.fileSize = [&options](const std::string& name) -> std::optional<uint64_t> {
            const std::vector<uint8_t> bytes = options.dataFiles(name);
            return bytes.empty() ? std::nullopt : std::optional<uint64_t>(bytes.size());
        };
    std::map<std::string, std::set<std::tuple<uint32_t, size_t, size_t>>> wide;   // file -> line, statement, operand
    for (size_t main = 0; main < files.size(); ++main)
    {
        if (main < included.size() && included[main])
            continue;
        const layout::LayoutResult laid = layout::Layout(files, main, layoutOptions);
        for (const layout::WideByteOperand& w : laid.wideBytes)
            wide[w.file].insert({w.line, w.statement, w.operand});
    }
    for (ProjectFile& f : files)
    {
        const auto found = wide.find(f.name);
        if (found != wide.end())
            for (const auto& [line, statement, operand] : found->second)
            {
                if (line == 0 || line > f.document.lines.size())
                    continue;
                std::string& text = f.document.lines[line - 1].text;
                auto [code, comment] = SplitComment(text);
                Statements s = SplitStatements(code);
                if (statement >= s.parts.size() || !WrapOperand(s.parts[statement], operand))
                {
                    diagnostics.push_back({Severity::Warning, line, 0, f.name + ": a byte operand wider than a byte not marked with low"});
                    continue;
                }
                std::string rebuilt = s.label;
                for (size_t k = 0; k < s.parts.size(); ++k)
                    rebuilt += (k ? ":" : "") + s.parts[k];
                if (!comment.empty())
                {
                    // Keep the comment where it was, when the code still fits before it
                    const size_t column = code.size();
                    std::string trimmed = rebuilt.substr(0, rebuilt.find_last_not_of(" \t") + 1);
                    if (trimmed.size() + 1 < column)
                        trimmed.append(column - trimmed.size(), ' ');
                    else
                        trimmed.push_back(' ');
                    rebuilt = trimmed + comment;
                }
                text = rebuilt;
            }
        MarkOrgPages(f.document);
    }
    // The object saver: a unit with an ObjTab that INCLUDEs SAVEOBJ*
    const std::set<std::string> orgPages = OrgPages(files);
    for (size_t k = 0; k < files.size(); ++k)
    {
        bool saver = false;
        for (const SourceLine& line : files[k].document.lines)
        {
            const std::string code = z80::Lower(Trim(SplitComment(line.text).first));
            if (code.rfind("include ", 0) == 0 && code.find("saveobj") != std::string::npos)
                saver = true;
        }
        if (saver)
            SaveObjects(files[k].document, diagnostics, files[k].name, orgPages);
    }
}
}  // namespace unrealasm::dialects
