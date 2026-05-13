#include "wd1793_collector.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/fdc/wd1793state.h"

/// Get port name for a given port number
/// @param port Port number
/// @return Human-readable name of the port
// Forward declaration of the WD1793 class
class WD1793;

// Static string constants for port names to avoid string construction on each call
namespace PortNames
{
    // WD1793 Register Names
    static constexpr const char* COMMAND_REGISTER = "Command Register";        // PORT_1F write
    static constexpr const char* STATUS_REGISTER = "Status Register";          // PORT_1F read
    static constexpr const char* TRACK_REGISTER = "Track Register";            // PORT_3F read/write
    static constexpr const char* SECTOR_REGISTER = "Sector Register";          // PORT_5F read/write
    static constexpr const char* DATA_REGISTER = "Data Register";              // PORT_7F read/write
    static constexpr const char* SYSTEM_REGISTER = "Beta128 Status Register";  // PORT_FF read
    static constexpr const char* BETA128_CTRL = "Beta128 Control Register";    // PORT_FF write
    static constexpr const char* UNKNOWN = "Unknown port";                     // Unknown port
}

std::string WD1793Collector::getPortName(uint8_t port, bool isWrite)
{
    std::string result;
    const char* portName = "Unknown";
    
    // Map port numbers to meaningful names based on WD1793 register layout
    switch (port)
    {
        case WD1793::PORT_1F:  // 0x1F - Command (write) / Status (read)
            portName = isWrite ? PortNames::COMMAND_REGISTER : PortNames::STATUS_REGISTER;
            break;
        case WD1793::PORT_3F:  // 0x3F - Track register (read/write)
            portName = PortNames::TRACK_REGISTER;
            break;
        case WD1793::PORT_5F:  // 0x5F - Sector register (read/write)
            portName = PortNames::SECTOR_REGISTER;
            break;
        case WD1793::PORT_7F:  // 0x7F - Data register (read/write)
            portName = PortNames::DATA_REGISTER;
            break;
        case WD1793::PORT_FF:  // 0xFF - System (read) / Beta128 control (write)
            portName = isWrite ? PortNames::BETA128_CTRL : PortNames::SYSTEM_REGISTER;
            break;
        default:
            portName = PortNames::UNKNOWN;
            break;
    }
    
    // Format the result with the port number
    return StringHelper::Format("%s (0x%02X)", portName, port);
}

// Lookup table for operation type strings
static constexpr const char* OPERATION_TYPE_STRINGS[] = 
{
    "Unknown",          // OperationType::UNKNOWN
    "Command",          // OperationType::COMMAND
    "Register Write",   // OperationType::REG_WRITE
    "Register Read",    // OperationType::REG_READ
    "Data Write",       // OperationType::DATA_WRITE
    "Data Read"         // OperationType::DATA_READ
};

/// Get operation type as string
/// @param operation Operation type enum value
/// @return String representation of the operation type
std::string WD1793Collector::getOperationTypeString(OperationType operation)
{
    const size_t index = (size_t)operation;
    const size_t numOperations = sizeof(OPERATION_TYPE_STRINGS) / sizeof(OPERATION_TYPE_STRINGS[0]);
    
    std::string result = OPERATION_TYPE_STRINGS[0];

    if (index < numOperations)
    {
        result = OPERATION_TYPE_STRINGS[index];
    }
    
    return result;
}

/// Get human-readable description of an operation
/// @param wd1793 Reference to the WD1793 instance to get state from
/// @param port Port number that was accessed
/// @param operation Type of operation
/// @param value Value that was written or read
/// @param isWrite True if this is a write operation, false if read
/// @return Human-readable description of the operation
std::string WD1793Collector::getOperationDescription(const WD1793& wd1793, uint8_t port, uint8_t value, bool isWrite)
{
    std::stringstream ss;
    
    // Base description on port type and operation
    switch (port)
    {
        case WD1793::PORT_1F: 
            if (isWrite)
            {
                // Writing to command register (1Fh)
                ss << "Command register: ";
                
                // Use the existing decodeWD93Command method to get the command type
                WD1793::WD_COMMANDS command = wd1793.decodeWD93Command(value);
                
                // Get the command name
                const char* commandName = WD1793::getWD_COMMANDName(command);
                ss << commandName;
                
                // Add command-specific details based on the command type
                if (command >= WD1793::WD_COMMANDS::WD_CMD_STEP && command <= WD1793::WD_COMMANDS::WD_CMD_STEP_OUT) {
                    // For step commands, show direction based on the LSB of the command value
                    if (value & 0x01)
                    {
                        ss << " (to higher tracks)";
                    }
                    else
                    {
                        ss << " (to lower tracks)";
                    }
                }
                
                // Add flags for sector operations
                if (command == WD1793::WD_COMMANDS::WD_CMD_READ_SECTOR || 
                    command == WD1793::WD_COMMANDS::WD_CMD_WRITE_SECTOR) {
                    // For read/write sector, show if it's a single or multiple operation
                    if (value & 0x01) {
                        ss << " (multiple)";
                    } else {
                        ss << " (single)";
                    }
                }
                // Add flags if present
                if (value & 0x01) ss << " (h)";  // Head load flag
                if (value & 0x02) ss << " (v)";  // Verify flag
                if (value & 0x04) ss << " (!)";  // Write precompensation flag
                if (value & 0x08) ss << " (s)";  // Step rate flag
                if (value & 0x10) ss << " (u)";  // Update flag
                if (value & 0x20) ss << " (m)";  // Multiple sector flag
                if (value & 0x40) ss << " (d)";  // Deleted data mark flag
                if (value & 0x80) ss << " (a)";  // Additional delay flag
            }
            else
            {
                // Reading from status register (1Fh)
                ss << StringHelper::Format("Status register: 0x%02X [", value);
                
                // Get the last command to determine how to interpret status bits
                WD1793::WD_COMMANDS lastCmd = wd1793.getLastDecodedCommand();
                bool isType1Command = (static_cast<uint8_t>(lastCmd) & 0x80) == 0;
                
                if (isType1Command || lastCmd == WD1793::WD_COMMANDS::WD_CMD_FORCE_INTERRUPT)
                {
                    // Type I commands (Restore, Seek, Step, Step In, Step Out, Force Interrupt)
                    if (value & 0x01) ss << "Busy ";
                    if (value & 0x02) ss << "Index ";
                    if (value & 0x04) ss << "Track0 ";
                    if (value & 0x08) ss << "CRC_Error ";
                    if (value & 0x10) ss << "Seek_Error ";
                    if (value & 0x20) ss << "Head_Loaded ";
                    if (value & 0x40) ss << "Write_Protect ";
                    if (value & 0x80) ss << "Not_Ready ";
                }
                else if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_SECTOR || 
                         lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_ADDRESS)
                {
                    // Type II commands (Read Sector, Read Address)
                    if (value & 0x01) ss << "Busy ";
                    if (value & 0x02) ss << "DRQ ";
                    if (value & 0x04) ss << "Lost_Data ";
                    if (value & 0x08) ss << "CRC_Error ";
                    if (value & 0x10) ss << "RNF ";
                    if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_SECTOR && (value & 0x20)) ss << "RecType ";
                    if (value & 0x80) ss << "Not_Ready ";
                }
                else if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_WRITE_SECTOR || 
                        lastCmd == WD1793::WD_COMMANDS::WD_CMD_WRITE_TRACK)
                {
                    // Type III commands (Write Sector, Write Track)
                    if (value & 0x01) ss << "Busy ";
                    if (value & 0x02) ss << "DRQ ";
                    if (value & 0x04) ss << "Lost_Data ";
                    if (value & 0x08) ss << "CRC_Error ";
                    if (value & 0x10) ss << "RNF ";
                    if (value & 0x20) ss << "Write_Fault ";
                    if (value & 0x40) ss << "Write_Protect ";
                    if (value & 0x80) ss << "Not_Ready ";
                }
                else if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_TRACK)
                {
                    // Special case for Read Track
                    if (value & 0x01) ss << "Busy ";
                    if (value & 0x02) ss << "DRQ ";
                    if (value & 0x04) ss << "Lost_Data ";
                    if (value & 0x80) ss << "Not_Ready ";
                }
                else
                {
                    // Fallback for unknown commands
                    if (value & 0x01) ss << "Bit0 ";
                    if (value & 0x02) ss << "Bit1 ";
                    if (value & 0x04) ss << "Bit2 ";
                    if (value & 0x08) ss << "Bit3 ";
                    if (value & 0x10) ss << "Bit4 ";
                    if (value & 0x20) ss << "Bit5 ";
                    if (value & 0x40) ss << "Bit6 ";
                    if (value & 0x80) ss << "Bit7 ";
                }
                
                // Remove trailing space if any
                std::string statusStr = ss.str();
                if (statusStr.back() == ' ')
                {
                    statusStr.pop_back();
                    ss.str("");
                    ss << statusStr;
                }
                
                ss << "]";
            }
            break;
            
        case WD1793::PORT_3F: // Track register
            ss << "Track register " << (isWrite ? "set to " : "read as ")
               << std::dec << static_cast<int>(value);
            break;
            
        case WD1793::PORT_5F: // Sector register
            ss << "Sector register " << (isWrite ? "set to " : "read as ")
               << std::dec << static_cast<int>(value);
            break;
            
        case WD1793::PORT_7F: // Data register
            ss << "Data register " << (isWrite ? "write " : "read ")
               << StringHelper::Format("0x%02X", value);
            break;
            
        case WD1793::PORT_FF: // BetaDisk Interface control/status register
            if (isWrite)
            {
                // Control register write (FFh) - BetaDisk control
                ss << "BetaDisk control register set to " << StringHelper::Format("0x%02X [", value);
                
                // Bit 7: Reserved (0)

                // Bit 6: Density (0=double, 1=single)
                if (value & 0x40) ss << "SINGLE_DENSITY ";
                else ss << "DOUBLE_DENSITY ";
                
                // Bit 5: Reserved (0)
                

                // Bit 4: Side select (0=lower, 1=upper)
                if (value & 0x10) ss << "UPPER_SIDE ";
                else ss << "LOWER_SIDE ";
                
                // Bit 3: Prepare (1=prepare for operation)
                if (value & 0x08) ss << "PREPARE ";
                
                // Bit 2: Reset (0=reset controller)
                if (!(value & 0x04)) ss << "RESET ";
                
                // Bit 1-0: Drive select (00=A, 01=B, 10=C, 11=D)
                uint8_t driveSel = value & 0x03;
                ss << "DRIVE_" << static_cast<char>('A' + driveSel);
                
                ss << "]";
            }
            else
            {
                // Status register read (FFh) - BetaDisk status
                ss << "BetaDisk status register read as " << StringHelper::Format("0x%02X [", value);
                
                // Bit 7: Ready (1=controller is ready)
                if (value & 0x80) ss << "INTRQ ";
                
                // Bit 6: Data available (1=data byte available for reading)
                if (value & 0x40) ss << "DRQ ";
                
                // Bits 5-0: Not used in status register
                
                ss << "]";
            }
            break;
            
        default:
            ss << StringHelper::Format("Unknown port access: 0x%02X", value);
            break;
    }
    
    return ss.str();
}

void WD1793Collector::recordCommandStart(WD1793& wd1793, uint8_t command)
{    
    // Get the Z80 context for timing information
    Z80& z80 = *wd1793._context->pCore->GetZ80();
    Memory& memory = *wd1793._context->pCore->GetMemory();
    
    // Get current state from the WD1793 controller
    WD1793::WD_COMMANDS decodedCommand = wd1793._lastDecodedCmd;
    uint8_t trackRegister = wd1793.getTrackRegister();
    uint8_t sectorRegister = wd1793.getSectorRegister();
    uint8_t dataRegister = wd1793.getDataRegister();
    uint8_t statusRegister = wd1793.getStatusRegister();
    uint8_t beta128Status = wd1793.getBeta128Status();
    FDD* selectedDrive = wd1793.getDrive();
    bool diskInserted = selectedDrive != nullptr && selectedDrive->isDiskInserted();
    uint8_t currentTrack = wd1793._trackRegister;
    
    // Get CPU state
    size_t tState = z80.t;
    uint16_t pc = z80.prev_pc;
    std::string bankName = memory.GetBankNameForAddress(pc);
    
    // Capture stack values
    uint16_t sp = z80.sp;
    std::vector<uint16_t> stackValues(STACK_VALUES_COUNT, 0);
    for (size_t i = 0; i < STACK_VALUES_COUNT && sp + i * 2 < 0xFFFF; i++)
    {
        uint8_t lsb = memory.DirectReadFromZ80Memory(sp + i * 2);
        uint8_t msb = memory.DirectReadFromZ80Memory(sp + i * 2 + 1);
        stackValues[i] = (msb << 8) | lsb;
    }
    
    // Decode command type based on the upper 4 bits
    uint8_t commandType = (command >> 4) & 0x0F;
    switch (commandType)
    {
        case 0: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_RESTORE; break;
        case 1: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_SEEK; break;
        case 2: [[fallthrough]];
        case 3: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_STEP; break;
        case 4: [[fallthrough]];
        case 5: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_STEP_IN; break;
        case 6: [[fallthrough]];
        case 7: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_STEP_OUT; break;
        case 8: [[fallthrough]];
        case 9: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_READ_SECTOR; break;
        case 10: [[fallthrough]];
        case 11: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_WRITE_SECTOR; break;
        case 12: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_READ_ADDRESS; break;
        case 13: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_FORCE_INTERRUPT; break;
        case 14: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_READ_TRACK; break;
        case 15: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_WRITE_TRACK; break;
        default: decodedCommand = WD1793::WD_COMMANDS::WD_CMD_INVALID; break;
    }
    
    // Create description
    std::string description = StringHelper::Format("Command: %s (0x%02X) Track: %d Sector: %d",
        WD1793::getWD_COMMANDName(decodedCommand),
        command,
        trackRegister,
        sectorRegister);
    
    // Create and add the command record
    CommandRecord record(
        tState,                     // T-state time when command was issued
        WD1793::PORT_1F,            // Port number (command register)
        getPortName(WD1793::PORT_1F, true), // Port name
        OperationType::COMMAND,     // Type of operation
        decodedCommand,             // Decoded command
        command,                    // Command value with flags
        trackRegister,              // Current track register value
        sectorRegister,             // Current sector register value
        dataRegister,               // Current data register value
        statusRegister,             // Status register value before command
        beta128Status,              // Beta128 status register value
        diskInserted,               // Whether disk was inserted
        currentTrack,               // Physical track position
        pc,                         // Z80 Program Counter value when command was issued
        bankName,                   // Memory bank name when command was issued
        stackValues,                // Stack values
        true,                       // This is a command
        true,                       // This is a write operation
        wd1793._index,              // Current index pulse state
        description                 // Human-readable description
    );
    
    record.isCompletionRecord = false;
    
    _commandCollection.push_back(record);
}

void WD1793Collector::recordPortAccess(WD1793& wd1793, uint8_t port, uint8_t value, bool isWrite)
{
    // Skip invalid ports
    if (port < 0x1F || port > 0x3F) return;
    
    // Get the Z80 context for timing information
    Z80& z80 = *wd1793._context->pCore->GetZ80();
    Memory& memory = *wd1793._context->pCore->GetMemory();
    
    // Get current state from the WD1793 controller
    uint8_t trackRegister = wd1793.getTrackRegister();
    uint8_t sectorRegister = wd1793.getSectorRegister();
    uint8_t dataRegister = wd1793.getDataRegister();
    uint8_t statusRegister = wd1793.getStatusRegister();
    uint8_t beta128Status = wd1793.getBeta128Status();
    FDD* selectedDrive = wd1793.getDrive();
    bool diskInserted = selectedDrive != nullptr && selectedDrive->isDiskInserted();
    uint8_t currentTrack = wd1793._trackRegister;
    
    // Get CPU state
    size_t tState = z80.t;
    uint16_t pc = z80.prev_pc;
    std::string bankName = memory.GetBankNameForAddress(pc);
    
    // Capture stack values
    uint16_t sp = z80.sp;
    std::vector<uint16_t> stackValues(STACK_VALUES_COUNT, 0);
    for (size_t i = 0; i < STACK_VALUES_COUNT && sp + i * 2 < 0xFFFF; i++)
    {
        uint8_t lsb = memory.DirectReadFromZ80Memory(sp + i * 2);
        uint8_t msb = memory.DirectReadFromZ80Memory(sp + i * 2 + 1);
        stackValues[i] = (msb << 8) | lsb;
    }
    
    // Determine operation type
    OperationType operation = OperationType::UNKNOWN;
    switch (port)
    {
        case WD1793::PORT_1F:  // Status register read
            operation = OperationType::REG_READ;
            break;
            
        case WD1793::PORT_3F:  // Track register (R/W)
            operation = isWrite ? OperationType::REG_WRITE : OperationType::REG_READ;
            break;
            
        case WD1793::PORT_5F:  // Sector register (R/W)
            operation = isWrite ? OperationType::REG_WRITE : OperationType::REG_READ;
            break;
            
        case WD1793::PORT_7F:  // Data register (R/W)
            operation = isWrite ? OperationType::DATA_WRITE : OperationType::DATA_READ;
            break;
            
        default:
            operation = OperationType::UNKNOWN;
            break;
    }
    
    // Create description
    std::string description = StringHelper::Format("%s to %s: 0x%02X", 
        (isWrite ? "Write" : "Read"), 
        getPortName(port, isWrite).c_str(), 
        value);
    
    // Create and add the port access record
    CommandRecord record(
        tState,                     // T-state time when port was accessed
        port,                       // Port number that was accessed
        getPortName(port, isWrite), // Human-readable port name
        operation,                  // Type of operation
        WD1793::WD_COMMANDS::WD_CMD_INVALID, // Not a command
        isWrite ? value : 0,        // Value written (0 for reads)
        trackRegister,              // Current track register value
        sectorRegister,             // Current sector register value
        dataRegister,               // Current data register value
        statusRegister,             // Status register value
        beta128Status,              // Beta128 status register value
        diskInserted,               // Whether disk was inserted
        currentTrack,               // Physical track position
        pc,                         // Z80 Program Counter value
        bankName,                   // Memory bank name
        stackValues,                // Stack values
        false,                      // This is not a command
        isWrite,                    // Whether this is a write operation
        wd1793._index,              // Current index pulse state
        description                 // Human-readable description
    );
    
    _commandCollection.push_back(record);
}

void WD1793Collector::recordCommandEnd(WD1793& wd1793)
{
    if (_commandCollection.empty()) 
        return;

    // Get the Z80 context for timing information
    Z80& z80 = *wd1793._context->pCore->GetZ80();
    Memory& memory = *wd1793._context->pCore->GetMemory();
    
    // Get current state from the WD1793 controller
    uint8_t trackRegister = wd1793.getTrackRegister();
    uint8_t sectorRegister = wd1793.getSectorRegister();
    uint8_t dataRegister = wd1793.getDataRegister();
    uint8_t statusRegister = wd1793.getStatusRegister();
    uint8_t beta128Status = wd1793.getBeta128Status();
    FDD* selectedDrive = wd1793.getDrive();
    bool diskInserted = selectedDrive != nullptr && selectedDrive->isDiskInserted();
    uint8_t currentTrack = wd1793._trackRegister;
    
    // Get CPU state
    size_t tState = z80.t;
    uint16_t pc = z80.prev_pc;
    std::string bankName = memory.GetBankNameForAddress(pc);
    
    // Capture stack values
    uint16_t sp = z80.sp;
    std::vector<uint16_t> stackValues(STACK_VALUES_COUNT, 0);
    for (size_t i = 0; i < STACK_VALUES_COUNT && sp + i * 2 < 0xFFFF; i++)
    {
        uint8_t lsb = memory.DirectReadFromZ80Memory(sp + i * 2);
        uint8_t msb = memory.DirectReadFromZ80Memory(sp + i * 2 + 1);
        stackValues[i] = (msb << 8) | lsb;
    }
    
    // Create completion description
    std::stringstream ss;
    ss << "[COMPLETED] " << WD1793::getWD_COMMANDName(wd1793._lastDecodedCmd);
    ss << " - Status: " << StringHelper::FormatBinary(statusRegister);
    ss << " - " << wd1793.dumpStatusRegister(wd1793._lastDecodedCmd);
    std::string description = ss.str();
    
    // Create and add the completion record
    CommandRecord record(
        tState,                     // T-state time when command completed
        WD1793::PORT_1F,            // Port number (command register)
        getPortName(WD1793::PORT_1F, true), // Port name
        OperationType::COMMAND,     // Type of operation
        wd1793._lastDecodedCmd,     // Decoded command
        0,                          // Command value (not relevant for completion)
        trackRegister,              // Current track register value
        sectorRegister,             // Current sector register value
        dataRegister,               // Current data register value
        statusRegister,             // Status register value at completion
        beta128Status,              // Beta128 status register value
        diskInserted,               // Whether disk was inserted
        currentTrack,               // Physical track position
        pc,                         // Z80 Program Counter value
        bankName,                   // Memory bank name
        stackValues,                // Stack values
        true,                       // This is a command
        false,                      // Not a write operation
        wd1793._index,              // Current index pulse state
        description                 // Human-readable description
    );
    
    // Mark as completion record
    record.isCompletionRecord = true;
    
    _commandCollection.push_back(record);
}

/// Dump collected commands to a file
/// @param filename Path to the output file
void WD1793Collector::dumpCollectedCommandInfo(const std::string& filename)
{
    std::ofstream outFile(filename);
    if (!outFile.is_open())
    {
        return;
    }

    // Write CSV header
    outFile << "T-State,Port,PortName,Operation,Command Reg,Value,Track Reg,Sector Reg,Data Reg,Status Reg,Beta128 Status,Disk,CurTrack,PC,Bank,Stack,IsCommand,IsWrite,IndexPulse,Description\n";

    // Write each command record
    for (const auto& record : _commandCollection)
    {
        // Skip non-command records unless they are completion records
        if (!record.isCommand && !record.isCompletionRecord) continue;

        // Common fields for both command and completion records
        outFile << record.tState << ",";

        // Handle completion records
        if (record.isCompletionRecord)
        {
            // For completion records, leave port/operation fields empty
            outFile << ","              // port
                    << "\"\","          // portName
                    << "\"\",";         // operation

            // Command reg (empty for completion records)
            outFile << "\"\",";

            // Value (empty for completion records)
            outFile << ",";

            // Register values for completion record
            outFile << StringHelper::Format("%d,%d,0x%02X,",
                record.trackRegister,
                record.sectorRegister,
                record.dataRegister);

            // Status register (important for completion)
            outFile << StringHelper::Format("0x%02X,", record.statusRegister);

            // Beta128 status register (empty for completion records)
            outFile << StringHelper::Format("0x%02X,", record.beta128Status);

            // Disk and track info
            outFile << StringHelper::Format("%s,%d,", 
                record.diskInserted ? "Yes" : "No", 
                record.currentTrack);

            // PC and bank (empty for completion records)
            outFile << ",\"\",";  // pc, bankName

            // Stack (empty for completion records)
            outFile << "\"\",";

            // Command/Write flags (empty for completion records)
            outFile << "No,No,No,";  // isCommand, isWrite, indexPulse
        }
        // Handle regular command records
        else
        {
            // Full command record
            outFile << StringHelper::Format("0x%02X,", record.port);
            outFile << "\"" << record.portName << "\",";
            outFile << "\"" << getOperationTypeString(record.operation) << "\",";
            outFile << "\"" << WD1793::getWD_COMMANDName(record.decodedCommand) << "\",";
            outFile << StringHelper::Format("0x%02X,", record.commandRegister);
            outFile << static_cast<int>(record.trackRegister) << ",";
            outFile << static_cast<int>(record.sectorRegister) << ",";
            outFile << StringHelper::Format("0x%02X,", record.dataRegister);
            outFile << StringHelper::Format("0x%02X,", record.statusRegister);
            outFile << StringHelper::Format("0x%02X,", record.beta128Status);
            outFile << (record.diskInserted ? "Yes," : "No,");
            outFile << static_cast<int>(record.currentTrack) << ",";
            outFile << StringHelper::Format("0x%04X,\"", record.pc);
            outFile << record.bankName << "\",";

            // Format stack values
            std::string stackStr;
            for (size_t i = 0; i < record.stack.size(); i++)
            {
                if (i > 0)
                    stackStr += " ";
                stackStr += StringHelper::Format("%04X", record.stack[i]);
            }
            outFile << "\"" << stackStr << "\",";

            // Command flags
            outFile << (record.isCommand ? "Yes," : "No,");
            outFile << (record.isWrite ? "Write," : "Read,");
            outFile << (record.indexPulse ? "Yes," : "No,");
        }

        // Description (always present)
        outFile << "\"" << record.description << "\"\n";
    }

    outFile.close();
}
