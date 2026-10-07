#include "unrealasm/symbols/store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <unordered_map>

namespace unrealasm::symbols
{
namespace
{
constexpr size_t kMaxName = 1024;
constexpr std::array<std::string_view, 4> kPolicyNames = {"both", "keep", "replace", "fail"};

std::string Key(const std::string& name, CaseRule rule)
{
    if (rule == CaseRule::Exact)
        return name;
    std::string out = name;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

void Warn(Diagnostics& diagnostics, const Symbol& record, std::string message)
{
    diagnostics.push_back({Severity::Warning, record.provenance.line, 0, std::move(message)});
}

/// Fills the fields `into` does not know from `from` (same name, same place); true when anything changed
bool FillEmpty(Symbol& into, const Symbol& from)
{
    bool changed = false;
    auto fill = [&changed](auto& field, const auto& value, const auto& empty) {
        if (field == empty && !(value == empty))
        {
            field = value;
            changed = true;
        }
    };
    fill(into.kind, from.kind, SymbolKind::Unknown);
    fill(into.size, from.size, 0u);
    fill(into.window, from.window, -1);
    fill(into.parent, from.parent, std::string());
    fill(into.module, from.module, std::string());
    fill(into.source.file, from.source.file, std::string());
    fill(into.source.line, from.source.line, 0u);
    fill(into.source.column, from.source.column, 0u);
    fill(into.comment, from.comment, std::string());
    for (const std::string& alias : from.aliases)
        if (alias != into.name && std::find(into.aliases.begin(), into.aliases.end(), alias) == into.aliases.end())
        {
            into.aliases.push_back(alias);
            changed = true;
        }
    return changed;
}
}  // namespace

std::string_view PolicyName(MergePolicy policy)
{
    return kPolicyNames[static_cast<size_t>(policy)];
}

bool ParsePolicy(std::string_view text, MergePolicy& out)
{
    for (size_t i = 0; i < kPolicyNames.size(); ++i)
        if (text == kPolicyNames[i])
        {
            out = static_cast<MergePolicy>(i);
            return true;
        }
    return false;
}

std::vector<Symbol> Normalize(std::vector<Symbol> records, const ImportOptions& options, Diagnostics& diagnostics)
{
    std::vector<Symbol> out;
    std::unordered_map<std::string, size_t> byName;
    for (Symbol& r : records)
    {
        if (r.name.empty())
        {
            Warn(diagnostics, r, "a symbol without a name (skipped)");
            continue;
        }
        if (r.name.size() > kMaxName)
        {
            Warn(diagnostics, r, "a name longer than " + std::to_string(kMaxName) + " bytes (cut)");
            r.name.resize(kMaxName);
        }
        // DT-1: the record's own page, else the option's space, else const for a constant, else the CPU view
        const bool ownSpace = r.location.space.kind != SpaceKind::CpuView || r.location.space.cpu != "main";
        if (!ownSpace)
        {
            if (options.space)
                r.location.space = *options.space;
            else if (r.kind == SymbolKind::Const)
                r.location.space = AddressSpace{"main", SpaceKind::Constant, 0, {}};
        }
        if (r.location.space.kind != SpaceKind::Constant)
            r.location.offset += options.base;
        const uint32_t extent = r.location.space.Extent();
        if (extent && r.location.offset >= extent)
        {
            Warn(diagnostics, r, r.name + ": offset " + std::to_string(r.location.offset) + " outside " + r.location.space.Format() + " (skipped)");
            continue;
        }
        // The same name twice in one file: once at the same place, the first wins elsewhere
        if (const auto it = byName.find(r.name); it != byName.end())
        {
            Symbol& first = out[it->second];
            if (first.location == r.location)
                FillEmpty(first, r);
            else
                Warn(diagnostics, r, r.name + " is defined again at another place (the first one is kept)");
            continue;
        }
        byName.emplace(r.name, out.size());
        out.push_back(std::move(r));
    }
    return out;
}

bool Merge(SymbolSet& set, const std::vector<Symbol>& records, MergePolicy policy, ImportReport& report)
{
    std::unordered_map<std::string, size_t> byName;
    std::map<Location, std::vector<size_t>> byLocation;
    auto rebuild = [&] {
        byName.clear();
        byLocation.clear();
        for (size_t i = 0; i < set.symbols.size(); ++i)
        {
            byName.emplace(Key(set.symbols[i].name, set.caseRule), i);
            byLocation[set.symbols[i].location].push_back(i);
        }
    };
    rebuild();

    if (policy == MergePolicy::Fail)
    {
        // Nothing changes when any record conflicts
        bool conflict = false;
        for (const Symbol& r : records)
        {
            const auto named = byName.find(Key(r.name, set.caseRule));
            if (named != byName.end() && set.symbols[named->second].location != r.location)
            {
                report.conflicts.push_back({Conflict::Type::Moved, r.name, {}, set.symbols[named->second].location, r.location, r.provenance.line, "failed"});
                conflict = true;
            }
            else if (named == byName.end())
                if (const auto at = byLocation.find(r.location); at != byLocation.end())
                {
                    report.conflicts.push_back({Conflict::Type::SamePlace, r.name, set.symbols[at->second.front()].name, r.location, r.location, r.provenance.line, "failed"});
                    conflict = true;
                }
        }
        if (conflict)
            return false;
    }

    for (const Symbol& r : records)
    {
        const auto named = byName.find(Key(r.name, set.caseRule));
        if (named != byName.end())
        {
            Symbol& old = set.symbols[named->second];
            if (old.location == r.location)
            {
                report.updated += FillEmpty(old, r);
                continue;
            }
            Conflict c{Conflict::Type::Moved, r.name, {}, old.location, r.location, r.provenance.line, "kept"};
            if (policy == MergePolicy::Replace)
            {
                old = r;
                c.resolution = "moved";
                rebuild();
            }
            else
                ++report.skipped;
            report.conflicts.push_back(std::move(c));
            continue;
        }
        const auto at = byLocation.find(r.location);
        if (at == byLocation.end())
        {
            set.symbols.push_back(r);
            byName.emplace(Key(r.name, set.caseRule), set.symbols.size() - 1);
            byLocation[r.location].push_back(set.symbols.size() - 1);
            ++report.added;
            continue;
        }
        Symbol& other = set.symbols[at->second.front()];
        Conflict c{Conflict::Type::SamePlace, r.name, other.name, other.location, r.location, r.provenance.line, "alias"};
        switch (policy)
        {
            case MergePolicy::Both:
                if (std::find(other.aliases.begin(), other.aliases.end(), r.name) == other.aliases.end())
                    other.aliases.push_back(r.name);
                byName.emplace(Key(r.name, set.caseRule), at->second.front());
                ++report.aliased;
                break;
            case MergePolicy::Keep:
                c.resolution = "kept";
                ++report.skipped;
                break;
            case MergePolicy::Replace:
            case MergePolicy::Fail:
                other = r;
                c.resolution = "replaced";
                rebuild();
                break;
        }
        report.conflicts.push_back(std::move(c));
    }
    return true;
}

SymbolStore::SymbolStore()
{
    Publish({});
}

ImportReport SymbolStore::Import(std::vector<Symbol> records, const ImportOptions& options)
{
    std::lock_guard lock(_change);
    ImportReport report;
    report.set = options.set.empty() ? std::string("user") : options.set;
    std::vector<SymbolSet> sets = *_sets;
    auto it = std::find_if(sets.begin(), sets.end(), [&](const SymbolSet& s) { return s.id == report.set; });
    if (it == sets.end())
    {
        SymbolSet set;
        set.id = report.set;
        set.title = options.title.empty() ? report.set : options.title;
        set.origin = options.origin;
        sets.push_back(std::move(set));
        it = sets.end() - 1;
    }
    const std::vector<Symbol> normalized = Normalize(std::move(records), options, report.diagnostics);
    report.ok = Merge(*it, normalized, options.policy, report);
    if (report.ok)
        Publish(std::move(sets));
    return report;
}

void SymbolStore::PutSets(std::vector<SymbolSet> incoming)
{
    std::lock_guard lock(_change);
    std::vector<SymbolSet> sets = *_sets;
    for (SymbolSet& set : incoming)
    {
        const auto it = std::find_if(sets.begin(), sets.end(), [&](const SymbolSet& s) { return s.id == set.id; });
        if (it != sets.end())
            *it = std::move(set);
        else
            sets.push_back(std::move(set));
    }
    Publish(std::move(sets));
}

bool SymbolStore::Drop(const std::string& id)
{
    std::lock_guard lock(_change);
    std::vector<SymbolSet> sets = *_sets;
    const auto it = std::find_if(sets.begin(), sets.end(), [&](const SymbolSet& s) { return s.id == id; });
    if (it == sets.end())
        return false;
    sets.erase(it);
    Publish(std::move(sets));
    return true;
}

bool SymbolStore::SetEnabled(const std::string& id, bool enabled)
{
    std::lock_guard lock(_change);
    std::vector<SymbolSet> sets = *_sets;
    const auto it = std::find_if(sets.begin(), sets.end(), [&](const SymbolSet& s) { return s.id == id; });
    if (it == sets.end())
        return false;
    it->enabled = enabled;
    Publish(std::move(sets));
    return true;
}

bool SymbolStore::SetPriority(const std::string& id, int priority)
{
    std::lock_guard lock(_change);
    std::vector<SymbolSet> sets = *_sets;
    const auto it = std::find_if(sets.begin(), sets.end(), [&](const SymbolSet& s) { return s.id == id; });
    if (it == sets.end())
        return false;
    it->priority = priority;
    Publish(std::move(sets));
    return true;
}

void SymbolStore::Clear()
{
    std::lock_guard lock(_change);
    Publish({});
}

std::vector<SymbolSet> SymbolStore::Sets() const
{
    std::lock_guard lock(_change);
    return *_sets;
}

std::optional<SymbolSet> SymbolStore::GetSet(const std::string& id) const
{
    std::lock_guard lock(_change);
    for (const SymbolSet& set : *_sets)
        if (set.id == id)
            return set;
    return std::nullopt;
}

std::shared_ptr<const SymbolIndex> SymbolStore::Index() const
{
    std::lock_guard lock(_publish);
    return _index;
}

void SymbolStore::Publish(std::vector<SymbolSet> sets)
{
    _sets = std::make_shared<const std::vector<SymbolSet>>(std::move(sets));
    auto index = std::make_shared<const SymbolIndex>(_sets);
    std::lock_guard lock(_publish);
    _index = std::move(index);
}
}  // namespace unrealasm::symbols
