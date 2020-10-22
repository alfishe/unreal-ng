#pragma once
#include "stdafx.h"

#include <regex>
#include <vector>
#include "emulator/platform.h"

/// region <Types>

/// Represents opcode dictionary data record from disassembler decoding tables
struct OpCode
{
    uint8_t flags;
    uint8_t t;				// T-states for unconditional operations
    uint8_t met_t;          // T-states for conditional operation when condition met
    uint8_t notmet_t;       // T-states for conditional operation when condition not met
    const char* mnem;		// mnemonic
};

/// All information about single Z80 command after decoding and operand fetching
struct DecodedInstruction
{
    bool isValid = false;

    /// region <Raw data>

    OpCode opcode;
    std::vector<uint8_t> instructionBytes;
    std::vector<uint8_t> operandBytes;

    uint8_t fullCommandLen = 0;
    uint8_t operandsLen = 0;

    uint16_t prefix = 0x0000;
    uint8_t command = 0x00;

    bool hasDisplacement = false;
    bool hasJumpOffset = false;
    bool hasByteOperand = false;
    bool hasWordOperand = false;

    bool hasCondition = false;
    bool hasVariableCycles = false;

    uint8_t displacement = 0x00;
    uint8_t jumpOffset = 0x00;
    uint8_t byteOperand = 0x00;
    uint16_t wordOperand = 0x0000;

    /// endregion </Raw data>

    /// region <Runtime data>
    uint16_t instructionAddr = 0x0000;
    uint16_t jumpAddr = 0x0000;
    uint16_t displacementAddr = 0x0000;
    /// endregion </Runtime data>

    std::string hexDump;
    std::string mnemonic;
    std::string mnemonicWithLabel;
};

constexpr uint16_t OF_NONE = 0;
constexpr uint16_t OF_PREFIX = 1;               // current byte is opcode prefix
constexpr uint16_t OF_EXT = OF_PREFIX;          // alias to opcode prefix
constexpr uint16_t OF_MBYTE = (1 << 1);		    // operand is byte from memory
constexpr uint16_t OF_MWORD = (1 << 2);		    // operand is word from memory
constexpr uint16_t OF_DISP = (1 << 3);          // opcode uses index register displacement: bit 0,(ix+<d>) or srl (iy+<d>),h
constexpr uint16_t OF_MEMADR = (1 << 4);	    // operand contains memory address (nn)
constexpr uint16_t OF_CONDITION = (1 << 5);     // operation checks condition
constexpr uint16_t OF_RELJUMP = (1 << 6);       // opcode uses relative jump offset as operand
constexpr uint16_t OF_VAR_T = (1 << 7);         // operation takes variable number of t-states (i.e. cycles)

constexpr uint16_t OF_SKIPABLE = (1 << 8);      // opcode is skippable by f8

/// endregion </Types>

class ModuleLogger;

class Z80Disassembler
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISASSEMBLER;
    const uint16_t _SUBMODULE = PlatformDisassemblerSubmodulesEnum::SUBMODULE_DISASSEMBLER_CORE;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Static>
protected:
    static OpCode noprefixOpcodes[256];
    static OpCode cbOpcodes[256];
    static OpCode ddOpcodes[256];
    static OpCode edOpcodes[256];
    static OpCode fdOpcodes[256];
    static OpCode ddcbOpcodes[256];
    static OpCode fdcbOpcodes[256];

    static std::regex regexOpcodeOperands;

    /// endregion </Static>

    /// region <Fields>
protected:
    ModuleLogger* _logger;
    /// endregion </Fields>

public:
    std::string disassembleSingleCommand(const uint8_t* buffer, size_t len, uint8_t* commandLen = nullptr);

    /// region <Helper methods>
protected:
    uint8_t getByte();
    uint16_t getWord();
    int getRelativeOffset();

    DecodedInstruction decodeInstruction(const uint8_t* buffer, size_t len);
    OpCode getOpcode(uint16_t prefix, uint8_t fetchByte);
    uint8_t hasOperands(OpCode& opcode);
    std::string formatMnemonic(const DecodedInstruction& decoded);
    std::vector<uint8_t> parseOperands(std::string& mnemonic, uint8_t* expectedOperandsLen = nullptr);
    std::string formatOperandString(std::string& mnemonic, std::vector<uint16_t>& values);

    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class Z80DisassemblerCUT : public Z80Disassembler
{
public:
    using Z80Disassembler::getByte;
    using Z80Disassembler::getWord;
    using Z80Disassembler::getRelativeOffset;

    using Z80Disassembler::decodeInstruction;
    using Z80Disassembler::getOpcode;
    using Z80Disassembler::hasOperands;
    using Z80Disassembler::formatMnemonic;
    using Z80Disassembler::parseOperands;
    using Z80Disassembler::formatOperandString;

};
#endif // _CODE_UNDER_TEST