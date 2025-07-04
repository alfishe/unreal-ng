#pragma once
#include "stdafx.h"

#include <regex>
#include <vector>
#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

// Forward declarations
class ModuleLogger;
class DebugManager;
class LabelManager;
struct Z80Registers;
class Memory;

/// region <Types>

/// Represents opcode dictionary data record from disassembler decoding tables
struct OpCode
{
    uint32_t flags;
    uint8_t t;				// T-states for unconditional operations
    uint8_t met_t;          // T-states for conditional operation when condition met
    uint8_t notmet_t;       // T-states for conditional operation when condition not met
    const char* mnem;		// mnemonic
};

/// All information about single Z80 command after decoding and operand fetching
struct DecodedInstruction
{
    bool isValid = false;
    bool hasRuntime = false;

    /// region <Raw data>

    OpCode opcode;
    std::vector<uint8_t> instructionBytes;
    std::vector<uint8_t> operandBytes;

    uint8_t fullCommandLen = 0;
    uint8_t operandsLen = 0;

    uint16_t prefix = 0x0000;
    uint8_t command = 0x00;

    bool hasJump = false;
    bool hasRelativeJump = false;
    bool hasDisplacement = false;
    bool hasIndirect = false;
    bool hasReturn = false;
    bool hasByteOperand = false;
    bool hasWordOperand = false;

    bool hasCondition = false;
    bool hasVariableCycles = false;
    bool isRst = false;             // RST instruction (jump to predefined address)
    bool isBlockOp = false;         // Block operations (LDI, LDIR, etc.)
    bool isIO = false;              // I/O operations (IN, OUT)
    bool isInterrupt = false;       // Interrupt-related (EI, DI, IM)
    bool isRegExchange = false;     // Register exchange operations (EX)
    bool isDjnz = false;            // DJNZ instruction
    bool affectsFlags = false;      // Instruction modifies flags
    bool affectsAllFlags = false;   // All flags affected (AND, OR, XOR)
    bool affectsSZFlags = false;    // Only S and Z flags affected

    int8_t displacement = 0x00;             // 8-bit signed displacement (offset) for IX/IY indexed operations
    int8_t relJumpOffset = 0x00;            // 8-bit relative jump offset
    uint8_t byteOperand = 0x00;
    uint16_t wordOperand = 0x0000;

    /// endregion </Raw data>

    /// region <Runtime data>
    uint16_t instructionAddr = 0x0000;      // Address of first byte of instruction (prefix or opcode)
    uint16_t jumpAddr = 0x0000;             // Target jump or call address
    uint16_t relJumpAddr = 0x0000;          // Target address for relative jumps
    uint16_t displacementAddr = 0x0000;     // Target address for indexed (via IX/IY) operations
    uint16_t returnAddr = 0x0000;           // Address to return from call (from stack)
    /// endregion </Runtime data>

    std::string hexDump;
    std::string mnemonic;
    std::string label;
    std::string annotation;
    std::string comment;

    /// region <Constructors / Destructors>

    // Default constructor
    DecodedInstruction() = default;

    // Copy constructor
    DecodedInstruction(const DecodedInstruction&) = default;

    // Move constructor
    DecodedInstruction(DecodedInstruction&&) = default;

    // Copy assignment operator
    DecodedInstruction& operator=(const DecodedInstruction&) = default;

    // Move assignment operator
    DecodedInstruction& operator=(DecodedInstruction&&) = default;

    // Destructor
    virtual ~DecodedInstruction() = default;

    /// endregion </Constructors / Destructors>

    /// region <Helper methods>
    void clear()
    {
        *this = DecodedInstruction{}; // Reset to the default state
    }

    /// endregion </Helper methods>
};

constexpr uint32_t OF_NONE = 0;
constexpr uint32_t OF_PREFIX = 1;                   // current byte is opcode prefix
constexpr uint32_t OF_EXT = OF_PREFIX;              // alias to opcode prefix
constexpr uint32_t OF_MBYTE = (1UL << 1);           // operand is byte from memory
constexpr uint32_t OF_MWORD = (1UL << 2);           // operand is word from memory
constexpr uint32_t OF_DISP = (1UL << 3);            // opcode uses index register displacement, like: bit 0,(ix+<d>) or srl (iy+<d>),h
constexpr uint32_t OF_MEMADR = (1UL << 4);          // operand contains memory address (nn)
constexpr uint32_t OF_CONDITION = (1UL << 5);       // operation checks condition
constexpr uint32_t OF_RELJUMP = (1UL << 6);         // opcode uses relative jump offset as operand
constexpr uint32_t OF_VAR_T = (1UL << 7);           // operation takes variable number of t-states (i.e. cycles)
constexpr uint32_t OF_JUMP = (1UL << 8);            // operand is jump address (JP, JR)
constexpr uint32_t OF_CALL = (1UL << 9);            // operand is call address (CALL)
constexpr uint32_t OF_RET = (1UL << 10);            // operation will take next PC address from stack
constexpr uint32_t OF_RST = (1UL << 11);            // operation will jump to pre-defined address (RST n)
constexpr uint32_t OF_FLAGS_AFFECTED = (1UL << 12); // Instruction modifies flags
constexpr uint32_t OF_FLAGS_ALL = (1UL << 13);      // All flags affected (like AND, OR, XOR)
constexpr uint32_t OF_FLAGS_SZ = (1UL << 14);       // Only S and Z flags affected
constexpr uint32_t OF_REG_EXCHANGE = (1UL << 15);   // Register exchange operations (EX)
constexpr uint32_t OF_BLOCK = (1UL << 16);          // Block operations (LDI, LDIR, etc.)
constexpr uint32_t OF_IO = (1UL << 17);             // I/O operations (IN, OUT)
constexpr uint32_t OF_INTERRUPT = (1UL << 18);      // Interrupt-related (EI, DI, IM)
constexpr uint32_t OF_DJNZ = (1UL << 19);           // DJNZ instruction
constexpr uint32_t OF_INDIRECT = (1UL << 20);       // Command uses indirect addressing (via one register pairs)

namespace OpFlags
{
    struct Flag
    {
        uint32_t value;
        const char* name;
        const char* description;

        constexpr Flag(uint32_t v, const char* n, const char* desc) : 
            value(v), name(n), description(desc)
        {
        }

        constexpr operator uint32_t() const
        {
            return value;
        }
    };

    // Basic flags
    namespace Basic
    {
        constexpr Flag None{0, "None", "No flags set"};
        constexpr Flag Prefix{1, "Prefix", "Current byte is opcode prefix"};
        constexpr Flag Ext = Prefix;  // Alias
    }

    // Memory-related flags
    namespace Memory
    {
        constexpr Flag Byte{1UL << 1, "MByte", "Operand is byte from memory"};
        constexpr Flag Word{1UL << 2, "MWord", "Operand is word from memory"};
        constexpr Flag Disp{1UL << 3, "Disp", "Uses index register displacement"};
        constexpr Flag Addr{1UL << 4, "MemAddr", "Contains memory address"};
    }

    // Control flow flags
    namespace Flow
    {
        constexpr Flag Condition{1UL << 5, "Condition", "Operation checks condition"};
        constexpr Flag RelJump{1UL << 6, "RelJump", "Uses relative jump offset"};
        constexpr Flag Jump{1UL << 8, "Jump", "Jump or call address"};
        constexpr Flag Ret{1UL << 9, "Ret", "Takes next PC from stack"};
        constexpr Flag Rst{1UL << 10, "Rst", "Jumps to pre-defined address"};
    }

    // CPU flags
    namespace Flags
    {
        constexpr Flag Affected{1UL << 11, "FlagsAffected", "Instruction modifies flags"};
        constexpr Flag All{1UL << 12, "FlagsAll", "All flags affected"};
        constexpr Flag SZ{1UL << 13, "FlagsSZ", "Only S and Z flags affected"};
    }

    // Operation type flags
    namespace Operation
    {
        constexpr Flag VarCycles{1UL << 7, "VarCycles", "Variable number of t-states"};
        constexpr Flag RegExchange{1UL << 14, "RegExchange", "Register exchange operations"};
        constexpr Flag Block{1UL << 15, "Block", "Block operations (LDI, LDIR, etc.)"};
        constexpr Flag IO{1UL << 16, "IO", "I/O operations (IN, OUT)"};
        constexpr Flag Interrupt{1UL << 17, "Interrupt", "Interrupt-related (EI, DI, IM)"};
        constexpr Flag Djnz{1UL << 18, "Djnz", "DJNZ instruction"};
    }

    // Debug flags
    namespace Debug
    {
        constexpr Flag Skipable{1UL << 31, "Skipable", "Skippable during single step debug"};
    }

    // Helper functions
    inline std::string getFlagName(uint32_t flag)
    {
        if (flag == 0) return Basic::None.name;
        std::string result;
        // Check all flag groups
        const Flag* allFlags[] = 
        {
            &Basic::Prefix,
            &Memory::Byte, &Memory::Word, &Memory::Disp, &Memory::Addr,
            &Flow::Condition, &Flow::RelJump, &Flow::Jump, &Flow::Ret, &Flow::Rst,
            &Flags::Affected, &Flags::All, &Flags::SZ,
            &Operation::RegExchange, &Operation::Block, &Operation::IO,
            &Operation::Interrupt, &Operation::VarCycles,
            &Debug::Skipable
        };
        
        for (const Flag* f : allFlags)
        {
            if (flag & f->value)
            {
                if (!result.empty()) result += " | ";
                result += f->name;
            }
        }
        return result;
    }

    inline std::string getFlagDescription(uint32_t flag)
    {
        if (flag == 0) return Basic::None.description;
        std::string result;
        // Similar to getFlagName but returns descriptions
        const Flag* allFlags[] = 
        {
            &Basic::Prefix,
            &Memory::Byte, &Memory::Word, &Memory::Disp, &Memory::Addr,
            &Flow::Condition, &Flow::RelJump, &Flow::Jump, &Flow::Ret, &Flow::Rst,
            &Flags::Affected, &Flags::All, &Flags::SZ,
            &Operation::RegExchange, &Operation::Block, &Operation::IO,
            &Operation::Interrupt, &Operation::VarCycles,
            &Debug::Skipable
        };
        
        for (const Flag* f : allFlags)
        {
            if (flag & f->value)
            {
                if (!result.empty()) result += "; ";
                result += f->description;
            }
        }
        return result;
    }
}

/// endregion </Types>



class Z80Disassembler
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISASSEMBLER;
    const uint16_t _SUBMODULE = PlatformDisassemblerSubmodulesEnum::SUBMODULE_DISASSEMBLER_CORE;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    /// Maximum length of a Z80 instruction in bytes (prefix + opcode + operands)
    static constexpr size_t MAX_INSTRUCTION_LENGTH = 4;

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
    EmulatorContext* _context;
    /// endregion </Fields>

public:
    explicit Z80Disassembler(EmulatorContext* context);
    virtual ~Z80Disassembler() = default;

    void SetLogger(ModuleLogger* logger) { _logger = logger; }
    
    std::string disassembleSingleCommand(const std::vector<uint8_t>& buffer, uint16_t instructionAddr, uint8_t* commandLen = nullptr, DecodedInstruction* decoded = nullptr);
    std::string disassembleSingleCommandWithRuntime(const std::vector<uint8_t>& buffer, uint16_t instructionAddr, uint8_t* commandLen, Z80Registers* registers, Memory* memory, DecodedInstruction* decoded = nullptr);

    std::string getRuntimeHints(DecodedInstruction& decoded);
    
    // Helper methods for debugger step functionality
    bool shouldStepOver(const std::vector<uint8_t>& buffer);
    uint16_t getNextInstructionAddress(uint16_t currentAddress, Memory* memory);
        
    // Get address ranges that should be excluded from breakpoint triggering during step-over
    // Returns vector of address range pairs (start, end) that should be excluded
    std::vector<std::pair<uint16_t, uint16_t>> getStepOverExclusionRanges(
        uint16_t currentPC, Memory* memory, int maxDepth = 5);
    
    /// @brief Generate a runtime annotation for a decoded instruction
    /// @param decodedInstruction The decoded instruction
    /// @param registers CPU registers for runtime state evaluation
    /// @return String containing the annotation or empty string if not applicable
    std::string getCommandAnnotation(const DecodedInstruction& decodedInstruction, Z80Registers* registers);

    /// region <Helper methods>
protected:
    uint8_t getByte();
    uint16_t getWord();
    int getRelativeOffset();

    DecodedInstruction decodeInstruction(const std::vector<uint8_t>& buffer, uint16_t instructionAddr = 0, Z80Registers* registers = nullptr, Memory* memory = nullptr);
    OpCode getOpcode(uint16_t prefix, uint8_t fetchByte);
    uint8_t hasOperands(OpCode& opcode);
    std::string formatMnemonic(const DecodedInstruction& decoded);
    std::vector<uint8_t> parseOperands(std::string& mnemonic, uint8_t* expectedOperandsLen = nullptr);
    std::string formatOperandString(const DecodedInstruction& decoded, const std::string& mnemonic, std::vector<uint16_t>& values);
    
    // Helper method for step-over exclusion ranges
    void analyzeCalledFunction(uint16_t functionAddress, Memory* memory, 
                              std::vector<std::pair<uint16_t, uint16_t>>& ranges, int maxDepth);

    /// endregion </Helper methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class Z80DisassemblerCUT : public Z80Disassembler
{
public:
    // Add a constructor that calls the base class constructor with required parameters
    Z80DisassemblerCUT(EmulatorContext* context) : Z80Disassembler(context) {};

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