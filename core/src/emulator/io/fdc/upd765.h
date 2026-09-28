#pragma once

#include <cstddef>
#include <cstdint>

#include "debugger/ttd/ttdserializable.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdd.h"
#include "emulator/platform.h"
#include "stdafx.h"

class ModuleLogger;

/// NEC uPD765A floppy disk controller as wired in the ZX Spectrum +3.
///
/// Design: docs/inprogress/2026-09-28-plus3-upd765/technical-design.md
///
/// +3 wiring: #2FFD reads the main status register, #3FFD reads / writes the data register, #1FFD bit 3
/// switches the motor of both drives. TC is tied low (every read runs to EOT and ends with "end of cylinder"),
/// INT and DMA are not connected (the CPU polls the MSR), US1 is not connected (units 2 / 3 are drives 0 / 1).
///
/// Clock: absolute T-states (emulatorState.t_states + Z80 t), the same as the WD1793. The controller has no
/// per-instruction hook: every wait is a deadline (_eventTime), and process() replays the deadlines that
/// passed in time order on each port access. What happens depends only on the deadlines, never on when the
/// CPU happens to look, so the result is the same however often the CPU polls.
class UPD765 : public ttd::TTDSerializable
{
    /// region <Constants>
public:
    static constexpr const uint8_t UNITS = 4;                  // Unit select US0-US1 addresses 4 units...
    static constexpr const uint8_t DRIVES = 2;                 // ...but the +3 leaves US1 unconnected
    static constexpr const uint8_t RECALIBRATE_STEPS = 77;     // RECALIBRATE gives up after 77 step pulses
    static constexpr const size_t MAX_COMMAND_BYTES = 9;
    static constexpr const size_t MAX_RESULT_BYTES = 7;
    static constexpr const size_t MAX_FORMAT_SECTORS = 64;     // More than any 6250-byte track can hold
    static constexpr const size_t ID_FIELD_BYTES = 7;          // FE C H R N CRC CRC

    // FORMAT TRACK layout (IBM System 34 as TrackFormatSpec::ibm writes it): gap 4a, sync, IAM, gap 1
    static constexpr const size_t FORMAT_PREAMBLE_BYTES = 80 + 12 + 4 + 50;
    // Sector slot before the data field: sync, IDAM, ID field, gap 2, sync, DAM
    static constexpr const size_t FORMAT_SLOT_BYTES = 12 + 4 + 4 + 2 + 22 + 12 + 4 + 2;
    static constexpr const size_t FORMAT_ID_OFFSET = 12 + 4;   // First ID byte inside a sector slot
    /// endregion </Constants>

    /// region <Types>
public:
    /// Command codes (low 5 bits of the first command byte)
    enum UPD_COMMANDS : uint8_t
    {
        CMD_READ_TRACK = 0x02,
        CMD_SPECIFY = 0x03,
        CMD_SENSE_DRIVE_STATUS = 0x04,
        CMD_WRITE_DATA = 0x05,
        CMD_READ_DATA = 0x06,
        CMD_RECALIBRATE = 0x07,
        CMD_SENSE_INTERRUPT_STATUS = 0x08,
        CMD_WRITE_DELETED_DATA = 0x09,
        CMD_READ_ID = 0x0A,
        CMD_READ_DELETED_DATA = 0x0C,
        CMD_FORMAT_TRACK = 0x0D,
        CMD_SEEK = 0x0F,
        CMD_INVALID = 0xFF
    };

    /// First command byte flags
    enum UPD_COMMAND_FLAGS : uint8_t
    {
        CMD_FLAG_MT = 0x80,  // Multi-track: continue on head 1 after EOT on head 0
        CMD_FLAG_MF = 0x40,  // MFM (double density)
        CMD_FLAG_SK = 0x20   // Skip sectors with the other data address mark
    };

    /// Main status register (#2FFD)
    enum UPD_MSR : uint8_t
    {
        MSR_D0B = 0x01,  // Drive 0..3 seeking (cleared by SENSE INTERRUPT STATUS)
        MSR_D1B = 0x02,
        MSR_D2B = 0x04,
        MSR_D3B = 0x08,
        MSR_CB = 0x10,   // Command busy
        MSR_EXM = 0x20,  // Execution phase (non-DMA mode)
        MSR_DIO = 0x40,  // 1: FDC -> CPU
        MSR_RQM = 0x80   // Data register ready
    };

    enum UPD_ST0 : uint8_t
    {
        ST0_US = 0x03,
        ST0_HD = 0x04,
        ST0_NR = 0x08,  // Not ready
        ST0_EC = 0x10,  // Equipment check (RECALIBRATE found no track 0)
        ST0_SE = 0x20,  // Seek end
        ST0_IC_ABNORMAL = 0x40,      // IC = 01: abnormal termination
        ST0_IC_INVALID = 0x80,       // IC = 10: invalid command
        ST0_IC_READY_CHANGE = 0xC0,  // IC = 11: ready line changed
        ST0_IC = 0xC0
    };

    enum UPD_ST1 : uint8_t
    {
        ST1_MA = 0x01,  // Missing address mark
        ST1_NW = 0x02,  // Not writable
        ST1_ND = 0x04,  // No data (sector not found)
        ST1_OR = 0x10,  // Overrun
        ST1_DE = 0x20,  // Data error (CRC)
        ST1_EN = 0x80   // End of cylinder
    };

    enum UPD_ST2 : uint8_t
    {
        ST2_MD = 0x01,  // Missing address mark in data field
        ST2_BC = 0x02,  // Bad cylinder (ID C = FF)
        ST2_WC = 0x10,  // Wrong cylinder
        ST2_DD = 0x20,  // Data error in data field
        ST2_CM = 0x40   // Control mark (the other data address mark)
    };

    enum UPD_ST3 : uint8_t
    {
        ST3_US = 0x03,
        ST3_HD = 0x04,
        ST3_TS = 0x08,  // Two sided
        ST3_T0 = 0x10,  // Track 0
        ST3_RY = 0x20,  // Ready
        ST3_WP = 0x40,  // Write protected
        ST3_FT = 0x80   // Fault
    };

    enum UPDPHASE : uint8_t
    {
        PHASE_COMMAND = 0,
        PHASE_EXECUTION,
        PHASE_RESULT
    };

    /// Execution phase states; each runs at _eventTime
    enum UPDSTATE : uint8_t
    {
        S_IDLE = 0,
        S_SEARCH,          // Look for the next sector C H R N from the head position
        S_READ_BYTE,       // Byte _byteIndex of the sector arrives; the previous one must have been taken
        S_WRITE_BYTE,      // Byte _byteIndex of the sector is due; the previous one must have been given
        S_READ_ID,         // The ID field found by READ ID has passed under the head
        S_FORMAT_REQUEST,  // FORMAT asks for the next ID byte
        S_FORMAT_DUE,      // The requested ID byte must have been given by now
        S_FORMAT_END,      // Index pulse after the formatted revolution
        S_RESULT           // Enter the result phase (errors found ahead of time, e.g. after two index pulses)
    };

    /// Per unit (US0-US1) controller state. Units 2 / 3 drive the heads of drives 0 / 1 on the +3, but the
    /// controller keeps their cylinder and seek state separately, as the chip does
    struct UnitState
    {
        uint8_t pcn = 0;                // Present cylinder number
        uint8_t ncn = 0;                // Cylinder the running seek ends on
        uint8_t st0 = 0;                // ST0 a SENSE INTERRUPT STATUS returns
        bool seeking = false;           // SEEK / RECALIBRATE stepping
        bool interruptPending = false;  // ST0 waits for SENSE INTERRUPT STATUS
        bool seekBusy = false;          // MSR DnB: from SEEK / RECALIBRATE start until SENSE INTERRUPT STATUS
        int16_t stepDelta = 0;          // Physical head movement applied when the seek ends
        uint64_t seekEndTime = 0;
    };
    /// endregion </Types>

    /// region <Fields>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DISK;
    const uint16_t _SUBMODULE = PlatformDiskSubmodulesEnum::SUBMODULE_DISK_FDC;
    ModuleLogger* _logger = nullptr;

    EmulatorContext* _context = nullptr;

    // Time
    uint64_t _time = 0;       // Now (absolute T-states)
    uint64_t _eventTime = 0;  // When the execution phase state runs next

    // Phases
    UPDPHASE _phase = PHASE_COMMAND;
    UPDSTATE _state = S_IDLE;

    uint8_t _command[MAX_COMMAND_BYTES] = {};
    uint8_t _commandLength = 0;  // Bytes the current command takes (0 - waiting for the first byte)
    uint8_t _commandPos = 0;

    uint8_t _result[MAX_RESULT_BYTES] = {};
    uint8_t _resultLength = 0;
    uint8_t _resultPos = 0;

    // Execution
    uint8_t _st0 = 0;
    uint8_t _st1 = 0;
    uint8_t _st2 = 0;
    uint8_t _dataRegister = 0xFF;
    bool _dataReady = false;       // Read: a byte waits for the CPU
    bool _dataRequested = false;   // Write / format: the FDC waits for a byte
    uint16_t _byteIndex = 0;       // Bytes of the sector (or format IDs) handled so far
    uint16_t _transferSize = 0;
    int16_t _sectorIndex = -1;     // Sector being transferred: index into Track::sectors()
    bool _endAfterSector = false;  // READ met the other data address mark with SK = 0
    uint64_t _formatStart = 0;     // Index pulse FORMAT started at
    uint8_t _formatIds[MAX_FORMAT_SECTORS * 4] = {};

    // SPECIFY
    uint8_t _stepRateTime = 0x0A;  // SRT: step every (16 - SRT) * 2 ms (4 MHz clock)
    uint8_t _headLoadTime = 0x01;  // HLT: head load (HLT * 4 ms)

    // Drives
    UnitState _units[UNITS];
    bool _motorOn = false;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    UPD765(EmulatorContext* context);
    virtual ~UPD765() = default;
    /// endregion </Constructors / destructors>

    /// region <Ports>
public:
    /// #2FFD: main status register
    uint8_t readMainStatus();

    /// #3FFD in: result bytes, execution phase data
    uint8_t readData();

    /// #3FFD out: command bytes, execution phase data
    void writeData(uint8_t value);

    /// #1FFD bit 3: motor of both drives
    void setMotor(bool on);
    bool getMotor() const { return _motorOn; }
    /// endregion </Ports>

    /// region <Methods>
public:
    void reset();

    /// Run every deadline that is due at the current time
    void process();

    UPDPHASE getPhase() const { return _phase; }
    uint8_t getPresentCylinder(uint8_t unit) const { return _units[unit & 0x03].pcn; }
    /// endregion </Methods>

    /// region <TTDSerializable>
public:
    size_t TTDStateSize() const override;
    void TTDSaveState(uint8_t* dst) const override;
    void TTDLoadState(const uint8_t* src) override;
    std::string TTDDeviceName() const override { return "UPD765"; }
    ttd::PeripheralId TTDPeripheralId() const override { return ttd::PeripheralId::Upd765; }
    uint64_t TTDHashState() const override;
    /// endregion </TTDSerializable>

    /// region <Helper methods>
protected:
    /// Current time from the emulator (tests override it)
    virtual void updateTimeFromEmulatorState()
    {
        _time = _context->emulatorState.t_states + _context->pCore->GetZ80()->t;
    }

    static uint8_t commandLengthFor(uint8_t code);
    uint8_t commandCode() const { return _command[0] & 0x1F; }
    uint8_t unit() const { return _command[1] & 0x03; }
    uint8_t head() const { return (_command[1] >> 2) & 0x01; }

    /// Drive a unit reaches: US1 is not connected on the +3
    FDD* driveFor(uint8_t unitNumber) const;
    bool isReady(uint8_t unitNumber) const;

    /// Track under the head of the unit's drive, nullptr when the image has none there
    DiskImage::Track* currentTrack() const;
    DiskImage::Sector* currentSector() const;

    /// T-states in one millisecond / one disk revolution, from the machine's base CPU clock
    /// (emulatorState.base_z80_frequency, the clock t_states count in)
    uint64_t tstatesPerMs() const;
    uint64_t rotationTStates() const { return tstatesPerMs() * FDD::DISK_INDEX_PERIOD_MS; }

    /// Head position on the track (stream offset) at the given time, the WD1793's formula
    size_t headByteOffset(const DiskImage::Track& track, uint64_t time) const;
    size_t byteCellTStates(const DiskImage::Track* track) const;
    uint64_t nextIndexPulse(uint64_t time) const;
    uint64_t headLoadTStates() const;
    uint64_t stepTStates() const;

    // Command dispatch
    void executeCommand();
    void commandSpecify();
    void commandSenseDriveStatus();
    void commandSenseInterruptStatus();
    void commandSeek(bool recalibrate);
    void commandReadWrite();
    void commandReadId();
    void commandFormatTrack();
    void commandInvalid();

    // Execution phase states
    void runState();
    void searchSector();
    void readByte();
    void writeByte();
    void readIdDone();
    void formatRequest();
    void formatDue();
    void formatEnd();
    void finishSector(uint64_t time);
    void commitWrittenSector();

    void completeSeeks();

    /// Leave the execution phase: the result phase starts at `time`
    void scheduleResult(uint64_t time);
    void enterDataResult();
    void enterResult(const uint8_t* bytes, uint8_t count);
    /// endregion </Helper methods>
};

#ifdef _CODE_UNDER_TEST

/// Test wrapper: the test owns the clock
class UPD765CUT : public UPD765
{
public:
    UPD765CUT(EmulatorContext* context) : UPD765(context) {}

    void updateTimeFromEmulatorState() override {}  // _time is set by the test

    using UPD765::_time;
    using UPD765::_eventTime;
    using UPD765::_phase;
    using UPD765::_state;
    using UPD765::_units;
    using UPD765::_stepRateTime;
    using UPD765::_headLoadTime;
    using UPD765::headByteOffset;
    using UPD765::byteCellTStates;
    using UPD765::headLoadTStates;
    using UPD765::stepTStates;
    using UPD765::rotationTStates;
    using UPD765::tstatesPerMs;
};

#endif  // _CODE_UNDER_TEST
