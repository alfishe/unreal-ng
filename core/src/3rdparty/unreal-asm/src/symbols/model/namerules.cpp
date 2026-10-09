#include "unrealasm/symbols/namerules.h"

#include <array>
#include <cctype>
#include <map>
#include <set>

#include "dialects/common/z80.h"
#include "unrealasm/symbols/codec.h"

namespace unrealasm::symbols
{
namespace
{
constexpr std::array<std::string_view, 3> kUnrepresentable = {"fold", "comment", "drop"};

bool Allowed(char c, const NameRules& r)
{
    if (r.forbidden.find(c) != std::string_view::npos || c == '\n' || c == '\r')
        return false;
    switch (r.charset)
    {
        case Charset::Any: return true;
        case Charset::NoBlank: return c != ' ' && c != '\t';
        case Charset::Identifier: return std::isalnum(static_cast<unsigned char>(c)) || r.extra.find(c) != std::string_view::npos;
    }
    return true;
}

bool AllowedFirst(char c, const NameRules& r)
{
    if (r.charset != Charset::Identifier)
        return Allowed(c, r);
    return std::isalpha(static_cast<unsigned char>(c)) || r.firstExtra.find(c) != std::string_view::npos;
}

bool Reserved(const std::string& name, const NameRules& r)
{
    const std::string lower = dialects::z80::Lower(name);
    if (r.reserveZ80 && (dialects::z80::IsMnemonic(lower) || dialects::z80::IsRegister(dialects::z80::NormalizeRegister(lower)) ||
                         dialects::z80::IsCondition(lower)))
        return true;
    for (const std::string_view w : r.reserved)
        if (lower == dialects::z80::Lower(w))
            return true;
    return false;
}

std::string Mangle(const std::string& name, const NameRules& r)
{
    std::string out = r.upper ? dialects::z80::Upper(name) : name;
    for (char& c : out)
        if (!Allowed(c, r))
            c = '_';
    if (out.empty())
        out = "_";
    if (!AllowedFirst(out[0], r))
        out.insert(out.begin(), AllowedFirst('_', r) ? '_' : 'L');
    if (Reserved(out, r))
        out += '_';
    if (r.maxLength && out.size() > r.maxLength)
    {
        uint32_t hash = 2166136261u;   // FNV-1a of the original name
        for (const unsigned char c : name)
            hash = (hash ^ c) * 16777619u;
        out = out.substr(0, r.maxLength - 5) + "_" + dialects::z80::HexDigits(hash & 0xFFFF, 4);
    }
    return out;
}
}  // namespace

std::vector<Rename> ApplyNameRules(SymbolFile& file, const NameRules& rules, std::string_view keepFrom)
{
    std::vector<Rename> renames;
    std::set<std::string> used;
    std::map<std::string, std::string> map;   // old name -> new name
    for (SymbolSet& set : file.sets)
        for (Symbol& s : set.symbols)
        {
            const bool keep = !keepFrom.empty() && s.provenance.importer == keepFrom;
            std::string name = keep ? s.name : Mangle(s.name, rules);
            if (used.count(name))
            {
                const std::string base = name;
                for (int n = 2; used.count(name); ++n)
                    name = base + "_" + std::to_string(n);
            }
            used.insert(name);
            if (name != s.name)
            {
                renames.push_back({s.name, name});
                map.emplace(s.name, name);
                s.name = name;
            }
        }
    if (!map.empty())
        for (SymbolSet& set : file.sets)
            for (Symbol& s : set.symbols)
                if (const auto it = map.find(s.parent); it != map.end())
                    s.parent = it->second;
    return renames;
}

std::string_view UnrepresentableName(Unrepresentable u)
{
    return kUnrepresentable[static_cast<size_t>(u)];
}

bool ParseUnrepresentable(std::string_view text, Unrepresentable& out)
{
    for (size_t i = 0; i < kUnrepresentable.size(); ++i)
        if (text == kUnrepresentable[i])
        {
            out = static_cast<Unrepresentable>(i);
            return true;
        }
    return false;
}

PreparedFile Prepare(const SymbolFile& file, const CodecInfo& info, const SymbolEncodeOptions& options, Diagnostics& diagnostics)
{
    PreparedFile p;
    p.file = file;
    const std::string prefix = info.comment.empty() ? std::string() : info.comment + " ";
    for (const Rename& r : ApplyNameRules(p.file, info.rules, info.id))
    {
        diagnostics.push_back({Severity::Info, 0, 0, "renamed " + r.from + " -> " + r.to});
        if (!prefix.empty())
            p.header.push_back(prefix + "renamed " + r.from + " -> " + r.to);
    }
    if (info.pages || options.unrepresentable == Unrepresentable::Fold)
        return p;
    for (SymbolSet& set : p.file.sets)
    {
        std::vector<Symbol> kept;
        for (Symbol& s : set.symbols)
        {
            const SpaceKind k = s.location.space.kind;
            const bool paged = k == SpaceKind::Rom || k == SpaceKind::Ram || k == SpaceKind::Cache;
            if (!paged)
            {
                kept.push_back(std::move(s));
                continue;
            }
            const std::string where = s.name + " " + s.location.space.Format() + ":#" + dialects::z80::HexDigits(s.location.offset, 4);
            if (options.unrepresentable == Unrepresentable::Comment && !prefix.empty())
                p.header.push_back(prefix + where);
            else
                diagnostics.push_back({Severity::Warning, 0, 0, where + ": the format has no pages (dropped)"});
        }
        set.symbols = std::move(kept);
    }
    return p;
}
}  // namespace unrealasm::symbols
