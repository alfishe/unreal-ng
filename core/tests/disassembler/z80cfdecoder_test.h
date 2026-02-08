#pragma once
#include "pch.h"

#include "debugger/disassembler/z80cfdecoder.h"
#include "emulator/memory/calltrace.h"  // For Z80CFType enum definition

/// @brief Test fixture for Z80ControlFlowDecoder unit tests.
///
/// The decoder is a stateless static class, so no setup/teardown is needed.
/// Memory is passed as nullptr for most tests (stack reads return 0).
class Z80CFDecoder_Test : public ::testing::Test
{
protected:
    // Helper to call Decode with common defaults
    bool Decode(const uint8_t* bytes, uint16_t pc, Z80ControlFlowResult& result,
                uint8_t flags = 0x00, uint8_t b_reg = 0, uint16_t sp = 0xFFF0,
                uint16_t hl = 0, uint16_t ix = 0, uint16_t iy = 0)
    {
        return Z80ControlFlowDecoder::Decode(bytes, pc, flags, b_reg, sp, hl, ix, iy,
                                              nullptr, result);
    }
};
