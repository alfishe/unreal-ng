#include "wd1793.h"

#include <sstream>

#include "common/dumphelper.h"
#include "common/filehelper.h"
#include "common/stringhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "wd1793_collector.h"

/// region <Constructors / destructors>

WD1793::WD1793(EmulatorContext* context) : PortDecoder(context)
{
    _context = context;
    _logger = context->pModuleLogger;

    _stateHandlers.resize(WDSTATE::WDSTATE_MAX);
    _stateHandlers[S_IDLE] = &WD1793::processIdle;
    _stateHandlers[S_WAIT] = &WD1793::processWait;
    _stateHandlers[S_FETCH_FIFO] = &WD1793::processFetchFIFO;
    _stateHandlers[S_STEP] = &WD1793::processStep;
    _stateHandlers[S_VERIFY] = &WD1793::processVerify;
    _stateHandlers[S_SEARCH_ID] = &WD1793::processSearchID;
    _stateHandlers[S_READ_SECTOR] = &WD1793::processReadSector;
    _stateHandlers[S_WRITE_SECTOR] = &WD1793::processWriteSector;
    _stateHandlers[S_READ_TRACK] = &WD1793::processReadTrack;
    _stateHandlers[S_WRITE_TRACK] = &WD1793::processWriteTrack;
    _stateHandlers[S_READ_BYTE] = &WD1793::processReadByte;
    _stateHandlers[S_WRITE_BYTE] = &WD1793::processWriteByte;
    _stateHandlers[S_READ_CRC] = &WD1793::processReadCRC;
    _stateHandlers[S_WRITE_CRC] = &WD1793::processWriteCRC;
    _stateHandlers[S_WAIT_INDEX] = &WD1793::processWaitIndex;
    _stateHandlers[S_END_COMMAND] = &WD1793::processEndCommand;

    // TODO: remove collector once WD1793 logic is fully implemented and tested
    _collector = new WD1793Collector();

    /// region <Create FDD instances

    for (size_t i = 0; i < 4; i++)
    {
        _context->coreState.diskDrives[i] = new FDD(_context);
    }

    /// endregion </Create FDD instances>

    // Set drive A: as default
    _selectedDrive = _context->coreState.diskDrives[0];

    reset();
}

WD1793::~WD1793()
{
    // Dump collected events file to disk
    std::string filename = FileHelper::GetExecutablePath() + "/wd1793_events.csv";
    _collector->dumpCollectedCommandInfo(filename);

    delete _collector;
    _collector = nullptr;

    // Note: Disk drives and disk images are owned by Emulator/CoreState
    // They are managed and deleted by the Emulator, not by WD1793
    // WD1793 only uses them via pointers stored in CoreState
}

/// endregion </Constructors / destructors>

/// region <Methods>

void WD1793::reset()
{
    // Reset all fields and states
    internalReset();
}

void WD1793::internalReset()
{
    // Clear operations FIFO
    std::queue<FSMEvent> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    // In-place re-initialization
    _wd93State = WD93State();

    // Clear FDC state
    _state = S_IDLE;
    _state2 = S_IDLE;
    _statusRegister = 0;
    _trackRegister = 0;
    _sectorRegister = 1;  // As per datasheet. Sector is set to 1 after the RESTORE command
    _dataRegister = 0;

    _indexPulseCounter = 0;
    _index = false;
    _prevIndex = false;
    _lastIndexPulseStartTime = 0;
    _motorTimeoutTStates = 0;
    _lastTime = 0;
    _diffTime = 0;

    _lastCmdValue = 0;
    _delayTStates = 0;
    _headLoaded = false;

    _time = 0;
    _lastTime = 0;
    _diffTime = 0;

    // Clear Force Interrupt condition monitoring
    _interruptConditions = 0;
    _prevReady = false;

    // Clear Type 2 command state
    _useDeletedDataMark = false;

    clearAllErrors();

    // Deassert output signals
    clearIntrq();
    clearDrq();

    // Start in sleep mode - will wake on first port access
    _sleeping = true;
    _wakeTimestamp = 0;
}

void WD1793::process()
{
    // Timing synchronization between host and FDC (specific for software emulation)
    processClockTimings();

    // Emulate disk rotation and index strobe changes
    processFDDIndexStrobe();

    // Maintain FDD motor state
    processFDDMotorState();

    // Monitor Force Interrupt conditions (I0, I1, I2)
    // I2 is handled in processFDDIndexStrobe(), I0/I1 are handled here
    processForceInterruptConditions();

    // Process counters and timeouts
    // - DRQ (Data Request) serving timeout
    // - RNF (Record Not Found)
    // - Seek error
    // - Index timeout
    processCountersAndTimeouts();

    /// region <HandlerMap as the replacement for lengthy switch()>

    // Get the handler for State1
    FSMHandler handler = _stateHandlers[_state];

    if (handler)  // Handler found
    {
        // Call the correspondent handler through the pointer or reference
        (this->*handler)();
    }
    else  // Handler is not available
    {
        MLOGERROR("No handler available for the state %d (%s)", _state, WDSTATEToString(_state).c_str());
    }

    /// endregion </HandlerMap as the replacement for lengthy switch()>
}

/// endregion </Methods>

/// region <Helper methods>

void WD1793::ejectDisk()
{
    if (_selectedDrive)
    {
        _selectedDrive->ejectDisk();
    }
}

/// Wake FDD from sleep mode (called on port access)
void WD1793::wakeUp()
{
    if (_sleeping)
    {
        _sleeping = false;
        updateTimeFromEmulatorState();
        _wakeTimestamp = _time;
        MLOGDEBUG("WD1793 waking up from sleep mode");
    }
}

/// Put FDD into sleep mode (called when idle for too long)
void WD1793::enterSleepMode()
{
    if (!_sleeping)
    {
        _sleeping = true;
        MLOGDEBUG("WD1793 entering sleep mode");
    }
}

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
        if (beta128ChangedBits & SYS_HLT)  // When HLT signal positive edge (from 0 to 1) detected
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

        _statusRegister |= WDS_NOTRDY;  // Set NOT READY status bit

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

/// Monitor Force Interrupt conditions I0 and I1 (ready state transitions)
/// I0: Not-Ready to Ready transition
/// I1: Ready to Not-Ready transition  
/// I2 is monitored in processFDDIndexStrobe()
void WD1793::processForceInterruptConditions()
{
    // Skip if no interrupt conditions are being monitored
    if (_interruptConditions == 0)
    {
        return;
    }

    // Get current ready state
    bool currentReady = isReady();
    
    // Check for I0: Not-Ready to Ready transition
    if (_interruptConditions & WD_FORCE_INTERRUPT_NOT_READY)
    {
        if (!_prevReady && currentReady)
        {
            MLOGINFO("Force Interrupt I0: INTRQ raised on Not-Ready->Ready transition");
            raiseIntrq();
            // Clear the I0 condition after triggering
            _interruptConditions &= ~WD_FORCE_INTERRUPT_NOT_READY;
        }
    }

    // Check for I1: Ready to Not-Ready transition
    if (_interruptConditions & WD_FORCE_INTERRUPT_READY)
    {
        if (_prevReady && !currentReady)
        {
            MLOGINFO("Force Interrupt I1: INTRQ raised on Ready->Not-Ready transition");
            raiseIntrq();
            // Clear the I1 condition after triggering
            _interruptConditions &= ~WD_FORCE_INTERRUPT_READY;
        }
    }

    // Update previous ready state for next cycle
    _prevReady = currentReady;
}

/// Emulate disk rotation and index strobe changes
void WD1793::processFDDIndexStrobe()
{
    // Based on:
    // - 300 revolutions per minute => 5 revolutions per second => 200ms or 1/5 second for single revolution
    // - base Z80 frequency 3.5MHz
    // We're getting 700'000 Z80 clock cycles period for each disk revolution / rotation
    static constexpr const size_t INDEX_STROBE_DURATION_IN_TSTATES =
        TSTATES_PER_MS * FDD::DISK_INDEX_STROBE_DURATION_MS;

    bool diskInserted = _selectedDrive && _selectedDrive->isDiskInserted();
    bool motorOn = _motorTimeoutTStates > 0 && _selectedDrive->getMotor();
    bool oldIndex = _index;

    if (diskInserted && motorOn)
    {
        // Calculate the current position in the disk rotation
        size_t diskRotationPhaseCounter = _time % DISK_ROTATION_PERIOD_TSTATES;

        // Index pulse is active for the first few ms of each revolution
        _index = (diskRotationPhaseCounter < INDEX_STROBE_DURATION_IN_TSTATES);

        // Detect rising edge of index pulse (transition from low to high)
        if (!_prevIndex && _index)
        {
            _indexPulseCounter++;

            MLOGDEBUG("Index pulse #%u detected at T-state: %llu. Motor timeout remaining: %d T-states (%.2f ms)",
                      _indexPulseCounter, _time, _motorTimeoutTStates, _motorTimeoutTStates * 1000.0 / Z80_FREQUENCY);

            // Force Interrupt I2 condition: generate interrupt on each index pulse
            if (_interruptConditions & WD_FORCE_INTERRUPT_INDEX_PULSE)
            {
                MLOGINFO("Force Interrupt I2: INTRQ raised on index pulse #%u", _indexPulseCounter);
                raiseIntrq();
                // Clear the I2 condition after triggering (per datasheet behavior)
                _interruptConditions &= ~WD_FORCE_INTERRUPT_INDEX_PULSE;
            }
        }

        // Debug log for index pulse state changes
        if (oldIndex != _index)
        {
            if (_index)
            {
                // Pulse started
                _lastIndexPulseStartTime = _time;
                MLOGDEBUG(">> Index pulse #%d started at T-state: %llu", _indexPulseCounter, _time);
            }
            else if (_lastIndexPulseStartTime > 0)
            {
                // Pulse ended - log duration
                uint64_t pulseDurationTStates = _time - _lastIndexPulseStartTime;
                double pulseDurationMs = pulseDurationTStates * 1000.0 / Z80_FREQUENCY;
                std::string formattedTStates =
                    StringHelper::FormatWithCustomThousandsDelimiter(pulseDurationTStates, '\'');

                MLOGDEBUG("<< Index pulse #%d ended at T-state: %llu (duration: %s T-states, %.2f ms)",
                          _indexPulseCounter, _time, formattedTStates.c_str(), pulseDurationMs);
            }
            else
            {
                MLOGDEBUG("Index pulse state changed at T-state: %llu (unknown duration)", _time);
            }
        }
    }
    else
    {
        _index = false;  // No index pulse when disk not inserted or motor off
    }

    // Update previous index state for edge detection in the next call
    _prevIndex = _index;

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

    // Debug: Log if we're not getting any index pulses
    // Use instance member to avoid race conditions across multiple WD1793 instances
    if (_time - _lastDebugLogTime > Z80_FREQUENCY)  // Log once per second
    {
        _lastDebugLogTime = _time;
        double milliseconds = (static_cast<double>(_time) * 1000.0) / Z80_FREQUENCY;
        MLOGDEBUG("Motor state: %s, Disk inserted: %s, Index pulses: %u, Time: %.2f ms (%llu T-states)",
                  motorOn ? "ON" : "OFF", diskInserted ? "YES" : "NO", _indexPulseCounter, milliseconds, _time);
    }
}

/// Process counters and timeouts
/// - DRQ (Data Request) serving timeout
/// - RNF (Record Not Found)
/// - Seek error
/// - Index timeout
void WD1793::processCountersAndTimeouts() {}

/// Ensure FDD motor is spinning and set default stop timeout
/// @details Condition for Timeout: The FDC is in an idle state (Busy status bit S0 = 0,
/// meaning no command is currently being executed).
/// Timeout Trigger: 15 Index Pulses (IP) have occurred while the FDC remains idle and the head is loaded (HLD is
/// active). Action: After the 15th Index Pulse is detected under these conditions, the FDC automatically de-asserts the
/// HLD (Head Load) signal, causing the R/W head to unload from the disk surface.
///
/// Since an Index Pulse occurs once per disk revolution:
///   The timeout is 15 disk revolutions.
void WD1793::prolongFDDMotorRotation()
{
    // Set motor timeout to 15 disk revolutions (3 seconds at 5 RPS)
    static constexpr const size_t REVOLUTIONS_FOR_TIMEOUT = 15;  // 15 revolutions at 5 RPS = 3 seconds
    static constexpr const size_t TSTATES_PER_REVOLUTION = Z80_FREQUENCY / FDD::DISK_REVOLUTIONS_PER_SECOND;
    _motorTimeoutTStates = REVOLUTIONS_FOR_TIMEOUT * TSTATES_PER_REVOLUTION;

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

    double milliseconds = ((double)_time * 1000.0) / Z80_FREQUENCY;
    MLOGINFO("FDD motor started: %.2f ms (T-states: %llu)", milliseconds, _time);
}

/// Stops FDD motor
void WD1793::stopFDDMotor()
{
    _selectedDrive->setMotor(false);
    _motorTimeoutTStates = 0;

    // Reset index pulse counter and related state when motor is stopped
    _index = false;
    _prevIndex = false;
    _lastIndexPulseStartTime = 0;

    MLOGINFO("FDD motor stopped");

#if defined(_DEBUG)
    {
        double milliseconds = ((double)_time * 1000.0) / Z80_FREQUENCY;
        MLOGINFO("FDD motor stopped: %.2f ms (T-states: %llu)", milliseconds, _time);
    }
#endif  // _DEBUG

    MLOGEMPTY();
    MLOGEMPTY();
}

/// Load head
void WD1793::loadHead()
{
    if (isType1Command(_commandRegister))
    {
        _statusRegister |= WDS_HEADLOADED;
    }

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

    _extStatus &= ~SIG_OUT_HLD;  // Unload read-write head
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
        _statusRegister &= ~(WDS_INDEX | WDS_TRK00 | WDS_SEEKERR | WDS_HEADLOADED | WDS_WRITEPROTECTED);

        // Update index strobe state according to rotation timings
        processFDDIndexStrobe();

        // Set Status Register bits specific for Type I commands
        if (_selectedDrive->isWriteProtect())
        {
            _statusRegister |= WDS_WRITEPROTECTED;
        }
        else
        {
            _statusRegister &= ~WDS_WRITEPROTECTED;
        };

        // Set head load state based on HLD and HLT signals
        uint8_t headStatus = ((_extStatus & SIG_OUT_HLD) && (_beta128Register & 0b0000'1000)) ? WDS_HEADLOADED : 0;
        if (headStatus)
        {
            _statusRegister |= headStatus;
        }
        else
        {
            _statusRegister &= ~headStatus;
        };

        if (_seek_error)
        {
            _statusRegister |= WDS_SEEKERR;
        }
        else
        {
            _statusRegister &= ~WDS_SEEKERR;
        };

        if (_crc_error)
        {
            _statusRegister |= WDS_CRCERR;
        }
        else
        {
            _statusRegister &= ~WDS_CRCERR;
        };

        if (_selectedDrive->isTrack00())
        {
            _statusRegister |= WDS_TRK00;
        }
        else
        {
            _statusRegister &= ~WDS_TRK00;
        };

        if (_index)
        {
            _statusRegister |= WDS_INDEX;
        }
        else
        {
            _statusRegister &= ~WDS_INDEX;
        };

        if (_index)
            _index = _index ? _index : false;
        else
            _index = _index ? _index : false;
    }
    else
    {
        // Type II or III command specific status register bits
        // Main differences:
        // - Bit 1 means DRQ (Data Request)
        // - Bit 2 means LOST DATA (CPU was unable to read or write the data from data register during 13us waiting
        // period)
        // - Bits 3 to 6 are specific for each command
        // - Bit 7 means NOT READY (FDC is not ready to accept commands)

        // Reset bits according each command requirements from the datasheet
        switch (_lastDecodedCmd)
        {
            case WD_CMD_READ_ADDRESS:
                _statusRegister &= 0b1001'1111;  // Reset bit5 and bit6 as stated in the datasheet

                if (_record_not_found)
                {
                    _statusRegister |= WDS_NOTFOUND;
                }
                else
                {
                    _statusRegister &= ~WDS_NOTFOUND;
                };
                if (_crc_error)
                {
                    _statusRegister |= WDS_CRCERR;
                }
                else
                {
                    _statusRegister &= ~WDS_CRCERR;
                };
                break;
            case WD_CMD_READ_SECTOR:
                _statusRegister &= 0b1011'1111;  // Reset bit 6 as stated in the datasheet

                // TODO: set WDS_RECORDTYPE
                if (_record_not_found)
                {
                    _statusRegister |= WDS_NOTFOUND;
                }
                else
                {
                    _statusRegister &= ~WDS_NOTFOUND;
                };
                if (_crc_error)
                {
                    _statusRegister |= WDS_CRCERR;
                }
                else
                {
                    _statusRegister &= ~WDS_CRCERR;
                };
                break;
            case WD_CMD_WRITE_SECTOR:
                // No bits reset => All bits are used for the Write Sector command

                if (_write_protect)
                {
                    _statusRegister |= WDS_WRITEPROTECTED;
                }
                else
                {
                    _statusRegister &= ~WDS_WRITEPROTECTED;
                };
                if (_write_fault)
                {
                    _statusRegister |= WDS_WRITEFAULT;
                }
                else
                {
                    _statusRegister &= ~WDS_WRITEFAULT;
                };
                if (_record_not_found)
                {
                    _statusRegister |= WDS_NOTFOUND;
                }
                else
                {
                    _statusRegister &= ~WDS_NOTFOUND;
                };
                if (_crc_error)
                {
                    _statusRegister |= WDS_CRCERR;
                }
                else
                {
                    _statusRegister &= ~WDS_CRCERR;
                };
                break;
            case WD_CMD_READ_TRACK:
                _statusRegister &= 0b1000'0111;  // Reset bits 3 to 6 as stated in the datasheet

                // DRQ bit means that a raw data byte, including gaps and sync marks, is placed into the Data Register
                break;
            case WD_CMD_WRITE_TRACK:
                _statusRegister &= 0b1110'0111;  // Reset bit3 and bit4 as stated in the datasheet

                if (_write_protect)
                {
                    _statusRegister |= WDS_WRITEPROTECTED;
                }
                else
                {
                    _statusRegister &= ~WDS_WRITEPROTECTED;
                };
                if (_write_fault)
                {
                    _statusRegister |= WDS_WRITEFAULT;
                }
                else
                {
                    _statusRegister &= ~WDS_WRITEFAULT;
                };
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

        if (_lost_data)
            _statusRegister |= WDS_LOSTDATA;
        if (_drq_out)
        {
            _statusRegister |= WDS_DRQ;
        }
        else
        {
            _statusRegister &= ~WDS_DRQ;
        }
    }

    // BUSY (bit 0) is driven by an FDC state machine and set directly during command processing

    if (_lastDecodedCmd == WD_CMD_SEEK && _trackRegister > 0 && _trackRegister == _dataRegister &&
        _selectedDrive->isTrack00())
        (void)_statusRegister;

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
    // - Step: The step command moves the read/write head in the direction previously specified (inwards or outwards) by
    // the "step in" or "step out" command.
    // - Step In: This command moves the read/write head one track towards the center of the disk.
    // - Step Out: This command moves the read/write head one track away from the center of the disk.
    // - Read Sector: This command allows you to read a single sector from the current track.
    // - Write Sector: The write sector command enables you to write data to a specified sector on the current track.
    // - Read Address: This command reads the address field (track number, side number, sector number) of the current
    // sector.
    // - Read Track: This command is used to read the entire contents of a track into the FDC's internal buffer.
    // - Write Track: The write track command allows you to write an entire track worth of data from the FDC's internal
    // buffer to the floppy disk.
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
    static constexpr uint8_t commandMasksMatches[WD93_COMMAND_COUNT][2] = {
        //    Mask   ,   Match
        {0b1111'0000, 0b0000'0000},  // [ 0] Restore          Match value: (  0, 0x00)
        {0b1111'0000, 0b0001'0000},  // [ 1] Seek             Match value: ( 16, 0x10)
        {0b1110'0000, 0b0010'0000},  // [ 2] Step             Match value: ( 32, 0x20)
        {0b1110'0000, 0b0100'0000},  // [ 3] Step In          Match value: ( 64, 0x40)
        {0b1110'0000, 0b0110'0000},  // [ 4] Step Out         Match value: ( 96, 0x60)
        {0b1110'0000, 0b1000'0000},  // [ 5] Read Sector      Match value: (128, 0x80)
        {0b1110'0000, 0b1010'0000},  // [ 6] Write Sector     Match value: (160, 0xA0)
        {0b1111'0000, 0b1100'0000},  // [ 7] Read Address     Match value: (192, 0xC0)
        {0b1111'0000, 0b1110'0000},  // [ 8] Read Track       Match value: (224, 0xE0)
        {0b1111'0000, 0b1111'0000},  // [ 9] Write Track      Match value: (240, 0xF0)
        {0b1111'0000, 0b1101'0000}   // [10] Force Interrupt. Match value: (208, 0xD0)
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
    static constexpr uint8_t commandMaskValues[WD93_COMMAND_COUNT] = {
        //    Mask
        0b0000'1111,  // [ 0] Restore
        0b0000'1111,  // [ 1] Seek
        0b0001'1111,  // [ 2] Step
        0b0001'1111,  // [ 3] Step In
        0b0001'1111,  // [ 4] Step Out
        0b0001'1110,  // [ 5] Read Sector
        0b0001'1111,  // [ 6] Write Sector
        0b0000'0100,  // [ 7] Read Address
        0b0000'0100,  // [ 8] Read Track
        0b0000'0100,  // [ 9] Write Track
        0b0000'1111,  // [10] Force Interrupt
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
    static constexpr CommandHandler const commandTable[] = {
        &WD1793::cmdRestore,   &WD1793::cmdSeek,       &WD1793::cmdStep,          &WD1793::cmdStepIn,
        &WD1793::cmdStepOut,   &WD1793::cmdReadSector, &WD1793::cmdWriteSector,   &WD1793::cmdReadAddress,
        &WD1793::cmdReadTrack, &WD1793::cmdWriteTrack, &WD1793::cmdForceInterrupt};

    // Decode command
    WD1793::WD_COMMANDS command = decodeWD93Command(value);
    uint8_t commandValue = getWD93CommandValue(command, value);

    // Persist information about command
    _commandRegister = value;
    _lastDecodedCmd = command;
    _lastCmdValue = commandValue;

    if (command < sizeof(commandTable) / sizeof(commandTable[0]))
    {
        // Register call in a collection
        _collector->recordCommandStart(*this, value);

        const CommandHandler& handler = commandTable[command];
        bool isBusy = _statusRegister & WDS_BUSY;

        if (command == WD_CMD_FORCE_INTERRUPT)  // Force interrupt command executes in any state
        {
            // Call the corresponding command method
            (this->*handler)(commandValue);
        }
        else if (!isBusy)  // All other commands are ignored if controller is busy
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
    std::string message =
        StringHelper::Format("Command Restore: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType1Command();

    // We're not sure about the current head position at this moment. Will be determined during step positioning
    _trackRegister = 0xFF;

    // Direction must always be out (towards Track 0)
    _stepDirectionIn = false;

    // Check if already at track 0 - if so, complete immediately
    if (_selectedDrive->isTrack00())
    {
        _trackRegister = 0;
        type1CommandVerify();
        return;
    }

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
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
    std::string message =
        StringHelper::Format("Command Seek: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    message += StringHelper::Format(" From trk: %d, to trk: %d", _trackRegister, _dataRegister);
    MLOGINFO(message.c_str());

    startType1Command();

    // Check if already at target track - if so, complete immediately
    if (_trackRegister == _dataRegister)
    {
        _selectedDrive->setTrack(_trackRegister);
        type1CommandVerify();
        return;
    }

    _stepDirectionIn = _dataRegister > _trackRegister;

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> ... -> S_STEP -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
}

/// Performs single head step movement remaining previously set direction
/// @param value STEP command parameter bits
void WD1793::cmdStep(uint8_t value)
{
    std::string message =
        StringHelper::Format("Command Step: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType1Command();

    // FSM will transition across steps (making required wait cycles as needed):
    // S_STEP -> S_VERIFY -> S_IDLE
    transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
}

void WD1793::cmdStepIn(uint8_t value)
{
    std::string message =
        StringHelper::Format("Command Step In: %d | %s", value, StringHelper::FormatBinary(value).c_str());
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
    std::string message =
        StringHelper::Format("Command Step Out: %d | %s", value, StringHelper::FormatBinary(value).c_str());
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
    std::string message =
        StringHelper::Format("Command Read Sector: %d | %s", value, StringHelper::FormatBinary(value).c_str());
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
    // Capture values to avoid dangling pointer issues when drive state changes
    FSMEvent readSector(WDSTATE::S_READ_SECTOR, [this, selectedDrive = _selectedDrive, trackReg = _trackRegister,
                                                 sectorReg = _sectorRegister, sideUp = _sideUp]() {
        // Validate pointers before use
        if (!selectedDrive || !selectedDrive->isDiskInserted())
        {
            this->_statusRegister |= WDS_NOTRDY;
            return;
        }

        DiskImage* diskImage = selectedDrive->getDiskImage();
        if (!diskImage)
        {
            this->_statusRegister |= WDS_NOTRDY;
            return;
        }

        DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(trackReg, sideUp);
        if (!track)
        {
            this->_statusRegister |= WDS_NOTFOUND;
            return;
        }

        uint8_t sectorIndex = sectorReg - 1;
        this->_sectorData = track->getDataForSector(sectorIndex);
        this->_rawDataBuffer = this->_sectorData;
        this->_bytesToRead = this->_sectorSize;
    });
    _operationFIFO.push(readSector);

    // Start FSM playback using FIFO queue
    transitionFSM(WDSTATE::S_FETCH_FIFO);
}

void WD1793::cmdWriteSector(uint8_t value)
{
    std::string message =
        StringHelper::Format("Command Write Sector: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType2Command();

    // Per WD1793 datasheet: Check write protect before writing
    // If write protected, terminate immediately with WP status
    if (_selectedDrive->isWriteProtect())
    {
        MLOGINFO("Write Sector: Write protect detected - command terminated");
        _statusRegister |= WDS_WRITEPROTECTED;
        _statusRegister &= ~WDS_BUSY;  // Clear BUSY since command is terminated
        _state = S_IDLE;
        _state2 = S_IDLE;
        raiseIntrq();  // Raise interrupt to signal completion
        return;
    }

    // Decode command bits:
    // Bit 0 (a0): 0 = Normal Data Mark (FB), 1 = Deleted Data Mark (F8)
    // Bit 4 (m):  0 = Single sector, 1 = Multiple sectors
    bool useDeletedDataMark = (value & CMD_WRITE_DEL) != 0;
    bool multiSector = (value & CMD_MULTIPLE) != 0;
    
    // Store the data mark type for use when sector writing completes
    _useDeletedDataMark = useDeletedDataMark;

    MLOGINFO("  Data Mark: %s, Multi-sector: %s", 
             useDeletedDataMark ? "Deleted (F8)" : "Normal (FB)",
             multiSector ? "Yes" : "No");

    // Step 1: search for ID address mark
    /*
    FSMEvent searchIDAM(WDSTATE::S_SEARCH_ID,
                        []() {}
                        );
    _operationFIFO.push(searchIDAM);
    */

    // Step 2: start sector writing (queue correspondent command to the FIFO)
    // Capture values to avoid dangling pointer issues when drive state changes
    FSMEvent writeSector(WDSTATE::S_WRITE_SECTOR, [this, selectedDrive = _selectedDrive, trackReg = _trackRegister,
                                                   sectorReg = _sectorRegister, sideUp = _sideUp]() {
        // Validate pointers before use
        if (!selectedDrive || !selectedDrive->isDiskInserted())
        {
            this->_statusRegister |= WDS_NOTRDY;
            return;
        }

        DiskImage* diskImage = selectedDrive->getDiskImage();
        if (!diskImage)
        {
            this->_statusRegister |= WDS_NOTRDY;
            return;
        }

        DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(trackReg, sideUp);
        if (!track)
        {
            this->_statusRegister |= WDS_NOTFOUND;
            return;
        }

        this->_sectorData = track->getDataForSector(sectorReg - 1);
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
    std::string message =
        StringHelper::Format("Command Read Address: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType3Command();

    // Step 1: search for ID address mark
    FSMEvent searchIDAM(WDSTATE::S_SEARCH_ID, []() {});
    _operationFIFO.push(searchIDAM);

    // Step 2: start IDAM read (6 bytes)
    FSMEvent readIDAM(WDSTATE::S_READ_BYTE, [this]() {
        this->_bytesToRead = 6;
        this->_rawDataBuffer = this->_idamData;
    });
    _operationFIFO.push(readIDAM);

    // Start FSM playback using FIFO queue
    transitionFSM(WDSTATE::S_FETCH_FIFO);
}

void WD1793::cmdReadTrack(uint8_t value)
{
    std::string message =
        StringHelper::Format("Command Read Track: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType3Command();

    // Get raw track data pointer - validate early
    DiskImage* diskImage = _selectedDrive->getDiskImage();
    if (!diskImage)
    {
        _statusRegister |= WDS_NOTRDY;
        transitionFSM(S_END_COMMAND);
        return;
    }

    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(_trackRegister, _sideUp);
    if (!track)
    {
        _statusRegister |= WDS_NOTFOUND;
        transitionFSM(S_END_COMMAND);
        return;
    }

    uint8_t* rawTrackData = track->getRawTrackData(_trackRegister, _sideUp);
    if (!rawTrackData)
    {
        // Handle error - invalid track
        _statusRegister |= WDS_NOTRDY;
        transitionFSM(S_END_COMMAND);
        return;
    }

    // Capture values into the lambda to avoid dangling pointer issues when drive state changes
    FSMEvent readTrack(WDSTATE::S_READ_TRACK,
                       [this, selectedDrive = _selectedDrive, trackReg = _trackRegister, sideUp = _sideUp]() {
                           // Validate pointers before use
                           if (!selectedDrive || !selectedDrive->isDiskInserted())
                           {
                               this->_statusRegister |= WDS_NOTRDY;
                               return;
                           }

                           DiskImage* diskImage = selectedDrive->getDiskImage();
                           if (!diskImage)
                           {
                               this->_statusRegister |= WDS_NOTRDY;
                               return;
                           }

                           DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(trackReg, sideUp);
                           if (!track)
                           {
                               this->_statusRegister |= WDS_NOTFOUND;
                               return;
                           }

                           uint8_t* rawTrackData = track->getRawTrackData(trackReg, sideUp);
                           if (!rawTrackData)
                           {
                               this->_statusRegister |= WDS_NOTRDY;
                               return;
                           }

                           _bytesToRead = DiskImage::RawTrack::RAW_TRACK_SIZE;  // 6250 bytes
                           _rawDataBuffer = rawTrackData;
                       });
    _operationFIFO.push(readTrack);

    // Per WD1793 datasheet: Wait for index pulse before starting to read
    // Store the target state in _state2 for processWaitIndex to use
    _state2 = S_READ_TRACK;
    
    // Transition to wait for index pulse
    transitionFSM(WDSTATE::S_WAIT_INDEX);
}

void WD1793::cmdWriteTrack(uint8_t value)
{
    std::string message =
        StringHelper::Format("Command Write Track: %d | %s", value, StringHelper::FormatBinary(value).c_str());
    MLOGINFO(message.c_str());

    startType3Command();

    // Check write protect first (per datasheet)
    if (_selectedDrive->isWriteProtect())
    {
        _statusRegister |= WDS_WRITEPROTECTED;
        MLOGINFO("Write Track: Disk is write protected");
        transitionFSM(S_END_COMMAND);
        return;
    }

    // Get track for writing - validate early
    DiskImage* diskImage = _selectedDrive->getDiskImage();
    if (!diskImage)
    {
        _statusRegister |= WDS_NOTRDY;
        transitionFSM(S_END_COMMAND);
        return;
    }

    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(_trackRegister, _sideUp);
    if (!track)
    {
        _statusRegister |= WDS_NOTFOUND;
        transitionFSM(S_END_COMMAND);
        return;
    }

    // Set DRQ to request first byte from host (per datasheet: "wait for DRQ service")
    raiseDrq();

    // Capture values into the lambda to avoid dangling pointer issues
    FSMEvent writeTrack(WDSTATE::S_WRITE_TRACK,
                        [this, selectedDrive = _selectedDrive, trackReg = _trackRegister, sideUp = _sideUp]() {
                            // Validate pointers before use
                            if (!selectedDrive || !selectedDrive->isDiskInserted())
                            {
                                this->_statusRegister |= WDS_NOTRDY;
                                return;
                            }

                            DiskImage* diskImage = selectedDrive->getDiskImage();
                            if (!diskImage)
                            {
                                this->_statusRegister |= WDS_NOTRDY;
                                return;
                            }

                            DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(trackReg, sideUp);
                            if (!track)
                            {
                                this->_statusRegister |= WDS_NOTFOUND;
                                return;
                            }

                            _bytesToWrite = DiskImage::RawTrack::RAW_TRACK_SIZE;  // 6250 bytes
                            _rawDataBuffer = reinterpret_cast<uint8_t*>(&track->sectors[0]);
                            _rawDataBufferIndex = 0;
                            _crcAccumulator = 0xCDB4;  // WD1793 CRC preset value (after 3x A1 sync bytes)
                            _writeTrackTarget = track;  // Store track for reindexing on completion
                        });
    _operationFIFO.push(writeTrack);

    // Per WD1793 datasheet: Wait for index pulse before starting to write
    // Store the target state in _state2 for processWaitIndex to use
    _state2 = S_WRITE_TRACK;

    // Transition to wait for index pulse
    transitionFSM(WDSTATE::S_WAIT_INDEX);
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
// Bit3 (J3) = 1 - Immediate interrupt
// If all bits [0:3] are not set (= 0) - terminate with no interrupt
/// @details Upon receipt of any command, except the Force Interrupt command, the Busy Status bit is set and the
/// rest of the status bits are updated or cleared for the new command.
///
/// If the Force Interrupt Command is received when there is a current command under execution,
/// the Busy status bit is reset, and the rest of the status bits are unchanged.
///
/// If the Force Interrupt command is received when there is not a current command under execution, the Busy Status bit
/// is reset and the rest of the status bits are updated or cleared. In this case, Status reflects the Type I commands.
void WD1793::cmdForceInterrupt(uint8_t value)
{
    MLOGINFO("Command Force Interrupt: 0x%02X | %s", value, StringHelper::FormatBinary(value).c_str());

    bool noCommandExecuted = _state == S_IDLE;
    WDSTATE prevState = _state;
    [[maybe_unused]] WDSTATE prevState2 = _state2;

    // Terminate any current command - always happens regardless of I0-I3 flags
    _state = S_IDLE;
    _state2 = S_IDLE;
    _delayTStates = 0;
    _statusRegister &= ~WDS_BUSY;
    clearDrq();

    // Clear operations FIFO
    std::queue<FSMEvent> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    // Extract only the interrupt condition bits (I0-I3)
    value &= 0b0000'1111;

    // Store interrupt conditions for monitoring (I0, I1, I2)
    // I3 is handled immediately, so we only store I0-I2
    _interruptConditions = value & 0b0000'0111;  // Mask out I3, keep I0, I1, I2

    if (value == 0)  // $D0 - Terminate with NO interrupt
    {
        // Per datasheet: "If I0-I3 = 0, there is no interrupt generated but the current 
        // command is terminated and busy is reset."
        // DO NOT raise INTRQ - this is the key behavior for $D0
        MLOGINFO("Force Interrupt $D0: Terminate without INTRQ");
    }
    else
    {
        // At least one interrupt condition is set
        
        if (value & WD_FORCE_INTERRUPT_IMMEDIATE)  // Bit3 (I3) - Immediate interrupt
        {
            // Immediate interrupt - generate INTRQ now
            raiseIntrq();
            MLOGINFO("Force Interrupt I3: Immediate interrupt");
        }
        else
        {
            // I0, I1, or I2 set - interrupt will be generated when condition occurs
            // The actual interrupt generation happens in process() via monitoring
            
            if (value & WD_FORCE_INTERRUPT_INDEX_PULSE)  // Bit2 (I2)
            {
                MLOGINFO("Force Interrupt I2: Interrupt on next index pulse");
            }
            
            if (value & WD_FORCE_INTERRUPT_READY)  // Bit1 (I1)
            {
                // Store current ready state for transition detection
                MLOGINFO("Force Interrupt I1: Interrupt on Ready->Not-Ready transition");
            }
            
            if (value & WD_FORCE_INTERRUPT_NOT_READY)  // Bit0 (I0)
            {
                // Store current ready state for transition detection
                MLOGINFO("Force Interrupt I0: Interrupt on Not-Ready->Ready transition");
            }
        }
    }

    // Update status register based on whether a command was executing
    if (noCommandExecuted)
    {
        // Per datasheet: "If the Force Interrupt command is received when there is not a current 
        // command under execution, the Busy Status bit is reset and the rest of the status bits 
        // are updated or cleared. In this case, Status reflects the Type I commands."
        _statusRegister &= ~(WDS_CRCERR | WDS_SEEKERR | WDS_HEADLOADED);
        _statusRegister |= !_selectedDrive->isDiskInserted() ? WDS_NOTRDY : 0x00;
        _statusRegister |= _selectedDrive->isWriteProtect() ? WDS_WRITEPROTECTED : 0x00;

        _statusRegister |= _selectedDrive->getTrack() == 0 ? WDS_TRK00 : 0x00;
        if (_index)
        {
            _statusRegister |= WDS_INDEX;
        }
        else
        {
            _statusRegister &= ~WDS_INDEX;
        };

        MLOGINFO("<<== FORCE_INTERRUPT, no active command");
        MLOGINFO("  Status: %s", StringHelper::FormatBinary(_statusRegister).c_str());
        MLOGINFO("  Beta128: %s\n", StringHelper::FormatBinary(_beta128status).c_str());
    }
    else
    {
        // Per datasheet: "If the Force Interrupt Command is received when there is a current 
        // command under execution, the Busy status bit is reset, and the rest of the status 
        // bits are unchanged."
        MLOGINFO("<<== FORCE_INTERRUPT, command interrupted");
        MLOGINFO("  Command: %s", getWD_COMMANDName(_lastDecodedCmd));
        MLOGINFO("  State: %s, State2: %s\n", WDSTATEToString(prevState).c_str(), WDSTATEToString(prevState2).c_str());
    }
}

void WD1793::startType1Command()
{
    MLOGINFO("==>> Start Type 1 command (%s)", getWD_COMMANDName(_lastDecodedCmd));

    _statusRegister |= WDS_BUSY;                     // Set BUSY flag
    _statusRegister &= ~(WDS_SEEKERR | WDS_CRCERR);  // Clear positioning and CRC errors

    // Clear Data Request and Interrupt request bits before starting command processing
    clearDrq();
    clearIntrq();
    clearAllErrors();

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

    // Fully clear Status Register;
    _statusRegister = 0x00;

    // Set required Status Register flags
    _statusRegister |= WDS_BUSY;
    if (!_selectedDrive || !_selectedDrive->isDiskInserted())
        _statusRegister |= WDS_NOTRDY;

    // Type2 commands have timeout for data availability in Data Register
    _drq_served = false;

    // Clear Data Request and Interrupt request bits before starting command processing
    clearDrq();
    clearIntrq();
    clearAllErrors();

    if (!isReady())
    {
        // If the drive is not ready - end immediately
        transitionFSM(WD1793::S_END_COMMAND);
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

    // Fully clear Status Register;
    _statusRegister = 0x00;

    // Set required Status Register flags
    _statusRegister |= WDS_BUSY;
    if (!_selectedDrive || !_selectedDrive->isDiskInserted())
        _statusRegister |= WDS_NOTRDY;

    // Type2 commands have timeout for data availability in Data Register
    _drq_served = false;

    // Clear Data Request and Interrupt request bits before starting command processing
    clearDrq();
    clearIntrq();
    clearAllErrors();

    if (!isReady())
    {
        // If the drive is not ready - end immediately
        transitionFSM(WD1793::S_END_COMMAND);
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
    _statusRegister &= ~WDS_BUSY;  // Reset BUSY flag
    raiseIntrq();                  // INTRQ must be set at a completion of any command

    // Notify collector about command completion
    _collector->recordCommandEnd(*this);

    // Clear FIFO
    std::queue<FSMEvent> emptyQueue;
    _operationFIFO.swap(emptyQueue);

    // Debug logging
    MLOGINFO("<<== End command (%s). status: %s beta128: %s", getWD_COMMANDName(_lastDecodedCmd),
             StringHelper::FormatBinary(_statusRegister).c_str(), StringHelper::FormatBinary(_beta128status).c_str());
}

/// Helper for Type1 command states to execute VERIFY using unified approach
void WD1793::type1CommandVerify()
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
        transitionFSM(WD1793::S_END_COMMAND);
    }
}

/// endregion </Command handling>

/// region <State machine handlers>

/// @brief Processes idle state in the WD1793 controller FSM
/// @details Clears the busy flag in the status register when the controller
///          is in idle state and not executing any commands
void WD1793::processIdle()
{
    _statusRegister &= ~WDS_BUSY;  // Remove busy flag
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
        // Get the next FIFO value (and remove it from the queue immediately)
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
        transitionFSM(WD1793::S_END_COMMAND);
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

        transitionFSM(WD1793::S_END_COMMAND);
        return;
    }
    else
    {
        _stepCounter++;
    }
    /// endregion </Check for step limits>

    // Early termination checks (before any stepping occurs)
    // RESTORE: Check if already at track 0 via TR00 signal
    if (_lastDecodedCmd == WD_CMD_RESTORE && _selectedDrive->isTrack00())
    {
        _trackRegister = 0;
        _selectedDrive->setTrack(0);
        type1CommandVerify();
        return;
    }
    // SEEK: Check if already at target track (trackRegister == dataRegister)
    if (_lastDecodedCmd == WD_CMD_SEEK && _trackRegister == _dataRegister)
    {
        _selectedDrive->setTrack(_trackRegister);
        type1CommandVerify();
        return;
    }

    /// region <Make head step>
    int8_t stepCorrection = _stepDirectionIn ? 1 : -1;

    // Apply changes to the WD1793 Track Register
    // SEEK/RESTORE always update Track Register; STEP commands only if 'u' bit is set
    if (_lastDecodedCmd == WD_CMD_SEEK || _lastDecodedCmd == WD_CMD_RESTORE ||
        (_lastCmdValue & 0x10))
    {
        _trackRegister += stepCorrection;
    }

    // Apply changes to FDD state (physical movement always happens)
    uint8_t fddTrack = _selectedDrive->getTrack();
    fddTrack += stepCorrection;
    _selectedDrive->setTrack(fddTrack);

    /// endregion </Make head step>

    // Check for track 0
    if (!_stepDirectionIn && _selectedDrive->isTrack00())  // RESTORE or SEEK 0 command finished
    {
        // We've reach Track 0. No further movements
        _trackRegister = 0;

        // Check if position verification was requested
        type1CommandVerify();
    }
    else if (_lastDecodedCmd == WD_CMD_SEEK && _dataRegister == _trackRegister)  // SEEK command finished
    {
        // Apply track change to selected FDD
        _selectedDrive->setTrack(_trackRegister);

        // Check if position verification was requested
        type1CommandVerify();
    }
    else
    {
        // Continue positioning if required
        if (_lastDecodedCmd == WD_CMD_RESTORE || _lastDecodedCmd == WD_CMD_SEEK)
        {
            // Schedule next step according currently selected stepping motor rate
            transitionFSMWithDelay(WDSTATE::S_STEP, _steppingMotorRate * TSTATES_PER_MS);
        }
        else if (_lastDecodedCmd == WD_CMD_STEP || _lastDecodedCmd == WD_CMD_STEP_IN ||
                 _lastDecodedCmd == WD_CMD_STEP_OUT)
        {
            // Check if position verification was requested
            type1CommandVerify();
        }
        else
        {
            // int a = a;
            // throw std::logic_error("Only Type 1 commands can have S_STEP state");
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
    transitionFSM(WD1793::S_END_COMMAND);
}

/// Attempt to find next ID Address Mark on current track
void WD1793::processSearchID()
{
    DiskImage* diskImage = _selectedDrive->getDiskImage();

    // Use the current FDD track, not WD1793 track register!
    DiskImage::Track* track = diskImage->getTrackForCylinderAndSide(_selectedDrive->getTrack(), _sideUp ? 1 : 0);

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
        _idamData = (uint8_t*)track->getIDForSector(_sectorRegister) + 1;  // We need to skip id_address_mark = 0xFE
        _sectorData = track->getDataForSector(_sectorRegister);

        // TODO: apply the delay related to disk rotation so searching for ID Address Mark may take up to a full disk
        // revolution

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

        raiseRecordNotFound();
        _statusRegister |= WDS_NOTFOUND;
        transitionFSM(S_END_COMMAND);
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
        FSMEvent readSector(WDSTATE::S_READ_SECTOR, [this]() {
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
        });

        _operationFIFO.push(readSector);
    }

    // Start reading sector bytes
    transitionFSM(WD1793::S_READ_BYTE);
}

/// Handles read single byte for sector or track operations
/// _rawDataBuffer and _bytesToRead values must be set before reading the first byte
void WD1793::processReadByte()
{
    // TODO: implement DRQ serve time timeout
    if (false && !_drq_served)
    {
        // Data was not fetched by CPU from Data Register
        // Set LOST_DATA error and terminate
        _statusRegister |= WDS_LOSTDATA;
        transitionFSM(WDSTATE::S_END_COMMAND);
        return;
    }

    // Validate buffer pointer before use (multi-instance safety)
    if (!_rawDataBuffer)
    {
        _statusRegister |= WDS_NOTRDY;
        transitionFSM(WDSTATE::S_END_COMMAND);
        return;
    }

    // Reset Data Register access flag
    _drq_served = false;
    clearDrq();

    // Read the next byte from the raw data buffer
    _dataRegister = *(_rawDataBuffer++);
    _bytesToRead--;

    // Byte is ready to be consumed by the host
    raiseDrq();

    if (_bytesToRead > 0)
    {
        // Transition to the next byte read state
        transitionFSMWithDelay(WDSTATE::S_READ_BYTE, WD93_TSTATES_PER_FDC_BYTE);
    }
    else
    {
        // We still need to give host time to read the byte
        transitionFSMWithDelay(WDSTATE::S_END_COMMAND, WD93_TSTATES_PER_FDC_BYTE);
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
        FSMEvent writeSector(WDSTATE::S_WRITE_SECTOR, [this]() {
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
        });

        _operationFIFO.push(writeSector);
    }

    transitionFSM(WD1793::S_WRITE_BYTE);
}

void WD1793::processWriteByte()
{
    // Lost Data detection: if host didn't provide data in time, set error and terminate
    // Per WD1793 datasheet: If DRQ is not serviced in time, data is lost
    if (_drq_out && !_drq_served)
    {
        // Data was not provided by CPU to Data Register in time
        // Set LOST_DATA error and terminate
        MLOGWARNING("Write Sector: Lost Data - DRQ not serviced in time");
        _statusRegister |= WDS_LOSTDATA;
        raiseLostData();
        transitionFSM(WDSTATE::S_END_COMMAND);
        return;
    }

    if (_rawDataBuffer)
    {
        // Reset Data Register access flag
        _drq_served = false;

        // Put the next byte to write from the Data Register
        *(_rawDataBuffer++) = _dataRegister;
        _bytesToWrite--;

        if (_bytesToWrite > 0)
        {
            // Raise DRQ to request the next byte from the host
            raiseDrq();
            
            // Transition to next byte write state
            transitionFSMWithDelay(WD1793::S_WRITE_BYTE, WD93_TSTATES_PER_FDC_BYTE);
        }
        else
        {
            // No more bytes to write
            // Clear DRQ since DR is no longer empty
            _statusRegister &= ~WDS_DRQ;
            clearDrq();

            /// region <Set WDS_RECORDTYPE bit depending on Data Address Mark>

            // Use the data mark type that was set when the command was issued
            // Status bit 5:
            // 1 - Deleted Data Mark (F8)
            // 0 - Data Mark (FB)
            if (_useDeletedDataMark)
            {
                _statusRegister |= WDS_RECORDTYPE;
            }
            else
            {
                _statusRegister &= ~WDS_RECORDTYPE;
            }
            /// endregion </Set WDS_RECORDTYPE bit depending on Data Address Mark>

            // TODO: Recalculate CRC for the written sector data
            // The disk image should update the CRC in the sector's ID field

            // Fetch the next command from fifo (or end if no more commands left)
            transitionFSM(WDSTATE::S_FETCH_FIFO);
        }
    }
    else
    {
        // For some reason data not available - treat it as NOT READY and abort
        _statusRegister |= WDS_NOTRDY;
        transitionFSM(WDSTATE::S_END_COMMAND);
    }
}

void WD1793::processReadTrack()
{
    _bytesToRead = DiskImage::RawTrack::RAW_TRACK_SIZE;

    transitionFSM(WD1793::S_READ_BYTE);
}

/// @brief Process Write Track (Format) command - writes raw track data from index to index
/// Per WD1793 datasheet: Special control bytes are handled:
/// - F5 (MFM): Write A1 with missing clock, preset CRC
/// - F6 (MFM): Write C2 with missing clock
/// - F7: Write 2 CRC bytes (calculated)
/// - F8-FB: Write Data Address Mark (F8=deleted, FB=normal)
/// - FC: Write Index Address Mark
/// - FE: Write ID Address Mark, preset CRC
/// All other values are written literally
void WD1793::processWriteTrack()
{
    // Check if DRQ was serviced - if not, it's Lost Data
    if (_drq_out && !_drq_served)
    {
        // Host didn't provide data in time - Lost Data
        _statusRegister |= WDS_LOSTDATA;
        MLOGWARNING("Write Track: Lost Data - DRQ not serviced");
        transitionFSM(S_END_COMMAND);
        return;
    }

    // Check if we've written all bytes (6250)
    if (_bytesToWrite <= 0 || _rawDataBufferIndex >= DiskImage::RawTrack::RAW_TRACK_SIZE)
    {
        MLOGINFO("Write Track complete: %zu bytes written", _rawDataBufferIndex);
        
        // Reindex sector structure by parsing raw MFM data
        // This uses MFMParser to find IDAMs and rebuild sectorsOrderedRef[]
        if (_writeTrackTarget)
        {
            auto result = _writeTrackTarget->reindexFromMFM();
            
            // Log validation result
            if (result.passed)
            {
                MLOGINFO("Write Track MFM validation: PASSED (%zu/16 sectors)", 
                         result.parseResult.validSectors);
            }
            else
            {
                MLOGWARNING("Write Track MFM validation: FAILED (%zu/16 sectors, %zu errors)",
                            result.parseResult.validSectors, result.issues.size());
                // Detailed report available via result.report()
            }
            
            _writeTrackTarget = nullptr;  // Clear after use
        }
        
        transitionFSM(S_END_COMMAND);
        return;
    }

    // Get the byte from data register
    uint8_t dataByte = _dataRegister;
    uint8_t byteToWrite = dataByte;

    // Handle special format control bytes (MFM mode)
    switch (dataByte)
    {
        case 0xF5:
            // Write A1 with missing clock (sync byte for MFM)
            // Also presets CRC to initial value
            byteToWrite = 0xA1;
            _crcAccumulator = 0xCDB4;  // Preset CRC
            break;

        case 0xF6:
            // Write C2 with missing clock
            byteToWrite = 0xC2;
            break;

        case 0xF7:
            // Generate and write 2 CRC bytes
            // Write CRC high byte, then low byte
            if (_rawDataBufferIndex < DiskImage::RawTrack::RAW_TRACK_SIZE)
            {
                _rawDataBuffer[_rawDataBufferIndex++] = static_cast<uint8_t>(_crcAccumulator >> 8);
                _bytesToWrite--;
            }
            if (_rawDataBufferIndex < DiskImage::RawTrack::RAW_TRACK_SIZE)
            {
                _rawDataBuffer[_rawDataBufferIndex++] = static_cast<uint8_t>(_crcAccumulator & 0xFF);
                _bytesToWrite--;
            }
            // CRC bytes written - request next data
            raiseDrq();
            return;  // Don't write a third byte

        case 0xF8:
        case 0xF9:
        case 0xFA:
        case 0xFB:
            // Data Address Mark bytes - written literally
            // F8 = Deleted Data Mark, FB = Normal Data Mark
            byteToWrite = dataByte;
            break;

        case 0xFC:
            // Index Address Mark
            byteToWrite = 0xFC;
            break;

        case 0xFE:
            // ID Address Mark - preset CRC after writing
            byteToWrite = 0xFE;
            _crcAccumulator = 0xCDB4;  // Preset CRC
            break;

        default:
            // Normal data byte - written literally
            byteToWrite = dataByte;
            break;
    }

    // Write the byte to the raw track buffer
    if (_rawDataBufferIndex < DiskImage::RawTrack::RAW_TRACK_SIZE)
    {
        _rawDataBuffer[_rawDataBufferIndex++] = byteToWrite;
        _bytesToWrite--;

        // Update CRC for non-F7 bytes
        if (dataByte != 0xF7)
        {
            // Simple CRC update (matching WD1793 algorithm)
            _crcAccumulator = (_crcAccumulator << 8) ^ 
                (((_crcAccumulator >> 8) ^ byteToWrite) << 8);  // Simplified - accurate CRC is complex
        }
    }

    // Request next byte
    raiseDrq();
}

void WD1793::processReadCRC()
{
    MLOGDEBUG("processReadCRC");

    _bytesToRead = 2;

    // Use regular data read flog. DRQ will be asserted on CRC bytes as well
    transitionFSM(WD1793::S_READ_BYTE);
}

void WD1793::processWriteCRC()
{
    MLOGDEBUG("processWriteCRC");

    // We should not assert DRQ data requests, so just write 2 bytes of CRC silently
    _bytesToWrite = 2;

    // Make delay for 2 bytes transfer and end command execution
    transitionFSMWithDelay(WD1793::S_END_COMMAND, WD93_TSTATES_PER_FDC_BYTE * _bytesToWrite);
    ;
}

/// @brief Wait for index pulse (rising edge) before starting track read/write
/// Per WD1793 datasheet: Read Track and Write Track wait for index pulse before starting
void WD1793::processWaitIndex()
{
    // Use internal counter tracking: _waitIndexPulseCount is set when entering S_WAIT_INDEX
    // When _indexPulseCounter increments past our stored value, a new index pulse was detected
    // Note: SIZE_MAX indicates uninitialized state (first call)
    
    // If _waitIndexPulseCount hasn't been set (first call), store current counter + 1
    // We add 1 to wait for the NEXT pulse, not the current one
    if (_waitIndexPulseCount == SIZE_MAX)
    {
        _waitIndexPulseCount = _indexPulseCounter;
        MLOGDEBUG("S_WAIT_INDEX: Waiting for next index pulse (current count: %zu)", _waitIndexPulseCount);
        return;
    }
    
    // Check if a new index pulse occurred (counter incremented)
    if (_indexPulseCounter > _waitIndexPulseCount)
    {
        MLOGINFO("Index pulse detected (count: %zu -> %zu) - starting track operation", 
                 _waitIndexPulseCount, _indexPulseCounter);
        
        // Reset wait counter for next use
        _waitIndexPulseCount = SIZE_MAX;
        
        // Fetch from FIFO to set up track buffer and proceed
        transitionFSM(S_FETCH_FIFO);
    }
    // If no new pulse detected, stay in S_WAIT_INDEX state and wait
}

void WD1793::processEndCommand()
{
    endCommand();

    // Transition to IDLE state
    transitionFSM(S_IDLE);
}

/// endregion </State machine handlers>

/// region <Emulation events>
void WD1793::handleFrameStart()
{
    // Nothing to do here
}

void WD1793::handleStep()
{
    // Skip processing if sleeping - major CPU optimization when FDD is idle
    if (_sleeping)
    {
        return;
    }

    // Check if we should enter sleep mode (idle for too long with motor off)
    if (_state == S_IDLE && _motorTimeoutTStates == 0)
    {
        updateTimeFromEmulatorState();
        if (_time - _wakeTimestamp > SLEEP_AFTER_IDLE_TSTATES)
        {
            enterSleepMode();
            return;
        }
    }

    // We need better precision to read data from the disk at 112 t-states per byte rate, so update FSM state after each
    // CPU command execution
    process();
}

void WD1793::handleFrameEnd()
{
    // Skip processing if sleeping
    if (_sleeping)
    {
        return;
    }

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

    // MLOGINFO("In port:0x%04X, pc: 0x%04X bank: %s", port, pc, memBankName.c_str());

    /// endregion </Debug print>

    // Wake from sleep mode on port access
    wakeUp();

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:  // Return status register value
            result = getStatusRegister();

            {
                std::string statusInfo = dumpStatusRegister(_lastDecodedCmd);
                // Replace newlines with spaces for cleaner log output
                std::replace(statusInfo.begin(), statusInfo.end(), '\n', ' ');
                // Remove any double spaces that might have been created
                std::string::size_type pos;
                while ((pos = statusInfo.find("  ")) != std::string::npos)
                {
                    statusInfo.replace(pos, 2, " ");
                }
                // Trim trailing space
                if (!statusInfo.empty() && statusInfo.back() == ' ')
                {
                    statusInfo.pop_back();
                }
                MLOGDEBUG("In #1F (Get Status Register) - 0x%02X - %s, pc: 0x%04X bank: %s", result, statusInfo.c_str(),
                          pc, memBankName.c_str());
            }

            // Reset INTRQ (Interrupt request) flag - status register is read
            clearIntrq();
            break;
        case PORT_3F:  // Return the current track number
            result = _trackRegister;

            MLOGDEBUG("In #3F (Get Track Register) - 0x%02X, pc: 0x%04X bank: %s", result, pc, memBankName.c_str());
            break;
        case PORT_5F:  // Return current sector number
            result = _sectorRegister;

            MLOGDEBUG("In #5F (Get Sector Register) - 0x%02X, pc: 0x%04X bank: %s", result, pc, memBankName.c_str());
            break;
        case PORT_7F:  // Return data byte and update internal state
            // Read Data Register
            result = readDataRegister();

            // Reset DRQ (Data Request) flag
            clearDrq();

            MLOGDEBUG("In #7F (Get Data Register) - 0x%02X, pc: 0x%04X bank: %s", result, pc, memBankName.c_str());
            break;
        case PORT_FF:  // Handle Beta128 system port (#FF)
            // Only bits 6 and 7 are used
            result = _beta128status | (_beta128Register & 0x3F);

            MLOGDEBUG("In #FF Beta128: %s, pc: 0x%04X bank: %s", StringHelper::FormatBinary(result).c_str(), pc,
                      memBankName.c_str());
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

    // Wake from sleep mode on port access
    wakeUp();

    // Update FDC internal states
    process();

    // Handle FDC ports
    switch (port)
    {
        case PORT_1F:  // Write to Command Register
            // TODO: remove debug
            /// region <Debug logging>
            {
                WD_COMMANDS cmd = decodeWD93Command(value);
                const char* cmdName = getWD_COMMANDName(cmd);
                MLOGINFO(StringHelper::Format("  #1F - Set command: 0x%02X (%s). status: %s, beta128: %s", value,
                                              cmdName, StringHelper::FormatBinary(_statusRegister).c_str(),
                                              StringHelper::FormatBinary(_beta128status).c_str())
                             .c_str());
            }

            /// endregion </Debug logging>

            // Reset INTRQ (Interrupt request) flag - command register is written to
            clearIntrq();

            // Process the command
            processWD93Command(value);

            break;
        case PORT_3F:  // Write to Track Register
            _trackRegister = value;

            // TODO: remove debug
            if (_trackRegister >= MAX_CYLINDERS)
            {
                //_trackRegister = _trackRegister;

                _trackRegister = 79;
            }
            MLOGINFO(StringHelper::Format("  #3F - Set track: 0x%02X", _trackRegister).c_str());
            break;
        case PORT_5F:  // Write to Sector Register
            _sectorRegister = value;

            // TODO: remove debug
            if (_sectorRegister == 0 || _sectorRegister > 16)
            {
                //_sectorRegister = _sectorRegister;

                _sectorRegister = 16;
            }
            MLOGINFO(StringHelper::Format("  #5F - Set sector: 0x%02X", _sectorRegister).c_str());
            break;
        case PORT_7F:  // Write to Data Register
            // Write and mark that Data Register was accessed (for Type 2 and Type 3 operations)
            writeDataRegister(value);

            // Mark DRQ as served since CPU has written to the data register
            _drq_served = true;

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

            // TODO: remove debug
            MLOGINFO(StringHelper::Format("  #7F - Set data: 0x%02X", _dataRegister).c_str());
            break;
        case PORT_FF:  // Write to Beta128 system register
            processBeta128(value);
            MLOGINFO(StringHelper::Format("  #FF - Set beta128: 0x%02X", value).c_str());
            ;
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

        result = decoder->RegisterPortHandler(0x001F, this);
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
    static constexpr const char* STATUS_REGISTER_FLAGS[][8] = {
        {"BUSY", "INDEX", "TRACK 0", "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT",
         "NOT READY"},  // RESTORE
        {"BUSY", "INDEX", "TRACK 0", "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"},  // SEEK
        {"BUSY", "INDEX", "TRACK 0", "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT", "NOT READY"},  // STEP
        {"BUSY", "INDEX", "TRACK 0", "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT",
         "NOT READY"},  // STEP IN
        {"BUSY", "INDEX", "TRACK 0", "CRC ERROR", "SEEK ERROR", "HEAD LOADED", "WRITE PROTECT",
         "NOT READY"},                                                                                  // STEP OUT
        {"BUSY", "DRQ", "LOST DATA", "CRC ERROR", "RNF", "RECORD TYPE", "ZERO6", "NOT READY"},          // READ SECTOR
        {"BUSY", "DRQ", "LOST DATA", "CRC ERROR", "RNF", "WRITE FAULT", "WRITE PROTECT", "NOT READY"},  // WRITE SECTOR
        {"BUSY", "DRQ", "LOST DATA", "CRC ERROR", "RNF", "ZERO5", "ZERO6", "NOT READY"},                // READ ADDRESS
        {"BUSY", "DRQ", "LOST DATA", "ZERO3", "ZERO4", "ZERO5", "ZERO6", "NOT READY"},                  // READ TRACK
        {"BUSY", "DRQ", "LOST DATA", "ZERO3", "ZERO4", "WRITE FAULT", "WRITE PROTECT", "NOT READY"},    // WRITE TRACK
        // FORCE INTERRUPT doesn't have its own status bits. Bits from the previous / ongoing command to be shown
        // instead
    };

    std::stringstream ss;
    uint8_t status = _statusRegister;

    ss << StringHelper::Format("Command: %s. Status: 0x%02X. ", getWD_COMMANDName(command), status);

    switch (command)
    {
        case WD_CMD_FORCE_INTERRUPT:
            ss << "Force interrupt";
            break;
        default: {
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
    std::string result = ss.str();
    return result;
}

std::string WD1793::dumpBeta128Register()
{
    std::stringstream ss;

    ss << StringHelper::Format("Beta128 status: 0x%02X", _beta128status) << std::endl;

    // Define bit positions and their meanings
    static const std::pair<uint8_t, std::string> bitDescriptions[] = {{(uint8_t)BETA_STATUS_BITS::DRQ, "DRQ"},
                                                                      {(uint8_t)BETA_STATUS_BITS::INTRQ, "INTRQ"}};

    // Print bits horizontally
    ss << "  ";
    for (uint8_t bit = 0; bit < 8; bit++)
    {
        bool isSet = (_beta128status & (1 << bit)) != 0;

        std::string description = isSet ? "1" : "0";
        for (const auto& [descBit, desc] : bitDescriptions)
        {
            if (descBit == (1 << bit) && isSet)
            {
                description = desc;
                break;
            }
        }

        ss << StringHelper::Format("<%s> ", description);
    }

    ss << std::endl;

    std::string result = ss.str();
    return result;
}

std::string WD1793::dumpCommand(uint8_t value) const
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

std::string WD1793::dumpFullState()
{
    std::stringstream ss;

    // Convert T-states to microseconds
    size_t time_us = static_cast<size_t>(_time * 285.714 / 1000.0 + 0.5);  // +0.5 for rounding
    std::string time_us_str = StringHelper::Format("%d us", time_us);

    ss << "WD1793 state dump @ " << _time << " T (" << time_us_str << ")" << std::endl;
    ss << "----------------" << std::endl;
    ss << dumpCommand(_lastDecodedCmd) << std::endl;
    ss << dumpStep() << std::endl;
    ss << dumpStatusRegister((WD_COMMANDS)_lastDecodedCmd) << std::endl;
    ss << dumpBeta128Register() << std::endl;

    // Add additional state information
    ss << StringHelper::Format("State: %s", WDSTATEToString(_state)) << std::endl;
    ss << StringHelper::Format("State2: %s", WDSTATEToString(_state2)) << std::endl;
    ss << StringHelper::Format("DRQ served: %s", _drq_served ? "YES" : "NO") << std::endl;
    ss << StringHelper::Format("DRQ out: %s", _drq_out ? "YES" : "NO") << std::endl;
    ss << StringHelper::Format("INTRQ out: %s", _intrq_out ? "YES" : "NO") << std::endl;
    ss << _bytesToRead << " bytes to read" << std::endl;
    ss << _bytesToWrite << " bytes to write" << std::endl;

    ss << "----------------" << std::endl;
    std::string result = ss.str();

    return result;
}

std::string WD1793::dumpIndexStrobeData(bool skipNoTransitions)
{
    static constexpr const size_t LOG_UPDATE_INTERVAL_TSTATES =
        Z80_FREQUENCY / MILLISECONDS_PER_SECOND;  // Log updates every 1ms

    std::stringstream ss;

    // Get disk and motor state first as it affects other calculations
    bool diskInserted = _selectedDrive && _selectedDrive->isDiskInserted();
    bool motorOn = _motorTimeoutTStates > 0;

    if (!diskInserted)
        return "Disk not inserted => No rotation => No index strobe";
    if (!motorOn)
        return "Motor not on => No rotation => No index strobe";

    // Calculate timing values based on current state
    size_t tStateInRevolution = _time % DISK_ROTATION_PERIOD_TSTATES;

    // Calculate current revolution using the index pulse counter and current position
    // Each index pulse corresponds to one full revolution
    size_t currentRevolution = _indexPulseCounter;

    // Calculate disk rotation phase (0-360 degrees, scaled to 256 for 8-bit)
    size_t diskRotationPhaseCounter = (tStateInRevolution * 256) / DISK_ROTATION_PERIOD_TSTATES;

    // Detect edge transitions
    bool isFallingEdge = _prevIndex && !_index;
    bool isRisingEdge = !_prevIndex && _index;

    // Do not spam if not needed
    if (skipNoTransitions && !isFallingEdge && !isRisingEdge)
        return "";

    // Add transition state information
    ss << "Index pulse state: " << (_index ? "HIGH" : "LOW") << " | ";
    if (isRisingEdge)
    {
        ss << "RISING EDGE DETECTED | ";
    }
    else if (isFallingEdge)
    {
        ss << "FALLING EDGE DETECTED | ";
    }
    else
    {
        ss << "NO TRANSITION | ";
    }
    ss << std::endl;

    // Pre-calculate common values with thousands separators
    const std::string revStr = StringHelper::FormatWithCustomThousandsDelimiter(currentRevolution, '\'');
    const std::string tStateStr = StringHelper::FormatWithCustomThousandsDelimiter(tStateInRevolution, '\'');
    const std::string periodStr = StringHelper::FormatWithCustomThousandsDelimiter(DISK_ROTATION_PERIOD_TSTATES, '\'');
    const std::string phaseStr = StringHelper::FormatWithCustomThousandsDelimiter(diskRotationPhaseCounter, '\'');
    const std::string durationStr =
        StringHelper::FormatWithCustomThousandsDelimiter(INDEX_STROBE_DURATION_TSTATES, '\'');
    const std::string timeStr = StringHelper::FormatWithCustomThousandsDelimiter(_time, '\'');
    const std::string diffTimeStr = StringHelper::FormatWithCustomThousandsDelimiter(_diffTime, '\'');

    // Pre-calculate percentages
    const float tStatePercent = (tStateInRevolution * 100.0f) / DISK_ROTATION_PERIOD_TSTATES;
    const float phasePercent = (diskRotationPhaseCounter * 100.0f) / INDEX_STROBE_DURATION_TSTATES;

    // Pre-format time values
    std::stringstream timeSs;
    timeSs << std::fixed << std::setprecision(2) << (tStateInRevolution * 1000.0f) / Z80_FREQUENCY << "ms ("
           << (tStateInRevolution * 1000000.0f) / Z80_FREQUENCY << "us)";
    const std::string timeFormatted = timeSs.str();

    // Always show detailed timing information
    ss << "Revolution=" << revStr << ", T-state in rev=" << tStateStr << "/" << periodStr
       << StringHelper::Format(" (%.2f%%), ", tStatePercent) << "time=" << timeFormatted
       << ", phaseCounter=" << phaseStr << "/" << durationStr
       << StringHelper::Format(" (%.2f%%), _time=%s, _diffTime=%s", phasePercent, timeStr.c_str(), diffTimeStr.c_str());

    // Log regular state changes less frequently to avoid log spam
    if (_time % LOG_UPDATE_INTERVAL_TSTATES == 0)  // Log approximately every 100ms
    {
        if (_index)
        {
            ss << "Index pulse active. "
               << "Revolution=" << revStr << ", T-state in rev=" << tStateStr << "/" << periodStr
               << StringHelper::Format(" (%.2f%%), ", tStatePercent) << "time=" << timeFormatted
               << ", phaseCounter=" << phaseStr << "/" << durationStr
               << StringHelper::Format(" (%.2f%%)", phasePercent);
        }
        else
        {
            ss << "No index pulse. "
               << "Revolution=" << revStr << ", T-state in rev=" << tStateStr << "/" << periodStr
               << StringHelper::Format(" (%.2f%%), ", tStatePercent) << "time=" << timeFormatted;
        }
    }

    return ss.str();
}

/// endregion </Debug methods>