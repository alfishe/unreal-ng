#include "calltrace.h"

#include "common/stringhelper.h"
#include <algorithm>
#include <fstream>

// @file calltrace.cpp
// @brief Implements the CallTraceBuffer for Z80 control flow event tracing.
//
// @example Typical usage flow:
//   CallTraceBuffer buffer;
//   buffer.LogEvent(event); // Log a new control flow event
//   auto latest = buffer.GetLatest(10); // Get latest 10 events
//   buffer.SaveToFile("trace.bin"); // Save all events to file
//   buffer.Reset(); // Clear the buffer
//
// The buffer is used by MemoryAccessTracker to automatically log Z80 control flow events.

//
// @section Buffer Growth Algorithm
//
// The CallTraceBuffer is implemented as a circular (ring) buffer with dynamic growth:
//   - Initial state: The buffer is allocated with CALLTRACE_INITIAL_SIZE entries (1M events).
//   - Growth rule: When the buffer becomes full and a new event is logged, the buffer size is doubled (up to a maximum limit).
//   - Limit: The buffer will not grow beyond CALLTRACE_MAX_SIZE (1GiB worth of events).
//   - When the buffer is full and at max size, the oldest events are overwritten by new ones (true ring buffer behavior).
//   - All growth and access operations are thread-safe via a mutex.
//
// Example:
//   1. Buffer starts with 1M slots.
//   2. After 1M events, the next event triggers a resize to 2M slots.
//   3. This doubling continues until the max size is reached.
//   4. Beyond that, oldest events are overwritten.

CallTraceBuffer::CallTraceBuffer() : _start(0), _end(0), _size(0), _capacity(CALLTRACE_INITIAL_SIZE)
{
    // Allocate the initial buffer with CALLTRACE_INITIAL_SIZE entries.
    _buffer.resize(_capacity);
}

CallTraceBuffer::~CallTraceBuffer() = default;

/// @brief Log a new Z80 control flow event into the buffer.
/// @param event The event to log.
/// @details Ring buffer logic and size growth control provided
void CallTraceBuffer::LogEvent(const Z80ControlFlowEvent& event)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_size < _capacity)
    {
        // Buffer not full yet, just increment size used
        _buffer[_end] = event;
        _end = (_end + 1) % _capacity;
        ++_size;
    }
    else
    {
        // Buffer is full
        if (_capacity < CALLTRACE_MAX_SIZE)
        {
            // Buffer can grow: double the buffer size before writing the new event
            // This preserves all existing events (no overwrite)
            Grow();
            // After growing, _end points to the next free slot
            _buffer[_end] = event;
            _end = (_end + 1) % _capacity;
            ++_size;
        }
        else
        {
            // Buffer is at max capacity: must overwrite the oldest event
            // Move _start forward to drop the oldest event
            _buffer[_end] = event;
            _end = (_end + 1) % _capacity;
            _start = (_start + 1) % _capacity;
            // _size remains at max
        }
    }
}

/// @brief Double the buffer size, up to the maximum allowed.
/// @details Growth algorithm:
///   - When the buffer is full and not at max size, allocate a new buffer with double the current capacity.
///   - Copy all existing events in order from oldest to newest into the new buffer.
///   - Reset start/end pointers to match the new buffer layout.
///   - If already at max size, do not grow further; continue overwriting oldest events.
void CallTraceBuffer::Grow()
{
    // Calculate new capacity, but do not exceed the maximum allowed
    size_t new_capacity = std::min(_capacity * 2, CALLTRACE_MAX_SIZE);
    if (new_capacity == _capacity) // Already at max size, do not grow further
        return;

    // Allocate a new buffer with the increased capacity
    std::vector<Z80ControlFlowEvent> new_buffer(new_capacity);

    // Copy all existing events from the old buffer to the new buffer in order
    // The ring buffer may be wrapped, so use modulo arithmetic
    for (size_t i = 0; i < _size; ++i)
    {
        // Calculate the index in the old buffer for the i-th oldest event
        size_t old_idx = (_start + i) % _capacity;

        // Place the event in the new buffer at the same logical position
        new_buffer[i] = _buffer[old_idx];
    }

    // Replace the old buffer with the new, larger buffer
    _buffer = std::move(new_buffer);

    // After copying, the oldest event is at index 0, newest at _size-1
    // Reset start and end pointers to match the new layout
    _start = 0;
    _end = _size;
    _capacity = new_capacity;
}

/// @brief Reset the buffer, clearing all events.
void CallTraceBuffer::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _start = _end = _size = 0;
}

/// @brief Get the current number of events in the buffer.
/// @return Number of events stored.
size_t CallTraceBuffer::Size() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _size;
}

/// @brief Get the current buffer capacity (number of events it can hold before growing).
/// @return Buffer capacity.
size_t CallTraceBuffer::Capacity() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _capacity;
}

/// @brief Get the latest N events from the buffer (most recent last).
/// @param count Number of events to retrieve.
/// @return Vector of events, up to 'count' in size.
std::vector<Z80ControlFlowEvent> CallTraceBuffer::GetLatest(size_t count) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Z80ControlFlowEvent> result;
    if (_size == 0)
        return result;
    count = std::min(count, _size);
    for (size_t i = 0; i < count; ++i)
    {
        size_t idx = (_end + _capacity - count + i) % _capacity;
        result.push_back(_buffer[idx]);
    }
    return result;
}

/// @brief Get all events currently in the buffer, in order from oldest to newest.
/// @return Vector of all events.
std::vector<Z80ControlFlowEvent> CallTraceBuffer::GetAll() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Z80ControlFlowEvent> result;
    for (size_t i = 0; i < _size; ++i)
    {
        result.push_back(_buffer[(_start + i) % _capacity]);
    }
    return result;
}

/// @brief Save all events in the buffer to a binary file.
/// @param filename Path to the output file.
/// @return True if successful, false otherwise.
bool CallTraceBuffer::SaveToFile(const std::string& filename) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::ofstream out(filename);
    if (!out)
        return false;
    out << "calltrace:" << std::endl;
    for (size_t i = 0; i < _size; ++i)
    {
        const auto& ev = _buffer[(_start + i) % _capacity];
        out << StringHelper::Format("  - idx: %d", (int)i) << std::endl;
        out << StringHelper::Format("    m1_pc: 0x%04X", ev.m1_pc) << std::endl;
        out << StringHelper::Format("    type: %d", (int)ev.type) << std::endl;
        out << StringHelper::Format("    target: 0x%04X", ev.target_addr) << std::endl;
        out << StringHelper::Format("    flags: 0x%02X", ev.flags) << std::endl;
        out << StringHelper::Format("    sp: 0x%04X", ev.sp) << std::endl;

        out << "    opcodes: [";
        for (size_t j = 0; j < ev.opcode_bytes.size(); ++j)
        {
            out << StringHelper::Format("0x%02X", (int)ev.opcode_bytes[j]);
            if (j + 1 < ev.opcode_bytes.size()) out << ", ";
        }
        out << "]" << std::endl;

        out << "    banks: [";
        for (int b = 0; b < 4; ++b)
        {
            out << StringHelper::Format("{is_rom: %s, page: %d}", ev.banks[b].is_rom ? "true" : "false", (int)ev.banks[b].page_num);
            if (b < 3) out << ", ";
        }
        out << "]" << std::endl;

        out << "    stack_top: [";
        for (int s = 0; s < 3; ++s)
        {
            out << StringHelper::Format("0x%04X", ev.stack_top[s]);
            if (s < 2) out << ", ";
        }
        out << "]" << std::endl;
    }
    
    return true;
}

bool CallTraceBuffer::LogIfControlFlow(EmulatorContext* context, Memory* memory, uint16_t address)
{
    if (!context || !memory)
        return false;
    if (!context->pCore || !context->pCore->GetZ80())
        return false;
    Z80* z80 = context->pCore->GetZ80();
    Z80Registers* regs = static_cast<Z80Registers*>(z80);
    // Get or create a disassembler
    static thread_local std::unique_ptr<Z80Disassembler> disasm;
    if (!disasm)
        disasm = std::make_unique<Z80Disassembler>(context);
    // Fetch up to 4 bytes for the instruction
    std::vector<uint8_t> buffer_bytes(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (size_t i = 0; i < buffer_bytes.size(); ++i)
        buffer_bytes[i] = memory->DirectReadFromZ80Memory(address + i);
    DecodedInstruction decoded;
    disasm->disassembleSingleCommandWithRuntime(buffer_bytes, address, nullptr, regs, memory, &decoded);
    // Use the same logic as before to determine if this is a taken control flow event
    uint16_t target = 0;
    Z80CFType type;
    std::array<Z80BankInfo, 4> banks;
    uint16_t sp = 0;
    std::array<uint16_t, 3> stack_top = {0, 0, 0};
    if (!z80 || !disasm || !memory)
        return false;
    bool taken = false;
    if (!decoded.isValid ||
        !(decoded.hasJump || decoded.hasRelativeJump || decoded.hasReturn || decoded.isRst || decoded.isDjnz))
        return false;
    if (decoded.isRst)
    {
        taken = true;
        target = decoded.jumpAddr;
        type = Z80CFType::RST;
    }
    else if (decoded.isDjnz)
    {
        taken = ((z80->b - 1) != 0);
        target = decoded.relJumpAddr;
        type = Z80CFType::DJNZ;
    }
    else if (decoded.hasReturn)
    {
        taken = true;
        target = decoded.returnAddr;
        type = decoded.opcode.mnem && strstr(decoded.opcode.mnem, "reti") ? Z80CFType::RETI : Z80CFType::RET;
    }
    else if (decoded.hasJump && !decoded.hasRelativeJump)
    {
        if (decoded.hasCondition)
        {
            std::string ann = disasm->getCommandAnnotation(decoded, regs);
            taken = (ann.find("jump") != std::string::npos || ann.find("Calling") != std::string::npos);
        }
        else
        {
            taken = true;
        }
        target = decoded.jumpAddr;
        type = (decoded.opcode.mnem && strstr(decoded.opcode.mnem, "call")) ? Z80CFType::CALL : Z80CFType::JP;
    }
    else if (decoded.hasRelativeJump)
    {
        if (decoded.hasCondition)
        {
            std::string ann = disasm->getCommandAnnotation(decoded, regs);
            taken = (ann.find("jump") != std::string::npos || ann.find("Looping") != std::string::npos);
        }
        else
        {
            taken = true;
        }
        target = decoded.relJumpAddr;
        type = Z80CFType::JR;
    }
    else
    {
        taken = false;
    }
    if (!taken)
        return false;
    for (int i = 0; i < 4; ++i)
    {
        Z80BankInfo bi;
        bi.is_rom = (memory->GetMemoryBankMode(i) == BANK_ROM);
        bi.page_num = bi.is_rom ? memory->GetROMPageForBank(i) : memory->GetRAMPageForBank(i);
        banks[i] = bi;
    }
    sp = z80->sp;
    if (type == Z80CFType::RET || type == Z80CFType::RETI)
    {
        uint16_t stackptr = z80->sp;
        for (int i = 0; i < 3; ++i)
        {
            uint8_t lo = memory->DirectReadFromZ80Memory(stackptr + i * 2);
            uint8_t hi = memory->DirectReadFromZ80Memory(stackptr + i * 2 + 1);
            stack_top[i] = (hi << 8) | lo;
        }
    }
    else
    {
        stack_top = {0, 0, 0};
    }
    Z80ControlFlowEvent ev;
    ev.m1_pc = address;
    ev.target_addr = target;
    ev.opcode_bytes = decoded.instructionBytes;
    ev.flags = z80->f;
    ev.type = type;
    ev.banks = banks;
    ev.sp = sp;
    ev.stack_top = stack_top;
    this->LogEvent(ev);
    return true;
}