#include "pch.h"

#include "opcode_test.h"
#include <iostream>
#include <exception>

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
		throw("memory should not be null");
	}

    // Clear first few bytes in ROM area to have only related commands, not left from previous runs
    for (size_t index = 0; index < 10; index++)
    {
        memory[index] = 0x00;
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
		throw "Z80 instruction cannot be 0 bytes in length. Something wrong with test table(s)";
	}

	// Transfer instruction into memory
	uint8_t memIndex = 0;
    uint8_t opLength = prefix == 0x00 ? result + 1 : result; // No-prefix instructions require correction since instruction table always have first byte of prefix
	for (uint8_t i = 0; i < opLength; i++)
	{
		// Skip prefix byte if not needed
		if (prefix == 0x00 && i == 0)
			continue;

		// Check instructions table correctness (if instruction has length 2-4 bytes, they have to be filled by correspondent parameter markers
		if (operation.instruction[i] == 0x00 && prefix == 0x00 && i > 1)
		{
			snprintf(message, sizeof message, "Test table invalid. Opcode:0x%02X. Instruction[%d] shouldn't be 0x00", opcode, i);
			throw message;
			break;
		}

		memory[memIndex] = operation.instruction[i];
		memIndex++;
	}

	// Add HALT instruction right after the one requested
	memory[result] = HALT;

	return result;
}