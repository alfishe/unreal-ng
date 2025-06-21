#include "memoryaccesstracker.h"
#include "calltrace.h"
#include "common/logger.h"
#include "common/stringhelper.h"
#include "debugger/disassembler/z80disasm.h"
#include "emulator/cpu/core.h"
#include "emulator/memory/rom.h"
#include "filesystem"
#include "memory.h"
#include "stdafx.h"

// Constructor
MemoryAccessTracker::MemoryAccessTracker(Memory* memory, EmulatorContext* context) : _memory(memory), _context(context)
{
    // Initialize vectors with appropriate sizes
    _z80ReadCounters.resize(PAGE_SIZE * 4, 0);     // 64KB for Z80 address space
    _z80WriteCounters.resize(PAGE_SIZE * 4, 0);    // 64KB for Z80 address space
    _z80ExecuteCounters.resize(PAGE_SIZE * 4, 0);  // 64KB for Z80 address space

    _physReadCounters.resize(PAGE_SIZE * MAX_PAGES, 0);     // All physical memory
    _physWriteCounters.resize(PAGE_SIZE * MAX_PAGES, 0);    // All physical memory
    _physExecuteCounters.resize(PAGE_SIZE * MAX_PAGES, 0);  // All physical memory

    _pageReadCounters.resize(MAX_PAGES, 0);     // Per-page counters
    _pageWriteCounters.resize(MAX_PAGES, 0);    // Per-page counters
    _pageExecuteCounters.resize(MAX_PAGES, 0);  // Per-page counters

    _pageReadMarks.resize(MAX_PAGES / 8, 0);     // Page access flags
    _pageWriteMarks.resize(MAX_PAGES / 8, 0);    // Page access flags
    _pageExecuteMarks.resize(MAX_PAGES / 8, 0);  // Page access flags

    _callTraceBuffer = std::make_unique<CallTraceBuffer>();
    _disassembler = std::make_unique<Z80Disassembler>(context);
}

// Destructor
MemoryAccessTracker::~MemoryAccessTracker()
{
    // Clean up any resources if needed
}

// Initialize the tracker with the specified mode
void MemoryAccessTracker::Initialize(TrackingMode mode)
{
    _currentMode = mode;
    ResetCounters();
}

// Reset all counters and statistics
void MemoryAccessTracker::ResetCounters()
{
    // Reset Z80 address space counters
    std::fill(_z80ReadCounters.begin(), _z80ReadCounters.end(), 0);
    std::fill(_z80WriteCounters.begin(), _z80WriteCounters.end(), 0);
    std::fill(_z80ExecuteCounters.begin(), _z80ExecuteCounters.end(), 0);

    // Reset physical memory counters
    std::fill(_physReadCounters.begin(), _physReadCounters.end(), 0);
    std::fill(_physWriteCounters.begin(), _physWriteCounters.end(), 0);
    std::fill(_physExecuteCounters.begin(), _physExecuteCounters.end(), 0);

    // Reset page-level counters
    std::fill(_pageReadCounters.begin(), _pageReadCounters.end(), 0);
    std::fill(_pageWriteCounters.begin(), _pageWriteCounters.end(), 0);
    std::fill(_pageExecuteCounters.begin(), _pageExecuteCounters.end(), 0);

    // Reset bank and page marks
    _z80BankReadMarks = 0;
    _z80BankWriteMarks = 0;
    _z80BankExecuteMarks = 0;

    std::fill(_pageReadMarks.begin(), _pageReadMarks.end(), 0);
    std::fill(_pageWriteMarks.begin(), _pageWriteMarks.end(), 0);
    std::fill(_pageExecuteMarks.begin(), _pageExecuteMarks.end(), 0);

    // Reset monitored regions and ports statistics
    for (auto& region : _monitoredRegions)
    {
        region.stats = AccessStats();
    }

    for (auto& port : _monitoredPorts)
    {
        port.stats = AccessStats();
    }

    // Reset segments if segmented tracking is enabled
    if (_segmentTrackingEnabled)
    {
        _segments.clear();
        _currentSegment = nullptr;
    }

    // Reset HALT detection
    _lastExecutedAddress = 0xFFFF;
    _haltExecutionCount = 0;
}

// Set the current tracking mode
void MemoryAccessTracker::SetTrackingMode(TrackingMode mode)
{
    _currentMode = mode;
}

// Get the current tracking mode
TrackingMode MemoryAccessTracker::GetTrackingMode() const
{
    return _currentMode;
}

// Add a monitored memory region with the specified options
bool MemoryAccessTracker::AddMonitoredRegion(const std::string& name, uint16_t startAddress, uint16_t length,
                                             const MonitoringOptions& options)
{
    // Check if a region with this name already exists
    for (const auto& region : _monitoredRegions)
    {
        if (region.name == name)
        {
            return false;  // Region with this name already exists
        }
    }

    // Create a new monitored region
    MonitoredRegion region;
    region.name = name;
    region.startAddress = startAddress;
    region.length = length;
    region.options = options;
    region.mode = _currentMode;

    // Add the region to the list
    _monitoredRegions.push_back(region);

    // Update the address to region map
    UpdateAddressToRegionMap();

    return true;
}

// Add a monitored I/O port with the specified options
bool MemoryAccessTracker::AddMonitoredPort(const std::string& name, uint16_t port, const MonitoringOptions& options)
{
    // Check if a port with this name already exists
    for (const auto& monitoredPort : _monitoredPorts)
    {
        if (monitoredPort.name == name)
        {
            return false;  // Port with this name already exists
        }
    }

    // Create a new monitored port
    MonitoredPort monitoredPort;
    monitoredPort.name = name;
    monitoredPort.port = port;
    monitoredPort.options = options;

    // Add the port to the list
    _monitoredPorts.push_back(monitoredPort);

    // Update the port to index map
    _portToIndexMap[port] = _monitoredPorts.size() - 1;

    return true;
}

// Remove a monitored memory region by name
bool MemoryAccessTracker::RemoveMonitoredRegion(const std::string& name)
{
    for (auto it = _monitoredRegions.begin(); it != _monitoredRegions.end(); ++it)
    {
        if (it->name == name)
        {
            _monitoredRegions.erase(it);
            UpdateAddressToRegionMap();
            return true;
        }
    }

    return false;  // Region not found
}

// Remove a monitored I/O port by name
bool MemoryAccessTracker::RemoveMonitoredPort(const std::string& name)
{
    for (auto it = _monitoredPorts.begin(); it != _monitoredPorts.end(); ++it)
    {
        if (it->name == name)
        {
            uint16_t port = it->port;
            _monitoredPorts.erase(it);
            _portToIndexMap.erase(port);

            // Rebuild the port to index map
            for (size_t i = 0; i < _monitoredPorts.size(); ++i)
            {
                _portToIndexMap[_monitoredPorts[i].port] = i;
            }

            return true;
        }
    }

    return false;  // Port not found
}

// Get statistics for a monitored memory region
const AccessStats* MemoryAccessTracker::GetRegionStats(const std::string& name) const
{
    for (const auto& region : _monitoredRegions)
    {
        if (region.name == name)
        {
            return &region.stats;
        }
    }

    return nullptr;  // Region not found
}

// Get statistics for a monitored I/O port
const AccessStats* MemoryAccessTracker::GetPortStats(const std::string& name) const
{
    for (const auto& port : _monitoredPorts)
    {
        if (port.name == name)
        {
            return &port.stats;
        }
    }

    return nullptr;  // Port not found
}

// Update the address to region map after adding or removing a region
void MemoryAccessTracker::UpdateAddressToRegionMap()
{
    // Clear the current map
    _addressToRegionMap.clear();

    // Rebuild the map
    for (size_t i = 0; i < _monitoredRegions.size(); ++i)
    {
        const auto& region = _monitoredRegions[i];
        for (uint16_t addr = region.startAddress; addr < region.startAddress + region.length; ++addr)
        {
            _addressToRegionMap[addr].push_back(i);
        }
    }
}

// Enable or disable segmented tracking
void MemoryAccessTracker::EnableSegmentTracking(bool enable)
{
    _segmentTrackingEnabled = enable;

    if (!enable)
    {
        // Clear all segments if disabling
        _segments.clear();
        _currentSegment = nullptr;
    }
}

// Start a new tracking segment
void MemoryAccessTracker::StartSegment(const std::string& name, TrackingEvent eventType, uint32_t eventId)
{
    if (!_segmentTrackingEnabled)
    {
        return;  // Segmented tracking is not enabled
    }

    // End the current segment if one exists
    if (_currentSegment != nullptr)
    {
        EndSegment();
    }

    // Create a new segment
    TrackingSegment segment;
    segment.name = name;
    segment.eventType = eventType;
    segment.eventId = eventId;

    // Add the segment to the list
    _segments.push_back(segment);

    // Set the current segment
    _currentSegment = &_segments.back();
}

// End the current tracking segment
void MemoryAccessTracker::EndSegment()
{
    if (!_segmentTrackingEnabled || _currentSegment == nullptr)
    {
        return;  // Segmented tracking is not enabled or no current segment
    }

    // Capture current statistics for all monitored regions and ports
    for (const auto& region : _monitoredRegions)
    {
        _currentSegment->regionStats[region.name] = region.stats;
    }

    for (const auto& port : _monitoredPorts)
    {
        _currentSegment->portStats[port.name] = port.stats;
    }

    // Clear the current segment pointer
    _currentSegment = nullptr;
}

// Get statistics for a specific segment
const TrackingSegment* MemoryAccessTracker::GetSegment(const std::string& name) const
{
    for (const auto& segment : _segments)
    {
        if (segment.name == name)
        {
            return &segment;
        }
    }

    return nullptr;  // Segment not found
}

// Get all tracking segments
const std::vector<TrackingSegment>& MemoryAccessTracker::GetAllSegments() const
{
    return _segments;
}

// Track memory read access
void MemoryAccessTracker::TrackMemoryRead(uint16_t address, uint8_t value, uint16_t callerAddress)
{
    // Update Z80 address space counters if we're in Z80 mode or both modes
    if (_currentMode == TrackingMode::Z80AddressSpace)
    {
        // Increment the counter for this address, capping at max value
        if (_z80ReadCounters[address] < UINT32_MAX)
        {
            _z80ReadCounters[address]++;
        }

        // Set the bank mark
        uint8_t bank = address >> 14;  // Divide by 16384 (PAGE_SIZE) to get bank number (0-3)
        _z80BankReadMarks |= (1 << bank);
    }

    // Update physical memory counters
    size_t physOffset = GetPhysicalOffsetForZ80Address(address);
    if (physOffset < _physReadCounters.size())
    {
        // Increment the counter for this physical address, capping at max value
        if (_physReadCounters[physOffset] < UINT32_MAX)
        {
            _physReadCounters[physOffset]++;
        }

        // Update page-level counters and marks
        uint16_t page = GetPageForPhysicalOffset(physOffset);
        if (page < MAX_PAGES)
        {
            if (_pageReadCounters[page] < UINT32_MAX)
            {
                _pageReadCounters[page]++;
            }

            // Set the page mark
            _pageReadMarks[page / 8] |= (1 << (page % 8));
        }
    }

    // Update region statistics
    UpdateRegionStats(address, value, callerAddress, AccessType::Read);
}

// Track memory write access
void MemoryAccessTracker::TrackMemoryWrite(uint16_t address, uint8_t value, uint16_t callerAddress)
{
    // Update Z80 address space counters if we're in Z80 mode or both modes
    if (_currentMode == TrackingMode::Z80AddressSpace)
    {
        // Increment the counter for this address, capping at max value
        if (_z80WriteCounters[address] < UINT32_MAX)
        {
            _z80WriteCounters[address]++;
        }

        // Set the bank mark
        uint8_t bank = address >> 14;  // Divide by 16384 (PAGE_SIZE) to get bank number (0-3)
        _z80BankWriteMarks |= (1 << bank);
    }

    // Update physical memory counters
    size_t physOffset = GetPhysicalOffsetForZ80Address(address);
    if (physOffset < _physWriteCounters.size())
    {
        // Increment the counter for this physical address, capping at max value
        if (_physWriteCounters[physOffset] < UINT32_MAX)
        {
            _physWriteCounters[physOffset]++;
        }

        // Update page-level counters and marks
        uint16_t page = GetPageForPhysicalOffset(physOffset);
        if (page < MAX_PAGES)
        {
            if (_pageWriteCounters[page] < UINT32_MAX)
            {
                _pageWriteCounters[page]++;
            }

            // Set the page mark
            _pageWriteMarks[page / 8] |= (1 << (page % 8));
        }
    }

    // Update region statistics
    UpdateRegionStats(address, value, callerAddress, AccessType::Write);
}

// Track memory execute access
void MemoryAccessTracker::TrackMemoryExecute(uint16_t address, uint16_t callerAddress)
{
    // HALT detection: explicitly check for 0x76 HALT opcode
    // If this detection block is disabled - HALT will rapidly increment execution count
    // for this address due to constantly doing pc-- after it's M1 cycle
    if (_memory != nullptr)
    {
        uint8_t opcode = _memory->DirectReadFromZ80Memory(address);
        if (opcode == 0x76)  // HALT instruction
        {
            if (address == _lastExecutedAddress)
            {
                _haltExecutionCount++;
                if (_haltExecutionCount > MAX_HALT_EXECUTIONS)
                {
                    // Skip tracking this execution - it's a repeated HALT
                    return;
                }
            }
            else
            {
                // New HALT instruction, reset counter
                _lastExecutedAddress = address;
                _haltExecutionCount = 1;
            }
        }
        else
        {
            // Not a HALT instruction, reset detection
            _lastExecutedAddress = address;
            _haltExecutionCount = 0;
        }
    }

    // Update Z80 address space counters if we're in Z80 mode or both modes
    if (_currentMode == TrackingMode::Z80AddressSpace)
    {
        // Increment the counter for this address, capping at max value
        if (_z80ExecuteCounters[address] < UINT32_MAX)
        {
            _z80ExecuteCounters[address]++;
        }

        // Set the bank mark
        uint8_t bank = address >> 14;  // Divide by 16384 (PAGE_SIZE) to get bank number (0-3)
        _z80BankExecuteMarks |= (1 << bank);
    }

    // Update physical memory counters
    size_t physOffset = GetPhysicalOffsetForZ80Address(address);
    if (physOffset < _physExecuteCounters.size())
    {
        // Increment the counter for this physical address, capping at max value
        if (_physExecuteCounters[physOffset] < UINT32_MAX)
        {
            _physExecuteCounters[physOffset]++;
        }

        // Update page-level counters and marks
        uint16_t page = GetPageForPhysicalOffset(physOffset);
        if (page < MAX_PAGES)
        {
            if (_pageExecuteCounters[page] < UINT32_MAX)
            {
                _pageExecuteCounters[page]++;
            }

            // Set the page mark
            _pageExecuteMarks[page / 8] |= (1 << (page % 8));
        }
    }

    // Update region statistics (no value for execute)
    UpdateRegionStats(address, 0, callerAddress, AccessType::Execute);

    // --- Call trace integration ---
    if (_callTraceBuffer)
    {
        _callTraceBuffer->LogIfControlFlow(_context, _memory, address, _context->emulatorState.frame_counter);
    }
    // --- End call trace integration ---
}

// Track port read access
void MemoryAccessTracker::TrackPortRead(uint16_t port, uint8_t value, uint16_t callerAddress)
{
    // Check if this port is monitored
    auto it = _portToIndexMap.find(port);
    if (it != _portToIndexMap.end())
    {
        size_t index = it->second;
        MonitoredPort& monitoredPort = _monitoredPorts[index];

        // Update read count, capping at max value
        if (monitoredPort.stats.readCount < UINT32_MAX)
        {
            monitoredPort.stats.readCount++;
        }

        // Track caller addresses if enabled
        if (monitoredPort.options.trackCallers)
        {
            AddToCallerMap(monitoredPort.stats.callerAddresses, callerAddress, monitoredPort.options.maxCallers);
        }

        // Track data values if enabled
        if (monitoredPort.options.trackDataFlow)
        {
            AddToDataFlowMap(monitoredPort.stats.dataValues, value, monitoredPort.options.maxDataValues);
        }

        // Update segment statistics if segment tracking is enabled
        if (_segmentTrackingEnabled && _currentSegment != nullptr)
        {
            UpdateSegmentStats(monitoredPort.name, monitoredPort.stats, true);
        }
    }
}

// Track port write access
void MemoryAccessTracker::TrackPortWrite(uint16_t port, uint8_t value, uint16_t callerAddress)
{
    // Check if this port is monitored
    auto it = _portToIndexMap.find(port);
    if (it != _portToIndexMap.end())
    {
        size_t index = it->second;
        MonitoredPort& monitoredPort = _monitoredPorts[index];

        // Update write count, capping at max value
        if (monitoredPort.stats.writeCount < UINT32_MAX)
        {
            monitoredPort.stats.writeCount++;
        }

        // Track caller addresses if enabled
        if (monitoredPort.options.trackCallers)
        {
            AddToCallerMap(monitoredPort.stats.callerAddresses, callerAddress, monitoredPort.options.maxCallers);
        }

        // Track data values if enabled
        if (monitoredPort.options.trackDataFlow)
        {
            AddToDataFlowMap(monitoredPort.stats.dataValues, value, monitoredPort.options.maxDataValues);
        }

        // Update segment statistics if segment tracking is enabled
        if (_segmentTrackingEnabled && _currentSegment != nullptr)
        {
            UpdateSegmentStats(monitoredPort.name, monitoredPort.stats, true);
        }
    }
}

// Get the physical memory offset for a Z80 address
size_t MemoryAccessTracker::GetPhysicalOffsetForZ80Address(uint16_t address) const
{
    // Delegate to the Memory class to get the physical offset
    return _memory->GetPhysicalOffsetForZ80Address(address);
}

// Get the page number for a physical memory offset
uint16_t MemoryAccessTracker::GetPageForPhysicalOffset(size_t offset) const
{
    return static_cast<uint16_t>(offset / PAGE_SIZE);
}

// Update region statistics for a memory access
void MemoryAccessTracker::UpdateRegionStats(uint16_t address, uint8_t value, uint16_t callerAddress,
                                            AccessType accessType)
{
    // Check if this address is part of any monitored regions
    auto it = _addressToRegionMap.find(address);
    if (it != _addressToRegionMap.end())
    {
        // Update statistics for all regions that contain this address
        for (size_t index : it->second)
        {
            MonitoredRegion& region = _monitoredRegions[index];

            // Only update if the region's mode matches the current mode
            if (region.mode == _currentMode)
            {
                // Update counters based on access type
                if ((static_cast<uint8_t>(accessType) & static_cast<uint8_t>(AccessType::Read)) != 0)
                {
                    if (region.stats.readCount < UINT32_MAX)
                    {
                        region.stats.readCount++;
                    }
                }

                if ((static_cast<uint8_t>(accessType) & static_cast<uint8_t>(AccessType::Write)) != 0)
                {
                    if (region.stats.writeCount < UINT32_MAX)
                    {
                        region.stats.writeCount++;
                    }
                }

                if ((static_cast<uint8_t>(accessType) & static_cast<uint8_t>(AccessType::Execute)) != 0)
                {
                    if (region.stats.executeCount < UINT32_MAX)
                    {
                        region.stats.executeCount++;
                    }
                }

                // Track caller addresses if enabled
                if (region.options.trackCallers)
                {
                    AddToCallerMap(region.stats.callerAddresses, callerAddress, region.options.maxCallers);
                }

                // Track data values if enabled and this is a read or write access
                if (region.options.trackDataFlow &&
                    ((static_cast<uint8_t>(accessType) &
                      (static_cast<uint8_t>(AccessType::Read) | static_cast<uint8_t>(AccessType::Write))) != 0))
                {
                    AddToDataFlowMap(region.stats.dataValues, value, region.options.maxDataValues);
                }

                // Update segment statistics if segment tracking is enabled
                if (_segmentTrackingEnabled && _currentSegment != nullptr)
                {
                    UpdateSegmentStats(region.name, region.stats, false);
                }
            }
        }
    }
}

// Update segment statistics if segment tracking is enabled
void MemoryAccessTracker::UpdateSegmentStats(const std::string& name, const AccessStats& stats, bool isPort)
{
    if (!_segmentTrackingEnabled || _currentSegment == nullptr)
    {
        return;
    }

    // Update the appropriate map in the current segment
    if (isPort)
    {
        _currentSegment->portStats[name] = stats;
    }
    else
    {
        _currentSegment->regionStats[name] = stats;
    }
}

// Add a value to the data flow tracking map with limit enforcement
void MemoryAccessTracker::AddToDataFlowMap(std::unordered_map<uint8_t, uint32_t>& map, uint8_t value,
                                           uint32_t maxEntries)
{
    // If the value is already in the map, increment its count
    auto it = map.find(value);
    if (it != map.end())
    {
        if (it->second < UINT32_MAX)
        {
            it->second++;
        }
    }
    else if (map.size() < maxEntries)
    {
        // Add the value to the map if we haven't reached the limit
        map[value] = 1;
    }
    else
    {
        // Find the least frequently used value and replace it
        auto leastFrequent =
            std::min_element(map.begin(), map.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

        if (leastFrequent != map.end() && leastFrequent->second <= 1)
        {
            map.erase(leastFrequent);
            map[value] = 1;
        }
    }
}

// Add a caller to the caller tracking map with limit enforcement
void MemoryAccessTracker::AddToCallerMap(std::unordered_map<uint16_t, uint32_t>& map, uint16_t callerAddress,
                                         uint32_t maxEntries)
{
    // If the caller is already in the map, increment its count
    auto it = map.find(callerAddress);
    if (it != map.end())
    {
        if (it->second < UINT32_MAX)
        {
            it->second++;
        }
    }
    else if (map.size() < maxEntries)
    {
        // Add the caller to the map if we haven't reached the limit
        map[callerAddress] = 1;
    }
    else
    {
        // Find the least frequently used caller and replace it
        auto leastFrequent =
            std::min_element(map.begin(), map.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

        if (leastFrequent != map.end() && leastFrequent->second <= 1)
        {
            map.erase(leastFrequent);
            map[callerAddress] = 1;
        }
    }
}

// Get total access count for a Z80 bank
uint32_t MemoryAccessTracker::GetZ80BankTotalAccessCount(uint8_t bank) const
{
    uint32_t result = 0;
    uint8_t bankBit = 1 << bank;
    bool bankWasRead = _z80BankReadMarks & bankBit;
    bool bankWasWritten = _z80BankWriteMarks & bankBit;
    bool bankWasExecuted = _z80BankExecuteMarks & bankBit;

    if (bankWasRead || bankWasWritten || bankWasExecuted)
    {
        size_t startAddr = bank * PAGE_SIZE;
        size_t endAddr = startAddr + PAGE_SIZE;

        for (size_t addr = startAddr; addr < endAddr; addr++)
        {
            if (bankWasRead)
                result += _z80ReadCounters[addr];
            if (bankWasWritten)
                result += _z80WriteCounters[addr];
            if (bankWasExecuted)
                result += _z80ExecuteCounters[addr];
        }
    }

    return result;
}

// Get read access count for a Z80 bank
uint32_t MemoryAccessTracker::GetZ80BankReadAccessCount(uint8_t bank) const
{
    uint32_t result = 0;
    uint8_t bankBit = 1 << bank;

    if (_z80BankReadMarks & bankBit)
    {
        size_t startAddr = bank * PAGE_SIZE;
        size_t endAddr = startAddr + PAGE_SIZE;

        for (size_t addr = startAddr; addr < endAddr; addr++)
        {
            result += _z80ReadCounters[addr];
        }
    }

    return result;
}

// Get write access count for a Z80 bank
uint32_t MemoryAccessTracker::GetZ80BankWriteAccessCount(uint8_t bank) const
{
    uint32_t result = 0;
    uint8_t bankBit = 1 << bank;

    if (_z80BankWriteMarks & bankBit)
    {
        size_t startAddr = bank * PAGE_SIZE;
        size_t endAddr = startAddr + PAGE_SIZE;

        for (size_t addr = startAddr; addr < endAddr; addr++)
        {
            result += _z80WriteCounters[addr];
        }
    }

    return result;
}

// Get execute access count for a Z80 bank
uint32_t MemoryAccessTracker::GetZ80BankExecuteAccessCount(uint8_t bank) const
{
    uint32_t result = 0;
    uint8_t bankBit = 1 << bank;

    if (_z80BankExecuteMarks & bankBit)
    {
        size_t startAddr = bank * PAGE_SIZE;
        size_t endAddr = startAddr + PAGE_SIZE;

        for (size_t addr = startAddr; addr < endAddr; addr++)
        {
            result += _z80ExecuteCounters[addr];
        }
    }

    return result;
}

// Get total access count for a physical memory page
uint32_t MemoryAccessTracker::GetPageTotalAccessCount(uint16_t page) const
{
    if (page >= MAX_PAGES)
    {
        return 0;
    }

    return _pageReadCounters[page] + _pageWriteCounters[page] + _pageExecuteCounters[page];
}

// Get read access count for a physical memory page
uint32_t MemoryAccessTracker::GetPageReadAccessCount(uint16_t page) const
{
    if (page >= MAX_PAGES)
    {
        return 0;
    }

    return _pageReadCounters[page];
}

// Get write access count for a physical memory page
uint32_t MemoryAccessTracker::GetPageWriteAccessCount(uint16_t page) const
{
    if (page >= MAX_PAGES)
    {
        return 0;
    }

    return _pageWriteCounters[page];
}

// Get execute access count for a physical memory page
uint32_t MemoryAccessTracker::GetPageExecuteAccessCount(uint16_t page) const
{
    if (page >= MAX_PAGES)
    {
        return 0;
    }

    return _pageExecuteCounters[page];
}

// Generate a report of all monitored regions and their statistics
std::string MemoryAccessTracker::GenerateRegionReport() const
{
    std::stringstream ss;
    ss << "Memory Region Access Report\n";
    ss << "==========================\n\n";
    ss << "Current Mode: " << (_currentMode == TrackingMode::Z80AddressSpace ? "Z80 Address Space" : "Physical Memory")
       << "\n\n";

    ss << "Monitored Regions: " << _monitoredRegions.size() << "\n";
    ss << "------------------\n";

    for (const auto& region : _monitoredRegions)
    {
        if (region.mode == _currentMode)
        {
            ss << "Region: " << region.name << "\n";
            ss << "  Address Range: 0x" << std::hex << region.startAddress << " - 0x"
               << (region.startAddress + region.length - 1) << std::dec << " (" << region.length << " bytes)\n";
            ss << "  Read Count: " << region.stats.readCount << "\n";
            ss << "  Write Count: " << region.stats.writeCount << "\n";
            ss << "  Execute Count: " << region.stats.executeCount << "\n";
            ss << "  Total Accesses: " << (region.stats.readCount + region.stats.writeCount + region.stats.executeCount)
               << "\n";

            if (region.options.trackCallers && !region.stats.callerAddresses.empty())
            {
                ss << "  Top Callers:\n";

                // Sort callers by access count
                std::vector<std::pair<uint16_t, uint32_t>> sortedCallers(region.stats.callerAddresses.begin(),
                                                                         region.stats.callerAddresses.end());
                std::sort(sortedCallers.begin(), sortedCallers.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });

                // Show top 10 callers or all if less than 10
                size_t count = std::min(sortedCallers.size(), static_cast<size_t>(10));
                for (size_t i = 0; i < count; i++)
                {
                    ss << "    0x" << std::hex << sortedCallers[i].first << std::dec << ": " << sortedCallers[i].second
                       << " accesses\n";
                }
            }

            if (region.options.trackDataFlow && !region.stats.dataValues.empty())
            {
                ss << "  Top Data Values:\n";

                // Sort data values by frequency
                std::vector<std::pair<uint8_t, uint32_t>> sortedValues(region.stats.dataValues.begin(),
                                                                       region.stats.dataValues.end());
                std::sort(sortedValues.begin(), sortedValues.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });

                // Show top 10 values or all if less than 10
                size_t count = std::min(sortedValues.size(), static_cast<size_t>(10));
                for (size_t i = 0; i < count; i++)
                {
                    ss << "    0x" << std::hex << static_cast<int>(sortedValues[i].first) << std::dec << ": "
                       << sortedValues[i].second << " occurrences\n";
                }
            }

            ss << "\n";
        }
    }

    return ss.str();
}

// Generate a report of all monitored ports and their statistics
std::string MemoryAccessTracker::GeneratePortReport() const
{
    std::stringstream ss;
    ss << "I/O Port Access Report\n";
    ss << "=====================\n\n";

    ss << "Monitored Ports: " << _monitoredPorts.size() << "\n";
    ss << "---------------\n";

    for (const auto& port : _monitoredPorts)
    {
        ss << "Port: " << port.name << "\n";
        ss << "  Port Number: 0x" << std::hex << port.port << std::dec << "\n";
        ss << "  Read Count: " << port.stats.readCount << "\n";
        ss << "  Write Count: " << port.stats.writeCount << "\n";
        ss << "  Total Accesses: " << (port.stats.readCount + port.stats.writeCount) << "\n";

        if (port.options.trackCallers && !port.stats.callerAddresses.empty())
        {
            ss << "  Top Callers:\n";

            // Sort callers by access count
            std::vector<std::pair<uint16_t, uint32_t>> sortedCallers(port.stats.callerAddresses.begin(),
                                                                     port.stats.callerAddresses.end());
            std::sort(sortedCallers.begin(), sortedCallers.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });

            // Show top 10 callers or all if less than 10
            size_t count = std::min(sortedCallers.size(), static_cast<size_t>(10));
            for (size_t i = 0; i < count; i++)
            {
                ss << "    0x" << std::hex << sortedCallers[i].first << std::dec << ": " << sortedCallers[i].second
                   << " accesses\n";
            }
        }

        if (port.options.trackDataFlow && !port.stats.dataValues.empty())
        {
            ss << "  Top Data Values:\n";

            // Sort data values by frequency
            std::vector<std::pair<uint8_t, uint32_t>> sortedValues(port.stats.dataValues.begin(),
                                                                   port.stats.dataValues.end());
            std::sort(sortedValues.begin(), sortedValues.end(),
                      [](const auto& a, const auto& b) { return a.second > b.second; });

            // Show top 10 values or all if less than 10
            size_t count = std::min(sortedValues.size(), static_cast<size_t>(10));
            for (size_t i = 0; i < count; i++)
            {
                ss << "    0x" << std::hex << static_cast<int>(sortedValues[i].first) << std::dec << ": "
                   << sortedValues[i].second << " occurrences\n";
            }
        }

        ss << "\n";
    }

    return ss.str();
}

// Check if there's any activity (read/write/execute) in the specified address range
bool MemoryAccessTracker::HasActivity(uint16_t start, uint16_t end) const
{
    // Ensure start <= end
    if (start > end)
    {
        std::swap(start, end);
    }

    // Check Z80 address space counters first (faster check for most cases)
    for (uint32_t addr = start; addr <= end; addr++)
    {
        if (_z80ReadCounters[addr] > 0 || _z80WriteCounters[addr] > 0 || _z80ExecuteCounters[addr] > 0)
        {
            return true;
        }
    }

    // If we're only tracking Z80 address space, we can return now
    if (_currentMode == TrackingMode::Z80AddressSpace)
    {
        return false;
    }

    // If we're tracking physical memory, check physical memory counters too
    for (uint32_t addr = start; addr <= end; addr++)
    {
        size_t physOffset = GetPhysicalOffsetForZ80Address(addr);
        if (physOffset < _physReadCounters.size() &&
            (_physReadCounters[physOffset] > 0 || _physWriteCounters[physOffset] > 0 ||
             _physExecuteCounters[physOffset] > 0))
        {
            return true;
        }
    }

    return false;
}

// Generate a report of all segments and their statistics
std::string MemoryAccessTracker::GenerateSegmentReport() const
{
    std::stringstream ss;
    ss << "Segment Tracking Report\n";
    ss << "=====================\n\n";

    if (!_segmentTrackingEnabled)
    {
        ss << "Segment tracking is disabled.\n";
        return ss.str();
    }

    ss << "Segments: " << _segments.size() << "\n";
    ss << "---------\n";

    for (const auto& segment : _segments)
    {
        ss << "Segment: " << segment.name << "\n";
        ss << "  Event Type: ";
        switch (segment.eventType)
        {
            case TrackingEvent::Frame:
                ss << "Frame";
                break;
            case TrackingEvent::Interrupt:
                ss << "Interrupt";
                break;
            case TrackingEvent::Custom:
                ss << "Custom";
                break;
        }
        ss << "\n";
        ss << "  Event ID: " << segment.eventId << "\n";

        if (!segment.regionStats.empty())
        {
            ss << "  Regions:\n";
            for (const auto& pair : segment.regionStats)
            {
                ss << "    " << pair.first << ":\n";
                ss << "      Read Count: " << pair.second.readCount << "\n";
                ss << "      Write Count: " << pair.second.writeCount << "\n";
                ss << "      Execute Count: " << pair.second.executeCount << "\n";
                ss << "      Total Accesses: "
                   << (pair.second.readCount + pair.second.writeCount + pair.second.executeCount) << "\n";
            }
        }

        if (!segment.portStats.empty())
        {
            ss << "  Ports:\n";
            for (const auto& pair : segment.portStats)
            {
                ss << "    " << pair.first << ":\n";
                ss << "      Read Count: " << pair.second.readCount << "\n";
                ss << "      Write Count: " << pair.second.writeCount << "\n";
                ss << "      Total Accesses: " << (pair.second.readCount + pair.second.writeCount) << "\n";
            }
        }

        ss << "\n";
    }

    return ss.str();
}

std::string MemoryAccessTracker::SaveAccessData(const std::string& outputPath, const std::string& format, bool singleFile,
                                         const std::vector<std::string>& filterPages)
{
    namespace fs = std::filesystem;

    try
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
        std::string timestamp = ss.str();

        if (format != "yaml")
        {
            LOGERROR("Only YAML format is currently supported");
            return "";
        }

        if (singleFile)
        {
            // Single file output - use outputPath directly
            fs::path filePath = outputPath;

            // If no specific file path provided, generate unique filename
            if (filePath.empty())
            {
                filePath = "memory_access_" + timestamp + ".yaml";
            }

            std::ofstream out(filePath);
            if (!out)
            {
                LOGERROR("Failed to create output file: %s", filePath.string().c_str());
                return "";
            }

            SaveMemoryLayout(out);
            out << std::endl;
            SavePageSummaries(out, filterPages);
            out << std::endl;
            SaveDetailedAccessData(out, filterPages);

            LOGDEBUG("Saved memory access data to single file: %s", filePath.string().c_str());
            return filePath.string();
        }
        else
        {
            // Multiple files in a directory
            fs::path baseDir = outputPath;
            if (!fs::is_directory(baseDir))
            {
                baseDir = fs::path(outputPath).parent_path();
                if (baseDir.empty())
                {
                    baseDir = ".";
                }
            }

            fs::path sessionDir = baseDir / ("memory_access_" + timestamp);
            fs::create_directories(sessionDir);

            // Save memory layout
            {
                std::ofstream out(sessionDir / "memory_layout.yaml");
                if (!out)
                {
                    LOGERROR("Failed to create memory layout file");
                    return "";
                }
                SaveMemoryLayout(out);
            }

            // Save page summaries
            {
                std::ofstream out(sessionDir / "page_summary.yaml");
                if (!out)
                {
                    LOGERROR("Failed to create page summary file");
                    return "";
                }
                SavePageSummaries(out, filterPages);
            }

            // Save detailed access data
            fs::path accessDir = sessionDir / "access";
            fs::create_directories(accessDir);
            if (!SaveDetailedAccessData(accessDir, filterPages))
            {
                return "";
            }

            LOGDEBUG("Saved memory access data to directory: %s", sessionDir.string().c_str());
            return sessionDir.string();
        }
    }
    catch (const std::exception& e)
    {
        LOGERROR("Failed to save memory access data: %s", e.what());
        return "";
    }
}

void MemoryAccessTracker::SaveMemoryLayout(std::ostream& out)
{
    out << "memory_layout:" << std::endl;
    for (int bank = 0; bank < 4; bank++)
    {
        std::string pageName = GetBankPageName(bank);
        out << StringHelper::Format("  bank_%d: \"%s\"  # 0x%04X-0x%04X", bank, pageName.c_str(), bank * 0x4000,
                                    (bank + 1) * 0x4000 - 1)
            << std::endl;
    }
}

void MemoryAccessTracker::SavePageSummaries(std::ostream& out, const std::vector<std::string>& filterPages)
{
    out << "page_summaries:" << std::endl;

    // Save RAM pages
    for (uint16_t page = 0; page < MAX_RAM_PAGES; page++)
    {
        std::string pageName = "RAM " + std::to_string(page);
        if (!filterPages.empty() && std::find(filterPages.begin(), filterPages.end(), pageName) == filterPages.end())
            continue;
        if (GetPageTotalAccessCount(page) > 0)
        {
            out << "  \"" << pageName << "\":" << std::endl;
            out << "    reads: " << GetPageReadAccessCount(page) << std::endl;
            out << "    writes: " << GetPageWriteAccessCount(page) << std::endl;
            out << "    executes: " << GetPageExecuteAccessCount(page) << std::endl;
        }
    }
    // Save ROM pages (start after RAM, cache, and misc pages)
    const uint16_t FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;
    ROM* rom = (_context && _context->pCore) ? _context->pCore->GetROM() : nullptr;
    uint8_t* romBase = _memory ? _memory->ROMBase() : nullptr;
    for (uint16_t page = 0; page < MAX_ROM_PAGES; page++)
    {
        std::string pageName = "ROM " + std::to_string(page);
        if (!filterPages.empty() && std::find(filterPages.begin(), filterPages.end(), pageName) == filterPages.end())
            continue;
        uint16_t physicalPage = FIRST_ROM_PAGE + page;
        if (GetPageTotalAccessCount(physicalPage) > 0)
        {
            std::string hash, title;
            if (rom && romBase)
            {
                hash = rom->CalculateSignature(romBase + page * PAGE_SIZE, PAGE_SIZE);
                title = rom->GetROMTitle(hash);
            }
            out << "  \"" << pageName << "\": # " << (title.empty() ? "Unknown ROM" : title)
                << " - hash: " << (hash.empty() ? "?" : hash) << std::endl;
            out << "    reads: " << GetPageReadAccessCount(physicalPage) << std::endl;
            out << "    writes: 0" << std::endl;
            out << "    executes: " << GetPageExecuteAccessCount(physicalPage) << std::endl;
        }
    }
}

bool MemoryAccessTracker::SaveDetailedAccessData(const std::filesystem::path& dirPath,
                                                 const std::vector<std::string>& filterPages)
{
    namespace fs = std::filesystem;

    // Save RAM pages
    for (uint16_t page = 0; page < MAX_RAM_PAGES; page++)
    {
        std::string pageName = "RAM " + std::to_string(page);
        if (!filterPages.empty() && std::find(filterPages.begin(), filterPages.end(), pageName) == filterPages.end())
        {
            continue;
        }

        // Skip pages with no activity
        if (GetPageTotalAccessCount(page) == 0)
        {
            continue;
        }

        fs::path filePath = dirPath / ("RAM_" + std::to_string(page) + ".yaml");
        std::ofstream out(filePath);
        if (!out)
        {
            LOGERROR("Failed to create access data file: %s", filePath.string().c_str());
            return false;
        }

        out << "page: \"" << pageName << "\"" << std::endl;
        out << "accessed_addresses:" << std::endl;

        SaveSinglePageAccessData(out, page, "  ");
    }

    // Save ROM pages (start after RAM, cache, and misc pages)
    const uint16_t FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;
    for (uint16_t page = 0; page < MAX_ROM_PAGES; page++)
    {
        std::string pageName = "ROM " + std::to_string(page);
        if (!filterPages.empty() && std::find(filterPages.begin(), filterPages.end(), pageName) == filterPages.end())
        {
            continue;
        }

        uint16_t physicalPage = FIRST_ROM_PAGE + page;

        // Skip pages with no activity
        if (GetPageTotalAccessCount(physicalPage) == 0)
        {
            continue;
        }

        fs::path filePath = dirPath / ("ROM_" + std::to_string(page) + ".yaml");
        std::ofstream out(filePath);
        if (!out)
        {
            LOGERROR("Failed to create access data file: %s", filePath.string().c_str());
            return false;
        }

        out << "page: \"" << pageName << "\"" << std::endl;
        out << "accessed_addresses:" << std::endl;

        SaveSinglePageAccessData(out, physicalPage, "  ");
    }

    return true;
}

void MemoryAccessTracker::SaveDetailedAccessData(std::ostream& out, const std::vector<std::string>& filterPages)
{
    out << "detailed_access:" << std::endl;

    // Save RAM pages
    for (uint16_t page = 0; page < MAX_RAM_PAGES; page++)
    {
        std::string pageName = "RAM " + std::to_string(page);
        if (!filterPages.empty() && std::find(filterPages.begin(), filterPages.end(), pageName) == filterPages.end())
        {
            continue;
        }
        if (GetPageTotalAccessCount(page) > 0)
        {
            out << "  \"" << pageName << "\":" << std::endl;
            out << "    accessed_addresses:" << std::endl;
            SaveSinglePageAccessData(out, page, "      ");
        }
    }

    // Save ROM pages (start after RAM, cache, and misc pages)
    const uint16_t FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;
    ROM* rom = (_context && _context->pCore) ? _context->pCore->GetROM() : nullptr;
    uint8_t* romBase = _memory ? _memory->ROMBase() : nullptr;
    for (uint16_t page = 0; page < MAX_ROM_PAGES; page++)
    {
        std::string pageName = "ROM " + std::to_string(page);
        if (!filterPages.empty() && std::find(filterPages.begin(), filterPages.end(), pageName) == filterPages.end())
        {
            continue;
        }

        uint16_t physicalPage = FIRST_ROM_PAGE + page;
        if (GetPageTotalAccessCount(physicalPage) > 0)
        {
            std::string hash;
            std::string title;

            if (rom && romBase)
            {
                hash = rom->CalculateSignature(romBase + page * PAGE_SIZE, PAGE_SIZE);
                title = rom->GetROMTitle(hash);
            }

            out << "  \"" << pageName << "\": # " << (title.empty() ? "Unknown ROM" : title)
                << " - hash: " << (hash.empty() ? "?" : hash) << std::endl;
            out << "    accessed_addresses:" << std::endl;
            SaveSinglePageAccessData(out, physicalPage, "      ");
        }
    }
}

void MemoryAccessTracker::SaveSinglePageAccessData(std::ostream& out, uint16_t page, const std::string& indent)
{
    for (uint32_t offset = 0; offset < PAGE_SIZE; offset++)
    {
        uint32_t addr = (page * PAGE_SIZE) + offset;
        uint32_t reads = _physReadCounters[addr];
        uint32_t writes = _physWriteCounters[addr];
        uint32_t executes = _physExecuteCounters[addr];

        if (reads > 0 || writes > 0 || executes > 0)
        {
            out << StringHelper::Format("%s0x%04X: {reads: %d, writes: %d, executes: %d}", indent.c_str(), offset,
                                        reads, writes, executes)
                << std::endl;
        }
    }
}

std::string MemoryAccessTracker::GetBankPageName(uint8_t bank) const
{
    if (bank >= 4)
    {
        return "INVALID";
    }

    // Get the memory instance to query bank mappings
    if (!_memory)
    {
        return "UNKNOWN";
    }

    // Get the physical page this bank is mapped to
    uint16_t page = _memory->GetPageForBank(bank);
    if (page == MEMORY_UNMAPPABLE)
    {
        return "UNMAPPED";
    }

    // Check if it's ROM or RAM
    if (bank == 0 ? _memory->IsBank0ROM() : (_memory->GetMemoryBankMode(bank) == BANK_ROM))
    {
        // ROM pages start after RAM, cache, and misc pages
        const uint16_t FIRST_ROM_PAGE = MAX_RAM_PAGES + MAX_CACHE_PAGES + MAX_MISC_PAGES;
        return "ROM " + std::to_string(page - FIRST_ROM_PAGE);
    }
    else
    {
        return "RAM " + std::to_string(page);
    }
}

CallTraceBuffer* MemoryAccessTracker::GetCallTraceBuffer()
{
    return _callTraceBuffer.get();
}

void MemoryAccessTracker::LogControlFlowEvent(const Z80ControlFlowEvent& event)
{
    if (_callTraceBuffer)
    {
        _callTraceBuffer->LogEvent(event, _context->emulatorState.frame_counter);
    }
}

// Reset HALT detection when PC changes (called from Z80 when PC changes)
void MemoryAccessTracker::ResetHaltDetection(uint16_t newPC)
{
    if (newPC != _lastExecutedAddress)
    {
        _lastExecutedAddress = newPC;
        _haltExecutionCount = 0;
    }
}
