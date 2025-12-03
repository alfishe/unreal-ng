#pragma once
#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/fdc/wd1793state.h"

/// Helper class to perform comprehensive data collection from WD1793 class
/// @details This class provides functionality to collect, record, and analyze operations
/// performed by the WD1793 floppy disk controller. It captures commands, register operations,
/// and data transfers with detailed context information including timing, CPU state, and disk status.
/// The collected data can be used for debugging, performance analysis, and behavioral validation
/// of the emulated floppy disk subsystem.
/// @see WD1793 For detailed information about the emulated floppy disk controller implementation
class WD1793Collector
{
    /// region <Constants>
public:
    /// Number of stack values to collect
    static constexpr int STACK_VALUES_COUNT = 5;
    /// endregion </Constants>

    /// region <Command collection>
public:
    /// Operation type for command records
    enum class OperationType
    {
        UNKNOWN,
        COMMAND,    // Command register write
        REG_WRITE,  // Register write
        REG_READ,   // Register read
        DATA_WRITE, // Data write
        DATA_READ   // Data read
    };

    /// Structure to store command information
    struct CommandRecord
    {
        size_t tState;                      // T-state time when command was issued
        uint8_t port;                       // Port number that was accessed
        std::string portName;               // Human-readable port name
        OperationType operation;            // Type of operation
        WD1793::WD_COMMANDS decodedCommand; // Decoded command
        uint8_t commandRegister;            // Command value with flags
        uint8_t trackRegister;              // Current track register value
        uint8_t sectorRegister;             // Current sector register value
        uint8_t dataRegister;               // Current data register value
        uint8_t statusRegister;             // Status register value before command
        uint8_t beta128Status;              // Beta128 status register value
        bool diskInserted;                  // Whether disk was inserted
        uint8_t currentTrack;               // Physical track position
        uint16_t pc;                        // Z80 Program Counter value when command was issued
        std::string bankName;               // Memory bank name when command was issued
        std::vector<uint16_t> stack;        // Stack values (SP, SP+2, SP+4, SP+6, SP+8, SP+10, SP+12)
        bool isCommand;                     // Whether this is a command (write to command register)
        bool isWrite;                       // Whether this is a write operation
        bool indexPulse;                    // Whether index pulse was active during the command
        bool isCompletionRecord;            // Whether this is a completion record
        std::string description;            // Human-readable description of the operation


        CommandRecord
        (
            size_t t,                    // Current T-state time
            uint8_t p,                   // Port number
            const std::string& pn,       // Human-readable port name
            OperationType op,            // Type of operation
            WD1793::WD_COMMANDS decoded, // Decoded command
            uint8_t value,               // Command value with flags
            uint8_t track,               // Current track register
            uint8_t sector,              // Current sector register
            uint8_t data,                // Current data register
            uint8_t status,              // Status register before command
            uint8_t beta128,             // Beta128 status register
            bool disk,                   // Whether disk is inserted
            uint8_t curTrack,            // Physical track position
            uint16_t programCounter,     // Z80 Program Counter
            const std::string& bank,     // Memory bank name
            const std::vector<uint16_t>& stackValues, // Stack values
            bool isCmd,
            bool isWr,
            bool indexPulse = false,     // Whether index pulse was active
            const std::string& desc = "" // Human-readable description
        ) :
            tState(t), port(p), portName(pn), operation(op),
            decodedCommand(decoded),
            commandRegister(value),
            trackRegister(track), sectorRegister(sector), dataRegister(data),
            statusRegister(status), beta128Status(beta128), diskInserted(disk), currentTrack(curTrack),
            pc(programCounter), bankName(bank), stack(stackValues),
            isCommand(isCmd), isWrite(isWr), indexPulse(indexPulse), description(desc)
        {}

        ~CommandRecord() = default;
    };



    /// Constructor
    /// @param wd1793 Reference to the WD1793 instance to collect data from
    WD1793Collector() = default;

    // Get human-readable name for a port
    // @param port Port number
    // @param isWrite Whether this is a write operation (false for read)
    // @return Human-readable name of the port
    static std::string getPortName(uint8_t port, bool isWrite = false);

    // Get operation description
    // @param wd1793 Reference to the WD1793 instance to get state from
    // @param port Port number that was accessed
    // @param operation Type of operation
    // @param value Value that was written or read
    // @param isWrite True if this is a write operation, false if read
    // @return Human-readable description of the operation
    static std::string getOperationDescription(const WD1793& wd1793, uint8_t port, uint8_t value, bool isWrite);

    // Get an operation type as string
    static std::string getOperationTypeString(OperationType operation);

    /// Record a command start (write to command register 0x1F)
    /// @param wd1793 Reference to the WD1793 instance
    /// @param command The command value that was written
    void recordCommandStart(WD1793& wd1793, uint8_t command);
    
    /// Record command completion
    /// @param wd1793 Reference to the WD1793 instance
    void recordCommandEnd(WD1793& wd1793);

    /// Record a port access (any port except command register)
    /// @param wd1793 Reference to the WD1793 instance
    /// @param port The port number that was accessed (0x1F-0x3F for WD1793)
    /// @param value The value that was written to or read from the port
    /// @param isWrite True if this is a write operation, false if read
    void recordPortAccess(WD1793& wd1793, uint8_t port, uint8_t value, bool isWrite);

    // Dump collected commands to a file
    void dumpCollectedCommandInfo(const std::string& filename);

private:
    std::vector<CommandRecord> _commandCollection;
};