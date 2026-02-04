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
    
    // Breakpoint address info
    uint16_t address;          // Z80 address where breakpoint hit
    std::string address_label; // Human-readable label (e.g., "CMD_DISPATCHER", "SERVICE_ENTRY")
    
    // Physical memory page info
    std::string page_type;     // "ROM", "RAM", or "UNKNOWN"
    uint8_t page_index;        // Page number (0-255)
    uint16_t page_offset;      // Offset within page
    
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

/// TR-DOS low-level service codes (C register at $3D13)
/// NOTE: These are disk API service codes, NOT user commands!
/// User commands (RUN, LOAD, CAT) are detected via $3D1A + CH_ADD parsing
enum class TRDOSService : uint8_t
{
    UNKNOWN = 0xFF,
    RESTORE = 0x00,         // Move head to Track 0
    SELECT_DRIVE = 0x01,    // Select drive A=0-3
    SEEK_TRACK = 0x02,      // Move head to track A
    SET_SECTOR = 0x03,      // Set sector register
    SET_DMA = 0x04,         // Set transfer address HL
    READ_SECTORS = 0x05,    // Read B sectors from D(track), E(sector) to HL
    WRITE_SECTORS = 0x06,   // Write B sectors from HL to D(track), E(sector)
    CATALOG = 0x07,         // Print directory to stream A
    READ_DESCRIPTOR = 0x08, // Read catalog entry A (0-127)
    WRITE_DESCRIPTOR = 0x09,// Write catalog entry
    FIND_FILE = 0x0A,       // Search for file in $5CDD
    SAVE_FILE = 0x0B,       // Save file (HL=start, DE=length)
    SAVE_BASIC = 0x0C,      // Save BASIC program
    EXIT = 0x0D,            // Return to BASIC
    LOAD_FILE = 0x0E,       // Load file (A=0:orig addr, A=3:HL addr)
    DELETE_SECTOR = 0x12,   // Internal delete operation
    MOVE_DESC_IN = 0x13,    // Copy descriptor to $5CDD (HL=source)
    MOVE_DESC_OUT = 0x14,   // Copy $5CDD to memory (HL=dest)
    FORMAT_TRACK = 0x15,    // Format single track
    SELECT_SIDE_0 = 0x16,   // Select upper disk side
    SELECT_SIDE_1 = 0x17,   // Select lower disk side
    READ_SYS_SECTOR = 0x18, // Read Track 0, Sector 9
};

/// TR-DOS user commands (detected via token at CH_ADD when hitting $3D1A)
/// These are BASIC tokens that represent user-typed commands
enum class TRDOSUserCommand : uint8_t
{
    UNKNOWN = 0,
    CAT = 0xCF,             // CAT (list directory)
    SAVE = 0xF8,            // SAVE
    LOAD_RUN_MERGE = 0xEF,  // LOAD, RUN, or MERGE (needs disambiguation)
    FORMAT = 0xD0,          // FORMAT
    MOVE = 0xD1,            // MOVE (rename)
    ERASE = 0xD2,           // ERASE (delete)
    COPY = 0xFF,            // COPY
    MERGE = 0xD5,           // MERGE
    VERIFY = 0xD6,          // VERIFY
};

/// Legacy alias for compatibility (prefer TRDOSService for $3D13 events)
using TRDOSCommand = TRDOSService;

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
    
    // Service/Command info (if applicable)
    TRDOSService service;           // Low-level service code (C register at $3D13)
    TRDOSUserCommand userCommand;   // User-typed command (token at CH_ADD)
    
    // Transfer info
    uint16_t bytesTransferred;
    
    // Flags
    uint16_t flags;
    
    // For file operations
    std::string filename;       // 8 chars max for TR-DOS
    
    /// Format event as human-readable string
    std::string format() const;
};
