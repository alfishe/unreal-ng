#pragma once

/// @file nexthistory.h
/// @brief The ZX Next's histories (debugger/history/devicehistory.h) beside its journals (nextjournal.h): the copper's
/// instructions (`copper`: an execution history of the second processor), the DMA's blocks (`dma`) and the UARTs'
/// bytes (`uart`: both channels, each record names its own).

#include <cstdint>

#include "debugger/history/devicehistory.h"

struct StateNode;

/// One instruction the copper completed: a WAIT that became true or a MOVE (reg 0: the NOP)
struct NextCopperRecord
{
    uint64_t frame = 0;
    uint16_t pc = 0;    ///< the instruction's index in the copper list
    bool move = false;
    uint8_t reg = 0;
    uint8_t value = 0;
    uint16_t line = 0;  ///< where the beam was: the line counter with NR #64 applied, the pixel clock in the row
    uint16_t hc = 0;

    void ToState(StateNode& node) const;
};

/// One block the DMA finished: from where to where, how long, in which mode
struct NextDmaRecord
{
    uint64_t frame = 0;
    uint32_t tacts = 0;
    uint16_t pc = 0;
    uint16_t source = 0;
    uint16_t destination = 0;
    uint16_t length = 0;     ///< the block length programmed (WR0)
    bool sourceIo = false;
    bool destinationIo = false;
    uint8_t mode = 0;        ///< WR4 bits 6:5: 0 byte, 1 continuous, 2 burst
    bool restart = false;    ///< auto restart: the next block follows

    void ToState(StateNode& node) const;
};

/// One byte a UART put on its line or took off it
struct NextUartRecord
{
    uint64_t frame = 0;
    uint64_t at = 0;       ///< the UART clock the byte's frame ended on
    uint8_t channel = 0;   ///< 0 ESP, 1 Pi
    bool tx = false;
    uint8_t value = 0;
    bool error = false;    ///< received with a framing / parity / overflow error, or lost to a full FIFO

    void ToState(StateNode& node) const;
};
