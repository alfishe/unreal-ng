#pragma once

// PC history for a debugger (debugger additions tdd §7, gap analysis E2): the address of every instruction the CPU
// starts, with the physical page its 16K window showed at that moment, in a ring of the newest kCapacity entries.
//
// It costs nothing until a debugger asks: Arm(true) raises the step-work bit kStepWorkPcHistory, and only then does
// Z80::StepInstructionWithWork call Record before each instruction (an accepted interrupt is not an instruction: it
// is not in this ring; the execution history `cpu` records it as an event, RecordInterrupt). Example: a CALL #C000 on TS-Conf with RAM page #20 in window 3 leaves {address #C000, ram, #20}
// as the next entry. The ring belongs to the emulation thread; Report reads it at a coherent moment.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "debugger/history/exechistory.h"
#include "emulator/state/statenode.h"

class Emulator;
class EmulatorContext;

class PcHistory
{
public:
    static constexpr size_t kCapacity = 1024;

    struct Entry
    {
        uint16_t address = 0;
        uint8_t kind = 0;  ///< MemoryBankModeEnum of the window: ROM, RAM, cache
        uint8_t page = 0;  ///< the physical page of that kind
    };

    /// Registers the execution history `cpu` in the machine's histories (off)
    explicit PcHistory(EmulatorContext* context);

    /// Start (clearing what was kept) or stop recording; stopping keeps the entries for reading
    void Arm(bool on);
    bool IsArmed() const { return _armed; }
    void Clear();

    /// One instruction starts at `address` at T `t` of the frame (Z80 step, emulation thread only)
    void Record(uint16_t address, uint32_t t);
    /// An INT or NMI acknowledge starts at T `t`, interrupting `address`: an event record of the execution history
    /// `cpu` (the twin timeline's INT marker and the width of the acknowledge), not an entry of this ring
    void RecordInterrupt(bool nmi, uint16_t address, uint32_t t);
    /// The execution history `cpu` (a reader's snapshot; the twin timeline switches it on while armed)
    history::DeviceHistory<ExecRecord>& Exec() { return *_exec; }

    /// The newest `depth` entries, newest first
    std::vector<Entry> Newest(size_t depth) const;
    /// Instructions recorded since the last Arm(true) / Clear
    uint64_t Total() const { return _total; }

    /// {armed, started_now, total, capacity, entries [{address, kind, page}]} now, arming when off; the caller makes
    /// sure nothing runs the machine meanwhile (a coherent moment: Report, the debugger snapshot)
    StateNode ReportNow(size_t depth);

    struct Result
    {
        StateNode report;   ///< {armed, total, capacity, entries [{address, kind, page}]}
        std::string error;
        bool busy = false;
    };
    /// Read `depth` entries (at most kCapacity) at a coherent moment; arms the history when it was off (the first
    /// read starts it: entries come from then on). `waitMs`: how long to wait for that moment (0: one try, busy at
    /// once; a GUI thread never waits and tries again later)
    static Result Report(Emulator* emulator, size_t depth, uint32_t waitMs = 500);
    /// Arm (starting empty) or stop the history at a coherent moment; empty string or the reason
    static std::string SetArmed(Emulator* emulator, bool on);

private:
    EmulatorContext* _context;
    ExecHistory _exec;  ///< `cpu`: the device history face of the same hook (any mode, limit, stream)
    std::array<Entry, kCapacity> _ring{};
    size_t _next = 0;
    uint64_t _total = 0;
    bool _armed = false;
};
