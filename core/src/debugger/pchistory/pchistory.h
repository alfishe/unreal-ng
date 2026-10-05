#pragma once

// PC history for a debugger (debugger additions tdd §7, gap analysis E2): the address of every instruction the CPU
// starts, with the physical page its 16K window showed at that moment, in a ring of the newest kCapacity entries.
//
// It costs nothing until a debugger asks: Arm(true) raises the step-work bit kStepWorkPcHistory, and only then does
// Z80::StepInstructionWithWork call Record before each instruction (an accepted interrupt is not an instruction and
// is not recorded). Example: a CALL #C000 on TS-Conf with RAM page #20 in window 3 leaves {address #C000, ram, #20}
// as the next entry. The ring belongs to the emulation thread; Report reads it at a coherent moment.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

    explicit PcHistory(EmulatorContext* context) : _context(context) {}

    /// Start (clearing what was kept) or stop recording; stopping keeps the entries for reading
    void Arm(bool on);
    bool IsArmed() const { return _armed; }
    void Clear();

    /// One instruction starts at `address` (Z80 step, emulation thread only)
    void Record(uint16_t address);

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
    /// read starts it: entries come from then on)
    static Result Report(Emulator* emulator, size_t depth);
    /// Arm (starting empty) or stop the history at a coherent moment; empty string or the reason
    static std::string SetArmed(Emulator* emulator, bool on);

private:
    EmulatorContext* _context;
    std::array<Entry, kCapacity> _ring{};
    size_t _next = 0;
    uint64_t _total = 0;
    bool _armed = false;
};
