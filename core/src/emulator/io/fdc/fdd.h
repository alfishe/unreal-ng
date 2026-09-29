#pragma once

#include <stdafx.h>

#include <functional>
#include "3rdparty/message-center/messagecenter.h"
#include "debugger/ttd/ttdserializable.h"  // TTDSerializable (P1.5 — captured via WD1793)
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/io/fdc/fdc.h"
#include "emulator/io/fdc/diskimage.h"

class EmulatorContext;

class FDD : public ttd::TTDSerializable
{
    /// region <Constants>
public:
    // Typical motor stop timeout is 200..300ms
    static constexpr const size_t MOTOR_STOP_TIMEOUT_MS = 200;

    // Typical head engage time is 30...100ms depending on the drive
    static constexpr const size_t HEAD_LOAD_TIME_MS = 50;

    // The floppy rotated at 300 revolutions per minute, or five revolutions per second
    static constexpr const size_t DISK_REVOLUTIONS_PER_SECOND = 5;
    static constexpr const size_t DISK_INDEX_PERIOD_MS = 200;           // Index strobe appears every 200ms
    static constexpr const size_t DISK_INDEX_STROBE_DURATION_MS = 4;    // Index strobe kept active for 4ms

    // Head movement signal duration (at least 0.8 usec)
    static constexpr const size_t HEAD_STEP_DURATION_NS = 800;
    /// endregion </Constants>

    /// region <Fields>
protected:
    EmulatorContext* _context = nullptr;

    uint8_t _driveID = 0;               // Drive number. 0..3

    // Read / write circuit signals
    bool _sideTop = false;
    bool _readDataBit = false;
    bool _writeDataBit = false;

    // Input signals
    bool _motorOn = false;
    bool _direction = true;             // True - from outside to inner tracks. false - from inner to outside
    bool _step = false;                 // Step strobe. Active high
    bool _headLoad = false;             // Activate head load solenoid

    // Output signals
    bool _index = false;
    bool _ready = false;
    bool _writeProtect = false;

    DiskImage* _diskImage = nullptr;    // Pointer to a disk image inserted to this drive
    // Called on insert / eject, before the previous image can be released:
    // the controller drops every pointer it holds into that image
    std::function<void(FDD*)> _diskChanged;
    bool _diskInserted = false;
    uint8_t _track = 0;                 // Physical head position (step pulses only; not the FDC's track register)
    uint8_t _driveCylinders = 80;       // Mechanics: 80-track (96 tpi, Beta 128 drives) or 40-track (48 tpi, the +3's 3" drive)
    uint8_t _readDataByte = 0;
    uint8_t _writeDataByte = 0;

    size_t _motorStopTimeoutMs = 0;     // 0 - stopped, >0 - timeout when motor will be stopped
    size_t _motorRotationCounter = 0;   //

    uint64_t _lastFrame = 0;            // Frame counter during last call
    uint32_t _lastTime = 0;             // CPU t-state counter during last call (for time synchronization)

    /// endregion </Fields>

    /// region <Properties>
public:
    bool getSide() { return _sideTop; };
    void setSide(bool sideTop) { _sideTop = sideTop; };

    bool readDataBit() { return _readDataBit; }
    void writeDataBit(bool value) { _writeDataBit = value; }

    bool getMotor() { return _motorOn; };
    void setMotor(bool motorOn)
    {
        // Start the spindle motor
        _motorOn = motorOn;

        if (motorOn)
        {
            // Set initial default timeout. Each next access operation will reset this timeout to original value
            resetMotorTimeout();

            // Notify subscribers that motor was started
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_FDD_MOTOR_STARTED, new SimpleNumberPayload(_driveID), true);
        }
        else
        {
            // Notify subscribers that motor was stopped
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_FDD_MOTOR_STOPPED, new SimpleNumberPayload(_driveID), true);
        }
    };

    int8_t getTrack() { return _track; };
    void setTrack(int8_t track)
    {
        // The head stops at the drive's mechanical end: an 80-track drive
        // travels to MAX_CYLINDERS, a 40-track one to 42 (TR-DOS tells the
        // two apart by seeking to 50 and back to 2, then testing TRK00)
        const int limit = maxPhysicalTrack();
        if (track < 0)
        {
            _track = 0;
        }
        else if (track > limit)
        {
            _track = static_cast<uint8_t>(limit);
        }
        else
        {
            _track = static_cast<uint8_t>(track);
        }
    };

    /// Drive mechanics: 80 (96 tpi) or 40 (48 tpi) tracks
    uint8_t getDriveCylinders() const { return _driveCylinders; }
    void setDriveCylinders(uint8_t cylinders) { _driveCylinders = cylinders; }
    int maxPhysicalTrack() const { return _driveCylinders >= 80 ? MAX_CYLINDERS : 42; }

    /// The disk track under the head, or nullptr when there is none. A
    /// 40-track disk (48 tpi: DiskImage::isFortyTrack) in an 80-track drive
    /// (96 tpi) has its tracks at every second head position: position p
    /// reads cylinder p / 2, and an odd position sits between two tracks.
    /// TR-DOS steps twice per track for such a disk (it reads the disk type
    /// in sector 9 and the drive type it measured), as on real hardware
    DiskImage::Track* trackUnderHead(uint8_t side);

    bool isTrack00() { return _track == 0; }
    bool isIndex() { return _index; }
    bool isWriteProtect() { return _writeProtect; }
    void setWriteProtect(bool protect) { _writeProtect = protect; }
    bool isReady() { return _ready; }

    bool isDiskInserted() { return _diskInserted; }
    DiskImage* getDiskImage() { return _diskImage; }
    uint8_t getDriveId() const { return _driveID; }
    /// endregion </Properties>

    /// region <Constructors / destructors>
public:
    FDD(EmulatorContext* context);
    virtual ~FDD();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    void process();

    void insertDisk(DiskImage* diskImage);
    void ejectDisk();
    /// The controller that reads this drive: told on every insert / eject,
    /// before the caller releases the previous image (nullptr to detach)
    void setDiskChangedCallback(std::function<void(FDD*)> callback) { _diskChanged = std::move(callback); }
    /// endregion </Methods>

    /// region <TTDSerializable interface (P1.5 — parent TDD §6.4, §4 row 4)>
    ///
    /// Per parent TDD §4 row 4 + §17: FDD state is captured as part of the
    /// WD1793 subsystem blob. The FDD itself is a TTDSerializable so the
    /// WD1793 serializer can delegate via the public interface without
    /// friending; this also lets FDD-only unit tests exercise the path.
    ///
    /// Serialized: driveID, side, motor, direction, headLoad, diskInserted,
    ///             track, motorStopTimeoutMs, motorRotationCounter,
    ///             index/ready/writeProtect cached signals.
    /// Excluded: _diskImage pointer (the media manager's slot re-inserts it
    ///           on session restore — disk image identity is session-scoped
    ///           per TDD §12.2), _context, transient strobes (_step, _read/
    ///           _writeDataBit/Byte), sync counters (_lastFrame, _lastTime).
    size_t TTDStateSize() const override;
    void   TTDSaveState(uint8_t* dst) const override;
    void   TTDLoadState(const uint8_t* src) override;
    /// endregion </TTDSerializable interface>

    /// region <Helper methods>
protected:
    void resetMotorTimeout()
    {
        _motorStopTimeoutMs = MOTOR_STOP_TIMEOUT_MS;
    }
    /// endregion </Helper methods>
};
