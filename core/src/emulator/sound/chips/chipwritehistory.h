#pragma once

/// @file chipwritehistory.h
/// @brief The register-write history of a sound chip outside the AY path (debugger/history/devicehistory.h): the
/// MoonSound OPL4, the Covox / SounDrive DACs, the Fuller Box AY, the ZX-MultiSound's SAA and SounDrive, the
/// Sprinter's Covox Blaster. One per device, named by its registry id; each record carries the chip surface it went to
/// (the chip register breakpoints' kinds, debugger/breakpoints/chipregister.h).

#include <cstdint>
#include <string>

#include "debugger/breakpoints/chipregister.h"
#include "debugger/history/devicehistory.h"

class EmulatorContext;
struct StateNode;

/// The standby ring size
constexpr size_t kChipWriteRing = 4096;

struct ChipWriteRecord
{
    uint64_t frame = 0;
    uint16_t tacts = 0;     ///< the frame's T-state of the OUT
    uint16_t pc = 0;        ///< PC of the OUT (the main CPU's)
    uint16_t reg = 0;       ///< the register (OPL4: bank << 8 | register; DAC: the channel)
    SoundChipKind kind = SoundChipKind::Ay;
    uint8_t unit = 0;       ///< the chip of a pair (TS-FM, MultiSound YM)
    uint8_t value = 0;

    void ToState(StateNode& node) const;
};

using ChipWriteHistory = history::Registered<ChipWriteRecord>;

/// The registry id of the device the main CPU's OUT to `port` goes to; `fallback` when the registry names none.
/// For a resolver: the device follows the slot set
std::string ChipHistoryDevice(const EmulatorContext* context, uint16_t port, const char* fallback);

/// The machine's histories (nullptr without a context)
history::Registry* ChipHistoryRegistry(EmulatorContext* context);

/// One register write, after the chip applied it: PC of the OUT, the frame and its T-state. Skipped while TTD replays
/// silently. The caller checks history.On()
void RecordChipWrite(const EmulatorContext* context, ChipWriteHistory& history, SoundChipKind kind, uint8_t unit,
                     uint16_t reg, uint8_t value);
