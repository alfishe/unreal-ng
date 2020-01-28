#include "pch.h"

#include "opcode_test.h"
#include <iostream>

using namespace z80_test;

OpcodeTest::OpcodeTest()
{
	_noprefix_conditional.insert(std::pair<uint8_t, ConditionalTimings>(0x10, { 0x10, 13, 8, FLAG_ZF }));	// djnz xx
}

uint8_t OpcodeTest::PrepareInstruction(uint8_t prefix, uint8_t opcode, uint8_t* memory)
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
			break;
		case 0xDD:
			break;
		case 0xED:
			break;
		case 0xFD:
			break;
		default:
			throw("Invalid prefix");
	}

	OpDescriptor& operation = opTable[opcode];
	result = operation.bytes;
	if (result == 0)
	{
		throw("Z80 instruction cannot be 0 bytes in length. Something wrong with test table(s)");
	}

	// Transfer instruction into memory
	for (uint8_t i = 0; i < result; i++)
	{
		// Check instructions table correctness (if instruction has length 2-4 bytes, they have to be filled by correspondent parameter markers
		if (operation.instruction[i] == 0x00 && i > 0)
		{
			snprintf(message, sizeof message, "Test table invalid. Opcode:0x%02X. Instruction[%d] shouldn't be 0x00\0", opcode, i);
			std::cout << message << std::endl;
			throw("Invalid instruction content");
			break;
		}

		memory[i] = operation.instruction[i];
	}

	// Add HALT instruction right after requested
	memory[result] = HALT;

	return result;
}