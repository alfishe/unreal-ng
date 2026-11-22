/// @file ttdcontrollerdiffmoments.cpp
/// @brief TimeTravelController::QueryDiffMoments (docs/inprogress/2026-11-03-ttd-diff-moments/design.md): what differs
/// between two moments of the recorded history. Each moment is replayed to and the machine's whole state taken there
/// (SaveLiveState); the write journal names the last write in between per changed range.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/ttddiffmoments.h"
#include "debugger/ttd/ttdscreenlayout.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace ttd
{

namespace
{

struct SectionName
{
    uint32_t bit;
    const char* name;
};
constexpr SectionName kSections[] = {
    {kDiffCpu, "cpu"},         {kDiffPaging, "paging"},   {kDiffMemory, "memory"},
    {kDiffScreen, "screen"},   {kDiffDevices, "devices"}, {kDiffBetween, "between"},
};

std::string Hex(uint32_t v, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%0*X", digits, v);
    return buf;
}

std::string Hex64(uint64_t v, int digits)
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "#%0*llX", digits, static_cast<unsigned long long>(v));
    return buf;
}

std::string TimeText(const TTDTimePoint& t)
{
    return "f" + std::to_string(t.frame) + "+" + std::to_string(t.tInFrame);
}

std::string SlotPageText(PhysPage page, uint8_t half)
{
    if (page == kPhysPageNone)
        return "ROM";
    return "RAM " + std::to_string(page) + (half ? " (other 8K half)" : "");
}

/// A byte a range spans before it is split: a gap of up to this many unchanged bytes joins two changed ones
constexpr uint32_t kRangeGap = 16;
/// A range this short shows its bytes at both moments
constexpr uint32_t kShowBytes = 8;

/// Everything taken at one moment
struct Moment
{
    TTDCpuState cpu{};
    TTDChipsetState chipset{};
    std::vector<uint8_t> ram;
    std::vector<std::vector<uint8_t>> devices;
    PhysPage slotPage[8] = {};
    uint8_t slotHalf[8] = {};
    std::vector<TTDScreenRange> screen;
};

/// The (page, flip) the slot showing Z80 address @p address had at @p m; false when ROM
bool AddressAt(const Moment& m, PhysPage page, uint32_t offsetInPage, uint16_t& address)
{
    for (uint8_t s = 0; s < 8; ++s)
    {
        if (m.slotPage[s] != page)
            continue;
        // The slot's 8K inside the page: its own parity, or the other one when the slot shows the other half
        const uint32_t half = ((s & 1u) ^ m.slotHalf[s]) & 1u;
        if (((offsetInPage >> 13) & 1u) == half)
        {
            address = static_cast<uint16_t>(s * 0x2000u + (offsetInPage & 0x1FFFu));
            return true;
        }
    }
    return false;
}

}  // namespace

bool TTDDiffSectionByName(const std::string& name, uint32_t& out)
{
    if (name == "all")
    {
        out = kDiffAll;
        return true;
    }
    for (const SectionName& s : kSections)
        if (name == s.name)
        {
            out = s.bit;
            return true;
        }
    return false;
}

const char* TTDDiffSectionName(uint32_t section)
{
    for (const SectionName& s : kSections)
        if (section == s.bit)
            return s.name;
    return "";
}

const char* TTDDiffRegionName(TTDDiffRegion region)
{
    switch (region)
    {
        case TTDDiffRegion::ScreenPixels: return "screen pixels";
        case TTDDiffRegion::ScreenAttributes: return "screen attributes";
        case TTDDiffRegion::Ram: break;
    }
    return "ram";
}

void TTDDiffDeviceFields(const TTDStateFieldList& layout, const std::vector<TTDTimeField>& timeFields,
                         const std::vector<uint8_t>& before, const std::vector<uint8_t>& after, uint32_t limit,
                         TTDDiffDevice& out)
{
    const size_t n = std::max(before.size(), after.size());
    const size_t common = std::min(before.size(), after.size());
    auto differs = [&](size_t i) { return i >= common || before[i] != after[i]; };
    out.described = !layout.empty();
    out.fields.clear();
    out.moreFields = 0;
    out.fieldsChanged = 0;
    out.countersChanged = 0;

    // Each byte's field: the layout's (the first that covers it), then the time fields over them. A time field that
    // is a layout field makes it a counter; one inside a larger field (an opaque engine block) is a field of its own
    std::vector<TTDDiffField> fields;
    fields.reserve(layout.size() + timeFields.size());
    std::vector<int32_t> owner(n, -1);
    for (const TTDStateField& f : layout)
    {
        if (f.size == 0 || f.offset >= n)
            continue;
        const int32_t index = static_cast<int32_t>(fields.size());
        TTDDiffField d;
        d.name = f.name;
        d.offset = f.offset;
        d.size = static_cast<uint32_t>(std::min<size_t>(f.size, n - f.offset));
        d.counter = (f.flags & kStateFieldCounter) != 0;
        fields.push_back(std::move(d));
        for (size_t b = f.offset; b < size_t(f.offset) + fields.back().size; ++b)
            if (owner[b] < 0)
                owner[b] = index;
    }
    for (const TTDTimeField& t : timeFields)
    {
        if (t.width == 0 || t.offset >= n)
            continue;
        const int32_t at = owner[t.offset];
        if (at >= 0 && fields[size_t(at)].offset == t.offset && fields[size_t(at)].size == t.width)
        {
            fields[size_t(at)].counter = true;
            continue;
        }
        TTDDiffField d;
        d.name = at >= 0 ? fields[size_t(at)].name + "+" + std::to_string(t.offset - fields[size_t(at)].offset)
                         : "@" + std::to_string(t.offset);
        d.offset = t.offset;
        d.size = static_cast<uint32_t>(std::min<size_t>(t.width, n - t.offset));
        d.counter = true;
        const int32_t index = static_cast<int32_t>(fields.size());
        fields.push_back(std::move(d));
        for (size_t b = t.offset; b < size_t(t.offset) + fields.back().size; ++b)
            owner[b] = index;
    }

    // The bytes that differ, per field; the ones no field covers as ranges (a gap of a few same bytes joins them)
    constexpr size_t kUnnamedGap = 3;
    std::vector<TTDDiffField> unnamed;
    for (size_t i = 0; i < n; ++i)
    {
        if (!differs(i))
            continue;
        if (owner[i] >= 0)
        {
            ++fields[size_t(owner[i])].changed;
            continue;
        }
        if (!unnamed.empty() && i - (unnamed.back().offset + unnamed.back().size) <= kUnnamedGap)
            unnamed.back().size = static_cast<uint32_t>(i + 1 - unnamed.back().offset);
        else
        {
            TTDDiffField d;
            d.name = "@" + std::to_string(i);
            d.offset = static_cast<uint32_t>(i);
            d.size = 1;
            unnamed.push_back(std::move(d));
        }
        ++unnamed.back().changed;
    }

    std::vector<TTDDiffField> changed;
    for (TTDDiffField& f : fields)
        if (f.changed)
            changed.push_back(std::move(f));
    for (TTDDiffField& f : unnamed)
        changed.push_back(std::move(f));
    std::stable_sort(changed.begin(), changed.end(), [](const TTDDiffField& a, const TTDDiffField& b) {
        return a.counter != b.counter ? !a.counter : a.offset < b.offset;
    });
    auto value = [](const std::vector<uint8_t>& v, const TTDDiffField& f) {
        uint64_t x = 0;
        for (uint32_t k = 0; k < f.size; ++k)
            x |= uint64_t(f.offset + k < v.size() ? v[f.offset + k] : 0) << (8 * k);
        return x;
    };
    for (TTDDiffField& f : changed)
    {
        (f.counter ? out.countersChanged : out.fieldsChanged)++;
        if (out.fields.size() >= limit)
        {
            ++out.moreFields;
            continue;
        }
        if (f.size <= 8 && f.offset + f.size <= common)
        {
            f.hasValues = true;
            f.before = value(before, f);
            f.after = value(after, f);
        }
        out.fields.push_back(std::move(f));
    }
}

std::string TTDDiffMomentsText(const TTDDiffMomentsQuery& query, const TTDDiffMomentsResult& r)
{
    const uint32_t s = query.sections ? query.sections : kDiffAll;
    std::string text = "diff " + TimeText(r.from) + " -> " + TimeText(r.to);
    if (s & kDiffBetween)
    {
        text += ": " + std::to_string(r.frames) + " frame(s), " + std::to_string(r.t) + " T" +
                (r.backwards ? " (backwards)" : "");
    }
    text += "\n";

    auto values = [&](const char* title, const std::vector<TTDDiffValue>& list) {
        if (list.empty())
            return;
        text += title;
        for (const TTDDiffValue& v : list)
            text += " " + v.name + " " + Hex(v.before, v.digits) + "->" + Hex(v.after, v.digits) + ";";
        text.back() = '\n';
    };
    if (s & kDiffCpu)
    {
        if (r.registers.empty())
            text += "cpu: same\n";
        values("cpu:", r.registers);
    }
    if (s & kDiffPaging)
    {
        values("latches:", r.latches);
        if (!r.slotMap.empty())
        {
            text += "mapping:";
            for (const TTDDiffSlot& sl : r.slotMap)
                text += " " + Hex(sl.slot * 0x2000u, 4) + " " + SlotPageText(sl.before, sl.beforeHalf) + "->" +
                        SlotPageText(sl.after, sl.afterHalf) + ";";
            text.back() = '\n';
        }
        if (r.latches.empty() && r.slotMap.empty())
            text += "paging: same\n";
    }
    if (s & kDiffMemory)
    {
        text += "memory: " + std::to_string(r.ramChanged) + " byte(s) in " + std::to_string(r.pagesChanged) +
                " page(s)";
        text += r.memory.empty() ? "\n" : ":\n";
        for (const TTDDiffRange& m : r.memory)
        {
            text += "  ";
            if (m.hasAddress)
                text += Hex(m.address, 4) + " ";
            text += "(RAM " + std::to_string(m.page) + " " + Hex(m.offset, 4) + ")";
            if (m.region != TTDDiffRegion::Ram)
                text += std::string(" ") + TTDDiffRegionName(m.region);
            text += " " + std::to_string(m.changed) + "/" + std::to_string(m.length);
            if (!m.before.empty())
            {
                text += " ";
                for (uint8_t b : m.before)
                    text += Hex(b, 2).substr(1);
                text += "->";
                for (uint8_t b : m.after)
                    text += Hex(b, 2).substr(1);
                if (m.before.size() == 1)
                    text += " (" + std::to_string(int(m.after[0]) - int(m.before[0])) + ")";
            }
            if (m.hasWriter)
            {
                text += ", last write " + TimeText(m.writerTime) + " pc " + Hex(m.writerPc, 4);
                if (!m.writerText.empty())
                    text += " " + m.writerText;
            }
            else if (m.writerUnknown)
                text += ", no CPU write in between";
            text += "\n";
        }
        if (r.moreMemory)
            text += "  +" + std::to_string(r.moreMemory) + " more range(s)\n";
    }
    if ((s & kDiffScreen) && r.hasScreen)
    {
        text += "screen: " + std::to_string(r.pixelsChanged + r.attributesChanged) + "/" + std::to_string(r.screenBytes) +
                " byte(s) (" + std::to_string(r.pixelsChanged) + " pixels, " + std::to_string(r.attributesChanged) +
                " attributes)";
        if (r.screenSwitched)
            text += ", shown page switched";
        if (r.borderBefore != r.borderAfter)
            text += ", border " + std::to_string(r.borderBefore) + "->" + std::to_string(r.borderAfter);
        text += "\n";
    }
    if (s & kDiffDevices)
    {
        if (r.devices.empty() && r.moreDevices == 0)
            text += "devices: same\n";
        else
        {
            const size_t changed = r.devices.size() + r.moreDevices;
            text += "devices: " + std::to_string(changed) + " changed";
            if (r.devicesCountersOnly)
                text += " (" + std::to_string(r.devicesCountersOnly) + " only counters)";
            text += ", " + std::to_string(r.devicesSame) + " same:\n";
            for (const TTDDiffDevice& d : r.devices)
            {
                text += "  " + d.name + " " + std::to_string(d.changed) + "/" + std::to_string(d.bytes) + " byte(s)";
                text += d.CountersOnly() ? ", only counters:" : ":";
                std::string counters;
                for (const TTDDiffField& f : d.fields)
                {
                    if (f.counter)
                    {
                        counters += (counters.empty() ? " " : ", ") + f.name;
                        continue;
                    }
                    text += " " + f.name;
                    if (f.hasValues)
                        text += " " + Hex64(f.before, int(f.size) * 2) + "->" + Hex64(f.after, int(f.size) * 2);
                    else
                        text += " " + std::to_string(f.changed) + "/" + std::to_string(f.size) + " byte(s)";
                    text += ";";
                }
                if (!counters.empty())
                    text += (d.CountersOnly() ? "" : " counters:") + counters + ";";
                if (d.moreFields)
                    text += " +" + std::to_string(d.moreFields) + " more;";
                text.back() = '\n';
            }
            if (r.moreDevices)
                text += "  +" + std::to_string(r.moreDevices) + " more device(s)\n";
        }
    }
    if (s & kDiffBetween)
    {
        if (!r.keysHeld.empty() || !r.keys.empty())
        {
            text += "keys:";
            if (!r.keysHeld.empty())
            {
                text += " held";
                for (const std::string& k : r.keysHeld)
                    text += " " + k;
                text += ";";
            }
            for (const TTDDiffKey& k : r.keys)
                text += " " + TimeText(k.time) + " " + k.key + (k.pressed ? " pressed;" : " released;");
            if (r.moreKeys)
                text += " +" + std::to_string(r.moreKeys) + " more;";
            text.back() = '\n';
        }
        if (!r.markers.empty())
        {
            text += "markers:";
            for (const std::string& m : r.markers)
                text += " " + m + ";";
            text.back() = '\n';
        }
        if (!r.barrier.empty())
            text += "barrier in between: " + r.barrier + " (writers before it not searched)\n";
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%u writer search(es), %.1f ms\n", r.writerSearches, r.ms);
    return text + buf;
}

TTDDiffMomentsResult TimeTravelController::QueryDiffMoments(const TTDDiffMomentsQuery& query)
{
    const auto started = std::chrono::steady_clock::now();
    // Replays to both moments and puts the machine back: an operation that changes it
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDDiffMomentsResult result;
    auto finish = [&]() -> TTDDiffMomentsResult& {
        result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return result;
    };

    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    Memory* memory = _context ? _context->pMemory : nullptr;
    Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (_line->timeline.empty() || !memory || !z80 || !_context->pEmulator)
    {
        result.error = "no recorded history";
        return finish();
    }
    const uint32_t limit = std::clamp(query.limit, 1u, TTDDiffMomentsQuery::kMaxLimit);
    const uint32_t sections = query.sections ? query.sections : kDiffAll;
    const TTDTimePoint sessionStart = _lines.front()->timeline.empty() ? _line->timeline.front().time
                                                                       : _lines.front()->timeline.front().time;   // the lineage's
    const TTDTimePoint sessionEnd = SessionEndPosition();
    const TTDTimePoint from = query.from;
    const TTDTimePoint to = query.hasTo ? query.to : CurrentPosition();
    for (const TTDTimePoint& at : {from, to})
        if (at < sessionStart || sessionEnd < at || at.tInFrame >= FrameSpan())
        {
            result.error = "the moment " + TimeText(at) + " is outside the recorded history (" +
                           TimeText(sessionStart) + " .. " + TimeText(sessionEnd) + ")";
            return finish();
        }

    // ---- both moments ----
    const TimeTravelEngine& engine = *_engine;
    uint32_t ramRegion = UINT32_MAX;
    const std::vector<TTDRegionDesc>& regions = engine.Regions();
    for (uint32_t r = 0; r < regions.size(); ++r)
        if (regions[r].id < TTDRegionId::DeviceStateFirst && regions[r].name == "ram")
        {
            ramRegion = r;
            break;
        }
    const MEM_MODEL model = _context->config.mem_model;
    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot live;
    SaveLiveState(live);
    Moment moments[2];
    std::string reachError;
    for (int k = 0; k < 2 && reachError.empty(); ++k)
    {
        const TTDTimePoint at = k == 0 ? from : to;
        // A moment inside a frame is the first instruction boundary at or after it (where a seek stops)
        TTDTimePoint taken = at;
        bool there = CurrentPosition() == at;
        if (!there)
        {
            ReplayModeScope seek(*this);
            TTDSeekResult reached;
            there = SeekToInternal(at, &reached) && reached.reached && reached.haltReason == TTDSeekHaltReason::Target;
            taken = reached.arrivedAt;
            seek.Exit();
        }
        if (!there)
        {
            reachError = "the replay could not reach " + TimeText(at);
            break;
        }
        (k == 0 ? result.from : result.to) = taken;
        LiveStateSnapshot snap;
        SaveLiveState(snap);
        Moment& m = moments[k];
        m.cpu = snap.cpu;
        m.chipset = snap.chipset;
        m.ram = std::move(snap.ram);
        m.devices = std::move(snap.deviceStates);
        for (uint8_t s = 0; s < 8; ++s)
            m.slotPage[s] = memory->TtdKeyOf(static_cast<uint16_t>(s * 0x2000u), m.slotHalf[s]);
        // The picture: the engine's captured layout of the moment's frame, else the ULA page by the latch
        const int64_t ei = engine.CheckpointIndexOf({0, at.frame, 0});
        const TTDEngineCheckpoint* c = ei >= 0 ? engine.Checkpoint(size_t(ei)) : nullptr;
        if (const std::vector<TTDScreenRange>* l = c ? engine.ScreenLayoutOf(*c) : nullptr)
            m.screen = *l;
        else if (HasUlaScreen(model) && ramRegion != UINT32_MAX)
            m.screen = UlaScreenLayout(ramRegion, model, m.chipset.p7FFD);
    }
    {
        ReplayModeScope restore(*this);
        RestoreLiveState(live);
        restore.Exit();
        SetState(stateBefore);
    }
    if (!reachError.empty())
    {
        result.error = reachError;
        return finish();
    }
    const Moment& a = moments[0];
    const Moment& b = moments[1];
    result.backwards = result.to < result.from;
    const TTDTimePoint early = result.backwards ? result.to : result.from;
    const TTDTimePoint late = result.backwards ? result.from : result.to;

    // ---- cpu ----
    if (sections & kDiffCpu)
    {
        auto reg = [&](const char* name, uint32_t before, uint32_t after, uint8_t digits) {
            if (before != after)
                result.registers.push_back({name, before, after, digits});
        };
        reg("PC", a.cpu.pc, b.cpu.pc, 4);
        reg("SP", a.cpu.sp, b.cpu.sp, 4);
        reg("AF", a.cpu.af, b.cpu.af, 4);
        reg("BC", a.cpu.bc, b.cpu.bc, 4);
        reg("DE", a.cpu.de, b.cpu.de, 4);
        reg("HL", a.cpu.hl, b.cpu.hl, 4);
        reg("IX", a.cpu.ix, b.cpu.ix, 4);
        reg("IY", a.cpu.iy, b.cpu.iy, 4);
        reg("AF'", a.cpu.alt_af, b.cpu.alt_af, 4);
        reg("BC'", a.cpu.alt_bc, b.cpu.alt_bc, 4);
        reg("DE'", a.cpu.alt_de, b.cpu.alt_de, 4);
        reg("HL'", a.cpu.alt_hl, b.cpu.alt_hl, 4);
        reg("I", a.cpu.i, b.cpu.i, 2);
        reg("IFF1", a.cpu.iff1, b.cpu.iff1, 1);
        reg("IM", a.cpu.im, b.cpu.im, 1);
        reg("HALT", a.cpu.halted, b.cpu.halted, 1);
    }

    // ---- paging ----
    if (sections & kDiffPaging)
    {
        auto latch = [&](const char* name, uint32_t before, uint32_t after) {
            if (before != after)
                result.latches.push_back({name, before, after, 2});
        };
        latch("7FFD", a.chipset.p7FFD, b.chipset.p7FFD);
        latch("EFF7", a.chipset.pEFF7, b.chipset.pEFF7);
        latch("FE", a.chipset.pFE, b.chipset.pFE);
        latch("AY register", a.chipset.pBFFD, b.chipset.pBFFD);
        latch("flags", a.chipset.flags, b.chipset.flags);
        latch("turbo", a.chipset.hw_turbo_ratio, b.chipset.hw_turbo_ratio);
        for (uint8_t s = 0; s < 8; ++s)
            if (a.slotPage[s] != b.slotPage[s] || a.slotHalf[s] != b.slotHalf[s])
                result.slotMap.push_back({s, a.slotPage[s], a.slotHalf[s], b.slotPage[s], b.slotHalf[s]});
    }

    // ---- memory, screen ----
    const size_t ramSize = std::min(a.ram.size(), b.ram.size());
    auto screenClassOf = [&](const std::vector<TTDScreenRange>& layout, uint32_t offset) -> int {
        uint32_t seen = 0;
        for (const TTDScreenRange& r : layout)
        {
            if (r.region == ramRegion && offset >= r.begin && offset < r.end)
            {
                const uint32_t at = seen + (offset - r.begin);
                return layout.size() == 1 && r.end - r.begin == 6912 && at >= 6144 ? 2 : 1;
            }
            seen += r.end - r.begin;
        }
        return 0;
    };
    std::vector<TTDDiffRange> ranges;
    {
        int screenRange[3] = {-1, -1, -1};
        int open = -1;
        uint32_t openLast = 0;
        uint32_t lastPage = UINT32_MAX;
        for (uint32_t o = 0; o < ramSize; ++o)
        {
            if (a.ram[o] == b.ram[o])
                continue;
            ++result.ramChanged;
            const uint32_t page = o / 0x4000u;
            if (page != lastPage)
            {
                ++result.pagesChanged;
                lastPage = page;
            }
            const int sc = std::max(screenClassOf(a.screen, o), screenClassOf(b.screen, o));
            size_t index = SIZE_MAX;
            if (sc)
            {
                if (screenRange[sc] < 0)
                {
                    TTDDiffRange r;
                    r.region = sc == 1 ? TTDDiffRegion::ScreenPixels : TTDDiffRegion::ScreenAttributes;
                    r.page = static_cast<PhysPage>(page);
                    r.offset = o & 0x3FFFu;
                    ranges.push_back(r);
                    screenRange[sc] = static_cast<int>(ranges.size() - 1);
                }
                index = size_t(screenRange[sc]);
            }
            else
            {
                if (open >= 0 && ranges[size_t(open)].page == page && o - openLast <= kRangeGap)
                    index = size_t(open);
                else
                {
                    TTDDiffRange r;
                    r.page = static_cast<PhysPage>(page);
                    r.offset = o & 0x3FFFu;
                    ranges.push_back(r);
                    open = static_cast<int>(ranges.size() - 1);
                    index = ranges.size() - 1;
                }
                openLast = o;
            }
            TTDDiffRange& r = ranges[index];
            const uint32_t start = static_cast<uint32_t>(r.page) * 0x4000u + r.offset;
            r.length = std::max(r.length, o - start + 1);
            ++r.changed;
        }
    }
    if (sections & kDiffMemory)
    {
        result.moreMemory = ranges.size() > limit ? static_cast<uint32_t>(ranges.size() - limit) : 0;
        for (size_t i = 0; i < ranges.size() && i < limit; ++i)
        {
            TTDDiffRange r = ranges[i];
            const uint32_t start = static_cast<uint32_t>(r.page) * 0x4000u + r.offset;
            if (r.length <= kShowBytes)
            {
                r.before.assign(a.ram.begin() + start, a.ram.begin() + start + r.length);
                r.after.assign(b.ram.begin() + start, b.ram.begin() + start + r.length);
            }
            uint16_t address = 0;
            if (AddressAt(b, r.page, r.offset, address) || AddressAt(a, r.page, r.offset, address))
            {
                r.hasAddress = true;
                r.address = address;
            }
            result.memory.push_back(std::move(r));
        }
    }
    if ((sections & kDiffScreen) && !b.screen.empty())
    {
        result.hasScreen = true;
        for (const TTDScreenRange& sr : b.screen)
            if (sr.region == ramRegion)
                for (uint32_t o = sr.begin; o < sr.end && o < ramSize; ++o)
                {
                    ++result.screenBytes;
                    if (a.ram[o] != b.ram[o])
                    {
                        if (screenClassOf(b.screen, o) == 2)
                            ++result.attributesChanged;
                        else
                            ++result.pixelsChanged;
                    }
                }
        auto shown = [&](const std::vector<TTDScreenRange>& l) { return l.empty() ? UINT32_MAX : l.front().begin; };
        result.screenSwitched = shown(a.screen) != shown(b.screen);
        result.borderBefore = a.chipset.pFE & 7;
        result.borderAfter = b.chipset.pFE & 7;
    }

    // ---- devices ----
    if (sections & kDiffDevices)
    {
        // Field by field where the device names them (TTDDescribeFields); those whose counters alone moved after
        // the others, so the limit keeps the devices where something happened
        const std::vector<TTDRegistryEntry>& entries = _peripherals.Entries();
        std::vector<TTDDiffDevice> changed;
        std::vector<TTDDiffDevice> countersOnly;
        for (size_t d = 0; d < entries.size() && d < a.devices.size() && d < b.devices.size(); ++d)
        {
            const std::vector<uint8_t>& x = a.devices[d];
            const std::vector<uint8_t>& y = b.devices[d];
            uint32_t bytes = 0;
            const size_t n = std::min(x.size(), y.size());
            for (size_t i = 0; i < n; ++i)
                bytes += x[i] != y[i];
            bytes += static_cast<uint32_t>(std::max(x.size(), y.size()) - n);
            if (bytes == 0)
            {
                ++result.devicesSame;
                continue;
            }
            const TTDSerializable* device = entries[d].device;
            TTDDiffDevice dev;
            const std::string& instance = entries[d].key.instance;
            dev.name = instance.empty() && device ? device->TTDDeviceName() : instance;
            dev.bytes = static_cast<uint32_t>(std::max(x.size(), y.size()));
            dev.changed = bytes;
            TTDStateFieldList layout;
            std::vector<TTDTimeField> timeFields;
            if (device)
            {
                device->TTDDescribeFields(layout);
                timeFields = device->TTDDescribe().timeFields;
            }
            TTDDiffDeviceFields(layout, timeFields, x, y, limit, dev);
            if (dev.CountersOnly())
            {
                ++result.devicesCountersOnly;
                countersOnly.push_back(std::move(dev));
            }
            else
                changed.push_back(std::move(dev));
        }
        for (std::vector<TTDDiffDevice>* list : {&changed, &countersOnly})
            for (TTDDiffDevice& dev : *list)
            {
                if (result.devices.size() >= limit)
                    ++result.moreDevices;
                else
                    result.devices.push_back(std::move(dev));
            }
    }

    // ---- between ----
    result.frames = late.frame - early.frame;
    result.t = GlobalT(late) - GlobalT(early);
    if (const std::optional<TTDExternalEvent> barrier = FirstBarrierBetween(early, late))
        result.barrier = TimeText(barrier->time) + " " +
                         std::string(barrier->reason, strnlen(barrier->reason, sizeof(barrier->reason)));
    if (sections & kDiffBetween)
    {
        std::vector<uint8_t> held;
        for (const TTDInputEvent& e : _line->inputJournal.Events())
        {
            if (e.kind != TTDInputKind::Key)
                continue;
            if (e.time < early || e.time == early)
            {
                auto it = std::find(held.begin(), held.end(), e.key);
                if (e.pressed && it == held.end())
                    held.push_back(e.key);
                else if (!e.pressed && it != held.end())
                    held.erase(it);
            }
            else if (e.time < late || e.time == late)
            {
                if (result.keys.size() < limit)
                    result.keys.push_back({e.time, DebugKeyboardManager::GetKeyDisplayName(static_cast<ZXKeysEnum>(e.key)),
                                           e.pressed});
                else
                    ++result.moreKeys;
            }
            else
                break;
        }
        for (uint8_t key : held)
            result.keysHeld.push_back(DebugKeyboardManager::GetKeyDisplayName(static_cast<ZXKeysEnum>(key)));
        auto inside = [&](const TTDTimePoint& t) { return early < t && (t < late || t == late); };
        for (const TTDExternalEvent& e : _line->externalEvents.Events())
            if (inside(e.time))
                result.markers.push_back(TimeText(e.time) + " " + std::string(e.reason, strnlen(e.reason, sizeof(e.reason))));
        for (const TTDBookmark& bm : GetBookmarks())
            if (inside(bm.time))
                result.markers.push_back(TimeText(bm.time) + " bookmark '" + bm.label + "'");
    }

    // ---- the last write in between per listed range (the newest CPU write to it after the earlier moment) ----
    if ((sections & kDiffMemory) && query.writers && !(early == late))
    {
        Z80Disassembler* disassembler =
            _context->pDebugManager ? _context->pDebugManager->GetDisassembler().get() : nullptr;
        const Moment& late_ = result.backwards ? a : b;
        const Moment& early_ = result.backwards ? b : a;
        for (TTDDiffRange& r : result.memory)
        {
            // The Z80 addresses the range had at the later moment (else at the earlier); a range no slot showed is
            // not searched by address
            uint16_t address = 0;
            if (!AddressAt(late_, r.page, r.offset, address) && !AddressAt(early_, r.page, r.offset, address))
                continue;
            const uint32_t span = std::min<uint32_t>(r.length, 0x10000u - address);
            TTDSearchQuery q;
            q.access = TTDAccessType::Write;
            q.addrFrom = address;
            q.addrTo = static_cast<uint16_t>(address + span - 1);
            q.hasPhysPageFilter = true;
            q.physPage = r.page;
            q.beforeGlobalT = GlobalT(late);
            TTDFindOptions options;
            options.fromFrame = early.frame;
            ++result.writerSearches;
            const TTDFindResult found = FindAccesses(q, options);
            const TTDFoundAccess* hit = nullptr;
            for (const TTDFoundAccess& h : found.hits)
                if (early < h.access.time)
                {
                    hit = &h;
                    break;
                }
            if (!hit)
            {
                r.writerUnknown = true;
                continue;
            }
            r.hasWriter = true;
            r.writerTime = hit->access.time;
            r.writerPc = hit->access.pc;
            r.writerAddress = hit->access.addr;
            if (disassembler)
            {
                const TTDCpuView view = ReadCpuView(hit->access.time, hit->access.pc, 4);
                if (view.ok && view.bytes.size() == 4)
                {
                    uint8_t len = 0;
                    r.writerText = disassembler->disassembleSingleCommand(view.bytes, hit->access.pc, &len);
                }
            }
        }
    }
    return finish();
}

}  // namespace ttd
