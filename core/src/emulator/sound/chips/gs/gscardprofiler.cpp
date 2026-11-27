#include "emulator/sound/chips/gs/gscardprofiler.h"

#include <algorithm>

#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace
{
/// CALL nn, CALL cc,nn, RST p; a DD / FD prefix in front of one is ignored by the CPU
bool IsCall(uint8_t op0, uint8_t op1)
{
    const uint8_t op = (op0 == 0xDD || op0 == 0xFD) ? op1 : op0;
    return op == 0xCD || (op & 0xC7) == 0xC4 || (op & 0xC7) == 0xC7;
}

uint32_t Clamp32(int64_t v)
{
    if (v <= 0)
        return 0;
    return v > int64_t(UINT32_MAX) ? UINT32_MAX : static_cast<uint32_t>(v);
}

uint16_t Clamp16(uint64_t v)
{
    return v > 0xFFFF ? uint16_t(0xFFFF) : static_cast<uint16_t>(v);
}
}  // namespace

GSCardProfiler::~GSCardProfiler() = default;

void GSCardProfiler::Start()
{
    Clear();
    _on.store(true, std::memory_order_relaxed);
}

void GSCardProfiler::Clear()
{
    _clearRequested.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> lock(_mutex);
    _published = GSProfileSnapshot();
}

bool GSCardProfiler::LastActivity(GSProfileActivity& out) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_published.activity.empty())
        return false;
    out = _published.activity.back();
    return true;
}

GSProfileSnapshot GSCardProfiler::Snapshot() const
{
    GSProfileSnapshot copy;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        copy = _published;
    }
    copy.on = On();
    return copy;
}

void GSCardProfiler::ResetWork()
{
    if (!_work)
    {
        _work = std::make_unique<Work>();
        _work->routineSlots.resize(kRoutineSlots);
        _work->marks.reserve(1024);
        _work->activity.reserve(kActivityFrames);
    }
    else
    {
        std::vector<Slot> routineSlots = std::move(_work->routineSlots);
        std::vector<GSProfileIsrMark> marks = std::move(_work->marks);
        std::vector<GSProfileActivity> activity = std::move(_work->activity);
        *_work = Work();
        std::fill(routineSlots.begin(), routineSlots.end(), Slot());
        marks.clear();
        activity.clear();
        _work->routineSlots = std::move(routineSlots);
        _work->marks = std::move(marks);
        _work->activity = std::move(activity);
    }
    if (_counters)
    {
        Work& w = *_work;
        w.cmdRead = _counters->cardCommandsRead;
        w.dataIn = _counters->cardDataIn;
        w.polls = _counters->cardStatusPolls;
        w.acksAtRead = _counters->cardCommandAcks;
        w.coalesced = _counters->interruptsCoalesced;
    }
}

void GSCardProfiler::Resync()
{
    Work& w = *_work;
    // The frames are dropped unmeasured; the interrupt time they held is not the frame's
    if (w.isrDepth > 0)
    {
        w.isrWallTotal += std::max<int64_t>(0, w.prevNow - w.isrOpenAt);
        w.frameIsr += std::max<int64_t>(0, w.prevNow - w.isrFrameFrom);
    }
    w.depth = 0;
    w.isrDepth = 0;
    w.prevValid = false;
    w.segOpen = false;
    w.resyncs++;
}

void GSCardProfiler::RunStart(int64_t now, int64_t anchor, int64_t period, int64_t unitsPerCycle)
{
    // The clear request is taken on every start (a first run too: left set, it would wipe the first frame later)
    const bool clear = _clearRequested.exchange(false, std::memory_order_acq_rel);
    if (!_work || clear)
    {
        ResetWork();
        _work->lastEnd = now;
        _work->frameStart = now;
    }
    Work& w = *_work;
    if (w.lastEnd != now)
    {
        // Time moved without the profiler (a TTD seek or replay, a reset, a run while it was off): the stack is gone
        Resync();
        if (w.frameStart < 0 || w.frameStart > now)
            w.frameStart = now;
    }
    if (w.frameStart < 0)
        w.frameStart = now;
    w.anchor = anchor;
    w.period = period;
    w.upc = unitsPerCycle > 0 ? unitsPerCycle : 1;
    if (_counters && _counters->cardCommandsRead < w.cmdRead)
    {
        // The activity counters were reset: take them as they are now
        w.cmdRead = _counters->cardCommandsRead;
        w.dataIn = _counters->cardDataIn;
        w.polls = _counters->cardStatusPolls;
        w.acksAtRead = _counters->cardCommandAcks;
        w.coalesced = _counters->interruptsCoalesced;
    }
}

uint32_t GSCardProfiler::Cycles(int64_t units) const
{
    return Clamp32(units / _work->upc);
}

int64_t GSCardProfiler::PeriodStart(int64_t t) const
{
    const Work& w = *_work;
    if (w.period <= 0)
        return t;
    int64_t k = (t - w.anchor) / w.period;
    if ((t - w.anchor) % w.period < 0)
        k--;
    return w.anchor + k * w.period;
}

int64_t GSCardProfiler::IsrWallAt(int64_t now) const
{
    const Work& w = *_work;
    return w.isrWallTotal + (w.isrDepth > 0 ? std::max<int64_t>(0, now - w.isrOpenAt) : 0);
}

int32_t GSCardProfiler::RoutineSlot(uint16_t pc)
{
    Work& w = *_work;
    const GeneralSoundCard::CardPage page = _card ? _card->mapCardAddress(pc) : GeneralSoundCard::CardPage();
    const uint32_t key = 1u + ((static_cast<uint32_t>(page.kind) << 24) | (static_cast<uint32_t>(page.page) << 16) | pc);
    uint32_t index = (key * 2654435761u) >> (32 - 11);
    for (uint32_t probe = 0; probe < kRoutineSlots; probe++, index = (index + 1) & (kRoutineSlots - 1))
    {
        Slot& slot = w.routineSlots[index];
        if (slot.key == key)
            return static_cast<int32_t>(index);
        if (slot.key == 0)
        {
            if (w.used >= kMaxRoutines)
                break;
            slot.key = key;
            slot.r = GSProfileRoutine();
            slot.r.pc = pc;
            slot.r.kind = static_cast<uint8_t>(page.kind);
            slot.r.page = page.page;
            w.used++;
            return static_cast<int32_t>(index);
        }
    }
    w.routinesDropped++;
    return -1;
}

void GSCardProfiler::Push(uint16_t pc, uint16_t sp, int64_t start, bool isr, bool nmi)
{
    Work& w = *_work;
    if (isr)
    {
        if (w.isrDepth == 0)
        {
            w.isrOpenAt = start;
            w.isrFrameFrom = start;
        }
        w.isrDepth++;
    }
    if (w.depth >= kMaxDepth)
    {
        // Untracked: the watermark still ends the frames below. An interrupt frame lost here leaves its depth
        // counted until a resync; deeper than 64 does not happen in card firmware
        w.stackOverflows++;
        if (isr)
            w.isrDepth--;
        return;
    }
    Frame& f = w.stack[w.depth++];
    f = Frame();
    f.start = start;
    f.retSp = sp;
    f.isr = isr;
    f.nmi = nmi;
    f.routine = RoutineSlot(pc);
    if (isr)
    {
        if (w.marks.size() < kMaxIsrsPerFrame)
        {
            f.mapIndex = static_cast<int32_t>(w.marks.size());
            w.marks.push_back(GSProfileIsrMark{ start, -1, pc, nmi });
        }
        else
            w.marksTruncated = true;
    }
}

void GSCardProfiler::Pop(int64_t now)
{
    Work& w = *_work;
    Frame f = w.stack[--w.depth];
    const int64_t elapsed = std::max<int64_t>(0, now - f.start);
    const int64_t inclusive = std::max<int64_t>(0, elapsed - f.stolen);
    const int64_t exclusive = std::max<int64_t>(0, inclusive - f.child);
    if (f.routine >= 0)
    {
        GSProfileRoutine& r = w.routineSlots[static_cast<size_t>(f.routine)].r;
        const uint32_t incl = Cycles(inclusive);
        r.min = r.calls == 0 || incl < r.min ? incl : r.min;
        r.max = std::max(r.max, incl);
        r.calls++;
        r.interrupts += f.isr ? 1 : 0;
        r.inclusive += incl;
        r.exclusive += Cycles(exclusive);
    }
    if (w.depth > 0)
    {
        Frame& parent = w.stack[w.depth - 1];
        if (f.isr)
            parent.stolen += elapsed;
        else
        {
            parent.child += inclusive;
            parent.stolen += f.stolen;
        }
    }
    if (!f.isr)
        return;

    // The interrupt ended: its length against its period
    const uint32_t length = Cycles(elapsed);
    if (f.nmi)
    {
        w.isrTotal.nmis++;
        w.isrFrame.nmis++;
    }
    else
    {
        const int64_t periodStart = PeriodStart(f.start);
        const uint32_t latency = Cycles(f.start - periodStart);
        const bool overrun = w.period > 0 && now > periodStart + w.period;
        w.isrTotal.Add(length, latency, overrun);
        w.isrFrame.Add(length, latency, overrun);
    }
    if (f.mapIndex >= 0 && static_cast<size_t>(f.mapIndex) < w.marks.size())
        w.marks[static_cast<size_t>(f.mapIndex)].end = now;
    if (--w.isrDepth <= 0)
    {
        w.isrDepth = 0;
        w.isrWallTotal += std::max<int64_t>(0, now - w.isrOpenAt);
        w.frameIsr += std::max<int64_t>(0, now - w.isrFrameFrom);
    }
}

void GSCardProfiler::Transition(uint16_t pc, uint16_t sp, int64_t now)
{
    Work& w = *_work;
    // Returns: every frame whose return address slot is below SP now (the watermark)
    while (w.depth > 0 && sp > w.stack[w.depth - 1].retSp)
        Pop(now);
    // A taken call: SP down by the return address, the instruction a CALL / RST
    if (sp == static_cast<uint16_t>(w.prevSp - 2) && IsCall(w.prevOp0, w.prevOp1))
        Push(pc, sp, now, false, false);
}

void GSCardProfiler::Interrupt(bool nmi, int64_t now, int64_t ackUnits, uint16_t handlerPc, uint16_t sp, uint16_t retPc,
                               int64_t unitsPerCycle)
{
    if (!_work)
        return;
    Work& w = *_work;
    w.upc = unitsPerCycle > 0 ? unitsPerCycle : 1;
    const int64_t ackStart = now - std::max<int64_t>(0, ackUnits);
    if (w.prevValid)
    {
        // The instruction before the acknowledge first: its call / return, its mailbox read
        if (w.prevHalted)
            w.frameHalt += std::max<int64_t>(0, ackStart - w.prevNow);
        const uint16_t spBefore = static_cast<uint16_t>(sp + 2);
        if (spBefore != w.prevSp)
            Transition(retPc, spBefore, ackStart);
        if (_counters && MailboxMoved())
            Mailbox(ackStart);
    }
    if (!nmi)
        w.frameInterrupts++;
    Push(handlerPc, sp, ackStart, true, nmi);
    w.prevValid = false;
}

void GSCardProfiler::OpenSegment(int64_t now)
{
    Work& w = *_work;
    if (w.segOpen || !w.cmdOpen)
        return;
    w.segOpen = true;
    w.segStart = now;
    w.segIsr = IsrWallAt(now);
}

void GSCardProfiler::CloseSegment(int64_t now)
{
    Work& w = *_work;
    if (!w.segOpen)
        return;
    w.segOpen = false;
    const int64_t busy = (now - w.segStart) - (IsrWallAt(now) - w.segIsr);
    w.cmdUnits += std::max<int64_t>(0, busy);
}

void GSCardProfiler::FinishCommand()
{
    Work& w = *_work;
    if (!w.cmdOpen)
        return;
    w.cmdOpen = false;
    GSProfileCommand& c = w.commands[w.cmdCode];
    const uint32_t cycles = Cycles(w.cmdUnits);
    c.min = c.count == 0 || cycles < c.min ? cycles : c.min;
    c.max = std::max(c.max, cycles);
    c.count++;
    c.cycles += cycles;
}

void GSCardProfiler::Mailbox(int64_t now)
{
    Work& w = *_work;
    const GSActivityCounters& c = *_counters;
    if (c.cardCommandsRead != w.cmdRead)
    {
        w.cmdRead = c.cardCommandsRead;
        const uint8_t code = _card ? _card->getCommandFromHost() : 0;
        // A read of the same command before its acknowledge (RSCOM) is the same command
        const bool same = w.cmdOpen && code == w.cmdCode && c.cardCommandAcks == w.acksAtRead;
        w.acksAtRead = c.cardCommandAcks;
        if (!same)
        {
            CloseSegment(now);
            FinishCommand();
            w.cmdOpen = true;
            w.cmdCode = code;
            w.cmdUnits = 0;
            w.frameCommands++;
        }
        OpenSegment(now);
    }
    if (c.cardDataIn != w.dataIn)
    {
        w.dataIn = c.cardDataIn;
        OpenSegment(now);
    }
    if (c.cardStatusPolls != w.polls)
    {
        w.polls = c.cardStatusPolls;
        CloseSegment(now);
    }
}

void GSCardProfiler::FrameEnd(int64_t now, uint64_t hostFrame)
{
    if (!_work)
        return;
    Work& w = *_work;
    if (w.lastEnd != now || w.frameStart < 0 || now < w.frameStart)
    {
        // The frame's last run was not profiled (a replay, the profiler just turned on): nothing to show for it
        w.frameStart = now;
        w.frameIsr = w.frameIdle = w.frameHalt = 0;
        w.frameCommands = w.frameInterrupts = 0;
        w.isrFrame = GSProfileIsrStats();
        w.marks.clear();
        w.marksTruncated = false;
        return;
    }
    if (w.isrDepth > 0)
    {
        w.frameIsr += std::max<int64_t>(0, now - w.isrFrameFrom);
        w.isrFrameFrom = now;
    }
    if (w.prevValid && w.prevHalted)
    {
        w.frameHalt += std::max<int64_t>(0, now - w.prevNow);
        w.prevNow = now;
    }

    GSProfileActivity a;
    a.hostFrame = hostFrame;
    a.cycles = Cycles(now - w.frameStart);
    a.isrCycles = Cycles(w.frameIsr);
    a.idleCycles = Cycles(w.frameIdle);
    a.haltCycles = Cycles(w.frameHalt);
    a.isrMax = w.isrFrame.max;
    a.commands = Clamp16(w.frameCommands);
    a.interrupts = Clamp16(w.frameInterrupts);
    a.overruns = Clamp16(w.isrFrame.overruns);
    if (_counters)
    {
        const uint64_t coalesced = _counters->interruptsCoalesced;
        const uint64_t lost = coalesced >= w.coalesced ? coalesced - w.coalesced : 0;
        w.coalesced = coalesced;
        a.lost = Clamp16(lost);
        w.isrFrame.lost = lost;
        w.isrTotal.lost += lost;
    }
    if (w.activity.size() < kActivityFrames)
        w.activity.push_back(a);
    else
    {
        w.activity[w.activityHead] = a;
        w.activityHead = (w.activityHead + 1) % kActivityFrames;
    }
    w.frames++;
    w.cycles += a.cycles;

    // The frame map: the interrupts acknowledged in the frame; the ones still running stay open (end -1) and are not
    // the next frame's
    GSProfileFrameMap map;
    map.valid = true;
    map.hostFrame = hostFrame;
    map.start = w.frameStart;
    map.end = now;
    map.anchor = w.anchor;
    map.period = w.period;
    map.unitsPerCycle = w.upc;
    map.truncated = w.marksTruncated;
    map.isrs.reserve(w.marks.size());
    for (const GSProfileIsrMark& m : w.marks)
        if (m.accept >= w.frameStart)
            map.isrs.push_back(m);
    for (int i = 0; i < w.depth; i++)
        w.stack[i].mapIndex = -1;
    w.marks.clear();
    w.marksTruncated = false;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _published.frames = w.frames;
        _published.cycles = w.cycles;
        _published.isrTotal = w.isrTotal;
        _published.isrLastFrame = w.isrFrame;
        _published.routines.clear();
        _published.routines.reserve(w.used);
        for (const Slot& s : w.routineSlots)
            if (s.key != 0)
                _published.routines.push_back(s.r);
        _published.commands = w.commands;
        _published.commandOpen = w.cmdOpen;
        _published.commandCode = w.cmdCode;
        int64_t open = w.cmdUnits;
        if (w.segOpen)
            open += std::max<int64_t>(0, (now - w.segStart) - (IsrWallAt(now) - w.segIsr));
        _published.commandCycles = Cycles(open);
        _published.activity.clear();
        _published.activity.reserve(w.activity.size());
        for (size_t i = 0; i < w.activity.size(); i++)
            _published.activity.push_back(w.activity[(w.activityHead + i) % w.activity.size()]);
        _published.map = std::move(map);
        _published.periodUnits = w.period;
        _published.unitsPerCycle = w.upc;
        _published.stackOverflows = w.stackOverflows;
        _published.routinesDropped = w.routinesDropped;
        _published.resyncs = w.resyncs;
    }

    w.frameStart = now;
    w.frameIsr = w.frameIdle = w.frameHalt = 0;
    w.frameCommands = w.frameInterrupts = 0;
    w.isrFrame = GSProfileIsrStats();
}
