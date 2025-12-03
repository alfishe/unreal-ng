#pragma once
#include "stdafx.h"

#include "debugger/disassembler/z80disasm.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <list>


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
    bool is_rom;       // True if this bank is mapped to ROM, false if RAM
    uint8_t page_num;  // ROM or RAM page number for this bank

    /// @brief Comparison operator for loop compression 
    bool operator==(const Z80BankInfo& rhs) const
    {
        return is_rom == rhs.is_rom && page_num == rhs.page_num;
    }
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
    std::array<uint16_t, 3> stack_top;  // For RET/RETI: top 3 values on the stack after return (SP, SP+2, SP+4); zeroed
                                        // for other instructions
    uint32_t loop_count = 1;            // Number of consecutive times this event occurred (for loop compression)
    bool was_hot = false;               // True if this event was ever in the hot buffer (never saved to file)

    /// @brief Comparison operator for loop compression (ignores flags and loop_count)
    bool operator==(const Z80ControlFlowEvent& rhs) const
    {
        return m1_pc == rhs.m1_pc && target_addr == rhs.target_addr && type == rhs.type && sp == rhs.sp &&
               opcode_bytes == rhs.opcode_bytes && banks == rhs.banks;
    }
};

/// @brief Represents a hot (pinned) event in the hot buffer.
struct HotEvent
{
    Z80ControlFlowEvent event;  // The compressed event
    uint32_t loop_count;        // Accumulated loop count
    uint64_t last_seen_frame;   // Last frame this event was seen
};

// Maximum initial and maximum buffer sizes
constexpr size_t CALLTRACE_INITIAL_SIZE = 1'000'000;                               // 1M events
constexpr size_t CALLTRACE_MAX_SIZE = (1ull << 30) / sizeof(Z80ControlFlowEvent);  // 1GiB worth of events

// Helper struct for fast cold buffer lookup (only fields that allow to determine it's the same instruction doing the same jump iteratively)
struct EventKey
{
    uint16_t m1_pc;
    uint16_t target_addr;
    uint32_t opcode_bytes_packed = 0;
    uint8_t opcode_len = 0;
    Z80CFType type;
    std::array<Z80BankInfo, 4> banks;
    bool operator==(const EventKey& rhs) const
    {
        return m1_pc == rhs.m1_pc && target_addr == rhs.target_addr && opcode_len == rhs.opcode_len &&
               opcode_bytes_packed == rhs.opcode_bytes_packed && type == rhs.type && banks == rhs.banks;
    }
};

namespace std
{
    template<>
    struct hash<EventKey>
    {
        size_t operator()(const EventKey& k) const
        {
            size_t h = 0;
            h ^= std::hash<uint16_t>()(k.m1_pc) << 1;
            h ^= std::hash<uint16_t>()(k.target_addr) << 2;
            h ^= std::hash<uint32_t>()(k.opcode_bytes_packed) << 3;
            h ^= std::hash<uint8_t>()(k.opcode_len) << 4;
            h ^= std::hash<int>()(static_cast<int>(k.type)) << 5;
            for (const auto& bank : k.banks)
            {
                h ^= std::hash<bool>()(bank.is_rom) << 6;
                h ^= std::hash<uint8_t>()(bank.page_num) << 7;
            }
            return h;
        }
    };
}

/// @brief Main class for call trace buffering with hot/cold segmentation and time-based pinning.
class CallTraceBuffer
{
public:
    CallTraceBuffer(size_t cold_capacity = CALLTRACE_INITIAL_SIZE, size_t hot_capacity = 1024,
                    uint32_t hot_threshold = 100, uint32_t hot_timeout_frames = 1);
    ~CallTraceBuffer();

    void LogEvent(const Z80ControlFlowEvent& event, uint64_t current_frame);
    void FlushHotBuffer(uint64_t current_frame);
    void Reset();
    size_t ColdSize() const;
    size_t ColdCapacity() const;
    size_t HotSize() const { return _hotBuffer.size(); }
    size_t HotCapacity() const { return _hotCapacity; }
    std::vector<Z80ControlFlowEvent> GetLatestCold(size_t count) const;
    std::vector<HotEvent> GetLatestHot(size_t count) const;
    std::vector<Z80ControlFlowEvent> GetAll() const;
    bool SaveToFile(const std::string& filename) const;
    bool LogIfControlFlow(EmulatorContext* context, Memory* memory, uint16_t address, uint64_t current_frame);

private:
    std::vector<Z80ControlFlowEvent> _coldBuffer;
    size_t _coldStart;
    size_t _coldEnd;
    size_t _coldSize;
    size_t _coldCapacity;
    std::vector<HotEvent> _hotBuffer;
    size_t _hotCapacity;
    uint32_t _hotThreshold;
    uint32_t _hotTimeoutFrames;
    mutable std::mutex _mutex;
    std::optional<size_t> findInHotBuffer(const Z80ControlFlowEvent& event);
    void moveToHotBuffer(const Z80ControlFlowEvent& event, uint64_t current_frame);
    void logToColdBuffer(const Z80ControlFlowEvent& event, uint64_t current_frame);
    void evictExpiredHotEvents(uint64_t current_frame);
    void Grow();

    // Fast lookup for cold buffer compression (LRU limited)
    static constexpr size_t kColdMapLimit = 100;
    std::unordered_map<EventKey, std::pair<size_t, std::list<EventKey>::iterator>> _coldMap;
    std::list<EventKey> _coldLRU;

    static const char* Z80CFTypeToString(Z80CFType type)
    {
        switch (type)
        {
            case Z80CFType::JP:   return "JP";
            case Z80CFType::JR:   return "JR";
            case Z80CFType::CALL: return "CALL";
            case Z80CFType::RST:  return "RST";
            case Z80CFType::RET:  return "RET";
            case Z80CFType::RETI: return "RETI";
            case Z80CFType::DJNZ: return "DJNZ";
            default:              return "UNKNOWN";
        }
    }
};