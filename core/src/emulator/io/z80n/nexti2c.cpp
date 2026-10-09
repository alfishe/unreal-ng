#include "stdafx.h"

#include "nexti2c.h"

namespace
{
uint8_t Bcd(unsigned v)
{
    return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
}
}  // namespace

Ds1307::Ds1307()
{
    Reset();
}

void Ds1307::Reset()
{
    for (uint8_t& r : _reg)
        r = 0;
    SetTime(2026, 1, 1, 12, 0, 0);
    _state = State::Idle;
    _lastScl = _lastSda = true;
    _sdaOut = true;
    _pointer = 0;
}

void Ds1307::SetTime(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second)
{
    _reg[0] = Bcd(second);
    _reg[1] = Bcd(minute);
    _reg[2] = Bcd(hour);  // 24-hour mode
    _reg[3] = 1;          // day of week (not derived)
    _reg[4] = Bcd(day);
    _reg[5] = Bcd(month);
    _reg[6] = Bcd(year % 100);
}

bool Ds1307::OnBus(bool scl, bool sda)
{
    // START: SDA falls while SCL is high; STOP: SDA rises while SCL is high
    if (scl && _lastScl)
    {
        if (_lastSda && !sda)
        {
            _state = State::Address;
            _bits = 0;
            _shift = 0;
            _sdaOut = true;
            _pointerSet = false;
        }
        else if (!_lastSda && sda)
        {
            _state = State::Idle;
            _sdaOut = true;
        }
    }
    else if (scl && !_lastScl)  // rising edge: the master's bit is valid
    {
        switch (_state)
        {
            case State::Address:
            case State::RegisterPointer:
            case State::WriteData:
                _shift = static_cast<uint8_t>((_shift << 1) | (sda ? 1 : 0));
                _bits++;
                break;
            case State::MasterAck:  // the master acknowledges (SDA low) a byte we sent: send the next
                _bits = 0;
                if (!sda)
                {
                    _state = State::ReadData;  // the next register goes out from the falling edge on
                    _shift = _reg[_pointer & 0x3F];
                    _pointer = (_pointer + 1) & 0x3F;
                }
                else
                    _state = State::Idle;
                break;
            default:
                break;
        }
    }
    else if (!scl && _lastScl)  // falling edge: change our output
    {
        switch (_state)
        {
            case State::Address:
            case State::RegisterPointer:
            case State::WriteData:
                if (_bits == 8)
                {
                    // a byte is in: acknowledge it (when it is for us)
                    if (_state == State::Address)
                    {
                        if ((_shift >> 1) == kAddress)
                        {
                            _readMode = (_shift & 1) != 0;
                            _sdaOut = false;
                            _state = State::Ack;
                        }
                        else
                            _state = State::Idle;
                    }
                    else if (_state == State::RegisterPointer)
                    {
                        _pointer = _shift & 0x3F;
                        _pointerSet = true;
                        _sdaOut = false;
                        _state = State::Ack;
                    }
                    else
                    {
                        _reg[_pointer & 0x3F] = _shift;
                        _pointer = (_pointer + 1) & 0x3F;
                        _sdaOut = false;
                        _state = State::Ack;
                    }
                    _bits = 0;
                }
                break;
            case State::Ack:
                // the acknowledge bit is over: next byte (or our data)
                _sdaOut = true;
                _bits = 0;
                _shift = 0;
                if (_readMode)
                {
                    _state = State::ReadData;
                    _shift = _reg[_pointer & 0x3F];
                    _pointer = (_pointer + 1) & 0x3F;
                    _sdaOut = (_shift & 0x80) != 0;
                    _bits = 1;  // the first bit is on the line
                    _shift = static_cast<uint8_t>(_shift << 1);
                }
                else
                    _state = _pointerSet ? State::WriteData : State::RegisterPointer;
                break;
            case State::ReadData:
                if (_bits < 8)
                {
                    _sdaOut = (_shift & 0x80) != 0;
                    _shift = static_cast<uint8_t>(_shift << 1);
                    _bits++;
                }
                else
                {
                    _sdaOut = true;  // release for the master's acknowledge
                    _state = State::MasterAck;
                }
                break;
            default:
                break;
        }
    }
    _lastScl = scl;
    _lastSda = sda;
    return _sdaOut;
}

void NextI2c::Reset()
{
    _scl = _sda = _slaveSda = true;
    _rtc.Reset();
}

uint8_t NextI2c::Read(uint16_t port) const
{
    const bool level = port == kPortScl ? _scl : (_sda && _slaveSda);
    return static_cast<uint8_t>(0xFE | (level ? 1 : 0));
}

void NextI2c::Write(uint16_t port, uint8_t value)
{
    const bool level = (value & 1) != 0;
    if (port == kPortScl)
        _scl = level;
    else
        _sda = level;
    _slaveSda = _rtc.OnBus(_scl, _sda && _slaveSda);
}
