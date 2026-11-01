#pragma once

/// @file exechistory.h
/// @brief The execution history of a processor on the device history pattern (devicehistory.h): one record per
/// instruction it starts, with the page its address showed. One history per processor: `cpu` the machine's main CPU
/// (PcHistory feeds it), `<slot>.cpu` a sound card's Z80. Fed at instruction rate, so through PushFast (no lock);
/// off, the processor's gate (a step-work bit, the card's debug loop) stays down and nothing runs.

#include <cstdint>

#include "debugger/history/devicehistory.h"

struct StateNode;

struct ExecRecord
{
    uint64_t frame = 0;
    uint16_t pc = 0;
    uint8_t kind = 0;  ///< MemoryBankModeEnum of the address's window: 0 ROM, 1 RAM, 2 cache, #FF none
    uint8_t page = 0;  ///< the physical page of that kind

    void ToState(StateNode& node) const;
};

using ExecHistory = history::Registered<ExecRecord>;

/// The ring of an execution history without a limit: a few frames of a busy program
constexpr size_t kExecHistoryRing = 65536;
