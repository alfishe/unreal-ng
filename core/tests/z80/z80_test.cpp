#include "pch.h"

#include "z80_test.h"

#include "common/modulelogger.h"
#include <string>

/// region <SetUp / TearDown>

void Z80_Test::SetUp()
{
	// Instantiate emulator with all peripherals, but no configuration loaded
	_context = new EmulatorContext(LoggerLevel::LogError); // Filter out all messages with level below error

	_cpu = new CPU(_context);
	if (_cpu->Init())
	{
        // Use Spectrum48K / Pentagon memory layout
        _cpu->GetMemory()->InternalSetBanks();

        // Instantiate opcode test helper
        _opcode = new OpcodeTest();
    }
	else
    {
	    throw std::logic_error("Z80_Test::SetUp - _cpu->Init() failed");
    }
}

void Z80_Test::TearDown()
{
    if (_opcode != nullptr)
    {
        delete _opcode;
        _opcode = nullptr;
    }

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

/// endregion </Setup / TearDown>

TEST_F(Z80_Test, Z80Reset)
{
	Z80* cpu = _cpu->GetZ80();
	
	cpu->Reset();
	EXPECT_EQ(cpu->pc, 0x0000);
	EXPECT_EQ(cpu->sp, 0xFFFF);
	EXPECT_EQ(cpu->af, 0xFFFF);

	EXPECT_EQ(cpu->ir_, 0x0000);
	EXPECT_EQ(cpu->int_flags, 0);
	EXPECT_EQ(cpu->int_pending, false);
	EXPECT_EQ(cpu->int_gate, true);
	EXPECT_EQ(cpu->last_branch, 0x0000);

	// Reset procedure should take 3 clock cycles
	EXPECT_EQ(cpu->cycle_count, 3);
}

TEST_F(Z80_Test, Z80OpcodeTimings_Noprefix)
{
	Z80& z80 = *_cpu->GetZ80();
	uint32_t start_cycles = 0;
	uint32_t finish_cycles = 0;
	uint32_t delta_cycles = 0;
	static char message[256];

	// Use 48k (SOS) ROM for testing purposes
	uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
	if (memory == nullptr)
	{
		FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
	}

	// Test no-prefix opcode instructions
	for (uint16_t i = 0; i <= 0xFF; i++)	// uint16_t is used since MSVC and GCC compilers are going crazy for 1 byte type and type overflow condition wnen comparing with 0xFF after increment to 0x00 and loosing overflow flag
	{
		if (i == 0xCB || i == 0xDD || i == 0xED || i == 0xFD)
			continue;

		// Perform reset to get clean results for each instruction
		z80.Reset();

		// Prepare instruction in ROM (0x0000 address)
		OpDescriptor& descriptor = _opcode->_noprefix[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0x00, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

		// Capture clock cycle counter before instruction execution
		start_cycles = z80.cycle_count;

		// Execute single instruction
		z80.Z80Step();

		// Measure instruction execution in clock cycles
		finish_cycles = z80.cycle_count;
		delta_cycles = finish_cycles - start_cycles;

		snprintf(message, sizeof message, "Opcode: 0x%02X", i);
		EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
	}

	// Test 0xCB prefixed opcodes

	// Test 0xDD prefixed opcodes



	// Test 0xFD prefixed opcodes
}

TEST_F(Z80_Test, Z80OpcodeTimings_ED)
{
	Z80& z80 = *_cpu->GetZ80();
	uint32_t start_cycles = 0;
	uint32_t finish_cycles = 0;
	uint32_t delta_cycles = 0;
	static char message[256];

	// Use 48k (SOS) ROM for testing purposes
	uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
	if (memory == nullptr)
	{
		FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
	}

	// Test 0xED prefixed opcodes
	for (uint16_t i = 0; i <= 0xFF; i++)	// uint16_t required since C++ compilers unable to make loop [0:255] working using 1 byte counter due to inability to utilize overflow flag
	{
		// Perform reset to get clean results for each instruction
		z80.Reset();

		// Prepare instruction in ROM (0x0000 address)
		OpDescriptor& descriptor = _opcode->_prefixED[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xED, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

		// Capture clock cycle counter before instruction execution
		start_cycles = z80.cycle_count;

		// Execute single instruction
		z80.Z80Step();

		// Measure instruction execution in clock cycles
		finish_cycles = z80.cycle_count;
		delta_cycles = finish_cycles - start_cycles;

		snprintf(message, sizeof message, "Opcode: 0xED 0x%02X", i);
		EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
	}
}

TEST_F(Z80_Test, Z80OpcodeTimings_CB)
{
	Z80& z80 = *_cpu->GetZ80();
	uint32_t start_cycles = 0;
	uint32_t finish_cycles = 0;
	uint32_t delta_cycles = 0;
	static char message[256];

	// Use 48k (SOS) ROM for testing purposes
	uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
	if (memory == nullptr)
	{
		FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
	}

	// Test 0xCB prefixed opcodes
	for (uint16_t i = 0; i <= 0xFF; i++)	// uint16_t required since C++ compilers unable to make loop [0:255] working using 1 byte counter due to inability to utilize overflow flag
	{
		// Perform reset to get clean results for each instruction
		z80.Reset();

		// Prepare instruction in ROM (0x0000 address)
		OpDescriptor& descriptor = _opcode->_prefixCB[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xCB, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

		// Capture clock cycle counter before instruction execution
		start_cycles = z80.cycle_count;

		// Execute single instruction
		z80.Z80Step();

		// Measure instruction execution in clock cycles
		finish_cycles = z80.cycle_count;
		delta_cycles = finish_cycles - start_cycles;

		snprintf(message, sizeof message, "Opcode: 0xCB 0x%02X", i);
		EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
	}
}

TEST_F(Z80_Test, Z80OpcodeTimings_DD)
{
	Z80& z80 = *_cpu->GetZ80();
	uint32_t start_cycles = 0;
	uint32_t finish_cycles = 0;
	uint32_t delta_cycles = 0;
	static char message[256];

	// Use 48k (SOS) ROM for testing purposes
	uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
	if (memory == nullptr)
	{
		FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
	}

	// Test 0xDD prefixed opcodes
	for (uint16_t i = 0; i <= 0xFF; i++)	// uint16_t required since C++ compilers unable to make loop [0:255] working using 1 byte counter due to inability to utilize overflow flag
	{
		// Perform reset to get clean results for each instruction
		z80.Reset();

		// Prepare instruction in ROM (0x0000 address)
		OpDescriptor& descriptor = _opcode->_prefixDD[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xDD, (uint8_t)i, memory);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

		// Capture clock cycle counter before instruction execution
		start_cycles = z80.cycle_count;

		// Execute single instruction
		z80.Z80Step();

		// Measure instruction execution in clock cycles
		finish_cycles = z80.cycle_count;
		delta_cycles = finish_cycles - start_cycles;

		snprintf(message, sizeof message, "Opcode: 0xDD 0x%02X", i);
		EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
	}
}

TEST_F(Z80_Test, Z80OpcodeTimings_DDCB)
{
	Z80& z80 = *_cpu->GetZ80();
	uint32_t start_cycles = 0;
	uint32_t finish_cycles = 0;
	uint32_t delta_cycles = 0;
	static char message[256];

	// Use 48k (SOS) ROM for testing purposes
	uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
	if (memory == nullptr)
	{
		FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
	}

	// Test 0xDD 0xCB prefixed opcodes
	for (uint16_t i = 0; i <= 0xFF; i++)	// uint16_t required since C++ compilers unable to make loop [0:255] working using 1 byte counter due to inability to utilize overflow flag
	{
		// Perform reset to get clean results for each instruction
		z80.Reset();

		// Prepare instruction in ROM (0x0000 address)
		OpDescriptor& descriptor = _opcode->_prefixDDCB[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xDD, (uint8_t)i, memory, 0xCB);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

		// Capture clock cycle counter before instruction execution
		start_cycles = z80.cycle_count;

		// Execute single instruction
		z80.Z80Step();

		// Measure instruction execution in clock cycles
		finish_cycles = z80.cycle_count;
		delta_cycles = finish_cycles - start_cycles;

		snprintf(message, sizeof message, "Opcode: 0xDD 0xCB 0x%02X", i);
		EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
	}
}

TEST_F(Z80_Test, Z80OpcodeTimings_FD)
{
	Z80& z80 = *_cpu->GetZ80();
	uint32_t start_cycles = 0;
	uint32_t finish_cycles = 0;
	uint32_t delta_cycles = 0;
	static char message[256];

	// Use 48k (SOS) ROM for testing purposes
	uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
	if (memory == nullptr)
	{
		FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
	}

	// Test 0xFD prefixed opcodes
	for (uint16_t i = 0; i <= 0xFF; i++)	// uint16_t required since C++ compilers unable to make loop [0:255] working using 1 byte counter due to inability to utilize overflow flag
	{
		// Perform reset to get clean results for each instruction
		z80.Reset();

		// Prepare instruction in ROM (0x0000 address)
		OpDescriptor& descriptor = _opcode->_prefixFD[i];
		try
		{
            uint8_t len = _opcode->PrepareInstruction(0xFD, (uint8_t) i, memory);
        }
		catch (const char* err)
        {
		    FAIL() << err << std::endl;
        }

		// Capture clock cycle counter before instruction execution
		start_cycles = z80.cycle_count;

		// Execute single instruction
		z80.Z80Step();

		// Measure instruction execution in clock cycles
		finish_cycles = z80.cycle_count;
		delta_cycles = finish_cycles - start_cycles;

		snprintf(message, sizeof message, "Opcode: 0xFD 0x%02X", i);
		EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
	}
}

TEST_F(Z80_Test, Z80OpcodeTimings_FDCB)
{
	Z80& z80 = *_cpu->GetZ80();
	uint32_t start_cycles = 0;
	uint32_t finish_cycles = 0;
	uint32_t delta_cycles = 0;
	static char message[256];

	// Use 48k (SOS) ROM for testing purposes
	uint8_t* memory = _cpu->GetMemory()->base_sos_rom;
	if (memory == nullptr)
	{
		FAIL() << "memory->base_sos_rom not initialized correctly" << std::endl;
	}

	// Test 0xFD 0xCB prefixed opcodes
	for (uint16_t i = 0; i <= 0xFF; i++)	// uint16_t required since C++ compilers unable to make loop [0:255] working using 1 byte counter due to inability to utilize overflow flag
	{
		// Perform reset to get clean results for each instruction
		z80.Reset();

		// Prepare instruction in ROM (0x0000 address)
		OpDescriptor& descriptor = _opcode->_prefixFDCB[i];
        try
        {
            uint8_t len = _opcode->PrepareInstruction(0xFD, (uint8_t)i, memory, 0xCB);
        }
        catch (const char* err)
        {
            FAIL() << err << std::endl;
        }

		// Capture clock cycle counter before instruction execution
		start_cycles = z80.cycle_count;

		// Execute single instruction
		z80.Z80Step();

		// Measure instruction execution in clock cycles
		finish_cycles = z80.cycle_count;
		delta_cycles = finish_cycles - start_cycles;

		snprintf(message, sizeof message, "Opcode: 0xFD 0xCB 0x%02X", i);
		EXPECT_EQ(delta_cycles, descriptor.cycles) << message << std::endl;
	}
}