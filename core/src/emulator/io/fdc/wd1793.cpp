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

    // Attach single drive by default
    _selectedDrive = new FDD(_context);
}

WD1793::~WD1793()
{
    if (_selectedDrive)
    {
        delete _selectedDrive;
    }
}

/// endregion </Constructors / destructors>

/// region <Methods>

void WD1793::reset()
{
    // Clear operations FIFO
    std::queue<WDSTATE> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    _state = S_IDLE;
    _state2 = S_IDLE;
    _statusRegister = 0;
    _trackRegister = 0;
    _sectorRegister = 0x01; // As per datasheet. Sector is set to 1 after the RESTORE command
    _dataRegister = 0;

    _indexPulseCounter = 0;
    _delayTStates = 0;
    _headLoaded = false;

    // Execute RESTORE command
    uint8_t restoreValue = 0b0000'1111;
    _lastDecodedCmd = WD_CMD_RESTORE;
    _commandRegister = restoreValue;
    _lastCmdValue = restoreValue;
    cmdRestore(restoreValue);
}

void WD1793::process()
{
    // Timings synchronization
    processClockTimings();

    // Maintain FDD motor state
    processFDDMotorState();

    // Emulate disk rotation and index strobe changes
    processFDDIndexStrobe();

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

/// endregion </Methods>

/// region <Helper methods>

/// Handle Beta128 interface system controller commands
/// @param value
void WD1793::processBeta128(uint8_t value)
{
    // Set active drive, Bits[0,1] (0..3)
    _drive = value & 0b0000'0011;

    // Set side Bit[4] (0..1)
    _sideUp = ~(value >> 4) & 0b0000'0001;

    // TODO: Select different drive if requested

    // Reset Bit[3] (active low)
    bool reset = !(value & 0b0000'0100);

    if (reset)
    {
        _statusRegister &= ~WDS_NOTRDY;
        _beta128status = INTRQ;

        // Stop FDD motor, reset all related counters
        _selectedDrive->setMotor(false);
        _motorTimeoutTStates = 0;
        _indexPulseCounter = 0;

        // Set initial state after reset
        _lastCmdValue = 0x00;
        _lastDecodedCmd = WD_CMD_RESTORE;
        _lastCmdValue = 0x00;
        cmdRestore(_lastCmdValue);
    }
    else
    {
        uint8_t beta128ChangedBits = _beta128 ^ value;
        if (beta128ChangedBits & SYS_HLT) // When HLT signal positive edge (from 0 to 1) detected
        {
            // FIXME: index strobes should be set by disk rotation timings, not by HLT / BUSY edges
            if (!(_statusRegister & WDS_BUSY))
            {
                _indexPulseCounter++;
            }
        }

        _beta128 = value;
    }
}

/// Handle motor start/stop events as well as timeouts
void WD1793::processFDDMotorState()
{
    // Apply time difference from the previous call
    _motorTimeoutTStates -= _diffTime;

    if (_motorTimeoutTStates <= 0)
    {
        // Motor timeout passed. Prepare to stop
        _motorTimeoutTStates = 0;

        _statusRegister |= WDS_NOTRDY;              // Set NOT READY status bit

        // Unload head (and set all required flags)
        if (_headLoaded)
        {
            unloadHead();
        }

        // Send stop motor command to FDD
        if (_selectedDrive->getMotor())
        {
            stopFDDMotor();

            // Notify via Beta128 status INTRQ bit about changes
            _beta128status = INTRQ;
        }
    }
}

/// Emulate disk rotation and index strobe changes
void WD1793::processFDDIndexStrobe()
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
        bool lastIndex = _index;

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

        // Trigger on index pulse rising edge
        if (!lastIndex && _index)
        {
            _indexPulseCounter++;
        }
    }

    // Update status register for Type 1 commands
    if (isType1Command(_lastCmdValue))
    {
        if (_index)
        {
            _statusRegister |= WDS_INDEX;
        }
        else
        {
            _statusRegister &= ~WDS_INDEX;
        }
    }
}

/// Ensure FDD motor is spinning and set default stop timeout
void WD1793::prolongFDDMotorRotation()
{
    static constexpr const size_t MOTOR_STOP_TIMEOUT_TSTATES = 15 * Z80_FREQUENCY / FDD_RPS;

    // Set motor timeout to 15 disk revolutions (3 seconds)
    _motorTimeoutTStates = MOTOR_STOP_TIMEOUT_TSTATES;

    // Start motor if not spinning yet
    if (!_selectedDrive->getMotor())
    {
        startFDDMotor();
    }
}

/// Activates FDD motor
void WD1793::startFDDMotor()
{
    _selectedDrive->setMotor(true);

    MLOGINFO("FDD motor started");
}

/// Stops FDD motor
void WD1793::stopFDDMotor()
{
    _selectedDrive->setMotor(false);

    MLOGINFO("FDD motor stopped");
}

/// Load head
void WD1793::loadHead()
{
    _statusRegister |= WDS_HEADLOADED;
    _extStatus |= SIG_OUT_HLD;
    _headLoaded = true;

    MLOGINFO("> Head loaded");
}

/// Unload head
void WD1793::unloadHead()
{
    // Update status register only if Type1 command was executed
    if (isType1Command(_commandRegister))
    {
        _statusRegister &= ~WDS_HEADLOADED;
    }

    _extStatus &= ~SIG_OUT_HLD;         // Unload read-write head
    _headLoaded = false;

    MLOGINFO("< Head unloaded");
}

uint8_t WD1793::getStatusRegister()
{
    bool isType1Command = (_commandRegister & 0x80) == 0;

    if (isType1Command || _lastDecodedCmd == WD_CMD_FORCE_INTERRUPT)
    {
        // Type I or type IV command

        // Clear all bits that will be recalculated
        _statusRegister &= ~(WDS_INDEX | WDS_TRK00 | WDS_HEADLOADED | WDS_WRITEPROTECTED);

        // Update index strobe state according rotation timing
        processFDDIndexStrobe();
        if (_index)
        {
            _statusRegister |= WDS_INDEX;
        }

        if (_selectedDrive->isTrack00())
        {
            _statusRegister |= WDS_TRK00;
        }

        if (_selectedDrive->isWriteProtect())
        {
            _statusRegister |= WDS_WRITEPROTECTED;
        }

        // Set head load state based on HLD and HLT signals
        uint8_t headStatus = ((_extStatus & SIG_OUT_HLD) && (_beta128 & 0b0000'1000)) ? WDS_HEADLOADED : 0;
        _statusRegister |= headStatus;
    }
    else
    {
        // Type II or III command so bit 1 should be DRQ
    }

    if (isReady())
    {
        _statusRegister &= ~WDS_NOTRDY;
    }
    else
    {
        _statusRegister |= WDS_NOTRDY;
    }

    return _statusRegister;
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
        0b0000'1111,   // [ 0] Restore
        0b0000'1111,   // [ 1] Seek
        0b0001'1111,   // [ 2] Step
        0b0001'1111,   // [ 3] Step In
        0b0001'1111,   // [ 4] Step Out
        0b0001'1110,   // [ 5] Read Sector
        0b0001'1111,   // [ 6] Write Sector
        0b0000'0100,   // [ 7] Read Address
        0b0000'0100,   // [ 8] Read Track
        0b0000'0100,   // [ 9] Write Track
        0b0000'1111,   // [10] Force Interrupt
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
        bool isBusy = _statusRegister & WDS_BUSY;

        if (command == WD_CMD_FORCE_INTERRUPT)          // Force interrupt command executes in any state
        {
            // Call the corresponding command method
            (this->*handler)(commandValue);
        }
        else if (!isBusy)                               // All other commands are ignored if controller is busy
        {
            _commandRegister = value;
            _statusRegister |= WDS_BUSY;
            _beta128status = 0;
            _indexPulseCounter = 0;

            // Call the corresponding command method
            (this->*handler)(commandValue);
        }
    }
}

/// Resolves Type1 Command r0r1 bits into stepping rate in ms
/// @param command Type1 command
/// @return Resolved stepping speed rate in milliseconds
uint8_t WD1793::getPositioningRateForType1CommandMs(uint8_t command)
{
    uint8_t rateIndex = command & 0b0000'0011;
    uint8_t result = STEP_TIMINGS_MS_1MHZ[rateIndex];

    return result;
}


/// Restore (Seek track 0)
/// @param value
void WD1793::cmdRestore(uint8_t value)
{
    std::string message = StringHelper::Format("Command Restore: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType1Command();

    // We're not sure about current head position at this moment. Will be determined during step positioning
    _trackRegister = 0xFF;

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> S_VERIFY -> S_IDLE
    transitionFSM(WDSTATE::S_STEP);
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
    std::string message = StringHelper::Format("Command Seek: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    message += StringHelper::Format(" From trk: %d, to trk: %d", _trackRegister, _dataRegister);
    MLOGINFO(message.c_str());

    startType1Command();

    // FSM will transition across steps (making required wait cycles as needed):
    // S_SEEK -> S_STEP -> S_SEEK -> ..... -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_SEEK, _steppingMotorRate * TSTATES_PER_MS);
}

/// Performs single head step movement remaining previously set direction
/// @param value STEP command parameter bits
void WD1793::cmdStep(uint8_t value)
{
    std::string message = StringHelper::Format("Command Step: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType1Command();

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
}

void WD1793::cmdStepIn(uint8_t value)
{
    std::string message = StringHelper::Format("Command Step In: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType1Command();

    // Yes, we move the head towards central ring (increasing track number)
    _stepDirectionIn = true;

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
}

void WD1793::cmdStepOut(uint8_t value)
{
    std::string message = StringHelper::Format("Command Step Out: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType1Command();

    // We move the head outwards, to track 0 (decreasing track number)
    _stepDirectionIn = false;

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
}

void WD1793::cmdReadSector(uint8_t value)
{
    std::cout << "Command Read Sector: " << static_cast<int>(value) << std::endl;

    startType2Command();

    // Step 1: search for ID address mark
    _operationFIFO.push(WDSTATE::S_SEARCH_ID);

    // Step 2: start sector reading
    _operationFIFO.push(WDSTATE::S_READ_BYTE);
}

void WD1793::cmdWriteSector(uint8_t value)
{
    std::cout << "Command Write Sector: " << static_cast<int>(value) << std::endl;

    startType2Command();
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
}

void WD1793::cmdReadTrack(uint8_t value)
{
    std::cout << "Command Read Track: " << static_cast<int>(value) << std::endl;

    startType3Command();
}

void WD1793::cmdWriteTrack(uint8_t value)
{
    std::cout << "Command Write Track: " << static_cast<int>(value) << std::endl;

    startType3Command();
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
    std::string message = StringHelper::Format("Command Force Interrupt: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    bool noCommandExecuted = _state == S_IDLE;
    WDSTATE prevState = _state;
    WDSTATE prevState2 = _state2;

    // Ensure we have only relevant parameter bits
    value &= 0b0000'1111;
    if (value != 0)
    {
        // Handling interrupts in decreasing priority

        if (value & WD_FORCE_INTERRUPT_IMMEDIATE_INTERRUPT) // Bit3 (J3) - Immediate interrupt
        {
            // Not fully implemented
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            _beta128status |= INTRQ;
            _beta128status &= ~DRQ;
        }

        if (value & WD_FORCE_INTERRUPT_INDEX_PULSE)         // Bit2 (J2) - Every index pulse
        {
            // Not fully implemented
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            _beta128status |= INTRQ;
            _beta128status &= ~DRQ;
        }

        if (value & WD_FORCE_INTERRUPT_READY)               // Bit1 (J1) - Ready to Not-Ready transition
        {
            // Not fully implemented
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            _beta128status |= INTRQ;
            _beta128status &= ~DRQ;
        }

        if (value & WD_FORCE_INTERRUPT_NOT_READY)           // Bit0 (J0) - Not-Ready to Ready transition
        {
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            _beta128status |= INTRQ;
            _beta128status &= ~DRQ;
        }
    }
    else    // Terminate with no interrupt
    {
        // Currently executed command is terminated and BUSY flag is reset
        _state = S_IDLE;
        _state2 = S_IDLE;
        _delayTStates = 0;

        _statusRegister &= ~WDS_BUSY;       // Deactivate busy flag
        _beta128status &= ~(DRQ | INTRQ);   // Deactivate Data Request (DRQ) and Interrupt Request (INTRQ) signals
    }

    // Clear operations FIFO
    std::queue<WDSTATE> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    // Set status register according Type 1 command layout
    if (noCommandExecuted)
    {
        _statusRegister &= ~(WDS_CRCERR | WDS_SEEKERR | WDS_HEADLOADED);
        _statusRegister |= _selectedDrive->getTrack() == 0 ? WDS_TRK00 : 0x00;
        _statusRegister |= !_selectedDrive->isDiskInserted() ? WDS_NOTRDY : 0x00;
        _statusRegister |= _selectedDrive->isWriteProtect() ? WDS_WRITEPROTECTED : 0x00;

        MLOGINFO("<<== FORCE_INTERRUPT, no active command");
    }
    else
    {
        MLOGINFO("<<== FORCE_INTERRUPT, command interrupted. cmd: %s state: %s state2: %s", getWD_COMMANDName(_lastDecodedCmd), WDSTATEToString(prevState).c_str()), WDSTATEToString(prevState2).c_str();
    }
}

void WD1793::startType1Command()
{
    MLOGINFO("==>> Start Type 1 command");

    _statusRegister |= WDS_BUSY;                            // Set BUSY flag
    _statusRegister &= ~(WDS_SEEKERR | WDS_CRCERR);         // Clear positioning and CRC errors

    _beta128status &= ~(DRQ | INTRQ);               // Clear Data Request and Interrupt request bits

    // Ensure motor is spinning
    prolongFDDMotorRotation();

    // Decode stepping motor rate from bits [0..1] (r0r1)
    _steppingMotorRate = getPositioningRateForType1CommandMs(_commandRegister);

    // Determines if VERIFY (check for ID Address Mark) needs to be done after head positioning
    _verifySeek = _commandRegister & CMD_SEEK_VERIFY;

    // Determines if load should be loaded or unloaded during Type1 command
    _loadHead = _commandRegister & CMD_SEEK_HEADLOAD;
    if (_loadHead)
    {
        loadHead();
    }
    else
    {
        unloadHead();
    }

    // Reset head step counter
    _stepCounter = 0;
}

void WD1793::startType2Command()
{
    MLOGINFO("==>> Start Type 2 command");

    _statusRegister |= WDS_BUSY;                                // Set BUSY flag
    _statusRegister &= ~(WDS_LOSTDATA | WDS_NOTFOUND |              // Reset Type2 error flags
                WDS_RECORDTYPE | WDS_WRITEPROTECTED);
    _dataRegisterAccessed = false;                               // Type2 commands have timeout for data availability in Data Register

    if (!isReady())
    {
        // If drive is not ready - end immediately
        endCommand();
    }
    else
    {
        // Ensure motor is spinning
        prolongFDDMotorRotation();

        // Head must be loaded
        loadHead();

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
    MLOGINFO("==>> Start Type 3 command");

    _statusRegister |= WDS_BUSY;                                // Set BUSY flag
    _statusRegister &= ~(WDS_LOSTDATA | WDS_NOTFOUND |              // Reset Type3 error flags
                 WDS_RECORDTYPE);

    if (!isReady())
    {
        // If drive is not ready - end immediately
        endCommand();
    }
    else
    {
        // Ensure motor is spinning
        prolongFDDMotorRotation();

        // Head must be loaded
        loadHead();

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

/// Each WD1793 command finishes with resetting BUSY flag
void WD1793::endCommand()
{
    _statusRegister &= ~WDS_BUSY;   // Reset BUSY flag
    _beta128status |= INTRQ;        // INTRQ must be set at a completion of any command

    // Transition to IDLE state
    transitionFSM(S_IDLE);

    // Debug logging
    MLOGINFO("<<== End command. status: %s beta128: %s", StringHelper::FormatBinary(_statusRegister).c_str(), StringHelper::FormatBinary(_beta128status).c_str());
}

/// Helper for Type1 command states to execute VERIFY using unified approach
void  WD1793::type1CommandVerify()
{
    if (_verifySeek)
    {
        // Yes, verification is required

        // Activate head load
        loadHead();

        // Transition to FSM S_VERIFY state after the delay
        transitionFSMWithDelay(WDSTATE::S_VERIFY, WD93_VERIFY_DELAY_MS * TSTATES_PER_MS);
    }
    else
    {
        // No, verification is not required, command execution finished
        endCommand();
    }
}

/// endregion </Command handling>

/// region <State machine handlers>
void WD1793::processIdle()
{
    _statusRegister &= ~WDS_BUSY;   // Remove busy flag
}

void WD1793::processWait()
{
    // Attempt time correction before checks
    if (_delayTStates > 0)
    {
        _delayTStates -= _diffTime;
    }

    // Transition to the next state when delay elapsed
    if (_delayTStates <= 0)
    {
        _delayTStates = 0;
        transitionFSM(_state2);
    }
}

/// Fetch next state from FIFO
void WD1793::processFetchFIFO()
{
    WDSTATE nextState = WDSTATE::S_IDLE;
    if (!_operationFIFO.empty())
    {
        // Get next FIFO value (and remove it from the queue immediately)
        nextState = _operationFIFO.front();
        _operationFIFO.pop();
    }
    else
    {
        MLOGWARNING("WDSTATE::S_FETCH_FIFO state activated but no operations in queue");
    }

    // Transition to next state
    transitionFSM(nextState);
}

void WD1793::processStep()
{
    /// region <Check for step limits>
    if (_stepCounter >= WD93_STEPS_MAX)
    {
        // We've reached limit - seek error
        _statusRegister |= WDS_SEEKERR;
        _beta128status |= INTRQ;

        endCommand();
    }
    else
    {
        _stepCounter++;
    }
    /// endregion </Check for step limits>

    /// region <Make head step>
    int8_t stepCorrection = _stepDirectionIn ? 1 : -1;

    // Apply changes to the WD1793 Track Register
    _trackRegister += stepCorrection;
    /// endregion </Make head step>

    // Check for track 0
    if (!_stepDirectionIn && _selectedDrive->isTrack00())                       // RESTORE command finished
    {
        // We've reach Track 0. No further movements
        _trackRegister = 0;

        // Check if position verification was requested
        type1CommandVerify();
    }
    else if (_lastDecodedCmd == WD_CMD_SEEK && _dataRegister == _trackRegister)   // SEEK command finished
    {
        // Apply track change to selected FDD
        uint8_t driveTrack = _selectedDrive->getTrack();
        driveTrack += stepCorrection;
        _selectedDrive->setTrack(driveTrack);

        // Check if position verification was requested
        type1CommandVerify();
    }
    else
    {
        // Apply track change to selected FDD
        uint8_t driveTrack = _selectedDrive->getTrack();
        driveTrack += stepCorrection;
        _selectedDrive->setTrack(driveTrack);

        if (_lastDecodedCmd == WD_CMD_RESTORE || _lastDecodedCmd == WD_CMD_SEEK)
        {
            // Schedule next step according currently selected stepping motor rate
            transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
        }
        else if (_lastDecodedCmd == WD_CMD_STEP || _lastDecodedCmd == WD_CMD_STEP_IN || _lastDecodedCmd == WD_CMD_STEP_OUT)
        {
            // Check if position verification was requested
            type1CommandVerify();
        }
        else
        {
            int a = a;
            //throw std::logic_error("Only Type 1 commands can have S_STEP state");
        }
    }

    // Debug print
    MLOGINFO(dumpStep().c_str());
}

void WD1793::processVerify()
{
    // If h=0 but V=1 (head not requested to load, but verification requests)
    if (!_headLoaded && _verifySeek)
    {
        // Head must be loaded to read ID Address Mark
        _statusRegister |= WDS_HEADLOADED;
        _headLoaded = true;
    }

    // TODO: implement VERIFY
    // Currently just end command
    endCommand();
}

/// Process SEEK flow (attempting to reach track in Data Register)
void WD1793::processSeek()
{
    if (_dataRegister == _trackRegister)
    {
        // We've reached requested track

        // Check if position verification was requested
        if (_verifySeek)
        {
            // Yes, verification is required
            transitionFSMWithDelay(WDSTATE::S_VERIFY, WD93_VERIFY_DELAY_MS * TSTATES_PER_MS);
        }
        else
        {
            // No, verification is not required, command execution finished
            endCommand();
        }
    }
    else
    {
        // Not on the track requested. Further re-positioning required
        _stepDirectionIn = _dataRegister > _trackRegister;

        // Start re-positioning without additional delays
        transitionFSM(WDSTATE::S_STEP);
    }
}

/// Handles read single byte for sector or track operations
/// _rawDataBuffer and _bytesToRead values must be set before reading the first byte
void WD1793::processReadByte()
{
    if (!_dataRegisterAccessed)
    {
        // Data was not fetched by CPU from Data Register
        // Set LOST_DATA error and terminate
        _statusRegister |= WDS_LOSTDATA;
        endCommand();
    }
    else if (_rawDataBuffer)
    {
        // Reset Data Register access flag
        _dataRegisterAccessed = false;

        // Put next byte read from disk into Data Register
        _dataRegister = *(_rawDataBuffer++);
        _bytesToRead--;

        if (_bytesToRead > 0)
        {
            // Schedule next byte read
            transitionFSMWithDelay(WD1793::S_READ_BYTE, TSTATES_PER_FDC_BYTE);
        }
        else
        {
            // No more bytes to read - finish operation
            endCommand();
        }
    }
    else
    {
        // For some reason data not available - treat it as NOT READY and abort
        _statusRegister |= WDS_NOTRDY;
        endCommand();
    }
}

void WD1793::processWriteByte()
{
    throw new std::logic_error("Not implemented yet");
}

/// endregion </State machine handlers>

/// region <PortDevice interface methods>

uint8_t WD1793::portDeviceInMethod(uint16_t port)
{
    uint8_t result = 0xFF;

    /// region <Debug print>

    uint16_t pc = _context->pCore->GetZ80()->m1_pc;
    std::string memBankName = _context->pMemory->GetCurrentBankName(0);

    MLOGINFO("In port:0x%04X, pc: 0x%04X bank: %s", port, pc, memBankName.c_str());

    /// endregion </Debug print>

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:   // Return status register value
            _beta128status &= ~INTRQ;     // Reset INTRQ (Interrupt request) flag - status register is read

            result = getStatusRegister();

            // TODO: remove debug
            MLOGINFO("%s", dumpStatusRegister(_lastDecodedCmd).c_str());
            break;
        case PORT_3F:   // Return current track number
            result = _trackRegister;
            break;
        case PORT_5F:   // Return current sector number
            result = _sectorRegister;
            break;
        case PORT_7F:   // Return data byte and update internal state
            _beta128status &= ~DRQ;         // Reset DRQ (Data Request) flag
            _dataRegisterAccessed = true;   // Mark that Data Register was accessed (for Type 2 and Type 3 operations)
            result = _dataRegister;
            break;
        case PORT_FF:   // Handle BETA128 system port (#FF)
            result = _beta128status | (_beta128 & 0x3F);
            MLOGINFO("Beta128 status: %s", StringHelper::FormatBinary(result).c_str());
            break;
        default:
            break;
    }

    return result;
}

void WD1793::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    /// region <Debug print>

    uint16_t pc = _context->pCore->GetZ80()->m1_pc;
    std::string memBankName = _context->pMemory->GetCurrentBankName(0);

    MLOGINFO("Out port:0x%04X, value: 0x%02X pc: 0x%04X bank: %s", port, value, pc, memBankName.c_str());

    /// endregion </Debug print>

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:   // Write to Command Register
            _beta128status &= ~INTRQ;     // Reset INTRQ (Interrupt request) flag - command register is written to

            processWD93Command(value);

            //TODO: remove debug
            MLOGINFO(dumpCommand(value).c_str());
            break;
        case PORT_3F:   // Write to Track Register
            _trackRegister = value;

            //TODO: remove debug
            MLOGINFO(StringHelper::Format("#3F - Set track: 0x%02X", _trackRegister).c_str());
            break;
        case PORT_5F:   // Write to Sector Register
            _sectorRegister = value;

            //TODO: remove debug
            MLOGINFO(StringHelper::Format("#5F - Set sector: 0x%02X", _sectorRegister).c_str());
            break;
        case PORT_7F:   // Write to Data Register
            _dataRegister = value;

            // Reset Data Request bit (DRQ) in Beta128 register
            _beta128status &= ~DRQ;

            // Reset Data Request bit in status register (only if Type 2 or Type 3 command was executed)
            if (isType2Command(_lastCmdValue) || isType3Command(_lastCmdValue))
            {
                _statusRegister &= ~WDS_DRQ;
            }

            // Mark that Data Register was accessed (for Type 2 and Type 3 operations)
            _dataRegisterAccessed = true;

            //TODO: remove debug
            MLOGINFO(StringHelper::Format("#7F - Set data: 0x%02X", _dataRegister).c_str());
            break;
        case PORT_FF:   // Write to Beta128 system register
            processBeta128(value);
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
    uint8_t status = _statusRegister;

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
    std::string commandBits = StringHelper::FormatBinary<uint8_t>(value);

    ss << StringHelper::Format("0x%02X: %s. Bits: %s", value, commandName.c_str(), commandBits.c_str()) << std::endl;

    std::string result = ss.str();

    return result;
}

std::string WD1793::dumpStep()
{
    std::stringstream ss;

    std::string direction = _stepDirectionIn ? "In" : "Out";

    ss << "Step" << std::endl;
    ss << StringHelper::Format("WD1793 track: %d", _trackRegister) << std::endl;
    ss << StringHelper::Format("   FDD track: %d", _selectedDrive->getTrack()) << std::endl;
    ss << StringHelper::Format("   Direction: %s", direction.c_str()) << std::endl;
    ss << StringHelper::Format("      Status: %s", StringHelper::FormatBinary(_statusRegister).c_str()) << std::endl;
    ss << StringHelper::Format("     Beta128: %s", StringHelper::FormatBinary(_beta128status).c_str());

    std::string result = ss.str();

    return result;
}
/// endregion </Debug methods>