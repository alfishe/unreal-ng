#include "unrealasm/symbols/index.h"

#include <algorithm>
#include <cctype>

namespace unrealasm::symbols
{
namespace
{
uint64_t Key(uint32_t space, uint32_t offset)
{
    return static_cast<uint64_t>(space) << 32 | offset;
}
}  // namespace

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

    // The distinct spaces (a handful as a rule: collected without copying one per symbol), sorted
    const AddressSpace* last = nullptr;
    for (const SymbolSet* set : order)
        for (const Symbol& s : set->symbols)
        {
            if (!s.enabled || (last && *last == s.location.space))
                continue;
            const auto known = std::find(_spaces.begin(), _spaces.end(), s.location.space);
            if (known == _spaces.end())
                _spaces.push_back(s.location.space);
            last = &s.location.space;
        }
    std::sort(_spaces.begin(), _spaces.end());

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
    _keys.reserve(_byLocation.size());
    for (const Entry& e : _byLocation)
        _keys.push_back(Key(e.space, e.offset));
}

std::optional<uint32_t> SymbolIndex::SpaceKey(const AddressSpace& space) const
{
    // A handful of spaces as a rule: compare the cheap fields first, the names only when they agree
    if (_spaces.size() <= 16)
    {
        for (size_t k = 0; k < _spaces.size(); ++k)
        {
            const AddressSpace& s = _spaces[k];
            if (s.kind == space.kind && s.page == space.page && s.cpu == space.cpu && s.region == space.region)
                return static_cast<uint32_t>(k);
        }
        return std::nullopt;
    }
    const auto it = std::lower_bound(_spaces.begin(), _spaces.end(), space);
    if (it == _spaces.end() || !(*it == space))
        return std::nullopt;
    return static_cast<uint32_t>(it - _spaces.begin());
}

const Symbol* SymbolIndex::At(const Location& location) const
{
    const std::optional<uint32_t> key = SpaceKey(location.space);
    if (!key)
        return nullptr;
    const uint64_t want = Key(*key, location.offset);
    const auto it = std::lower_bound(_keys.begin(), _keys.end(), want);
    return it != _keys.end() && *it == want ? _byLocation[static_cast<size_t>(it - _keys.begin())].symbol : nullptr;
}

std::vector<const Symbol*> SymbolIndex::AllAt(const Location& location) const
{
    std::vector<const Symbol*> out;
    const std::optional<uint32_t> key = SpaceKey(location.space);
    if (!key)
        return out;
    const uint64_t want = Key(*key, location.offset);
    for (auto it = std::lower_bound(_keys.begin(), _keys.end(), want); it != _keys.end() && *it == want; ++it)
        out.push_back(_byLocation[static_cast<size_t>(it - _keys.begin())].symbol);
    return out;
}

Nearest SymbolIndex::NearestBelow(const Location& location, uint32_t maxDistance) const
{
    const std::optional<uint32_t> key = SpaceKey(location.space);
    if (!key)
        return {};
    auto it = std::upper_bound(_keys.begin(), _keys.end(), Key(*key, location.offset));
    if (it == _keys.begin())
        return {};
    --it;
    const Entry* e = &_byLocation[static_cast<size_t>(it - _keys.begin())];
    if (e->space != *key || location.offset - e->offset > maxDistance)
        return {};
    // The first entry at that offset is the highest priority one
    const uint64_t at = *it;
    while (it != _keys.begin() && *(it - 1) == at)
        --it;
    e = &_byLocation[static_cast<size_t>(it - _keys.begin())];
    return {e->symbol, location.offset - e->offset};
}

std::vector<const Symbol*> SymbolIndex::InRange(const Location& from, uint32_t to) const
{
    std::vector<const Symbol*> out;
    const std::optional<uint32_t> key = SpaceKey(from.space);
    if (!key)
        return out;
    const uint64_t last = Key(*key, to);
    for (auto it = std::lower_bound(_keys.begin(), _keys.end(), Key(*key, from.offset)); it != _keys.end() && *it <= last; ++it)
        out.push_back(_byLocation[static_cast<size_t>(it - _keys.begin())].symbol);
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
