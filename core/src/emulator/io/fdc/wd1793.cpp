#include "wd1793.h"

#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

/// region <Constructors / destructors>

WD1793::WD1793(EmulatorContext* context) : PortDecoder(context)
{
    _context = context;
    _logger = context->pModuleLogger;
}

WD1793::~WD1793()
{
}

/// endregion </Constructors / destructors>

/// region <Methods>

void WD1793::reset()
{
    _state = S_IDLE;
    _status = 0;
    _trackRegister = 0;
    _sectorRegister = 0;
    _dataRegister = 0;

    // Execute RESTORE command
    uint8_t restoreValue = 0b0000'1111;
    _lastDecodedCmd = WD_CMD_RESTORE;
    _commandRegister = restoreValue;
    _lastCmdValue = restoreValue;
    //cmdRestore(restoreValue);
}

void WD1793::process()
{
    /// region <Get current Z80 clock state for timings synchronization>
    uint64_t totalTime = _context->emulatorState.t_states;
    uint64_t frameTime = _context->pCore->GetZ80()->t;
    _time = totalTime + frameTime;
    /// endregion <Get current Z80 clock state for timings synchronization>

    /// region <Replacement for lengthy switch()>

    // Get the handler for State1
    FSMHandler handler = _stateHandlerMap[_state];

    if (handler)    // Handler found
    {
        // Call correspondent handler through the pointer or reference
        (this->*handler)();
    }
    else            // Handler is not available
    {

    }

    /// endregion </Replacement for lengthy switch()>
}

/// Emulate disk rotation and index strobe changes
void WD1793::processIndexStrobe()
{
    // Based on:
    // - 300 revolutions per minute => 5 revolutions per second => 200ms or 1/5 second for single revolution
    // - base Z80 frequency 3.5MHz
    // We're getting 700'000 Z80 clock cycles period for each disk revolution / rotation
    static constexpr const size_t DISK_ROTATION_PERIOD_IN_Z80_CLOCK_CYCLES = Z80_FREQUENCY / FDD::DISK_REVOLUTIONS_PER_SECOND;

    // For 4ms index strobe and base Z80 frequency 3.5Mhz we're getting 14'000 Z80 clock cycles for 4ms index strobe duration
    static constexpr const size_t INDEX_STROBE_DURATION_IN_Z80_CLOCK_CYCLES = Z80_CLK_CYCLES_PER_MS * FDD::DISK_INDEX_STROBE_DURATION_MS;

    bool diskInserted = _selectedDrive->isDiskInserted();
    bool motorOn = _selectedDrive->getMotor();

    if (diskInserted && motorOn)
    {
        // Set new state for the INDEX flag based on rotating disk position
        // Note: it is assumed that each disk revolution started with index strobe
        size_t diskRotationPhaseCounter = (_time % DISK_ROTATION_PERIOD_IN_Z80_CLOCK_CYCLES);
        if (diskRotationPhaseCounter < INDEX_STROBE_DURATION_IN_Z80_CLOCK_CYCLES)
        {
            _index = true;
        }
        else
        {
            _index = false;
        }
    }
}

/// endregion </Methods>

/// region <Helper methods>

uint8_t WD1793::getStatusRegister()
{
    bool isType1Command = (_commandRegister & 0x80) == 0;

    if (isType1Command || _lastDecodedCmd == WD_CMD_FORCE_INTERRUPT)
    {
        // Type I or type IV command

        // Clear all bits that will be recalculated
        _status &= ~(WDS_INDEX | WDS_TRK00 | WDS_HEADLOADED | WDS_WRITEPROTECTED);

        // Update index strobe state according rotation timing
        processIndexStrobe();
        if (_index)
        {
            _status |= WDS_INDEX;
        }

        if (_selectedDrive->isTrack00())
        {
            _status |= WDS_TRK00;
        }

        if (_selectedDrive->isWriteProtect())
        {
            _status |= WDS_WRITEPROTECTED;
        }

        // Set head load state based on HLD and HLT signals
        uint8_t headStatus = ((_extStatus & SIG_OUT_HLD) && (_beta128 & 0b0000'1000)) ? WDS_HEADLOADED : 0;
        _status |= headStatus;
    }
    else
    {
        // Type II or III command so bit 1 should be DRQ
    }

    if (isReady())
    {
        _status &= ~WDS_NOTRDY;
    }
    else
    {
        _status |= WDS_NOTRDY;
    }

    return _status;
}

bool WD1793::isReady()
{
    bool result = _selectedDrive->isDiskInserted();

    return result;
}
/// endregion </Helper methods>

/// region <Command handling>


/// Detect Type1 command (RESTORE, SEEK, STEP, STEP IN, STEP OUT)
/// @param command
/// @return
bool WD1793::isType1Command(uint8_t command)
{
    // Type 1 commands have Bit7 low
    static constexpr uint8_t TYPE1_MASK = 0b1000'0000;

    bool result = false;
    uint8_t masked = command & TYPE1_MASK;

    if (masked == 0)
    {
        result = true;
    }

    return result;
}

/// Detect Type2 command (READ SECTOR, WRITE SECTOR)
/// @param command
/// @return
bool WD1793::isType2Command(uint8_t command)
{
    // Type 2 commands have Bit7 high and decoded using highest 3 bits
    static constexpr uint8_t TYPE2_MASK = 0b1110'0000;

    bool result = false;
    uint8_t masked = command & TYPE2_MASK;

    // If READ SECTOR or WRITE SECTOR command codes met
    if (masked == 0b1000'0000 || masked == 0b1010'0000)
    {
        result = true;
    }

    return result;
}

/// Detect Type3 command (READ ADDRESS, READ TRACK, WRITE TRACK)
/// @param command
/// @return
bool WD1793::isType3Command(uint8_t command)
{
    // Type 3commands have Bit7 high and decoded using highest 4 bits
    static constexpr uint8_t TYPE3_MASK = 0b1111'0000;

    bool result = false;
    uint8_t masked = command & TYPE3_MASK;

    // If READ ADDRESS or READ TRACK or WRITE TRACK command codes met
    if (masked == 0b1100'0000 || masked == 0b1110'0000 || masked == 0b1111'0000)
    {
        result = true;
    }

    return result;
}

/// Detect Type4 command (FORCE INTERUPT)
/// @param command
/// @return
bool WD1793::isType4Command(uint8_t command)
{
    // Type 3commands have Bit7 high and decoded using highest 4 bits
    static constexpr uint8_t TYPE3_MASK = 0b1111'0000;

    bool result = false;
    uint8_t masked = command & TYPE3_MASK;

    // If FORCE INTERRUPT command codes met
    if (masked == 0b1101'0000)
    {
        result = true;
    }

    return result;
}

WD1793::WD_COMMANDS WD1793::decodeWD93Command(uint8_t value)
{
    // All 11 WD1793 commands supported
    // - Restore: This command is used to move the read/write head to the outermost track (track 0).
    // - Seek: The seek command is used to move the read/write head to a specified track on the floppy disk.
    // - Step: The step command moves the read/write head in the direction previously specified (inwards or outwards) by the "step in" or "step out" command.
    // - Step In: This command moves the read/write head one track towards the center of the disk.
    // - Step Out: This command moves the read/write head one track away from the center of the disk.
    // - Read Sector: This command allows you to read a single sector from the current track.
    // - Write Sector: The write sector command enables you to write data to a specified sector on the current track.
    // - Read Address: This command reads the address field (track number, side number, sector number) of the current sector.
    // - Read Track: This command is used to read the entire contents of a track into the FDC's internal buffer.
    // - Write Track: The write track command allows you to write an entire track worth of data from the FDC's internal buffer to the floppy disk.
    // - Force Interrupt: This command forces an interrupt to occur, regardless of the current state of the FDC.
    // ╔═════════╤═════════════════╤═══════════════════════════════════════════════╗
    // ║ Command │     Command     │                     Bits                      ║
    // ║   type  │       name      │  7  │  6  │  5  │  4  │  3  │  2  │  1  │  0  ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    1    │ Restore         │  0  │  0  │  0  │  0  │  h  │  V  │  r1 │  r0 ║
    // ║    1    │ Seek            │  0  │  0  │  0  │  1  │  h  │  V  │  r1 │  r0 ║
    // ║    1    │ Step            │  0  │  0  │  1  │  u  │  h  │  V  │  r1 │  r0 ║
    // ║    1    │ Step In         │  0  │  1  │  0  │  u  │  h  │  V  │  r1 │  r0 ║
    // ║    1    │ Step Out        │  0  │  1  │  1  │  u  │  h  │  V  │  r1 │  r0 ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    2    │ Read Sector     │  1  │  0  │  0  │  m  │  s  │  E  │  C  │  0  ║
    // ║    2    │ Write Sector    │  1  │  0  │  1  │  m  │  s  │  E  │  C  │  a0 ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    3    │ Read Address    │  1  │  1  │  0  │  0  │  0  │  E  │  0  │  0  ║
    // ║    3    │ Read Track      │  1  │  1  │  1  │  0  │  0  │  E  │  0  │  0  ║
    // ║    3    │ Write Track     │  1  │  1  │  1  │  1  │  0  │  E  │  0  │  0  ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    4    │ Force Interrupt │  1  │  1  │  0  │  1  │  J3 │  J2 │  J3 │  J0 ║
    // ╚═════════╧═════════════════╧═════╧═════╧═════╧═════╧═════╧═════╧═════╧═════╝
    static constexpr uint8_t commandMasksMatches[WD93_COMMAND_COUNT][2] =
    {
        //    Mask   ,   Match
        { 0b1111'0000, 0b0000'0000 },   // [ 0] Restore          Match value: (  0, 0x00)
        { 0b1111'0000, 0b0001'0000 },   // [ 1] Seek             Match value: ( 16, 0x10)
        { 0b1110'0000, 0b0010'0000 },   // [ 2] Step             Match value: ( 32, 0x20)
        { 0b1110'0000, 0b0100'0000 },   // [ 3] Step In          Match value: ( 64, 0x40)
        { 0b1110'0000, 0b0110'0000 },   // [ 4] Step Out         Match value: ( 96, 0x60)
        { 0b1110'0000, 0b1000'0000 },   // [ 5] Read Sector      Match value: (128, 0x80)
        { 0b1110'0000, 0b1010'0000 },   // [ 6] Write Sector     Match value: (160, 0xA0)
        { 0b1111'0000, 0b1100'0000 },   // [ 7] Read Address     Match value: (192, 0xC0)
        { 0b1111'0000, 0b1110'0000 },   // [ 8] Read Track       Match value: (224, 0xE0)
        { 0b1111'0000, 0b1111'0000 },   // [ 9] Write Track      Match value: (240, 0xF0)
        { 0b1111'0000, 0b1101'0000 }    // [10] Force Interrupt. Match value: (208, 0xD0)
    };

    WD1793::WD_COMMANDS result = WD_CMD_RESTORE;

    int index = -1;
    for (size_t i = 0; i < WD93_COMMAND_COUNT; i++)
    {
        // result = (value & mask) == match
        if ((value & commandMasksMatches[i][0]) == commandMasksMatches[i][1])
        {
            index = i;
            break;
        }
    }

    if (index >= 0)
    {
        result = (WD1793::WD_COMMANDS)index;
    }

    return result;
}

uint8_t WD1793::getWD93CommandValue(WD1793::WD_COMMANDS command, uint8_t value)
{
    static constexpr uint8_t commandMaskValues[WD93_COMMAND_COUNT] =
            {
                    //    Mask
                    0b0000'1111,   // [ 0] Restore          Match value: (  0, 0x00)
                    0b0000'1111,   // [ 1] Seek             Match value: ( 16, 0x10)
                    0b0001'1111,   // [ 2] Step             Match value: ( 32, 0x20)
                    0b0001'1111,   // [ 3] Step In          Match value: ( 64, 0x40)
                    0b0001'1111,   // [ 4] Step Out         Match value: ( 96, 0x60)
                    0b0001'1110,   // [ 5] Read Sector      Match value: (128, 0x80)
                    0b0001'1111,   // [ 6] Write Sector     Match value: (160, 0xA0)
                    0b0000'0100,   // [ 7] Read Address     Match value: (192, 0xC0)
                    0b0000'0100,   // [ 8] Read Track       Match value: (224, 0xE0)
                    0b0000'0100,   // [ 9] Write Track      Match value: (240, 0xF0)
                    0b0000'1111,   // [10] Force Interrupt. Match value: (208, 0xD0)
            };

    uint8_t result = 0x00;

    if (command < sizeof(commandMaskValues) / sizeof(commandMaskValues[0]))
    {
        result = value & commandMaskValues[command];
    }

    return result;
}

/// Handles port #1F (CMD) port writes
/// @param value Command written
void WD1793::processWD93Command(uint8_t value)
{
    static constexpr CommandHandler const commandTable[] =
    {
            &WD1793::cmdRestore,
            &WD1793::cmdSeek,
            &WD1793::cmdStep,
            &WD1793::cmdStepIn,
            &WD1793::cmdStepOut,
            &WD1793::cmdReadSector,
            &WD1793::cmdWriteSector,
            &WD1793::cmdReadAddress,
            &WD1793::cmdReadTrack,
            &WD1793::cmdWriteTrack,
            &WD1793::cmdForceInterrupt
    };

    // Decode command
    WD1793::WD_COMMANDS command = decodeWD93Command(value);
    uint8_t commandValue = getWD93CommandValue(command, value);

    // Persist information about command
    _commandRegister = value;
    _lastDecodedCmd = command;
    _lastCmdValue = commandValue;

    if (command < sizeof(commandTable) / sizeof(commandTable[0]))
    {
        const CommandHandler& handler = commandTable[command];
        bool isBusy = _status & WDS_BUSY;

        if (command == WD_CMD_FORCE_INTERRUPT)          // Force interrupt command executes in any state
        {
            // Call the corresponding command method
            (this->*handler)(commandValue);
        }
        else if (!isBusy)                               // All other commands are ignored if controller is busy
        {
            _commandRegister = value;
            _status |= WDS_BUSY;
            _rqs = 0;
            _indexPulseCounter = 0;
            _rotationCounter = SIZE_MAX;

            // Call the corresponding command method
            (this->*handler)(commandValue);
        }
    }
}


/// Restore (Seek track 0)
/// @param value
void WD1793::cmdRestore(uint8_t value)
{
    std::cout << "Command Restore: " << static_cast<int>(value) << std::endl;

    startType1Command();
    //updateStatusesForSeek(value);
}

/// This command assumes that Track Register contains the track number of the current position
/// of the Read-Write head and the Data Register contains the desired track number.
/// Controller will update the Track register and issue stepping pulses in the appropriate direction
/// until the contents of the Track register are equal to the contents of the Data Register.
/// A verification operation takes place if the V flag is on.
/// The h bit allows the head to be loaded at the start of the command.
/// An interrupt is generated at the completion of the command
/// @param value
void WD1793::cmdSeek(uint8_t value)
{
    std::cout << "Command Seek: " << static_cast<int>(value) << std::endl;

    //updateStatusesForSeek(value);
}

void WD1793::cmdStep(uint8_t value)
{
    std::cout << "Command Step: " << static_cast<int>(value) << std::endl;

    startType1Command();
    //updateStatusesForSeek(value);
}

void WD1793::cmdStepIn(uint8_t value)
{
    std::cout << "Command Step In: " << static_cast<int>(value) << std::endl;

    startType1Command();
    //updateStatusesForSeek(value);
}

void WD1793::cmdStepOut(uint8_t value)
{
    std::cout << "Command Step Out: " << static_cast<int>(value) << std::endl;

    startType1Command();
    //updateStatusesForSeek(value);
}

void WD1793::cmdReadSector(uint8_t value)
{
    std::cout << "Command Read Sector: " << static_cast<int>(value) << std::endl;

    startType2Command();
    //updateStatusesForReadWrite();
}

void WD1793::cmdWriteSector(uint8_t value)
{
    std::cout << "Command Write Sector: " << static_cast<int>(value) << std::endl;

    startType2Command();
    //updateStatusesForReadWrite();
}

/// Upon receipt of the Read Address command, the head is loaded and the Busy Status bit is set.
/// The next encountered ID field is then read in from the disk, and the six data bytes of the
/// ID field are assembled and transferred to the DR, and a DRQ is generated for each byte.
/// The six bytes of the ID field are : Track address, Side number, Sector address, Sector Length, CRC1, CRC2.
/// Although the CRC bytes are transferred to the computer, the FD179X checks for validity and the CRC error
/// status bit is set if there is a CRC error. The track address of the ID field is written into the sector
/// register so that a comparison can be made by the user. At the end of the operation, an interrupt is generated
/// and the Busy status bit is reset.
void WD1793::cmdReadAddress(uint8_t value)
{
    std::cout << "Command Read Address: " << static_cast<int>(value) << std::endl;

    startType3Command();
    //updateStatusesForReadWrite();
}

void WD1793::cmdReadTrack(uint8_t value)
{
    std::cout << "Command Read Track: " << static_cast<int>(value) << std::endl;

    startType3Command();
    //updateStatusesForReadWrite();
}

void WD1793::cmdWriteTrack(uint8_t value)
{
    std::cout << "Command Write Track: " << static_cast<int>(value) << std::endl;

    startType3Command();
    //updateStatusesForReadWrite();
}

/// Execute Force Interrupt command
/// @param value Command parameters (already masked)
// ╔═════════╤═════════════════╤═══════════════════════════════════════════════╗
// ║ Command │     Command     │                     Bits                      ║
// ║   type  │       name      │  7  │  6  │  5  │  4  │  3  │  2  │  1  │  0  ║
// ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
// ║    4    │ Force Interrupt │  1  │  1  │  0  │  1  │  J3 │  J2 │  J1 │  J0 ║
// ╚═════════╧═════════════════╧═════╧═════╧═════╧═════╧═════╧═════╧═════╧═════╝
// Bits description:
// Bit0 (J0) = 1 - Not-Ready to Ready transition
// Bit1 (J1) = 1 - Ready to Not-Ready transition
// Bit2 (J2) = 1 - Index pulse
// Bit2 (J3) = 1 - Immediate interrupt
// If all bits [0:3] are not set (= 0) - terminate with no interrupt
void WD1793::cmdForceInterrupt(uint8_t value)
{
    std::cout << "Command Force Interrupt: " << value << std::endl;

    _indexPulseCounter = 0;
    _rotationCounter = SIZE_MAX;

    if (value != 0)
    {
        // Handling interrupts in decreasing priority

        if (value & WD_FORCE_INTERRUPT_IMMEDIATE_INTERRUPT) // Bit3 (J3) - Immediate interrupt
        {
            // Not fully implemented
            _state = S_IDLE;
            _rqs = INTRQ;
            _status &= ~WDS_BUSY;
        }

        if (value & WD_FORCE_INTERRUPT_INDEX_PULSE)         // Bit2 (J2) - Index pulse
        {
            // Not fully implemented
            _state = S_IDLE;
            _rqs = INTRQ;
            _status &= ~WDS_BUSY;
        }

        if (value & WD_FORCE_INTERRUPT_READY)               // Bit1 (J1) - Ready to Not-Ready transition
        {
            // Not fully implemented
            _state = S_IDLE;
            _rqs = INTRQ;
            _status &= ~WDS_BUSY;
        }

        if (value & WD_FORCE_INTERRUPT_NOT_READY)           // Bit0 (J0) - Not-Ready to Ready transition
        {
            _state = S_IDLE;
            _rqs = INTRQ;
            _status &= ~WDS_BUSY;
        }
    }
    else
    {
        // Terminate with no interrupt
        _state = S_IDLE;
        _status &= ~WDS_BUSY;
        _rqs = 0;
    }
}

void WD1793::startType1Command()
{
    _status |= WDS_BUSY;                        // Set BUSY flag
    _status &= ~(WDS_SEEKERR | WDS_CRCERR);     // Clear positioning and CRC errors

    // Handle HEAD LOAD flag
    if (_commandRegister & CMD_SEEK_HEADLOAD)
    {
        // Activate HLD
    }
    else
    {
        // Deactivate HLD
    }
}

void WD1793::startType2Command()
{
    _status |= WDS_BUSY;                                // Set BUSY flag
    _status &= ~(WDS_LOST | WDS_NOTFOUND |              // Reset Type2 error flags
                WDS_RECORDTYPE | WDS_WRITEPROTECTED);
    _dataRegisterWritten = false;                       // Type2 commands have timeout for data availability in Data Register

    if (!isReady())
    {
        // If drive is not ready - end immediately
        endCommand();
    }
    else
    {
        if (_commandRegister & CMD_DELAY)
        {
            // 30ms @ 1MHz or 15ms @ 2MHz delay requested
        }
        else
        {
            // No delay, so execute Type2 command immediately

        }
    }
}

void WD1793::startType3Command()
{
    _status |= WDS_BUSY;                                // Set BUSY flag
    _status &= ~(WDS_LOST | WDS_NOTFOUND |              // Reset Type3 error flags
                 WDS_RECORDTYPE);

    if (!isReady())
    {
        // If drive is not ready - end immediately
        endCommand();
    }
    else
    {
        if (_commandRegister & CMD_DELAY)
        {
            // 30ms @ 1MHz or 15ms @ 2MHz delay requested
        }
        else
        {
            // No delay, so execute Type2 command immediately

        }
    }
}

void WD1793::endCommand()
{
    _status &= ~WDS_BUSY;                        // Reset BUSY flag
}

/// endregion </Command handling>

/// region <State machine handlers>
void WD1793::processIdle()
{
    _status &= ~WDS_BUSY;   // Remove busy flag

    // Stop motor after 3 seconds (3 * 5 revolutions per second) being idle
    if (_indexPulseCounter > 3 * FDD_RPS || _time > _rotationCounter)
    {
        _indexPulseCounter = 3 * FDD_RPS;
        _status = 0x00;                     // Clear status
        _status |= WDS_NOTRDY;              // Set NOT READY status bit
        _extStatus &= ~SIG_OUT_HLD;         // Unload read-write head

        _selectedDrive->setMotor(false);    // Stop motor
    }

    _rqs = INTRQ;
}

void WD1793::processWait()
{

}

/// endregion </State machine handlers>

/// region <PortDevice interface methods>

uint8_t WD1793::portDeviceInMethod(uint16_t port)
{
    uint8_t result = 0xFF;

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:   // Return status register value
            _rqs &= ~INTRQ;     // Reset INTRQ (Interrupt request) flag

            result = getStatusRegister();

            // TODO: remove debug
            //std::cout << dumpStatusRegister(_lastDecodedCmd);
            break;
        case PORT_3F:   // Return current track number
            result = _trackRegister;
            break;
        case PORT_5F:   // Return current sector number
            result = _sectorRegister;
            break;
        case PORT_7F:   // Return data byte and update internal state
            _rqs &= ~DRQ;
            result = _dataRegister;
            break;
        case PORT_FF:   // Handle BETA128 system port (#FF)
            result = _rqs | (_beta128 & 0x3F);
            break;
        default:
            break;
    }

    return result;
}

void WD1793::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    MLOGINFO("Out port:0x04X, value: 0x%02X", port, value);

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:   // Write to Command Register
            //processWD93Command(value);

            //TODO: remove debug
            std::cout << dumpCommand(value);
            break;
        case PORT_3F:   // Write to Track Register
            //_track = value;

            //TODO: remove debug
            //std::cout << StringHelper::Format("#3F - Set track: 0x%02X", _track) << std::endl;
            break;
        case PORT_5F:   // Write to Sector Register
            //_sector = value;

            //TODO: remove debug
            //std::cout << StringHelper::Format("#5F - Set sector: 0x%02X", _sector) << std::endl;
            break;
        case PORT_7F:   // Write to Data Register
            //_data = value;
            //_rqs &= ~DRQ;
            //_status &= ~WDS_DRQ;

            //TODO: remove debug
            //std::cout << StringHelper::Format("#7F - Set data: 0x%02X", _data) << std::endl;
            break;
        case PORT_FF:   // Write to Beta128 system register
            //processBeta128(value);
            break;
        default:
            break;
    }
}

/// endregion </PortDevice interface methods>

/// region <Ports interaction>

bool WD1793::attachToPorts()
{
    bool result = false;

    PortDecoder* decoder = _context->pPortDecoder;
    if (decoder)
    {
        _portDecoder = decoder;

        result  = decoder->RegisterPortHandler(0x001F, this);
        result &= decoder->RegisterPortHandler(0x003F, this);
        result &= decoder->RegisterPortHandler(0x005F, this);
        result &= decoder->RegisterPortHandler(0x007F, this);
        result &= decoder->RegisterPortHandler(0x00FF, this);

        if (result)
        {
            _chipAttachedToPortDecoder = true;
        }
    }

    return result;
}

void WD1793::detachFromPorts()
{
    if (_portDecoder && _chipAttachedToPortDecoder)
    {
        _portDecoder->UnregisterPortHandler(0x001F);
        _portDecoder->UnregisterPortHandler(0x003F);
        _portDecoder->UnregisterPortHandler(0x005F);
        _portDecoder->UnregisterPortHandler(0x007F);
        _portDecoder->UnregisterPortHandler(0x00FF);

        _chipAttachedToPortDecoder = false;
    }
}

/// endregion </Ports interaction>

/// region <Debug methods>
std::string WD1793::dumpStatusRegister(WD_COMMANDS command)
{
    static constexpr const char *STATUS_REGISTER_FLAGS[][8] =
    {
        {"BUSY", "INDEX", "TRACK 0",   "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"},  // RESTORE
        {"BUSY", "INDEX", "TRACK 0",   "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"},  // SEEK
        {"BUSY", "INDEX", "TRACK 0",   "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"},  // STEP
        {"BUSY", "INDEX", "TRACK 0",   "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"},  // STEP IN
        {"BUSY", "INDEX", "TRACK 0",   "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"},  // STEP OUT
        {"BUSY", "DRQ",   "LOST DATA", "CRC ERROR", "RNF",        "ZERO5",       "ZERO6",         "NOT READY"},  // READ ADDRESS
        {"BUSY", "DRQ",   "LOST DATA", "CRC ERROR", "RNF",        "RECORD TYPE", "ZERO6",         "NOT READY"},  // READ SECTOR
        {"BUSY", "DRQ",   "LOST DATA", "ZERO3",     "ZERO4",      "ZERO5",       "ZERO6",         "NOT READY"},  // READ TRACK
        {"BUSY", "DRQ",   "LOST DATA", "CRC ERROR", "RNF",        "WRITE FAULT", "WRITE PROTECT", "NOT READY"},  // WRITE SECTOR
        {"BUSY", "DRQ",   "LOST DATA", "ZERO3",     "ZERO4",      "WRITE FAULT", "WRITE PROTECT", "NOT READY"},  // WRITE TRACK
        // FORCE INTERRUPT doesn't have its own status bits. Bits from previous / ongoing command to be shown instead
    };

    std::stringstream ss;
    uint8_t status = _status;

    ss << StringHelper::Format("Command: %s. Status: 0x%02X", getWD_COMMANDName(command), status) << std::endl;
    switch (command)
    {
        case WD_CMD_FORCE_INTERRUPT:
            ss << "Force interrupt" << std::endl;
            break;
        default:
        {
            for (uint8_t i = 0; i < 8; i++)
            {
                if (status & 0x01)
                {
                    ss << StringHelper::Format("<%s> ", STATUS_REGISTER_FLAGS[command][i]);
                }
                else
                {
                    ss << "<0> ";
                }

                status >>= 1;
            }
        }
            break;
    }
    ss << std::endl;

    std::string result = ss.str();

    return result;
}

std::string WD1793::dumpCommand(uint8_t value)
{
    std::stringstream ss;

    WD1793::WD_COMMANDS command = decodeWD93Command(value);
    uint8_t commandValue = getWD93CommandValue(command, value);
    std::string commandName = getWD_COMMANDName(command);
    std::string commandBits = StringHelper::FormatBinary<uint8_t>(commandValue);

    ss << StringHelper::Format("0x%02X: %s. Bits: %s", value, commandName.c_str(), commandBits.c_str()) << std::endl;

    std::string result = ss.str();

    return result;
}
/// endregion </Debug methods>