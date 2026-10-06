#pragma once

// The symbol store (symbols/tdd.md §2, §5): the sets of one CPU, imports with normalization and the merge policy
// (architecture.md DT-1, DT-2), and the published index. Changes are serialized by a mutex and build a new immutable
// index; readers take the current one and keep it as long as they need.

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "unrealasm/diagnostics.h"
#include "unrealasm/symbols/index.h"
#include "unrealasm/symbols/symbol.h"

namespace unrealasm::symbols
{
enum class MergePolicy : uint8_t
{
    Both,       ///< a second name for a place becomes an alias; a name that moves keeps its old place (default, P-4)
    Keep,       ///< what the set has wins
    Replace,    ///< the new record wins
    Fail,       ///< stop at the first conflict, nothing changes
};

std::string_view PolicyName(MergePolicy policy);
bool ParsePolicy(std::string_view text, MergePolicy& out);

struct ImportOptions
{
    std::string set;                            ///< the target set id (made when missing)
    std::string title;                          ///< for a new set
    Origin origin;                              ///< for a new set
    std::optional<AddressSpace> space;          ///< records without a space of their own go here (--page ram3)
    uint32_t base = 0;                          ///< added to every offset
    MergePolicy policy = MergePolicy::Both;
};

struct Conflict
{
    enum class Type : uint8_t
    {
        SamePlace,      ///< another name already at the place
        Moved,          ///< the name is already at another place
    };
    Type type = Type::SamePlace;
    std::string name;
    std::string other;          ///< the name already there (SamePlace)
    Location oldLocation;
    Location newLocation;
    uint32_t line = 0;          ///< of the imported record
    std::string resolution;     ///< "alias", "kept", "replaced", "moved", "failed"
};

struct ImportReport
{
    std::string set;
    size_t added = 0;
    size_t aliased = 0;
    size_t updated = 0;         ///< same name and place: empty fields filled
    size_t skipped = 0;
    std::vector<Conflict> conflicts;
    Diagnostics diagnostics;
    bool ok = false;
};

/// Records -> normalized records (space by DT-1, base, range and name checks, duplicates inside the file); what is
/// dropped is reported
std::vector<Symbol> Normalize(std::vector<Symbol> records, const ImportOptions& options, Diagnostics& diagnostics);
/// Merges normalized records into a set by the policy (DT-2); false (set unchanged) when the policy is Fail and a
/// conflict was found
bool Merge(SymbolSet& set, const std::vector<Symbol>& records, MergePolicy policy, ImportReport& report);

class SymbolStore
{
public:
    SymbolStore();

    /// Normalizes and merges records into options.set (made when missing); publishes a new index
    ImportReport Import(std::vector<Symbol> records, const ImportOptions& options);
    /// Adds (or replaces) whole sets as they are (the native file)
    void PutSets(std::vector<SymbolSet> sets);
    bool Drop(const std::string& id);
    bool SetEnabled(const std::string& id, bool enabled);
    bool SetPriority(const std::string& id, int priority);
    void Clear();

    std::vector<SymbolSet> Sets() const;
    std::optional<SymbolSet> GetSet(const std::string& id) const;
    /// The current index (immutable; valid as long as it is held)
    std::shared_ptr<const SymbolIndex> Index() const;

private:
    void Publish(std::vector<SymbolSet> sets);   // under _change

    mutable std::mutex _change;
    std::shared_ptr<const std::vector<SymbolSet>> _sets;
    mutable std::mutex _publish;                 // guards the pointer copy only
    std::shared_ptr<const SymbolIndex> _index;
};
}  // namespace unrealasm::symbols
