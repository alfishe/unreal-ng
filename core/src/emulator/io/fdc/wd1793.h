#pragma once

#include "stdafx.h"

#include <common/stringhelper.h>
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "emulator/cpu/core.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/io/fdc/fdc.h"
#include "emulator/io/fdc/fdd.h"

class WD1793 : public PortDecoder, public PortDevice
{
    /// region <Types>
public:
    /// WD1793 / VG93 commands
    enum WD_COMMANDS : uint8_t
    {
        WD_CMD_RESTORE = 0,     // Restore         - Move the read/write head to the outermost track (track 0)
        WD_CMD_SEEK,            // Seek            - Move the read/write head to a specified track on the floppy disk
        WD_CMD_STEP,            // Step            - Moves the read/write head in the direction previously specified (inwards or outwards) by the "step in" or "step out" command.
        WD_CMD_STEP_IN,         // Step In         - Moves the read/write head one track towards the center of the disk (increase track number)
        WD_CMD_STEP_OUT,        // Step Out        - Moves the read/write head one track away from the center of the disk (decrease track number)

        WD_CMD_READ_SECTOR,     // Read Sector     - Read a single sector from the current track
        WD_CMD_WRITE_SECTOR,    // Write Sector    - Write data to a specified sector on the current track

        WD_CMD_READ_ADDRESS,    // Read Address    - Reads the address field (track number, side number, sector number) of the current sector
        WD_CMD_READ_TRACK,      // Read Track      - Read the entire contents of a track into the FDC's internal buffer
        WD_CMD_WRITE_TRACK,     // Write Track     - Write an entire track worth of data from the FDC's internal buffer to the floppy disk

        WD_CMD_FORCE_INTERRUPT  // Force Interrupt - Forces an interrupt to occur, regardless of the current state of the FDC
    };

    inline static const char* const getWD_COMMANDName(WD_COMMANDS command)
    {
        static const char* const names[] =
        {
            "Restore",          // [ 0] Restore
            "Seek",             // [ 1] Seek
            "Step",             // [ 2] Step
            "Step In",          // [ 3] Step In
            "Step Out",         // [ 4] Step Out
            "Read Sector",      // [ 5] Read Sector
            "Write Sector",     // [ 6] Write Sector
            "Read Address",     // [ 7] Read Address
            "Read Track",       // [ 8] Read Track
            "Write Track",      // [ 9] Write Track
            "Force Interrupt"   // [10] Force Interrupt
        };

        return names[command];
    }

    /// WD1793 / VG93 state machine states
    enum WDSTATE : uint8_t
    {
        S_IDLE = 0,
        S_WAIT,         // Dedicated state to handle timing delays

        S_STEP,
        S_VERIFY,
        S_SEEK,

        S_DELAY_BEFORE_CMD,
        S_CMD_RW,
        S_FOUND_NEXT_ID,
        S_RDSEC,
        S_READ,
        S_WRSEC,
        S_WRITE,
        S_WRTRACK,
        S_WR_TRACK_DATA,

        S_TYPE1_CMD,

        S_SEEKSTART,
        S_RESTORE,
        S_VERIFY2,

        S_WAIT_HLT,
        S_WAIT_HLT_RW,

        S_EJECT1,
        S_EJECT2,

        WDSTATE_MAX
    };
    static std::string WDSTATEToString(WDSTATE state)
    {
        std::string result;

        static constexpr const char* const names[] =
        {
            "S_IDLE",
            "S_WAIT",

            "S_STEP",
            "S_VERIFY",
            "S_SEEK",

            "S_DELAY_BEFORE_CMD",
            "S_CMD_RW",
            "S_FOUND_NEXT_ID",
            "S_RDSEC",
            "S_READ",
            "S_WRSEC",
            "S_WRITE",
            "S_WRTRACK",
            "S_WR_TRACK_DATA",

            "S_TYPE1_CMD",

            "S_SEEKSTART",
            "S_RESTORE",
            "S_VERIFY2",

            "S_WAIT_HLT",
            "S_WAIT_HLT_RW",

            "S_EJECT1",
            "S_EJECT2"
        };

        if (state <= sizeof(names)/sizeof(names[0]))
        {
            result = names[state];
        }
        else
        {
            result = "<Unknown state>";
        }

        return result;
    }

    enum WD93_CMD_BITS : uint8_t
    {
        CMD_SEEK_RATE     = 0x03,
        CMD_SEEK_VERIFY   = 0x04,
        CMD_SEEK_HEADLOAD = 0x08,
        CMD_SEEK_TRKUPD   = 0x10,
        CMD_SEEK_DIR      = 0x20,

        CMD_WRITE_DEL     = 0x01,
        CMD_SIDE_CMP_FLAG = 0x02,
        CMD_DELAY         = 0x04,
        CMD_SIDE          = 0x08,
        CMD_SIDE_SHIFT    = 3,
        CMD_MULTIPLE      = 0x10
    };

    // Force Interrupt command parameter bits
    enum WD_FORCE_INTERRUPT_BITS : uint8_t
    {
        WD_FORCE_INTERRUPT_NOT_READY            = 0x01,
        WD_FORCE_INTERRUPT_READY                = 0x02,
        WD_FORCE_INTERRUPT_INDEX_PULSE          = 0x04,
        WD_FORCE_INTERRUPT_IMMEDIATE_INTERRUPT  = 0x08
    };

    enum BETA_COMMAND_BITS : uint8_t
    {
        BETA_CMD_DRIVE_MASK = 0b0000'0011,  // Bits[0,1] define drive selection. 00 - A, 01 - B, 10 - C, 11 - D

        BETA_CMD_RESET      = 0b0000'0100,  // Bit2 (active low) allows to reset BDI and WD73 controller. Similar to RESTORE command execution for the application
        // HLT - Head Load Timing is an input signal used to determine head engagement time.
        // When HLT = 1, FDC assumes that head is completely engaged. Usually it takes 30-100ms for FDD to react on HLD signal from FDC and engage the head
        BETA_CMD_BLOCK_HLT  = 0b0000'1000,  // Bit3 (active low) blocks HLT signal. Normally it should be inactive (high).
        BETA_CMD_HEAD       = 0b0001'0000,  // Bit4 - select head / side. 0 - lower side head. 1 - upper side head
        BETA_CMD_RESERVED5  = 0b0010'0000,  // Bit5 - Unused
        BETA_CMD_DENSITY    = 0b0100'0000,  // Bit6 - 0 - Double density / MFM, 1 - Single density / FM
        BETA_CMD_RESERVED7  = 0b1000'0000,  // Bit7 - Unused
    };

    /// FDC status (corresponds to port #1F read)
    // ╔═════════╤══════════════════════════════════════════════════════════════════╗
    // ║   Bit   │                            Commands                              ║
    // ║         │  Command Type 1 │ Write  │  Read   │  Read   │ Write   │   Read  ║
    // ║         │ (Recover & Seek)│ Sector │ Sector  │ Address │ Track   │  Track  ║
    // ╟─────────┼─────────────────┼────────┼─────────┼─────────┼─────────┼─────────╢
    // ║    7    │ NOT READY - Drive readiness ( 1 - not ready; 0 - ready           ║
    // ╟─────────┼─────────────────┼────────┼─────────┼─────────┼─────────┼─────────╢
    // ║    6    │ WRITE PROTECT   │    0   │    0    │    0    │  WRITE  │  WRITE  ║
    // ║         │                 │        │         │         │ PROTECT │ PROTECT ║
    // ╟─────────┼─────────────────┼────────┼─────────┼─────────┼─────────┼─────────╢
    // ║    5    │ HEAD LOADED     │    0   │  0  │  1  │  u  │  h  │  V  │  r1 │  r0 ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    4    │ SEEK ERROR      │  0  │  1  │  0  │  u  │  h  │  V  │  r1 │  r0 ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    3    │ CRC ERROR       │  0  │  1  │  1  │  u  │  h  │  V  │  r1 │  r0 ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    2    │ TRACK 0         │  1  │  0  │  0  │  m  │  s  │  E  │  C  │  0  ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    1    │ INDEX           │  1  │  0  │  1  │  m  │  s  │  E  │  C  │  a0 ║
    // ╟─────────┼─────────────────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────╢
    // ║    0    │ BUSY            │  1  │  1  │  0  │  0  │  0  │  E  │  0  │  0  ║
    // ╚═════════╧═════════════════╧═════╧═════╧═════╧═════╧═════╧═════╧═════╧═════╝
    enum WD_STATUS : uint8_t
    {
        WDS_BUSY           = 0x01,   // For all commands
        WDS_INDEX          = 0x02,   // For Type 1 (Restore & Seek) commands only
        WDS_DRQ            = 0x02,   // For all read/write commands only
        WDS_TRK00          = 0x04,   // For Type 1 (Restore & Seek) commands only
        WDS_LOST           = 0x04,   // For all read/write commands only
        WDS_CRCERR         = 0x08,   // For Type 1 (Restore & Seek) commands + READ ADDRESS + READ SECTOR + WRITE SECTOR
        WDS_NOTFOUND       = 0x10,   // RNF (Record Not Found) - For READ ADDRESS + READ SECTOR + WRITE SECTOR
        WDS_SEEKERR        = 0x10,   // For Type 1 (Restore & Seek) commands only

        // The data mark code is a byte value that is read from the disk and helps identify the sector's characteristics.
        // Data mark code for the READ_SECTOR operation is typically:
        // - 0xFB (binary 11111011) or
        // - 0xF8 (binary 11111000)
        WDS_RECORDTYPE     = 0x20,   // For READ SECTOR command only. Indicates record type code from data field address mark. 1 - Deleted Data Mark; 0 - Data Mark
        WDS_HEADLOADED     = 0x20,
        WDS_WRITEFAULT     = 0x20,
        WDS_WRITEPROTECTED = 0x40,   // Disk is write-protected
        WDS_NOTRDY         = 0x80    // Drive is not ready
    };

    enum BETA_STATUS_BITS : uint8_t
    {
        DRQ   = 0x40,   // Bit6 - Indicates (active low) that Data Register(DR) contains assembled data in Read operations or empty in Write operations

        /// INTRQ = 0 - Command complete
        /// INTRQ = 1 - Command in progress
        INTRQ = 0x80    // Bit7 - Set (active low) at the completion of any command and is reset when the STATUS register is read or the command register os written to
    };

    enum WD_SYS : uint8_t
    {
        SYS_HLT       = 0x08
    };

    /// Describes WD1793 / VG93 output pin signals
    enum WD_SIG : uint8_t
    {
        /// Head LoaD (HLD) output controls the loading of the Read-Write head against the media
        SIG_OUT_HLD = 0x01
    };

    using CommandHandler = void (WD1793::*)(uint8_t);
    using FSMHandler = void (WD1793::*)();

    /// endregion </Types>

    /// region <Constants>
protected:
    static constexpr const size_t Z80_FREQUENCY = 3.5 * 1'000'000;
    static constexpr const size_t Z80_CLK_CYCLES_PER_MS = Z80_FREQUENCY / 1000;
    static constexpr const double Z80_CLK_CYCLES_PER_US = (double)Z80_FREQUENCY / 1'000'000.0;
    static constexpr const size_t TSTATES_PER_MS = Z80_FREQUENCY / 1000;

    static constexpr const size_t WD93_FREQUENCY = 1'000'000;
    static constexpr const double WD93_CLK_CYCLES_PER_Z80_CLK = Z80_FREQUENCY / WD93_FREQUENCY;

    /// Time limit to retrieve single byte from WD1793
    /// We must read the whole track during single disk spin (200ms), so we have just 114 t-states per byte
    static constexpr const size_t T_STATES_PER_FDC_BYTE = Z80_FREQUENCY / (MAX_TRACK_LEN * FDD_RPS);

    static constexpr const size_t WD93_REVOLUTIONS_TILL_MOTOR_STOP = 15;
    static constexpr const size_t WD93_TSTATES_TILL_MOTOR_STOP = Z80_FREQUENCY * WD93_REVOLUTIONS_TILL_MOTOR_STOP / FDD_RPS;
    static constexpr const size_t WD93_REVOLUTIONS_LIMIT_FOR_INDEX_MARK_SEARCH = 5;
    static constexpr const size_t WD93_TSTATES_LIMIT_FOR_INDEX_MARK_SEARCH = Z80_FREQUENCY * WD93_REVOLUTIONS_LIMIT_FOR_INDEX_MARK_SEARCH /  FDD_RPS;

    /// We can do no more than 255 head steps. Normally it cannot be more than 80-83 track positioning steps. If we reached 255 limit - FDD is broken
    static constexpr const size_t WD93_STEPS_MAX = 255;

    /// After the last directional step an additional 15 milliseconds of head settling time takes place if the Verify flag is set in Type I commands
    static constexpr const size_t WD93_VERIFY_DELAY_MS = 15;

    static constexpr const size_t WD93_COMMAND_COUNT = 11;

    // Decoded port addresses (physical address line matching done in platform port decoder)
    static constexpr const uint16_t PORT_1F = 0x001F;     // Write - command register; Read - state
    static constexpr const uint16_t PORT_3F = 0x003F;     // Track register
    static constexpr const uint16_t PORT_5F = 0x005F;     // Sector register
    static constexpr const uint16_t PORT_7F = 0x007F;     // Data register
    static constexpr const uint16_t PORT_FF = 0x00FF;     // Write - BETA128 system controller; Read - FDC readiness (Bit6 - DRQ, bit7 - INTRQ)
    static constexpr const uint16_t PORT_7FFD = 0x7FFD;   // DOS lock mode. Bit4 = 0 - block; Bit4 = 1 - allow

    // Stepping rates from WD93 datasheet
    static constexpr const uint8_t  STEP_TIMINGS_MS_1MHZ[] = { 6, 12, 20, 30 };
    static constexpr const uint8_t STEP_TIMINGS_MS_2MHZ[] = { 3, 6, 10, 15 };

    /// endregion </Constants>

    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISK;
    const uint16_t _SUBMODULE = PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    PortDecoder* _portDecoder = nullptr;
    bool _chipAttachedToPortDecoder = false;

    FDD* _selectedDrive = nullptr;

    // Counters to measure time intervals
    size_t _time = 0;                   // Current time mark (in Z80 t-states)
    size_t _lastTime = 0;               // Time mark when process() was called last time
    int64_t _diffTime = 0;              // Difference between _time and _lastTime (_time - _lastTime)
    int64_t _delayTStates = 0;          // Delay between switching to next state

    // WD93 internal state
    uint8_t _commandRegister = 0x00;    // Last command executed (full data byte)
    uint8_t _trackRegister = 0;
    uint8_t _sectorRegister = 0;
    uint8_t _dataRegister = 0x00;       // WD1793 Data Register
    uint8_t _statusRegister = 0x00;     // WD1793 Status Register

    uint8_t _beta128 = 0x00;            // BETA128 system register
    uint8_t _beta128status = 0x00;      // BETA128 status output: Data request (DRQ) and interrupt request (INTRQ) flags
    uint8_t _extStatus = 0x00;          // External status. Only HLD flag is supported

    // Beta128 state
    uint8_t _drive = 0;                 // Currently selected drive index [0..3]
    bool _sideUp = false;               // False - bottom side. True - top side

    WD_COMMANDS _lastDecodedCmd;        // Last command executed (decoded)
    uint8_t _lastCmdValue = 0x00;       // Last command parameters (already masked)
    WDSTATE _state = S_IDLE;
    WDSTATE _state2 = S_IDLE;

    // Type 1 command params
    bool _loadHead = false;             // Determines if load should be loaded or unloaded during Type1 command
    bool _verifySeek = false;           // Determines if VERIFY (check for ID Address Mark) needs to be done after head positioning
    uint8_t _steppingMotorRate = 6;     // Positioning speed rate. Value is resolved from Type1 command via STEP_TIMINGS_MS_1MHZ and STEP_TIMINGS_MS_2MHZ arrays depending on WD1793 clock speed

    // Type 2 command params
    bool _dataRegisterWritten = false;  // Type2 commands have timeout for data availability in Data Register


    // Internal state
    int8_t _stepDirectionIn = false;    // Head step direction. True - move head towards center cut (Step In). False - move outwards to Track 0 (Step Out)
    size_t _stepCounter = 0;            // Count each head positioning step during current Type 1 command

    // FDD state
    bool _index = false;                // Current state of index strobe
    size_t _indexPulseCounter = 0;      // Index pulses counter
    size_t _rotationCounter = 0;        // Tracks disk rotation
    int64_t _motorTimeoutTStates = 0;   // 0 - motor already stopped. >0 - how many ticks left till auto-stop (timeout is 15 disk revolutions)

    /// endregion </Fields>

    /// region <Properties>
public:
    FDD* getDrive() { return _selectedDrive; }
    void setDrive(FDD* drive) { _selectedDrive = drive; }
    /// endregion </Properties>

    /// region <Constructors / destructors>
public:
    WD1793(EmulatorContext* context);
    virtual ~WD1793();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    virtual void reset();
    void process();
    /// endregion </Methods>

    /// region <Helper methods>
protected:
    void processBeta128(uint8_t value);
    void processFDDMotorState();
    void processFDDIndexStrobe();
    void prolongFDDMotorRotation();
    void startFDDMotor();
    void stopFDDMotor();
    uint8_t getStatusRegister();
    bool isReady();
    /// endregion </Helper methods>

    /// region <Command handling
protected:
    static bool isType1Command(uint8_t command);
    static bool isType2Command(uint8_t command);
    static bool isType3Command(uint8_t command);
    static bool isType4Command(uint8_t command);
    static WD_COMMANDS decodeWD93Command(uint8_t value);
    static uint8_t getWD93CommandValue(WD1793::WD_COMMANDS command, uint8_t value);
    void processWD93Command(uint8_t value);

    uint8_t getPositioningRateForType1CommandMs(uint8_t command);

    // WD93 Command handlers
    void cmdRestore(uint8_t value);
    void cmdSeek(uint8_t value);
    void cmdStep(uint8_t value);
    void cmdStepIn(uint8_t value);
    void cmdStepOut(uint8_t value);
    void cmdReadSector(uint8_t value);
    void cmdWriteSector(uint8_t value);
    void cmdReadAddress(uint8_t value);
    void cmdReadTrack(uint8_t value);
    void cmdWriteTrack(uint8_t value);
    void cmdForceInterrupt(uint8_t value);

    // Command group related
    void startType1Command();
    void startType2Command();
    void startType3Command();
    void endCommand();

    void type1CommandVerify();


    /// endregion </Command handling>

    /// region <State machine handlers>
protected:
    std::map<WDSTATE, FSMHandler> _stateHandlerMap =
    {
        { S_IDLE,   &WD1793::processIdle },
        { S_WAIT,   &WD1793::processWait },
        { S_STEP,   &WD1793::processStep },
        { S_VERIFY, &WD1793::processVerify },
        { S_SEEK,   &WD1793::processSeek },
    };

    void processIdle();
    void processWait();
    void processStep();
    void processVerify();
    void processSeek();

    void transitionFSM(WDSTATE nextState)
    {
        /// region <Debug logging>
        std::string timeMark = StringHelper::Format("  [%d | %d ms]", _time, convertTStatesToMs(_time));
        std::string message = StringHelper::Format("  %s -> %s <nodelay> %s", WDSTATEToString(_state).c_str(), WDSTATEToString(nextState).c_str(), timeMark.c_str());
        MLOGINFO(message.c_str());
        /// endregion </Debug logging>

        _state = nextState;
        _state2 = WDSTATE::S_IDLE;
    }

    void transitionFSMWithDelay(WDSTATE nextState, size_t delayTStates)
    {
        std::string delayNote = StringHelper::Format(" delay(%d | %d ms)", delayTStates, convertTStatesToMs(delayTStates));
        std::string message = StringHelper::Format("%s -> %s %s", WDSTATEToString(_state).c_str(), WDSTATEToString(nextState).c_str(), delayNote.c_str());
        MLOGINFO(message.c_str());

        _state2 = nextState;
        _delayTStates = delayTStates - 1;
        _state = WDSTATE::S_WAIT;  // Keep FSM in wait state until delay time elapsed
    }

    void processClockTimings()
    {
        // Decouple time sync with emulator state. Unit tests override updateTimeFromEmulatorState(); as dummy stub
        updateTimeFromEmulatorState();

        _diffTime = std::abs((int64_t)(_time - _lastTime));

        // If we're waiting more than 15 disk revolutions - something goes wrong on Z80 side,
        // and we need to recover into
        if (_diffTime > Z80_FREQUENCY * 15 / FDD_RPS)
        {
            // TODO: handle the case
            //throw std::logic_error("Not implemented yet");
        }

        // Update last call time
        _lastTime = _time;
    }

    /// Get current time mark from emulator state
    virtual void updateTimeFromEmulatorState()
    {
        uint64_t totalTime = _context->emulatorState.t_states;
        uint64_t frameTime = _context->pCore->GetZ80()->t;
        _time = totalTime + frameTime;
    }

    /// Reset internal time marks
    void resetTime()
    {
        _time = 0;
        _lastTime = 0;
        _diffTime = 0;
    };

    /// endregion </State machine handlers>

    /// region <PortDevice interface methods>
public:
    uint8_t portDeviceInMethod(uint16_t port);
    void portDeviceOutMethod(uint16_t port, uint8_t value);
    /// endregion </PortDevice interface methods>

    /// region <Ports interaction>
public:
    bool attachToPorts();
    void detachFromPorts();
    /// endregion </Ports interaction>

    /// region <Debug methods>
public:
    std::string dumpStatusRegister(WD_COMMANDS command);
    std::string dumpCommand(uint8_t value);
    std::string dumpStep();

    static inline size_t convertTStatesToMs(size_t tStates)
    {
        size_t result = tStates / TSTATES_PER_MS;

        return result;
    }
    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class WD1793CUT : public WD1793
{
public:
    WD1793CUT(EmulatorContext* context) : WD1793(context) {};

    using WD1793::_selectedDrive;

    using WD1793::_commandRegister;
    using WD1793::_lastDecodedCmd;
    using WD1793::_lastCmdValue;

    using WD1793::_trackRegister;
    using WD1793::_sectorRegister;
    using WD1793::_dataRegister;
    using WD1793::_statusRegister;

    using WD1793::_state;
    using WD1793::_state2;
    using WD1793::_delayTStates;

    using WD1793::_time;
    using WD1793::_lastTime;
    using WD1793::_diffTime;

    using WD1793::_stepDirectionIn;
    using WD1793::_stepCounter;
    using WD1793::_steppingMotorRate;
    using WD1793::_motorTimeoutTStates;

    using WD1793::_index;
    using WD1793::_indexPulseCounter;

    virtual void reset() override // Override default implementation for testing purposes, do not run RESTORE command at the end
    {
        _state = S_IDLE;
        _statusRegister = 0;
        _trackRegister = 0;
        _sectorRegister = 0;
        _dataRegister = 0;
    }

    using WD1793::isType1Command;
    using WD1793::isType2Command;
    using WD1793::isType3Command;
    using WD1793::isType4Command;
    using WD1793::decodeWD93Command;
    using WD1793::getWD93CommandValue;
    using WD1793::processWD93Command;

    using WD1793::cmdRestore;
    using WD1793::cmdSeek;
    using WD1793::cmdStep;
    using WD1793::cmdStepIn;
    using WD1793::cmdStepOut;
    using WD1793::cmdReadSector;
    using WD1793::cmdWriteSector;
    using WD1793::cmdReadAddress;
    using WD1793::cmdReadTrack;
    using WD1793::cmdWriteTrack;
    using WD1793::cmdForceInterrupt;

    using WD1793::processIdle;
    using WD1793::processWait;
    using WD1793::processStep;
    using WD1793::processVerify;

    using WD1793::transitionFSMWithDelay;
    using WD1793::processClockTimings;
    virtual void updateTimeFromEmulatorState() override {}; // Make it dummy stub and skip reading T-State counters from the emulator
    using WD1793::resetTime;
    using WD1793::prolongFDDMotorRotation;
    using WD1793::startFDDMotor;
    using WD1793::stopFDDMotor;
};

#endif // _CODE_UNDER_TEST