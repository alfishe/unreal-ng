#pragma once

#include "stdafx.h"

#include <algorithm>
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/io/fdc/fdc.h"
#include "emulator/io/fdc/fdd.h"

/// @see https://www.retrotechnology.com/herbs_stuff/WD179X.PDF
/// @see https://zxpress.ru/book_articles.php?id=1356
/// Track 0 is the most outer track of the floppy disk
class VG93 : public PortDecoder, public PortDevice
{
    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISK;
    const uint16_t _SUBMODULE = PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Types>
public:
    enum WD93_REGISTERS : uint8_t
    {
        REG_COMMAND = 0,    // COMMAND/STATUS register (port #1F)
        REG_TRACK,          // TRACK register (port #3F)
        REG_SECTOR,         // SECTOR register (port #5F)
        REG_DATA,           // DATA register (port #7F)
        REG_SYSTEM          // BETA128/System register (port #FF)
    };

    /// WD92 / VG93 state machine states
    enum WDSTATE : uint8_t
    {
        S_IDLE = 0,
        S_WAIT,

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
        S_STEP,
        S_SEEKSTART,
        S_RESTORE,
        S_SEEK,
        S_VERIFY,
        S_VERIFY2,

        S_WAIT_HLT,
        S_WAIT_HLT_RW,

        S_EJECT1,
        S_EJECT2
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

    /// WD93 / VG93 commands
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

    using CommandHandler = void (VG93::*)(uint8_t);

    /// endregion </Types>

    /// region <Constants>
protected:
    static constexpr const size_t Z80_FREQUENCY = 3.5 * 1'000'000;
    static constexpr const size_t Z80_CLK_CYCLES_PER_MS = Z80_FREQUENCY / 1000;
    static constexpr const double Z80_CLK_CYCLES_PER_US = (double)Z80_FREQUENCY / 1'000'000.0;
    static constexpr const size_t WD93_FREQUENCY = 1'000'000;
    static constexpr const double WD93_CLK_CYCLES_PER_Z80_CLK = Z80_FREQUENCY / WD93_FREQUENCY;
    static constexpr const size_t T_STATES_PER_BYTE = Z80_FREQUENCY / (MAX_TRACK_LEN * FDD_RPS);    // We must read the whole track during single disk spin (200ms)

    static constexpr const size_t WD93_COMMAND_COUNT = 11;

    // Decoded port addresses (physical address line matching done in platform port decoder)
    static constexpr const uint16_t PORT_1F = 0x001F;     // Write - command register; Read - state
    static constexpr const uint16_t PORT_3F = 0x003F;     // Track register
    static constexpr const uint16_t PORT_5F = 0x005F;     // Sector register
    static constexpr const uint16_t PORT_7F = 0x007F;     // Data register
    static constexpr const uint16_t PORT_FF = 0x00FF;     // Write - BETA128 system controller; Read - FDC readiness (Bit6 - DRQ, bit7 - INTRQ)
    static constexpr const uint16_t PORT_7FFD = 0x7FFD;   // DOS lock mode. Bit4 = 0 - block; Bit4 = 1 - allow

    // Stepping rates from WD93 datasheet
    static constexpr uint8_t const STEP_TIMINGS_1MHZ[] = { 6, 12, 20, 30 };
    static constexpr uint8_t const STEP_TIMINGS_2MHZ[] = { 3, 6, 10, 15 };


    /// endregion </Constants>

    /// region <Fields>
public:
    PortDecoder* _portDecoder = nullptr;
    bool _chipAttachedToPortDecoder = false;

    FDD* _selectedDrive = nullptr;

    bool _ejectPending = false;                 // Disk is ejecting. FDC is already locked

    // WD93 internal state machine
    WDSTATE _state;
    WDSTATE _state2;

    // Counters to measure time intervals
    size_t _next;
    size_t _time;

    // Notify host system
    uint8_t _drive;
    uint8_t _side;

    // Controller state
    uint8_t _lastCmd;                               // Last command executed (full data byte)
    WD_COMMANDS _lastDecodedCmd;                    // Last command executed (decoded)
    uint8_t _lastCmdValue;                          // Last command parameters (already masked)
    uint8_t _data;
    uint8_t _track;
    uint8_t _sector;
    uint8_t _rqs;
    uint8_t _status;
    uint8_t _extStatus;                             // External status. Only HLD is supported

    int16_t _stepDirection = 1;                     // Head movement direction
    uint8_t _beta128 = 0x00;                        // BETA128 system register


    uint8_t _type1CmdStatus = 0;
    uint8_t _type2CmdStatus = 0;
    uint8_t _type3CmdStatus = 0;
    bool _index = false;                            // Current state of index strobe
    size_t _indexPulseCounter = 0;                  // Index pulses counter
    size_t _rotationCounter = 0;                    // Tracks disk rotation

    uint16_t _trackCRC = 0x0000;                    // Track CRC (used during formatting)

    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    VG93(EmulatorContext* context);
    virtual ~VG93();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void reset();
    void eject(uint8_t drive);

protected:
    void process();
    void processBeta128(uint8_t value);
    void findMarker();
    void processIndexStrobe();
    void seekInDiskImage();

    static WD_COMMANDS decodeWD93Command(uint8_t value);
    static uint8_t getWD93CommandValue(VG93::WD_COMMANDS command, uint8_t value);
    void processWD93Command(uint8_t value);
    void updateStatusesForReadWrite();
    void updateStatusesForSeek(uint8_t maskedValue);

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

    /// endregion </Methods>

    /// region <Helper methods>
protected:
    uint8_t getStatusRegister();
    bool isReady();
    /// endregion </Helper methods>

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
    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing / benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class VG93CUT : public VG93
{
public:
    VG93CUT(EmulatorContext* context) : VG93(context) {};

    using VG93::decodeWD93Command;
    using VG93::getWD93CommandValue;
    using VG93::processWD93Command;

    using VG93::cmdRestore;
    using VG93::cmdSeek;
    using VG93::cmdStep;
    using VG93::cmdStepIn;
    using VG93::cmdStepOut;
    using VG93::cmdReadSector;
    using VG93::cmdWriteSector;
    using VG93::cmdReadAddress;
    using VG93::cmdReadTrack;
    using VG93::cmdWriteTrack;
    using VG93::cmdForceInterrupt;
};

#endif // _CODE_UNDER_TEST