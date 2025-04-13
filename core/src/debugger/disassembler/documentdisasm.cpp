#include "stdafx.h"
#include "documentdisasm.h"

DocumentDisasm::DocumentDisasm()
{
    m_disassembler = new Z80Disassembler();
}

DocumentDisasm::~DocumentDisasm()
{
    if (m_disassembler)
    {
        delete m_disassembler;
        m_disassembler = nullptr;
    }
}

DisplayInstruction DocumentDisasm::getInstructionForZ80Address(uint16_t address)
{
    // Mute unused param warnings until implemented
    (void)address;

    DisplayInstruction result;

    return result;
}