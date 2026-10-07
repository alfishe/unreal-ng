#include "unrealasm/symbols/index.h"

#include <algorithm>
#include <cctype>

namespace unrealasm::symbols
{
namespace
{
std::string Upper(const std::string& s)
{
    std::string out = s;
    for (char& c : out)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}
}  // namespace

SymbolIndex::SymbolIndex(std::shared_ptr<const std::vector<SymbolSet>> sets) : _sets(std::move(sets))
{
    // Sets by priority, highest first (stable: the earlier set wins a tie)
    std::vector<const SymbolSet*> order;
    for (const SymbolSet& set : *_sets)
        if (set.enabled)
            order.push_back(&set);
    std::stable_sort(order.begin(), order.end(), [](const SymbolSet* a, const SymbolSet* b) { return a->priority > b->priority; });

    for (const SymbolSet* set : order)
        for (const Symbol& s : set->symbols)
            if (s.enabled)
                _spaces.push_back(s.location.space);
    std::sort(_spaces.begin(), _spaces.end());
    _spaces.erase(std::unique(_spaces.begin(), _spaces.end()), _spaces.end());

    for (const SymbolSet* set : order)
        for (const Symbol& s : set->symbols)
        {
            if (!s.enabled)
                continue;
            _byLocation.push_back({*SpaceKey(s.location.space), s.location.offset, set->priority, &s});
            auto& names = set->caseRule == CaseRule::Fold ? _byFoldedName : _byName;
            const bool fold = set->caseRule == CaseRule::Fold;
            names.emplace(fold ? Upper(s.name) : s.name, Named{&s, set->priority});   // emplace keeps the first (higher priority)
            for (const std::string& alias : s.aliases)
                names.emplace(fold ? Upper(alias) : alias, Named{&s, set->priority});
        }
    std::stable_sort(_byLocation.begin(), _byLocation.end(), [](const Entry& a, const Entry& b) {
        if (a.space != b.space)
            return a.space < b.space;
        if (a.offset != b.offset)
            return a.offset < b.offset;
        return a.priority > b.priority;
    });
}

std::optional<uint32_t> SymbolIndex::SpaceKey(const AddressSpace& space) const
{
    const auto it = std::lower_bound(_spaces.begin(), _spaces.end(), space);
    if (it == _spaces.end() || !(*it == space))
        return std::nullopt;
    return static_cast<uint32_t>(it - _spaces.begin());
}

const Symbol* SymbolIndex::At(const Location& location) const
{
    const std::vector<const Symbol*> all = AllAt(location);
    return all.empty() ? nullptr : all.front();
}

std::vector<const Symbol*> SymbolIndex::AllAt(const Location& location) const
{
    std::vector<const Symbol*> out;
    const std::optional<uint32_t> key = SpaceKey(location.space);
    if (!key)
        return out;
    auto it = std::lower_bound(_byLocation.begin(), _byLocation.end(), std::make_pair(*key, location.offset),
                               [](const Entry& e, const std::pair<uint32_t, uint32_t>& k) { return std::make_pair(e.space, e.offset) < k; });
    for (; it != _byLocation.end() && it->space == *key && it->offset == location.offset; ++it)
        out.push_back(it->symbol);
    return out;
}

Nearest SymbolIndex::NearestBelow(const Location& location, uint32_t maxDistance) const
{
    const std::optional<uint32_t> key = SpaceKey(location.space);
    if (!key)
        return {};
    auto it = std::upper_bound(_byLocation.begin(), _byLocation.end(), std::make_pair(*key, location.offset),
                               [](const std::pair<uint32_t, uint32_t>& k, const Entry& e) { return k < std::make_pair(e.space, e.offset); });
    if (it == _byLocation.begin())
        return {};
    --it;
    if (it->space != *key || location.offset - it->offset > maxDistance)
        return {};
    // The first entry at that offset is the highest priority one
    const uint32_t offset = it->offset;
    while (it != _byLocation.begin() && (it - 1)->space == *key && (it - 1)->offset == offset)
        --it;
    return {it->symbol, location.offset - offset};
}

std::vector<const Symbol*> SymbolIndex::InRange(const Location& from, uint32_t to) const
{
    std::vector<const Symbol*> out;
    const std::optional<uint32_t> key = SpaceKey(from.space);
    if (!key)
        return out;
    auto it = std::lower_bound(_byLocation.begin(), _byLocation.end(), std::make_pair(*key, from.offset),
                               [](const Entry& e, const std::pair<uint32_t, uint32_t>& k) { return std::make_pair(e.space, e.offset) < k; });
    for (; it != _byLocation.end() && it->space == *key && it->offset <= to; ++it)
        out.push_back(it->symbol);
    return out;
}

const Symbol* SymbolIndex::Find(const std::string& name) const
{
    const auto exact = _byName.find(name);
    const auto folded = _byFoldedName.find(Upper(name));
    if (exact != _byName.end() && (folded == _byFoldedName.end() || exact->second.second >= folded->second.second))
        return exact->second.first;
    return folded != _byFoldedName.end() ? folded->second.first : nullptr;
}
}  // namespace unrealasm::symbols
