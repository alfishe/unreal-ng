#pragma once

/// @file gscardprofiler.h
/// @brief The card CPU's profiler (gs-debugger X9, X2, X20; design §8A.3, §8A.6): the ISR budget per interrupt
/// period, the cost of each host command, a routine profiler with firmware names, the interrupt periods of the last
/// complete host frame (the event viewer's ISR bars) and per-frame activity, for the General Sound cards that run a
/// real Z80 (classic GS, the ZX-MultiSound's GS, NeoGS).
///
/// **Off by default, nothing on the hot path.** The card asks `On()` once a run (a relaxed load next to the execution
/// history's test); only while it is on does the card run its record loop (GSCardRunner::runToRecord, the loop the
/// execution history uses) and feed Step before each instruction and Interrupt after each acknowledge. The fast loop
/// is untouched.
///
/// **What is measured** (all in card CPU cycles: card time / units per cycle at the moment, NeoGS by its clock):
/// - **A frame of the call stack** is pushed by a taken CALL / CALL cc / RST (the instruction before the boundary is
///   one and SP went down by 2) and by an accepted INT or NMI (an interrupt frame, its start the acknowledge's start).
///   A frame ends at the first boundary where SP is above the slot of its return address (RET, RETI, RETN, a POP of
///   the return address, a stack reload): the SP watermark, so any way out of a routine ends it.
/// - **The ISR's length** = from the INT acknowledge's start to the boundary after the instruction that popped its
///   return address (EI + RET, RETI ...), nested interrupts included (they run inside it). Its **latency** = from the
///   period's start (the INT request) to the acknowledge. **Overrun**: the ISR ended after its period's end (the next
///   request came while it ran). `lost`: the requests that merged into a pending one (interruptsCoalesced).
/// - **Inclusive** time of a routine = its frame's time minus the interrupt frames inside it (never leaks across
///   interrupts); **exclusive** = inclusive minus its callees' inclusive. An interrupt frame is its own root: its
///   handler is a routine whose time excludes only the interrupts nested in it.
/// - **A command's cost** = the card's busy cycles on it: from the card's read of the command (COMRG #01) and from
///   each data byte it takes (DATRG #02) to its next status poll (FLAGS #04, the firmware back in a wait loop), minus
///   interrupt time, summed until the next command read. The wait loops for the host's bytes do not count.
/// - **Idle** = cycles the runner slept through the firmware's idle poll loop; **halted** = cycles in HALT.
///
/// **No load on emulation:** bounded memory (64 frames of stack, 1,536 routines, 4,096 ISRs a frame, 256 frames of
/// activity, allocated on the emulation thread at the first profiled run); the emulation thread publishes a copy once
/// a frame under a short lock; readers copy that copy (Snapshot) and never wait for the emulation.
/// Statistics, not machine state: nothing here is in a TTD blob; a TTD replay is not profiled (the stack is re-synced
/// at the next profiled run).

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "emulator/sound/chips/gs/gsporttrace.h"  // GSActivityCounters

class GeneralSoundCard;

/// The ISR budget over a range of interrupts (cycles)
struct GSProfileIsrStats
{
    uint64_t count = 0;       ///< INTs that ended (NMIs apart)
    uint64_t cycles = 0;      ///< their lengths summed
    uint32_t min = 0;
    uint32_t max = 0;
    uint64_t overruns = 0;
    uint64_t latencySum = 0;
    uint32_t latencyMax = 0;
    uint64_t nmis = 0;        ///< NMIs that ended
    uint64_t lost = 0;        ///< requests merged into a pending one (interruptsCoalesced)

    void Add(uint32_t length, uint32_t latency, bool overrun)
    {
        min = count == 0 || length < min ? length : min;
        max = length > max ? length : max;
        count++;
        cycles += length;
        overruns += overrun ? 1 : 0;
        latencySum += latency;
        latencyMax = latency > latencyMax ? latency : latencyMax;
    }
};

/// One routine: an entry address on a page (cycles)
struct GSProfileRoutine
{
    uint16_t pc = 0;
    uint8_t kind = 0xFF;      ///< GeneralSoundCard::CardPage::Kind: 0 ROM (flash), 1 RAM, 0xFF unmapped
    uint8_t page = 0;
    uint64_t calls = 0;       ///< frames that ended
    uint64_t interrupts = 0;  ///< of them interrupt frames (the routine is a handler)
    uint64_t inclusive = 0;
    uint64_t exclusive = 0;
    uint32_t min = 0;         ///< inclusive per call
    uint32_t max = 0;
};

/// One host command code (cycles)
struct GSProfileCommand
{
    uint64_t count = 0;
    uint64_t cycles = 0;
    uint32_t min = 0;
    uint32_t max = 0;
};

/// An interrupt of the frame map: card time in the card's unit
struct GSProfileIsrMark
{
    int64_t accept = 0;   ///< the acknowledge's start
    int64_t end = -1;     ///< the boundary after the return; -1: still running at the frame's end
    uint16_t entry = 0;   ///< the handler's address
    bool nmi = false;
};

/// The interrupts of one host frame (the event viewer's rows come from `anchor` / `period`)
struct GSProfileFrameMap
{
    bool valid = false;
    uint64_t hostFrame = 0;   ///< the host frame number
    int64_t start = 0;        ///< card time (units) of the frame's start and end
    int64_t end = 0;
    int64_t anchor = 0;       ///< a period start (units): periods start at anchor + k * period
    int64_t period = 0;       ///< units
    int64_t unitsPerCycle = 1;
    bool truncated = false;   ///< more than kMaxIsrsPerFrame interrupts: the rest not kept
    std::vector<GSProfileIsrMark> isrs; ///< in acknowledge order; interrupts acknowledged in the frame
};

/// One host frame of activity (cycles)
struct GSProfileActivity
{
    uint64_t hostFrame = 0;
    uint32_t cycles = 0;      ///< the frame's card cycles
    uint32_t isrCycles = 0;   ///< in interrupt frames (nested ones once)
    uint32_t idleCycles = 0;  ///< the idle poll loop slept through
    uint32_t haltCycles = 0;
    uint32_t isrMax = 0;      ///< the longest ISR that ended in the frame
    uint16_t commands = 0;    ///< host commands the card took
    uint16_t interrupts = 0;  ///< INTs acknowledged
    uint16_t overruns = 0;
    uint16_t lost = 0;
};

/// What a reader gets (GSCardProfiler::Snapshot): the copy published at the last frame end
struct GSProfileSnapshot
{
    bool on = false;
    uint64_t frames = 0;              ///< frames profiled since the session started (or the clear)
    uint64_t cycles = 0;              ///< their card cycles
    GSProfileIsrStats isrTotal;       ///< since the start / clear
    GSProfileIsrStats isrLastFrame;
    std::vector<GSProfileRoutine> routines;
    std::array<GSProfileCommand, 256> commands{};
    bool commandOpen = false;         ///< the last command the card took, still the current one
    uint8_t commandCode = 0;
    uint64_t commandCycles = 0;       ///< its cost so far
    std::vector<GSProfileActivity> activity; ///< oldest first, at most kActivityFrames
    GSProfileFrameMap map;            ///< the last complete frame
    int64_t periodUnits = 0;          ///< the interrupt period now
    int64_t unitsPerCycle = 1;
    uint64_t stackOverflows = 0;      ///< calls deeper than kMaxDepth (not tracked)
    uint64_t routinesDropped = 0;     ///< calls of routines past the table's capacity (not tracked)
    uint64_t resyncs = 0;             ///< the stack dropped: a time jump (TTD seek, reset, a run not profiled)
};

class GSCardProfiler
{
public:
    static constexpr int kMaxDepth = 64;
    static constexpr uint32_t kRoutineSlots = 2048;   ///< power of two
    static constexpr uint32_t kMaxRoutines = 1536;
    static constexpr size_t kActivityFrames = 256;
    static constexpr size_t kMaxIsrsPerFrame = 4096;

    GSCardProfiler() = default;
    GSCardProfiler(const GSCardProfiler&) = delete;
    GSCardProfiler& operator=(const GSCardProfiler&) = delete;
    ~GSCardProfiler();

    /// The card it profiles and its counters (the mailbox counters give the command reads and polls). Construction
    void Bind(GeneralSoundCard* card, const GSActivityCounters* counters)
    {
        _card = card;
        _counters = counters;
    }

    /// region <Any thread>
    /// A new session: cleared, on from the next card run
    void Start();
    /// Off from the next card run; the data stay readable
    void Stop() { _on.store(false, std::memory_order_relaxed); }
    /// Everything zeroed (the published copy now, the working state at the next run)
    void Clear();
    bool On() const { return _on.load(std::memory_order_relaxed); }
    /// The working state exists (a run was profiled since construction); emulation thread, tests
    bool HasWork() const { return _work != nullptr; }
    /// The copy of the last frame end (a short lock; never waits for the emulation)
    GSProfileSnapshot Snapshot() const;
    /// endregion

    /// region <Emulation thread: the card>
    /// Before a profiled run, the card on a boundary at `now`: the period grid now (units), units per cycle
    void RunStart(int64_t now, int64_t anchor, int64_t period, int64_t unitsPerCycle);
    /// After a profiled run
    void RunEnd(int64_t now)
    {
        if (_work)
            _work->lastEnd = now;
    }

    /// Before the instruction at `pc`: SP, its first two bytes, the time (units), the HALT latch, units per cycle
    void Step(uint16_t pc, uint16_t sp, uint8_t op0, uint8_t op1, int64_t now, bool halted, int64_t unitsPerCycle)
    {
        Work& w = *_work;
        w.upc = unitsPerCycle > 0 ? unitsPerCycle : 1;
        if (w.prevValid) [[likely]]
        {
            if (w.prevHalted) [[unlikely]]
                w.frameHalt += now - w.prevNow;
            if (sp != w.prevSp) [[unlikely]]
                Transition(pc, sp, now);
            if (_counters && MailboxMoved()) [[unlikely]]
                Mailbox(now);
        }
        w.prevValid = true;
        w.prevPc = pc;
        w.prevSp = sp;
        w.prevOp0 = op0;
        w.prevOp1 = op1;
        w.prevNow = now;
        w.prevHalted = halted;
    }

    /// An INT or NMI acknowledged (`now` after it, `ackUnits` its length): the handler at `handlerPc`, SP after the
    /// push, the return address it pushed
    void Interrupt(bool nmi, int64_t now, int64_t ackUnits, uint16_t handlerPc, uint16_t sp, uint16_t retPc,
                   int64_t unitsPerCycle);
    /// The idle loop slept from `start` to `end` (units)
    void Idle(int64_t start, int64_t end)
    {
        if (_work && end > start)
            _work->frameIdle += end - start;
    }
    /// The host frame `hostFrame` ended with the card at `now`: one activity row, the frame map, the copy published
    void FrameEnd(int64_t now, uint64_t hostFrame);
    /// endregion

private:
    struct Frame
    {
        int64_t start = 0;
        int64_t stolen = 0;   ///< interrupt time inside (units)
        int64_t child = 0;    ///< callees' inclusive (units)
        int32_t routine = -1; ///< slot, -1 untracked
        int32_t mapIndex = -1;
        uint16_t retSp = 0;
        bool isr = false;
        bool nmi = false;
    };

    struct Slot
    {
        uint32_t key = 0;     ///< 0 empty; else 1 + (kind << 24 | page << 16 | pc)
        GSProfileRoutine r;
    };

    struct Work
    {
        // The stack
        Frame stack[kMaxDepth];
        int depth = 0;
        int isrDepth = 0;
        int64_t isrOpenAt = 0;     ///< the outermost interrupt frame's start
        int64_t isrFrameFrom = 0;  ///< ... or the frame's start, for the frame's share
        int64_t isrWallTotal = 0;  ///< closed outermost interrupt frames, summed (units)

        // The previous boundary
        bool prevValid = false;
        bool prevHalted = false;
        uint16_t prevPc = 0;
        uint16_t prevSp = 0;
        uint8_t prevOp0 = 0;
        uint8_t prevOp1 = 0;
        int64_t prevNow = 0;
        int64_t upc = 1;

        // Runs, the period grid
        int64_t lastEnd = -1;
        int64_t anchor = 0;
        int64_t period = 0;

        // Routines
        std::vector<Slot> routineSlots;
        uint32_t used = 0;

        // Commands: the mailbox counters seen, the command now, its open segment
        uint64_t cmdRead = 0;
        uint64_t dataIn = 0;
        uint64_t polls = 0;
        uint64_t acksAtRead = 0;
        uint64_t coalesced = 0;
        bool cmdOpen = false;
        uint8_t cmdCode = 0;
        int64_t cmdUnits = 0;
        bool segOpen = false;
        int64_t segStart = 0;
        int64_t segIsr = 0;
        std::array<GSProfileCommand, 256> commands{};

        // The budget, the frame
        GSProfileIsrStats isrTotal;
        GSProfileIsrStats isrFrame;
        int64_t frameStart = -1;
        int64_t frameIsr = 0;
        int64_t frameIdle = 0;
        int64_t frameHalt = 0;
        uint32_t frameCommands = 0;
        uint32_t frameInterrupts = 0;
        std::vector<GSProfileIsrMark> marks;
        bool marksTruncated = false;
        std::vector<GSProfileActivity> activity; ///< a ring of kActivityFrames
        size_t activityHead = 0;
        uint64_t frames = 0;
        uint64_t cycles = 0;

        uint64_t stackOverflows = 0;
        uint64_t routinesDropped = 0;
        uint64_t resyncs = 0;
    };

    /// A mailbox read since the last boundary (three compares a step)
    bool MailboxMoved() const
    {
        const Work& w = *_work;
        return _counters->cardCommandsRead != w.cmdRead || _counters->cardDataIn != w.dataIn ||
               _counters->cardStatusPolls != w.polls;
    }
    void Mailbox(int64_t now);
    void Transition(uint16_t pc, uint16_t sp, int64_t now);
    void Push(uint16_t pc, uint16_t sp, int64_t start, bool isr, bool nmi);
    void Pop(int64_t now);
    void Resync();
    void ResetWork();
    int32_t RoutineSlot(uint16_t pc);
    int64_t PeriodStart(int64_t t) const;
    int64_t IsrWallAt(int64_t now) const;
    void OpenSegment(int64_t now);
    void CloseSegment(int64_t now);
    void FinishCommand();
    uint32_t Cycles(int64_t units) const;

    GeneralSoundCard* _card = nullptr;
    const GSActivityCounters* _counters = nullptr;
    std::unique_ptr<Work> _work;  ///< emulation thread only
    std::atomic<bool> _on{ false };
    std::atomic<bool> _clearRequested{ false };

    mutable std::mutex _mutex;
    GSProfileSnapshot _published;
};
