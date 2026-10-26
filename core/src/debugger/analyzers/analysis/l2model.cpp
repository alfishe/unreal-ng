#include "debugger/analyzers/analysis/l2model.h"

#include <algorithm>
#include <cstdio>
#include <tuple>

namespace analysis
{

namespace
{

struct KindName
{
    RegionKind kind;
    const char* name;
};

constexpr KindName kKindNames[] = {
    {RegionKind::Code, "code"},
    {RegionKind::Data, "data"},
    {RegionKind::Text, "text"},
    {RegionKind::Font, "font"},
    {RegionKind::Sprite, "sprite"},
    {RegionKind::Tile, "tile"},
    {RegionKind::Screen, "screen"},
    {RegionKind::Buffer, "buffer"},
    {RegionKind::InitTable, "init_table"},
    {RegionKind::RecordArray, "records"},
    {RegionKind::Variable, "variable"},
    {RegionKind::PointerTable, "pointer_table"},
    {RegionKind::HandlerTable, "handler_table"},
    {RegionKind::RngState, "rng"},
    {RegionKind::MusicData, "music"},
    {RegionKind::Packed, "packed"},
    {RegionKind::LevelMap, "level_map"},
    {RegionKind::Unused, "unused"},
    {RegionKind::Untouched, "untouched"},
};

std::string Hex4(uint16_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value);
    return text;
}

/// Kinds that describe what a byte is (one per byte); the others (Buffer, RecordArray, Variable's group) describe
/// structure and may hold any of them
bool Exclusive(RegionKind kind)
{
    return kind == RegionKind::Code || kind == RegionKind::Text || kind == RegionKind::Unused;
}

}  // namespace

const char* RegionKindName(RegionKind kind)
{
    for (const KindName& k : kKindNames)
        if (k.kind == kind)
            return k.name;
    return "?";
}

bool ParseRegionKind(const std::string& text, RegionKind& kind)
{
    for (const KindName& k : kKindNames)
        if (text == k.name)
        {
            kind = k.kind;
            return true;
        }
    return false;
}

const char* DetectorName(DetectorId id)
{
    switch (id)
    {
        case DetectorId::Signatures:
            return "signatures";
        case DetectorId::CodeMap:
            return "code_map";
        case DetectorId::Text:
            return "text";
        case DetectorId::InitTables:
            return "init_tables";
        case DetectorId::Records:
            return "records";
        case DetectorId::PointerTables:
            return "pointer_tables";
        case DetectorId::Variables:
            return "variables";
        case DetectorId::Graphics:
            return "graphics";
        case DetectorId::LevelMaps:
            return "level_maps";
        case DetectorId::Screens:
            return "screens";
        case DetectorId::Rng:
            return "rng";
        case DetectorId::Unpackers:
            return "unpackers";
        case DetectorId::Music:
            return "music";
    }
    return "?";
}

const char* RegionStatusName(RegionStatus status)
{
    return status == RegionStatus::Observed ? "observed" : "inferred";
}

const char* VarRoleName(VarRole role)
{
    switch (role)
    {
        case VarRole::None:
            return "";
        case VarRole::Counter:
            return "counter";
        case VarRole::Flag:
            return "flag";
        case VarRole::Pointer:
            return "pointer";
        case VarRole::Word:
            return "word";
        case VarRole::Value:
            return "value";
    }
    return "";
}

namespace confidence
{
uint16_t Clamp(int64_t value)
{
    return static_cast<uint16_t>(std::clamp<int64_t>(value, 0, kMax));
}

uint16_t Combine(uint16_t a, uint16_t b)
{
    const int64_t x = std::min<uint16_t>(a, kMax);
    const int64_t y = std::min<uint16_t>(b, kMax);
    return Clamp(kMax - (kMax - x) * (kMax - y) / kMax);
}

uint16_t Scale(uint16_t value, uint32_t num, uint32_t den)
{
    if (den == 0)
        return 0;
    return Clamp(static_cast<int64_t>(value) * num / den);
}

uint16_t FromCount(uint16_t base, uint16_t max, uint64_t count, uint64_t half)
{
    if (max <= base)
        return Clamp(base);
    const uint64_t c = std::min<uint64_t>(count, 1u << 30);
    return Clamp(base + static_cast<int64_t>((max - base) * c / (c + std::max<uint64_t>(half, 1))));
}
}  // namespace confidence

bool RegionStore::Add(Region region)
{
    if (region.evidence.empty() || region.hi < region.lo || KeyPage(region.lo) != KeyPage(region.hi))
        return false;
    region.confidence = std::min<uint16_t>(region.confidence, confidence::kMax);
    region.order = _next++;
    std::sort(region.users.begin(), region.users.end());
    region.users.erase(std::unique(region.users.begin(), region.users.end()), region.users.end());
    if (region.users.size() > 16)
        region.users.resize(16);
    std::sort(region.geometry.instances.begin(), region.geometry.instances.end());
    _regions.push_back(std::move(region));
    _sorted = false;
    return true;
}

void RegionStore::Finish()
{
    std::sort(_regions.begin(), _regions.end(), [](const Region& a, const Region& b) {
        return std::make_tuple(a.lo, static_cast<int>(a.detector), a.order) <
               std::make_tuple(b.lo, static_cast<int>(b.detector), b.order);
    });
    _sorted = true;
}

std::vector<const Region*> RegionStore::Overlapping(PhysKey lo, PhysKey hi) const
{
    std::vector<const Region*> out;
    for (const Region& r : _regions)
        if (r.lo <= hi && r.hi >= lo)
            out.push_back(&r);
    if (!_sorted)
        std::sort(out.begin(), out.end(), [](const Region* a, const Region* b) {
            return std::make_tuple(a->lo, static_cast<int>(a->detector), a->order) <
                   std::make_tuple(b->lo, static_cast<int>(b->detector), b->order);
        });
    return out;
}

std::vector<const Region*> RegionStore::ClaimsAt(PhysKey key) const
{
    return Overlapping(key, key);
}

bool RegionStore::Claimed(PhysKey key) const
{
    for (const Region& r : _regions)
        if (r.kind != RegionKind::Untouched && r.lo <= key && key <= r.hi)
            return true;
    return false;
}

bool RegionStore::ClaimedAs(PhysKey key, RegionKind kind) const
{
    for (const Region& r : _regions)
        if (r.kind == kind && r.lo <= key && key <= r.hi)
            return true;
    return false;
}

std::vector<RegionStore::Conflict> RegionStore::Conflicts() const
{
    std::vector<Conflict> out;
    for (uint32_t i = 0; i < _regions.size(); ++i)
        for (uint32_t j = i + 1; j < _regions.size(); ++j)
        {
            const Region& a = _regions[i];
            const Region& b = _regions[j];
            if (b.lo > a.hi && _sorted)
                break;
            if (a.lo > b.hi || b.lo > a.hi)
                continue;
            const bool clash = (Exclusive(a.kind) || Exclusive(b.kind)) ? a.kind != b.kind
                               : (a.kind == RegionKind::RecordArray && b.kind == RegionKind::RecordArray)
                                   ? a.geometry.stride != b.geometry.stride
                                   : false;
            if (clash)
                out.push_back(Conflict{i, j});
        }
    return out;
}

const CodeLine* L2Result::CodeAt(PhysKey key) const
{
    const auto it =
        std::lower_bound(code.begin(), code.end(), key, [](const CodeLine& c, PhysKey k) { return c.key < k; });
    return it != code.end() && it->key == key ? &*it : nullptr;
}

StateNode RegionNode(const Region& region)
{
    StateNode node = StateNode::Object();
    node["start"] = Hex4(region.z80);
    node["end"] = Hex4(region.Z80End());
    node["page"] = static_cast<uint64_t>(KeyPage(region.lo));
    node["size"] = static_cast<uint64_t>(region.Size());
    node["kind"] = RegionKindName(region.kind);
    node["detector"] = DetectorName(region.detector);
    node["status"] = RegionStatusName(region.status);
    node["confidence"] = static_cast<uint64_t>(region.confidence);
    if (!region.label.empty())
        node["label"] = region.label;
    if (!region.title.empty())
        node["title"] = region.title;
    const Geometry& g = region.geometry;
    StateNode geometry = StateNode::Object();
    if (g.width != 0)
        geometry["width"] = static_cast<uint64_t>(g.width);
    if (g.height != 0)
        geometry["height"] = static_cast<uint64_t>(g.height);
    if (g.frames != 0)
        geometry["frames"] = static_cast<uint64_t>(g.frames);
    if (g.stride != 0)
        geometry["stride"] = static_cast<uint64_t>(g.stride);
    if (g.count != 0)
        geometry["count"] = static_cast<uint64_t>(g.count);
    if (g.mask)
        geometry["mask"] = true;
    if (g.prefix != 0)
        geometry["prefix"] = static_cast<uint64_t>(g.prefix);
    if (!g.layout.empty())
        geometry["layout"] = g.layout;
    if (!g.terminator.empty())
        geometry["terminator"] = g.terminator;
    if (!g.generator.empty())
        geometry["generator"] = g.generator;
    if (g.role != VarRole::None)
    {
        geometry["role"] = VarRoleName(g.role);
        geometry["min"] = static_cast<uint64_t>(g.minValue);
        geometry["max"] = static_cast<uint64_t>(g.maxValue);
    }
    if (!g.text.empty())
        geometry["text"] = g.text.size() > 160 ? g.text.substr(0, 160) + "..." : g.text;
    if (!g.fields.empty())
    {
        StateNode fields = StateNode::Array();
        for (const Field& f : g.fields)
        {
            StateNode field = StateNode::Object();
            field["offset"] = static_cast<uint64_t>(f.offset);
            field["size"] = static_cast<uint64_t>(f.size);
            if (!f.name.empty())
                field["name"] = f.name;
            field["min"] = static_cast<uint64_t>(f.minValue);
            field["max"] = static_cast<uint64_t>(f.maxValue);
            fields.push(std::move(field));
        }
        geometry["fields"] = std::move(fields);
    }
    if (!g.instances.empty())
    {
        StateNode instances = StateNode::Array();
        for (const uint16_t i : g.instances)
            instances.push(Hex4(i));
        geometry["instances"] = std::move(instances);
    }
    node["geometry"] = std::move(geometry);
    StateNode users = StateNode::Array();
    for (const uint16_t u : region.users)
        users.push(Hex4(u));
    node["users"] = std::move(users);
    StateNode evidence = StateNode::Array();
    for (const Evidence& e : region.evidence)
    {
        StateNode item = StateNode::Object();
        item["what"] = e.what;
        if (e.z80 != 0)
            item["at"] = Hex4(e.z80);
        if (e.count != 0)
            item["count"] = e.count;
        if (!e.detail.empty())
            item["detail"] = e.detail;
        evidence.push(std::move(item));
    }
    node["evidence"] = std::move(evidence);
    return node;
}

StateNode L2Result::ToStateNode() const
{
    StateNode node = StateNode::Object();
    StateNode regions = StateNode::Array();
    for (const Region& r : store.Regions())
        regions.push(RegionNode(r));
    node["regions"] = std::move(regions);
    StateNode conflicts = StateNode::Array();
    for (const RegionStore::Conflict& c : store.Conflicts())
    {
        StateNode item = StateNode::Object();
        const Region& a = store.Regions()[c.a];
        const Region& b = store.Regions()[c.b];
        item["a"] = Hex4(a.z80) + " " + RegionKindName(a.kind) + " (" + DetectorName(a.detector) + ")";
        item["b"] = Hex4(b.z80) + " " + RegionKindName(b.kind) + " (" + DetectorName(b.detector) + ")";
        conflicts.push(std::move(item));
    }
    node["conflicts"] = std::move(conflicts);
    uint64_t executed = 0;
    for (const CodeLine& c : code)
        executed += c.executed ? 1 : 0;
    node["instructions"] = static_cast<uint64_t>(code.size());
    node["instructions_executed"] = executed;
    node["entries"] = static_cast<uint64_t>(entries.size());
    StateNode notesNode = StateNode::Array();
    for (const std::string& n : notes)
        notesNode.push(n);
    node["notes"] = std::move(notesNode);
    return node;
}

}  // namespace analysis
