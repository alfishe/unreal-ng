#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

/// TR-DOS event types
enum class TRDOSEventType : uint8_t
{
    // System events
    TRDOS_ENTRY,        // Entered TR-DOS ROM
    TRDOS_EXIT,         // Exited TR-DOS ROM
    
    // Command events
    COMMAND_START,      // TR-DOS command started
    COMMAND_COMPLETE,   // TR-DOS command completed
    
    // File operations
    FILE_FOUND,         // File located in catalog
    FILE_NOT_FOUND,     // File not found
    MODULE_LOAD,        // Module (BASIC/CODE/DATA) loaded
    MODULE_SAVE,        // Module saved
    
    // FDC operations
    FDC_CMD_RESTORE,    // Restore (seek track 0)
    FDC_CMD_SEEK,       // Seek to track
    FDC_CMD_STEP,       // Step in/out
    FDC_CMD_READ,       // Read sector
    FDC_CMD_WRITE,      // Write sector
    FDC_CMD_READ_ADDR,  // Read address
    FDC_CMD_READ_TRACK, // Read track
    FDC_CMD_WRITE_TRACK,// Write track (format)
    
    // Data transfer
    SECTOR_TRANSFER,    // Sector data transfer complete
    
    // Errors
    ERROR_CRC,          // CRC error
    ERROR_RNF,          // Record not found
    ERROR_LOST_DATA,    // Lost data
    ERROR_WRITE_PROTECT,// Write protected
    
    // Special
    LOADER_DETECTED,    // Custom loader detected
    PROTECTION_DETECTED,// Copy protection detected
};

// ==================== Layer 1: Raw Events ====================
// Fast capture with full Z80 context for offline analysis

/// Raw FDC port access event (captured on every port I/O)
struct RawFDCEvent
{
    // Timing
    uint64_t tstate;
    uint32_t frameNumber;
    uint64_t timestamp;  // Duplicate of tstate for RingBuffer compatibility
    
    // FDC Port Access
    uint8_t port;              // 0x1F, 0x3F, 0x5F, 0x7F, 0xFF
    uint8_t direction;         // 0=READ(IN), 1=WRITE(OUT)
    uint8_t value;             // Value read/written
    
    // FDC Register Snapshot
    uint8_t commandReg;
    uint8_t statusReg;
    uint8_t trackReg;
    uint8_t sectorReg;
    uint8_t dataReg;
    uint8_t systemReg;         // Port 0xFF (drive/side/density)
    
    // Z80 Context
    uint16_t pc;
    uint16_t sp;
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t iff1, iff2, im;
    
    // Stack snapshot (8 return addresses for call chain reconstruction)
    // See TDD Stack Trace Validation section for address validation rules
    std::array<uint8_t, 16> stack;
};

/// Raw breakpoint hit event (captured on ROM entry points)
struct RawBreakpointEvent
{
    // Timing
    uint64_t tstate;
    uint32_t frameNumber;
    uint64_t timestamp;  // Duplicate of tstate for RingBuffer compatibility
    
    // Breakpoint
    uint16_t address;          // Breakpoint address hit
    
    // Z80 Context (full snapshot)
    uint16_t pc;
    uint16_t sp;
    uint16_t af, bc, de, hl;
    uint16_t af_, bc_, de_, hl_;  // Alternate set
    uint16_t ix, iy;
    uint8_t i, r;
    uint8_t iff1, iff2, im;
    
    // Stack snapshot (8 return addresses)
    std::array<uint8_t, 16> stack;
};

/// TR-DOS command types
enum class TRDOSCommand : uint8_t
{
    UNKNOWN = 0,
    LOAD,
    SAVE,
    CAT,        // LIST
    RUN,
    FORMAT,
    ERASE,
    COPY,
    VERIFY,
    MERGE,
    MOVE,
    NEW,        // Rename
    GOTO,       // Change drive
};

/// Semantic event captured by the analyzer
// Event context capturing state at time of event
struct EventContext
{
    uint16_t pc;                         // Current Program Counter
    uint16_t callerAddress;              // Immediate return address from stack
    uint16_t originalRAMCaller;          // First non-ROM address in stack
    uint8_t iff1;                        // Interrupt Enable Flag (1=EI, 0=DI)
    uint8_t im;                          // Interrupt Mode (0, 1, 2)
};

/// Semantic event captured by the analyzer
struct TRDOSEvent
{
    // Timing
    uint64_t timestamp;         // T-state when event occurred
    uint32_t frameNumber;       // Video frame number
    
    // Classification
    TRDOSEventType type;
    
    // Context
    EventContext context;
    
    // FDC state (if applicable)
    uint8_t track;
    uint8_t sector;
    uint8_t side;
    uint8_t fdcCommand;
    uint8_t fdcStatus;
    
    // Command info (if applicable)
    TRDOSCommand command;
    
    // Transfer info
    uint16_t bytesTransferred;
    
    // Flags
    uint16_t flags;
    
    // For file operations
    std::string filename;       // 8 chars max for TR-DOS
    
    /// Format event as human-readable string
    std::string format() const;
};
