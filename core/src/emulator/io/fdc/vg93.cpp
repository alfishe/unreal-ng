#include "vg93.h"

#include "common/stringhelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/core.h"

/// region <Constructors / destructors>

VG93::VG93(EmulatorContext* context) : PortDecoder(context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _chipAttachedToPortDecoder = false;
    _ejectPending = false;

    _selectedDrive = new FDD(context);
}

VG93::~VG93()
{
    if (_selectedDrive)
    {
        delete _selectedDrive;
    }
}

/// endregion </Constructors / destructors>

/// region <Methods>

/// Update FDC internal state
void VG93::process()
{
    //return;

    // Get current Z80 clock state for timings synchronization
    uint64_t totalTime = _context->emulatorState.t_states;
    uint64_t frameTime = _context->pCore->GetZ80()->t;
    _time = totalTime + frameTime;

    // Emulate disk rotation and index strobe changes
    processIndexStrobe();

    // Stop motor if HLD signal is inactive
    if (!(_extStatus & SIG_OUT_HLD))
    {
        _selectedDrive->setMotor(false);
    }

    // If no data available from disk image - not ready flag must be set
    if (_selectedDrive->getRawData())
    {
        _status &= ~WDS_NOTRDY;     // Data available => clear NOT READY
    }
    else
    {
        _status |= WDS_NOTRDY;      // No data => NOT READY
    }

    /// region <Main state machine

    while (true)
    {
        if (!_selectedDrive->getMotor())
        {
            _status |= WDS_NOTRDY;  // Motor is not started yet
        }
        else
        {
            _status &= ~WDS_NOTRDY; // Motor already started
        }

        switch (_state)
        {
            case S_IDLE:
                _status &= ~WDS_BUSY;   // Remove busy flag

                // Stop motor after 3 seconds (3 * 5 revolutions per second) being idle
                if (_indexPulseCounter > 15 || _time > _rotationCounter)
                {
                    _indexPulseCounter = 15;
                    _status = 0x00;                     // Clear status
                    _status |= WDS_NOTRDY;              // Set NOT READY status bit
                    _extStatus &= ~SIG_OUT_HLD;         // Unload read-write head

                    _selectedDrive->setMotor(false);    // Stop motor
                }
                _rqs = INTRQ;
                return;

                break;
            case S_WAIT:
                if (_time >= _next)
                {
                    // Delay already passed, set next status (queued in _state2)
                    _state = _state2;
                }
                else
                {
                    // Delay is still active. Do not change current status
                    return;
                }
                break;
            case S_DELAY_BEFORE_CMD: // Type 2 or 3 command issues (read/write sector or track)
                if (_lastCmd & CMD_DELAY) // Check for Bit2: E=1 command parameter
                {
                    _next = _time + Z80_CLK_CYCLES_PER_MS * 15; // Make 15ms delay as requested
                }
                _state2 = S_WAIT_HLT_RW;
                _state = S_WAIT;
                break;
            case S_WAIT_HLT_RW:     // Wait for the read-write head to be engaged
                if (!(_beta128 & BETA_COMMAND_BITS::BETA_CMD_BLOCK_HLT)) // If BETA_CMD_BLOCK_HLT is active low - infinite wait
                {
                    return;
                }
                _state = S_CMD_RW;  // Transition to read/write operation after the delay
                break;
            case S_CMD_RW:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                break;
            case S_FOUND_NEXT_ID:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                break;
            case S_RDSEC:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                break;
            case S_READ:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                // Set READ_SECTOR specific data mark status
                // seldrive->t.hdr[foundid].data[-1] == 0xF8
                if (false)
                    _status |= WDS_RECORDTYPE;
                else
                    _status &= ~WDS_RECORDTYPE;

                _next += T_STATES_PER_BYTE;

                _state = S_WAIT;
                _state2 = S_READ;
                break;
            case S_WRSEC:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                break;
            case S_WRITE:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                break;
            case S_WRTRACK:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                break;
            case S_WR_TRACK_DATA:
                // TODO: transfer code
                throw std::logic_error("Not implemented yet");
                break;
            case S_TYPE1_CMD:           // Type 1 (RESTORE / SEEK / STEP) command issued
                {
                    _status &= ~(WDS_CRCERR | WDS_SEEKERR | WDS_WRITEPROTECTED);    // Reset error flags

                    _rqs = 0;

                    // Start motor for the selected disk drive
                    bool motorFlag = (_lastCmd & CMD_SEEK_HEADLOAD) || (_beta128 & BETA_COMMAND_BITS::BETA_CMD_DRIVE_MASK) ? _next + 2 * Z80_FREQUENCY : 0;
                    _selectedDrive->setMotor(motorFlag);

                    _state2 = S_SEEKSTART;  // Start with restore/seek by default

                    /// STEP has - 0b001x'xxxx code
                    /// STEP IN  - 0b010x'xxxx
                    /// STEP OUT - 0b011x'xxxx
                    /// So only those three type 1 commands will match. RESTORE and SEEK - won't
                    constexpr uint8_t const stepCommandMask = 0b1110'0000;
                    if (_lastCmd & stepCommandMask)
                    {
                        _state2 = S_STEP;

                        // Set step direction for STEP IN and STEP OUT commands (STEP command will use previous direction)
                        if (_lastDecodedCmd == WD_CMD_STEP_IN || _lastDecodedCmd == WD_CMD_STEP_OUT)
                        {
                            _stepDirection = (_lastCmd & CMD_SEEK_DIR) ? -1 : 1;
                        }
                    }

                    // Make 14us delay before FDC will report BUSY status
                    size_t delay = (14 * Z80_CLK_CYCLES_PER_MS) / 1000;
                    _next = _time + delay;
                    _state = S_WAIT;
                }
                break;
            case S_STEP:
                {
                    _status |= WDS_BUSY;

                    uint8_t track = _selectedDrive->getTrack();
                    if (track == 0 && _stepDirection < 0) // If we already at TRK00 and step out command issued - this is the limit
                    {
                        _track = 0;
                        _state = S_VERIFY;  // Next step will be - processing V bit (Verify) for the command if set
                    }
                    else
                    {
                        // Update Track register (RESTORE command - always, STEP IN / STEP OUT - only if bit4 (u flag) was set
                        if (_lastDecodedCmd == WD_CMD_RESTORE || _lastCmd & CMD_SEEK_TRKUPD)
                        {
                            _track += _stepDirection;
                        }

                        // Apply step changes to the drive state
                        if (_selectedDrive->getMotor())
                        {
                            int8_t track = _selectedDrive->getTrack();
                            track += _stepDirection;

                            if (track < 0)
                            {
                                track = 0;
                            } else if (track >= MAX_PHYSICAL_CYLINDER)
                            {
                                track = MAX_PHYSICAL_CYLINDER;
                            }

                            _selectedDrive->setTrack(track);
                        }

                        // Set step timings / delays
                        uint8_t rateIndex = _lastCmdValue & CMD_SEEK_RATE;
                        uint8_t rate = STEP_TIMINGS_1MHZ[rateIndex];
                        _next += rate * Z80_CLK_CYCLES_PER_MS;

                        // Next step will be:
                        // - S_VERIFY for STEP / STEP IN / STEP OUT commands
                        // - S_SEEK for SEEK and RESTORE commands
                        constexpr uint8_t const stepCommandMask = 0b1110'0000;
                        _state2 = (_lastCmd & stepCommandMask) ? S_VERIFY : S_SEEK;
                        _state = S_WAIT;
                    }
                }
                break;
            case S_SEEKSTART:
                _status |= WDS_BUSY;

                if (_lastDecodedCmd == WD_CMD_RESTORE)
                {
                    _state2 = S_RESTORE;
                    _next = _time + (Z80_CLK_CYCLES_PER_MS * 21) / 1000;   // 21us delay before Register Register load
                    _state = S_WAIT;
                }
                else
                {
                    _state = S_SEEK;
                }
                break;
            case S_RESTORE:
                _track = 0xFF;
                _data = 0;
                _state = S_SEEK;
                break;
            case S_SEEK:
                if (_data == _track) // If we already reached requested track - next step will be to handle verify flags (Bit2)
                {
                    _state = S_VERIFY;
                }
                else                // Otherwise - continue stepping in required direction
                {
                    _stepDirection = (_data < _track) ? -1 : 1;
                    _state = S_STEP;
                }
                break;
            case S_VERIFY:
                if (_lastCmd & CMD_SEEK_VERIFY)  // Verification is required (Bit2 in command set)
                {
                    _extStatus |= SIG_OUT_HLD;

                    // Wait for 15ms
                    //       |
                    //       v
                    // Wait for HLT == 1
                    //       |
                    //       v
                    // S_VERIFY2
                    _next += 15 * Z80_CLK_CYCLES_PER_MS;    // 15ms delay
                    _state2 = S_WAIT_HLT;
                }
                else                            // Verification is not required (Bit2 in command not set)
                {
                    _status |= WDS_BUSY;
                    _state2 = S_IDLE;
                    _state = S_WAIT;
                    _rotationCounter = _next + 15 * Z80_FREQUENCY / FDD::DISK_REVOLUTIONS_PER_SECOND;   // Disk will be spinning at least for additional 15 whole turns
                    _next += (105 * Z80_CLK_CYCLES_PER_MS) / 1000;  // Set 105 us delay
                }
                break;
            case S_WAIT_HLT:
                if (_beta128 & BETA_CMD_RESET)  // Reset is NOT active (active low)
                {
                    _state = S_VERIFY2;
                }
                break;
            case S_VERIFY2:
                // TODO: transfer code
                seekInDiskImage();  // Position within disk image
                findMarker();       // Find sector marker
                break;
            case S_EJECT1:  // Initiate eject
                _next = _time + 10 * Z80_CLK_CYCLES_PER_MS; // 10ms delay
                _state2 = S_EJECT2;
                _state = S_WAIT;
                break;
            case S_EJECT2:  // Continue the ejection after the delay
                _status &= ~WDS_WRITEPROTECTED;
                _ejectPending = false;
                _state = S_IDLE;
                break;
        }
    }

    /// endregion </<Main state machine>
}

/// Handle Beta128 interface system controller commands
/// @param value
void VG93::processBeta128(uint8_t value)
{
    // Set active drive, Bits[0,1] (0..3)
    _drive = value & 0b0000'0011;

    // Set side Bit[4] (0..1)
    _side = ~(value >> 4) & 0b0000'0001;

    // TODO: Select drive

    // Reset Bit[3] low
    bool reset = !(value & 0b0000'0100);

    if (reset)
    {
        _status &= ~WDS_NOTRDY;
        _rqs = INTRQ;
        _selectedDrive->setMotor(false);
        _indexPulseCounter = 0;

        // Set initial state after reset
        _state = S_TYPE1_CMD;
        _lastCmd =
        _lastDecodedCmd = WD_CMD_RESTORE;
    }
    else
    {
        uint8_t beta128ChangedBits = _beta128 ^ value;
        if (beta128ChangedBits & SYS_HLT) // When HLT signal positive edge (from 0 to 1) detected
        {
            // FIXME: index strobes should be set by disk rotation timings, not by HLT / BUSY edges
            if (!(_status & WDS_BUSY))
            {
                _indexPulseCounter++;
            }
        }

        _beta128 = value;
    }
}

void VG93::findMarker()
{
    seekInDiskImage();

    int headerIndex = -1;
    if (_selectedDrive->getMotor() && _selectedDrive->getRawData())
    {

    }
    else
    {
        // next = comp.t_states + cpu.t + 1;
    }

    _state = S_WAIT;
    _state2 = S_FOUND_NEXT_ID;
}

/// Emulate disk rotation and index strobe changes
void VG93::processIndexStrobe()
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

void VG93::seekInDiskImage()
{
    // seldrive->t.seek(seldrive, seldrive->track, side, LOAD_SECTORS);
}

void VG93::reset()
{
    _state = S_IDLE;
    _status = 0;
    _track = 0;
    _sector = 0;
    _data = 0;

    // Execute RESTORE command
    uint8_t restoreValue = 0b0000'1111;
    _lastDecodedCmd = WD_CMD_RESTORE;
    _lastCmd = restoreValue;
    _lastCmdValue = restoreValue;
    cmdRestore(restoreValue);
}

/// Initiate disk ejection sequence
void VG93::eject(uint8_t drive)
{
    _status |= WDS_WRITEPROTECTED;  // Write protection sensor is covered when disk is partially inserted / ejected
    _state = S_EJECT1;
    _ejectPending = true;

    // TODO: trigger floppy disk image unmount
}

/// Handles port #1F (CMD) port writes
/// @param value Command written
void VG93::processWD93Command(uint8_t value)
{
    static constexpr CommandHandler const commandTable[] =
    {
        &VG93::cmdRestore,
        &VG93::cmdSeek,
        &VG93::cmdStep,
        &VG93::cmdStepIn,
        &VG93::cmdStepOut,
        &VG93::cmdReadSector,
        &VG93::cmdWriteSector,
        &VG93::cmdReadAddress,
        &VG93::cmdReadTrack,
        &VG93::cmdWriteTrack,
        &VG93::cmdForceInterrupt
    };

    // Decode command
    VG93::WD_COMMANDS command = decodeWD93Command(value);
    uint8_t commandValue = getWD93CommandValue(command, value);

    // Persist information about command
    _lastCmd = value;
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
            _lastCmd = value;
            _status |= WDS_BUSY;
            _rqs = 0;
            _indexPulseCounter = 0;
            _rotationCounter = SIZE_MAX;

            // Call the corresponding command method
            (this->*handler)(commandValue);
        }
    }
}

/// Common status update for all Type 2 and 3 commands:
/// - Read Sector
/// - Write Sector
/// - Read Address
/// - Read Track
/// - Write Track
void VG93::updateStatusesForReadWrite()
{
    _status |= _status | WDS_BUSY;                                                         // Set BUSY status
    _status &= ~(WDS_DRQ | WDS_LOST | WDS_NOTFOUND | WDS_RECORDTYPE | WDS_WRITEPROTECTED); // Reset other status

    // Continue disk spinning
    //seldrive->motor = next + 2*Z80FQ;

    // Abort command if no disk detected
    if (_status & WDS_NOTRDY)
    {
        _state2 = S_IDLE;
        _state = S_WAIT;
        //next = comp.t_states + cpu.t + Z80FQ/FDD_RPS;
        _rqs = INTRQ;
    }
    else
    {
        _extStatus |= SIG_OUT_HLD;
        _state = S_DELAY_BEFORE_CMD;
    }
}

/// Common status update for all Type 1 seek commands:
/// - Restore
/// - Seek
/// - Step
/// - Step In
/// - Step Out
/// @param maskedValue
// If h = 1, the head is loaded at the beginning of the command and HLD output is made active
// If h = 0, HLD is deactivated
// Once the head is loaded, the head will remain engaged until controller receives a command
// that specifically disengages the head.
// Note: if controller is idle (busy = 0) for 15 revolutions of the disk, the head will be
// automatically disengaged (HLD made inactive)
//
// Type 1 commands also contain a verification (V) flag which determines if a verification
// operation is to take place on the destination track.
// If v = 1, a verification is performed
// If v = 0, no verification is performed
// During verification, the head is loaded and after an internal 15ms delay, the HLT input is
// sampled. When HLT is active (logic high), the first encountered ID field is read off the disk.
// The track address of the ID field is then compared to the Track Register.
// If there is a match and a valid ID CRC, the verification is complete, an interrupt is
// generated and Seek Error Status bit (bit4 in Status register) is reset.
void VG93::updateStatusesForSeek(uint8_t maskedValue)
{
    if (maskedValue & CMD_SEEK_HEADLOAD)    // h = 1 (Bit3), Head will be loaded until unload command or for 15 disk revolutions until timeout
    {
        _extStatus |= SIG_OUT_HLD;
    }
    else                                    // Head will be unloaded
    {
        _extStatus ^= ~SIG_OUT_HLD;
    }

    // FIXME: Seek operations should not be instantaneous. There should be a realistic delay
    _status &= ~WDS_BUSY;
    _state = S_TYPE1_CMD;
}

/// Restore (Seek track 0)
/// @param value
void VG93::cmdRestore(uint8_t value)
{
    std::cout << "Command Restore: " << static_cast<int>(value) << std::endl;

    updateStatusesForSeek(value);
}

/// This command assumes that Track Register contains the track number of the current position
/// of the Read-Write head and the Data Register contains the desired track number.
/// Controller will update the Track register and issue stepping pulses in the appropriate direction
/// until the contents of the Track register are equal to the contents of the Data Register.
/// A verification operation takes place if the V flag is on.
/// The h bit allows the head to be loaded at the start of the command.
/// An interrupt is generated at the completion of the command
/// @param value
void VG93::cmdSeek(uint8_t value)
{
    std::cout << "Command Seek: " << static_cast<int>(value) << std::endl;

    updateStatusesForSeek(value);
}

void VG93::cmdStep(uint8_t value)
{
    std::cout << "Command Step: " << static_cast<int>(value) << std::endl;

    updateStatusesForSeek(value);
}

void VG93::cmdStepIn(uint8_t value)
{
    std::cout << "Command Step In: " << static_cast<int>(value) << std::endl;

    updateStatusesForSeek(value);
}

void VG93::cmdStepOut(uint8_t value)
{
    std::cout << "Command Step Out: " << static_cast<int>(value) << std::endl;

    updateStatusesForSeek(value);
}

void VG93::cmdReadSector(uint8_t value)
{
    std::cout << "Command Read Sector: " << static_cast<int>(value) << std::endl;

    updateStatusesForReadWrite();
}

void VG93::cmdWriteSector(uint8_t value)
{
    std::cout << "Command Write Sector: " << static_cast<int>(value) << std::endl;

    updateStatusesForReadWrite();
}

void VG93::cmdReadAddress(uint8_t value)
{
    std::cout << "Command Read Address: " << static_cast<int>(value) << std::endl;

    updateStatusesForReadWrite();
}

void VG93::cmdReadTrack(uint8_t value)
{
    std::cout << "Command Read Track: " << static_cast<int>(value) << std::endl;

    updateStatusesForReadWrite();
}

void VG93::cmdWriteTrack(uint8_t value)
{
    std::cout << "Command Write Track: " << static_cast<int>(value) << std::endl;

    updateStatusesForReadWrite();
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
void VG93::cmdForceInterrupt(uint8_t value)
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



VG93::WD_COMMANDS VG93::decodeWD93Command(uint8_t value)
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

    VG93::WD_COMMANDS result = WD_CMD_RESTORE;

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
        result = (VG93::WD_COMMANDS)index;
    }

    return result;
}

uint8_t VG93::getWD93CommandValue(VG93::WD_COMMANDS command, uint8_t value)
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

/// endregion </Methods>

/// region <Helper methods>
uint8_t VG93::getStatusRegister()
{
    bool isType1Command = (_lastCmd & 0x80) == 0;

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

bool VG93::isReady()
{
    bool result = _selectedDrive->isDiskInserted();

    return result;
}
/// endregion </Helper methods>

/// region <PortDevice interface methods>

uint8_t VG93::portDeviceInMethod(uint16_t port)
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
            std::cout << dumpStatusRegister(_lastDecodedCmd);
            break;
        case PORT_3F:   // Return current track number
            result = _track;
            break;
        case PORT_5F:   // Return current sector number
            result = _sector;
            break;
        case PORT_7F:   // Return data byte and update internal state
            _rqs &= ~DRQ;
            result = _data;
            break;
        case PORT_FF:   // Handle BETA128 system port (#FF)
            result = _rqs | (_beta128 & 0x3F);
            break;
        default:
            break;
    }

    return result;
}

void VG93::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    MLOGINFO("Out port:0x04X, value: 0x%02X", port, value);

    // Update FDC internal states
    process();

    // Eject blocks command execution
    if (_ejectPending)
    {
        return;
    }

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:
            processWD93Command(value);
            break;
        case PORT_3F:
            _track = value;
            break;
        case PORT_5F:
            _sector = value;
            break;
        case PORT_7F:
            _data = value;
            _rqs &= ~DRQ;
            _status &= ~WDS_DRQ;
            break;
        case PORT_FF:
            processBeta128(value);
            break;
        default:
            break;
    }
}

/// endregion </PortDevice interface methods>

/// region <Ports interaction>

bool VG93::attachToPorts()
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

void VG93::detachFromPorts()
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
std::string VG93::dumpStatusRegister(WD_COMMANDS command)
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
/// endregion </Debug methods>