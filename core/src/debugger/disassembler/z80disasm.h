#pragma once
#include "stdafx.h"

/// region <Types>

struct OpCode
{
    uint8_t flags;
    uint8_t t;				// T-states for unconditional operations
    uint8_t met_t;          // T-states for conditional operation when condition met
    uint8_t notmet_t;       // T-states for conditional operation when condition not met
    const char* mnem;		// mnemonic
};

constexpr uint8_t OF_NONE = 0;
constexpr uint8_t OF_PREFIX = 1;
constexpr uint8_t OF_EXT = OF_PREFIX;
constexpr uint8_t OF_SKIPABLE = (1 << 1);	// opcode is skippable by f8
constexpr uint8_t OF_RELJUMP = (1 << 2);    // opcode uses relative jump offset as operand
constexpr uint8_t OF_MBYTE = (1 << 3);		// operand is byte from memory
constexpr uint8_t OF_MWORD = (1 << 4);		// operand is word from memory
constexpr uint8_t OF_MEMADR = (1 << 5);		// operand contains memory address (nn)
constexpr uint8_t OF_CONDITION = (1 << 6);  // operation checks condition
constexpr uint8_t OF_VAR_T = (1 << 7);      // operation takes variable number of t-states (i.e. cycles)

/// endergion </Types>

class Z80Disassembler
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISASSEMBLER;
    const uint16_t _SUBMODULE = PlatformDisassemblerSubmodulesEnum::SUBMODULE_DISASSEMBLER_CORE;
    /// endregion </ModuleLogger definitions for Module/Submodule>

protected:
    static OpCode noprefixOpcodes[256];
    static OpCode cbOpcodes[256];
    static OpCode ddOpcodes[256];
    static OpCode edOpcodes[256];
    static OpCode fdOpcodes[256];
    static OpCode ddcbOpcodes[256];
    static OpCode fdcbOpcodes[256];

    /// region <Fields>
protected:
    ModuleLogger* _logger;
    /// endregion </Fields>

public:
    std::string disassembleSingleCommand(const uint8_t* buffer, size_t len);

    uint8_t getByte();
    uint16_t getWord();
    int getRelativeOffset();
};
