#pragma once

#include <stdafx.h>

class FDD
{
    /// region <Constants>
public:
    // Typical head engage time is 30...100ms depending on the drive
    static constexpr const size_t HEAD_LOAD_TIME_MS = 50;

    // The floppy rotated at 300 revolutions per minute, or five revolutions per second
    static constexpr const size_t DISK_REVOLUTIONS_PER_SECOND = 5;
    static constexpr const size_t DISK_INDEX_PERIOD_MS = 200;  // Index strobe active

    // Index strobe (active low) duration (2...5.5ms typically)
    static constexpr const size_t DISK_INDEX_STROBE_DURATION_US = 3500;

    // Head movement signal duration (at least 0.8 usec)
    static constexpr const size_t HEAD_STEP_DURATION_NS = 800;
    /// endregion </Constants>

    /// region <Fields>
protected:
    bool _motorOn = false;
    bool _sideTop = false;
    uint8_t _track = 0;
    uint8_t* _rawData = nullptr;

    /// endregion </Fields>

    /// region <Fields>
protected:
    uint32_t _motorSpinningCounter = 0; // 0 - not spinning; >0 - timeout until stop (in us)
    /// endregion </Fields>

    /// region <Properties>
public:
    bool getMotor() { return _motorOn; };
    void setMotor(bool motorOn) { _motorOn = motorOn; };

    bool getSide() { return _sideTop; };
    void setSide(bool sideTop) { _sideTop = sideTop; };

    int8_t getTrack() { return _track; };
    void setTrack(int8_t track) { _track = track; };

    uint8_t* getRawData() { return _rawData; };
    void setRawData(uint8_t* rawData) { _rawData = rawData; };
    /// endregion </Properties>

    /// region <Methods>
public:
    void process();
    /// endregion </Methods>
};
