#pragma once

#include "stdafx.h"

#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/memory.h"
#include "debugger/disassembler/z80disasm.h"
#include <vector>
#include <array>
#include <cstdint>
#include <string>
#include <mutex>

// Enum for control flow instruction types
enum class Z80CFType : uint8_t
{
    JP,
    JR,
    CALL,
    RST,
    RET,
    RETI,
    DJNZ
};

// -----------------------------------------------------------------------------
// Z80ControlFlowEvent: Structure for logging Z80 control flow instructions
// -----------------------------------------------------------------------------
// This structure is used to record every taken control flow event (JP, JR, CALL,
// RST, RET, RETI, DJNZ) during Z80 emulation. Each event captures:
//   - The address and type of the instruction
//   - The resolved jump/return/call target
//   - The full opcode bytes (including prefixes and operands)
//   - The CPU flags at the time of execution
//   - The current memory bank mapping for all 4 Z80 banks (ROM/RAM and page)
//   - The stack pointer (SP) after execution
//   - The top 3 values on the stack after a return (for RET/RETI), zeroed otherwise
//
// This data enables detailed post-mortem analysis, call/return tracing, and
// memory mapping correlation for debugging and profiling.
// -----------------------------------------------------------------------------

/// @brief Describes the mapping of a single Z80 memory bank (16K region)
/// @details
///   - is_rom: true if this bank is mapped to ROM, false if RAM
///   - page_num: the ROM or RAM page number currently mapped to this bank
struct Z80BankInfo
{
    bool is_rom;      // True if this bank is mapped to ROM, false if RAM
    uint8_t page_num; // ROM or RAM page number for this bank
};

/// @brief Describes a single control flow event in the Z80 CPU
struct Z80ControlFlowEvent
{
    uint16_t m1_pc;                     // Address of the instruction being executed (PC at M1 cycle)
    uint16_t target_addr;               // The resolved jump/call/return address (where control will go)
    std::vector<uint8_t> opcode_bytes;  // Full instruction bytes (including prefixes and operands)
    uint8_t flags;                      // Z80 F register (flags) at the time of execution
    Z80CFType type;                     // Type of control flow instruction (JP, JR, CALL, RST, RET, RETI, DJNZ)
    std::array<Z80BankInfo, 4> banks;   // Mapping for all 4 Z80 banks (0-3): ROM/RAM and page number
    uint16_t sp;                        // Stack pointer after execution of the instruction
    std::array<uint16_t, 3> stack_top;  // For RET/RETI: top 3 values on the stack after return (SP, SP+2, SP+4); zeroed for other instructions
};

// Maximum initial and maximum buffer sizes
constexpr size_t CALLTRACE_INITIAL_SIZE = 1'000'000;                               // 1M events
constexpr size_t CALLTRACE_MAX_SIZE = (1ull << 30) / sizeof(Z80ControlFlowEvent);  // 1GiB worth of events

class CallTraceBuffer
{
public:
    CallTraceBuffer();
    ~CallTraceBuffer();

    void LogEvent(const Z80ControlFlowEvent& event);
    void Reset();
    size_t Size() const;
    size_t Capacity() const;
    std::vector<Z80ControlFlowEvent> GetLatest(size_t count) const;
    std::vector<Z80ControlFlowEvent> GetAll() const;
    bool SaveToFile(const std::string& filename) const;

    /**
     * @brief Disassembles and logs a control flow event if the instruction at the given address is a taken control flow instruction.
     * @param context EmulatorContext for access to CPU, disassembler, etc.
     * @param memory Pointer to Memory for reading bytes and bank info.
     * @param address The Z80 address of the instruction to check.
     * @return true if an event was logged, false otherwise.
     */
    bool LogIfControlFlow(EmulatorContext* context, Memory* memory, uint16_t address);

private:
    void Grow();
    std::vector<Z80ControlFlowEvent> _buffer;
    size_t _start;
    size_t _end;
    size_t _size;
    size_t _capacity;
    mutable std::mutex _mutex;
};