#include "disassembler_opcode_test.h"

#include <vector>
#include "common/dumphelper.h"
#include "common/stringhelper.h"

/// region <SetUp / TearDown>

void Disassembler_Opcode_Test::SetUp()
{
    // Instantiate emulator with all peripherals, but no configuration loaded
    _disasm = new Z80DisassemblerCUT();
}

void Disassembler_Opcode_Test::TearDown()
{
    if (_disasm != nullptr)
    {
        delete _disasm;
        _disasm = nullptr;
    }
}

/// endregion </Setup / TearDown>

TEST_F(Disassembler_Opcode_Test, TestAllNoPrefixOpCodes)
{
    for (unsigned opcode = 0; opcode < 256; opcode++)
    {
        // Exclude prefixes from processing
        if (opcode == 0xCB || opcode == 0xDD || opcode == 0xED || opcode == 0xFD)
            continue;

        // Get the opcode structure
        const OpCode& op = _disasm->getOpcode(0, opcode); // 0 prefix means no prefix
        const std::string mnemonic = op.mnem;

        // Create a buffer with the opcode and random parameters
        uint8_t buffer[4] = { (uint8_t)opcode }; // Start with opcode
        size_t bufferLen = 1; // Start with default opcode length

        // Generate random parameters based on opcode flags
        if (op.flags & OF_MBYTE) // Has byte operand
        {
            buffer[1] = (uint8_t)(rand() % 256);
            bufferLen = 2;
        }
        else if (op.flags & OF_MWORD) // Has word operand
        {
            buffer[1] = (uint8_t)(rand() % 256);
            buffer[2] = (uint8_t)(rand() % 256);
            bufferLen = 3;
        }

        // Disassemble the command
        std::string result = _disasm->disassembleSingleCommand(buffer, bufferLen);

        // Get expected result using the opcode mnemonic
        std::string referenceResult = mnemonic;

        // Format expected result with random values
        uint8_t commandLength = bufferLen;
        if (op.flags & OF_MBYTE)
        {
            referenceResult = StringHelper::ReplaceAll(referenceResult, string(":1"), string("%s"));
            referenceResult = StringHelper::Format(referenceResult, StringHelper::ToHexWithPrefix(buffer[1], "#", true));
        }
        else if (op.flags & OF_MWORD)
        {
            uint16_t word = (buffer[2] << 8) | buffer[1];

            referenceResult = StringHelper::ReplaceAll(referenceResult, string(":2"), string("%s"));
            referenceResult = StringHelper::Format(referenceResult, StringHelper::ToHexWithPrefix(word, "#", true));
        }

        if (result != referenceResult)
        {
            std::string message = StringHelper::Format("Iteration %d. Data '%02X'. Expected '%s', found '%s'",
                                                       opcode, opcode, referenceResult.c_str(), result.c_str());
            EXPECT_EQ(referenceResult, result) << message << std::endl;
            return;
        }
        else
        {
#ifdef _DEBUG
            //std::cout << std::left << std::setw(16) << hexCommand << std::setw(0) << result << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(Disassembler_Opcode_Test, TestAllEDOpCodes)
{
    FAIL() << "Not implemented";
}

TEST_F(Disassembler_Opcode_Test, TestAllCBOpCodes)
{
    // Operations in order
    const std::string operations[32] =
        {
            // Rotate/Shift (00xxx000 to 00xxx111)
            "rlc", "rrc", "rl",  "rr",   // 0x00-0x03
            "sla", "sra", "sll", "srl",  // 0x04-0x07

            // BIT operations (01xxx000 to 01xxx111)
            "bit", "bit", "bit", "bit",  // 0x08-0x0B
            "bit", "bit", "bit", "bit",  // 0x0C-0x0F

            // RES operations (10xxx000 to 10xxx111)
            "res", "res", "res", "res",  // 0x10-0x13
            "res", "res", "res", "res",  // 0x14-0x17

            // SET operations (11xxx000 to 11xxx111)
            "set", "set", "set", "set",  // 0x18-0x1B
            "set", "set", "set", "set"   // 0x1C-0x1F
        };

    // Register names in order
    const std::string registers[] = {"b", "c", "d", "e", "h", "l", "(hl)", "a"};

    // Test each opcode
    for (int opcode = 0; opcode < 256; ++opcode)
    {
        int operationIndex = opcode >> 3;
        std::string operation = operations[operationIndex];
        std::string registerName = registers[opcode % 8];
        int bitNumber = (opcode >> 3) & 0b0000'0111;

        uint8_t command[2] = {0xCB, (uint8_t)opcode};
        std::string hexCommand = DumpHelper::HexDumpBuffer(command, sizeof(command));

        std::string referenceResult;
        switch (operationIndex)
        {
            case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: // RLC-RR, SLA-SRA, SLL-SRL
                referenceResult = StringHelper::Format("%s %s", operation, registerName);
                break;
            
            case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: // BIT operations
                referenceResult = StringHelper::Format("%s %d,%s", operation, bitNumber, registerName);
                break;

            case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: // RES operations
            case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: // SET operations
                referenceResult = StringHelper::Format("%s %d,%s", operation, bitNumber, registerName);
                break;
        }

        std::string result = _disasm->disassembleSingleCommand(command, sizeof(command));
        
        if (result != referenceResult)
        {
            std::string message = StringHelper::Format("Iteration %d. Data 'CB %02X'. Expected '%s', found '%s'", 
                opcode, opcode, referenceResult.c_str(), result.c_str());
            EXPECT_EQ(referenceResult, result) << message << std::endl;
            return;
        }
        else
        {
#ifdef _DEBUG
            //std::cout << std::left << std::setw(16) << hexCommand << std::setw(0) << result << std::endl;
#endif // _DEBUG
        }
    }
}

TEST_F(Disassembler_Opcode_Test, TestAllDDOpCodes)
{
    FAIL() << "Not implemented";
}

TEST_F(Disassembler_Opcode_Test, TestAllDDCBOpCodes)
{
    char message[1024];

    // Operations in order
    const std::string operations[32] =
        {
            // Rotate/Shift (00xxx000 to 00xxx111)
            "rlc", "rrc", "rl",  "rr",   // 0x00-0x03
            "sla", "sra", "sll", "srl",  // 0x04-0x07

            // BIT operations (01xxx000 to 01xxx111)
            "bit", "bit", "bit", "bit",  // 0x08-0x0B
            "bit", "bit", "bit", "bit",  // 0x0C-0x0F

            // RES operations (10xxx000 to 10xxx111)
            "res", "res", "res", "res",  // 0x10-0x13
            "res", "res", "res", "res",  // 0x14-0x17

            // SET operations (11xxx000 to 11xxx111)
            "set", "set", "set", "set",  // 0x18-0x1B
            "set", "set", "set", "set"   // 0x1C-0x1F
        };

    // Register names in order
    const std::string registers[] = {"b", "c", "d", "e", "h", "l", "", "a"};

    // Test each combination of displacement and opcode
    for (int disp = -128; disp <= 127; ++disp)
    {
        for (int opcode = 0; opcode < 256; ++opcode)
        {
            uint8_t displacement = (uint8_t)disp;
            int operationIndex = opcode >> 3;
            std::string operation = operations[operationIndex];
            std::string registerName = registers[opcode % 8];
            int bitNumber = (opcode >> 3) & 0b0000'0111;

            uint8_t command[4] = {0xDD, 0xCB, (uint8_t)disp, (uint8_t)opcode};
            std::string hexCommand = DumpHelper::HexDumpBuffer(command, sizeof(command));

            // Generate reference result
            std::string referenceResult;

            switch (operationIndex)
            {
                case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: // RLC-RR, SLA-SRA, SLL-SRL
                {
                    if (registerName.empty())
                    {
                        referenceResult = StringHelper::Format("%s (ix+#%02X)", operation, displacement);
                    }
                    else
                    {
                        referenceResult =
                            StringHelper::Format("%s (ix+#%02X),%s", operation, displacement, registerName);
                    }
                }
                break;

                case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: // BIT operations
                {
                    referenceResult = StringHelper::Format("%s %d,(ix+#%02X)", operation, bitNumber, displacement);
                }
                break;

                case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: // RES operations
                case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: // SET operations
                {
                    if (registerName.empty())
                    {
                        referenceResult = StringHelper::Format("%s %d,(ix+#%02X)", operation, bitNumber, displacement);
                    }
                    else
                    {
                        referenceResult = StringHelper::Format("%s %d,(ix+#%02X),%s", operation, bitNumber, displacement, registerName);
                    }
                }
                break;
            }

            // Probe method under test and get result
            std::string result = _disasm->disassembleSingleCommand(command, sizeof(command));

            if (result != referenceResult)
            {
                snprintf(message, sizeof message, "Opcode 0x%02X. Displacement %d. Data '%s'. Expected '%s', found '%s'",
                         opcode,
                         (int8_t)displacement,
                         hexCommand.c_str(), referenceResult.c_str(), result.c_str());

                EXPECT_EQ(referenceResult, result) << message << std::endl;
                return;
            }
            else
            {
#ifdef _DEBUG
                //std::cout << std::left << std::setw(16) << hexCommand << std::setw(0) << result << std::endl;
#endif // _DEBUG
            }
        }
    }
}

TEST_F(Disassembler_Opcode_Test, TestAllFDCBOpCodes)
{
    char message[1024];

    // Operations in order
    const std::string operations[32] =
    {
        // Rotate/Shift (00xxx000 to 00xxx111)
        "rlc", "rrc", "rl",  "rr",   // 0x00-0x03
        "sla", "sra", "sll", "srl",  // 0x04-0x07

        // BIT operations (01xxx000 to 01xxx111)
        "bit", "bit", "bit", "bit",  // 0x08-0x0B
        "bit", "bit", "bit", "bit",  // 0x0C-0x0F

        // RES operations (10xxx000 to 10xxx111)
        "res", "res", "res", "res",  // 0x10-0x13
        "res", "res", "res", "res",  // 0x14-0x17

        // SET operations (11xxx000 to 11xxx111)
        "set", "set", "set", "set",  // 0x18-0x1B
        "set", "set", "set", "set"   // 0x1C-0x1F
    };

    // Register names in order
    const std::string registers[] = {"b", "c", "d", "e", "h", "l", "", "a"};

    // Test each combination of displacement and opcode
    for (int disp = -128; disp <= 127; ++disp)
    {
        for (int opcode = 0; opcode < 256; ++opcode)
        {
            uint8_t displacement = (uint8_t)disp;
            int operationIndex = opcode >> 3;
            std::string operation = operations[operationIndex];
            std::string registerName = registers[opcode % 8];
            int bitNumber = (opcode >> 3) & 0b0000'0111;

            uint8_t command[4] = {0xFD, 0xCB, (uint8_t)disp, (uint8_t)opcode};
            std::string hexCommand = DumpHelper::HexDumpBuffer(command, sizeof(command));

            // Generate reference result
            std::string referenceResult;

            switch (operationIndex)
            {
                case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: // RLC-RR, SLA-SRA, SLL-SRL
                {
                    if (registerName.empty())
                    {
                        referenceResult = StringHelper::Format("%s (iy+#%02X)", operation, displacement);
                    }
                    else
                    {
                        referenceResult =
                            StringHelper::Format("%s (iy+#%02X),%s", operation, displacement, registerName);
                    }
                }
                break;

                case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: // BIT operations
                {
                    referenceResult = StringHelper::Format("%s %d,(iy+#%02X)", operation, bitNumber, displacement);
                }
                break;

                case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: // RES operations
                case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: // SET operations
                {
                    if (registerName.empty())
                    {
                        referenceResult = StringHelper::Format("%s %d,(iy+#%02X)", operation, bitNumber, displacement);
                    }
                    else
                    {
                        referenceResult = StringHelper::Format("%s %d,(iy+#%02X),%s", operation, bitNumber, displacement, registerName);
                    }
                }
                break;
            }

            // Probe method under test and get result
            std::string result = _disasm->disassembleSingleCommand(command, sizeof(command));

            if (result != referenceResult)
            {
                snprintf(message, sizeof message, "Opcode 0x%02X. Displacement %d. Data '%s'. Expected '%s', found '%s'",
                         opcode,
                         (int8_t)displacement,
                         hexCommand.c_str(), referenceResult.c_str(), result.c_str());

                EXPECT_EQ(referenceResult, result) << message << std::endl;
                return;
            }
            else
            {
#ifdef _DEBUG
                //std::cout << std::left << std::setw(16) << hexCommand << std::setw(0) << result << std::endl;
#endif // _DEBUG
            }
        }
    }
}