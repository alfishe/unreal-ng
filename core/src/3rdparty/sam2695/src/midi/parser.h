// libsam2695 - MIDI 1.0 byte-stream parser.
//
// Running status for channel messages; system common messages cancel it. Realtime bytes (F8-FF) are
// delivered at once and never disturb a message in progress. System Exclusive is assembled into a
// bounded buffer: F7 or any other status byte ends it; a message longer than the buffer is dropped
// whole and counted. Data bytes without a status are ignored.
#pragma once

#include "sam2695/sam2695config.h"

#include <array>
#include <cstdint>

namespace sam2695
{

struct MidiMessage
{
    uint8_t status = 0;              // 80-EF channel, F0 SysEx (body in sysEx), F1-F6 common, F8-FF realtime
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    const uint8_t* sysEx = nullptr;  // SysEx body without F0 / F7
    uint16_t sysExLength = 0;
};

class MidiParser
{
public:
    void Reset();

    template <class Sink>
    void Feed(uint8_t byte, Sink&& sink);

    uint64_t SysExReceived() const { return _sysExReceived; }
    uint64_t SysExOverflows() const { return _sysExOverflows; }
    void ClearCounters() { _sysExReceived = _sysExOverflows = 0; }

    template <class Ar>
    void Serialize(Ar& ar)
    {
        ar(_running);
        ar(_data);
        ar(_count);
        ar(_inSysEx);
        ar(_sysExOverflow);
        ar(_sysExLength);
        ar(_sysEx);
        ar(_sysExReceived);
        ar(_sysExOverflows);
    }

private:
    static uint8_t DataCount(uint8_t status)
    {
        switch (status & 0xF0)
        {
            case 0xC0:
            case 0xD0:
                return 1;
            case 0xF0:
                return status == 0xF2 ? 2 : (status == 0xF1 || status == 0xF3) ? 1 : 0;
            default:
                return 2;
        }
    }

    template <class Sink>
    void EndSysEx(Sink& sink)
    {
        _inSysEx = false;
        if (_sysExOverflow)
        {
            _sysExOverflows++;
            return;
        }
        _sysExReceived++;
        MidiMessage m;
        m.status = 0xF0;
        m.sysEx = _sysEx.data();
        m.sysExLength = _sysExLength;
        sink(m);
    }

    uint8_t _running = 0;
    std::array<uint8_t, 2> _data{};
    uint8_t _count = 0;
    bool _inSysEx = false;
    bool _sysExOverflow = false;
    uint16_t _sysExLength = 0;
    std::array<uint8_t, kSysExCapacity> _sysEx{};
    uint64_t _sysExReceived = 0;
    uint64_t _sysExOverflows = 0;
};

inline void MidiParser::Reset()
{
    _running = 0;
    _count = 0;
    _inSysEx = false;
    _sysExOverflow = false;
    _sysExLength = 0;
}

template <class Sink>
void MidiParser::Feed(uint8_t byte, Sink&& sink)
{
    if (byte >= 0xF8)
    {
        MidiMessage m;
        m.status = byte;
        sink(m);
        return;
    }
    if (byte & 0x80)
    {
        if (_inSysEx)
            EndSysEx(sink); // F7, or any other status byte, ends the exclusive message
        _count = 0;
        if (byte == 0xF7)
        {
            _running = 0;
            return;
        }
        if (byte == 0xF0)
        {
            _running = 0;
            _inSysEx = true;
            _sysExOverflow = false;
            _sysExLength = 0;
            return;
        }
        _running = byte;
        if (byte >= 0xF0)
        {
            if (byte == 0xF4 || byte == 0xF5)
            {
                _running = 0; // undefined system common
                return;
            }
            if (DataCount(byte) == 0)
            {
                MidiMessage m;
                m.status = byte;
                _running = 0;
                sink(m);
            }
        }
        return;
    }
    if (_inSysEx)
    {
        if (_sysExLength < kSysExCapacity)
            _sysEx[_sysExLength++] = byte;
        else
            _sysExOverflow = true;
        return;
    }
    if (_running == 0)
        return; // data byte without a status
    _data[_count++] = byte;
    if (_count < DataCount(_running))
        return;
    MidiMessage m;
    m.status = _running;
    m.data1 = _data[0];
    m.data2 = _count > 1 ? _data[1] : 0;
    _count = 0;
    if (_running >= 0xF0)
        _running = 0; // system common: no running status
    sink(m);
}

} // namespace sam2695
