#include "wd1793_collector.h"

#include <fstream>
#include <iomanip>
#include <sstream>

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/fdc/wd1793state.h"
#include "emulator/platform.h"

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
}  // namespace PortNames

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
static constexpr const char* OPERATION_TYPE_STRINGS[] = {
    "Unknown",         // OperationType::UNKNOWN
    "Command",         // OperationType::COMMAND
    "Register Write",  // OperationType::REG_WRITE
    "Register Read",   // OperationType::REG_READ
    "Data Write",      // OperationType::DATA_WRITE
    "Data Read"        // OperationType::DATA_READ
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
                if (command >= WD1793::WD_COMMANDS::WD_CMD_STEP && command <= WD1793::WD_COMMANDS::WD_CMD_STEP_OUT)
                {
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
                    command == WD1793::WD_COMMANDS::WD_CMD_WRITE_SECTOR)
                {
                    // For read/write sector, show if it's a single or multiple operation
                    if (value & 0x01)
                    {
                        ss << " (multiple)";
                    }
                    else
                    {
                        ss << " (single)";
                    }
                }
                // Add flags if present
                if (value & 0x01)
                    ss << " (h)";  // Head load flag
                if (value & 0x02)
                    ss << " (v)";  // Verify flag
                if (value & 0x04)
                    ss << " (!)";  // Write precompensation flag
                if (value & 0x08)
                    ss << " (s)";  // Step rate flag
                if (value & 0x10)
                    ss << " (u)";  // Update flag
                if (value & 0x20)
                    ss << " (m)";  // Multiple sector flag
                if (value & 0x40)
                    ss << " (d)";  // Deleted data mark flag
                if (value & 0x80)
                    ss << " (a)";  // Additional delay flag
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
                    if (value & 0x01)
                        ss << "Busy ";
                    if (value & 0x02)
                        ss << "Index ";
                    if (value & 0x04)
                        ss << "Track0 ";
                    if (value & 0x08)
                        ss << "CRC_Error ";
                    if (value & 0x10)
                        ss << "Seek_Error ";
                    if (value & 0x20)
                        ss << "Head_Loaded ";
                    if (value & 0x40)
                        ss << "Write_Protect ";
                    if (value & 0x80)
                        ss << "Not_Ready ";
                }
                else if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_SECTOR ||
                         lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_ADDRESS)
                {
                    // Type II commands (Read Sector, Read Address)
                    if (value & 0x01)
                        ss << "Busy ";
                    if (value & 0x02)
                        ss << "DRQ ";
                    if (value & 0x04)
                        ss << "Lost_Data ";
                    if (value & 0x08)
                        ss << "CRC_Error ";
                    if (value & 0x10)
                        ss << "RNF ";
                    if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_SECTOR && (value & 0x20))
                        ss << "RecType ";
                    if (value & 0x80)
                        ss << "Not_Ready ";
                }
                else if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_WRITE_SECTOR ||
                         lastCmd == WD1793::WD_COMMANDS::WD_CMD_WRITE_TRACK)
                {
                    // Type III commands (Write Sector, Write Track)
                    if (value & 0x01)
                        ss << "Busy ";
                    if (value & 0x02)
                        ss << "DRQ ";
                    if (value & 0x04)
                        ss << "Lost_Data ";
                    if (value & 0x08)
                        ss << "CRC_Error ";
                    if (value & 0x10)
                        ss << "RNF ";
                    if (value & 0x20)
                        ss << "Write_Fault ";
                    if (value & 0x40)
                        ss << "Write_Protect ";
                    if (value & 0x80)
                        ss << "Not_Ready ";
                }
                else if (lastCmd == WD1793::WD_COMMANDS::WD_CMD_READ_TRACK)
                {
                    // Special case for Read Track
                    if (value & 0x01)
                        ss << "Busy ";
                    if (value & 0x02)
                        ss << "DRQ ";
                    if (value & 0x04)
                        ss << "Lost_Data ";
                    if (value & 0x80)
                        ss << "Not_Ready ";
                }
                else
                {
                    // Fallback for unknown commands
                    if (value & 0x01)
                        ss << "Bit0 ";
                    if (value & 0x02)
                        ss << "Bit1 ";
                    if (value & 0x04)
                        ss << "Bit2 ";
                    if (value & 0x08)
                        ss << "Bit3 ";
                    if (value & 0x10)
                        ss << "Bit4 ";
                    if (value & 0x20)
                        ss << "Bit5 ";
                    if (value & 0x40)
                        ss << "Bit6 ";
                    if (value & 0x80)
                        ss << "Bit7 ";
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

        case WD1793::PORT_3F:  // Track register
            ss << "Track register " << (isWrite ? "set to " : "read as ") << std::dec << static_cast<int>(value);
            break;

        case WD1793::PORT_5F:  // Sector register
            ss << "Sector register " << (isWrite ? "set to " : "read as ") << std::dec << static_cast<int>(value);
            break;

        case WD1793::PORT_7F:  // Data register
            ss << "Data register " << (isWrite ? "write " : "read ") << StringHelper::Format("0x%02X", value);
            break;

        case WD1793::PORT_FF:  // BetaDisk Interface control/status register
            if (isWrite)
            {
                // Control register write (FFh) - BetaDisk control
                ss << "BetaDisk control register set to " << StringHelper::Format("0x%02X [", value);

                // Bit 7: Reserved (0)

                // Bit 6: Density (0=double, 1=single)
                if (value & 0x40)
                    ss << "SINGLE_DENSITY ";
                else
                    ss << "DOUBLE_DENSITY ";

                // Bit 5: Reserved (0)

                // Bit 4: Side select (0=lower, 1=upper)
                if (value & 0x10)
                    ss << "UPPER_SIDE ";
                else
                    ss << "LOWER_SIDE ";

                // Bit 3: Prepare (1=prepare for operation)
                if (value & 0x08)
                    ss << "PREPARE ";

                // Bit 2: Reset (0=reset controller)
                if (!(value & 0x04))
                    ss << "RESET ";

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
                if (value & 0x80)
                    ss << "INTRQ ";

                // Bit 6: Data available (1=data byte available for reading)
                if (value & 0x40)
                    ss << "DRQ ";

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

/// Collect command information for logging and analysis
/// @param port Port number that was accessed
/// @param value The value that was written to or read from the port
/// @param isWrite True if this is a write operation, false if read
void WD1793Collector::collectWD1793CallsInfo(WD1793& wd1793, uint8_t port, uint8_t value, bool isWrite)
{
    // Get current CPU state and context
    EmulatorContext* context = wd1793._context;
    Z80& z80 = *context->pCore->GetZ80();
    Memory& memory = *context->pCore->GetMemory();

    // Determine operation type based on port and access type
    OperationType operation = OperationType::UNKNOWN;
    bool isCommand = false;

    // Map host CPU ports to WD1793 registers using class constants
    switch (port)
    {
        case WD1793::PORT_1F:  // Command (write) / Status (read)
            if (isWrite)
            {
                operation = OperationType::COMMAND;
                isCommand = true;
            }
            else
            {
                operation = OperationType::REG_READ;  // Status register read
            }
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

        case WD1793::PORT_FF:  // System port (BETA128 control)
            operation = OperationType::UNKNOWN;
            break;

        default:
            // Handle any other ports that might be used with the WD1793
            operation = OperationType::UNKNOWN;
            break;
    }

    // Get a human-readable description
    std::string description = getOperationDescription(wd1793, port, value, isWrite);

    // Get port name
    std::string portName = getPortName(port, isWrite);

    // Get a current state from the WD1793 controller
    WD1793::WD_COMMANDS decodedCommand = wd1793._lastDecodedCmd;
    uint8_t commandValue = value;
    uint8_t trackRegister = wd1793.getTrackRegister();
    uint8_t sectorRegister = wd1793.getSectorRegister();
    uint8_t dataRegister = wd1793.getDataRegister();
    uint8_t statusRegister = wd1793.getStatusRegister();
    FDD* selectedDrive = wd1793.getDrive();
    bool diskInserted = selectedDrive != nullptr && selectedDrive->isDiskInserted();
    uint8_t currentTrack = wd1793._trackRegister;  // Current track position

    // Get CPU state
    size_t tState = z80.t;
    // Get program counter
    uint16_t pc = z80.prev_pc;

    // Get the current memory bank name
    std::string bankName = memory.GetBankNameForAddress(pc);

    // Capture stack values (up to a specified number of entries)
    uint16_t sp = z80.sp;
    std::vector<uint16_t> stackValues(STACK_VALUES_COUNT, 0);

    for (size_t i = 0; i < STACK_VALUES_COUNT && sp + i * 2 < 0xFFFF; i++)
    {
        uint8_t lsb = memory.DirectReadFromZ80Memory(sp + i * 2);
        uint8_t msb = memory.DirectReadFromZ80Memory(sp + i * 2 + 1);
        stackValues[i] = msb << 8 | lsb;
    }

    // For command register writes, attempt to decode the command
    if (isCommand)
    {
        // Decode command type based on the upper 4 bits
        uint8_t commandType = (value >> 4) & 0x0F;

        switch (commandType)
        {
            case 0:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_RESTORE;
                break;
            case 1:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_SEEK;
                break;
            case 2:
                [[fallthrough]];
            case 3:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_STEP;
                break;
            case 4:
                [[fallthrough]];
            case 5:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_STEP_IN;
                break;
            case 6:
                [[fallthrough]];
            case 7:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_STEP_OUT;
                break;
            case 8:
                [[fallthrough]];
            case 9:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_READ_SECTOR;
                break;
            case 10:
                [[fallthrough]];
            case 11:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_WRITE_SECTOR;
                break;
            case 12:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_READ_ADDRESS;
                break;
            case 13:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_FORCE_INTERRUPT;
                break;
            case 14:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_READ_TRACK;
                break;
            case 15:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_WRITE_TRACK;
                break;
            default:
                decodedCommand = WD1793::WD_COMMANDS::WD_CMD_INVALID;
                break;
        }
    }

    // Create a command record and add it to our collection
    CommandRecord record(tState,          // T-state time when command was issued
                         port,            // Port number that was accessed
                         portName,        // Human-readable port name
                         operation,       // Type of operation
                         decodedCommand,  // Decoded command
                         commandValue,    // Command value with flags
                         trackRegister,   // Current track register value
                         sectorRegister,  // Current sector register value
                         dataRegister,    // Current data register value
                         statusRegister,  // Status register value before command
                         diskInserted,    // Whether disk was inserted
                         currentTrack,    // Physical track position
                         pc,              // Z80 Program Counter value when command was issued
                         bankName,        // Memory bank name when command was issued
                         stackValues,     // Stack values (SP, SP+2, SP+4, SP+6, SP+8, SP+10, SP+12)
                         isCommand,       // Whether this is a command (write to command register)
                         isWrite,         // Whether this is a write operation
                         wd1793._index,   // Current index pulse state
                         description      // Human-readable description of the operation
    );

    // Add to the collection
    _commandCollection.push_back(record);
}

/// Dump collected commands to a file
/// @param filename Path to the output file
void WD1793Collector::dumpCollectedCommandInfo(const std::string& filename)
{
    // Skip if no commands collected
    if (_commandCollection.empty())
        return;

    // Open the output file
    std::ofstream outFile(filename);
    if (!outFile.is_open())
    {
        // Log error or handle file opening failure
        return;
    }

    // Write CSV header
    outFile << "T-State,Port,Port Name,Operation Type,Command,Decoded Command,Track Reg,Sector Reg,"
            << "Data Reg,Status Reg,Disk Inserted,Index Pulse,Current Track,PC,Bank,Stack Values,Description"
            << std::endl;

    // Write each command record
    for (const auto& record : _commandCollection)
    {
        outFile << record.tState << "," << StringHelper::Format("0x%02X", record.port) << ","
                << "\"" << record.portName << "\","
                << "\"" << getOperationTypeString(record.operation) << "\","
                << StringHelper::Format("0x%02X", record.commandRegister) << ","
                << (record.isCommand ? "\"" + std::string(WD1793::getWD_COMMANDName(record.decodedCommand)) + "\""
                                     : "N/A")
                << "," << static_cast<int>(record.trackRegister) << "," << static_cast<int>(record.sectorRegister)
                << "," << StringHelper::Format("0x%02X", record.dataRegister) << ","
                << StringHelper::Format("0x%02X", record.statusRegister) << "," << (record.diskInserted ? "Yes" : "No")
                << "," << (record.indexPulse ? "Yes" : "No") << "," << static_cast<int>(record.currentTrack) << ","
                << StringHelper::Format("0x%04X", record.pc) << ","
                << "\"" << record.bankName << "\",";

        // Format stack values
        outFile << "\"";
        bool first = true;
        for (const auto& stackValue : record.stack)
        {
            if (!first)
                outFile << " ";
            outFile << StringHelper::Format("0x%04X", stackValue);
            first = false;
        }
        outFile << "\",";

        // Description (escape any embedded quotes)
        std::string escapedDesc = record.description;
        StringHelper::ReplaceAll(escapedDesc, "\"", "\"\"");
        outFile << "\"" << escapedDesc << "\"" << std::endl;
    }

    outFile.close();
}
