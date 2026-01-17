#pragma once

#include <stdafx.h>
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/platform.h"
#include "emulator/io/fdc/fdc.h"

class EmulatorContext;
class DiskImage;

class FDD
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
    bool _diskInserted = false;
    uint8_t _track = 0;
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
    void setTrack(int8_t track) { _track = track > MAX_CYLINDERS ? MAX_CYLINDERS : track; };

    bool isTrack00() { return _track == 0; }
    bool isIndex() { return _index; }
    bool isWriteProtect() { return _writeProtect; }
    void setWriteProtect(bool protect) { _writeProtect = protect; }
    bool isReady() { return _ready; }

    bool isDiskInserted() { return _diskInserted; }
    DiskImage* getDiskImage() { return _diskImage; }
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
    /// endregion </Methods>

    /// region <Helper methods>
protected:
    void resetMotorTimeout()
    {
        _motorStopTimeoutMs = MOTOR_STOP_TIMEOUT_MS;
    }
    /// endregion </Helper methods>
};
