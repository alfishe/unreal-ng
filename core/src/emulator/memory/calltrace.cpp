#include "calltrace.h"

#include <algorithm>
#include <fstream>
#include <optional>

#include "common/stringhelper.h"
#include "debugger/debugmanager.h"

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
//   - Growth rule: When the buffer becomes full and a new event is logged, the buffer size is doubled (up to a maximum
//   limit).
//   - Limit: The buffer will not grow beyond CALLTRACE_MAX_SIZE (1GiB worth of events).
//   - When the buffer is full and at max size, the oldest events are overwritten by new ones (true ring buffer
//   behavior).
//   - All growth and access operations are thread-safe via a mutex.
//
// Example:
//   1. Buffer starts with 1M slots.
//   2. After 1M events, the next event triggers a resize to 2M slots.
//   3. This doubling continues until the max size is reached.
//   4. Beyond that, oldest events are overwritten.

/// @brief Construct a CallTraceBuffer with configurable buffer sizes and thresholds.
/// @param cold_capacity Number of events in the cold buffer (ring buffer).
/// @param hot_capacity Number of events in the hot buffer (for hot/pinned events).
/// @param hot_threshold Loop count threshold for promoting to hot buffer.
/// @param hot_timeout_frames Number of frames after which a hot event is evicted if not seen.
CallTraceBuffer::CallTraceBuffer(size_t cold_capacity, size_t hot_capacity, uint32_t hot_threshold,
                                 uint32_t hot_timeout_frames)
    : _coldBuffer(cold_capacity),
      _coldStart(0),
      _coldEnd(0),
      _coldSize(0),
      _coldCapacity(cold_capacity),
      _hotBuffer(),
      _hotCapacity(hot_capacity),
      _hotThreshold(hot_threshold),
      _hotTimeoutFrames(hot_timeout_frames)
{
}

/// @brief Destructor for CallTraceBuffer.
CallTraceBuffer::~CallTraceBuffer() = default;

/// @brief Try to find a matching event in the hot buffer.
/// @param event The event to search for.
/// @return Optional index of the found event, or std::nullopt if not found.
std::optional<size_t> CallTraceBuffer::findInHotBuffer(const Z80ControlFlowEvent& event)
{
    for (size_t i = 0; i < _hotBuffer.size(); ++i)
    {
        if (_hotBuffer[i].event == event)
        {
            return i;
        }
    }
    return std::nullopt;
}

/// @brief Helper to create EventKey from Z80ControlFlowEvent
static EventKey MakeEventKey(const Z80ControlFlowEvent& ev)
{
    EventKey key{};
    key.m1_pc = ev.m1_pc;
    key.target_addr = ev.target_addr;
    key.type = ev.type;
    key.banks = ev.banks;

    key.opcode_len = ev.opcode_bytes.size();
    if (key.opcode_len > 0)
    {
        memcpy(&key.opcode_bytes_packed, ev.opcode_bytes.data(),
               std::min<size_t>(key.opcode_len, sizeof(key.opcode_bytes_packed)));
    }

    return key;
}

/// @brief Add or update an event in the cold buffer (with loop compression).
/// @param event The event to log.
/// @param current_frame The current emulation frame.
void CallTraceBuffer::logToColdBuffer(const Z80ControlFlowEvent& event, uint64_t current_frame)
{
    EventKey key = MakeEventKey(event);
    auto it = _coldMap.find(key);
    if (it != _coldMap.end())
    {
        // Event exists: increment loop_count, update flags, move to front of LRU
        size_t idx = it->second.first;
        _coldBuffer[idx].loop_count++;
        _coldBuffer[idx].flags = event.flags;
    
        // LRU update
        _coldLRU.erase(it->second.second);
        _coldLRU.push_front(key);
        it->second.second = _coldLRU.begin();
    
        // Promote to hot if needed
        if (_coldBuffer[idx].loop_count > _hotThreshold && !_coldBuffer[idx].was_hot)
        {
            moveToHotBuffer(_coldBuffer[idx], current_frame);
        }
        return;
    }

    // New event: evict LRU if needed
    if (_coldMap.size() >= kColdMapLimit)
    {
        // Evict LRU
        const EventKey& lru_key = _coldLRU.back();
        auto lru_it = _coldMap.find(lru_key);
        if (lru_it != _coldMap.end())
        {
            size_t lru_idx = lru_it->second.first;

            // Remove from cold buffer by shifting
            for (size_t j = lru_idx; j + 1 < _coldSize; ++j)
            {
                size_t from = (_coldStart + j + 1) % _coldCapacity;
                size_t to = (_coldStart + j) % _coldCapacity;
                _coldBuffer[to] = _coldBuffer[from];

                // Update map indices for shifted events
                EventKey shifted_key = MakeEventKey(_coldBuffer[to]);
                auto shifted_it = _coldMap.find(shifted_key);
                if (shifted_it != _coldMap.end())
                    shifted_it->second.first = to;
            }

            _coldEnd = (_coldEnd + _coldCapacity - 1) % _coldCapacity;
            --_coldSize;
            _coldMap.erase(lru_it);
        }

        _coldLRU.pop_back();
    }

    // Insert new event
    _coldBuffer[_coldEnd] = event;
    size_t new_idx = _coldEnd;
    _coldEnd = (_coldEnd + 1) % _coldCapacity;
    if (_coldSize < _coldCapacity)
    {
        ++_coldSize;
    }
    else
    {
        _coldStart = (_coldStart + 1) % _coldCapacity;
    }
    
    _coldLRU.push_front(key);
    _coldMap[key] = std::make_pair(new_idx, _coldLRU.begin());
}

/// @brief Move an event from cold buffer to hot buffer.
/// @param event The event to promote.
/// @param current_frame The current emulation frame.
void CallTraceBuffer::moveToHotBuffer(const Z80ControlFlowEvent& event, uint64_t current_frame)
{
    // If hot buffer is full, evict the oldest expired event
    if (_hotBuffer.size() >= _hotCapacity)
    {
        evictExpiredHotEvents(current_frame);
        if (_hotBuffer.size() >= _hotCapacity)
        {
            // Still full, evict the oldest (least recently seen)
            auto oldest = std::min_element(
                _hotBuffer.begin(), _hotBuffer.end(),
                [](const HotEvent& a, const HotEvent& b) { return a.last_seen_frame < b.last_seen_frame; });
            if (oldest != _hotBuffer.end())
            {
                _hotBuffer.erase(oldest);
            }
        }
    }

    // Set was_hot = true for the event being promoted
    Z80ControlFlowEvent hotEvent = event;
    hotEvent.was_hot = true;
    // Add new hot event (preserve loop_count and was_hot)
    _hotBuffer.push_back({hotEvent, hotEvent.loop_count, current_frame});

    // Remove from cold map and LRU
    EventKey key = MakeEventKey(event);
    auto it = _coldMap.find(key);
    if (it != _coldMap.end())
    {
        size_t idx = it->second.first;
        // Remove from cold buffer by shifting
        for (size_t j = idx; j + 1 < _coldSize; ++j)
        {
            size_t from = (_coldStart + j + 1) % _coldCapacity;
            size_t to = (_coldStart + j) % _coldCapacity;
            _coldBuffer[to] = _coldBuffer[from];
            // Update map indices for shifted events
            EventKey shifted_key = MakeEventKey(_coldBuffer[to]);
            auto shifted_it = _coldMap.find(shifted_key);
            if (shifted_it != _coldMap.end())
                shifted_it->second.first = to;
        }

        _coldEnd = (_coldEnd + _coldCapacity - 1) % _coldCapacity;
        --_coldSize;

        _coldLRU.erase(it->second.second);
        _coldMap.erase(it);
    }
}

/// @brief Log a new control flow event, handling hot/cold segmentation and pinning.
/// @param event The event to log.
/// @param current_frame The current emulation frame.
void CallTraceBuffer::LogEvent(const Z80ControlFlowEvent& event, uint64_t current_frame)
{
    std::lock_guard<std::mutex> lock(_mutex);

    // 1. Check hot buffer
    auto hotIdx = findInHotBuffer(event);
    if (hotIdx)
    {
        _hotBuffer[*hotIdx].loop_count++;
        _hotBuffer[*hotIdx].last_seen_frame = current_frame;
        return;
    }

    // 2. Always log to cold buffer (handles compression and promotion internally)
    logToColdBuffer(event, current_frame);
}

/// @brief Flush hot buffer events that have expired (not seen for hot_timeout_frames).
/// @param current_frame The current emulation frame.
void CallTraceBuffer::FlushHotBuffer(uint64_t current_frame)
{
    std::lock_guard<std::mutex> lock(_mutex);
    evictExpiredHotEvents(current_frame);
}

/// @brief Evict and serialize expired hot events.
/// @param current_frame The current emulation frame.
void CallTraceBuffer::evictExpiredHotEvents(uint64_t current_frame)
{
    for (auto it = _hotBuffer.begin(); it != _hotBuffer.end();)
    {
        if (current_frame - it->last_seen_frame > _hotTimeoutFrames)
        {
            // Transfer expired hot event back to cold buffer, preserve was_hot and loop_count
            Z80ControlFlowEvent ev = it->event;
            ev.loop_count = it->loop_count;
            ev.was_hot = true;
            logToColdBuffer(ev, current_frame);
            it = _hotBuffer.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

/// @brief Reset both hot and cold buffers, clearing all events.
void CallTraceBuffer::Reset()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _coldStart = _coldEnd = _coldSize = 0;
    _hotBuffer.clear();
}

/// @brief Get the current number of events in the cold buffer.
/// @return Number of events stored in the cold buffer.
size_t CallTraceBuffer::ColdSize() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _coldSize;
}

/// @brief Get the current cold buffer capacity.
/// @return Cold buffer capacity.
size_t CallTraceBuffer::ColdCapacity() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _coldCapacity;
}

/// @brief Get the latest N events from the cold buffer.
/// @param count Number of events to retrieve.
/// @return Vector of events, up to 'count' in size.
std::vector<Z80ControlFlowEvent> CallTraceBuffer::GetLatestCold(size_t count) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Z80ControlFlowEvent> result;
    if (_coldSize == 0)
        return result;
    count = std::min(count, _coldSize);
    for (size_t i = 0; i < count; ++i)
    {
        size_t idx = (_coldEnd + _coldCapacity - count + i) % _coldCapacity;
        result.push_back(_coldBuffer[idx]);
    }
    return result;
}

/// @brief Get all events currently in the cold buffer, in order from oldest to newest.
/// @return Vector of all events.
std::vector<Z80ControlFlowEvent> CallTraceBuffer::GetAll() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<Z80ControlFlowEvent> result;
    for (size_t i = 0; i < _coldSize; ++i)
    {
        result.push_back(_coldBuffer[(_coldStart + i) % _coldCapacity]);
    }
    return result;
}

/// @brief Save all events (cold and hot) to a YAML file.
/// @param filename Output file path.
/// @return True if successful, false otherwise.
bool CallTraceBuffer::SaveToFile(const std::string& filename) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::ofstream out(filename);
    if (!out)
        return false;
    out << "calltrace:" << std::endl;

    // Save cold buffer events
    for (size_t i = 0; i < _coldSize; ++i)
    {
        const auto& ev = _coldBuffer[(_coldStart + i) % _coldCapacity];
        out << StringHelper::Format("  - idx: %d", (int)i) << std::endl;
        out << StringHelper::Format("    m1_pc: 0x%04X", ev.m1_pc) << std::endl;
        out << StringHelper::Format("    type: %s", Z80CFTypeToString(ev.type)) << std::endl;
        out << StringHelper::Format("    target: 0x%04X", ev.target_addr) << std::endl;
        out << StringHelper::Format("    flags: 0x%02X", ev.flags) << std::endl;

        if (ev.loop_count > 1)
        {
            out << StringHelper::Format("    loop_count: %u", ev.loop_count) << std::endl;
        }
        
        out << StringHelper::Format("    sp: 0x%04X", ev.sp) << std::endl;
        out << "    opcodes: [";
        for (size_t j = 0; j < ev.opcode_bytes.size(); ++j)
        {
            out << StringHelper::Format("0x%02X", (int)ev.opcode_bytes[j]);
            if (j + 1 < ev.opcode_bytes.size())
                out << ", ";
        }
        out << "]" << std::endl;
        out << "    banks: [";
        for (int b = 0; b < 4; ++b)
        {
            out << StringHelper::Format("{is_rom: %s, page: %d}", ev.banks[b].is_rom ? "true" : "false",
                                        (int)ev.banks[b].page_num);
            if (b < 3)
                out << ", ";
        }
        out << "]" << std::endl;
        out << "    stack_top: [";
        for (int s = 0; s < 3; ++s)
        {
            out << StringHelper::Format("0x%04X", ev.stack_top[s]);
            if (s < 2)
                out << ", ";
        }
        out << "]" << std::endl;
    }
    
    // Save hot buffer events at the end
    for (size_t i = 0; i < _hotBuffer.size(); ++i)
    {
        const auto& hot = _hotBuffer[i];
        const auto& ev = hot.event;
        out << StringHelper::Format("  - idx: hot_%d", (int)i) << std::endl;
        out << StringHelper::Format("    m1_pc: 0x%04X", ev.m1_pc) << std::endl;
        out << StringHelper::Format("    type: %s", Z80CFTypeToString(ev.type)) << std::endl;
        out << StringHelper::Format("    target: 0x%04X", ev.target_addr) << std::endl;
        out << StringHelper::Format("    flags: 0x%02X", ev.flags) << std::endl;
        if (hot.loop_count > 1)
        {
            out << StringHelper::Format("    loop_count: %u", hot.loop_count) << std::endl;
        }
        out << StringHelper::Format("    sp: 0x%04X", ev.sp) << std::endl;

        out << "    opcodes: [";
        for (size_t j = 0; j < ev.opcode_bytes.size(); ++j)
        {
            out << StringHelper::Format("0x%02X", (int)ev.opcode_bytes[j]);
            if (j + 1 < ev.opcode_bytes.size())
                out << ", ";
        }
        out << "]" << std::endl;

        out << "    banks: [";
        for (int b = 0; b < 4; ++b)
        {
            out << StringHelper::Format("{is_rom: %s, page: %d}", ev.banks[b].is_rom ? "true" : "false",
                                        (int)ev.banks[b].page_num);
            if (b < 3)
                out << ", ";
        }
        out << "]" << std::endl;

        out << "    stack_top: [";
        for (int s = 0; s < 3; ++s)
        {
            out << StringHelper::Format("0x%04X", ev.stack_top[s]);
            if (s < 2)
                out << ", ";
        }
        out << "]" << std::endl;
    }

    return true;
}

/// @brief Fast check to determine if an instruction might be a control flow instruction
/// @param buffer Instruction bytes
/// @return true if the instruction could be a control flow instruction
static bool IsPotentialControlFlowInstruction(const std::vector<uint8_t>& buffer)
{
    if (buffer.empty())
        return false;
    
    uint8_t opcode = buffer[0];
    
    // Check for DD/FD prefixes first
    if (opcode == 0xDD || opcode == 0xFD)
    {
        if (buffer.size() < 2)
            return false;
        
        uint8_t second_byte = buffer[1];
        
        // DD/FD prefixed control flow instructions
        switch (second_byte)
        {
            // JP instructions
            case 0xC2: case 0xC3: case 0xCA: case 0xCB: case 0xD2: case 0xD3: case 0xDA: case 0xDB:
            case 0xE2: case 0xE3: case 0xEA: case 0xEB: case 0xF2: case 0xF3: case 0xFA: case 0xFB:
            
            // JR instructions  
            case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
            
            // CALL instructions
            case 0xC4: case 0xC5: case 0xCC: case 0xCD: case 0xD4: case 0xD5: case 0xDC: case 0xDD:
            case 0xE4: case 0xE5: case 0xEC: case 0xED: case 0xF4: case 0xF5: case 0xFC: case 0xFD:
            
            // RET instructions
            case 0xC0: case 0xC8: case 0xC9: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
            
            // RST instructions
            case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
            
            // DJNZ
            case 0x10:
            
            // JP (IX) / JP (IY)
            case 0xE9:
                return true;
                
            default:
                return false;
        }
    }
    
    // Check for ED prefix
    if (opcode == 0xED)
    {
        if (buffer.size() < 2)
            return false;
        
        uint8_t second_byte = buffer[1];
        
        // ED prefixed control flow instructions
        switch (second_byte)
        {
            // RETI, RETN and undocumented RETN variants
            case 0x4D: case 0x45: case 0x55: case 0x5D: case 0x65: case 0x6D: case 0x75: case 0x7D:
            
            // Block transfer instructions that can loop (LDIR, LDDR, CPIR, CPDR, INIR, INDR, OTIR, OTDR)
            // Disable for now since they do not change PC / use stack
            // case 0xB0: case 0xB8: case 0xB1: case 0xB9: case 0xB2: case 0xBA: case 0xB3: case 0xBB:
            //    return true;
                
            default:
                return false;
        }
    }
    
    // Non-prefixed control flow instructions
    switch (opcode)
    {
        // JP instructions
        case 0xC2: case 0xC3: case 0xCA: case 0xCB: case 0xD2: case 0xD3: case 0xDA: case 0xDB:
        case 0xE2: case 0xE3: case 0xEA: case 0xEB: case 0xF2: case 0xF3: case 0xFA: case 0xFB:
        
        // JP (HL)
        case 0xE9:
        
        // JR instructions  
        case 0x18: case 0x20: case 0x28: case 0x30: case 0x38:
        
        // CALL instructions
        case 0xC4: case 0xC5: case 0xCC: case 0xCD: case 0xD4: case 0xD5: case 0xDC: case 0xDD:
        case 0xE4: case 0xE5: case 0xEC: case 0xED: case 0xF4: case 0xF5: case 0xFC: case 0xFD:
        
        // RET instructions
        case 0xC0: case 0xC8: case 0xC9: case 0xD0: case 0xD8: case 0xE0: case 0xE8: case 0xF0: case 0xF8:
        
        // RST instructions
        case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        
        // DJNZ
        case 0x10:
            return true;
            
        default:
            return false;
    }
}

/// @brief Disassembles and logs a control flow event if the instruction at the given address is a taken control flow
/// instruction.
/// @param context EmulatorContext for access to CPU, disassembler, etc.
/// @param memory Pointer to Memory for reading bytes and bank info.
/// @param address The Z80 address of the instruction to check.
/// @param current_frame The current emulation frame.
/// @return true if an event was logged, false otherwise.
bool CallTraceBuffer::LogIfControlFlow(EmulatorContext* context, Memory* memory, uint16_t address,
                                       uint64_t current_frame)
{
    // Validate input pointers
    if (!context || !memory)
        return false;
    if (!context->pCore || !context->pCore->GetZ80())
        return false;
    Z80* z80 = context->pCore->GetZ80();
    Z80Registers* regs = static_cast<Z80Registers*>(z80);

    Z80Disassembler* disasm = nullptr;
    if (context->pDebugManager)
    {
        disasm = context->pDebugManager->GetDisassembler().get();
    }

    if (!z80 || !disasm || !memory)
        return false;

    // Read instruction bytes from memory at the given address
    static thread_local std::vector<uint8_t> buffer_bytes(Z80Disassembler::MAX_INSTRUCTION_LENGTH);
    for (size_t i = 0; i < Z80Disassembler::MAX_INSTRUCTION_LENGTH; ++i)
        buffer_bytes[i] = memory->DirectReadFromZ80Memory(address + i);

    // Fast-path check: if this doesn't look like a control flow instruction, skip expensive disassembly
    if (!IsPotentialControlFlowInstruction(buffer_bytes))
        return false;

    // Disassemble the instruction
    DecodedInstruction decoded;
    disasm->disassembleSingleCommandWithRuntime(buffer_bytes, address, nullptr, regs, memory, &decoded);

    // Prepare variables for event details
    uint16_t target = 0;
    Z80CFType type;
    std::array<Z80BankInfo, 4> banks;
    uint16_t sp = 0;
    std::array<uint16_t, 3> stack_top = {0, 0, 0};

    // Determine if the instruction is a taken control flow instruction
    bool taken = false;
    if (!decoded.isValid ||
        !(decoded.hasJump || decoded.hasRelativeJump || decoded.hasReturn || decoded.isRst || decoded.isDjnz))
        return false;

    // Identify the control flow type and target address
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
            // Use annotation to determine if the jump is taken
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
            // Use annotation to determine if the relative jump is taken
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

    // Gather current bank mapping for all 4 Z80 banks
    for (int i = 0; i < 4; ++i)
    {
        Z80BankInfo bi;
        bi.is_rom = (memory->GetMemoryBankMode(i) == BANK_ROM);
        bi.page_num = bi.is_rom ? memory->GetROMPageForBank(i) : memory->GetRAMPageForBank(i);
        banks[i] = bi;
    }
    sp = z80->sp;

    // If this is a return instruction, extract the top 3 values from the stack
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

    // Create and populate the control flow event
    Z80ControlFlowEvent ev;
    ev.m1_pc = address;
    ev.target_addr = target;
    ev.opcode_bytes = decoded.instructionBytes;
    ev.flags = z80->f;
    ev.type = type;
    ev.banks = banks;
    ev.sp = sp;
    ev.stack_top = stack_top;

    // Log the event to the buffer
    this->LogEvent(ev, current_frame);

    return true;
}

/// @brief Get the latest N hot events from the hot buffer.
/// @param count Number of events to retrieve.
/// @return Vector of hot events, up to 'count' in size.
std::vector<HotEvent> CallTraceBuffer::GetLatestHot(size_t count) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<HotEvent> result;
    if (_hotBuffer.empty())
        return result;
    count = std::min(count, _hotBuffer.size());
    result.reserve(count);
    for (size_t i = _hotBuffer.size() - count; i < _hotBuffer.size(); ++i)
    {
        result.push_back(_hotBuffer[i]);
    }
    return result;
}