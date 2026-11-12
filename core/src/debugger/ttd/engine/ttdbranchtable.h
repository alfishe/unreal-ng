#pragma once

/// @file ttdbranchtable.h
/// @brief The branches of one recording session: identities, metadata, the
/// tree and the lineage queries (docs/inprogress/2026-11-08-ttd-branches/
/// architecture.md §2-3).
///
/// A branch is a line of history that leaves its parent at a fork position
/// and owns everything from there to its end; what lies before the fork it
/// shares with the parent. The trunk (id 0) has no parent and is never
/// deleted. Ids are never reused inside a session; a file numbers them anew.
///
/// Worked example: the trunk records frames 0-300; a resume at {0, 120, 500}
/// starts branch 1, which records to frame 180; a resume on branch 1 at
/// {1, 150, 0} starts branch 2. Lineage(2) = trunk [0, {120, 500}), branch 1
/// [{120, 500}, {150, 0}), branch 2 [{150, 0}, its end]. OwnerOf(lineage,
/// {130, 0}) = 1; OwnerOf(lineage, {100, 0}) = 0.
///
/// A session that never forks holds the trunk alone: no index is built and
/// nothing here runs on the capture path.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "debugger/ttd/engine/ttdtime.h"

namespace ttd
{

/// A branch's identity everywhere: files, folders, automation, tools
using TTDGuid = std::array<uint8_t, 16>;

std::string TTDGuidToString(const TTDGuid& guid);
/// 32 hex digits, dashes allowed anywhere
bool TTDGuidFromString(std::string_view text, TTDGuid& out);
/// 16 random bytes (version 4 layout)
TTDGuid TTDNewGuid();

constexpr uint32_t kTTDTrunk = 0;
constexpr uint32_t kTTDNoBranch = 0xFFFFFFFFu;
/// Ids fit the file's u16 branch field; 0xFFFF stays free
constexpr uint32_t kTTDMaxBranches = 0xFFFF;

/// {frame, T} order; the branch field is not compared
inline bool TTDBefore(const TTDPosition& a, const TTDPosition& b)
{
    return a.frame != b.frame ? a.frame < b.frame : a.tInFrame < b.tInFrame;
}

struct TTDBranchMeta
{
    std::string name;                ///< unique among the live branches; may be empty
    uint64_t createdHostMicros = 0;  ///< host time
    std::string createdBy;           ///< qt, deck, controller:<id>, gdb, dezog, mcp:<client>, webapi:<client>, cli, lua, python, analyzer
    std::string run;                 ///< the analyzer's or a script's run id
    std::string reason;              ///< resume, edit-memory, edit-register, edit-port, create, with details
    std::vector<std::string> tags;   ///< sorted, unique
    std::string note;
    bool pinned = false;
};

/// An edit of a branch's metadata: only the fields set change
struct TTDBranchUpdate
{
    std::optional<std::string> name;
    std::optional<std::string> note;
    std::optional<std::vector<std::string>> tags;   ///< replaces them
    std::vector<std::string> addTags;
    std::vector<std::string> removeTags;
    std::optional<bool> pinned;

    bool Empty() const
    {
        return !name && !note && !tags && addTags.empty() && removeTags.empty() && !pinned;
    }
};

struct TTDBranchInfo
{
    uint32_t id = kTTDTrunk;
    TTDGuid guid{};
    uint32_t parent = kTTDNoBranch;
    TTDPosition fork;                ///< on the parent (fork.branch == parent); {0, 0, 0} for the trunk
    TTDPosition end;                 ///< the last recorded position
    bool live = true;
    TTDBranchMeta meta;
    std::vector<uint32_t> children;  ///< live children, by fork position then id

    /// Frames from the fork to the end
    uint64_t Length() const { return end.frame >= fork.frame ? end.frame - fork.frame : 0; }
};

/// One branch's part of a lineage: positions in [from, to) are its own; the
/// last span is open (to = its end, inclusive)
struct TTDLineageSpan
{
    uint32_t branch = kTTDTrunk;
    TTDPosition from;
    TTDPosition to;
};

struct TTDBranchFilter
{
    std::string createdBy;              ///< prefix ("mcp" matches mcp:claude)
    std::string run;
    std::vector<std::string> tags;      ///< every one of them
    std::string namePrefix;
    std::optional<uint64_t> minLength, maxLength;   ///< frames
    std::optional<uint64_t> createdAfter, createdBefore;   ///< host micros
    std::optional<bool> pinned;
    bool includeTrunk = true;
};

class TTDBranchTable
{
public:
    TTDBranchTable() { Reset(TTDGuid{}); }

    /// The trunk alone, with @p trunkGuid
    void Reset(const TTDGuid& trunkGuid);

    /// Live branches, the trunk included
    size_t Count() const { return _live; }
    bool HasBranches() const { return _live > 1; }
    /// Every id ever given (deleted ones too): ids run 0..Ids()-1
    size_t Ids() const { return _branches.size(); }
    bool Indexed() const { return _indexed; }
    /// Changes with every change of the table (a branch added, edited, ended, deleted): readers that keep a copy
    /// rebuild it when this moved
    uint64_t Revision() const { return _revision; }

    /// A live branch; nullptr for a deleted or unknown id
    const TTDBranchInfo* Get(uint32_t id) const;
    const TTDBranchInfo* FindGuid(const TTDGuid& guid) const;
    const TTDBranchInfo* FindName(std::string_view name) const;
    const TTDBranchInfo& Trunk() const { return _branches.front(); }

    /// A branch leaving @p parent at @p fork. The fork lies in the parent's
    /// own part of history ([its fork, its end]); a position before that
    /// belongs to an ancestor (OwnerOf). A name already taken gets " (2)" ...
    std::optional<uint32_t> Create(uint32_t parent, const TTDPosition& fork, const TTDGuid& guid, TTDBranchMeta meta,
                                   std::string& error);
    /// Take the next id for a branch that is not there (a load left it out): the ids after it keep their numbers
    uint32_t SkipId();
    /// The branch recorded up to @p end
    bool SetEnd(uint32_t id, const TTDPosition& end);

    bool Rename(uint32_t id, std::string name, std::string& error);
    bool SetNote(uint32_t id, std::string note);
    bool SetPinned(uint32_t id, bool pinned);
    /// Tags set, added or removed (each list may be empty)
    bool UpdateTags(uint32_t id, const std::vector<std::string>* set, const std::vector<std::string>& add,
                    const std::vector<std::string>& remove);

    /// @p id and its descendants. Refused for the trunk, for a branch in
    /// @p protect (the active lineage) or with one there among its
    /// descendants, and with a pinned branch among them unless @p force.
    /// @p removed: the ids deleted, children first
    bool Delete(uint32_t id, const std::vector<uint32_t>& protect, bool force, std::vector<uint32_t>& removed,
                std::string& error);
    /// What Delete(@p id) would remove (no checks)
    std::vector<uint32_t> Subtree(uint32_t id) const;

    /// The spans from the trunk to @p id
    std::vector<TTDLineageSpan> Lineage(uint32_t id) const;
    /// Whether @p ancestor is @p id or one of its ancestors
    bool IsInLineage(uint32_t ancestor, uint32_t id) const;
    /// The branch of @p lineage that owns @p at (the last span starting at or before it)
    static uint32_t OwnerOf(const std::vector<TTDLineageSpan>& lineage, const TTDPosition& at);

    /// Live children of @p id that fork in frames [@p fromFrame, @p toFrame]
    void ChildrenIn(uint32_t id, uint64_t fromFrame, uint64_t toFrame, std::vector<uint32_t>& out) const;
    /// Live branches the filter takes, by id
    std::vector<uint32_t> Select(const TTDBranchFilter& filter) const;

    /// @p base when no live branch has that name, else "base (2)", "base (3)" ...
    std::string UniqueName(const std::string& base) const;
    /// 1, 2, 3 ... per @p kind ("resume", "edit", ...): default names. The first call per kind goes on from the
    /// names the table has ("resume 3" -> 4: a loaded session keeps numbering on)
    uint32_t NextCounter(const std::string& kind);

private:
    struct GuidHash
    {
        size_t operator()(const TTDGuid& g) const;
    };

    TTDBranchInfo* Mutable(uint32_t id);
    void BuildIndex();
    void IndexName(const TTDBranchInfo& b);
    void UnindexName(const TTDBranchInfo& b);
    void InsertChild(TTDBranchInfo& parent, uint32_t child);

    std::vector<TTDBranchInfo> _branches;
    size_t _live = 0;
    bool _indexed = false;   ///< from the second branch on
    std::unordered_map<TTDGuid, uint32_t, GuidHash> _byGuid;
    std::unordered_map<std::string, uint32_t> _byName;
    std::unordered_map<std::string, uint32_t> _counters;
    uint64_t _revision = 0;
};

}  // namespace ttd
