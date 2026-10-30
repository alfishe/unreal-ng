#pragma once

/// @file l1builder.h
/// @brief Folds the instruction stream of a replay into L1 (l1model.h) as it runs, with no stored trace: memory grows
/// with the code and data the program touched, never with the length of the recording.
///
/// The replay tracer (replaytracer.h) feeds it: OnStart at every instruction start (the Z80's m1 trace hook), OnBus
/// for every memory and I/O cycle in between (the bus trace hook). The pending instruction is completed at the next
/// start, when its successor, its operands and the registers after it are known. Build() closes the open loops and
/// turns the per-instruction records into blocks, routines, the call graph and the rest, all sorted.
///
/// Folding per instruction: the decoded op (z80opinfo.h, a table lookup); counts, T-states and frames; the successor
/// edge; data ranges and ports; the shadow call stack (CALL / RST / interrupt frames matched by the stack slot of the
/// return address, so PUSH + RET dispatch, patched returns and stack switches are told apart); live registers per
/// frame; loop instances (back edges, trips, per-pass register deltas, per-operand address strides); base + offset
/// accesses; the shallow data flow tags; the last flag producer for each conditional branch.

#include <cstdint>
#include <functional>
#include <memory>

#include "debugger/analyzers/analysis/l1model.h"

namespace analysis
{

class L1Builder
{
public:
    struct Regs
    {
        uint16_t pc = 0;
        uint16_t sp = 0;
        uint16_t af = 0;
        uint16_t bc = 0;
        uint16_t de = 0;
        uint16_t hl = 0;
        uint16_t ix = 0;
        uint16_t iy = 0;
    };

    /// The absolute page mapped at a Z80 address now
    using PageOf = std::function<uint16_t(uint16_t z80)>;

    /// @p salt seeds the hash of the builder's unordered containers (0: a random one). Output never depends on it:
    /// a build that did would differ from the next one (AnalysisL1_Test.ReplayDeterministic)
    explicit L1Builder(PageOf pageOf, uint64_t salt = 0);
    ~L1Builder();
    L1Builder(const L1Builder&) = delete;
    L1Builder& operator=(const L1Builder&) = delete;

    /// An instruction starts at @p pc (page: mapped now). @p halted: the CPU repeats a HALT (an idle fetch, not an
    /// instruction). @p interrupted: an interrupt was accepted since the last start; @p returnAddress is what it
    /// pushed (the pending instruction's successor) and the stack pointer in @p regs is after that push
    void OnStart(uint16_t pc, uint16_t page, const Regs& regs, uint64_t now, uint64_t frame, bool halted,
                 bool interrupted, bool nmi, uint16_t returnAddress);

    /// A bus cycle of the pending instruction: 'R' / 'W' memory, 'I' / 'O' port (others are ignored)
    void OnBus(char type, uint16_t address, uint8_t value)
    {
        if (type == 'N')
            return;
        if (_eventCount < kMaxEvents)
        {
            _events[_eventCount].type = type;
            _events[_eventCount].value = value;
            _events[_eventCount].address = address;
            ++_eventCount;
        }
    }

    /// Complete the pending instruction (the end of the replay); @p regs: the state after it
    void Flush(const Regs& regs, uint64_t now);

    /// Everything folded so far, as L1. @p pageName names an absolute page ("RAM 5", "ROM 1")
    L1Result Build(const ReplaySummary& summary, const std::function<std::string(uint16_t)>& pageName);

    /// Heap held by the folded records (the bounded-memory check)
    size_t MemoryBytes() const;
    uint64_t Instructions() const;

    struct BusEvent
    {
        char type;
        uint8_t value;
        uint16_t address;
    };
    static constexpr int kMaxEvents = 64;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
    BusEvent _events[kMaxEvents] = {};
    int _eventCount = 0;
};

}  // namespace analysis
