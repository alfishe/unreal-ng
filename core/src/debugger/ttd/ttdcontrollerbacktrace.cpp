/// @file ttdcontrollerbacktrace.cpp
/// @brief TimeTravelController::QueryBacktrace (docs/inprogress/2026-11-07-ttd-backtrace/README.md): the call stack at
/// a moment. The machine is replayed to the moment for SP and the stack words; each word's last write before the moment
/// comes from the write journal (or the pruned replay of FindAccesses), the frame of that write is traced with the
/// instruction and bus hooks (TraceCauseFrame) and the cause chain's source rules (TTDExplainInstruction) say whether
/// the writer pushed a return address. The machine is put back exactly where it stood.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <list>
#include <memory>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/ttd/ttdbacktrace.h"
#include "debugger/ttd/ttdcausechain.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace ttd
{

namespace
{
constexpr size_t kTraceCacheFrames = 8;

std::string FrameText(const TTDTimePoint& t)
{
    return "f" + std::to_string(t.frame) + "+" + std::to_string(t.tInFrame);
}

bool IsRst(uint8_t opcode)
{
    return (opcode & 0xC7) == 0xC7;
}

/// CALL nn (#CD) and CALL cc,nn (#C4, #CC, ... #FC)
bool IsCall(uint8_t opcode)
{
    return opcode == 0xCD || (opcode & 0xC7) == 0xC4;
}
}  // namespace

const char* TTDBacktraceKindName(TTDBacktraceKind kind)
{
    switch (kind)
    {
        case TTDBacktraceKind::Call: return "call";
        case TTDBacktraceKind::Rst: return "rst";
        case TTDBacktraceKind::Interrupt: return "interrupt";
    }
    return "call";
}

std::string TTDBacktraceText(const TTDBacktraceResult& r)
{
    char buf[256];
    std::string out;
    auto routineOf = [&](size_t i) -> std::string {
        // Frame i's code runs in the routine the frame below it (i, innermost first) entered
        if (i < r.frames.size() && r.frames[i].hasCallee)
        {
            std::snprintf(buf, sizeof(buf), " in #%04X", r.frames[i].callee);
            return buf;
        }
        return "";
    };
    std::snprintf(buf, sizeof(buf), "#0  pc #%04X%s, sp #%04X at %s\n", r.pc, routineOf(0).c_str(), r.sp,
                  FrameText(r.at).c_str());
    out += buf;
    for (size_t i = 0; i < r.frames.size(); ++i)
    {
        const TTDBacktraceFrame& f = r.frames[i];
        if (f.kind == TTDBacktraceKind::Interrupt)
            std::snprintf(buf, sizeof(buf), "#%zu  interrupt at #%04X%s, returns to #%04X, at %s (slot #%04X)", i + 1,
                          f.callerPc, routineOf(i + 1).c_str(), f.returnAddress, FrameText(f.calledAt).c_str(), f.slot);
        else
            std::snprintf(buf, sizeof(buf), "#%zu  #%04X %s%s, returns to #%04X, at %s (slot #%04X)", i + 1, f.callerPc,
                          f.text.c_str(), routineOf(i + 1).c_str(), f.returnAddress, FrameText(f.calledAt).c_str(),
                          f.slot);
        out += buf;
        if (f.dataWords > 0)
            out += ", " + std::to_string(f.dataWords) + " data word" + (f.dataWords == 1 ? "" : "s") + " below";
        out += "\n";
    }
    std::snprintf(buf, sizeof(buf), "stop: %s at #%04X; %u words, %u data, %u frames traced, %.1f ms", r.stop.c_str(),
                  r.stopSlot, r.slotsScanned, r.dataWords, r.framesTraced, r.ms);
    out += buf;
    return out;
}

TTDBacktraceResult TimeTravelController::QueryBacktrace(const TTDBacktraceQuery& query)
{
    const auto started = std::chrono::steady_clock::now();
    // Replays checkpoints and puts the machine back: an operation that changes it
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDBacktraceResult result;
    const LineageViewGuard lineageView{*this};   // with branches the walk views each lineage line it reads
    auto finish = [&]() -> TTDBacktraceResult& {
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
    const TTDTimePoint sessionStart = _lines.front()->timeline.front().time;   // the lineage starts on the trunk
    const TTDTimePoint at = query.hasTime ? query.at : CurrentPosition();
    if (at < sessionStart || SessionEndPosition() < at)
    {
        result.error = "the moment " + FrameText(at) + " is outside the recorded history (" + FrameText(sessionStart) +
                       " .. " + FrameText(SessionEndPosition()) + ")";
        return finish();
    }
    result.at = at;
    const uint32_t depthLimit = std::clamp(query.maxDepth, 1u, TTDBacktraceQuery::kMaxDepth);
    const uint32_t slotLimit = std::clamp(query.maxSlots, 1u, TTDBacktraceQuery::kMaxSlots);

    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot live;   // not _liveSnapshot: FindAccesses saves the machine there itself
    SaveLiveState(live);
    auto putBack = [&]() {
        ReplayModeScope restore(*this);
        RestoreLiveState(live);
        restore.Exit();
        SetState(stateBefore);
    };

    // The machine at the moment: PC, SP, and where each stack word is (its page, read before any trace moves it)
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
        putBack();
        result.error = "the replay could not reach " + FrameText(at);
        return finish();
    }
    result.pc = z80->pc;
    result.codePage = memory->TtdPageOf(z80->pc);
    result.sp = z80->sp;
    struct Slot
    {
        uint16_t address = 0;
        PhysPage page = kPhysPageNone;
        uint8_t flip = 0;
        uint16_t value = 0;
    };
    std::vector<Slot> slots;
    for (uint32_t k = 0; k < slotLimit; ++k)
    {
        const uint32_t address = uint32_t(result.sp) + 2 * k;
        if (address > 0xFFFE)
            break;
        Slot s;
        s.address = static_cast<uint16_t>(address);
        s.page = memory->TtdKeyOf(s.address, s.flip);
        s.value = static_cast<uint16_t>(memory->DirectReadFromZ80Memory(s.address) |
                                        memory->DirectReadFromZ80Memory(static_cast<uint16_t>(s.address + 1)) << 8);
        slots.push_back(s);
    }

    // Traced frames by checkpoint index, the newest used last
    std::list<std::pair<size_t, std::shared_ptr<TTDFrameTrace>>> cache;
    auto traceOf = [&](size_t cpIndex) -> std::shared_ptr<TTDFrameTrace> {
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->first == cpIndex)
            {
                cache.splice(cache.end(), cache, it);
                return cache.back().second;
            }
        const TTDCheckpoint* held = ViewLineageCheckpoint(cpIndex);   // an engine index of the active lineage
        if (!held)
            return std::make_shared<TTDFrameTrace>();
        const TTDCheckpoint& cp = *held;
        const TTDTimePoint end = SessionEndPosition();
        uint32_t endT = cp.time.frame == end.frame ? end.tInFrame : FrameSpan();
        if (const std::optional<TTDExternalEvent> barrier = FirstBarrierBetween(cp.time, TTDTimePoint{cp.time.frame, endT}))
            endT = barrier->time.frame == cp.time.frame ? barrier->time.tInFrame : 0;
        auto trace = TraceCauseFrame(cpIndex, endT);
        ++result.framesTraced;
        cache.emplace_back(cpIndex, trace);
        if (cache.size() > kTraceCacheFrames)
            cache.pop_front();
        return trace;
    };
    Z80Disassembler* disassembler = _context->pDebugManager ? _context->pDebugManager->GetDisassembler().get() : nullptr;

    const uint64_t atGlobal = GlobalT(at);
    uint32_t dataSince = 0;   // data words since the last frame found
    result.stop = slots.size() < slotLimit ? "top_of_memory" : "max_slots";
    for (const Slot& s : slots)
    {
        if (s.page == kPhysPageNone)
        {
            result.stop = "rom";
            result.stopSlot = s.address;
            break;
        }
        ++result.slotsScanned;
        result.stopSlot = s.address;

        // The word's last write before the moment: its low byte (a push writes the high byte first, then the low)
        TTDFindResult found;
        if (atGlobal > 0)
        {
            TTDSearchQuery q;
            q.addrFrom = q.addrTo = s.address;
            q.access = TTDAccessType::Write;
            q.hasPhysPageFilter = true;
            q.physPage = s.page;
            q.flip = static_cast<int8_t>(s.flip);
            q.beforeGlobalT = atGlobal - 1;
            found = FindAccesses(q, TTDFindOptions{});
        }
        if (found.hits.empty())
        {
            result.stop = found.blockingMarker.reason[0] != '\0' ? "barrier" : "before_session";
            break;
        }
        const TTDSearchResult& hit = found.hits.front().access;
        const int64_t hitIndex = ViewLineageIndexAtOrBefore(hit.time);
        bool isFrame = false;
        TTDBacktraceFrame frame;
        if (hitIndex >= 0)
        {
            std::shared_ptr<TTDFrameTrace> trace = traceOf(size_t(hitIndex));
            // The instruction holding that write
            size_t insn = SIZE_MAX;
            for (size_t k = trace->insns.size(); k-- > 0 && insn == SIZE_MAX;)
            {
                const TTDTraceInsn& in = trace->insns[k];
                for (uint32_t e = in.firstEvent; e < in.firstEvent + in.eventCount; ++e)
                {
                    const TTDTraceEvent& ev = trace->events[e];
                    if (ev.type == 'W' && ev.address == s.address && ev.page == s.page && ev.Flip() == s.flip &&
                        ev.tInFrame == hit.time.tInFrame)
                        insn = k;
                }
            }
            if (insn != SIZE_MAX)
            {
                const TTDCauseName name = TTDCauseName::Mem(s.address, s.page, s.flip);
                for (const TTDCauseOutput& o : TTDExplainInstruction(*trace, insn, false))
                {
                    // A pushed return address that is still the whole word (the high byte not written over since)
                    if (!(o.name == name) || (o.kind != TTDCauseKind::Return && o.kind != TTDCauseKind::Interrupt) ||
                        o.leafValue != s.value)
                        continue;
                    const TTDTraceInsn& in = trace->insns[insn];
                    isFrame = true;
                    frame.slot = s.address;
                    frame.returnAddress = s.value;
                    frame.calledAt = TTDTimePoint{trace->frame, in.tInFrame};
                    frame.callerPc = in.pc;
                    frame.callerPage = in.codePage;
                    if (o.kind == TTDCauseKind::Interrupt || in.interrupt)
                    {
                        frame.kind = TTDBacktraceKind::Interrupt;
                        frame.text = "interrupt";
                    }
                    else
                    {
                        frame.kind = IsRst(in.bytes[0]) ? TTDBacktraceKind::Rst : TTDBacktraceKind::Call;
                        if (disassembler)
                        {
                            uint8_t len = 0;
                            frame.text = disassembler->disassembleSingleCommand(
                                std::vector<uint8_t>(in.bytes, in.bytes + 4), in.pc, &len);
                        }
                    }
                    // The routine entered: the next instruction (an interrupt accepted right after the push is a
                    // record of its own), else what the instruction names
                    if (insn + 1 < trace->insns.size() && !trace->insns[insn + 1].interrupt)
                    {
                        frame.hasCallee = true;
                        frame.callee = trace->insns[insn + 1].pc;
                    }
                    else if (frame.kind == TTDBacktraceKind::Call && IsCall(in.bytes[0]))
                    {
                        frame.hasCallee = true;
                        frame.callee = static_cast<uint16_t>(in.bytes[1] | in.bytes[2] << 8);
                    }
                    else if (frame.kind == TTDBacktraceKind::Rst)
                    {
                        frame.hasCallee = true;
                        frame.callee = static_cast<uint16_t>(in.bytes[0] & 0x38);
                    }
                    break;
                }
            }
        }
        if (!isFrame)
        {
            ++result.dataWords;
            ++dataSince;
            continue;
        }
        frame.dataWords = dataSince;
        dataSince = 0;
        result.frames.push_back(std::move(frame));
        if (result.frames.size() >= depthLimit)
        {
            result.stop = "max_depth";
            break;
        }
    }

    putBack();
    return finish();
}

}  // namespace ttd
