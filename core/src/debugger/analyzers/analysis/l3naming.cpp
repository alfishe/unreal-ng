#include "debugger/analyzers/analysis/l3naming.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <set>

#include "debugger/analyzers/analysis/l3findings.h"
#include "debugger/analyzers/analysis/z80opinfo.h"
#include "emulator/state/statenodejson.h"

namespace analysis
{

namespace
{

std::string Hex4(uint32_t value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "$%04X", value & 0xFFFF);
    return text;
}

std::string Hex2(uint32_t value)
{
    char text[4];
    std::snprintf(text, sizeof(text), "%02X", value & 0xFF);
    return text;
}

bool Graphic(RegionKind kind)
{
    return kind == RegionKind::Sprite || kind == RegionKind::Tile || kind == RegionKind::Font;
}

const DraftEntry* EntryOver(const DraftPlan& plan, uint16_t z80)
{
    const auto it = std::upper_bound(plan.entries.begin(), plan.entries.end(), z80,
                                     [](uint16_t a, const DraftEntry& e) { return a < e.start; });
    if (it == plan.entries.begin())
        return nullptr;
    const DraftEntry& e = *(it - 1);
    return z80 <= e.end ? &e : nullptr;
}

std::string LabelAt(const DraftPlan& plan, uint16_t z80)
{
    const auto it = plan.labels.find(z80);
    return it != plan.labels.end() ? it->second : Hex4(z80);
}

/// Up to @p max addresses, " +N" for the rest
std::string List(const std::vector<std::string>& items, size_t max)
{
    std::string out;
    for (size_t i = 0; i < items.size() && i < max; ++i)
        out += (i ? " " : "") + items[i];
    if (items.size() > max)
        out += " +" + std::to_string(items.size() - max);
    return out;
}

/// The facts of one subject: one or two lines
std::string SubjectBlock(const NamingSubject& s, const L1Result& l1, const L2Result& l2, const DraftPlan& plan,
                         const std::map<uint16_t, std::vector<std::string>>& accesses,
                         const std::function<std::string(const uint8_t* bytes, uint16_t z80)>& disassemble)
{
    const DraftEntry* e = EntryOver(plan, s.z80);
    std::string block = Hex4(s.z80) + " " + s.label;
    if (s.entry && e != nullptr)
        block += ": " + e->title;
    else if (e != nullptr)
        block += " (inside " + e->label + ")";
    std::vector<std::string> refs;
    if (const auto it = plan.refs.find(s.z80); it != plan.refs.end())
        for (const uint16_t r : it->second)
            refs.push_back(Hex4(r));
    if (s.code)
    {
        std::vector<std::string> callers, callees;
        const Routine* routine = l1.RoutineAtZ80(s.z80);
        if (routine != nullptr && routine->z80 != s.z80)
            routine = nullptr;
        if (routine != nullptr)
            for (const CallEdge& c : l1.calls)
            {
                if (c.kind == CallKind::Interrupt || c.kind == CallKind::FallsInto)
                    continue;
                if (l1.routines[c.callee].z80 == s.z80)
                    for (const uint16_t site : c.sites)
                        callers.push_back(Hex4(site));
                if (l1.routines[c.caller].z80 == s.z80)
                {
                    const std::string name = LabelAt(plan, l1.routines[c.callee].z80);
                    if (std::find(callees.begin(), callees.end(), name) == callees.end())
                        callees.push_back(name);
                }
            }
        for (const std::string& r : refs)
            if (std::find(callers.begin(), callers.end(), r) == callers.end())
                callers.push_back(r);
        if (!callers.empty())
            block += "; from " + List(callers, 6);
        if (!callees.empty())
            block += "; calls " + List(callees, 5);
        if (const auto it = accesses.find(s.z80); it != accesses.end())
            block += "; data " + List(it->second, 5);
        if (routine != nullptr)
            block += "; entered " + std::to_string(routine->profile.entries) + "x";
        block += "\n ";
        // The head: four instructions from the start image
        uint16_t pc = s.z80;
        for (int line = 0; line < 4; ++line)
        {
            uint8_t bytes[4] = {};
            for (int i = 0; i < 4; ++i)
                l1.StartByte(l1.StartKey(static_cast<uint16_t>(pc + i)), bytes[i]);
            const OpInfo& op = DecodeOp(bytes);
            std::string text = disassemble ? disassemble(bytes, pc) : std::string();
            if (text.empty())
                text = Hex2(bytes[0]);
            block += (line ? " | " : " ") + text;
            if (op.flow == Flow::Ret && op.cond == kAlways)
                break;
            pc = static_cast<uint16_t>(pc + op.length);
            const DraftEntry* next = EntryOver(plan, pc);
            if (next == nullptr || next->start == pc)
                break;
        }
        return block + "\n";
    }
    // Data: the bytes, the accesses, who used them
    const uint16_t end = s.entry && e != nullptr ? e->end : s.z80;
    const uint32_t span = std::min<uint32_t>(static_cast<uint32_t>(end - s.z80) + 1u, 256u);
    uint64_t reads = 0, writes = 0;
    uint8_t lo = 0xFF, hi = 0;
    bool written = false;
    for (uint32_t i = 0; i < span; ++i)
        if (const ByteUse* u = l1.UseAt(l1.StartKey(static_cast<uint16_t>(s.z80 + i))))
        {
            reads += u->reads;
            writes += u->writes;
            if (u->writes != 0)
            {
                written = true;
                lo = std::min(lo, u->minWritten);
                hi = std::max(hi, u->maxWritten);
            }
        }
    block += "; " + std::to_string(reads) + " reads, " + std::to_string(writes) + " writes";
    if (written)
        block += " (values " + Hex2(lo) + "-" + Hex2(hi) + ")";
    std::vector<std::string> users;
    if (e != nullptr && e->region != nullptr)
        for (const uint16_t u : e->region->users)
            users.push_back(LabelAt(plan, u));
    if (!users.empty())
        block += "; used by " + List(users, 4);
    if (!refs.empty())
        block += "; refs " + List(refs, 4);
    std::string bytes;
    for (uint32_t i = 0; i < std::min<uint32_t>(span, 8); ++i)
    {
        uint8_t v = 0;
        l1.StartByte(l1.StartKey(static_cast<uint16_t>(s.z80 + i)), v);
        bytes += (i ? " " : "") + Hex2(v);
    }
    (void)l2;
    return block + "\n  bytes " + bytes + "\n";
}

std::map<uint16_t, std::vector<std::string>> Accesses(const DraftPlan& plan)
{
    std::map<uint16_t, std::vector<std::string>> out;
    for (const DraftEntry& e : plan.entries)
    {
        if (e.region == nullptr || e.ctl == 'c')
            continue;
        for (const uint16_t u : e.region->users)
            out[u].push_back(e.label);
    }
    return out;
}

std::string ResolveLine(const NamingBatch& batch)
{
    const NamingSubject& s = batch.subjects.front();
    return "resolve: analysis review op=resolve id=" + batch.id + " names='{\"" + Hex4(s.z80) +
           "\": \"Name\", \"$XXXX\": {\"label\": \"Name\", \"title\": \"...\"}}' (subjects left out are skipped; "
           "verdict=skip skips them all)\n";
}

bool ParseAddress(const std::string& text, uint16_t& z80)
{
    std::string t = text;
    if (!t.empty() && (t[0] == '$' || t[0] == '#'))
        t = t.substr(1);
    else if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        t = t.substr(2);
    if (t.empty() || t.size() > 4 ||
        !std::all_of(t.begin(), t.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }))
        return false;
    z80 = static_cast<uint16_t>(std::strtoul(t.c_str(), nullptr, 16));
    return true;
}

bool NameFrom(const StateNode& value, GivenName& name, std::string& error)
{
    if (value.kind == StateNode::Kind::String)
    {
        name.label = value.s;
        return true;
    }
    if (!value.isObject())
    {
        error = "a name is a label or an object {\"label\": ..., \"title\": ...}";
        return false;
    }
    for (const auto& [key, v] : value.members)
    {
        if (v.kind != StateNode::Kind::String)
        {
            error = key + ": a string";
            return false;
        }
        if (key == "label")
            name.label = v.s;
        else if (key == "title")
            name.title = v.s;
        else if (key != "at")
        {
            error = "unknown key " + key + " (at, label, title)";
            return false;
        }
    }
    return true;
}

}  // namespace

bool IsAddressName(const std::string& label, uint16_t z80)
{
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%04X", z80);
    if (label.size() < 4)
        return false;
    std::string tail = label.substr(label.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (tail != hex)
        return false;
    return std::all_of(label.begin(), label.end() - 4,
                       [](unsigned char c) { return std::isalpha(c) != 0 || c == '_'; });
}

NamingResult NamingBatches(const L1Result& l1, const L2Result& l2, const DraftPlan& plan)
{
    NamingResult result;
    std::set<uint16_t> routineEntries;
    for (const auto& e : l2.entries)
        routineEntries.insert(e.first);
    // The copy sources of buffers: a set the program copies as a whole
    std::vector<ExportRange> sources;
    for (const Region& r : l2.store.Regions())
        if (r.kind == RegionKind::Buffer)
            for (const uint16_t i : r.geometry.instances)
                sources.push_back(ExportRange{i, static_cast<uint16_t>(std::min<uint32_t>(0xFFFF, i + r.Size() - 1))});
    const auto inSource = [&](uint16_t z80) {
        return std::any_of(sources.begin(), sources.end(),
                           [z80](const ExportRange& s) { return s.start <= z80 && z80 <= s.end; });
    };

    // Subjects, grouped by entry
    std::vector<std::vector<NamingSubject>> groups;
    const DraftEntry* current = nullptr;
    for (const auto& [z80, label] : plan.labels)
    {
        if (z80 < 0x4000 || !IsAddressName(label, z80))
            continue;
        const DraftEntry* e = EntryOver(plan, z80);
        if (e == nullptr)
            continue;
        NamingSubject s;
        s.z80 = z80;
        s.label = label;
        s.code = e->ctl == 'c';
        s.entry = e->start == z80;
        s.entryStart = e->start;
        const bool referenced = plan.refs.count(z80) != 0;
        if (s.entry)
        {
            // Plain data the code uses is named too (a table read by index); untouched and unused bytes are not
            if (e->region == nullptr || e->ctl == 'u' || e->ctl == 'i' ||
                (IsFallbackClaim(*e->region) && (e->region->users.empty() || e->region->kind != RegionKind::Data)))
                continue;
            if (s.code && routineEntries.count(z80) == 0 && !referenced)
                continue;
            if (Graphic(e->region->kind) && !referenced && inSource(z80))
                continue;
        }
        if (e != current || groups.empty())
        {
            groups.emplace_back();
            current = e;
        }
        groups.back().push_back(std::move(s));
        ++result.subjects;
    }

    // Batches: neighbouring groups of one class, up to the subject count and the sheet size
    const std::map<uint16_t, std::vector<std::string>> accesses = Accesses(plan);
    constexpr size_t kRoom = kNameSheetBytes - 300;  // the head line and the resolve line
    NamingBatch batch;
    size_t bytes = 0;
    std::vector<NamingBatch> all;
    const auto close = [&]() {
        if (batch.subjects.empty())
            return;
        all.push_back(batch);
        batch = NamingBatch();
        bytes = 0;
    };
    for (const std::vector<NamingSubject>& group : groups)
    {
        for (const NamingSubject& s : group)
        {
            const size_t size = SubjectBlock(s, l1, l2, plan, accesses, nullptr).size() + 40;
            const bool fits = batch.subjects.empty() ||
                              (batch.code == s.code && batch.subjects.size() < kNameBatchSubjects && bytes + size <= kRoom);
            if (!fits)
                close();
            if (batch.subjects.empty())
            {
                batch.code = s.code;
                batch.start = s.entryStart;
            }
            const DraftEntry* e = EntryOver(plan, s.z80);
            batch.end = std::max(batch.end, e != nullptr ? e->end : s.z80);
            batch.subjects.push_back(s);
            bytes += size;
        }
    }
    close();

    // Over the cap: the batches that name the most go first (a routine's entry, then a data entry, then a point
    // inside code; a point inside data, a position in a buffer, the least), in address order among the kept ones
    std::vector<size_t> order(all.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    if (all.size() > kNameBatchCap)
    {
        const auto score = [](const NamingBatch& b) {
            uint32_t sum = 0;
            for (const NamingSubject& s : b.subjects)
                sum += s.entry ? (s.code ? 4u : 2u) : (s.code ? 1u : 0u);
            return sum;
        };
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t x, size_t y) { return score(all[x]) > score(all[y]); });
        for (size_t i = kNameBatchCap; i < order.size(); ++i)
            result.capped += static_cast<uint32_t>(all[order[i]].subjects.size());
        order.resize(kNameBatchCap);
        std::sort(order.begin(), order.end());
    }
    for (const size_t i : order)
    {
        all[i].id = "N" + std::to_string(result.batches.size() + 1);
        result.batches.push_back(std::move(all[i]));
    }
    return result;
}

std::string NamingSheet(const NamingBatch& batch, const L1Result& l1, const L2Result& l2, const DraftPlan& plan,
                        const std::function<std::string(const uint8_t* bytes, uint16_t z80)>& disassemble)
{
    const std::map<uint16_t, std::vector<std::string>> accesses = Accesses(plan);
    std::string sheet = batch.id + " name " + std::to_string(batch.subjects.size()) + " draft labels " +
                        Hex4(batch.start) + "-" + Hex4(batch.end) + " (" + (batch.code ? "code" : "data") +
                        "): give each a name (and a title) that says what it is for\n";
    const std::string resolve = ResolveLine(batch);
    for (const NamingSubject& s : batch.subjects)
    {
        const std::string block = SubjectBlock(s, l1, l2, plan, accesses, disassemble);
        if (sheet.size() + block.size() + resolve.size() > kNameSheetBytes)
        {
            sheet += "(" + Hex4(s.z80) + " " + s.label + " and on: no room, see analysis review op=show)\n";
            break;
        }
        sheet += block;
    }
    return sheet + resolve;
}

bool ParseGivenNames(const std::string& json, std::vector<GivenName>& names, std::string& error)
{
    StateNode node;
    std::string why;
    if (!StateNodeFromJsonText(json, node, &why))
    {
        error = "names is not JSON: " + why;
        return false;
    }
    names.clear();
    if (node.isObject())
    {
        for (const auto& [key, value] : node.members)
        {
            GivenName name;
            if (!ParseAddress(key, name.z80))
            {
                error = key + ": an address ($8068)";
                return false;
            }
            if (!NameFrom(value, name, error))
                return false;
            names.push_back(std::move(name));
        }
    }
    else if (node.isArray())
    {
        for (const StateNode& item : node.items)
        {
            GivenName name;
            const StateNode* at = nullptr;
            if (item.isObject())
                for (const auto& [key, value] : item.members)
                    if (key == "at")
                        at = &value;
            if (at == nullptr || at->kind != StateNode::Kind::String || !ParseAddress(at->s, name.z80))
            {
                error = "each item needs \"at\": an address ($8068)";
                return false;
            }
            if (!NameFrom(item, name, error))
                return false;
            names.push_back(std::move(name));
        }
    }
    else
    {
        error = "names: an object {\"$8068\": \"Name\"} or an array [{\"at\": \"$8068\", \"label\": \"Name\"}]";
        return false;
    }
    if (names.empty() || names.size() > 64)
    {
        error = "names: 1 to 64 names";
        return false;
    }
    return true;
}

}  // namespace analysis
