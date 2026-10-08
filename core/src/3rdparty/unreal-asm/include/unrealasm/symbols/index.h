#pragma once

// The lookup index over the enabled sets (symbols/tdd.md §2.1): built once per change, read without locks. By
// location it is a sorted array (binary search, no allocation per lookup); by name a hash map. A higher-priority set
// wins where two sets name the same place or use the same name.

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "unrealasm/symbols/symbol.h"

namespace unrealasm::symbols
{
struct Nearest
{
    const Symbol* symbol = nullptr;
    uint32_t distance = 0;      ///< location - symbol (PLAYMUS+12: 12)
};

class SymbolIndex
{
public:
    /// The index over `sets` (kept alive by the index: symbols point into them)
    explicit SymbolIndex(std::shared_ptr<const std::vector<SymbolSet>> sets);

    /// The symbol at the location (the highest priority one when several)
    const Symbol* At(const Location& location) const;
    /// Every symbol at the location, highest priority first
    std::vector<const Symbol*> AllAt(const Location& location) const;
    /// The nearest symbol at or below the location in the same space, at most `maxDistance` below
    Nearest NearestBelow(const Location& location, uint32_t maxDistance) const;
    /// Symbols with from <= location <= to in from's space, by offset
    std::vector<const Symbol*> InRange(const Location& from, uint32_t to) const;
    /// By name or alias (each set's case rule applies)
    const Symbol* Find(const std::string& name) const;

    size_t Size() const { return _byLocation.size(); }
    const std::vector<SymbolSet>& Sets() const { return *_sets; }

private:
    struct Entry
    {
        uint32_t space;          ///< index into _spaces (sorted, so comparing indexes orders spaces)
        uint32_t offset;
        int priority;
        const Symbol* symbol;
    };

    std::optional<uint32_t> SpaceKey(const AddressSpace& space) const;

    std::shared_ptr<const std::vector<SymbolSet>> _sets;
    std::vector<AddressSpace> _spaces;
    std::vector<Entry> _byLocation;
    std::vector<uint64_t> _keys;   ///< space << 32 | offset of each _byLocation entry: what lookups search (dense)
    using Named = std::pair<const Symbol*, int>;   // the symbol and its set's priority
    std::unordered_map<std::string, Named> _byName;
    std::unordered_map<std::string, Named> _byFoldedName;   // names of Fold sets, ASCII upper case
};
}  // namespace unrealasm::symbols
