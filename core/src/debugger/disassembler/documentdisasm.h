#pragma once
#include "stdafx.h"

#include "z80disasm.h"

/// region <Types>

/// Decoded instruction augmented with additional fields:
/// - Z80 address
/// - Full host address
struct DisplayInstruction : public DecodedInstruction
{
    uint16_t addressZ80;        // Address in 64k Z80 address space
    uint8_t* addressHost;       // Physical host memory address

    uint16_t bank;              // RAM bank number
    uint16_t addressBank;       // Address in the bank

    /// Weak references (can become invalid anytime)
};

/// endregion </Types>


class DocumentDisasm
{
public:
    explicit DocumentDisasm();
    virtual ~DocumentDisasm();
    DisplayInstruction getInstructionForZ80Address(uint16_t address);

protected:
    std::unique_ptr<Z80Disassembler> _disassembler;
};
