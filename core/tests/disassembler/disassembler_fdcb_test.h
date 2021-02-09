#pragma once
#include "pch.h"

#include "debugger/disassembler/z80disasm.h"

class Disassembler_FDCB_Test : public ::testing::Test
{
protected:
    Z80DisassemblerCUT* _disasm = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
