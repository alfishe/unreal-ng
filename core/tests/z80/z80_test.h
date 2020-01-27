#include "pch.h"

#include "emulator/cpu/cpu.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

class Z80_Test : public ::testing::Test
{
protected:
	EmulatorContext* _context = nullptr;
	CPU* _cpu = nullptr;

protected:
	void SetUp() override;
	void TearDown() override;
};