#include "stdafx.h"
#include "documentdisasm.h"

DocumentDisasm::DocumentDisasm(EmulatorContext* context) : _context(context)
{
    _disassembler = std::make_unique<Z80Disassembler>(context);
}

DocumentDisasm::~DocumentDisasm()
{
    _disassembler.reset();
}

DisplayInstruction DocumentDisasm::getInstructionForZ80Address(uint16_t address)
{
    // Mute unused param warnings until implemented
    (void)address;

    DisplayInstruction result;

    return result;
}