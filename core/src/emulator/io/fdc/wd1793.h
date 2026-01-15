#pragma once

#include <common/stringhelper.h>

#include <queue>
#include <vector>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/fdc.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/io/fdc/wd1793state.h"
#include "emulator/platform.h"
#include "emulator/ports/portdecoder.h"
#include "stdafx.h"

class WD1793Collector;

class WD1793 : public PortDecoder, public PortDevice
{
    friend WD1793Collector;

    /// region <Types>
public:
    /// region <WD1793 / VG93 commands>
    enum WD_COMMANDS : uint8_t
    {
        WD_CMD_RESTORE = 0,  // Restore         - Move the read/write head to the outermost track (track 0)
        WD_CMD_SEEK,         // Seek            - Move the read/write head to a specified track on the floppy disk
        WD_CMD_STEP,  // Step            - Moves the read/write head in the direction previously specified (inwards or
                      // outwards) by the "step in" or "step out" command.
        WD_CMD_STEP_IN,   // Step In         - Moves the read/write head one track towards the center of the disk
                          // (increase track number)
        WD_CMD_STEP_OUT,  // Step Out        - Moves the read/write head one track away from the center of the disk
                          // (decrease track number)

        WD_CMD_READ_SECTOR,   // Read Sector     - Read a single sector from the current track
        WD_CMD_WRITE_SECTOR,  // Write Sector    - Write data to a specified sector on the current track

        WD_CMD_READ_ADDRESS,  // Read Address    - Reads the address field (track number, side number, sector number) of
                              // the current sector
        WD_CMD_READ_TRACK,    // Read Track      - Read the entire contents of a track into the FDC's internal buffer
        WD_CMD_WRITE_TRACK,   // Write Track     - Write an entire track worth of data from the FDC's internal buffer to
                              // the floppy disk

        WD_CMD_FORCE_INTERRUPT,  // Force Interrupt - Forces an interrupt to occur, regardless of the current state of
                                 // the FDC

        WD_CMD_INVALID  // Default initializer
    };

    static inline const char* getWD_COMMANDName(WD_COMMANDS command)
    {
        static const char* const names[] = {
            "Restore",         // [ 0] Restore
            "Seek",            // [ 1] Seek
            "Step",            // [ 2] Step
            "Step In",         // [ 3] Step In
            "Step Out",        // [ 4] Step Out
            "Read Sector",     // [ 5] Read Sector
            "Write Sector",    // [ 6] Write Sector
            "Read Address",    // [ 7] Read Address
            "Read Track",      // [ 8] Read Track
            "Write Track",     // [ 9] Write Track
            "Force Interrupt"  // [10] Force Interrupt
        };

        return names[command];
    }

    // WD1793 command bit patterns (top bits only, flag bits are 0)
    enum WD_COMMAND_BITS : uint8_t
    {
        // Type I commands (positioning)
        WD_CMD_BITS_RESTORE = 0b0000'0000,   // 0x00 - Restore/Recalibrate
        WD_CMD_BITS_SEEK = 0b0001'0000,      // 0x10 - Seek
        WD_CMD_BITS_STEP = 0b0010'0000,      // 0x20 - Step
        WD_CMD_BITS_STEP_IN = 0b0100'0000,   // 0x40 - Step In
        WD_CMD_BITS_STEP_OUT = 0b0110'0000,  // 0x60 - Step Out

        // Type II commands (read/write sector)
        WD_CMD_BITS_READ_SECTOR = 0b1000'0000,   // 0x80 - Read Sector
        WD_CMD_BITS_WRITE_SECTOR = 0b1010'0000,  // 0xA0 - Write Sector

        // Type III commands (read/write track)
        WD_CMD_BITS_READ_ADDRESS = 0b1100'0000,  // 0xC0 - Read Address
        WD_CMD_BITS_READ_TRACK = 0b1110'0000,    // 0xE0 - Read Track
        WD_CMD_BITS_WRITE_TRACK = 0b1111'0000,   // 0xF0 - Write Track

        // Type IV command (interrupt)
        WD_CMD_BITS_FORCE_INTERRUPT = 0b1101'0000  // 0xD0 - Force Interrupt
    };

    // Resolves a command byte to its corresponding name by masking all flag bits
    static inline const char* getWD_COMMAND_BITSName(uint8_t commandByte)
    {
        static const char* const names[] = {
            "Restore",         // [0] 0b0000'0000 (0x00)
            "Seek",            // [1] 0b0001'0000 (0x10)
            "Step",            // [2] 0b0010'0000 (0x20)
            "Step In",         // [3] 0b0100'0000 (0x40)
            "Step Out",        // [4] 0b0110'0000 (0x60)
            "Read Sector",     // [5] 0b1000'0000 (0x80)
            "Write Sector",    // [6] 0b1010'0000 (0xA0)
            "Read Address",    // [7] 0b1100'0000 (0xC0)
            "Read Track",      // [8] 0b1110'0000 (0xE0)
            "Write Track",     // [9] 0b1111'0000 (0xF0)
            "Force Interrupt"  // [10] 0b1101'0000 (0xD0)
        };

        // Command pattern lookup structure
        struct CommandPattern
        {
            uint8_t mask;     // Bit mask to apply
            uint8_t result;   // Expected result after masking
            uint8_t cmdBits;  // Command bits value
        };

        // Define command patterns with appropriate masks
        static const CommandPattern patterns[] = {
            {0b1111'0000, 0b0000'0000, WD_CMD_BITS_RESTORE},          // Restore (mask high nibble)
            {0b1111'0000, 0b0001'0000, WD_CMD_BITS_SEEK},             // Seek (mask high nibble)
            {0b1110'0000, 0b0010'0000, WD_CMD_BITS_STEP},             // Step (mask bits 5-7)
            {0b1110'0000, 0b0100'0000, WD_CMD_BITS_STEP_IN},          // Step In (mask bits 5-7)
            {0b1110'0000, 0b0110'0000, WD_CMD_BITS_STEP_OUT},         // Step Out (mask bits 5-7)
            {0b1110'0000, 0b1000'0000, WD_CMD_BITS_READ_SECTOR},      // Read Sector (mask bits 5-7)
            {0b1110'0000, 0b1010'0000, WD_CMD_BITS_WRITE_SECTOR},     // Write Sector (mask bits 5-7)
            {0b1111'0000, 0b1100'0000, WD_CMD_BITS_READ_ADDRESS},     // Read Address (mask high nibble)
            {0b1111'0000, 0b1110'0000, WD_CMD_BITS_READ_TRACK},       // Read Track (mask high nibble)
            {0b1111'0000, 0b1111'0000, WD_CMD_BITS_WRITE_TRACK},      // Write Track (mask high nibble)
            {0b1111'0000, 0b1101'0000, WD_CMD_BITS_FORCE_INTERRUPT},  // Force Interrupt (mask high nibble)
        };

        // Find matching command pattern
        uint8_t maskedCmd = WD_CMD_BITS_RESTORE;  // Default to Restore if no match
        for (const auto& pattern : patterns)
        {
            if ((commandByte & pattern.mask) == pattern.result)
            {
                maskedCmd = pattern.cmdBits;
                break;
            }
        }

        // Convert command bit patterns to indices for the names array
        int index;
        switch (maskedCmd)
        {
            case WD_CMD_BITS_RESTORE:
                index = 0;
                break;
            case WD_CMD_BITS_SEEK:
                index = 1;
                break;
            case WD_CMD_BITS_STEP:
                index = 2;
                break;
            case WD_CMD_BITS_STEP_IN:
                index = 3;
                break;
            case WD_CMD_BITS_STEP_OUT:
                index = 4;
                break;
            case WD_CMD_BITS_READ_SECTOR:
                index = 5;
                break;
            case WD_CMD_BITS_WRITE_SECTOR:
                index = 6;
                break;
            case WD_CMD_BITS_READ_ADDRESS:
                index = 7;
                break;
            case WD_CMD_BITS_READ_TRACK:
                index = 8;
                break;
            case WD_CMD_BITS_WRITE_TRACK:
                index = 9;
                break;
            case WD_CMD_BITS_FORCE_INTERRUPT:
                index = 10;
                break;
            default:
                index = 0;
                break;  // Default to Restore
        }

        return names[index];
    }

    /// endregion </WD1793 / VG93 commands>

    /// @brief Raise INTRQ signal
    /// @details Sets INTRQ signal and updates corresponding beta128 bit
    void raiseIntrq()
    {
        MLOGDEBUG("INTRQ asserted");

        _beta128status |= INTRQ;
        _intrq_out = true;
    }

    /// @brief Clear INTRQ signal
    /// @details Clears INTRQ signal and updates corresponding beta128 bit
    void clearIntrq()
    {
        _beta128status &= ~INTRQ;
        _intrq_out = false;
    }

    /// @brief Raise DRQ signal
    /// @details Sets DRQ signal and updates corresponding beta128 bit
    void raiseDrq()
    {
        MLOGDEBUG("DRQ asserted");

        _beta128status |= DRQ;
        _drq_out = true;
        _drq_served = false;
    }

    /// @brief Clear DRQ signal
    /// @details Clears DRQ signal and updates corresponding beta128 bit
    void clearDrq()
    {
        _beta128status &= ~DRQ;
        _drq_out = false;
        _drq_served = false;
    }

    void raiseCrcError()
    {
        MLOGDEBUG("CRC ERROR asserted");

        _crc_error = true;

        raiseIntrq();
    }

    void raiseLostData()
    {
        MLOGDEBUG("LOST DATA asserted");

        // Status Register Bit 2 represents LOST DATA flags for all type 2 and 3 commands
        // Only in that case this error will be mapped to Status register Bit 2 (LOST DATA)
        _lost_data = true;
    }

    void raiseRecordNotFound()
    {
        MLOGDEBUG("Record not found asserted");

        _record_not_found = true;
    }

    void clearAllErrors()
    {
        _drq_served = false;
        _lost_data = false;
        _crc_error = false;
        _record_not_found = false;
        _write_fault = false;
        _write_protect = false;
        _seek_error = false;
    }

    /// endregion </WD1793 / VG93 commands>

    /// region <WD1793 / VG93 state machine states>
    enum WDSTATE : uint8_t
    {
        S_IDLE = 0,
        S_WAIT,        // Dedicated state to handle timing delays
        S_FETCH_FIFO,  // Fetch the next state from _operationFIFO queue

        S_STEP,
        S_VERIFY,

        S_SEARCH_ID,     // Corresponds to Seek command
        S_READ_SECTOR,   // Corresponds to Read Sector command
        S_WRITE_SECTOR,  // Corresponds to Write Sector command

        S_READ_TRACK,   // Corresponds to Read Track command
        S_WRITE_TRACK,  // Corresponds to Write Track command

        S_READ_BYTE,   // Internal state when reading a single byte
        S_WRITE_BYTE,  // Internal state when writing a single byte

        S_READ_CRC,   // Reads for CRC (2 bytes) at the end of the sector data block
        S_WRITE_CRC,  // Generates and write CRC (2 bytes) at the end of the sector data block

        S_END_COMMAND,  // Command execution ends

        WDSTATE_MAX
    };

    static std::string WDSTATEToString(WDSTATE state)
    {
        std::string result;

        static constexpr const char* const names[] = {
            "S_IDLE",        "S_WAIT",        "S_FETCH_FIFO",

            "S_STEP",        "S_VERIFY",

            "S_SEARCH_ID",   "S_READ_SECTOR", "S_WRITE_SECTOR",

            "S_READ_TRACK",  "S_WRITE_TRACK",

            "S_READ_BYTE",   "S_WRITE_BYTE",

            "S_READ_CRC",    "S_WRITE_CRC",

            "S_END_COMMAND",
        };

        if (state <= sizeof(names) / sizeof(names[0]))
        {
            result = names[state];
        }
        else
        {
            result = "<Unknown state>";
        }

        return result;
    }
    /// endregion </WD1793 / VG93 state machine states>

    enum WD93_CMD_BITS : uint8_t
    {
        CMD_SEEK_RATE_MASK = 0b0000'0011,
        CMD_SEEK_VERIFY = 0b0000'0100,
        CMD_SEEK_HEADLOAD = 0b0000'1000,
        CMD_SEEK_TRKUPD = 0b0001'0000,
        CMD_SEEK_DIR = 0b0010'0000,

        CMD_WRITE_DEL = 0x01,
        CMD_SIDE_CMP_FLAG = 0x02,
        CMD_DELAY = 0x04,
        CMD_SIDE = 0x08,
        CMD_SIDE_SHIFT = 3,
        CMD_MULTIPLE = 0x10
    };

    /// region <Force Interrupt command flags>
    enum WD_FORCE_INTERRUPT : uint8_t
    {
        WD_FORCE_INTERRUPT_NONE = 0,
        WD_FORCE_INTERRUPT_NOT_READY = 0b0000'0001,    // Bit0 (J0) - Not-Ready to Ready transition
        WD_FORCE_INTERRUPT_READY = 0b0000'0010,        // Bit1 (J1) - Ready to Not-Ready transition
        WD_FORCE_INTERRUPT_INDEX_PULSE = 0b0000'0100,  // Bit2 (J2) - Index pulse
        WD_FORCE_INTERRUPT_IMMEDIATE = 0b0000'1000     // Bit3 (J3) - Immediate interrupt
    };
    /// endregion </Force Interrupt command flags>

    enum BETA128_COMMAND_BITS : uint8_t
    {
        BETA_CMD_DRIVE_MASK = 0b0000'0011,  // Bits[0,1] define drive selection. 00 - A, 01 - B, 10 - C, 11 - D

        BETA_CMD_RESET = 0b0000'0100,  // Bit2 (active low) allows to reset BDI and WD73 controller. Similar to RESTORE
                                       // command execution for the application

        // HLT - Head Load Timing is an input signal used to determine head engagement time.
        // When HLT = 1, FDC assumes that head is completely engaged. Usually it takes 30-100ms for FDD to react on HLD
        // signal from FDC and engage the head
        BETA_CMD_BLOCK_HLT =
            0b0000'1000,  // Bit3 (active low) blocks HLT signal. Normally it should be inactive (high).

        // Bit4 - select head / side. 0 - lower side head. 1 - upper side head
        BETA_CMD_HEAD = 0b0001'0000,

        // Bit5 - Unused
        BETA_CMD_RESERVED5 = 0b0010'0000,

        // Bit6 - 0 - Double density / MFM; 1 - Single density / FM
        BETA_CMD_DENSITY = 0b0100'0000,

        // Bit7 - Unused
        BETA_CMD_RESERVED7 = 0b1000'0000,
    };

    /// FDC status (corresponds to port #1F read)
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// | Bit |                 |                            Commands                                        |
    /// |     | Description     | Command Type 1   | Write    | Read     | Read      | Write     | Read      |
    /// |     |                 | (Recover & Seek) | Sector   | Sector   | Address   | Track     | Track     |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  7  | NOT READY - Drive readiness (1 = not ready; 0 = ready)                                       |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  6  | WRITE PROTECT   |        0         |    0     |    0     |     0     |   WRITE   |   WRITE   |
    /// |     |                 |                  |          |          |           |  PROTECT  |  PROTECT  |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  5  | HEAD LOADED     |        0         |    0     |    1     |    'u'    |    'h'    |    'V'    |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  4  | SEEK ERROR      |        0         |    1     |    0     |    'u'    |    'h'    |    'V'    |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  3  | CRC ERROR       |        0         |    1     |    1     |    'u'    |    'h'    |    'V'    |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  2  | TRACK 0         |        1         |    0     |    0     |    'm'    |    's'    |    'E'    |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  1  | INDEX           |        1         |    0     |    1     |    'm'    |    's'    |    'E'    |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// |  0  | BUSY            |        1         |    1     |    0     |    '0'    |    '0'    |    'E'    |
    /// +-----+-----------------+------------------+----------+----------+-----------+-----------+-----------+
    /// Legend for status bit meanings in specific command contexts:
    ///
    /// --- For "Read Address" Command (Col 6 values) ---
    /// 'u': A bit from the Sector Address (SA) read from the first encountered ID field.
    /// 'm': A bit from the Sector Address (SA) read from the first encountered ID field.
    /// '0': (In Status Register Bit 0) reflects Sector Address Bit 0.
    ///
    /// --- For "Write Track" Command (Col 7 values) ---
    /// 'h': A bit of the Cylinder Address (CA) being written to ID fields during formatting.
    /// 's': A bit of the Sector Length (SL) code being written to ID fields.
    /// '0': (In Status Register Bit 0) may indicate SL Code Bit 0, or a fixed value.
    ///
    /// --- For "Read Track" Command (Col 8 values) ---
    /// 'V': A bit from the Cylinder Address (CA) read from the first encountered ID field.
    /// 'E': A bit from the Sector Length (SL) code read from the first encountered ID field.
    ///
    /// --- For "Cxt 1" / "Cxt 2" Columns (Cols 9 & 10 values) ---
    /// 'r1', 'r0', 'C', 'a0': These represent specific bit values from fields relevant
    ///      to an unlabelled command context or a more detailed status.
    ///      Their exact meaning depends on what "Cxt 1" and "Cxt 2" refer to,
    ///      which is not specified in the original table's headers.
    ///      Commonly, they might be other bits from ID fields, data register, or
    ///      specialized status under certain conditions.
    enum WD_STATUS : uint8_t
    {
        WDS_BUSY = 0x01,      // For all commands
        WDS_INDEX = 0x02,     // For Type 1 (Restore & Seek) commands only
        WDS_DRQ = 0x02,       // For all read/write commands only
        WDS_TRK00 = 0x04,     // For Type 1 (Restore & Seek) commands only
        WDS_LOSTDATA = 0x04,  // For all read/write commands only
        WDS_CRCERR = 0x08,    // For Type 1 (Restore & Seek) commands + READ ADDRESS + READ SECTOR + WRITE SECTOR
        WDS_NOTFOUND = 0x10,  // RNF (Record Not Found) - For READ ADDRESS + READ SECTOR + WRITE SECTOR
        WDS_SEEKERR = 0x10,   // For Type 1 (Restore & Seek) commands only

        // The data mark code is a byte value that is read from the disk and helps identify the sector's
        // characteristics. Data mark code for the READ_SECTOR operation is typically:
        // - 0xFB (binary 11111011) or
        // - 0xF8 (binary 11111000)
        WDS_RECORDTYPE = 0x20,  // For READ SECTOR command only. Indicates record type code from data field address
                                // mark. 1 - Deleted Data Mark; 0 - Data Mark
        WDS_HEADLOADED = 0x20,
        WDS_WRITEFAULT = 0x20,
        WDS_WRITEPROTECTED = 0x40,  // Disk is write-protected
        WDS_NOTRDY = 0x80           // Drive is not ready
    };

    enum BETA_STATUS_BITS : uint8_t
    {
        // Bit6 - Indicates (active low) that Data Register(DR) contains assembled data in Read operations or empty in
        // Write operations Gets status directly from FDC output pin DRQ
        DRQ = 0x40,

        /// Bit7 - Set (active low) at the completion of any command and is reset when the STATUS register is read or
        /// the command register os written to INTRQ = 1 - Command complete INTRQ = 0 - Command in progress
        INTRQ = 0x80
    };

    enum WD_SYS : uint8_t
    {
        SYS_HLT = 0x08
    };

    /// Describes WD1793 / VG93 output pin signals
    enum WD_SIG : uint8_t
    {
        /// Head LoaD (HLD) output controls the loading of the Read-Write head against the media
        SIG_OUT_HLD = 0x01
    };

    using CommandHandler = void (WD1793::*)(uint8_t);
    using FSMHandler = void (WD1793::*)();

    /// Class to handle FSM transition events
    class FSMEvent
    {
    public:
        FSMEvent(WDSTATE state, std::function<void()> action, size_t delayTStates = 0)
            : _state(state), _delayTStates(delayTStates), _action(action)
        {
        }

        WDSTATE getState() const
        {
            return _state;
        }

        size_t getDelay() const
        {
            return _delayTStates;
        }

        void executeAction() const
        {
            _action();
        }

    private:
        WDSTATE _state;
        size_t _delayTStates;
        std::function<void()> _action;
    };

public:
    static constexpr const auto ONE_SECOND = std::chrono::seconds(1);
    static constexpr const auto ONE_MILLISECOND = std::chrono::milliseconds(1);
    static constexpr const auto ONE_MICROSECOND = std::chrono::microseconds(1);
    static constexpr const size_t MILLISECONDS_PER_SECOND = std::chrono::milliseconds(ONE_SECOND).count();
    static constexpr const size_t MICROSECONDS_PER_SECOND = std::chrono::microseconds(ONE_SECOND).count();
    static constexpr const size_t MICROSECONDS_PER_MILLISECOND = std::chrono::milliseconds(ONE_MILLISECOND).count();

    static constexpr const size_t Z80_FREQUENCY = 3.5 * 1'000'000;
    static constexpr const size_t TSTATES_PER_MS = Z80_FREQUENCY / 1000;  // 3500 t-states per millisecond
    static constexpr const double TSTATES_PER_US = (double)Z80_FREQUENCY / 1'000'000.0;  // 3.5 t-states per microsecond

    static constexpr const size_t WD93_FREQUENCY = 1'000'000;
    static constexpr const double WD93_CLK_CYCLES_PER_Z80_CLK = Z80_FREQUENCY / WD93_FREQUENCY;

    /// Time limit to retrieve single byte from WD1793
    /// We must read the whole track during single disk spin (200ms), so we have just 114 t-states per byte
    static constexpr const size_t WD93_TSTATES_PER_FDC_BYTE = Z80_FREQUENCY / (MAX_TRACK_LEN * FDD_RPS);

    static constexpr const size_t WD93_REVOLUTIONS_TILL_MOTOR_STOP = 15;
    static constexpr const size_t WD93_TSTATES_TILL_MOTOR_STOP =
        Z80_FREQUENCY * WD93_REVOLUTIONS_TILL_MOTOR_STOP / FDD_RPS;

    // Type1 commands (with V=1 verify flag set) wait for index address mark no more than 5 disk revolutions
    static constexpr const size_t WD93_REVOLUTIONS_LIMIT_FOR_INDEX_MARK_SEARCH = 5;
    static constexpr const size_t WD93_TSTATES_LIMIT_FOR_INDEX_MARK_SEARCH =
        Z80_FREQUENCY * WD93_REVOLUTIONS_LIMIT_FOR_INDEX_MARK_SEARCH / FDD_RPS;

    /// Type2 commands wait 4 disk revolutions at most to find index address mark field on disk
    static constexpr const size_t WD93_REVOLUTIONS_LIMIT_FOR_TYPE2_INDEX_MARK_SEARCH = 4;
    static constexpr const size_t WD93_TSTATES_LIMIT_FOR_TYPE2_INDEX_MARK_SEARCH =
        Z80_FREQUENCY * WD93_REVOLUTIONS_LIMIT_FOR_TYPE2_INDEX_MARK_SEARCH / FDD_RPS;

    /// We can do no more than 255 head steps. Normally it cannot be more than 80-83 track positioning steps. If we
    /// reached 255 limit - FDD is broken
    static constexpr const size_t WD93_STEPS_MAX = 255;

    /// After the last directional step an additional 15 milliseconds of head settling time takes place if the Verify
    /// flag is set in Type I commands
    static constexpr const size_t WD93_VERIFY_DELAY_MS = 15;

    /// How many WD1793 commands we support (used for array/table allocations)
    static constexpr const size_t WD93_COMMAND_COUNT = 11;

    // Decoded port addresses (physical address line matching done in platform port decoder)
    static constexpr const uint16_t PORT_1F = 0x001F;    // Write - command register; Read - state
    static constexpr const uint16_t PORT_3F = 0x003F;    // Track register
    static constexpr const uint16_t PORT_5F = 0x005F;    // Sector register
    static constexpr const uint16_t PORT_7F = 0x007F;    // Data register
    static constexpr const uint16_t PORT_FF = 0x00FF;    // Write - BETA128 system controller; Read - data and interrupt
                                                         // request bits from WD1793 (Bit6 - DRQ, bit7 - INTRQ)
    static constexpr const uint16_t PORT_7FFD = 0x7FFD;  // DOS lock mode. Bit4 = 0 - block; Bit4 = 1 - allow

    // Stepping rates from WD93 datasheet
    static constexpr const uint8_t STEP_TIMINGS_MS_1MHZ[] = {6, 12, 20, 30};
    static constexpr const uint8_t STEP_TIMINGS_MS_2MHZ[] = {3, 6, 10, 15};

    // Disk rotation and timing constants
    static constexpr const size_t DISK_ROTATION_PERIOD_TSTATES = Z80_FREQUENCY / FDD::DISK_REVOLUTIONS_PER_SECOND;
    static constexpr const size_t INDEX_STROBE_DURATION_TSTATES =
        DISK_ROTATION_PERIOD_TSTATES / 100 * 2;  // 2% of rotation period

    // Sleep mode: FDD enters sleep after 2 seconds of inactivity to save CPU
    static constexpr const size_t SLEEP_AFTER_IDLE_TSTATES = Z80_FREQUENCY * 2;  // 2 seconds of inactivity

    /// endregion </Constants>

    /// region <ModuleLogger definitions for Module/Submodule>
public:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISK;
    const uint16_t _SUBMODULE = PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC;
    /// endregion </ModuleLogger definitions for Module/Submodule>

    /// region <Fields>
protected:
    WD1793Collector* _collector = nullptr;

    PortDecoder* _portDecoder = nullptr;
    bool _chipAttachedToPortDecoder = false;

    FDD* _selectedDrive = nullptr;

    // New WD93 state to migrate to
    WD93State _wd93State;

    // Counters to measure time intervals
    size_t _time = 0;           // Current time mark (in Z80 t-states)
    size_t _lastTime = 0;       // Time mark when process() was called last time
    int64_t _diffTime = 0;      // Difference between _time and _lastTime (_time - _lastTime)
    int64_t _delayTStates = 0;  // Delay between switching to the next state

    // WD93 internal state
    uint8_t _commandRegister = 0x00;  // WD1793 Command Register - Holds last command executed (full data byte)
    uint8_t _trackRegister = 0;       // WD1793 Track Register
    uint8_t _sectorRegister = 0;      // WD1793 Sector Register
    uint8_t _dataRegister = 0x00;     // WD1793 Data Register
    uint8_t _statusRegister = 0x00;   // WD1793 Status Register

    uint8_t _beta128Register = 0x00;  // Beta128 system register
    uint8_t _beta128status = 0x00;    // Beta128 status output: Data request (DRQ) and interrupt request (INTRQ) flags
    uint8_t _extStatus = 0x00;        // External status. Only HLD flag is supported

    // Beta128 state

    // Set when BETA128 register (port 0xFF) is written to
    // 0 - A, 1 - B, 2 - C, 3 - D
    uint8_t _drive = 0;    // Currently selected drive index [0..3]
    bool _sideUp = false;  // False - bottom side. True - top side

    // WD1793 state getters - moved to public section
    WD_COMMANDS _lastDecodedCmd = WD_CMD_RESTORE;  // Last command executed (decoded)
    uint8_t _lastCmdValue = 0x00;                  // Last command parameters (already masked)
    WDSTATE _state = S_IDLE;

    WDSTATE _state2 = S_IDLE;
    std::queue<FSMEvent> _operationFIFO;  // Holds FIFO queue for scheduled state transitions (when WD1793 command
                                          // requires complex FSM flow)

    // Type 1 command params
    bool _loadHead = false;  // Determines if load should be loaded or unloaded during Type1 command
    bool _verifySeek =
        false;  // Determines if VERIFY (check for ID Address Mark) needs to be done after head positioning
    uint8_t _steppingMotorRate =
        6;  // Positioning speed rate. Value is resolved from Type1 command via STEP_TIMINGS_MS_1MHZ and
            // STEP_TIMINGS_MS_2MHZ arrays depending on WD1793 clock speed

    // Internal state for Type1 commands
    int8_t _stepDirectionIn = false;  // Head step direction. True - move head towards center cut (Step In). False -
                                      // move outwards to Track 0 (Step Out)
    size_t _stepCounter = 0;          // Count each head positioning step during current Type 1 command
    bool _headLoaded = false;

    // Type 2 command params
    uint16_t _sectorSize =
        256;  // Sector size in bytes (will be read from ID Address Mark during READ SECTOR command execution)
    uint8_t* _idamData = nullptr;
    uint8_t* _sectorData = nullptr;
    uint8_t* _rawDataBuffer = nullptr;  // Pointer to read/write data
    int32_t _bytesToRead = 0;           // How many more bytes to read from the disk
    int32_t _bytesToWrite = 0;          // How many more bytes to write to the disk

    // FDD state
    // TODO: all timeouts must go to WD93State.counters
    bool _index = false;                    // Current state of index strobe
    bool _prevIndex = false;                // Previous state of index strobe
    uint64_t _lastIndexPulseStartTime = 0;  // T-state when the current index pulse started (0 if no active pulse)
    size_t _indexPulseCounter = 0;          // Index pulses counter
    int64_t _motorTimeoutTStates =
        0;  // 0 - motor already stopped. >0 - how many ticks left till auto-stop (timeout is 15 disk revolutions)

    // TODO Remove temporary fields once switched to WD93State
    bool _drq_served = false;  // Type II and III commands have timeout for data availability in Data Register
    bool _lost_data = false;   // Type II and III commands have this error type if DRQ service time condition is not met
    bool _crc_error = false;   // Type II: Read Sector, Write Sector, Read Address commands can trigger CRC Error
    bool _record_not_found =
        false;  // Type II: Read Sector, Write Sector, Read Address commands can trigger the Record not Found error
    bool _write_fault = false;  // Write sector and Write Track commands can trigger Write Fault error
    bool _write_protect =
        false;  // All Type I as well as Write sector and Write Track commands can trigger a Write Protect error
    bool _seek_error = false;  // All Type I commands can trigger Seek Error

    // TODO: Remove temporary fields once switched to WD93State.signals
    bool _intrq_out = false;
    bool _drq_out = false;

    // Force Interrupt command conditions (I0-I3 bits)
    uint8_t _interruptConditions = 0;  // Stores the interrupt condition flags from the Force Interrupt command
    bool _prevReady = false;           // Previous drive ready state (for I0/I1 transition detection)

    // Debug logging state (instance-specific to avoid race conditions)
    uint64_t _lastDebugLogTime = 0;  // Last time debug log was printed (prevents log spam)

    // Sleep mode state - reduces CPU overhead when FDD is idle
    bool _sleeping = true;        // Start in sleep mode (wake on first port access)
    uint64_t _wakeTimestamp = 0;  // T-state when last port access occurred

    /// endregion </Fields>

    /// region <Properties>
public:
    FDD* getDrive()
    {
        return _selectedDrive;
    }
    void setDrive(FDD* drive)
    {
        _selectedDrive = drive;
    }

    // Getters for WD1793 state and registers
    const WD93State& getState() const
    {
        return _wd93State;
    }
    WD_COMMANDS getLastDecodedCommand() const
    {
        return _lastDecodedCmd;
    }
    uint8_t getStatusRegister() const
    {
        return _statusRegister;
    }
    uint8_t getTrackRegister() const
    {
        return _trackRegister;
    }
    uint8_t getSectorRegister() const
    {
        return _sectorRegister;
    }
    uint8_t getDataRegister() const
    {
        return _dataRegister;
    }
    uint8_t getBeta128Status() const
    {
        return _beta128status;
    }
    /// endregion </Properties>

    /// region <Constructors / destructors>
public:
    WD1793(EmulatorContext* context);
    virtual ~WD1793();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    virtual void reset();
    void internalReset();

    void process();
    void ejectDisk();
    /// endregion </Methods>

    /// region <Helper methods>
protected:
    void processBeta128(uint8_t value);
    void processFDDMotorState();
    void processFDDIndexStrobe();
    void prolongFDDMotorRotation();
    void processCountersAndTimeouts();
    void processForceInterruptConditions();  // Monitor Force Interrupt I0/I1 conditions
    void startFDDMotor();
    void stopFDDMotor();
    void loadHead();
    void unloadHead();
    uint8_t getStatusRegister();
    bool isReady();

    // Sleep mode management - reduces CPU overhead when FDD is idle
    void wakeUp();          // Wake FDD from sleep mode
    void enterSleepMode();  // Put FDD into sleep mode
    bool isSleeping() const
    {
        return _sleeping;
    }
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
    std::vector<FSMHandler> _stateHandlers;

    void processIdle();
    void processWait();
    void processFetchFIFO();
    void processStep();
    void processVerify();
    void processSearchID();
    void processReadSector();
    void processReadTrack();
    void processWriteTrack();
    void processReadByte();
    void processWriteSector();
    void processWriteByte();
    void processReadCRC();
    void processWriteCRC();
    void processEndCommand();

    /// @brief Check if there are more actions in the FIFO queue
    bool hasMoreFIFOActions()
    {
        return !_operationFIFO.empty();
    }

    void transitionFSM(WDSTATE nextState)
    {
        /// region <Debug logging>
        std::string timeMark = StringHelper::Format("  [%d | %d ms]", _time, convertTStatesToMs(_time));
        std::string message = StringHelper::Format("  %s -> %s <nodelay> %s", WDSTATEToString(_state).c_str(),
                                                   WDSTATEToString(nextState).c_str(), timeMark.c_str());
        MLOGINFO(message.c_str());
        /// endregion </Debug logging>

        _state = nextState;
        _state2 = WDSTATE::S_IDLE;
    }

    void transitionFSMWithDelay(WDSTATE nextState, size_t delayTStates)
    {
        /// region <Debug logging>
        std::string delayNote =
            StringHelper::Format(" delay(%d | %.02f ms)", delayTStates, convertTStatesToMsFloat(delayTStates));
        std::string message = StringHelper::Format("  %s -> %s %s", WDSTATEToString(_state).c_str(),
                                                   WDSTATEToString(nextState).c_str(), delayNote.c_str());
        MLOGINFO(message.c_str());
        /// endregion </Debug logging>

        _state2 = nextState;
        _delayTStates = delayTStates - 1;
        _state = WDSTATE::S_WAIT;  // Keep FSM in wait state until delay time elapsed
    }

    void transitionFSMWithDelay(FSMEvent nextStateEvent, size_t delayTStates)
    {
        WDSTATE nextState = nextStateEvent.getState();

        /// region <Debug logging>
        std::string delayNote =
            StringHelper::Format(" delay(%d | %d ms)", delayTStates, convertTStatesToMs(delayTStates));
        std::string message = StringHelper::Format("  %s -> %s %s", WDSTATEToString(_state).c_str(),
                                                   WDSTATEToString(nextState).c_str(), delayNote.c_str());
        MLOGINFO(message.c_str());
        /// endregion </Debug logging>

        nextStateEvent.executeAction();

        _state2 = nextState;
        _delayTStates = delayTStates - 1;
        _state = WDSTATE::S_WAIT;  // Keep FSM in wait state until delay time elapsed
    }

    void processClockTimings()
    {
        // Decouple time sync with the emulator state. Unit tests override updateTimeFromEmulatorState(); as dummy stub
        updateTimeFromEmulatorState();

        _diffTime = std::abs((int64_t)(_time - _lastTime));

        // If we're waiting more than 15 disk revolutions - something goes wrong on Z80 side,
        // and we need to recover into
        if (_diffTime > (int64_t)(Z80_FREQUENCY * 15 / FDD_RPS))
        {
            // TODO: handle the case
            // throw std::logic_error("Not implemented yet");
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

    /// Provide value stored in Data Register and update access flag
    uint8_t readDataRegister()
    {
        uint8_t result = _dataRegister;

        _drq_served = true;
        clearDrq();

        return result;
    }

    void writeDataRegister(uint8_t value)
    {
        _dataRegister = value;

        // If we're on Read or Write operation and data was requested by asserting DRQ - we need to mark that request
        // fulfilled
        _drq_served = true;
        clearDrq();
    }

    /// endregion </State machine handlers>

    /// region <Emulation events>
public:
    void handleFrameStart();
    void handleStep();
    void handleFrameEnd();
    /// endregion </Emulation events>

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
    std::string dumpBeta128Register();
    std::string dumpCommand(uint8_t value) const;
    std::string dumpStep();
    std::string dumpFullState();
    std::string dumpIndexStrobeData(bool skipNoTransitions = true);

    static inline size_t convertTStatesToMs(size_t tStates)
    {
        size_t result = tStates / TSTATES_PER_MS;

        return result;
    }

    static inline double convertTStatesToMsFloat(size_t tStates)
    {
        double result = (double)tStates / (double)TSTATES_PER_MS;

        return result;
    }
    /// endregion </Debug methods>
};

//
// Code Under Test (CUT) wrapper to allow access to protected and private properties and methods for unit testing /
// benchmark purposes
//
#ifdef _CODE_UNDER_TEST

class WD1793CUT : public WD1793
{
public:
    WD1793CUT(EmulatorContext* context) : WD1793(context) {};

    using WD1793::_portDecoder;
    using WD1793::_selectedDrive;

    using WD1793::_commandRegister;
    using WD1793::_lastCmdValue;
    using WD1793::_lastDecodedCmd;

    using WD1793::_beta128Register;
    using WD1793::_beta128status;
    using WD1793::_dataRegister;
    using WD1793::_sectorRegister;
    using WD1793::_statusRegister;
    using WD1793::_trackRegister;

    using WD1793::_drive;
    using WD1793::_sideUp;

    using WD1793::_delayTStates;
    using WD1793::_state;
    using WD1793::_state2;

    using WD1793::_diffTime;
    using WD1793::_lastTime;
    using WD1793::_time;

    using WD1793::_motorTimeoutTStates;
    using WD1793::_stepCounter;
    using WD1793::_stepDirectionIn;
    using WD1793::_steppingMotorRate;

    using WD1793::_crc_error;
    using WD1793::_drq_out;
    using WD1793::_drq_served;
    using WD1793::_intrq_out;
    using WD1793::_lost_data;
    using WD1793::_record_not_found;
    using WD1793::_seek_error;
    using WD1793::_write_fault;
    using WD1793::_write_protect;

    using WD1793::_index;
    using WD1793::_indexPulseCounter;
    using WD1793::_interruptConditions;
    using WD1793::_lastIndexPulseStartTime;
    using WD1793::_prevIndex;

    using WD1793::decodeWD93Command;
    using WD1793::getWD93CommandValue;
    using WD1793::isType1Command;
    using WD1793::isType2Command;
    using WD1793::isType3Command;
    using WD1793::isType4Command;
    using WD1793::processWD93Command;

    using WD1793::cmdForceInterrupt;
    using WD1793::cmdReadAddress;
    using WD1793::cmdReadSector;
    using WD1793::cmdReadTrack;
    using WD1793::cmdRestore;
    using WD1793::cmdSeek;
    using WD1793::cmdStep;
    using WD1793::cmdStepIn;
    using WD1793::cmdStepOut;
    using WD1793::cmdWriteSector;
    using WD1793::cmdWriteTrack;

    using WD1793::processIdle;
    using WD1793::processStep;
    using WD1793::processVerify;
    using WD1793::processWait;

    using WD1793::processClockTimings;
    using WD1793::transitionFSMWithDelay;
    virtual void updateTimeFromEmulatorState() override {
    };  // Make it dummy stub and skip reading T-State counters from the emulator
    using WD1793::getDrive;
    using WD1793::processFDDIndexStrobe;
    using WD1793::processFDDMotorState;
    using WD1793::prolongFDDMotorRotation;
    using WD1793::readDataRegister;
    using WD1793::resetTime;
    using WD1793::startFDDMotor;
    using WD1793::stopFDDMotor;
    using WD1793::writeDataRegister;

    // Sleep mode fields and methods
    using WD1793::_sleeping;
    using WD1793::_wakeTimestamp;
    using WD1793::enterSleepMode;
    using WD1793::isSleeping;
    using WD1793::wakeUp;

    // Force Interrupt condition fields and methods
    using WD1793::_prevReady;
    using WD1793::isReady;
    using WD1793::processForceInterruptConditions;
};

#endif  // _CODE_UNDER_TEST