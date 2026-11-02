#pragma once

/// @file tsconfhistory.h
/// @brief The TS-Conf histories (debugger/history/devicehistory.h): the FPGA register writes (`ts-registers`), the DMA
/// transfers (`ts-registers.dma`: one record a launch, completed at its end) and the VDAC2's FT812 SPI transactions
/// (`vdac2`: one record a chip select window).

#include <cstdint>

#include "debugger/history/devicehistory.h"

struct StateNode;

/// One write to a #xxAF register
struct TsRegisterRecord
{
    uint64_t frame = 0;
    uint16_t tacts = 0;
    uint16_t pc = 0;
    uint8_t reg = 0;
    uint8_t value = 0;

    void ToState(StateNode& node) const;
};

/// One DMA transfer: its launch (DMA_CTRL) and, once it ends, its end
struct TsDmaRecord
{
    uint64_t frame = 0;
    uint16_t tacts = 0;
    uint16_t pc = 0;
    uint8_t device = 0;       ///< {DMA_CTRL[7], DMA_CTRL[2:0]} (TsConfDma's device codes)
    uint8_t flags = 0;        ///< TsConfDmaFlag bits at the launch
    uint32_t source = 0;      ///< byte address
    uint32_t destination = 0; ///< byte address
    uint16_t burst = 0;       ///< words a block (DMA_LEN + 1)
    uint16_t blocks = 0;      ///< DMA_NUM + 1
    bool aborted = false;     ///< relaunched while the previous transfer ran
    bool done = false;
    uint64_t endFrame = 0;

    void ToState(StateNode& node) const;
};

/// One FT812 SPI transaction (chip select low to high): host command, memory write or read
struct Vdac2Record
{
    uint64_t frame = 0;
    uint64_t clock = 0;       ///< the FT812 system clock at the select
    uint8_t header[3] = {};   ///< the first three bytes (command, or the address with its read / write bits)
    uint8_t data[8] = {};     ///< the first data bytes (written, or read back)
    uint32_t length = 0;      ///< bytes after the header (a read: after its dummy byte)

    void ToState(StateNode& node) const;
};

using TsRegisterHistory = history::Registered<TsRegisterRecord>;
using TsDmaHistory = history::Registered<TsDmaRecord>;
using Vdac2History = history::Registered<Vdac2Record>;
