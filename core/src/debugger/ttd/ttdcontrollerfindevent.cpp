/// @file ttdcontrollerfindevent.cpp
/// @brief TimeTravelController::QueryFindEvent (docs/inprogress/2026-11-02-ttd-find-event/design.md): the first or last
/// moments in a window where a described event happened. Frames are filtered from what the session stores - the
/// engine's piece changes (a byte, the picture), the coverage index (an address ran), the port and input journals -
/// and only the hit frames are replayed, for the exact write or instruction.

#include "stdafx.h"
#include "timetravelcontroller.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <functional>
#include <cstring>
#include <map>
#include <optional>

#include "debugger/debugmanager.h"
#include "debugger/disassembler/z80disasm.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/ttdfindevent.h"
#include "debugger/ttd/ttdportsearch.h"
#include "debugger/ttd/ttdscreenlayout.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace ttd
{

namespace
{
struct KindName
{
    TTDFindEventKind kind;
    const char* name;
};
constexpr KindName kKinds[] = {{TTDFindEventKind::Mem, "mem"},       {TTDFindEventKind::Screen, "screen"},
                               {TTDFindEventKind::Exec, "exec"},     {TTDFindEventKind::Port, "port"},
                               {TTDFindEventKind::Border, "border"}, {TTDFindEventKind::Key, "key"}};
struct CondName
{
    TTDFindEventCond cond;
    const char* name;
};
constexpr CondName kConds[] = {{TTDFindEventCond::Changed, "changed"},   {TTDFindEventCond::Increased, "increased"},
                               {TTDFindEventCond::Decreased, "decreased"}, {TTDFindEventCond::Equals, "equals"},
                               {TTDFindEventCond::Becomes, "becomes"},   {TTDFindEventCond::Differs, "differs"},
                               {TTDFindEventCond::BitSet, "bit_set"},    {TTDFindEventCond::BitClear, "bit_clear"},
                               {TTDFindEventCond::Change, "change"},     {TTDFindEventCond::Cleared, "cleared"}};

std::string Hex(uint32_t v, int digits)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%0*X", digits, v);
    return buf;
}

std::string FrameText(const TTDTimePoint& t)
{
    return "f" + std::to_string(t.frame) + "+" + std::to_string(t.tInFrame);
}

/// The condition between a value before and after (mem kinds; Equals at the window's start is the caller's)
bool Holds(const TTDFindEventQuery& q, uint32_t before, uint32_t after)
{
    switch (q.cond)
    {
        case TTDFindEventCond::Changed: return before != after;
        case TTDFindEventCond::Increased: return after > before;
        case TTDFindEventCond::Decreased: return after < before;
        case TTDFindEventCond::Equals:
        case TTDFindEventCond::Becomes: return after == q.value && before != q.value;
        case TTDFindEventCond::Differs: return before == q.value && after != q.value;
        case TTDFindEventCond::BitSet: return !((before >> q.bit) & 1u) && ((after >> q.bit) & 1u);
        case TTDFindEventCond::BitClear: return ((before >> q.bit) & 1u) && !((after >> q.bit) & 1u);
        default: return false;
    }
}

/// The hits a scan keeps: the first `limit` (and whether more follow), or the last `limit` of everything seen
class HitList
{
public:
    HitList(bool last, uint32_t limit) : _last(last), _limit(limit) {}

    /// False: the list is full and the scan can stop (first order only)
    bool Add(TTDFindEventHit hit)
    {
        if (!_last)
        {
            if (_hits.size() < _limit)
            {
                _hits.push_back(std::move(hit));
                return true;
            }
            _more = true;
            _next = hit.time.frame;
            return false;
        }
        _hits.push_back(std::move(hit));
        if (_hits.size() > _limit)
        {
            _more = true;
            _next = _hits.front().time.frame;
            _hits.pop_front();
        }
        return true;
    }
    bool Full() const { return !_last && _more; }

    /// The kept hits in the asked order (newest first for `last`) and where to continue
    void Into(TTDFindEventResult& result)
    {
        result.more = _more;
        if (_last)
        {
            result.nextToFrame = _more ? _next : 0;
            result.hits.assign(std::make_move_iterator(_hits.rbegin()), std::make_move_iterator(_hits.rend()));
        }
        else
        {
            result.nextFromFrame = _more ? _next : 0;
            result.hits.assign(std::make_move_iterator(_hits.begin()), std::make_move_iterator(_hits.end()));
        }
    }

private:
    bool _last;
    uint32_t _limit;
    std::deque<TTDFindEventHit> _hits;
    bool _more = false;
    uint64_t _next = 0;
};
}  // namespace

const char* TTDFindEventKindName(TTDFindEventKind kind)
{
    for (const KindName& k : kKinds)
        if (k.kind == kind)
            return k.name;
    return "?";
}

const char* TTDFindEventCondName(TTDFindEventCond cond)
{
    for (const CondName& c : kConds)
        if (c.cond == cond)
            return c.name;
    return "?";
}

bool TTDFindEventKindByName(const std::string& name, TTDFindEventKind& out)
{
    for (const KindName& k : kKinds)
        if (name == k.name)
        {
            out = k.kind;
            return true;
        }
    return false;
}

bool TTDFindEventCondByName(const std::string& name, TTDFindEventCond& out)
{
    for (const CondName& c : kConds)
        if (name == c.name)
        {
            out = c.cond;
            return true;
        }
    return false;
}

std::string TTDFindEventText(const TTDFindEventQuery& query, const TTDFindEventResult& result)
{
    if (!result.error.empty())
        return "find event: " + result.error;
    std::string text;
    for (const TTDFindEventHit& h : result.hits)
    {
        text += FrameText(h.time);
        if (h.hasPc)
            text += " " + Hex(h.pc, 4);
        if (!h.text.empty())
            text += " " + h.text;
        text += "  " + h.why + "\n";
    }
    for (const std::string& b : result.barriers)
        text += "barrier: " + b + "\n";
    for (const std::string& m : result.missing)
        text += "missing: " + m + "\n";
    char buf[200];
    std::snprintf(buf, sizeof(buf), "%zu hit%s (%s %s) in frames %llu..%llu, %llu scanned, %llu replayed, %.1f ms",
                  result.hits.size(), result.hits.size() == 1 ? "" : "s", query.last ? "last" : "first",
                  TTDFindEventKindName(query.kind), static_cast<unsigned long long>(result.fromFrame),
                  static_cast<unsigned long long>(result.toFrame),
                  static_cast<unsigned long long>(result.framesScanned),
                  static_cast<unsigned long long>(result.framesReplayed), result.ms);
    text += buf;
    if (result.more)
        text += query.last ? "; more up to frame " + std::to_string(result.nextToFrame)
                           : "; more from frame " + std::to_string(result.nextFromFrame);
    return text + "\n";
}


TTDFindEventResult TimeTravelController::QueryFindEvent(const TTDFindEventQuery& query)
{
    const auto started = std::chrono::steady_clock::now();
    // Replays the hit frames and puts the machine back: an operation that changes it
    const SessionOperation op{*this, SessionOperation::Kind::Change};
    TTDFindEventResult result;
    auto finish = [&]() -> TTDFindEventResult& {
        result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return result;
    };

    if (_state == TTDSessionState::Recording)
        PauseRecordingForBrowsing();
    Memory* memory = _context ? _context->pMemory : nullptr;
    Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const LineageViewGuard lineageView{*this};   // with branches the walk views each lineage line it reads
    const LineageEngine engine{*this};
    const size_t firstCp = engine.FirstCheckpoint();
    const size_t cpCount = engine.CheckpointCount();
    if (_line->timeline.empty() || !memory || !z80 || cpCount < firstCp + 2)
    {
        result.error = "no recorded history";
        return finish();
    }

    // The window: the frames with a checkpoint at both ends
    const uint64_t heldFrom = engine.Checkpoint(firstCp)->position.frame;
    const uint64_t heldTo = engine.Checkpoint(cpCount - 1)->position.frame - 1;
    result.fromFrame = std::max(query.fromFrame, heldFrom);
    result.toFrame = std::min(query.toFrame, heldTo);
    if (result.fromFrame > result.toFrame)
    {
        result.error = "the window is outside the history (frames " + std::to_string(heldFrom) + ".." +
                       std::to_string(heldTo) + ")";
        return finish();
    }
    const int64_t firstIndexSigned = engine.CheckpointIndexOf({0, result.fromFrame, 0});
    if (firstIndexSigned < 0)
    {
        result.error = "no checkpoint at frame " + std::to_string(result.fromFrame);
        return finish();
    }
    const size_t firstIndex = static_cast<size_t>(firstIndexSigned);
    const uint32_t limit = std::clamp(query.limit, 1u, TTDFindEventQuery::kMaxLimit);

    uint32_t ramRegion = UINT32_MAX;
    const std::vector<TTDRegionDesc>& regions = engine.Regions();
    for (uint32_t r = 0; r < regions.size(); ++r)
        if (regions[r].id < TTDRegionId::DeviceStateFirst && regions[r].name == "ram")
        {
            ramRegion = r;
            break;
        }

    Z80Disassembler* disassembler = _context->pDebugManager ? _context->pDebugManager->GetDisassembler().get() : nullptr;
    auto insnText = [&](const uint8_t* bytes, uint16_t pc) -> std::string {
        if (!disassembler)
            return "";
        uint8_t len = 0;
        return disassembler->disassembleSingleCommand(std::vector<uint8_t>(bytes, bytes + 4), pc, &len);
    };
    auto noteBarrier = [&](const TTDExternalEvent& e) {
        const std::string text = "f" + std::to_string(e.time.frame) + "+" + std::to_string(e.time.tInFrame) + " " +
                                 std::string(e.reason, strnlen(e.reason, sizeof(e.reason)));
        if (result.barriers.size() < 16 && std::find(result.barriers.begin(), result.barriers.end(), text) == result.barriers.end())
            result.barriers.push_back(text);
    };
    // A marker inside the frame: the state jumped, its two ends are not compared
    auto barrierIn = [&](uint64_t frame) -> bool {
        if (const std::optional<TTDExternalEvent> b = FirstBarrierBetween({frame, 0}, {frame + 1, 0}))
        {
            noteBarrier(*b);
            return true;
        }
        return false;
    };

    // Every marker in the window is listed: the history jumps there (a comparison across it is skipped)
    {
        TTDTimePoint from{result.fromFrame, 0};
        const TTDTimePoint to{result.toFrame + 1, 0};
        while (result.barriers.size() < 16)
        {
            const std::optional<TTDExternalEvent> b = FirstBarrierBetween(from, to);
            if (!b || b->time < from)
                break;
            noteBarrier(*b);
            from = {b->time.frame, b->time.tInFrame + 1};
        }
    }

    // The live machine, put back after the first replay
    const TTDSessionState stateBefore = _state;
    LiveStateSnapshot live;   // not _liveSnapshot: FindAccesses saves the machine there itself
    bool saved = false;
    const EmulatorState& st = _context->emulatorState;
    // One frame replayed from its checkpoint with the instruction hook and (optionally) the bus hook
    auto replayFrame = [&](uint64_t frame, const std::function<void(uint16_t)>& m1,
                           const std::function<void(char, uint16_t, uint8_t)>& bus) -> bool {
        const int64_t ci = ViewLineageIndexAtOrBefore({frame, 0});
        const TTDCheckpoint* at = ci < 0 ? nullptr : ViewLineageCheckpoint(static_cast<size_t>(ci));
        if (!at || at->time.frame != frame)
            return false;
        const TTDCheckpoint& cp = *at;
        const TTDTimePoint end = SessionEndPosition();
        uint32_t endT = frame == end.frame ? end.tInFrame : FrameSpan();
        if (const std::optional<TTDExternalEvent> b = FirstBarrierBetween(cp.time, TTDTimePoint{frame, endT}))
            endT = b->time.frame == frame ? b->time.tInFrame : 0;
        if (endT == 0)
            return false;
        if (!saved)
        {
            SaveLiveState(live);
            saved = true;
        }
        RestoreCheckpointForReplay(cp);
        z80->m1TraceHook = m1;
        if (bus)
            z80->busTraceHook = bus;
        {
            ReplayModeScope replay(*this);
            ReplayWithinFrame(frame, endT);
            replay.Exit();
        }
        z80->m1TraceHook = nullptr;
        z80->busTraceHook = nullptr;
        ++result.framesReplayed;
        return true;
    };
    auto putBack = [&]() {
        if (!saved)
            return;
        ReplayModeScope restore(*this);
        RestoreLiveState(live);
        restore.Exit();
        SetState(stateBefore);
        saved = false;
    };
    // The hits in the asked order, at most `limit`; `next` is the frame of the first one left out
    auto takeOrdered = [&](std::vector<TTDFindEventHit>& ordered) {
        if (ordered.size() > limit)
        {
            result.more = true;
            (query.last ? result.nextToFrame : result.nextFromFrame) = ordered[limit].time.frame;
            ordered.resize(limit);
        }
        result.hits = std::move(ordered);
    };

    switch (query.kind)
    {
        case TTDFindEventKind::Mem:
        {
            if (ramRegion == UINT32_MAX)
            {
                result.error = "the session holds no RAM region";
                return finish();
            }
            if ((query.cond == TTDFindEventCond::BitSet || query.cond == TTDFindEventCond::BitClear) &&
                query.bit >= (query.size == 2 ? 16 : 8))
            {
                result.error = "bit " + std::to_string(query.bit) + " is outside the value";
                return finish();
            }
            if (query.cond == TTDFindEventCond::Change || query.cond == TTDFindEventCond::Cleared)
            {
                result.error = std::string("'") + TTDFindEventCondName(query.cond) + "' is a screen condition";
                return finish();
            }
            // The bytes: the page mapped at the current position unless one was given (a word may cross a bank)
            const uint8_t size = query.size == 2 ? 2 : 1;
            struct Byte
            {
                uint16_t address = 0;
                PhysPage page = kPhysPageNone;
                uint32_t offset = 0;   ///< in the RAM region
                uint8_t flip = 0;      ///< Memory::TtdKeyOf
            };
            Byte bytes[2];
            for (uint8_t k = 0; k < size; ++k)
            {
                const uint16_t a = static_cast<uint16_t>(query.address + k);
                const bool sameBank = (a >> 14) == (query.address >> 14);
                uint16_t inPage = static_cast<uint16_t>(a & 0x3FFF);
                const PhysPage page = query.hasPage && sameBank ? query.page : memory->TtdLocate(a, false, inPage);
                if (page == kPhysPageNone)
                {
                    result.error = "address " + Hex(a, 4) + " is ROM at the current position: give phys_page";
                    return finish();
                }
                bytes[k] = {a, page, static_cast<uint32_t>(page) * 0x4000u + inPage,
                            static_cast<uint8_t>(((inPage ^ a) >> 13) & 1u)};
                if (bytes[k].offset >= static_cast<uint64_t>(regions[ramRegion].pieces) * kTTDPieceSize)
                {
                    result.error = "page " + std::to_string(page) + " is not in the session's RAM";
                    return finish();
                }
            }
            result.page = bytes[0].page;
            auto valueOf = [&](const uint8_t* v) { return size == 2 ? uint32_t(v[0]) | (uint32_t(v[1]) << 8) : uint32_t(v[0]); };
            const std::string name = "(" + Hex(query.address, 4) + " RAM " + std::to_string(bytes[0].page) + ")";
            auto valueText = [&](uint32_t v) { return Hex(v, size == 2 ? 4 : 2); };
            auto why = [&](uint32_t before, uint32_t after) {
                return name + " " + valueText(before) + " -> " + valueText(after) + "  " + TTDFindEventCondName(query.cond);
            };

            // The value at the window's start
            uint8_t now[2] = {};
            {
                std::vector<uint8_t> ram(static_cast<size_t>(regions[ramRegion].pieces) * kTTDPieceSize);
                engine.RestoreRegion(firstIndex, ramRegion, ram.data());
                for (uint8_t k = 0; k < size; ++k)
                    now[k] = ram[bytes[k].offset];
            }
            HitList hits(query.last, limit);
            if (query.cond == TTDFindEventCond::Equals && valueOf(now) == query.value)
            {
                // Equals: already V when the window starts
                TTDFindEventHit h;
                h.time = {result.fromFrame, 0};
                h.hasValues = true;
                h.before = h.after = valueOf(now);
                h.why = name + " = " + valueText(h.after) + " when the window starts";
                hits.Add(std::move(h));
            }

            if (query.exact)
            {
                // Every CPU write in the window (FindAccesses lists them newest first, in chunks), then forward
                struct Write
                {
                    uint64_t globalT;
                    TTDTimePoint time;
                    uint16_t pc;
                    uint8_t k;   ///< which byte of the value
                    uint8_t value;
                };
                std::vector<Write> writes;
                const uint64_t endT = GlobalT({result.toFrame + 1, 0});
                const bool split = size == 2 && bytes[1].page != bytes[0].page;
                for (uint8_t qi = 0; qi < (split ? 2 : 1); ++qi)
                {
                    TTDSearchQuery q;
                    q.addrFrom = bytes[qi].address;
                    q.addrTo = split ? bytes[qi].address : bytes[size - 1].address;
                    q.access = TTDAccessType::Write;
                    q.hasPhysPageFilter = true;
                    q.physPage = bytes[qi].page;
                    q.flip = static_cast<int8_t>(bytes[qi].flip);
                    q.beforeGlobalT = endT == 0 ? 0 : endT - 1;
                    TTDFindOptions o;
                    o.all = true;
                    o.limit = 10000;
                    o.fromFrame = result.fromFrame;
                    for (;;)
                    {
                        const TTDFindResult found = FindAccesses(q, o);
                        if (found.blockingMarker.reason[0] != '\0')
                            noteBarrier(found.blockingMarker);
                        for (const TTDFoundAccess& f : found.hits)
                        {
                            const uint8_t k = size == 2 && f.access.addr == bytes[1].address ? 1 : 0;
                            writes.push_back({f.globalT, f.access.time, f.access.pc, k, f.access.value});
                        }
                        if (!found.truncated || found.hits.empty() || found.hits.back().globalT == 0)
                            break;
                        q.beforeGlobalT = found.hits.back().globalT - 1;
                    }
                }
                std::sort(writes.begin(), writes.end(), [](const Write& a, const Write& b) { return a.globalT < b.globalT; });
                result.framesScanned = result.toFrame - result.fromFrame + 1;
                for (size_t w = 0; w < writes.size() && !hits.Full(); ++w)
                {
                    const uint32_t before = valueOf(now);
                    now[writes[w].k] = writes[w].value;
                    // A word store's second byte by the same instruction: one change
                    if (size == 2 && w + 1 < writes.size() && writes[w + 1].pc == writes[w].pc &&
                        writes[w + 1].k != writes[w].k && writes[w + 1].globalT - writes[w].globalT < 8)
                    {
                        ++w;
                        now[writes[w].k] = writes[w].value;
                    }
                    const uint32_t after = valueOf(now);
                    if (!Holds(query, before, after))
                        continue;
                    TTDFindEventHit h;
                    h.time = writes[w].time;
                    h.hasPc = true;
                    h.pc = writes[w].pc;
                    h.hasValues = true;
                    h.before = before;
                    h.after = after;
                    h.why = why(before, after);
                    hits.Add(std::move(h));
                }
            }
            else
            {
                // Frame boundaries: the value after each frame from the pieces it changed, no replay
                std::vector<uint8_t> piece(kTTDPieceSize);
                for (size_t i = firstIndex + 1; i < cpCount && !hits.Full(); ++i)
                {
                    const TTDEngineCheckpoint* cp = engine.Checkpoint(i);
                    if (!cp || cp->position.frame > result.toFrame + 1)
                        break;
                    const uint64_t frame = cp->position.frame - 1;   // the frame that ends at this checkpoint
                    ++result.framesScanned;
                    if (engine.ChangeCount(i, ramRegion) == 0)
                        continue;
                    const uint32_t before = valueOf(now);
                    bool touched = false;
                    for (const auto& change : engine.ChangesAt(i, ramRegion))
                    {
                        bool decoded = false;
                        for (uint8_t k = 0; k < size; ++k)
                            if (change.first == bytes[k].offset / kTTDPieceSize)
                            {
                                if (!decoded && !engine.PieceStore().Decode(change.second, piece.data()))
                                    break;
                                decoded = true;
                                now[k] = piece[bytes[k].offset % kTTDPieceSize];
                                touched = true;
                            }
                    }
                    if (!touched || barrierIn(frame))
                        continue;
                    const uint32_t after = valueOf(now);
                    if (!Holds(query, before, after))
                        continue;
                    TTDFindEventHit h;
                    h.time = {frame, 0};
                    h.hasValues = true;
                    h.before = before;
                    h.after = after;
                    h.why = why(before, after);
                    hits.Add(std::move(h));
                }
            }

            hits.Into(result);
            // Each hit's frame replayed once: the instruction (exact) or the write that made the condition true
            for (TTDFindEventHit& h : result.hits)
            {
                if (!h.hasPc && h.before == h.after)
                    continue;   // equals when the window starts
                const bool exactHit = h.hasPc;
                uint8_t v[2] = {static_cast<uint8_t>(h.before), static_cast<uint8_t>(h.before >> 8)};
                struct Insn
                {
                    uint16_t pc = 0;
                    PhysPage page = kPhysPageNone;
                    uint8_t code[4] = {};
                    uint32_t tIn = 0;
                };
                Insn cur;
                Insn best;
                bool found = false;
                bool dirty = false;
                auto judge = [&]() {
                    // After the instruction's writes (a word store writes two bytes): does the condition hold now
                    if (!dirty || found)
                        return;
                    dirty = false;
                    if (Holds(query, h.before, valueOf(v)))
                    {
                        found = true;
                        best = cur;
                        h.time = {h.time.frame, cur.tIn};
                        h.after = valueOf(v);
                    }
                };
                replayFrame(
                    h.time.frame,
                    [&](uint16_t pc) {
                        judge();
                        cur.pc = pc;
                        cur.page = memory->TtdPageOf(pc);
                        for (uint8_t k = 0; k < 4; ++k)
                            cur.code[k] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(pc + k));
                        cur.tIn = st.TtdTInFrame(static_cast<uint32_t>(z80->t));
                        if (exactHit && !found && pc == h.pc && cur.tIn <= h.time.tInFrame)
                            best = cur;   // the last start of the PC before the write
                    },
                    exactHit ? std::function<void(char, uint16_t, uint8_t)>()
                             : [&](char type, uint16_t address, uint8_t value) {
                                   if (type != 'W' || found)
                                       return;
                                   for (uint8_t k = 0; k < size; ++k)
                                   {
                                       uint8_t flip = 0;
                                       if (address == bytes[k].address &&
                                           memory->TtdKeyOf(address, flip) == bytes[k].page && flip == bytes[k].flip)
                                       {
                                           v[k] = value;
                                           dirty = true;
                                       }
                                   }
                               });
                judge();
                if (exactHit)
                {
                    if (best.pc == h.pc)
                    {
                        h.codePage = best.page;
                        h.text = insnText(best.code, best.pc);
                    }
                    continue;
                }
                if (found)
                {
                    h.hasPc = true;
                    h.pc = best.pc;
                    h.codePage = best.page;
                    h.text = insnText(best.code, best.pc);
                    h.why = why(h.before, h.after);
                }
                else
                {
                    h.device = true;
                    h.why += "  (no CPU write in the frame: a device, DMA or a ROM trap)";
                }
            }
            break;
        }

        case TTDFindEventKind::Screen:
        {
            if (query.cond != TTDFindEventCond::Change && query.cond != TTDFindEventCond::Cleared)
            {
                result.error = std::string("'") + TTDFindEventCondName(query.cond) + "' is not a screen condition (change, cleared)";
                return finish();
            }
            const MEM_MODEL model = _context->config.mem_model;
            std::vector<TTDScreenRange> fallback;
            auto layoutAt = [&](size_t i) -> const std::vector<TTDScreenRange>* {
                const TTDEngineCheckpoint* cp = engine.Checkpoint(i);
                if (!cp)
                    return nullptr;
                if (const std::vector<TTDScreenRange>* l = engine.ScreenLayoutOf(*cp))
                    return l;
                if (!HasUlaScreen(model) || ramRegion == UINT32_MAX)
                    return nullptr;
                fallback = UlaScreenLayout(ramRegion, model, cp->chipset.p7FFD);
                return &fallback;
            };
            const std::vector<TTDScreenRange>* firstLayout = layoutAt(firstIndex);
            if (!firstLayout)
            {
                result.error = "the session has no screen layout for this model";
                return finish();
            }
            // The regions' bytes, kept current for the pieces any layout seen so far covers
            std::map<uint32_t, std::vector<uint8_t>> buffers;
            std::map<uint32_t, std::vector<bool>> tracked;
            auto track = [&](size_t i, const std::vector<TTDScreenRange>& layout) {
                for (const TTDScreenRange& r : layout)
                {
                    std::vector<bool>& t = tracked[r.region];
                    if (t.empty())
                        t.assign(regions[r.region].pieces, false);
                    bool fresh = false;
                    for (uint32_t p = r.begin / kTTDPieceSize; p < t.size() && p * kTTDPieceSize < r.end; ++p)
                        if (!t[p])
                        {
                            t[p] = true;
                            fresh = true;
                        }
                    if (fresh)
                    {
                        std::vector<uint8_t>& b = buffers[r.region];
                        b.assign(static_cast<size_t>(regions[r.region].pieces) * kTTDPieceSize, 0);
                        engine.RestoreRegion(i, r.region, b.data());
                    }
                }
            };
            auto picture = [&](const std::vector<TTDScreenRange>& layout, std::vector<uint8_t>& out) {
                out.clear();
                for (const TTDScreenRange& r : layout)
                {
                    const std::vector<uint8_t>& b = buffers[r.region];
                    const uint32_t end = std::min<uint32_t>(r.end, static_cast<uint32_t>(b.size()));
                    if (r.begin < end)
                        out.insert(out.end(), b.begin() + r.begin, b.begin() + end);
                }
            };
            auto pixelsOneValue = [](const std::vector<uint8_t>& p) {
                const size_t n = p.size() == 6912 ? 6144 : p.size();
                return n > 0 && std::all_of(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(n),
                                            [&](uint8_t b) { return b == p[0]; });
            };
            std::vector<TTDScreenRange> layout = *firstLayout;
            track(firstIndex, layout);
            std::vector<uint8_t> before;
            std::vector<uint8_t> current;   // the picture at the frame start
            picture(layout, current);
            std::vector<uint8_t> piece(kTTDPieceSize);
            HitList hits(query.last, limit);
            auto countDiff = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, size_t& attr) {
                const size_t n = std::min(a.size(), b.size());
                size_t diff = std::max(a.size(), b.size()) - n;
                attr = 0;
                for (size_t k = 0; k < n; ++k)
                    if (a[k] != b[k])
                    {
                        ++diff;
                        attr += b.size() == 6912 && k >= 6144 ? 1 : 0;
                    }
                return diff;
            };
            // A redraw that takes several frames (an LDIR of the whole screen runs two) is one change: consecutive
            // frames that each change at least kRunPercent of the picture make a run, judged from its first frame's
            // start to its last frame's end
            constexpr uint32_t kRunPercent = 5;
            const bool runs = query.cond == TTDFindEventCond::Change && query.minChanged > kRunPercent;
            bool inRun = false;
            uint64_t runFrom = 0;
            uint64_t runTo = 0;
            std::vector<uint8_t> runBefore;
            auto judge = [&](uint64_t from, uint64_t to, const std::vector<uint8_t>& was, const std::vector<uint8_t>& now) {
                if (now.empty())
                    return;
                size_t attrDiff = 0;
                const size_t diff = countDiff(was, now, attrDiff);
                const uint32_t percent = static_cast<uint32_t>(diff * 100 / now.size());
                char buf[200];
                if (query.cond == TTDFindEventCond::Cleared)
                {
                    if (!pixelsOneValue(now) || pixelsOneValue(was))
                        return;
                    std::snprintf(buf, sizeof(buf), "screen cleared: every pixel byte %s (%u %% changed)",
                                  Hex(now[0], 2).c_str(), percent);
                }
                else
                {
                    if (diff == 0 || percent < query.minChanged)
                        return;
                    if (now.size() == 6912)
                        std::snprintf(buf, sizeof(buf), "screen %u %% changed (%zu of %zu bytes; attributes %zu %%)",
                                      percent, diff, now.size(), attrDiff * 100 / 768);
                    else
                        std::snprintf(buf, sizeof(buf), "screen %u %% changed (%zu of %zu bytes)", percent, diff,
                                      now.size());
                }
                TTDFindEventHit h;
                h.time = {from, 0};
                h.hasValues = true;
                h.before = static_cast<uint32_t>(diff);
                h.after = static_cast<uint32_t>(now.size());
                h.why = std::string(buf) + (to > from ? " over frames " + std::to_string(from) + ".." + std::to_string(to)
                                                      : std::string(" during the frame"));
                hits.Add(std::move(h));
            };
            auto closeRun = [&]() {
                if (inRun)
                    judge(runFrom, runTo, runBefore, current);
                inRun = false;
            };
            for (size_t i = firstIndex + 1; i < cpCount && !hits.Full(); ++i)
            {
                const TTDEngineCheckpoint* cp = engine.Checkpoint(i);
                if (!cp || cp->position.frame > result.toFrame + 1)
                    break;
                const uint64_t frame = cp->position.frame - 1;
                ++result.framesScanned;
                const std::vector<TTDScreenRange>* l = layoutAt(i);
                const std::vector<TTDScreenRange> next = l ? *l : layout;
                // Only a changed tracked piece or a new layout can change the picture
                bool changed = !(next == layout);
                for (auto& [region, t] : tracked)
                    if (!changed && engine.ChangeCount(i, region) > 0)
                        for (const auto& change : engine.ChangesAt(i, region))
                            changed = changed || (change.first < t.size() && t[change.first]);
                if (!changed)
                {
                    closeRun();
                    continue;
                }
                for (auto& [region, t] : tracked)
                    if (engine.ChangeCount(i, region) > 0)
                    {
                        std::vector<uint8_t>& b = buffers[region];
                        for (const auto& change : engine.ChangesAt(i, region))
                        {
                            const size_t at = static_cast<size_t>(change.first) * kTTDPieceSize;
                            if (change.first < t.size() && t[change.first] && at + kTTDPieceSize <= b.size() &&
                                engine.PieceStore().Decode(change.second, piece.data()))
                                std::copy(piece.begin(), piece.end(), b.begin() + static_cast<std::ptrdiff_t>(at));
                        }
                    }
                track(i, next);   // a layout with pieces not seen yet: their regions read at this checkpoint
                before.swap(current);
                picture(next, current);
                layout = next;
                if (barrierIn(frame))
                {
                    inRun = false;   // the jump is no change of the picture
                    continue;
                }
                if (!runs)
                {
                    judge(frame, frame, before, current);
                    continue;
                }
                size_t attrDiff = 0;
                const size_t diff = countDiff(before, current, attrDiff);
                if (!current.empty() && diff * 100 / current.size() >= kRunPercent)
                {
                    if (!inRun)
                    {
                        inRun = true;
                        runFrom = frame;
                        runBefore = before;
                    }
                    runTo = frame;
                    continue;
                }
                closeRun();
            }
            closeRun();
            hits.Into(result);
            break;
        }

        case TTDFindEventKind::Exec:
        {
            const uint16_t from = query.address;
            const uint16_t to = std::max(query.address, query.addressTo);
            std::optional<PhysPage> page;
            if (query.hasPage)
                page = query.page;
            result.page = query.hasPage ? query.page : kPhysPageNone;
            auto runIn = [&](uint64_t frame, TTDFindEventHit& h) {
                bool found = false;
                replayFrame(
                    frame,
                    [&](uint16_t pc) {
                        if (pc < from || pc > to || (found && !query.last))
                            return;
                        const PhysPage codePage = memory->TtdPageOf(pc);
                        if (query.hasPage && codePage != query.page)
                            return;
                        uint8_t code[4];
                        for (uint8_t k = 0; k < 4; ++k)
                            code[k] = memory->DirectReadFromZ80Memory(static_cast<uint16_t>(pc + k));
                        found = true;
                        h.time = {frame, st.TtdTInFrame(static_cast<uint32_t>(z80->t))};
                        h.hasPc = true;
                        h.pc = pc;
                        h.codePage = codePage;
                        h.text = insnText(code, pc);
                        h.why = Hex(pc, 4) + " ran (" + (query.last ? "its last start" : "its first start") +
                                " in the frame)  A=" + Hex(z80->a, 2) + " HL=" + Hex(z80->hl, 4);
                    },
                    nullptr);
                return found;
            };
            std::vector<TTDFindEventHit> ordered;
            const TTDCoverageScanResult scan = QueryCoverageScan(result.fromFrame, result.toFrame, TTDCoverageKind::Executed,
                                                                 from, to, page, limit + 1, query.last, false);
            if (scan.indexAvailable && scan.coveredFrom <= result.fromFrame && result.toFrame <= scan.coveredTo)
            {
                result.framesScanned = scan.scannedFrames;
                for (size_t k = 0; k < scan.frames.size(); ++k)
                {
                    TTDFindEventHit h;
                    if (k < limit && runIn(scan.frames[k], h))
                        ordered.push_back(std::move(h));
                    else if (k >= limit)
                    {
                        // The one past the list: only its frame, for next_*
                        h.time = {scan.frames[k], 0};
                        ordered.push_back(std::move(h));
                    }
                }
            }
            else
            {
                result.missing.push_back(
                    "coverage: the index does not hold the window; frames replayed one by one (max_frames " +
                    std::to_string(query.maxFrames) + ")");
                const uint64_t span = result.toFrame - result.fromFrame + 1;
                const uint64_t count =
                    std::min<uint64_t>(span, std::clamp(query.maxFrames, 1u, TTDFindEventQuery::kMaxFramesReplayed));
                for (uint64_t k = 0; k < count && ordered.size() <= limit; ++k)
                {
                    const uint64_t frame = query.last ? result.toFrame - k : result.fromFrame + k;
                    ++result.framesScanned;
                    TTDFindEventHit h;
                    if (runIn(frame, h))
                        ordered.push_back(std::move(h));
                }
                if (count < span && ordered.size() <= limit)
                    result.missing.push_back("frames: " + std::to_string(span - count) + " of the window not searched");
            }
            takeOrdered(ordered);
            break;
        }

        case TTDFindEventKind::Port:
        case TTDFindEventKind::Border:
        {
            TTDPortQuery q;
            std::string err;
            const bool border = query.kind == TTDFindEventKind::Border;
            if (border)
            {
                if (!BuildPortEventQuery("border", "", q, err))
                {
                    result.error = err;
                    return finish();
                }
                q.trigger = TTDPortTrigger::Every;   // the change is judged here, from the colour at the window's start
            }
            else
            {
                q.direction = query.portOut ? TTDPortJournal::Direction::Write : TTDPortJournal::Direction::Read;
                q.portMask = query.portMask;
                q.portValue = static_cast<uint16_t>(query.port & query.portMask);
                if (query.hasValue)
                {
                    q.valueMatch = TTDValueMatch::Equals;
                    q.valueMask = query.valueMask;
                    q.value = static_cast<uint8_t>(query.value & query.valueMask);
                }
            }
            q.from = {result.fromFrame, 0};
            q.to = {result.toFrame, UINT32_MAX};
            result.framesScanned = result.toFrame - result.fromFrame + 1;
            auto portHit = [&](const TTDPortRecord& r, uint8_t colourBefore) {
                TTDFindEventHit h;
                h.time = {r.frame, r.tInFrame};
                h.hasPc = true;
                h.pc = r.pc;
                h.hasValues = true;
                h.after = border ? (r.value & 7u) : r.value;
                if (border)
                {
                    h.before = colourBefore;
                    h.why = "border " + std::to_string(colourBefore) + " -> " + std::to_string(r.value & 7u) + "  (OUT " +
                            Hex(r.port, 4) + "," + Hex(r.value, 2) + ")";
                }
                else
                    h.why = std::string(query.portOut ? "OUT " : "IN ") + Hex(r.port, 4) + "," + Hex(r.value, 2);
                return h;
            };
            std::vector<TTDFindEventHit> ordered;
            if (!border)
            {
                q.limit = limit + 1;
                q.newestFirst = query.last;
                const TTDPortSearchResult found = SearchPortEvents(q);
                if (!found.ok)
                {
                    result.error = found.error.empty() ? "the port search failed" : found.error;
                    return finish();
                }
                for (const TTDPortHit& p : found.hits)
                    ordered.push_back(portHit(p.record, 0));
                takeOrdered(ordered);
                break;
            }
            // Border: every OUT forward in chunks; a hit is a colour other than the one before (and the asked one)
            const TTDEngineCheckpoint* startCp = engine.Checkpoint(firstIndex);
            uint8_t colour = startCp ? static_cast<uint8_t>(startCp->chipset.border_attr & 7u) : 0;
            HitList hits(query.last, limit);
            q.limit = 10000;
            q.newestFirst = false;
            for (;;)
            {
                const TTDPortSearchResult found = SearchPortEvents(q);
                if (!found.ok)
                {
                    result.error = found.error.empty() ? "the port search failed" : found.error;
                    return finish();
                }
                for (const TTDPortHit& p : found.hits)
                {
                    const uint8_t c = static_cast<uint8_t>(p.record.value & 7u);
                    if (c == colour)
                        continue;
                    const uint8_t was = colour;
                    colour = c;
                    if (query.hasValue && c != (query.value & 7u))
                        continue;
                    if (!hits.Add(portHit(p.record, was)))
                        break;
                }
                if (!found.truncated || found.hits.empty() || hits.Full())
                    break;
                const TTDPortRecord& lastRec = found.hits.back().record;
                q.from = {lastRec.frame, lastRec.tInFrame + 1};
            }
            hits.Into(result);
            break;
        }

        case TTDFindEventKind::Key:
        {
            ZXKeysEnum want = ZXKEY_NONE;
            if (!query.key.empty())
            {
                want = DebugKeyboardManager::ResolveKeyName(query.key);
                if (want == ZXKEY_NONE)
                {
                    result.error = "unknown key '" + query.key + "'";
                    return finish();
                }
            }
            const std::vector<TTDInputEvent> events = LineageInputEvents();
            result.framesScanned = result.toFrame - result.fromFrame + 1;
            auto matches = [&](const TTDInputEvent& e) {
                return e.kind == TTDInputKind::Key && e.pressed == query.pressed && e.time.frame >= result.fromFrame &&
                       e.time.frame <= result.toFrame && (want == ZXKEY_NONE || e.key == static_cast<uint8_t>(want));
            };
            std::vector<TTDFindEventHit> ordered;
            auto add = [&](const TTDInputEvent& e) {
                TTDFindEventHit h;
                h.time = e.time;
                const ZXKeysEnum key = static_cast<ZXKeysEnum>(e.key);
                h.why = DebugKeyboardManager::GetKeyDisplayName(key) + (e.pressed ? " pressed" : " released");
                if (e.pressed && ordered.size() < limit)
                {
                    // When the program saw it: the first read of the key's half-row alone with the key down
                    TTDPortQuery q;
                    std::string err;
                    const std::string name = !query.key.empty() ? query.key : DebugKeyboardManager::GetKeyDisplayName(key);
                    if (BuildPortEventQuery("key", name, q, err))
                    {
                        q.from = e.time;
                        q.to = {std::min(e.time.frame + 50, result.toFrame), UINT32_MAX};
                        q.limit = 1;
                        const TTDPortSearchResult seen = SearchPortEvents(q);
                        if (seen.ok && !seen.hits.empty())
                            h.why += "; the program read it at " + FrameText(seen.hits[0].record.Time()) + " (" +
                                     Hex(seen.hits[0].record.pc, 4) + ")";
                    }
                }
                ordered.push_back(std::move(h));
                return ordered.size() <= limit;
            };
            if (query.last)
            {
                for (auto it = events.rbegin(); it != events.rend(); ++it)
                    if (matches(*it) && !add(*it))
                        break;
            }
            else
            {
                for (const TTDInputEvent& e : events)
                    if (matches(e) && !add(e))
                        break;
            }
            takeOrdered(ordered);
            break;
        }
    }

    putBack();
    return finish();
}

}  // namespace ttd
