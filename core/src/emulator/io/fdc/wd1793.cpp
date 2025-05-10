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

    /// region <Create FDD instances

    for (size_t i = 0; i < 4; i++)
    {
        _context->coreState.diskDrives[i] = new FDD(_context);
    }

    /// endregion </Create FDD instances>

    // Set drive A: as default
    _selectedDrive = _context->coreState.diskDrives[0];
}

WD1793::~WD1793()
{
    for (size_t i = 0; i < 4; i++)
    {
        FDD* diskDrive = _context->coreState.diskDrives[i];
        if (diskDrive)
        {
            diskDrive->ejectDisk();
            delete diskDrive;

            _context->coreState.diskDrives[i] = nullptr;
        }

        DiskImage* diskImage = _context->coreState.diskImages[i];
        if (diskImage)
        {
            delete diskImage;

            _context->coreState.diskImages[i] = nullptr;
        }
    }
}

/// endregion </Constructors / destructors>

/// region <Methods>

void WD1793::reset()
{
    // Clear operations FIFO
    std::queue<FSMEvent> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    _state = S_IDLE;
    _state2 = S_IDLE;
    _statusRegister = 0;
    _trackRegister = 0;
    _sectorRegister = 1; // As per datasheet. Sector is set to 1 after the RESTORE command
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
    // Timing synchronization between host and FDC (specific for software emulation)
    processClockTimings();

    // Maintain FDD motor state
    processFDDMotorState();

    // Emulate disk rotation and index strobe changes
    processFDDIndexStrobe();

    // Process counters and timeouts
    // - DRQ (Data Request) serving timeout
    // - RNF (Record Not Found)
    // - Seek error
    // - Index timeout
    processCountersAndTimeouts();

    /// region <HandlerMap as the replacement for lengthy switch()>

    // Get the handler for State1
    FSMHandler handler = _stateHandlerMap[_state];

    if (handler)    // Handler found
    {
        // Call the correspondent handler through the pointer or reference
        (this->*handler)();
    }
    else            // Handler is not available
    {
        MLOGERROR("No handler available for the state %d (%s)", _state, WDSTATEToString(_state).c_str());
    }

    /// endregion </HandlerMap as the replacement for lengthy switch()>
}

/// endregion </Methods>

/// region <Helper methods>

/// Handle Beta128 interface system controller commands
/// @param value
void WD1793::processBeta128(uint8_t value)
{
    // Set active drive, Bits[0,1] (0..3)
    _drive = value & 0b0000'0011;

    // TODO: Select different drive if requested

    // Set side Bit[4] (0..1)
    _sideUp = ~(value >> 4) & 0b0000'0001;

    // Reset Bit[3] (active low)
    bool reset = !(value & 0b0000'0100);

    if (reset)
    {
        // Perform full WD1793 chip reset
        this->reset();

        _statusRegister &= ~WDS_NOTRDY;
        raiseIntrq();

        // Stop FDD motor, reset all related counters
        _selectedDrive->setMotor(false);
        _motorTimeoutTStates = 0;
        _indexPulseCounter = 0;
    }
    else
    {
        uint8_t beta128ChangedBits = _beta128Register ^ value;
        if (beta128ChangedBits & SYS_HLT) // When HLT signal positive edge (from 0 to 1) detected
        {
            // FIXME: index strobes should be set by disk rotation timings, not by HLT / BUSY edges
            if (!(_statusRegister & WDS_BUSY))
            {
                _indexPulseCounter++;
            }
        }

        _beta128Register = value;
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
            raiseIntrq();
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
    static constexpr const size_t DISK_ROTATION_PERIOD_TSTATES = Z80_FREQUENCY / FDD::DISK_REVOLUTIONS_PER_SECOND;

    // For 4ms index strobe and base Z80 frequency 3.5Mhz we're getting 14'000 Z80 clock cycles for 4ms index strobe duration
    static constexpr const size_t INDEX_STROBE_DURATION_IN_TSTATES = TSTATES_PER_MS * FDD::DISK_INDEX_STROBE_DURATION_MS;

    bool diskInserted = _selectedDrive->isDiskInserted();
    bool motorOn = _selectedDrive->getMotor();

    if (diskInserted && motorOn)
    {
        bool lastIndex = _index;

        // Set new state for the INDEX flag based on rotating disk position
        // Note: it is assumed that each disk revolution started with index strobe
        size_t diskRotationPhaseCounter = (_time % DISK_ROTATION_PERIOD_TSTATES);
        if (diskRotationPhaseCounter < INDEX_STROBE_DURATION_IN_TSTATES)
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

/// Process counters and timeouts
/// - DRQ (Data Request) serving timeout
/// - RNF (Record Not Found)
/// - Seek error
/// - Index timeout
void WD1793::processCountersAndTimeouts()
{

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

    MLOGINFO("FDD motor started: %d ms", _context->pCore->GetZ80());
}

/// Stops FDD motor
void WD1793::stopFDDMotor()
{
    _selectedDrive->setMotor(false);

    MLOGINFO("FDD motor stopped\n\n\n");
}

/// Load head
void WD1793::loadHead()
{
    _statusRegister |= WDS_HEADLOADED;
    _extStatus |= SIG_OUT_HLD;
    _headLoaded = true;

    MLOGINFO("> Head loaded");

    // Reset FDD motor stop timer
    prolongFDDMotorRotation();
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

        // Update index strobe state according to rotation timings
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
        uint8_t headStatus = ((_extStatus & SIG_OUT_HLD) && (_beta128Register & 0b0000'1000)) ? WDS_HEADLOADED : 0;
        _statusRegister |= headStatus;
    }
    else
    {
        // Type II or III command specific status register bits
        // Main differences:
        // - Bit 1 means DRQ (Data Request)
        // - Bit 2 means LOST DATA (CPU was unable to read or write the data from data register during 13us waiting period)
        // - Bits 3 to 6 are specific for each command
        // - Bit 7 means NOT READY (FDC is not ready to accept commands)

        // Reset bits according each command requirements from the datasheet
        switch (_lastDecodedCmd)
        {
            case WD_CMD_READ_ADDRESS:
                _statusRegister &= 0b1001'1111; // Reset bit5 and bit6 as stated in the datasheet

                // DRQ bit means that the FDC has placed one byte of the ID field into the Data Register, and the host CPU can read it
                break;
            case WD_CMD_READ_SECTOR:
                _statusRegister &= 0b1011'1111; // Reset bit 6 as stated in the datasheet

                // DRQ bit means that the FDC has fetched the next byte of the sector data and placed it into the Data Register
                break;
            case WD_CMD_WRITE_SECTOR:
                // No bits reset => All bits are used for the Write Sector command

                // DRQ bit indicates that the FDC is ready to accept data from the CPU
                break;
            case WD_CMD_READ_TRACK:
                _statusRegister &= 0b1000'0111; // Reset bits 3 to 6 as stated in the datasheet

                // DRQ bit means that a raw data byte, including gaps and sync marks, is placed into the Data Register
                break;
            case WD_CMD_WRITE_TRACK:
                _statusRegister &= 0b1110'0111; // Reset bit3 and bit4 as stated in the datasheet

                // DRQ bit indicates that the FDC is ready to accept data from the CPU
                break;
            default:
                throw std::logic_error("Unknown FDC command");
                break;
        }

        // NOT READY (bit 7) is driven by FDC state machine
        if (isReady())
        {
            _statusRegister &= ~WDS_NOTRDY;
        }
        else
        {
            _statusRegister |= WDS_NOTRDY;
        }

        // DRQ (bit 1) for all Type II and III commands is driven by an FDC state machine
        // But since beta128 register is used to hold chip DRQ output signal - get it from there
        _statusRegister |= (_beta128status & DRQ);
    }

    // BUSY (bit 0) is driven by an FDC state machine and set directly during command processing

    return _statusRegister;
}

bool WD1793::isReady()
{
    // NOT READY status register bit
    // MR (Master Reset) signal OR inverted drive readiness signal
    bool result = _selectedDrive->isDiskInserted() | ((_beta128Register & BETA128_COMMAND_BITS::BETA_CMD_RESET) == 0);

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

    // We're not sure about the current head position at this moment. Will be determined during step positioning
    _trackRegister = 0xFF;

    // Direction must always be out (towards Track 0)
    _stepDirectionIn = false;

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

    _stepDirectionIn = _dataRegister > _trackRegister;

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> ... -> S_STEP -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
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

    // Yes, we move the head towards the central ring (increasing track number)
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

/// Executes the Read Sector command (Type II command)
///
/// Initiates a sector read operation on the currently selected drive. This command:
/// 1. Searches for the ID Address Mark (IDAM) matching the track/sector registers
/// 2. Reads sector data into the controller's buffer
/// 3. Generates DRQ for each byte and INTRQ upon completion
///
/// @param value Command byte containing control flags:
///             - Bit 3 (m): Multiple sectors (0=single, 1=multi)
///             - Bit 2 (S): Side select (0=side 0, 1=side 1)
///             - Bit 1 (E): 15ms delay (0=no delay, 1=enable)
///             - Bit 0 (C): Side compare (0=disable, 1=enable)
///
/// @throws std::runtime_error If no disk is inserted or sector not found
///
/// @note Command format: 1 0 0 m S E C 0 (binary)
/// @note Actual sector read occurs in the FSM state machine via _operationFIFO
/// @see WD1793 datasheet section 5.2.2 for timing diagrams
///
/// Typical command values:
///   0x80 (10000000b) - Single sector read, side 0
///   0x84 (10000100b) - Single sector read with side compare
///   0xA0 (10100000b) - Multi-sector read, side 1
void WD1793::cmdReadSector(uint8_t value)
{
    std::string message = StringHelper::Format("Command Read Sector: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType2Command();

    // Step 1: search for ID address mark
    /*
    FSMEvent searchIDAM(WDSTATE::S_SEARCH_ID,
                        []() {}
                        );
    _operationFIFO.push(searchIDAM);
    */

    // Step 2: start sector reading (queue correspondent command to the FIFO)
    FSMEvent readSector(WDSTATE::S_READ_SECTOR, [this]()
        {
            // Position to the sector requested
            DiskImage* diskImage = this->_selectedDrive->getDiskImage();
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(this->_trackRegister, this->_sideUp);
            this->_sectorData = track->getDataForSector(this->_sectorRegister - 1);
            this->_rawDataBuffer = this->_sectorData;
        });
    _operationFIFO.push(readSector);

    // Start FSM playback using FIFO queue
    transitionFSM(WDSTATE::S_FETCH_FIFO);
}

void WD1793::cmdWriteSector(uint8_t value)
{
    std::string message = StringHelper::Format("Command Write Sector: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType2Command();

    // Step 1: search for ID address mark
    /*
    FSMEvent searchIDAM(WDSTATE::S_SEARCH_ID,
                        []() {}
                        );
    _operationFIFO.push(searchIDAM);
    */

    // Step 2: start sector writing (queue correspondent command to the FIFO)
    FSMEvent writeSector(WDSTATE::S_WRITE_SECTOR, [this]()
        {
            // Position to the sector requested
            DiskImage* diskImage = this->_selectedDrive->getDiskImage();
            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(this->_trackRegister, this->_sideUp);
            this->_sectorData = track->getDataForSector(this->_sectorRegister - 1);
            this->_rawDataBuffer = this->_sectorData;
        });
    _operationFIFO.push(writeSector);

    // Start FSM playback using FIFO queue
    transitionFSM(WDSTATE::S_FETCH_FIFO);
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
    std::string message = StringHelper::Format("Command Read Address: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType3Command();

    // Step 1: search for ID address mark
    FSMEvent searchIDAM(WDSTATE::S_SEARCH_ID,
                        []() {}
    );
    _operationFIFO.push(searchIDAM);

    // Step 2: start IDAM read (6 bytes)
    FSMEvent readIDAM(WDSTATE::S_READ_BYTE, [this]()
        {
            this->_bytesToRead = 6;
            this->_rawDataBuffer = this->_idamData;
        }
    );
    _operationFIFO.push(readIDAM);

    // Start FSM playback using FIFO queue
    transitionFSM(WDSTATE::S_FETCH_FIFO);
}

void WD1793::cmdReadTrack(uint8_t value)
{
    std::cout << "Command Read Track: " << static_cast<int>(value) << std::endl;

    startType3Command();
}

void WD1793::cmdWriteTrack(uint8_t value)
{
    std::cout << "Command Write Track: " << static_cast<int>(value) << std::endl;

    // Set DRQ immediately as the FDC is ready to receive the first track byte when INDEX PULSE is detected
    // Subsequent bytes will be requested by setting DRQ after each byte transfer
    raiseDrq();

    // Start command execution via FSM
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
    [[maybe_unused]] WDSTATE prevState2 = _state2;

    // Ensure we have only relevant parameter bits
    value &= 0b0000'1111;
    if (value != 0)
    {
        // Handling interrupts in decreasing priority

        if (value & WD_FORCE_INTERRUPT_IMMEDIATE_INTERRUPT) // Bit3 (J3) - Immediate interrupt
        {
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            raiseIntrq();
            clearDrq();
        }

        if (value & WD_FORCE_INTERRUPT_INDEX_PULSE)         // Bit2 (J2) - Every index pulse
        {
            // Not fully implemented
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            raiseIntrq();
            clearDrq();
        }

        if (value & WD_FORCE_INTERRUPT_READY)               // Bit1 (J1) - Ready to Not-Ready transition
        {
            // Not fully implemented
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            raiseIntrq();
            clearDrq();
        }

        if (value & WD_FORCE_INTERRUPT_NOT_READY)           // Bit0 (J0) - Not-Ready to Ready transition
        {
            _state = S_IDLE;
            _state2 = S_IDLE;
            _delayTStates = 0;

            _statusRegister &= ~WDS_BUSY;
            raiseIntrq();
            clearDrq();
        }
    }
    else    // Terminate with no interrupt
    {
        // Currently executed command is terminated and a BUSY flag is reset
        _state = S_IDLE;
        _state2 = S_IDLE;
        _delayTStates = 0;

        _statusRegister &= ~WDS_BUSY;       // Deactivate a busy flag
        clearDrq();
        clearIntrq();
    }

    // Clear operations FIFO
    std::queue<FSMEvent> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    // Set status register according Type 1 command layout
    if (noCommandExecuted)
    {
        _statusRegister &= ~(WDS_CRCERR | WDS_SEEKERR | WDS_HEADLOADED);
        _statusRegister |= _selectedDrive->getTrack() == 0 ? WDS_TRK00 : 0x00;
        _statusRegister |= !_selectedDrive->isDiskInserted() ? WDS_NOTRDY : 0x00;
        _statusRegister |= _selectedDrive->isWriteProtect() ? WDS_WRITEPROTECTED : 0x00;

        MLOGINFO("<<== FORCE_INTERRUPT, no active command status: %s, beta128: %s", StringHelper::FormatBinary(_statusRegister).c_str(), StringHelper::FormatBinary(_beta128status).c_str());
    }
    else
    {
        MLOGINFO("<<== FORCE_INTERRUPT, command interrupted. cmd: %s state: %s state2: %s", getWD_COMMANDName(_lastDecodedCmd), WDSTATEToString(prevState).c_str());
    }
}

void WD1793::startType1Command()
{
    MLOGINFO("==>> Start Type 1 command (%s)", getWD_COMMANDName(_lastDecodedCmd));

    _statusRegister |= WDS_BUSY;                    // Set BUSY flag
    _statusRegister &= ~(WDS_SEEKERR | WDS_CRCERR); // Clear positioning and CRC errors

    // Clear Data Request and Interrupt request bits before starting command processing
    clearDrq();
    clearIntrq();

    // Ensure the motor is spinning
    prolongFDDMotorRotation();

    // Decode stepping motor rate from bits [0..1] (r0r1)
    _steppingMotorRate = getPositioningRateForType1CommandMs(_commandRegister);

    // Determines if VERIFY (check for ID Address Mark) needs to be done after head positioning
    _verifySeek = _commandRegister & CMD_SEEK_VERIFY;

    // Determines if head should be loaded or unloaded during Type1 command
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
    MLOGINFO("==>> Start Type 2 command (%s)", getWD_COMMANDName(_lastDecodedCmd));

    _statusRegister |= WDS_BUSY;                                // Set BUSY flag
    _statusRegister &= ~(WDS_LOSTDATA | WDS_NOTFOUND |          // Reset Type2 error flags
        WDS_RECORDTYPE | WDS_WRITEPROTECTED);
    _DrqServed = false;                              // Type2 commands have timeout for data availability in Data Register

    if (!isReady())
    {
        // If the drive is not ready - end immediately
        endCommand();
    }
    else
    {
        // Ensure the motor is spinning
        prolongFDDMotorRotation();

        // Head must be loaded
        loadHead();

        if (_commandRegister & CMD_DELAY)
        {
            // 30ms @ 1MHz or 15ms @ 2MHz delay requested
        }
        else
        {
            // No delay, so execute the Type2 command immediately
        }
    }
}

void WD1793::startType3Command()
{
    MLOGINFO("==>> Start Type 3 command (%s)", getWD_COMMANDName(_lastDecodedCmd));

    _statusRegister |= WDS_BUSY;                                // Set BUSY flag
    _statusRegister &= ~(WDS_LOSTDATA | WDS_NOTFOUND |          // Reset Type3 error flags
                 WDS_RECORDTYPE);

    if (!isReady())
    {
        // If the drive is not ready - end immediately
        endCommand();
    }
    else
    {
        // Ensure the motor is spinning
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
    raiseIntrq();                   // INTRQ must be set at a completion of any command

    // Clear FIFO
    std::queue<FSMEvent> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    // Transition to IDLE state
    transitionFSM(S_IDLE);

    // Debug logging
    MLOGINFO("<<== End command (%s). status: %s beta128: %s", getWD_COMMANDName(_lastDecodedCmd), StringHelper::FormatBinary(_statusRegister).c_str(), StringHelper::FormatBinary(_beta128status).c_str());
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


/// @brief Processes idle state in the WD1793 controller FSM
/// @details Clears the busy flag in the status register when the controller
///          is in idle state and not executing any commands
void WD1793::processIdle()
{
    _statusRegister &= ~WDS_BUSY;   // Remove busy flag
}


/// @brief Processes wait state in the WD1793 controller FSM
/// @details Handles timing delays in the controller state machine by decreasing the
///          delay counter and transitioning to the next state when the delay has elapsed.
///          This emulates the real hardware timing requirements for operations like
///          head stepping and motor spin-up.
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
    if (!_operationFIFO.empty())
    {
        // Get next FIFO value (and remove it from the queue immediately)
        FSMEvent fsmEvent = _operationFIFO.front();
        _operationFIFO.pop();

        fsmEvent.executeAction();

        WDSTATE nextState = fsmEvent.getState();
        size_t delayTStates = fsmEvent.getDelay();

        // Transition to next state
        if (delayTStates > 0)
        {
            transitionFSMWithDelay(nextState, delayTStates);
        }
        else
        {
            transitionFSM(nextState);
        }
    }
    else
    {
        MLOGWARNING("WDSTATE::S_FETCH_FIFO state activated but no operations in queue");
        endCommand();
    }
}

void WD1793::processStep()
{
    /// region <Check for step limits>
    if (_stepCounter >= WD93_STEPS_MAX)
    {
        // We've reached limit - seek error
        _statusRegister |= WDS_SEEKERR;
        raiseIntrq();

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
    if (!_stepDirectionIn && _selectedDrive->isTrack00())                       // RESTORE or SEEK 0 command finished
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

        // Continue positioning if required
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
            //int a = a;
            //throw std::logic_error("Only Type 1 commands can have S_STEP state");
        }
    }

    // Info print
    MLOGINFO("STEP track: %d, direction: %s", _trackRegister, _stepDirectionIn ? "In" : "Out");

    // Debug print
    MLOGDEBUG(dumpStep().c_str());
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

/// Attempt to find next ID Address Mark on current track
void WD1793::processSearchID()
{
    DiskImage* diskImage = _selectedDrive->getDiskImage();
    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(_trackRegister, _sideUp ? 1 : 0);
    DiskImage::AddressMarkRecord* idAddressMark;

    if (track != nullptr && (idAddressMark = track->getIDForSector(_sectorRegister)) != nullptr)
    {
        // ID Address mark found

        // Get sector size from ID Address Mark
        // 00 - 128 bytes
        // 01 - 256 bytes
        // 10 - 512 bytes
        // 11 - 1024 bytes
        _sectorSize = 128 << (idAddressMark->sector_size & 0x03);

        // Set pointers to Address Mark record and to sector data
        _idamData = (uint8_t*)track->getIDForSector(_sectorRegister);
        _sectorData = track->getDataForSector(_sectorRegister);

        // TODO: apply the delay related to disk rotation so searching for ID Address Mark may take up to a full disk revolution

        // Transition to a next state registered in FIFO queue
        transitionFSM(WDSTATE::S_FETCH_FIFO);
    }
    else
    {
        // ID Address mark not found
        _idamData = nullptr;
        _sectorData = nullptr;
        _rawDataBuffer = nullptr;

        // Set typical timeout delay
        // TODO: apply the delay
        [[maybe_unused]] size_t delay = WD93_REVOLUTIONS_LIMIT_FOR_TYPE2_INDEX_MARK_SEARCH * Z80_FREQUENCY / FDD_RPS;

        _statusRegister |= WDS_NOTFOUND;
    }
}

// Handles read sector operation
void WD1793::processReadSector()
{
    _bytesToRead = _sectorSize;

    // If multiple sectors requested - register a follow-up operation in FIFO
    if (_commandRegister & CMD_MULTIPLE && _sectorRegister < DiskImage::RawTrack::SECTORS_PER_TRACK - 1)
    {
        // Register one more READ_SECTOR operation. Lambda will be executed just before FSM state switch
        FSMEvent readSector(WDSTATE::S_READ_SECTOR, [this]()
            {
                // Increase sector number for reading
                this->_sectorRegister += 1;

                // Re-position to new sector
                DiskImage* diskImage = this->_selectedDrive->getDiskImage();
                if (diskImage)
                {

                    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(this->_trackRegister, this->_sideUp);
                    this->_sectorData = track->getDataForSector(this->_sectorRegister - 1);

                    this->_rawDataBuffer = this->_sectorData;
                    this->_bytesToRead = this->_sectorSize;
                }
                else
                {
                    // Image missing / dismounted
                    // TODO: generate an error, terminate the command
                }
            }
        );

        _operationFIFO.push(readSector);
    }

    // Unblock the first byte read
    _DrqServed = true;

    transitionFSM(WD1793::S_READ_BYTE);
}

/// Handles read single byte for sector or track operations
/// _rawDataBuffer and _bytesToRead values must be set before reading the first byte
void WD1793::processReadByte()
{
    if (!_DrqServed)
    {
        // Data was not fetched by CPU from Data Register
        // Set LOST_DATA error and terminate
        _statusRegister |= WDS_LOSTDATA;
        endCommand();
    }
    else if (_rawDataBuffer)
    {
        // Reset Data Register access flag
        _DrqServed = false;

        // Put next byte read from disk into Data Register
        _dataRegister = *(_rawDataBuffer++);
        _bytesToRead--;

        // Signal to CPU that data byte is available - DRQ
        raiseDrq();

        // Set DRQ bit for Status Register as well. But only for Type 2 and Type 3 commands
        if (!isType1Command(_commandRegister))
        {
            _statusRegister |= WDS_DRQ;
        }

        if (_bytesToRead >= 0)
        {
            // Schedule next byte read
            transitionFSMWithDelay(WD1793::S_READ_BYTE, WD93_TSTATES_PER_FDC_BYTE);
        }
        else
        {
            // No more bytes to read

            // Remove status DRQ
            _statusRegister &= ~WDS_DRQ;
            clearDrq();

            /// region <Set WDS_RECORDTYPE bit depending on Data Address Mark>

            // TODO: fetch real state from DiskImage::Track
            bool isDataMarkDeleted = false;

            // Status bit 5
            // 1 - Deleted Data Mark
            // 0 - Data Mark
            if (isDataMarkDeleted)
            {
                _statusRegister |= WDS_RECORDTYPE;
            }
            else
            {
                _statusRegister &= ~WDS_RECORDTYPE;
            }
            /// endregion </Set WDS_RECORDTYPE bit depending on Data Address Mark>

            // Fetch the next command from fifo (or end if no more commands left)
            transitionFSM(WDSTATE::S_FETCH_FIFO);
        }
    }
    else
    {
        // For some reason data not available - treat it as NOT READY and abort
        _statusRegister |= WDS_NOTRDY;
        endCommand();
    }
}
void WD1793::processWriteSector()
{
    _bytesToWrite = _sectorSize;

    // Request the first byte from the host by raising DRQ
    raiseDrq();
    _statusRegister |= WDS_DRQ;

    // If multiple sectors requested - register a follow-up operation in FIFO
    if (_commandRegister & CMD_MULTIPLE && _sectorRegister < DiskImage::RawTrack::SECTORS_PER_TRACK - 1)
    {
        // Register one more WRITE_SECTOR operation. Lambda will be executed just before FSM state switch
        FSMEvent writeSector(WDSTATE::S_WRITE_SECTOR, [this]()
            {
                // Increase sector number for writing
                this->_sectorRegister += 1;

                // Re-position to new sector
                DiskImage* diskImage = this->_selectedDrive->getDiskImage();
                if (diskImage)
                {
                    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(this->_trackRegister, this->_sideUp);
                    this->_sectorData = track->getDataForSector(this->_sectorRegister - 1);

                    this->_rawDataBuffer = this->_sectorData;
                    this->_bytesToWrite = this->_sectorSize;
                }
                else
                {
                    // Image missing / dismounted
                    // TODO: generate an error, terminate the command
                }
            }
        );

        _operationFIFO.push(writeSector);
    }

    transitionFSM(WD1793::S_WRITE_BYTE);
}

void WD1793::processWriteByte()
{
    if (!_DrqServed)
    {
        // Data was not fetched by CPU from Data Register
        // Set LOST_DATA error and terminate
        _statusRegister |= WDS_LOSTDATA;
        endCommand();
    }
    else if (_rawDataBuffer)
    {
        // Reset Data Register access flag
        _DrqServed = false;

        // Put the next byte to write from the Data Register
        *(_rawDataBuffer++) = _dataRegister;
        _bytesToWrite--;

        if (_bytesToWrite > 0)
        {
            // DRQ is already set from previous state
            // Transition to next byte write state
            transitionFSMWithDelay(WD1793::S_WRITE_BYTE, WD93_TSTATES_PER_FDC_BYTE);
        }
        else
        {
            // No more bytes to read
            // Clear DRQ since DR is no longer empty
            _statusRegister &= ~WDS_DRQ;
            clearDrq();

            /// region <Set WDS_RECORDTYPE bit depending on Data Address Mark>

            // TODO: fetch real state from DiskImage::Track
            bool isDataMarkDeleted = false;

            // Status bit 5
            // 1 - Deleted Data Mark
            // 0 - Data Mark
            if (isDataMarkDeleted)
            {
                _statusRegister |= WDS_RECORDTYPE;
            }
            else
            {
                _statusRegister &= ~WDS_RECORDTYPE;
            }
            /// endregion </Set WDS_RECORDTYPE bit depending on Data Address Mark>

            // Fetch the next command from fifo (or end if no more commands left)
            transitionFSM(WDSTATE::S_FETCH_FIFO);
        }
    }
    else
    {
        // For some reason data not available - treat it as NOT READY and abort
        _statusRegister |= WDS_NOTRDY;
        endCommand();
    }
}

/// endregion </State machine handlers>

/// region <Emulation events>
void WD1793::handleFrameStart()
{
    // Nothing to do here
}

void WD1793::handleStep()
{
    // We need better precision to read data from the disk at 112 t-states per byte rate, so update FSM state after each CPU command execution
    process();
}

void WD1793::handleFrameEnd()
{
    // Perform FSM update at least once per frame (20ms) even if no active I/O with FDC performed
    process();
}
/// endregion </Emulation events>

/// region <PortDevice interface methods>

uint8_t WD1793::portDeviceInMethod(uint16_t port)
{
    uint8_t result = 0xFF;

    /// region <Debug print>

    [[maybe_unused]] uint16_t pc = _context->pCore->GetZ80()->m1_pc;
    std::string memBankName = _context->pMemory->GetCurrentBankName(0);

    //MLOGINFO("In port:0x%04X, pc: 0x%04X bank: %s", port, pc, memBankName.c_str());

    /// endregion </Debug print>

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:   // Return status register value
            result = getStatusRegister();

            // TODO: remove debug
            //MLOGINFO("In #1F - 0x%02X - %s", result, dumpStatusRegister(_lastDecodedCmd).c_str());

            clearIntrq();     // Reset INTRQ (Interrupt request) flag - status register is read
            break;
        case PORT_3F:   // Return current track number
            result = _trackRegister;
            break;
        case PORT_5F:   // Return current sector number
            result = _sectorRegister;
            break;
        case PORT_7F:   // Return data byte and update internal state
            clearDrq();         // Reset DRQ (Data Request) flag
            // Read and mark that Data Register was accessed (for Type 2 and Type 3 operations)
            result = readDataRegister();
            break;
        case PORT_FF:   // Handle Beta128 system port (#FF)
            // Only bits 6 and 7 are used
            result = _beta128status | (_beta128Register & 0x3F);
            //MLOGINFO("In #FF Beta128: %s pc: 0x%04X bank: %s", StringHelper::FormatBinary(result).c_str(), pc, memBankName.c_str());
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
            //TODO: remove debug
            /// region <Debug logging>
            {
                WD_COMMANDS cmd = decodeWD93Command(value);
                const char *cmdName = getWD_COMMANDName(cmd);
                MLOGINFO(StringHelper::Format("  #1F - Set command: 0x%02X (%s). status: %s, beta128: %s",
                                              value,
                                              cmdName,
                                              StringHelper::FormatBinary(_statusRegister).c_str(),
                                              StringHelper::FormatBinary(_beta128status).c_str()
                                              ).c_str());
            }
            /// endregion </Debug logging>

            // Reset INTRQ (Interrupt request) flag - command register is written to
            clearIntrq();

            // Process the command
            processWD93Command(value);

            break;
        case PORT_3F:   // Write to Track Register
            _trackRegister = value;

            //TODO: remove debug
            if (_trackRegister >= MAX_CYLINDERS)
            {
                //_trackRegister = _trackRegister;

                _trackRegister = 79;
            }
            MLOGINFO(StringHelper::Format("  #3F - Set track: 0x%02X", _trackRegister).c_str());
            break;
        case PORT_5F:   // Write to Sector Register
            _sectorRegister = value;

            //TODO: remove debug
            if (_sectorRegister == 0 || _sectorRegister > 16)
            {
                //_sectorRegister = _sectorRegister;

                _sectorRegister = 16;
            }
            MLOGINFO(StringHelper::Format("  #5F - Set sector: 0x%02X", _sectorRegister).c_str());
            break;
        case PORT_7F:   // Write to Data Register
            // Write and mark that Data Register was accessed (for Type 2 and Type 3 operations)
            writeDataRegister(value);

            // Mark DRQ as served since CPU has written to the data register
            _DrqServed = true;

            // Only clear DRQ if we're not in the middle of a sector write
            if (_state != WDSTATE::S_WRITE_BYTE)
            {
                // Reset Data Request bit (DRQ) in Beta128 register
                clearDrq();

                // Reset Data Request bit in status register (only if Type 2 or Type 3 command was executed)
                if (isType2Command(_lastCmdValue) || isType3Command(_lastCmdValue))
                {
                    _statusRegister &= ~WDS_DRQ;
                }
            }

            //TODO: remove debug
            MLOGINFO(StringHelper::Format("  #7F - Set data: 0x%02X", _dataRegister).c_str());
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
        // FORCE INTERRUPT doesn't have its own status bits. Bits from the previous / ongoing command to be shown instead
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
    [[maybe_unused]] uint8_t commandValue = getWD93CommandValue(command, value);
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