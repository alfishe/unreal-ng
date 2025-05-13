#include "stdafx.h"
#include "documentdisasm.h"

DocumentDisasm::DocumentDisasm()
{
    _disassembler = std::make_unique<Z80Disassembler>();
}

DocumentDisasm::~DocumentDisasm()
{
    if (_disassembler)
    {
        // Smart pointer will delete Z80Disassembler instance automatically
        _disassembler = nullptr;
    }
}

DisplayInstruction DocumentDisasm::getInstructionForZ80Address(uint16_t address)
{
    // Mute unused param warnings until implemented
    (void)address;

    DisplayInstruction result;

    return result;
}