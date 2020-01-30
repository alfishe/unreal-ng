#include "pch.h"

#include "opcode_test.h"
#include <iostream>

using namespace z80_test;

OpcodeTest::OpcodeTest()
{
	_noprefix_conditional.insert(std::pair<uint8_t, ConditionalTimings>(0x10, { 0x10, 13, 8, FLAG_ZF }));	// djnz xx
}

uint8_t OpcodeTest::PrepareInstruction(uint8_t prefix, uint8_t opcode, uint8_t* memory, uint8_t extraPrefix)
{
	uint8_t result = 0;
	static char message[256];

	if (memory == nullptr)
	{
		std::cout << "memory should not be null"  << std::endl;
		throw("memory should not be null");
	}

	OpDescriptor* opTable = nullptr;

	switch (prefix)
	{
		case 0x00:
			opTable = _noprefix;
			break;
		case 0xCB:
			opTable = _prefixCB;
			break;
		case 0xDD:
			if (extraPrefix == 0xCB)
				opTable = _prefixDDCB;
			else
				opTable = _prefixDD;
			break;
		case 0xED:
			opTable = _prefixED;
			break;
		case 0xFD:
			if (extraPrefix == 0xCB)
				opTable = _prefixFDCB;
			else
				opTable = _prefixFD;
			break;
		default:
			throw("Invalid prefix");
	}

	OpDescriptor& operation = opTable[opcode];
	result = operation.bytes;
	if (result == 0)
	{
		std::cout << "Z80 instruction cannot be 0 bytes in length. Something wrong with test table(s)" << std::endl;
		throw("Z80 instruction cannot be 0 bytes in length. Something wrong with test table(s)");
	}

	// No-prefix instructions started from instruction[1] so we need to offset
	if (prefix == 0x00)
	{
		result++;
	}

	// Transfer instruction into memory
	uint8_t idx = 0;
	for (uint8_t i = 0; i < result; i++)
	{
		// Skip prefix byte if not needed
		if (prefix == 0x00 && i == 0)
			continue;

		// Check instructions table correctness (if instruction has length 2-4 bytes, they have to be filled by correspondent parameter markers
		if (operation.instruction[i] == 0x00 && prefix == 0x00 && i > 1)
		{
			snprintf(message, sizeof message, "Test table invalid. Opcode:0x%02X. Instruction[%d] shouldn't be 0x00\0", opcode, i);
			std::cout << message << std::endl;
			throw("Invalid instruction content");
			break;
		}

		memory[idx] = operation.instruction[i];
		idx++;
	}

	// Add HALT instruction right after requested
	memory[result] = HALT;

	return result;
}