/// @file ttdcontrollercausechain.cpp
/// @brief TimeTravelController::QueryCauseChain: where a value came from (docs/inprogress/2026-11-02-ttd-cause-chain).
/// A frame's replay with the instruction and bus hooks gives its instructions and their bus events (TraceCauseFrame);
/// the source rules (TTDExplainInstruction, ttdcausechain.cpp) say what each instruction wrote and from what; the
/// walk below follows the inputs back, the last write to a memory byte from the traced frame or FindAccesses. The
/// machine is put back exactly where it stood.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <list>
#include <functional>
#include <map>
#include <memory>
#include <tuple>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/ttd/ttdcausechain.h"
#include "debugger/ttd/ttdportclassifier.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"

namespace ttd
{

namespace
{
constexpr size_t kTraceCacheFrames = 8;

void ReadRegs(const Z80& z, uint8_t* r)
{
    using R = TTDCauseReg;
    r[size_t(R::A)] = z.a;
    r[size_t(R::F)] = z.f;
    r[size_t(R::B)] = z.b;
    r[size_t(R::C)] = z.c;
    r[size_t(R::D)] = z.d;
    r[size_t(R::E)] = z.e;
    r[size_t(R::H)] = z.h;
    r[size_t(R::L)] = z.l;
    r[size_t(R::IXH)] = z.xh;
    r[size_t(R::IXL)] = z.xl;
    r[size_t(R::IYH)] = z.yh;
    r[size_t(R::IYL)] = z.yl;
    r[size_t(R::I)] = z.i;
    r[size_t(R::R)] = static_cast<uint8_t>((z.r_low & 0x7F) | (z.r_hi & 0x80));
    r[size_t(R::A2)] = static_cast<uint8_t>(z.alt.af >> 8);
    r[size_t(R::F2)] = static_cast<uint8_t>(z.alt.af);
    r[size_t(R::B2)] = static_cast<uint8_t>(z.alt.bc >> 8);
    r[size_t(R::C2)] = static_cast<uint8_t>(z.alt.bc);
    r[size_t(R::D2)] = static_cast<uint8_t>(z.alt.de >> 8);
    r[size_t(R::E2)] = static_cast<uint8_t>(z.alt.de);
    r[size_t(R::H2)] = static_cast<uint8_t>(z.alt.hl >> 8);
    r[size_t(R::L2)] = static_cast<uint8_t>(z.alt.hl);
    r[size_t(R::SPH)] = z.sph;
    r[size_t(R::SPL)] = z.spl;
}

std::string Hex2(uint8_t v)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02X", v);
    return buf;
}

std::string Hex4(uint16_t v)
{
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%04X", v);
    return buf;
}

std::string FrameText(const TTDTimePoint& t)
{
    return "f" + std::to_string(t.frame) + "+" + std::to_string(t.tInFrame);
}
}  // namespace

/// A frame's instructions and bus events, by a replay of the frame from its checkpoint with the two hooks. The
/// caller saved the live machine; the replay leaves the machine at the frame's end (or at @p endT)
std::shared_ptr<TTDFrameTrace> TimeTravelController::TraceCauseFrame(size_t cpIndex, uint32_t endT)
{
    auto trace = std::make_shared<TTDFrameTrace>();
    const TTDCheckpoint& cp = _timeline[cpIndex];
    trace->frame = cp.time.frame;
    Z80* z80 = _context->pCore ? _context->pCore->GetZ80() : nullptr;
    Memory* memory = _context->pMemory;
    if (!z80 || !memory)
        return trace;
    const EmulatorState& st = _context->emulatorState;

    RestoreCheckpointForReplay(cp);
    uint64_t interrupts = z80->interruptsAccepted;
    bool haltIdle = false;   // the open record is a HALT whose idle fetches follow
    std::vector<TTDTraceEvent> early;   // bus events before the frame's first instruction start

    // An interrupt accepted after the open record: its pushes (the last two writes) and the IM 2 vector reads after
    // them are a pseudo record of their own, before the instruction that starts now
    auto splitInterrupt = [&](const Z80& cpu) {
        if (trace->insns.empty())
            return;
        TTDTraceInsn& open = trace->insns.back();
        uint32_t first = open.firstEvent + open.eventCount;
        uint32_t writes = 0;
        for (uint32_t k = open.firstEvent + open.eventCount; k > open.firstEvent && writes < 2; --k)
            if (trace->events[k - 1].type == 'W')
            {
                first = k - 1;
                ++writes;
            }
        if (writes < 2)
            return;   // an NMI-less model or a push not seen: leave the record as it is
        TTDTraceInsn in;
        in.interrupt = true;
        in.tInFrame = trace->events[first].tInFrame;
        // The interrupted PC is the pushed return address; the registers are the ones before the push
        uint16_t pushed = 0;
        for (uint32_t k = first; k < first + 2; ++k)
        {
            const TTDTraceEvent& e = trace->events[k];
            pushed = static_cast<uint16_t>(pushed | (e.address == static_cast<uint16_t>(cpu.sp + 1) ? e.value << 8 : e.value));
        }
        in.pc = pushed;
        in.codePage = memory->TtdPageOfBank(static_cast<uint8_t>(in.pc >> 14));
        ReadRegs(cpu, in.regs);
        const uint16_t sp = static_cast<uint16_t>(cpu.sp + 2);
        in.regs[size_t(TTDCauseReg::SPH)] = static_cast<uint8_t>(sp >> 8);
        in.regs[size_t(TTDCauseReg::SPL)] = static_cast<uint8_t>(sp);
        for (uint8_t b = 0; b < 4; ++b)
            in.banks[b] = memory->TtdPageOfBank(b);
        in.firstEvent = first;
        in.eventCount = open.firstEvent + open.eventCount - first;
        open.eventCount = first - open.firstEvent;
        trace->insns.push_back(in);
    };

    z80->m1TraceHook = [&](uint16_t pc) {
        Z80& cpu = *z80;
        if (cpu.interruptsAccepted != interrupts)
        {
            interrupts = cpu.interruptsAccepted;
            if (trace->insns.empty())
            {
                // Accepted at the frame's start: the events before the first instruction, as an open record
                TTDTraceInsn open;
                open.firstEvent = 0;
                open.eventCount = static_cast<uint32_t>(early.size());
                trace->events = std::move(early);
                trace->insns.push_back(open);
                splitInterrupt(cpu);
                trace->insns.erase(trace->insns.begin());
            }
            else
                splitInterrupt(cpu);
            haltIdle = false;
        }
        // A halted CPU fetches again and again: one record for the HALT and its idle fetches
        if (cpu.halted && haltIdle)
            return;
        TTDTraceInsn in;
        in.tInFrame = st.TtdTInFrame(static_cast<uint32_t>(cpu.t));
        in.pc = pc;
        in.codePage = memory->TtdPageOfBank(static_cast<uint8_t>(pc >> 14));
        for (uint8_t k = 0; k < 4; ++k)
            in.bytes[k] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(pc + k));
        ReadRegs(cpu, in.regs);
        for (uint8_t b = 0; b < 4; ++b)
            in.banks[b] = memory->TtdPageOfBank(b);
        in.firstEvent = static_cast<uint32_t>(trace->events.size());
        trace->insns.push_back(in);
        haltIdle = in.bytes[0] == 0x76;
    };
    z80->busTraceHook = [&](char type, uint16_t address, uint8_t value) {
        if (type == 'N')
            return;   // internal cycles
        TTDTraceEvent e;
        e.type = type;
        e.value = value;
        e.address = address;
        e.page = type == 'R' || type == 'W' ? memory->TtdPageOfBank(static_cast<uint8_t>(address >> 14)) : kPhysPageNone;
        e.tInFrame = st.TtdTInFrame(static_cast<uint32_t>(z80->t));
        if (trace->insns.empty())
        {
            early.push_back(e);   // the frame before's last instruction, or an interrupt accepted at the start
            return;
        }
        trace->events.push_back(e);
        ++trace->insns.back().eventCount;
    };

    {
        ReplayModeScope replay(*this);
        ReplayWithinFrame(cp.time.frame, endT);
        replay.Exit();
    }
    if (z80->interruptsAccepted != interrupts)
        splitInterrupt(*z80);
    ReadRegs(*z80, trace->finalRegs);
    z80->m1TraceHook = nullptr;
    z80->busTraceHook = nullptr;
    return trace;
}

TTDCauseResult TimeTravelController::QueryCauseChain(const TTDCauseQuery& query)
{
    const auto started = std::chrono::steady_clock::now();
    // Replays checkpoints and puts the machine back: an operation that changes it
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDCauseResult result;
    auto finish = [&]() -> TTDCauseResult& {
        result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return result;
    };

    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    Memory* memory = _context ? _context->pMemory : nullptr;
    Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    if (_timeline.empty() || !memory || !z80 || !_context->pEmulator)
    {
        result.error = "no recorded history";
        return finish();
    }

    // The target
    std::vector<TTDCauseReg> regs;
    if (!query.memory && !TTDCauseRegsByName(query.reg, regs))
    {
        result.error = "unknown register '" + query.reg + "'";
        return finish();
    }
    const TTDTimePoint sessionStart = _timeline.front().time;
    const TTDTimePoint at = query.hasTime ? query.at : CurrentPosition();
    if (at < sessionStart || SessionEndPosition() < at)
    {
        result.error = "the moment " + FrameText(at) + " is outside the recorded history (" + FrameText(sessionStart) +
                       " .. " + FrameText(SessionEndPosition()) + ")";
        return finish();
    }
    result.at = at;
    const int64_t atIndex = TimelineIndexAtOrBefore(at);
    if (atIndex < 0)
    {
        result.error = "no checkpoint at or before " + FrameText(at);
        return finish();
    }
    const uint32_t depthLimit = std::min(query.depth, TTDCauseQuery::kMaxDepth);
    const uint32_t linkLimit = std::clamp(query.maxLinks, 1u, TTDCauseQuery::kMaxLinks);
    const uint32_t frameLimit = std::clamp(query.maxFrames, 1u, TTDCauseQuery::kMaxFrames);

    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot live;   // not _liveSnapshot: FindAccesses saves the machine there itself
    SaveLiveState(live);

    // A memory target: the page mapped at the moment (unless one was given) and the byte there
    PhysPage targetPage = query.page;
    uint8_t targetValue = 0;
    if (query.memory)
    {
        bool there = CurrentPosition() == at;
        if (!there)
        {
            ReplayModeScope seek(*this);
            TTDSeekResult reached;
            there = SeekToInternal(at, &reached) && reached.reached && reached.arrivedAt == at;
            seek.Exit();
        }
        if (!there)
        {
            ReplayModeScope restore(*this);
            RestoreLiveState(live);
            restore.Exit();
            SetState(stateBefore);
            result.error = "the replay could not reach " + FrameText(at);
            return finish();
        }
        if (!query.hasPage)
            targetPage = memory->TtdPageOfBank(static_cast<uint8_t>(query.address >> 14));
        targetValue = memory->DirectReadFromZ80Memory(query.address);
    }

    // Traced frames by checkpoint index, the newest used last; for every frame traced once, the registers it wrote
    // (a register search skips the frames that never wrote the byte without tracing them again)
    std::list<std::pair<size_t, std::shared_ptr<TTDFrameTrace>>> cache;
    std::map<size_t, uint32_t> frameRegWrites;
    auto traceOf = [&](size_t cpIndex) -> std::shared_ptr<TTDFrameTrace> {
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->first == cpIndex)
            {
                cache.splice(cache.end(), cache, it);
                return cache.back().second;
            }
        const TTDCheckpoint& cp = _timeline[cpIndex];
        const TTDTimePoint end = SessionEndPosition();
        uint32_t endT = cp.time.frame == end.frame ? end.tInFrame : FrameSpan();
        if (const std::optional<TTDExternalEvent> barrier = FirstBarrierBetween(cp.time, TTDTimePoint{cp.time.frame, endT}))
            endT = barrier->time.frame == cp.time.frame ? barrier->time.tInFrame : 0;
        auto trace = TraceCauseFrame(cpIndex, endT);
        ++result.framesTraced;
        uint32_t written = 0;
        for (size_t k = 0; k < trace->insns.size(); ++k)
            written |= TTDCauseWrittenRegs(*trace, k);
        frameRegWrites[cpIndex] = written;
        cache.emplace_back(cpIndex, trace);
        if (cache.size() > kTraceCacheFrames)
            cache.pop_front();
        return trace;
    };

    std::vector<TTDTimelineDevice> devices;
    std::unique_ptr<PortClassifier> ports;
    auto portText = [&](uint16_t port) {
        if (!ports)
            ports = std::make_unique<PortClassifier>(_context->pPortDecoder, devices);
        const int16_t d = ports->DeviceOf(port);
        std::string text = "port " + Hex4(port);
        if (d >= 0 && !devices[size_t(d)].name.empty())
            text += " (" + devices[size_t(d)].name + ")";
        return text;
    };
    Z80Disassembler* disassembler = _context->pDebugManager ? _context->pDebugManager->GetDisassembler().get() : nullptr;
    auto insnText = [&](const TTDTraceInsn& in) -> std::string {
        if (in.interrupt)
            return "interrupt";
        if (!disassembler)
            return "";
        uint8_t len = 0;
        return disassembler->disassembleSingleCommand(std::vector<uint8_t>(in.bytes, in.bytes + 4), in.pc, &len);
    };

    // A place in the history: an instruction of a traced frame (the producer's search goes back from before it)
    struct Place
    {
        size_t cpIndex = 0;
        size_t insn = 0;   ///< instructions [0, insn) of the frame are before
    };
    // The producer of a name, found back from a place
    struct Producer
    {
        bool found = false;
        std::string stop;         ///< not found: why (session_start, barrier, max_frames); empty: never written
        std::shared_ptr<TTDFrameTrace> trace;
        size_t cpIndex = 0;
        size_t insn = 0;
        TTDCauseOutput output;
    };
    auto outputOf = [&](const TTDFrameTrace& trace, size_t insn, const TTDCauseName& name, TTDCauseOutput& out) {
        for (TTDCauseOutput& o : TTDExplainInstruction(trace, insn, query.followFlags))
            if (o.name == name)
            {
                out = std::move(o);
                return true;
            }
        return false;
    };
    auto barrierBefore = [&](size_t cpIndex) {
        // A marker between the frame before and this one: the state jumped, the producer is not in the replay
        return cpIndex > 0 && FirstBarrierBetween(_timeline[cpIndex - 1].time, _timeline[cpIndex].time).has_value();
    };

    auto findProducer = [&](const TTDCauseName& name, Place from) {
        Producer p;
        if (name.memory && name.page == kPhysPageNone)
            return p;   // ROM: nothing writes it
        size_t cpIndex = from.cpIndex;
        size_t limit = from.insn;
        // Frames searched; the first one only when instructions of it come before the place
        for (uint32_t frames = limit > 0 ? 0 : UINT32_MAX;; ++frames)
        {
            const auto known = frameRegWrites.find(cpIndex);
            const bool skip = limit == 0 || (!name.memory && limit == SIZE_MAX && known != frameRegWrites.end() &&
                                             (known->second & (1u << static_cast<unsigned>(name.reg))) == 0);
            std::shared_ptr<TTDFrameTrace> trace = skip ? nullptr : traceOf(cpIndex);
            for (size_t k = trace ? std::min(limit, trace->insns.size()) : 0; k-- > 0;)
                if (TTDCauseWrites(*trace, k, name) && outputOf(*trace, k, name, p.output))
                {
                    p.found = true;
                    p.trace = trace;
                    p.cpIndex = cpIndex;
                    p.insn = k;
                    return p;
                }
            if (cpIndex == 0)
            {
                p.stop = name.memory ? "" : "session_start";
                return p;
            }
            if (barrierBefore(cpIndex))
            {
                p.stop = "barrier";
                return p;
            }
            if (name.memory)
                break;   // the older history: FindAccesses below
            if (frames != UINT32_MAX && frames + 1 >= frameLimit)
            {
                p.stop = "max_frames";
                return p;
            }
            --cpIndex;
            limit = SIZE_MAX;
        }

        // A memory byte written before the frame: the write journal or the pruned replay of FindAccesses
        TTDSearchQuery q;
        q.addrFrom = q.addrTo = name.address;
        q.access = TTDAccessType::Write;
        q.hasPhysPageFilter = name.page != kPhysPageNone;
        q.physPage = name.page;
        const uint64_t frameStart = GlobalT(_timeline[cpIndex].time);
        if (frameStart == 0)
            return p;
        q.beforeGlobalT = frameStart - 1;
        const TTDFindResult found = FindAccesses(q, TTDFindOptions{});
        if (found.hits.empty())
        {
            if (found.blockingMarker.reason[0] != '\0')
                p.stop = "barrier";
            return p;   // never written in the history
        }
        const TTDSearchResult& hit = found.hits.front().access;
        const int64_t hitIndex = TimelineIndexAtOrBefore(hit.time);
        if (hitIndex < 0)
            return p;
        std::shared_ptr<TTDFrameTrace> trace = traceOf(size_t(hitIndex));
        // The instruction holding that write (its time first, else the last write to the byte)
        size_t best = SIZE_MAX;
        for (size_t k = trace->insns.size(); k-- > 0 && best == SIZE_MAX;)
        {
            const TTDTraceInsn& in = trace->insns[k];
            for (uint32_t e = in.firstEvent; e < in.firstEvent + in.eventCount; ++e)
            {
                const TTDTraceEvent& ev = trace->events[e];
                if (ev.type == 'W' && ev.address == name.address && ev.page == name.page && ev.tInFrame == hit.time.tInFrame)
                    best = k;
            }
        }
        for (size_t k = trace->insns.size(); k-- > 0 && best == SIZE_MAX;)
            if (TTDCauseWrites(*trace, k, name))
                best = k;
        if (best != SIZE_MAX && outputOf(*trace, best, name, p.output))
        {
            p.found = true;
            p.trace = trace;
            p.cpIndex = size_t(hitIndex);
            p.insn = best;
        }
        return p;
    };

    // The walk: depth first, each link's inputs after it
    std::map<std::tuple<uint64_t, size_t, bool, uint16_t, PhysPage, uint8_t>, size_t> seen;
    std::function<void(const TTDCauseName&, uint8_t, Place, int32_t, uint32_t)> walk;
    walk = [&](const TTDCauseName& name, uint8_t value, Place from, int32_t parent, uint32_t depth) {
        const size_t index = result.links.size();
        result.links.emplace_back();
        {
            TTDCauseLink& l = result.links.back();
            l.parent = parent;
            l.depth = depth;
            l.name = name;
            l.value = value;
        }
        Producer p = findProducer(name, from);
        TTDCauseLink& link = result.links[index];
        if (!p.found)
        {
            if (name.memory && name.page == kPhysPageNone)
                link.kind = TTDCauseKind::Rom;
            else if (p.stop.empty() || p.stop == "session_start")
            {
                link.kind = TTDCauseKind::Initial;
                link.detail = "held since the session start " + FrameText(sessionStart);
            }
            else
            {
                link.kind = TTDCauseKind::Stop;
                link.stop = p.stop;
            }
            return;
        }

        const TTDTraceInsn& in = p.trace->insns[p.insn];
        link.hasProducer = true;
        link.time = TTDTimePoint{p.trace->frame, in.tInFrame};
        link.pc = in.pc;
        link.codePage = in.codePage;
        link.text = insnText(in);
        link.kind = p.output.kind;
        for (const TTDCauseInput& i : p.output.inputs)
            link.inputs.push_back(TTDCauseNameText(i.name) + "=" + Hex2(i.value));

        // CC-5: the value read is the one the producer wrote, else something else wrote the byte in between
        if (p.output.value != value)
        {
            link.kind = TTDCauseKind::Device;
            link.detail = "read " + Hex2(value) + ", the CPU wrote " + Hex2(p.output.value) + " at " +
                          FrameText(link.time) + ": written since by something other than the CPU";
            return;
        }
        switch (p.output.kind)
        {
            case TTDCauseKind::Port:
                link.detail = portText(p.output.port) + " read " + Hex2(value);
                break;
            case TTDCauseKind::Return:
                link.detail = "return address " + Hex4(p.output.leafValue);
                break;
            case TTDCauseKind::Interrupt:
                link.detail = "interrupted PC " + Hex4(p.output.leafValue);
                break;
            case TTDCauseKind::Constant:
                link.detail = "immediate";
                break;
            case TTDCauseKind::Refresh:
                link.detail = "the refresh counter";
                break;
            default:
                break;
        }

        // A loop's self update (LDIR's HL, INC HL in a loop body, DJNZ's B): the run of the same instruction is one
        // link, its inputs the first iteration's
        auto selfUpdate = [&](const Producer& q) {
            if (q.output.kind != TTDCauseKind::Computed)
                return false;
            for (const TTDCauseInput& i : q.output.inputs)
                if (i.name == name)
                    return true;
            return false;
        };
        Producer first = p;
        uint32_t repeats = 1;
        if (!name.memory && selfUpdate(p))
            for (;;)
            {
                Producer q = findProducer(name, Place{first.cpIndex, first.insn});
                if (!q.found || p.trace->frame - q.trace->frame >= frameLimit)
                    break;   // the run goes on beyond the frames asked for: its earliest iteration within them
                const TTDTraceInsn& qi = q.trace->insns[q.insn];
                const TTDTraceInsn& fi = first.trace->insns[first.insn];
                if (qi.interrupt || qi.pc != fi.pc || qi.codePage != fi.codePage || !selfUpdate(q))
                    break;
                first = std::move(q);
                ++repeats;
            }
        if (repeats > 1)
        {
            const TTDTraceInsn& fi = first.trace->insns[first.insn];
            link.detail = "repeated " + std::to_string(repeats) + " times, the first at " +
                          FrameText(TTDTimePoint{first.trace->frame, fi.tInFrame});
            link.inputs.clear();
            for (const TTDCauseInput& i : first.output.inputs)
                link.inputs.push_back(TTDCauseNameText(i.name) + "=" + Hex2(i.value));
        }

        const auto key = std::make_tuple(p.trace->frame, p.insn, name.memory, name.address, name.page,
                                         static_cast<uint8_t>(name.reg));
        if (auto it = seen.find(key); it != seen.end())
        {
            link.stop = "seen (link " + std::to_string(it->second) + ")";
            return;
        }
        seen.emplace(key, index);

        std::vector<TTDCauseInput> follow;
        for (const TTDCauseInput& i : first.output.inputs)
            if (i.followed)
                follow.push_back(i);
        if (follow.empty())
            return;
        if (depth >= depthLimit)
        {
            link.stop = "depth";
            return;
        }
        const Place producerPlace{first.cpIndex, first.insn};
        for (const TTDCauseInput& i : follow)
        {
            if (result.links.size() >= linkLimit)
            {
                result.links[index].stop = "max_links";
                return;
            }
            walk(i.name, i.value, producerPlace, static_cast<int32_t>(index), depth + 1);
        }
    };

    // The target's place: the instructions of its frame that started before the moment
    std::shared_ptr<TTDFrameTrace> atTrace = traceOf(size_t(atIndex));
    size_t atInsn = 0;
    while (atInsn < atTrace->insns.size() && (at.frame > atTrace->frame || atTrace->insns[atInsn].tInFrame < at.tInFrame))
        ++atInsn;
    const Place atPlace{size_t(atIndex), atInsn};
    const uint8_t* atRegs = atInsn < atTrace->insns.size() ? atTrace->insns[atInsn].regs : atTrace->finalRegs;

    if (query.memory)
        walk(TTDCauseName::Mem(query.address, targetPage), targetValue, atPlace, -1, 0);
    else
    {
        for (TTDCauseReg r : regs)
            walk(TTDCauseName::Reg(r), atRegs[size_t(r)], atPlace, -1, 0);
    }

    ReplayModeScope restore(*this);
    RestoreLiveState(live);
    restore.Exit();
    SetState(stateBefore);
    return finish();
}

}  // namespace ttd
