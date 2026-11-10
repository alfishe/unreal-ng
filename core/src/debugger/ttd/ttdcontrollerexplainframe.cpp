/// @file ttdcontrollerexplainframe.cpp
/// @brief TimeTravelController::QueryExplainFrame (docs/inprogress/2026-11-02-ttd-explain-frame/design.md): what one
/// frame did, from one replay of it with the instruction and bus hooks (the cause chain's frame trace), the RAM at the
/// frame's checkpoint and the journals (input, external events, bookmarks).

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <tuple>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/labels/labelmanager.h"
#include "debugger/ttd/ttdcausechain.h"
#include "debugger/ttd/ttdexplainframe.h"
#include "debugger/ttd/ttdportclassifier.h"
#include "debugger/ttd/ttdportsearch.h"
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
    {kExplainTime, "time"},       {kExplainRoutines, "routines"}, {kExplainInterrupt, "interrupt"},
    {kExplainPaging, "paging"},   {kExplainPorts, "ports"},       {kExplainMemory, "memory"},
    {kExplainScreen, "screen"},   {kExplainInput, "input"},       {kExplainMarkers, "markers"},
};

std::string Hex(uint32_t v, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%0*X", digits, v);
    return buf;
}

std::string PageText(PhysPage page)
{
    return page == kPhysPageNone ? std::string("ROM") : "RAM " + std::to_string(page);
}

uint16_t Sp(const uint8_t* regs)
{
    return static_cast<uint16_t>(regs[size_t(TTDCauseReg::SPH)] << 8 | regs[size_t(TTDCauseReg::SPL)]);
}

bool IsCall(const uint8_t* b)
{
    return b[0] == 0xCD || (b[0] & 0xC7) == 0xC4;   // CALL nn, CALL cc,nn
}
bool IsRst(const uint8_t* b)
{
    return (b[0] & 0xC7) == 0xC7;
}
bool IsRet(const uint8_t* b)
{
    return b[0] == 0xC9 || (b[0] & 0xC7) == 0xC0 || (b[0] == 0xED && (b[1] & 0xC7) == 0x45);   // RET, RET cc, RETI/RETN
}

/// "45 %"; under 10 % with one decimal ("0.2 %"), so a short routine does not read as nothing
std::string Percent(uint64_t part, uint64_t whole)
{
    if (whole == 0)
        return "0 %";
    const uint64_t tenths = (part * 1000 + whole / 2) / whole;
    if (tenths >= 100)
        return std::to_string((tenths + 5) / 10) + " %";
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " %";
}

}  // namespace

bool TTDExplainSectionByName(const std::string& name, uint32_t& out)
{
    if (name == "all")
    {
        out = kExplainAll;
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

const char* TTDExplainSectionName(uint32_t section)
{
    for (const SectionName& s : kSections)
        if (section == s.bit)
            return s.name;
    return "?";
}

const char* TTDExplainRegionName(TTDExplainRegion region)
{
    switch (region)
    {
        case TTDExplainRegion::ScreenPixels: return "screen pixels";
        case TTDExplainRegion::ScreenAttributes: return "screen attributes";
        case TTDExplainRegion::Stack: return "stack";
        case TTDExplainRegion::Ram: break;
    }
    return "ram";
}

std::string TTDExplainFrameText(const TTDExplainFrameQuery& query, const TTDExplainFrameResult& r)
{
    if (!r.error.empty())
        return "explain frame: " + r.error + "\n";
    const uint32_t s = query.sections;
    std::string text = "frame " + std::to_string(r.frame) + " (" + std::to_string(r.frameT) + " T, " +
                       std::to_string(r.instructions) + " instructions)";
    if (s & kExplainTime)
    {
        const uint64_t all = r.isrT + r.haltT + r.codeT;
        text += ": ISR " + Percent(r.isrT, all) + " (" + std::to_string(r.isrT) + " T), HALT " + Percent(r.haltT, all) +
                ", code " + Percent(r.codeT, all);
    }
    text += "\n";
    if (r.partial)
        text += "partial: the session ends at +" + std::to_string(r.endT) + "\n";
    if (!r.barrier.empty())
        text += "barrier: " + r.barrier + " (explained up to it)\n";

    if ((s & kExplainRoutines) && !r.routines.empty())
    {
        text += "routines by time:\n";
        for (const TTDExplainRoutine& x : r.routines)
        {
            std::string name = Hex(x.pc, 4);
            if (!x.label.empty())
                name += " " + x.label;
            if (x.isr)
                name += " (isr)";
            if (x.start)
                name = "at " + name + " (running at the frame start)";
            char buf[160];
            std::snprintf(buf, sizeof(buf), "  %-24s %ux  %llu T %s  own %llu  %llu insns", name.c_str(), x.calls,
                          static_cast<unsigned long long>(x.t), Percent(x.t, r.endT).c_str(),
                          static_cast<unsigned long long>(x.ownT), static_cast<unsigned long long>(x.insns));
            text += buf;
            if (x.hasCaller)
                text += "  from " + Hex(x.caller, 4);
            text += "\n";
        }
        if (r.moreRoutines)
            text += "  +" + std::to_string(r.moreRoutines) + " more\n";
    }
    if (s & kExplainInterrupt)
    {
        if (r.interrupts.empty())
            text += r.interruptsEnabled ? "interrupt: none in the frame\n" : "interrupt: none (DI)\n";
        for (const TTDExplainInterrupt& i : r.interrupts)
        {
            text += std::string("interrupt: ") + (i.beforeFrame ? "at the frame before's end" : "+" + std::to_string(i.tInFrame)) +
                    " IM " + std::to_string(i.mode);
            if (i.hasVector)
                text += " via (" + Hex(i.vectorAddress, 4) + ")";
            text += " -> " + Hex(i.entry, 4) + ", " + std::to_string(i.t) + " T";
            text += i.returned ? ", back to " + Hex(i.returnedTo, 4) : ", still running at the end";
            text += "\n";
        }
    }
    if ((s & kExplainPaging) && !r.paging.empty())
    {
        text += "paging:";
        for (const TTDExplainPaging& p : r.paging)
            text += " +" + std::to_string(p.tInFrame) + " " + Hex(p.pc, 4) + " bank " + std::to_string(p.bank) + " " +
                    PageText(p.was) + " -> " + PageText(p.page) + ";";
        text.back() = '\n';
        if (r.morePaging)
            text += "  +" + std::to_string(r.morePaging) + " more\n";
    }
    if ((s & kExplainPorts) && !r.ports.empty())
    {
        text += "ports:\n";
        for (const TTDExplainPort& p : r.ports)
        {
            text += std::string("  ") + (p.out ? "out " : "in ") + (p.device.empty() ? Hex(p.port, 4) : p.device);
            if (!p.device.empty())
                text += " " + Hex(p.port, 4) + (p.manyPorts ? ".." : "");
            text += " x" + std::to_string(p.count) + " (";
            for (size_t k = 0; k < p.values.size(); ++k)
                text += (k ? " " : "") + Hex(p.values[k], 2);
            text += p.moreValues ? " ...)" : ")";
            if (!p.keys.empty())
            {
                text += " keys down:";
                for (const std::string& key : p.keys)
                    text += " " + key;
            }
            text += " from +" + std::to_string(p.tInFrame) + " " + Hex(p.pc, 4) + "\n";
        }
        if (r.morePorts)
            text += "  +" + std::to_string(r.morePorts) + " more\n";
    }
    if ((s & kExplainMemory) && !r.memory.empty())
    {
        text += "memory:\n";
        for (const TTDExplainRange& m : r.memory)
        {
            std::string where = m.region == TTDExplainRegion::Ram ? PageText(m.page) : TTDExplainRegionName(m.region);
            if (m.hasAddress)
                where += " " + Hex(m.address, 4) + (m.length > 1 ? "-" + Hex(static_cast<uint16_t>(m.address + m.length - 1), 4) : "");
            else
                where += " +" + Hex(m.offset, 4);
            text += "  " + where;
            if (m.hasValues)
                text += "  " + Hex(m.before, m.length > 1 ? 4 : 2) + " -> " + Hex(m.after, m.length > 1 ? 4 : 2);
            text += "  " + std::to_string(m.changed) + " changed, " + std::to_string(m.written) + " written";
            if (m.device)
                text += "  by a device (no CPU write)";
            for (size_t k = 0; k < m.writers.size(); ++k)
                text += (k ? ", " : "  by ") + Hex(m.writers[k].pc, 4) + " " + m.writers[k].text + " x" +
                        std::to_string(m.writers[k].count);
            if (m.moreWriters)
                text += ", ...";
            text += "\n";
        }
        if (r.moreMemory)
            text += "  +" + std::to_string(r.moreMemory) + " more ranges\n";
    }
    if (s & kExplainScreen)
    {
        std::string line;
        if (r.hasScreen)
            line = Percent(r.screenChanged, r.screenBytes) + " changed (" + std::to_string(r.screenChanged) + " of " +
                   std::to_string(r.screenBytes) + " bytes; pixels " + std::to_string(r.pixelsChanged) +
                   ", attributes " + std::to_string(r.attributesChanged) + ")" +
                   (r.screenSwitched ? "; the shown screen switched" : "");
        if (!r.border.empty())
        {
            line += line.empty() ? "border " : "; border ";
            for (size_t k = 0; k < r.border.size(); ++k)
            {
                line += (k ? " -> " : "") + std::to_string(r.border[k].colour);
                if (r.border[k].count > 1)
                    line += " x" + std::to_string(r.border[k].count);
            }
        }
        if (!line.empty())
            text += "screen: " + line + "\n";
    }
    if ((s & kExplainInput) && (!r.keysHeld.empty() || !r.keys.empty()))
    {
        text += "input:";
        if (!r.keysHeld.empty())
        {
            text += " held";
            for (const std::string& k : r.keysHeld)
                text += " " + k;
            text += ";";
        }
        for (const TTDExplainKey& k : r.keys)
            text += " +" + std::to_string(k.tInFrame) + " " + k.key + (k.pressed ? " pressed;" : " released;");
        text.back() = '\n';
    }
    if ((s & kExplainMarkers) && !r.markers.empty())
    {
        text += "markers:";
        for (const std::string& m : r.markers)
            text += " " + m + ";";
        text.back() = '\n';
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%llu frame replayed, %.1f ms\n", static_cast<unsigned long long>(r.framesReplayed), r.ms);
    return text + buf;
}

namespace
{

/// A call frame still open at a frame's end
struct OpenBefore
{
    uint16_t entry = 0;
    PhysPage page = kPhysPageNone;
    uint16_t entrySP = 0;          ///< the SP after the push (the return address there)
    uint16_t caller = 0;           ///< the CALL's PC; an interrupt's: the interrupted PC
    bool interrupt = false;
    uint8_t mode = 0;
    bool hasVector = false;
    uint16_t vectorAddress = 0;
};

/// The call frames open at the trace's end from the outermost interrupt up (empty when no interrupt is open), by the
/// same push / pop rule as the routines walk
std::vector<OpenBefore> OpenAtTheEnd(const TTDFrameTrace& trace, uint8_t im)
{
    std::vector<OpenBefore> open;
    const std::vector<TTDTraceInsn>& insns = trace.insns;
    for (size_t k = 0; k < insns.size(); ++k)
    {
        const TTDTraceInsn& in = insns[k];
        const bool hasNext = k + 1 < insns.size();
        if (in.interrupt)
        {
            OpenBefore o;
            o.interrupt = true;
            o.mode = im;
            o.caller = in.pc;
            o.entrySP = static_cast<uint16_t>(Sp(in.regs) - 2);
            if (hasNext)
            {
                o.entry = insns[k + 1].pc;
                o.page = insns[k + 1].codePage;
            }
            if (im == 2)
                for (uint32_t e = in.firstEvent; e < in.firstEvent + in.eventCount; ++e)
                    if (trace.events[e].type == 'R')
                    {
                        o.hasVector = true;
                        o.vectorAddress = trace.events[e].address;
                        break;
                    }
            open.push_back(o);
            continue;
        }
        if (in.bytes[0] == 0xED && (in.bytes[1] == 0x46 || in.bytes[1] == 0x66))
            im = 0;
        else if (in.bytes[0] == 0xED && (in.bytes[1] == 0x56 || in.bytes[1] == 0x76))
            im = 1;
        else if (in.bytes[0] == 0xED && (in.bytes[1] == 0x5E || in.bytes[1] == 0x7E))
            im = 2;
        const uint16_t spBefore = Sp(in.regs);
        const uint16_t spAfter = Sp(trace.RegsAfter(k));
        if (hasNext && (IsCall(in.bytes) || IsRst(in.bytes)) && spAfter == static_cast<uint16_t>(spBefore - 2))
        {
            OpenBefore o;
            o.entry = insns[k + 1].pc;
            o.page = insns[k + 1].codePage;
            o.entrySP = spAfter;
            o.caller = in.pc;
            open.push_back(o);
        }
        else if (IsRet(in.bytes) && spAfter == static_cast<uint16_t>(spBefore + 2))
            while (!open.empty() && open.back().entrySP <= spBefore)
                open.pop_back();
    }
    const auto outer = std::find_if(open.begin(), open.end(), [](const OpenBefore& o) { return o.interrupt; });
    open.erase(open.begin(), outer);
    return open;
}

}  // namespace

TTDExplainFrameResult TimeTravelController::QueryExplainFrame(const TTDExplainFrameQuery& query)
{
    const auto started = std::chrono::steady_clock::now();
    // Replays the frame and puts the machine back: an operation that changes it
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDExplainFrameResult result;
    auto finish = [&]() -> TTDExplainFrameResult& {
        result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return result;
    };

    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    Memory* memory = _context ? _context->pMemory : nullptr;
    Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const LineageViewGuard lineageView{*this};   // with branches the frame is read on the lineage line holding it
    const LineageEngine engine{*this};            // checkpoint indices below: the lineage's (engine)
    const size_t firstCp = engine.FirstCheckpoint();
    const size_t cpCount = engine.CheckpointCount();
    if (_line->timeline.empty() || !memory || !z80 || !_context->pEmulator || cpCount <= firstCp)
    {
        result.error = "no recorded history";
        return finish();
    }
    const uint32_t limit = std::clamp(query.limit, 1u, TTDExplainFrameQuery::kMaxLimit);
    const uint32_t sections = query.sections ? query.sections : kExplainAll;

    // The frame: from its checkpoint to its end, the session end or a marker inside it
    const uint64_t heldFrom = engine.Checkpoint(firstCp)->position.frame;
    const TTDTimePoint sessionEnd = SessionEndPosition();
    const uint64_t frame = query.hasFrame ? query.frame : CurrentPosition().frame;
    const int64_t ciSigned = ViewLineageIndexAtOrBefore({frame, 0});
    const int64_t eiSigned = engine.CheckpointIndexOf({0, frame, 0});
    const TTDCheckpoint* frameCp = ciSigned < 0 ? nullptr : ViewLineageCheckpoint(static_cast<size_t>(ciSigned));
    if (frame < heldFrom || sessionEnd < TTDTimePoint{frame, 0} || !frameCp || frameCp->time.frame != frame ||
        eiSigned < 0)
    {
        result.error = "frame " + std::to_string(frame) + " is outside the recorded history (frames " +
                       std::to_string(heldFrom) + ".." + std::to_string(sessionEnd.frame) + ")";
        return finish();
    }
    const size_t ci = static_cast<size_t>(ciSigned);
    const size_t ei = static_cast<size_t>(eiSigned);
    const TTDCheckpoint& cp = *frameCp;
    result.frame = frame;
    result.frameT = FrameSpan();
    uint32_t endT = frame == sessionEnd.frame ? sessionEnd.tInFrame : result.frameT;
    result.partial = endT < result.frameT;
    if (const std::optional<TTDExternalEvent> b = FirstBarrierBetween(cp.time, TTDTimePoint{frame, endT}))
        if (b->time.frame == frame)
        {
            endT = b->time.tInFrame;
            result.barrier = "f" + std::to_string(b->time.frame) + "+" + std::to_string(b->time.tInFrame) + " " +
                             std::string(b->reason, strnlen(b->reason, sizeof(b->reason)));
            result.partial = false;
        }
    result.endT = endT;
    if (endT == 0)
    {
        result.error = "nothing ran in frame " + std::to_string(frame) +
                       (result.barrier.empty() ? std::string(": the session ends at its start")
                                               : ": a marker at its start (" + result.barrier + ")");
        return finish();
    }
    result.interruptsEnabled = cp.cpu.iff1 != 0;

    // The RAM region and the frame start's bytes
    uint32_t ramRegion = UINT32_MAX;
    const std::vector<TTDRegionDesc>& regions = engine.Regions();
    for (uint32_t r = 0; r < regions.size(); ++r)
        if (regions[r].id < TTDRegionId::DeviceStateFirst && regions[r].name == "ram")
        {
            ramRegion = r;
            break;
        }
    const bool wantRam = (sections & (kExplainMemory | kExplainScreen)) && ramRegion != UINT32_MAX;
    std::vector<uint8_t> startRam;
    if (wantRam)
    {
        startRam.assign(static_cast<size_t>(regions[ramRegion].pieces) * kTTDPieceSize, 0);
        engine.RestoreRegion(ei, ramRegion, startRam.data());
    }
    // The screen layouts at both ends (the next checkpoint's latch, or the start's in the last frame)
    const MEM_MODEL model = _context->config.mem_model;
    auto layoutOf = [&](size_t index) -> std::vector<TTDScreenRange> {
        const TTDEngineCheckpoint* c = engine.Checkpoint(index);
        if (!c)
            return {};
        if (const std::vector<TTDScreenRange>* l = engine.ScreenLayoutOf(*c))
            return *l;
        if (!HasUlaScreen(model) || ramRegion == UINT32_MAX)
            return {};
        return UlaScreenLayout(ramRegion, model, c->chipset.p7FFD);
    };
    const std::vector<TTDScreenRange> startLayout = layoutOf(ei);
    const std::vector<TTDScreenRange> endLayout = ei + 1 < cpCount ? layoutOf(ei + 1) : startLayout;

    // The replay
    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot live;
    SaveLiveState(live);
    // IFF1 off at the frame start: the code may be inside an interrupt routine acknowledged in the frame before. That
    // frame's trace gives the call frames still open at its end (from the interrupt up), carried into this one
    std::shared_ptr<TTDFrameTrace> before;
    const TTDCheckpoint* prev = ci > firstCp ? ViewLineageCheckpoint(ci - 1) : nullptr;
    if (cp.cpu.iff1 == 0 && prev && prev->time.frame + 1 == frame && !FirstBarrierBetween(prev->time, cp.time))
    {
        before = TraceCauseFrame(ci - 1, result.frameT);
        ++result.framesReplayed;
    }
    const std::shared_ptr<TTDFrameTrace> trace = TraceCauseFrame(ci, endT);
    ++result.framesReplayed;
    std::vector<uint8_t> endRam;
    if (wantRam && memory->RAMBase())
        endRam.assign(memory->RAMBase(), memory->RAMBase() + startRam.size());
    {
        ReplayModeScope restore(*this);
        RestoreLiveState(live);
        restore.Exit();
        SetState(stateBefore);
    }

    const std::vector<TTDTraceInsn>& insns = trace->insns;
    const std::vector<TTDTraceEvent>& events = trace->events;
    auto lengthOf = [&](size_t k) -> uint64_t {
        const uint32_t next = k + 1 < insns.size() ? insns[k + 1].tInFrame : endT;
        return next > insns[k].tInFrame ? next - insns[k].tInFrame : 0;
    };
    auto regsAfter = [&](size_t k) -> const uint8_t* { return k + 1 < insns.size() ? insns[k + 1].regs : trace->finalRegs; };
    Z80Disassembler* disassembler = _context->pDebugManager ? _context->pDebugManager->GetDisassembler().get() : nullptr;
    LabelManager* labels = _context->pDebugManager ? _context->pDebugManager->GetLabelManager() : nullptr;
    auto insnText = [&](const TTDTraceInsn& in) -> std::string {
        if (in.interrupt)
            return "interrupt";
        if (!disassembler)
            return "";
        uint8_t len = 0;
        return disassembler->disassembleSingleCommand(std::vector<uint8_t>(in.bytes, in.bytes + 4), in.pc, &len);
    };

    // ---- routines, time, interrupts ----
    struct Acc
    {
        TTDExplainRoutine r;
        uint64_t stamp = UINT64_MAX;   // the last record counted into t (an entry open twice counts once)
    };
    std::vector<Acc> routines;
    std::map<std::tuple<uint16_t, PhysPage, bool, bool>, size_t> routineIndex;
    auto routineOf = [&](uint16_t pc, PhysPage page, bool isr, bool start) -> size_t {
        const auto key = std::make_tuple(pc, page, isr, start);
        auto it = routineIndex.find(key);
        if (it != routineIndex.end())
            return it->second;
        Acc a;
        a.r.pc = pc;
        a.r.codePage = page;
        a.r.isr = isr;
        a.r.start = start;
        if (labels)
            if (const std::shared_ptr<Label> l = labels->GetLabelByZ80Address(pc); l && l->address == pc)
                a.r.label = l->name;
        routines.push_back(a);
        routineIndex.emplace(key, routines.size() - 1);
        return routines.size() - 1;
    };
    struct Open
    {
        size_t routine;
        uint16_t entrySP;   // the SP after the push (the return address there)
        int interrupt;      // index into result.interrupts, or -1
    };
    std::vector<Open> stack;
    uint8_t im = cp.cpu.im;
    uint32_t isrDepth = 0;
    std::vector<OpenBefore> carried;
    if (before)
        carried = OpenAtTheEnd(*before, prev->cpu.im);
    if (!insns.empty() && !carried.empty())
    {
        for (const OpenBefore& o : carried)
        {
            int irqIndex = -1;
            if (o.interrupt)
            {
                TTDExplainInterrupt irq;
                irq.beforeFrame = true;
                irq.mode = o.mode;
                irq.entry = o.entry;
                irq.hasVector = o.hasVector;
                irq.vectorAddress = o.vectorAddress;
                result.interrupts.push_back(irq);
                irqIndex = static_cast<int>(result.interrupts.size() - 1);
                ++isrDepth;
            }
            const size_t r = routineOf(o.entry, o.page, o.interrupt, false);
            Acc& a = routines[r];
            ++a.r.calls;
            if (!a.r.hasCaller)
            {
                a.r.hasCaller = true;
                a.r.caller = o.caller;
            }
            stack.push_back({r, o.entrySP, irqIndex});
        }
    }
    else if (!insns.empty())
    {
        const TTDTraceInsn& first = insns.front();
        // An interrupt acknowledged in the frame before's last step (the INT at the frame boundary): the frame starts
        // at the routine's entry with IFF1 off, the interrupted PC on the stack. It is the interrupt routine, not code
        // running at the start
        const bool ackedBefore = !first.interrupt && cp.cpu.iff1 == 0 && (im == 1 || im == 2) &&
                                 first.pc == (im == 1 ? 0x0038 : trace->startIm2Entry);
        if (ackedBefore)
        {
            TTDExplainInterrupt irq;
            irq.beforeFrame = true;
            irq.mode = im;
            irq.entry = first.pc;
            if (im == 2)
            {
                irq.hasVector = true;
                irq.vectorAddress = static_cast<uint16_t>(cp.cpu.i << 8 | 0xFF);
            }
            result.interrupts.push_back(irq);
            const size_t r = routineOf(first.pc, first.codePage, true, false);
            routines[r].r.calls = 1;
            routines[r].r.hasCaller = true;
            routines[r].r.caller = trace->startStackWord;
            stack.push_back({r, Sp(first.regs), 0});
            isrDepth = 1;
        }
        else
        {
            const size_t base = routineOf(first.pc, first.codePage, false, true);
            stack.push_back({base, 0xFFFF, -1});
            routines[base].r.calls = 1;
        }
    }
    for (size_t k = 0; k < insns.size(); ++k)
    {
        const TTDTraceInsn& in = insns[k];
        const uint64_t len = lengthOf(k) + (k == 0 ? in.tInFrame : 0);   // the first record also takes the lead-in
        if (in.interrupt)
        {
            const uint16_t sp = static_cast<uint16_t>(Sp(in.regs) - 2);
            const uint16_t entry = k + 1 < insns.size() ? insns[k + 1].pc : 0;
            const PhysPage page = k + 1 < insns.size() ? insns[k + 1].codePage : kPhysPageNone;
            TTDExplainInterrupt irq;
            irq.tInFrame = in.tInFrame;
            irq.mode = im;
            irq.entry = entry;
            if (im == 2)
                for (uint32_t e = in.firstEvent; e < in.firstEvent + in.eventCount; ++e)
                    if (events[e].type == 'R')
                    {
                        irq.hasVector = true;
                        irq.vectorAddress = events[e].address;
                        break;
                    }
            result.interrupts.push_back(irq);
            const size_t r = routineOf(entry, page, true, false);
            Acc& a = routines[r];
            ++a.r.calls;
            if (!a.r.hasCaller)
            {
                a.r.hasCaller = true;
                a.r.caller = in.pc;
            }
            stack.push_back({r, sp, static_cast<int>(result.interrupts.size() - 1)});
            ++isrDepth;
        }
        // The record's time: own to the top, inclusive to every entry open (once each) down to the innermost
        // interrupt (an interrupt routine is not part of the routines it interrupted)
        if (!stack.empty())
        {
            routines[stack.back().routine].r.ownT += len;
            for (size_t d = stack.size(); d-- > 0;)
            {
                const Open& o = stack[d];
                Acc& a = routines[o.routine];
                if (a.stamp != k)
                {
                    a.stamp = k;
                    a.r.t += len;
                    if (!in.interrupt)
                        ++a.r.insns;
                }
                if (o.interrupt >= 0)
                {
                    result.interrupts[size_t(o.interrupt)].t += len;
                    break;
                }
            }
        }
        if (isrDepth > 0)
            result.isrT += len;
        else if (!in.interrupt && in.bytes[0] == 0x76)
            result.haltT += len;
        else
            result.codeT += len;
        if (in.interrupt)
            continue;
        ++result.instructions;

        // The record's effect on the call structure
        const uint16_t spBefore = Sp(in.regs);
        const uint16_t spAfter = Sp(regsAfter(k));
        if (in.bytes[0] == 0xED && (in.bytes[1] == 0x46 || in.bytes[1] == 0x66))
            im = 0;
        else if (in.bytes[0] == 0xED && (in.bytes[1] == 0x56 || in.bytes[1] == 0x76))
            im = 1;
        else if (in.bytes[0] == 0xED && (in.bytes[1] == 0x5E || in.bytes[1] == 0x7E))
            im = 2;
        const bool hasNext = k + 1 < insns.size();
        if (hasNext && (IsCall(in.bytes) || IsRst(in.bytes)) && spAfter == static_cast<uint16_t>(spBefore - 2))
        {
            const uint16_t target = IsRst(in.bytes) ? static_cast<uint16_t>(in.bytes[0] & 0x38)
                                                    : static_cast<uint16_t>(in.bytes[1] | in.bytes[2] << 8);
            PhysPage page = in.banks[target >> 14];
            if (!insns[k + 1].interrupt && insns[k + 1].pc == target)
                page = insns[k + 1].codePage;
            const size_t r = routineOf(target, page, false, false);
            Acc& a = routines[r];
            ++a.r.calls;
            if (!a.r.hasCaller)
            {
                a.r.hasCaller = true;
                a.r.caller = in.pc;
            }
            stack.push_back({r, spAfter, -1});
        }
        else if (hasNext && IsRet(in.bytes) && spAfter == static_cast<uint16_t>(spBefore + 2))
        {
            const uint16_t returnTo = insns[k + 1].pc;   // an interrupt record's pc is the interrupted one: the same
            bool popped = false;
            // The bottom frame pops only when it is an interrupt routine entered before the frame
            while ((stack.size() > 1 || (!stack.empty() && stack.back().interrupt >= 0)) &&
                   stack.back().entrySP <= spBefore)
            {
                const Open o = stack.back();
                stack.pop_back();
                popped = true;
                if (o.interrupt >= 0)
                {
                    TTDExplainInterrupt& irq = result.interrupts[size_t(o.interrupt)];
                    irq.returned = true;
                    irq.returnedTo = returnTo;
                    --isrDepth;
                }
            }
            if ((!popped && stack.size() == 1 && stack.back().interrupt < 0) || stack.empty())
            {
                // The frame's starting code (or the interrupt routine entered before the frame) returned to its
                // caller: a new starting context there
                const PhysPage page = insns[k + 1].interrupt ? kPhysPageNone : insns[k + 1].codePage;
                const size_t r = routineOf(returnTo, page, false, true);
                ++routines[r].r.calls;
                if (stack.empty())
                    stack.push_back({r, 0xFFFF, -1});
                else
                    stack.back() = {r, 0xFFFF, -1};
            }
        }
    }
    if (sections & kExplainRoutines)
    {
        std::vector<TTDExplainRoutine> list;
        list.reserve(routines.size());
        for (const Acc& a : routines)
            if (a.r.t > 0 || a.r.calls > 0)
                list.push_back(a.r);
        std::stable_sort(list.begin(), list.end(),
                         [](const TTDExplainRoutine& a, const TTDExplainRoutine& b) { return a.t > b.t; });
        if (list.size() > limit)
        {
            result.moreRoutines = static_cast<uint32_t>(list.size() - limit);
            list.resize(limit);
        }
        result.routines = std::move(list);
    }

    // ---- paging ----
    if (sections & kExplainPaging)
    {
        for (size_t k = 1; k < insns.size(); ++k)
            for (uint8_t b = 0; b < 4; ++b)
                if (insns[k].banks[b] != insns[k - 1].banks[b])
                {
                    if (result.paging.size() >= limit)
                    {
                        ++result.morePaging;
                        continue;
                    }
                    size_t by = k - 1;
                    while (by > 0 && insns[by].interrupt)
                        --by;
                    result.paging.push_back({insns[k].tInFrame, insns[by].pc, b, insns[k].banks[b], insns[k - 1].banks[b]});
                }
    }

    // ---- ports and the border ----
    std::vector<TTDTimelineDevice> devices;
    PortClassifier classifier(_context->pPortDecoder, devices);
    const int16_t ula = classifier.UlaDevice();
    auto isUla = [&](uint16_t port) {
        return ula != PortClassifier::kNone ? classifier.DeviceOf(port) == ula : HasUlaScreen(model) && (port & 1) == 0;
    };
    if (sections & (kExplainPorts | kExplainScreen))
    {
        std::map<std::pair<bool, int32_t>, size_t> portIndex;   // (out, device or -1 - port)
        std::vector<TTDExplainPort> ports;
        for (size_t k = 0; k < insns.size(); ++k)
            for (uint32_t e = insns[k].firstEvent; e < insns[k].firstEvent + insns[k].eventCount; ++e)
            {
                const TTDTraceEvent& ev = events[e];
                if (ev.type != 'I' && ev.type != 'O')
                    continue;
                const bool out = ev.type == 'O';
                const uint16_t port = ev.address;
                if (out && (sections & kExplainScreen) && isUla(port))
                {
                    const uint8_t colour = ev.value & 0x07;
                    if (!result.border.empty() && result.border.back().colour == colour)
                        ++result.border.back().count;
                    else
                        result.border.push_back({colour, 1});
                }
                if (!(sections & kExplainPorts))
                    continue;
                const int16_t d = classifier.DeviceOf(port);
                const std::pair<bool, int32_t> key{out, d >= 0 ? d : -1 - static_cast<int32_t>(port)};
                auto it = portIndex.find(key);
                if (it == portIndex.end())
                {
                    TTDExplainPort p;
                    p.out = out;
                    p.device = d >= 0 ? devices[size_t(d)].name : "";
                    p.port = port;
                    p.tInFrame = ev.tInFrame;
                    p.pc = insns[k].pc;
                    ports.push_back(p);
                    it = portIndex.emplace(key, ports.size() - 1).first;
                }
                TTDExplainPort& p = ports[it->second];
                ++p.count;
                if (port != p.port)
                    p.manyPorts = true;
                if (std::find(p.values.begin(), p.values.end(), ev.value) == p.values.end())
                {
                    if (p.values.size() < 4)
                        p.values.push_back(ev.value);
                    else
                        p.moreValues = true;
                }
                if (!out && isUla(port))
                {
                    p.rows |= KeyboardRowsRead(port);
                    for (const std::string& key2 : KeysSeenDown(port, ev.value))
                        if (std::find(p.keys.begin(), p.keys.end(), key2) == p.keys.end())
                            p.keys.push_back(key2);
                }
            }
        std::stable_sort(ports.begin(), ports.end(), [](const TTDExplainPort& a, const TTDExplainPort& b) { return a.count > b.count; });
        if (ports.size() > limit)
        {
            result.morePorts = static_cast<uint32_t>(ports.size() - limit);
            ports.resize(limit);
        }
        result.ports = std::move(ports);
    }

    // ---- memory ----
    // The screen's bytes by RAM offset: 0 none, 1 pixels, 2 attributes (the start's layout and the end's)
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
    if ((sections & kExplainMemory) && !startRam.empty() && endRam.size() == startRam.size())
    {
        // The SP span the frame used: a write inside it is the stack's
        uint16_t spLow = 0xFFFF;
        uint16_t spHigh = 0;
        for (const TTDTraceInsn& in : insns)
        {
            spLow = std::min(spLow, Sp(in.regs));
            spHigh = std::max(spHigh, Sp(in.regs));
        }
        spLow = std::min(spLow, Sp(trace->finalRegs));
        spHigh = std::max(spHigh, Sp(trace->finalRegs));
        spLow = static_cast<uint16_t>(spLow >= 2 ? spLow - 2 : 0);
        spHigh = static_cast<uint16_t>(spHigh <= 0xFFFD ? spHigh + 1 : 0xFFFF);
        const bool stackKnown = !insns.empty() && spHigh >= spLow;

        // Every CPU write: RAM offset, the instruction, the Z80 address, stack or not
        struct Write
        {
            uint32_t offset;
            uint32_t insn;
            uint16_t address;
            bool stack;
        };
        std::vector<Write> writes;
        for (uint32_t k = 0; k < insns.size(); ++k)
            for (uint32_t e = insns[k].firstEvent; e < insns[k].firstEvent + insns[k].eventCount; ++e)
            {
                const TTDTraceEvent& ev = events[e];
                if (ev.type != 'W' || ev.page == kPhysPageNone || ev.page > kPhysPageMax)
                    continue;
                const uint32_t offset = static_cast<uint32_t>(ev.page) * 0x4000u + ev.offset;
                if (offset >= startRam.size())
                    continue;
                const bool stack = stackKnown && ev.address >= spLow && ev.address <= spHigh;
                writes.push_back({offset, k, ev.address, stack});
            }
        std::stable_sort(writes.begin(), writes.end(), [](const Write& a, const Write& b) { return a.offset < b.offset; });

        // Ranges: the screen's pixels, its attributes and the stack one each; the rest merged per page with gaps of
        // 16 bytes or less; changed bytes no CPU write explains on their own
        struct Building
        {
            TTDExplainRange range;
            uint32_t last = 0;
            bool hasAny = false;
            std::map<std::pair<uint16_t, PhysPage>, std::pair<uint32_t, uint32_t>> writers;   // -> count, insn
        };
        std::vector<Building> built;
        std::map<int, size_t> fixed;   // region class -> index into built (pixels, attributes, stack)
        int openRam = -1;              // the RAM range being extended
        int openDevice = -1;
        auto classOf = [&](uint32_t offset, bool stack) -> TTDExplainRegion {
            const int sc = std::max(screenClassOf(startLayout, offset), screenClassOf(endLayout, offset));
            if (sc == 1)
                return TTDExplainRegion::ScreenPixels;
            if (sc == 2)
                return TTDExplainRegion::ScreenAttributes;
            return stack ? TTDExplainRegion::Stack : TTDExplainRegion::Ram;
        };
        auto addressOf = [&](uint32_t offset) -> std::optional<uint16_t> {
            const PhysPage page = static_cast<PhysPage>(offset / 0x4000u);
            const TTDTraceInsn* at = insns.empty() ? nullptr : &insns.front();
            if (at)
                for (uint8_t b = 0; b < 4; ++b)
                    if (at->banks[b] == page)
                        return static_cast<uint16_t>(b * 0x4000u + (offset & 0x3FFFu));
            return std::nullopt;
        };
        auto touch = [&](uint32_t offset, bool changed, const Write* w, bool firstWriteOfByte) {
            const bool device = changed && !w;
            const TTDExplainRegion region = classOf(offset, w ? w->stack : false);
            const PhysPage page = static_cast<PhysPage>(offset / 0x4000u);
            size_t index = SIZE_MAX;
            if (region != TTDExplainRegion::Ram && !device)
            {
                auto it = fixed.find(static_cast<int>(region));
                if (it == fixed.end())
                {
                    Building b;
                    b.range.region = region;
                    built.push_back(std::move(b));
                    it = fixed.emplace(static_cast<int>(region), built.size() - 1).first;
                }
                index = it->second;
            }
            else
            {
                int& open = device ? openDevice : openRam;
                if (open >= 0)
                {
                    const Building& b = built[size_t(open)];
                    if (b.range.page == page && offset - b.last <= 16 && b.range.region == region)
                        index = size_t(open);
                }
                if (index == SIZE_MAX)
                {
                    Building b;
                    b.range.region = region;
                    b.range.device = device;
                    built.push_back(std::move(b));
                    open = static_cast<int>(built.size() - 1);
                    index = built.size() - 1;
                }
            }
            Building& b = built[index];
            if (!b.hasAny)
            {
                b.hasAny = true;
                b.range.page = page;
                b.range.offset = offset & 0x3FFFu;
                if (w)
                {
                    b.range.hasAddress = true;
                    b.range.address = w->address;
                }
                else if (const std::optional<uint16_t> a = addressOf(offset))
                {
                    b.range.hasAddress = true;
                    b.range.address = *a;
                }
                b.last = offset;
            }
            // A range's span: from its first byte to its last (screen ranges can cover several pages' worth)
            const uint32_t first = static_cast<uint32_t>(b.range.page) * 0x4000u + b.range.offset;
            if (offset < first)
            {
                b.range.length += first - offset;
                b.range.offset = offset & 0x3FFFu;
                b.range.page = page;
                if (b.range.hasAddress)
                    b.range.address = static_cast<uint16_t>(b.range.address - (first - offset));
            }
            const uint32_t start = static_cast<uint32_t>(b.range.page) * 0x4000u + b.range.offset;
            b.range.length = std::max(b.range.length, offset - start + 1);
            b.last = std::max(b.last, offset);
            if (firstWriteOfByte || !w)
            {
                if (changed)
                    ++b.range.changed;
                if (w)
                    ++b.range.written;
            }
            if (w)
            {
                const TTDTraceInsn& in = insns[w->insn];
                auto& slot = b.writers[{in.pc, in.codePage}];
                if (slot.first == 0)
                    slot.second = w->insn;
                ++slot.first;
            }
        };
        // Walk the offsets in order: written ones (all their writes) and changed ones with no write
        size_t wi = 0;
        auto changedAt = [&](uint32_t o) { return startRam[o] != endRam[o]; };
        const uint32_t ramSize = static_cast<uint32_t>(startRam.size());
        uint32_t o = 0;
        while (o < ramSize)
        {
            const uint32_t nextWrite = wi < writes.size() ? writes[wi].offset : ramSize;
            // The changed bytes before the next written one: memcmp in blocks
            while (o < nextWrite)
            {
                const uint32_t block = std::min<uint32_t>(nextWrite - o, 4096u - (o % 4096u));
                if (std::memcmp(startRam.data() + o, endRam.data() + o, block) == 0)
                {
                    o += block;
                    continue;
                }
                for (uint32_t x = o; x < o + block; ++x)
                    if (changedAt(x))
                        touch(x, true, nullptr, true);
                o += block;
            }
            if (o >= ramSize)
                break;
            bool firstOfByte = true;
            while (wi < writes.size() && writes[wi].offset == o)
            {
                touch(o, changedAt(o), &writes[wi], firstOfByte);
                firstOfByte = false;
                ++wi;
            }
            ++o;
        }

        std::vector<TTDExplainRange> ranges;
        for (Building& b : built)
        {
            TTDExplainRange& r = b.range;
            if (r.length <= 2 && r.region == TTDExplainRegion::Ram)
            {
                const uint32_t at = static_cast<uint32_t>(r.page) * 0x4000u + r.offset;
                r.hasValues = true;
                r.before = startRam[at];
                r.after = endRam[at];
                if (r.length == 2 && at + 1 < ramSize)
                {
                    r.before = static_cast<uint16_t>(r.before | startRam[at + 1] << 8);
                    r.after = static_cast<uint16_t>(r.after | endRam[at + 1] << 8);
                }
            }
            std::vector<std::pair<std::pair<uint16_t, PhysPage>, std::pair<uint32_t, uint32_t>>> ws(b.writers.begin(), b.writers.end());
            std::stable_sort(ws.begin(), ws.end(), [](const auto& a, const auto& c) { return a.second.first > c.second.first; });
            for (const auto& w : ws)
            {
                if (r.writers.size() >= limit)
                {
                    r.moreWriters = true;
                    break;
                }
                r.writers.push_back({w.first.first, w.first.second, insnText(insns[w.second.second]), w.second.first});
            }
            ranges.push_back(std::move(r));
        }
        std::stable_sort(ranges.begin(), ranges.end(), [](const TTDExplainRange& a, const TTDExplainRange& b) {
            return a.changed != b.changed ? a.changed > b.changed : a.written > b.written;
        });
        if (ranges.size() > limit)
        {
            result.moreMemory = static_cast<uint32_t>(ranges.size() - limit);
            ranges.resize(limit);
        }
        result.memory = std::move(ranges);
    }

    // ---- screen ----
    if ((sections & kExplainScreen) && !startRam.empty() && endRam.size() == startRam.size() && !startLayout.empty())
    {
        auto picture = [&](const std::vector<TTDScreenRange>& layout, const std::vector<uint8_t>& ram, std::vector<uint8_t>& out) {
            out.clear();
            for (const TTDScreenRange& r : layout)
                if (r.region == ramRegion && r.begin < r.end && r.end <= ram.size())
                    out.insert(out.end(), ram.begin() + r.begin, ram.begin() + r.end);
        };
        std::vector<uint8_t> a;
        std::vector<uint8_t> b;
        picture(startLayout, startRam, a);
        picture(endLayout, endRam, b);
        if (!a.empty() && a.size() == b.size())
        {
            result.hasScreen = true;
            result.screenBytes = static_cast<uint32_t>(a.size());
            const size_t pixels = a.size() == 6912 ? 6144 : a.size();
            for (size_t k = 0; k < a.size(); ++k)
                if (a[k] != b[k])
                {
                    ++result.screenChanged;
                    if (k < pixels)
                        ++result.pixelsChanged;
                    else
                        ++result.attributesChanged;
                }
            result.screenSwitched = startLayout.size() != endLayout.size() ||
                                    !std::equal(startLayout.begin(), startLayout.end(), endLayout.begin(),
                                                [](const TTDScreenRange& x, const TTDScreenRange& y) {
                                                    return x.region == y.region && x.begin == y.begin && x.end == y.end;
                                                });
        }
    }

    // ---- input ----
    if (sections & kExplainInput)
    {
        std::vector<uint8_t> held;
        for (const TTDInputEvent& e : LineageInputEvents())
        {
            if (e.kind != TTDInputKind::Key)
                continue;
            if (e.time.frame < frame)
            {
                auto it = std::find(held.begin(), held.end(), e.key);
                if (e.pressed && it == held.end())
                    held.push_back(e.key);
                else if (!e.pressed && it != held.end())
                    held.erase(it);
            }
            else if (e.time.frame == frame && e.time.tInFrame < endT)
                result.keys.push_back({e.time.tInFrame, DebugKeyboardManager::GetKeyDisplayName(static_cast<ZXKeysEnum>(e.key)), e.pressed});
            else if (e.time.frame > frame)
                break;
        }
        for (uint8_t key : held)
            result.keysHeld.push_back(DebugKeyboardManager::GetKeyDisplayName(static_cast<ZXKeysEnum>(key)));
    }

    // ---- markers ----
    if (sections & kExplainMarkers)
    {
        ViewOwnerOfFrame(frame);   // the frame's markers are its owner line's
        for (const TTDExternalEvent& e : _line->externalEvents.Events())
            if (e.time.frame == frame)
                result.markers.push_back("+" + std::to_string(e.time.tInFrame) + " " +
                                         std::string(e.reason, strnlen(e.reason, sizeof(e.reason))));
        for (const TTDBookmark& b : GetBookmarks())
            if (b.time.frame == frame)
                result.markers.push_back("+" + std::to_string(b.time.tInFrame) + " bookmark '" + b.label + "'");
    }
    return finish();
}

}  // namespace ttd
