#include "pch.h"

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

#include "opcode_test.h"

using namespace z80_test;

class Z80_Test : public ::testing::Test
{
protected:
	EmulatorContext* _context = nullptr;
	Core* _cpu = nullptr;
	OpcodeTest* _opcode = nullptr;

protected:
	void SetUp() override;
	void TearDown() override;

    void DumpFirst256ROMBytes();
    void ResetCPUAndMemory();
};