#include "debugger/analyzers/analysis/l1model.h"

#include <algorithm>
#include <cstdio>

#include "emulator/state/statenodejson.h"

namespace analysis
{

namespace
{

std::string Hex4(unsigned value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "$%04X", value & 0xFFFFu);
    return buffer;
}

std::string Hex2(unsigned value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "$%02X", value & 0xFFu);
    return buffer;
}

/// "12.3%" of @p part in @p total, integer arithmetic (the same text on every compiler)
std::string Percent(uint64_t part, uint64_t total)
{
    if (total == 0)
        return "0%";
    const uint64_t permille = (part * 1000 + total / 2) / total;
    return std::to_string(permille / 10) + "." + std::to_string(permille % 10) + "%";
}

uint64_t Permille(uint64_t part, uint64_t total)
{
    return total == 0 ? 0 : (part * 1000 + total / 2) / total;
}

std::string SignedText(int32_t value)
{
    return (value >= 0 ? "+" : "") + std::to_string(value);
}

const char* CondName(uint8_t cond)
{
    static const char* const kNames[] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M", "B<>0"};
    return cond < 9 ? kNames[cond] : "";
}

}  // namespace

const char* EdgeKindName(EdgeKind kind)
{
    switch (kind)
    {
        case EdgeKind::Fallthrough:
            return "fallthrough";
        case EdgeKind::Taken:
            return "taken";
        case EdgeKind::AfterCall:
            return "after-call";
        case EdgeKind::Call:
            return "call";
        case EdgeKind::Indirect:
            return "indirect";
        case EdgeKind::Dispatch:
            return "dispatch";
        case EdgeKind::Interrupt:
            return "interrupt";
    }
    return "?";
}

const char* CallKindName(CallKind kind)
{
    switch (kind)
    {
        case CallKind::Call:
            return "call";
        case CallKind::Rst:
            return "rst";
        case CallKind::Tail:
            return "tail";
        case CallKind::FallsInto:
            return "falls-into";
        case CallKind::Dispatch:
            return "dispatch";
        case CallKind::Indirect:
            return "indirect";
        case CallKind::Interrupt:
            return "interrupt";
    }
    return "?";
}

const char* BaseRegName(BaseReg reg)
{
    switch (reg)
    {
        case BaseReg::IX:
            return "IX";
        case BaseReg::IY:
            return "IY";
        case BaseReg::HL:
            return "HL";
    }
    return "?";
}

const char* SourceKindName(SourceKind kind)
{
    switch (kind)
    {
        case SourceKind::None:
            return "none";
        case SourceKind::Memory:
            return "memory";
        case SourceKind::Port:
            return "port";
        case SourceKind::Constant:
            return "constant";
        case SourceKind::Register:
            return "register";
    }
    return "?";
}

std::string EntryKindNames(uint16_t kinds)
{
    static const std::pair<uint16_t, const char*> kNames[] = {
        {entry::kCall, "call"},         {entry::kRst, "rst"},       {entry::kInterrupt, "interrupt"},
        {entry::kNmi, "nmi"},           {entry::kDispatch, "dispatch"}, {entry::kIndirect, "indirect"},
        {entry::kJump, "jump"},         {entry::kShared, "shared"}, {entry::kRoot, "root"}};
    std::string text;
    for (const auto& [bit, name] : kNames)
        if ((kinds & bit) != 0)
            text += (text.empty() ? "" : ",") + std::string(name);
    return text;
}

std::string L1Result::RegNames(uint16_t mask)
{
    struct Pair
    {
        uint16_t hiBit;
        uint16_t loBit;
        const char* pair;  ///< nullptr: always the halves (A F)
        const char* hi;
        const char* lo;
    };
    static const Pair kPairs[] = {{reg::kA, reg::kF, nullptr, "A", "F"},
                                  {reg::kB, reg::kC, "BC", "B", "C"},
                                  {reg::kD, reg::kE, "DE", "D", "E"},
                                  {reg::kH, reg::kL, "HL", "H", "L"},
                                  {reg::kIXH, reg::kIXL, "IX", "IXH", "IXL"},
                                  {reg::kIYH, reg::kIYL, "IY", "IYH", "IYL"}};
    std::string text;
    const auto add = [&text](const char* name) { text += (text.empty() ? "" : " ") + std::string(name); };
    for (const Pair& p : kPairs)
    {
        const bool hi = (mask & p.hiBit) != 0;
        const bool lo = (mask & p.loBit) != 0;
        if (hi && lo && p.pair != nullptr)
            add(p.pair);
        else
        {
            if (hi)
                add(p.hi);
            if (lo)
                add(p.lo);
        }
    }
    if (mask & reg::kSP)
        add("SP");
    return text.empty() ? "-" : text;
}

const char* PointerFormName(PointerForm form)
{
    switch (form)
    {
        case PointerForm::Immediate:
            return "immediate";
        case PointerForm::ImmediateHigh:
            return "immediate_high";
        case PointerForm::Word:
            return "word";
    }
    return "";
}

const ByteUse* L1Result::UseAt(PhysKey key) const
{
    const uint16_t page = KeyPage(key);
    const auto it = std::lower_bound(bytePages.begin(), bytePages.end(), page,
                                     [](const BytePage& p, uint16_t value) { return p.page < value; });
    if (it == bytePages.end() || it->page != page || it->bytes.size() <= KeyOffset(key))
        return nullptr;
    return &it->bytes[KeyOffset(key)];
}

namespace
{
const PageImage* ImageOf(const std::vector<PageImage>& images, uint16_t page)
{
    const auto it = std::lower_bound(images.begin(), images.end(), page,
                                     [](const PageImage& p, uint16_t value) { return p.page < value; });
    return it != images.end() && it->page == page ? &*it : nullptr;
}
}  // namespace

bool L1Result::StartByte(PhysKey key, uint8_t& value) const
{
    const PageImage* image = ImageOf(images, KeyPage(key));
    if (image == nullptr || image->start.size() <= KeyOffset(key))
        return false;
    value = image->start[KeyOffset(key)];
    return true;
}

bool L1Result::EndByte(PhysKey key, uint8_t& value) const
{
    const PageImage* image = ImageOf(images, KeyPage(key));
    if (image == nullptr || image->end.size() <= KeyOffset(key))
        return false;
    value = image->end[KeyOffset(key)];
    return true;
}

void L1Result::BuildIndex()
{
    byZ80.resize(instructions.size());
    for (uint32_t i = 0; i < instructions.size(); ++i)
        byZ80[i] = i;
    std::sort(byZ80.begin(), byZ80.end(), [this](uint32_t a, uint32_t b) {
        return std::make_pair(instructions[a].z80, instructions[a].key) <
               std::make_pair(instructions[b].z80, instructions[b].key);
    });
    if (instructionRoutine.size() != instructions.size())
        instructionRoutine.assign(instructions.size(), 0);
}

const Instruction* L1Result::InstructionAt(PhysKey key) const
{
    const auto it = std::lower_bound(instructions.begin(), instructions.end(), key,
                                     [](const Instruction& ins, PhysKey k) { return ins.key < k; });
    return it != instructions.end() && it->key == key ? &*it : nullptr;
}

const Routine* L1Result::RoutineOf(PhysKey key) const
{
    const auto it = std::lower_bound(instructions.begin(), instructions.end(), key,
                                     [](const Instruction& ins, PhysKey k) { return ins.key < k; });
    if (it == instructions.end() || it->key != key || routines.empty())
        return nullptr;
    const size_t index = static_cast<size_t>(it - instructions.begin());
    return index < instructionRoutine.size() ? &routines[instructionRoutine[index]] : nullptr;
}

const Routine* L1Result::RoutineAtZ80(uint16_t z80, int page) const
{
    // An entry at that address first (the routine asked for by its start), then an instruction covering it
    for (const Routine& r : routines)
        if (r.z80 == z80 && (page < 0 || KeyPage(r.entry) == page))
            return &r;
    const Routine* best = nullptr;
    for (const uint32_t i : byZ80)
    {
        const Instruction& ins = instructions[i];
        if (ins.z80 > z80)
            break;
        if (page >= 0 && KeyPage(ins.key) != page)
            continue;
        if (static_cast<uint32_t>(ins.z80) + ins.length > z80 && i < instructionRoutine.size() && best == nullptr)
            best = &routines[instructionRoutine[i]];
    }
    return best;
}

namespace
{

StateNode IntervalNode(const Interval& iv)
{
    StateNode node = StateNode::Object();
    node["from"] = Hex4(iv.z80);
    node["to"] = Hex4(static_cast<unsigned>(iv.z80 + (iv.hi - iv.lo)));
    node["page"] = static_cast<unsigned>(KeyPage(iv.lo));
    node["bytes"] = static_cast<uint64_t>(iv.hi - iv.lo + 1);
    return node;
}

StateNode IntervalList(const std::vector<Interval>& list)
{
    StateNode node = StateNode::Array();
    for (const Interval& iv : list)
        node.push(IntervalNode(iv));
    return node;
}

StateNode PortList(const std::vector<uint16_t>& ports)
{
    StateNode node = StateNode::Array();
    for (const uint16_t port : ports)
        node.push(Hex4(port));
    return node;
}

StateNode ProfileNode(const RoutineProfile& p, uint64_t totalT)
{
    StateNode node = StateNode::Object();
    node["reads"] = IntervalList(p.reads);
    node["writes"] = IntervalList(p.writes);
    node["executes"] = IntervalList(p.executes);
    node["ports_in"] = PortList(p.portsIn);
    node["ports_out"] = PortList(p.portsOut);
    node["ports_in_more"] = p.portsInMore;
    node["ports_out_more"] = p.portsOutMore;
    node["tstates"] = p.tstates;
    node["tstates_inclusive"] = p.tstatesInclusive;
    node["tstates_permille"] = Permille(p.tstates, totalT);
    node["entries"] = p.entries;
    node["active_frames"] = static_cast<uint64_t>(p.activeFrames);
    node["first_frame"] = p.firstFrame;
    node["last_frame"] = p.lastFrame;
    node["live_in"] = L1Result::RegNames(p.liveIn);
    node["live_out"] = L1Result::RegNames(p.liveOut);
    node["written"] = L1Result::RegNames(p.written);
    node["min_depth"] = static_cast<unsigned>(p.minDepth);
    node["max_depth"] = static_cast<unsigned>(p.maxDepth);
    node["abandoned"] = p.abandoned;
    node["patched_returns"] = p.patchedReturns;
    return node;
}

StateNode LoopNode(const Loop& loop)
{
    StateNode node = StateNode::Object();
    node["header"] = Hex4(loop.z80);
    node["page"] = static_cast<unsigned>(KeyPage(loop.header));
    node["latch"] = Hex4(loop.latchZ80);
    node["routine"] = static_cast<uint64_t>(loop.routine);
    node["instances"] = loop.instances;
    node["passes"] = loop.passes;
    node["min_trips"] = static_cast<uint64_t>(loop.minTrips);
    node["max_trips"] = static_cast<uint64_t>(loop.maxTrips);
    StateNode trips = StateNode::Array();
    for (const auto& [t, n] : loop.trips)
    {
        StateNode pair = StateNode::Array();
        pair.push(static_cast<uint64_t>(t));
        pair.push(n);
        trips.push(std::move(pair));
    }
    node["trips"] = std::move(trips);
    StateNode induction = StateNode::Array();
    for (const Induction& i : loop.induction)
    {
        StateNode entry = StateNode::Object();
        entry["reg"] = i.reg;
        entry["step"] = static_cast<int64_t>(i.step);
        induction.push(std::move(entry));
    }
    node["induction"] = std::move(induction);
    StateNode strides = StateNode::Array();
    for (const Stride& s : loop.strides)
    {
        StateNode entry = StateNode::Object();
        entry["at"] = Hex4(s.z80);
        entry["access"] = s.write ? "write" : "read";
        entry["stride"] = static_cast<int64_t>(s.stride);
        entry["constant"] = s.constant;
        if (!s.ranges.empty())
            entry["ranges"] = IntervalList(s.ranges);
        if (s.rangesMore)
            entry["ranges_more"] = true;
        if (!s.starts.empty())
        {
            StateNode starts = StateNode::Array();
            for (const uint16_t a : s.starts)
                starts.push(Hex4(a));
            entry["starts"] = std::move(starts);
            entry["starts_seen"] = static_cast<uint64_t>(s.startsSeen);
        }
        if (!s.origins.empty())
            entry["origins"] = IntervalList(s.origins);
        if (s.originsMore)
            entry["origins_more"] = true;
        strides.push(std::move(entry));
    }
    node["strides"] = std::move(strides);
    node["delay"] = loop.delay;
    return node;
}

StateNode PatternNode(const BaseOffsetPattern& p)
{
    StateNode node = StateNode::Object();
    node["routine"] = static_cast<uint64_t>(p.routine);
    node["base"] = BaseRegName(p.base);
    StateNode offsets = StateNode::Array();
    for (const int16_t o : p.offsets)
        offsets.push(static_cast<int64_t>(o));
    node["offsets"] = std::move(offsets);
    StateNode bases = StateNode::Array();
    for (const uint16_t b : p.bases)
        bases.push(Hex4(b));
    node["bases"] = std::move(bases);
    node["bases_seen"] = static_cast<uint64_t>(p.basesSeen);
    node["step"] = static_cast<int64_t>(p.step);
    node["reads"] = p.reads;
    node["writes"] = p.writes;
    StateNode at = StateNode::Array();
    for (const uint16_t a : p.instructions)
        at.push(Hex4(a));
    node["instructions"] = std::move(at);
    StateNode words = StateNode::Array();
    for (const int16_t o : p.wordOffsets)
        words.push(static_cast<int64_t>(o));
    node["word_offsets"] = std::move(words);
    StateNode atBase = StateNode::Array();
    for (const uint8_t n : p.offsetsAtBase)
        atBase.push(static_cast<unsigned>(n));
    node["offsets_at_base"] = std::move(atBase);
    return node;
}

StateNode BranchNode(const BranchCondition& b)
{
    StateNode node = StateNode::Object();
    node["at"] = Hex4(b.z80);
    node["page"] = static_cast<unsigned>(KeyPage(b.instruction));
    node["cond"] = CondName(b.cond);
    node["taken"] = b.taken;
    node["not_taken"] = b.notTaken;
    node["producer"] = Hex4(b.producerZ80);
    node["producer_class"] = OpClassName(b.producerClass);
    node["tested"] = SourceKindName(b.tested);
    node["address"] = Hex4(b.address);
    node["constant"] = Hex2(b.constant);
    node["varies"] = b.varies;
    node["first_frame"] = static_cast<uint64_t>(b.firstFrame);
    node["last_frame"] = static_cast<uint64_t>(b.lastFrame);
    return node;
}

/// "$8B70 (label)"
std::string Named(uint16_t address, const L1Result::LabelOf& label)
{
    const std::string name = label ? label(address) : std::string();
    return name.empty() ? Hex4(address) : Hex4(address) + " (" + name + ")";
}

std::string IntervalText(const std::vector<Interval>& list, size_t max)
{
    std::string text;
    for (size_t i = 0; i < list.size() && i < max; ++i)
    {
        const Interval& iv = list[i];
        const unsigned to = static_cast<unsigned>(iv.z80 + (iv.hi - iv.lo));
        text += (i ? ", " : "") + (iv.hi == iv.lo ? Hex4(iv.z80) : Hex4(iv.z80) + "-" + Hex4(to));
    }
    if (list.size() > max)
        text += " (+" + std::to_string(list.size() - max) + ")";
    return text.empty() ? "-" : text;
}

std::string PortText(const std::vector<uint16_t>& ports, bool more)
{
    std::string text;
    for (size_t i = 0; i < ports.size() && i < 4; ++i)
        text += (i ? " " : "") + Hex4(ports[i]);
    if (ports.size() > 4 || more)
        text += " ...";
    return text;
}

}  // namespace

StateNode L1Result::RoutineNode(const Routine& routine) const
{
    const uint32_t index = IndexOf(routine);
    StateNode node = StateNode::Object();
    node["routine"] = static_cast<uint64_t>(index);
    node["entry"] = Hex4(routine.z80);
    node["page"] = static_cast<unsigned>(KeyPage(routine.entry));
    std::string pageName;
    for (const auto& [page, name] : pageNames)
        if (page == KeyPage(routine.entry))
            pageName = name;
    node["page_name"] = pageName;
    node["kinds"] = EntryKindNames(routine.kinds);
    node["instructions"] = static_cast<uint64_t>(routine.instructions);
    node["blocks"] = static_cast<uint64_t>(routine.blocks.size());
    node["profile"] = ProfileNode(routine.profile, replay.tstates);

    StateNode callers = StateNode::Array();
    StateNode callees = StateNode::Array();
    for (const CallEdge& edge : calls)
    {
        if (edge.callee == index)
        {
            StateNode entry = StateNode::Object();
            entry["routine"] = static_cast<uint64_t>(edge.caller);
            entry["entry"] = Hex4(routines[edge.caller].z80);
            entry["kind"] = CallKindName(edge.kind);
            entry["count"] = edge.count;
            StateNode sites = StateNode::Array();
            for (const uint16_t s : edge.sites)
                sites.push(Hex4(s));
            entry["sites"] = std::move(sites);
            callers.push(std::move(entry));
        }
        if (edge.caller == index)
        {
            StateNode entry = StateNode::Object();
            entry["routine"] = static_cast<uint64_t>(edge.callee);
            entry["entry"] = Hex4(routines[edge.callee].z80);
            entry["kind"] = CallKindName(edge.kind);
            entry["count"] = edge.count;
            callees.push(std::move(entry));
        }
    }
    node["callers"] = std::move(callers);
    node["callees"] = std::move(callees);
    StateNode loopList = StateNode::Array();
    for (const Loop& loop : loops)
        if (loop.routine == index)
            loopList.push(LoopNode(loop));
    node["loops"] = std::move(loopList);
    StateNode fields = StateNode::Array();
    for (const BaseOffsetPattern& p : patterns)
        if (p.routine == index)
            fields.push(PatternNode(p));
    node["fields"] = std::move(fields);
    return node;
}

StateNode L1Result::ToStateNode() const
{
    StateNode doc = StateNode::Object();
    doc["format"] = "unreal-ng analysis L1";
    doc["version"] = 1;

    StateNode& r = doc["replay"];
    r = StateNode::Object();
    r["first_frame"] = replay.firstFrame;
    r["last_frame"] = replay.lastFrame;
    r["frames"] = replay.frames;
    r["instructions"] = replay.instructions;
    r["tstates"] = replay.tstates;
    r["interrupts"] = replay.interrupts;
    r["checked"] = static_cast<uint64_t>(replay.checked);
    r["diverged"] = static_cast<uint64_t>(replay.diverged);
    r["first_divergence"] = replay.firstDivergence;

    StateNode pages = StateNode::Array();
    for (const auto& [page, name] : pageNames)
    {
        StateNode entry = StateNode::Object();
        entry["page"] = static_cast<unsigned>(page);
        entry["name"] = name;
        pages.push(std::move(entry));
    }
    doc["pages"] = std::move(pages);

    StateNode routineList = StateNode::Array();
    for (const Routine& routine : routines)
    {
        StateNode node = StateNode::Object();
        node["entry"] = Hex4(routine.z80);
        node["page"] = static_cast<unsigned>(KeyPage(routine.entry));
        node["kinds"] = EntryKindNames(routine.kinds);
        node["instructions"] = static_cast<uint64_t>(routine.instructions);
        StateNode blockIds = StateNode::Array();
        for (const uint32_t b : routine.blocks)
            blockIds.push(static_cast<uint64_t>(b));
        node["blocks"] = std::move(blockIds);
        node["profile"] = ProfileNode(routine.profile, replay.tstates);
        routineList.push(std::move(node));
    }
    doc["routines"] = std::move(routineList);

    StateNode blockList = StateNode::Array();
    for (const Block& block : blocks)
    {
        StateNode node = StateNode::Object();
        node["start"] = Hex4(block.z80);
        node["end"] = Hex4(block.z80End);
        node["page"] = static_cast<unsigned>(KeyPage(block.start));
        node["routine"] = static_cast<uint64_t>(block.routine);
        node["instructions"] = static_cast<uint64_t>(block.instructions);
        node["executions"] = block.executions;
        StateNode succ = StateNode::Array();
        for (const Edge& e : block.successors)
        {
            StateNode edge = StateNode::Array();
            edge.push(static_cast<uint64_t>(e.to));
            edge.push(EdgeKindName(e.kind));
            edge.push(e.count);
            succ.push(std::move(edge));
        }
        node["successors"] = std::move(succ);
        StateNode targets = StateNode::Array();
        for (const auto& [target, count] : block.indirectTargets)
        {
            StateNode t = StateNode::Array();
            t.push(Hex4(target));
            t.push(count);
            targets.push(std::move(t));
        }
        node["indirect_targets"] = std::move(targets);
        blockList.push(std::move(node));
    }
    doc["blocks"] = std::move(blockList);

    StateNode callList = StateNode::Array();
    for (const CallEdge& edge : calls)
    {
        StateNode node = StateNode::Object();
        node["caller"] = static_cast<uint64_t>(edge.caller);
        node["callee"] = static_cast<uint64_t>(edge.callee);
        node["kind"] = CallKindName(edge.kind);
        node["count"] = edge.count;
        StateNode sites = StateNode::Array();
        for (const uint16_t s : edge.sites)
            sites.push(Hex4(s));
        node["sites"] = std::move(sites);
        callList.push(std::move(node));
    }
    doc["calls"] = std::move(callList);

    StateNode loopList = StateNode::Array();
    for (const Loop& loop : loops)
        loopList.push(LoopNode(loop));
    doc["loops"] = std::move(loopList);

    StateNode patternList = StateNode::Array();
    for (const BaseOffsetPattern& p : patterns)
        patternList.push(PatternNode(p));
    doc["patterns"] = std::move(patternList);

    StateNode& flow = doc["dataflow"];
    flow = StateNode::Object();
    StateNode imm = StateNode::Array();
    for (const ImmediateAddress& a : immediateAddresses)
    {
        StateNode node = StateNode::Object();
        node["at"] = Hex4(a.z80);
        node["page"] = static_cast<unsigned>(KeyPage(a.instruction));
        node["value"] = Hex4(a.value);
        node["used_at"] = Hex4(a.useZ80);
        node["uses"] = a.uses;
        imm.push(std::move(node));
    }
    flow["immediate_addresses"] = std::move(imm);
    StateNode cmp = StateNode::Array();
    for (const ConstantCompare& c : constantCompares)
    {
        StateNode node = StateNode::Object();
        node["at"] = Hex4(c.z80);
        node["page"] = static_cast<unsigned>(KeyPage(c.instruction));
        node["constant"] = Hex2(c.constant);
        node["source"] = SourceKindName(c.source);
        node["address"] = Hex4(c.address);
        node["loader"] = Hex4(c.loaderZ80);
        node["count"] = c.count;
        cmp.push(std::move(node));
    }
    flow["constant_compares"] = std::move(cmp);
    StateNode tables = StateNode::Array();
    for (const PageTable& t : pageTables)
    {
        StateNode node = StateNode::Object();
        node["at"] = Hex4(t.z80);
        node["page"] = static_cast<unsigned>(KeyPage(t.instruction));
        node["table"] = Hex4(t.table);
        node["reader"] = Hex4(t.readerZ80);
        node["count"] = t.count;
        tables.push(std::move(node));
    }
    flow["page_tables"] = std::move(tables);
    StateNode pointerList = StateNode::Array();
    for (const PointerUse& u : pointers)
    {
        StateNode node = StateNode::Object();
        node["form"] = PointerFormName(u.form);
        node["at"] = Hex4(u.z80);
        node["page"] = static_cast<unsigned>(KeyPage(u.origin));
        node["value"] = Hex4(u.value);
        if (u.form == PointerForm::Word)
        {
            node["min"] = Hex4(u.minValue);
            node["max"] = Hex4(u.maxValue);
        }
        node["used_at"] = Hex4(u.useZ80);
        node["uses"] = u.uses;
        node["min_delta"] = static_cast<int64_t>(u.minDelta);
        node["max_delta"] = static_cast<int64_t>(u.maxDelta);
        node["code"] = u.code;
        static const char* const kPairs[] = {"BC", "DE", "HL", "IX", "IY"};
        node["pair"] = kPairs[u.pair < 5 ? u.pair : 0];
        pointerList.push(std::move(node));
    }
    flow["pointers"] = std::move(pointerList);
    StateNode copyList = StateNode::Array();
    for (const BlockCopy& c : copies)
    {
        StateNode node = StateNode::Object();
        node["at"] = Hex4(c.z80);
        node["src"] = Hex4(c.src);
        node["dst"] = Hex4(c.dst);
        node["length"] = static_cast<uint64_t>(c.length);
        node["dir"] = static_cast<int64_t>(c.dir);
        node["count"] = c.count;
        copyList.push(std::move(node));
    }
    flow["copies"] = std::move(copyList);

    StateNode branchList = StateNode::Array();
    for (const BranchCondition& b : branches)
        branchList.push(BranchNode(b));
    doc["branches"] = std::move(branchList);

    StateNode insList = StateNode::Array();
    for (const Instruction& ins : instructions)
    {
        StateNode node = StateNode::Array();
        node.push(Hex4(ins.z80));
        node.push(static_cast<unsigned>(KeyPage(ins.key)));
        std::string bytes;
        for (int i = 0; i < ins.length && i < 4; ++i)
        {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%02X", ins.bytes[i]);
            bytes += hex;
        }
        node.push(bytes);
        node.push(OpClassName(ins.cls));
        node.push(ins.executions);
        node.push(ins.tstates);
        node.push(ins.selfModified);
        insList.push(std::move(node));
    }
    doc["instructions"] = std::move(insList);
    return doc;
}

std::string L1Result::ToJson() const
{
    return StateNodeToJsonText(ToStateNode());
}

std::string L1Result::FactSheet(const Routine& routine, const Disassemble& disassemble, const LabelOf& label) const
{
    constexpr size_t kBudget = 1024;
    constexpr int kMaxLines = 24;
    const uint32_t index = IndexOf(routine);
    const RoutineProfile& p = routine.profile;
    std::string pageName;
    for (const auto& [page, name] : pageNames)
        if (page == KeyPage(routine.entry))
            pageName = name;

    std::string text = "Routine " + Named(routine.z80, label) + " [" + pageName + "] " + EntryKindNames(routine.kinds) +
                       ", " + std::to_string(routine.instructions) + " instr, " +
                       std::to_string(routine.blocks.size()) + " blocks\n";
    text += "  T " + Percent(p.tstates, replay.tstates) + " (incl " + Percent(p.tstatesInclusive, replay.tstates) +
            "), entered " + std::to_string(p.entries) + "x, " + std::to_string(p.activeFrames) + " frames, depth " +
            std::to_string(p.minDepth) + "-" + std::to_string(p.maxDepth) + "\n";
    text += "  exec " + IntervalText(p.executes, 3) + "\n";
    if (!p.reads.empty() || !p.writes.empty())
        text += "  reads " + IntervalText(p.reads, 3) + "; writes " + IntervalText(p.writes, 3) + "\n";
    if (!p.portsIn.empty() || !p.portsOut.empty())
        text += "  ports in " + PortText(p.portsIn, p.portsInMore) + "; out " + PortText(p.portsOut, p.portsOutMore) +
                "\n";
    text += "  regs in " + RegNames(p.liveIn) + "; out " + RegNames(p.liveOut) + "\n";

    std::string callerText;
    int callerCount = 0;
    std::string calleeText;
    int calleeCount = 0;
    for (const CallEdge& edge : calls)
    {
        if (edge.callee == index && edge.caller != index)
        {
            if (callerCount++ < 3)
                callerText += (callerText.empty() ? "" : ", ") + Hex4(edge.sites.empty() ? routines[edge.caller].z80
                                                                                          : edge.sites.front()) +
                              " " + CallKindName(edge.kind) + "x" + std::to_string(edge.count);
        }
        if (edge.caller == index && edge.callee != index)
        {
            if (calleeCount++ < 4)
                calleeText += (calleeText.empty() ? "" : ", ") + Named(routines[edge.callee].z80, label) + " " +
                              CallKindName(edge.kind);
        }
    }
    if (callerCount > 3)
        callerText += " (+" + std::to_string(callerCount - 3) + ")";
    if (calleeCount > 4)
        calleeText += " (+" + std::to_string(calleeCount - 4) + ")";
    if (!callerText.empty())
        text += "  callers " + callerText + "\n";
    if (!calleeText.empty())
        text += "  calls " + calleeText + "\n";

    int loopsShown = 0;
    for (const Loop& loop : loops)
    {
        if (loop.routine != index || loopsShown >= 2)
            continue;
        ++loopsShown;
        std::string line = "  loop " + Hex4(loop.z80) + " trips " + std::to_string(loop.minTrips) +
                           (loop.maxTrips != loop.minTrips ? "-" + std::to_string(loop.maxTrips) : "") + " x" +
                           std::to_string(loop.instances);
        if (loop.delay)
            line += " delay";
        for (size_t i = 0; i < loop.induction.size() && i < 3; ++i)
            line += " " + loop.induction[i].reg + SignedText(loop.induction[i].step);
        for (size_t i = 0; i < loop.strides.size() && i < 3; ++i)
            line += std::string(" ") + (loop.strides[i].write ? "w" : "r") + Hex4(loop.strides[i].z80) +
                    (loop.strides[i].constant ? SignedText(loop.strides[i].stride) : "~");
        text += line + "\n";
    }
    int fieldsShown = 0;
    for (const BaseOffsetPattern& f : patterns)
    {
        if (f.routine != index || fieldsShown >= 2)
            continue;
        ++fieldsShown;
        std::string line = std::string("  fields ") + BaseRegName(f.base);
        for (size_t i = 0; i < f.offsets.size() && i < 8; ++i)
            line += (i ? "," : "") + SignedText(f.offsets[i]);
        if (f.offsets.size() > 8)
            line += ",...";
        line += " step " + std::to_string(f.step) + ", " + std::to_string(f.basesSeen) + " bases";
        if (!f.bases.empty())
            line += " " + Hex4(f.bases.front()) + ".." + Hex4(f.bases.back());
        text += line + "\n";
    }

    // The disassembly excerpt: the routine's instructions in address order, as far as the budget goes
    if (disassemble)
    {
        int lines = 0;
        for (const uint32_t b : routine.blocks)
        {
            const Block& block = blocks[b];
            auto it = std::lower_bound(instructions.begin(), instructions.end(), block.start,
                                       [](const Instruction& ins, PhysKey k) { return ins.key < k; });
            for (uint32_t i = 0; i < block.instructions && it != instructions.end(); ++i, ++it)
            {
                if (lines >= kMaxLines)
                    break;
                const std::string line = "  " + Hex4(it->z80) + "  " + disassemble(*it) + "\n";
                if (text.size() + line.size() > kBudget)
                {
                    lines = kMaxLines;
                    break;
                }
                text += line;
                ++lines;
            }
            if (lines >= kMaxLines)
                break;
        }
    }
    if (text.size() > kBudget)
        text.resize(kBudget);
    return text;
}

}  // namespace analysis
