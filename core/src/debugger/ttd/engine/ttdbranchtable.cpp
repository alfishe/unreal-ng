#include "debugger/ttd/engine/ttdbranchtable.h"

#include <algorithm>
#include <cstring>
#include <random>

namespace ttd
{

/// region <GUID>

std::string TTDGuidToString(const TTDGuid& guid)
{
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (size_t i = 0; i < guid.size(); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            out.push_back('-');
        out.push_back(kHex[guid[i] >> 4]);
        out.push_back(kHex[guid[i] & 0x0F]);
    }
    return out;
}

bool TTDGuidFromString(std::string_view text, TTDGuid& out)
{
    TTDGuid g{};
    size_t digits = 0;
    for (char c : text)
    {
        if (c == '-')
            continue;
        int v;
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else
            return false;
        if (digits >= 32)
            return false;
        g[digits / 2] = static_cast<uint8_t>(g[digits / 2] | (digits % 2 == 0 ? v << 4 : v));
        ++digits;
    }
    if (digits != 32)
        return false;
    out = g;
    return true;
}

TTDGuid TTDNewGuid()
{
    static thread_local std::mt19937_64 random{std::random_device{}() ^ (uint64_t(std::random_device{}()) << 32)};
    TTDGuid g;
    const uint64_t a = random(), b = random();
    std::memcpy(g.data(), &a, 8);
    std::memcpy(g.data() + 8, &b, 8);
    g[6] = static_cast<uint8_t>((g[6] & 0x0F) | 0x40);   // version 4
    g[8] = static_cast<uint8_t>((g[8] & 0x3F) | 0x80);   // variant 1
    return g;
}

size_t TTDBranchTable::GuidHash::operator()(const TTDGuid& g) const
{
    uint64_t a, b;
    std::memcpy(&a, g.data(), 8);
    std::memcpy(&b, g.data() + 8, 8);
    return static_cast<size_t>(a ^ (b * 0x9E3779B97F4A7C15ull));
}

/// endregion </GUID>

/// region <Table>

uint32_t TTDBranchTable::NextCounter(const std::string& kind)
{
    const auto [it, fresh] = _counters.try_emplace(kind, 0);
    if (fresh)
    {
        const std::string prefix = kind + " ";
        for (const TTDBranchInfo& b : _branches)
        {
            const std::string& name = b.meta.name;
            if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0)
                continue;
            uint64_t n = 0;
            size_t i = prefix.size();
            for (; i < name.size() && name[i] >= '0' && name[i] <= '9' && n < 0xFFFFFFFFull; ++i)
                n = n * 10 + static_cast<uint64_t>(name[i] - '0');
            if (i > prefix.size() && n < 0xFFFFFFFFull)
                it->second = std::max(it->second, static_cast<uint32_t>(n));
        }
    }
    return ++it->second;
}

void TTDBranchTable::Reset(const TTDGuid& trunkGuid)
{
    _branches.clear();
    _byGuid = {};
    _byName = {};
    _counters.clear();
    _indexed = false;
    TTDBranchInfo trunk;
    trunk.id = kTTDTrunk;
    trunk.guid = trunkGuid;
    trunk.meta.name = "trunk";
    _branches.push_back(std::move(trunk));
    _live = 1;
}

const TTDBranchInfo* TTDBranchTable::Get(uint32_t id) const
{
    if (id >= _branches.size() || !_branches[id].live)
        return nullptr;
    return &_branches[id];
}

TTDBranchInfo* TTDBranchTable::Mutable(uint32_t id)
{
    return const_cast<TTDBranchInfo*>(Get(id));
}

const TTDBranchInfo* TTDBranchTable::FindGuid(const TTDGuid& guid) const
{
    if (!_indexed)
        return _branches.front().guid == guid ? &_branches.front() : nullptr;
    const auto it = _byGuid.find(guid);
    return it == _byGuid.end() ? nullptr : Get(it->second);
}

const TTDBranchInfo* TTDBranchTable::FindName(std::string_view name) const
{
    if (name.empty())
        return nullptr;
    if (!_indexed)
        return _branches.front().meta.name == name ? &_branches.front() : nullptr;
    const auto it = _byName.find(std::string(name));
    return it == _byName.end() ? nullptr : Get(it->second);
}

void TTDBranchTable::BuildIndex()
{
    _indexed = true;
    for (const TTDBranchInfo& b : _branches)
        if (b.live)
        {
            _byGuid[b.guid] = b.id;
            IndexName(b);
        }
}

void TTDBranchTable::IndexName(const TTDBranchInfo& b)
{
    if (_indexed && !b.meta.name.empty())
        _byName[b.meta.name] = b.id;
}

void TTDBranchTable::UnindexName(const TTDBranchInfo& b)
{
    if (!_indexed || b.meta.name.empty())
        return;
    const auto it = _byName.find(b.meta.name);
    if (it != _byName.end() && it->second == b.id)
        _byName.erase(it);
}

void TTDBranchTable::InsertChild(TTDBranchInfo& parent, uint32_t child)
{
    auto pos = std::upper_bound(parent.children.begin(), parent.children.end(), child, [&](uint32_t c, uint32_t x) {
        const TTDPosition& a = _branches[c].fork;
        const TTDPosition& b = _branches[x].fork;
        if (TTDBefore(a, b) || TTDBefore(b, a))
            return TTDBefore(a, b);
        return c < x;
    });
    parent.children.insert(pos, child);
}

std::string TTDBranchTable::UniqueName(const std::string& base) const
{
    if (base.empty() || FindName(base) == nullptr)
        return base;
    for (uint32_t n = 2;; ++n)
    {
        std::string candidate = base + " (" + std::to_string(n) + ")";
        if (FindName(candidate) == nullptr)
            return candidate;
    }
}

std::optional<uint32_t> TTDBranchTable::Create(uint32_t parentId, const TTDPosition& fork, const TTDGuid& guid,
                                               TTDBranchMeta meta, std::string& error)
{
    const TTDBranchInfo* parent = Get(parentId);
    if (parent == nullptr)
    {
        error = "branch " + std::to_string(parentId) + " does not exist";
        return std::nullopt;
    }
    if (_live >= kTTDMaxBranches || _branches.size() >= kTTDMaxBranches)
    {
        error = "the session holds " + std::to_string(_live) +
                " branches, the most it can: delete some first (branch-prune)";
        return std::nullopt;
    }
    if (parentId != kTTDTrunk && TTDBefore(fork, parent->fork))
    {
        error = "the fork lies before the branch's own history: it belongs to an ancestor";
        return std::nullopt;
    }
    if (TTDBefore(parent->end, fork))
    {
        error = "the fork lies after the branch's end";
        return std::nullopt;
    }
    if (FindGuid(guid) != nullptr)
    {
        error = "the GUID " + TTDGuidToString(guid) + " is taken";
        return std::nullopt;
    }
    if (!_indexed)
        BuildIndex();
    std::sort(meta.tags.begin(), meta.tags.end());
    meta.tags.erase(std::unique(meta.tags.begin(), meta.tags.end()), meta.tags.end());
    meta.name = UniqueName(meta.name);

    TTDBranchInfo b;
    b.id = static_cast<uint32_t>(_branches.size());
    b.guid = guid;
    b.parent = parentId;
    b.fork = fork;
    b.fork.branch = parentId;
    b.end = b.fork;
    b.end.branch = b.id;
    b.meta = std::move(meta);
    _branches.push_back(std::move(b));
    const TTDBranchInfo& added = _branches.back();
    _byGuid[added.guid] = added.id;
    IndexName(added);
    InsertChild(_branches[parentId], added.id);
    ++_live;
    return added.id;
}

uint32_t TTDBranchTable::SkipId()
{
    TTDBranchInfo b;
    b.id = static_cast<uint32_t>(_branches.size());
    b.live = false;
    _branches.push_back(std::move(b));
    return _branches.back().id;
}

bool TTDBranchTable::SetEnd(uint32_t id, const TTDPosition& end)
{
    TTDBranchInfo* b = Mutable(id);
    if (b == nullptr)
        return false;
    b->end = end;
    b->end.branch = id;
    return true;
}

bool TTDBranchTable::Rename(uint32_t id, std::string name, std::string& error)
{
    TTDBranchInfo* b = Mutable(id);
    if (b == nullptr)
    {
        error = "branch " + std::to_string(id) + " does not exist";
        return false;
    }
    if (name == b->meta.name)
        return true;
    if (const TTDBranchInfo* other = FindName(name); other != nullptr && other->id != id)
    {
        error = "a branch is already named '" + name + "'";
        return false;
    }
    if (!_indexed)
        BuildIndex();
    UnindexName(*b);
    b->meta.name = std::move(name);
    IndexName(*b);
    return true;
}

bool TTDBranchTable::SetNote(uint32_t id, std::string note)
{
    TTDBranchInfo* b = Mutable(id);
    if (b == nullptr)
        return false;
    b->meta.note = std::move(note);
    return true;
}

bool TTDBranchTable::SetPinned(uint32_t id, bool pinned)
{
    TTDBranchInfo* b = Mutable(id);
    if (b == nullptr)
        return false;
    b->meta.pinned = pinned;
    return true;
}

bool TTDBranchTable::UpdateTags(uint32_t id, const std::vector<std::string>* set, const std::vector<std::string>& add,
                                const std::vector<std::string>& remove)
{
    TTDBranchInfo* b = Mutable(id);
    if (b == nullptr)
        return false;
    std::vector<std::string>& tags = b->meta.tags;
    if (set != nullptr)
        tags = *set;
    tags.insert(tags.end(), add.begin(), add.end());
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    for (const std::string& r : remove)
    {
        auto it = std::lower_bound(tags.begin(), tags.end(), r);
        if (it != tags.end() && *it == r)
            tags.erase(it);
    }
    return true;
}

std::vector<uint32_t> TTDBranchTable::Subtree(uint32_t id) const
{
    std::vector<uint32_t> out;
    if (Get(id) == nullptr)
        return out;
    // Depth first, children before their parent
    std::vector<std::pair<uint32_t, size_t>> stack{{id, 0}};
    while (!stack.empty())
    {
        auto& [b, next] = stack.back();
        const std::vector<uint32_t>& children = _branches[b].children;
        if (next < children.size())
        {
            const uint32_t c = children[next++];
            stack.push_back({c, 0});
            continue;
        }
        out.push_back(b);
        stack.pop_back();
    }
    return out;
}

bool TTDBranchTable::Delete(uint32_t id, const std::vector<uint32_t>& protect, bool force,
                            std::vector<uint32_t>& removed, std::string& error)
{
    removed.clear();
    if (id == kTTDTrunk)
    {
        error = "the trunk is never deleted";
        return false;
    }
    if (Get(id) == nullptr)
    {
        error = "branch " + std::to_string(id) + " does not exist";
        return false;
    }
    const std::vector<uint32_t> subtree = Subtree(id);
    std::string pinned;
    for (uint32_t b : subtree)
    {
        if (std::find(protect.begin(), protect.end(), b) != protect.end())
        {
            error = "branch '" + _branches[b].meta.name + "' is on the active lineage: switch to another branch first";
            return false;
        }
        if (_branches[b].meta.pinned)
            pinned += (pinned.empty() ? "'" : ", '") + _branches[b].meta.name + "'";
    }
    if (!pinned.empty() && !force)
    {
        error = "pinned: " + pinned + " (force deletes them too)";
        return false;
    }
    TTDBranchInfo& parent = _branches[_branches[id].parent];
    parent.children.erase(std::remove(parent.children.begin(), parent.children.end(), id), parent.children.end());
    for (uint32_t b : subtree)
    {
        TTDBranchInfo& info = _branches[b];
        UnindexName(info);
        _byGuid.erase(info.guid);
        info.live = false;
        info.children.clear();
        --_live;
    }
    removed = subtree;
    return true;
}

std::vector<TTDLineageSpan> TTDBranchTable::Lineage(uint32_t id) const
{
    std::vector<TTDLineageSpan> spans;
    const TTDBranchInfo* b = Get(id);
    if (b == nullptr)
        return spans;
    TTDPosition to = b->end;
    for (;;)
    {
        TTDLineageSpan s;
        s.branch = b->id;
        s.from = b->id == kTTDTrunk ? TTDPosition{} : b->fork;
        s.from.branch = b->id;
        s.to = to;
        s.to.branch = b->id;
        spans.push_back(s);
        if (b->id == kTTDTrunk)
            break;
        to = b->fork;
        b = &_branches[b->parent];
    }
    std::reverse(spans.begin(), spans.end());
    return spans;
}

bool TTDBranchTable::IsInLineage(uint32_t ancestor, uint32_t id) const
{
    for (const TTDBranchInfo* b = Get(id); b != nullptr; b = b->parent == kTTDNoBranch ? nullptr : &_branches[b->parent])
        if (b->id == ancestor)
            return true;
    return false;
}

uint32_t TTDBranchTable::OwnerOf(const std::vector<TTDLineageSpan>& lineage, const TTDPosition& at)
{
    if (lineage.empty())
        return kTTDNoBranch;
    // The last span whose start is at or before `at`
    auto it = std::upper_bound(lineage.begin(), lineage.end(), at,
                               [](const TTDPosition& p, const TTDLineageSpan& s) { return TTDBefore(p, s.from); });
    if (it == lineage.begin())
        return lineage.front().branch;
    return std::prev(it)->branch;
}

void TTDBranchTable::ChildrenIn(uint32_t id, uint64_t fromFrame, uint64_t toFrame, std::vector<uint32_t>& out) const
{
    out.clear();
    const TTDBranchInfo* b = Get(id);
    if (b == nullptr)
        return;
    const std::vector<uint32_t>& c = b->children;
    auto first = std::lower_bound(c.begin(), c.end(), fromFrame,
                                  [&](uint32_t x, uint64_t f) { return _branches[x].fork.frame < f; });
    for (auto it = first; it != c.end() && _branches[*it].fork.frame <= toFrame; ++it)
        out.push_back(*it);
}

std::vector<uint32_t> TTDBranchTable::Select(const TTDBranchFilter& f) const
{
    std::vector<uint32_t> out;
    for (const TTDBranchInfo& b : _branches)
    {
        if (!b.live || (!f.includeTrunk && b.id == kTTDTrunk))
            continue;
        const TTDBranchMeta& m = b.meta;
        if (!f.createdBy.empty() && m.createdBy.compare(0, f.createdBy.size(), f.createdBy) != 0)
            continue;
        if (!f.run.empty() && m.run != f.run)
            continue;
        if (!f.namePrefix.empty() && m.name.compare(0, f.namePrefix.size(), f.namePrefix) != 0)
            continue;
        if (f.pinned && m.pinned != *f.pinned)
            continue;
        if ((f.minLength && b.Length() < *f.minLength) || (f.maxLength && b.Length() > *f.maxLength))
            continue;
        if ((f.createdAfter && m.createdHostMicros < *f.createdAfter) ||
            (f.createdBefore && m.createdHostMicros >= *f.createdBefore))
            continue;
        bool tags = true;
        for (const std::string& t : f.tags)
            tags = tags && std::binary_search(m.tags.begin(), m.tags.end(), t);
        if (!tags)
            continue;
        out.push_back(b.id);
    }
    return out;
}

/// endregion </Table>

}  // namespace ttd
