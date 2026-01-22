#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

/// TR-DOS command types
enum class TRDOSCommand : uint8_t
{
    UNKNOWN = 0,
    LOAD,
    SAVE,
    CAT,
    RUN,
    FORMAT,
    ERASE,
    COPY,
    VERIFY,
    MERGE,
    MOVE,
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
    uint16_t pc;                // Program counter at event
    uint16_t callerAddress;     // Return address from stack
    
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
    uint8_t flags;
    
    // For file operations
    std::string filename;       // 8 chars max for TR-DOS
    
    /// Format event as human-readable string
    std::string format() const;
};
