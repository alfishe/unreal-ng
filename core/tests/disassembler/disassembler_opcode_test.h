#pragma once
#include "pch.h"

#include "debugger/disassembler/z80disasm.h"
#include "debugger/debugmanager.h"


class Disassembler_Opcode_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    DebugManager* _debugManager = nullptr;
    Z80DisassemblerCUT* _disasm = nullptr;

protected:
    void SetUp() override;
    void TearDown() override;
};
