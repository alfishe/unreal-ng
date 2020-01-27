#include "pch.h"

#include "z80_test.h"

void Z80_Test::SetUp()
{
	_context = new EmulatorContext();
	_cpu = new CPU(_context);
}

void Z80_Test::TearDown()
{
	if (_cpu != nullptr)
	{
		delete _cpu;
		_cpu = nullptr;
	}

	if (_context != nullptr)
	{
		delete _context;
		_context = nullptr;
	}
}

TEST_F(Z80_Test, Z80Reset)
{
	Z80* cpu = _cpu->GetZ80();
	
	cpu->Reset();
	EXPECT_EQ(cpu->pc, 0x0000);
	EXPECT_EQ(cpu->sp, 0xFFFF);
	EXPECT_EQ(cpu->af, 0xFFFF);

	EXPECT_EQ(cpu->ir_, 0x0000);
	EXPECT_EQ(cpu->int_flags, 0);
	EXPECT_EQ(cpu->int_pend, false);
	EXPECT_EQ(cpu->int_gate, true);
	EXPECT_EQ(cpu->last_branch, 0x0000);
	EXPECT_EQ(cpu->cycle_count, 3);
}