#include "opcode_profiler.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

#include "debugger/disassembler/z80disasm.h"
#include "emulator/emulatorcontext.h"

/// region <Constructors / Destructors>

OpcodeProfiler::OpcodeProfiler(EmulatorContext* context)
    : _context(context)
{
    // Allocate trace buffer
    _traceBuffer.resize(DEFAULT_TRACE_SIZE);
    
    // Initialize counters to zero (atomic array default-initialized)
    Clear();
}

/// endregion </Constructors / Destructors>

/// region <Session Control>

void OpcodeProfiler::Start()
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    Clear();  // Clear previous data
    _capturing = true;
}

void OpcodeProfiler::Stop()
{
    _capturing = false;
}

void OpcodeProfiler::Clear()
{
    // Reset counters
    for (auto& counter : _counters)
    {
        counter.store(0, std::memory_order_relaxed);
    }
    
    // Reset trace buffer
    _traceHead.store(0, std::memory_order_relaxed);
    _traceCount = 0;
    
    // Zero out trace entries
    std::memset(_traceBuffer.data(), 0, _traceBuffer.size() * sizeof(OpcodeTraceEntry));
}

/// endregion </Session Control>

/// region <Data Collection - Hot Path>

void OpcodeProfiler::LogExecution(uint16_t pc, uint16_t prefix, uint8_t opcode,
                                   uint8_t flags, uint8_t a, uint32_t frame, uint32_t tState)
{
    if (!_capturing)
        return;

    // Tier 1: Increment execution counter
    size_t index = ToIndex(prefix, opcode);
    if (index < COUNTER_TABLE_SIZE)
    {
        _counters[index].fetch_add(1, std::memory_order_relaxed);
    }

    // Tier 2: Add to trace ring buffer
    size_t head = _traceHead.load(std::memory_order_relaxed);
    OpcodeTraceEntry& entry = _traceBuffer[head];
    
    entry.pc = pc;
    entry.prefix = prefix;
    entry.opcode = opcode;
    entry.flags = flags;
    entry.a = a;
    entry.reserved = 0;
    entry.frame = frame;
    entry.tState = tState;

    // Advance head (wrap around)
    size_t newHead = (head + 1) % _traceBuffer.size();
    _traceHead.store(newHead, std::memory_order_relaxed);
    
    // Track actual entry count (up to capacity)
    if (_traceCount < _traceBuffer.size())
    {
        _traceCount++;
    }
}

/// endregion </Data Collection - Hot Path>

/// region <Data Retrieval>

ProfilerStatus OpcodeProfiler::GetStatus() const
{
    ProfilerStatus status;
    status.capturing = _capturing;
    status.totalExecutions = GetTotalExecutions();
    status.traceSize = static_cast<uint32_t>(_traceCount);
    status.traceCapacity = static_cast<uint32_t>(_traceBuffer.size());
    return status;
}

uint64_t OpcodeProfiler::GetTotalExecutions() const
{
    uint64_t total = 0;
    for (const auto& counter : _counters)
    {
        total += counter.load(std::memory_order_relaxed);
    }
    return total;
}

uint64_t OpcodeProfiler::GetCount(uint16_t prefix, uint8_t opcode) const
{
    size_t index = ToIndex(prefix, opcode);
    if (index < COUNTER_TABLE_SIZE)
    {
        return _counters[index].load(std::memory_order_relaxed);
    }
    return 0;
}

std::vector<OpcodeCounter> OpcodeProfiler::GetTopOpcodes(size_t limit) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    // Collect all non-zero counters
    std::vector<OpcodeCounter> result;
    result.reserve(COUNTER_TABLE_SIZE);

    // Non-prefixed opcodes (0-255)
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = PREFIX_NONE;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(PREFIX_NONE, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // CB prefixed (256-511)
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[256 + op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = PREFIX_CB;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(PREFIX_CB, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // DD prefixed (512-767)
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[512 + op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = PREFIX_DD;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(PREFIX_DD, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // ED prefixed (768-1023)
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[768 + op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = PREFIX_ED;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(PREFIX_ED, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // FD prefixed (1024-1279)
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[1024 + op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = PREFIX_FD;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(PREFIX_FD, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // DDCB prefixed (1280-1535)
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[1280 + op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = PREFIX_DDCB;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(PREFIX_DDCB, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // FDCB prefixed (1536-1791)
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[1536 + op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = PREFIX_FDCB;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(PREFIX_FDCB, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // Sort by count descending
    std::sort(result.begin(), result.end(),
              [](const OpcodeCounter& a, const OpcodeCounter& b) { return a.count > b.count; });

    // Limit results
    if (result.size() > limit)
    {
        result.resize(limit);
    }

    return result;
}

std::vector<OpcodeCounter> OpcodeProfiler::GetByPrefix(uint16_t prefix) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    std::vector<OpcodeCounter> result;
    result.reserve(256);

    size_t offset = GetPrefixOffset(prefix);
    
    for (uint16_t op = 0; op < 256; ++op)
    {
        uint64_t count = _counters[offset + op].load(std::memory_order_relaxed);
        if (count > 0)
        {
            OpcodeCounter c;
            c.prefix = prefix;
            c.opcode = static_cast<uint8_t>(op);
            c.count = count;
            c.mnemonic = GetMnemonic(prefix, static_cast<uint8_t>(op));
            result.push_back(c);
        }
    }

    // Sort by count descending
    std::sort(result.begin(), result.end(),
              [](const OpcodeCounter& a, const OpcodeCounter& b) { return a.count > b.count; });

    return result;
}

std::vector<OpcodeTraceEntry> OpcodeProfiler::GetRecentTrace(size_t count) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    
    std::vector<OpcodeTraceEntry> result;
    
    size_t actualCount = std::min(count, _traceCount);
    if (actualCount == 0)
        return result;

    result.reserve(actualCount);

    size_t head = _traceHead.load(std::memory_order_relaxed);
    size_t capacity = _traceBuffer.size();

    // Walk backwards from head (newest first)
    for (size_t i = 0; i < actualCount; ++i)
    {
        size_t idx = (head + capacity - 1 - i) % capacity;
        result.push_back(_traceBuffer[idx]);
    }

    return result;
}

/// endregion </Data Retrieval>

/// region <Export>

bool OpcodeProfiler::SaveToFile(const std::string& path) const
{
    std::ofstream out(path);
    if (!out.is_open())
        return false;

    ProfilerStatus status = GetStatus();
    auto topOpcodes = GetTopOpcodes(100);

    out << "# Z80 Opcode Profile Export\n";
    out << "# Generated: " << std::chrono::system_clock::now().time_since_epoch().count() << "\n\n";
    
    out << "status:\n";
    out << "  capturing: " << (status.capturing ? "true" : "false") << "\n";
    out << "  total_executions: " << status.totalExecutions << "\n";
    out << "  trace_size: " << status.traceSize << "\n";
    out << "  trace_capacity: " << status.traceCapacity << "\n\n";

    out << "top_opcodes:\n";
    for (const auto& op : topOpcodes)
    {
        out << "  - prefix: 0x" << std::hex << std::setw(4) << std::setfill('0') << op.prefix;
        out << "  opcode: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(op.opcode);
        out << "  mnemonic: \"" << op.mnemonic << "\"";
        out << "  count: " << std::dec << op.count << "\n";
    }

    out << "\nrecent_trace:\n";
    auto trace = GetRecentTrace(100);
    for (size_t i = 0; i < trace.size(); ++i)
    {
        const auto& t = trace[i];
        out << "  - idx: -" << i;
        out << "  pc: 0x" << std::hex << std::setw(4) << std::setfill('0') << t.pc;
        out << "  prefix: 0x" << std::setw(4) << t.prefix;
        out << "  opcode: 0x" << std::setw(2) << static_cast<int>(t.opcode);
        out << "  flags: 0x" << std::setw(2) << static_cast<int>(t.flags);
        out << "  a: 0x" << std::setw(2) << static_cast<int>(t.a);
        out << "  frame: " << std::dec << t.frame;
        out << "  t_state: " << t.tState << "\n";
    }

    return true;
}

/// endregion </Export>

/// region <Helper Methods>

size_t OpcodeProfiler::ToIndex(uint16_t prefix, uint8_t opcode)
{
    return GetPrefixOffset(prefix) + opcode;
}

size_t OpcodeProfiler::GetPrefixOffset(uint16_t prefix)
{
    switch (prefix)
    {
        case PREFIX_NONE: return 0;      // 0-255
        case PREFIX_CB:   return 256;    // 256-511
        case PREFIX_DD:   return 512;    // 512-767
        case PREFIX_ED:   return 768;    // 768-1023
        case PREFIX_FD:   return 1024;   // 1024-1279
        case PREFIX_DDCB: return 1280;   // 1280-1535
        case PREFIX_FDCB: return 1536;   // 1536-1791
        default:          return 0;      // Unknown prefix treated as non-prefixed
    }
}

std::string OpcodeProfiler::GetMnemonic(uint16_t prefix, uint8_t opcode) const
{
    // Return hex representation of prefix + opcode
    // TODO: Integrate with Z80Disassembler for human-readable mnemonics
    (void)_context;  // Suppress unused warning - will be used for disassembler integration
    std::stringstream ss;
    if (prefix != PREFIX_NONE)
    {
        ss << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << prefix << " ";
    }
    ss << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(opcode);
    return ss.str();
}

/// endregion </Helper Methods>
