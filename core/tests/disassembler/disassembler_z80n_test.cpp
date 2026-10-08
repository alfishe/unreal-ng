#include "disassembler_opcode_test.h"

#include <vector>

/// Z80N mode: the ED-prefixed Next extensions; sizes and T-states per table.specnext.dev

TEST_F(Disassembler_Opcode_Test, Z80n_ExtensionsAreUnknownUnlessTheModeIsOn)
{
    std::vector<uint8_t> command = {0xED, 0x23};
    uint8_t len = 0;
    std::string plain = _disasm->disassembleSingleCommand(command, 0, &len);
    EXPECT_NE(plain, "swapnib");

    _disasm->SetZ80nMode(true);
    EXPECT_EQ(_disasm->disassembleSingleCommand(command, 0, &len), "swapnib");
    EXPECT_EQ(len, 2);
}

TEST_F(Disassembler_Opcode_Test, Z80n_SizesTextAndTStates)
{
    struct Case { std::vector<uint8_t> bytes; const char* text; int size; int t; };
    const std::vector<Case> cases = {
        {{0xED, 0x27, 0x55}, "test #55", 3, 11},
        {{0xED, 0x28}, "bsla de,b", 2, 8},
        {{0xED, 0x30}, "mul d,e", 2, 8},
        {{0xED, 0x31}, "add hl,a", 2, 8},
        {{0xED, 0x34, 0x34, 0x12}, "add hl,#1234", 4, 16},
        {{0xED, 0x8A, 0x12, 0x34}, "push #1234", 4, 23},
        {{0xED, 0x91, 0x07, 0x03}, "nextreg #07,#03", 4, 20},
        {{0xED, 0x92, 0x07}, "nextreg #07,a", 3, 17},
        {{0xED, 0x98}, "jp (c)", 2, 13},
        {{0xED, 0xA5}, "ldws", 2, 14},
    };
    _disasm->SetZ80nMode(true);
    for (const Case& c : cases)
    {
        uint8_t len = 0;
        DecodedInstruction decoded;
        std::string text = _disasm->disassembleSingleCommand(c.bytes, 0, &len, &decoded);
        EXPECT_EQ(text, c.text);
        EXPECT_EQ(len, c.size) << c.text;
        EXPECT_EQ(decoded.opcode.t, c.t) << c.text;
    }
}

TEST_F(Disassembler_Opcode_Test, Z80n_RepeatingBlockCopiesCarryBothCosts)
{
    _disasm->SetZ80nMode(true);
    for (uint8_t code : {0xB4, 0xB7, 0xBC})
    {
        DecodedInstruction decoded;
        _disasm->disassembleSingleCommand({0xED, code}, 0, nullptr, &decoded);
        EXPECT_EQ(decoded.opcode.met_t, 21);
        EXPECT_EQ(decoded.opcode.notmet_t, 16);
    }
}
