#pragma once

/// @file nexti2c.h
/// @brief The Next's bit-banged I2C bus (ports #103B = SCL, #113B = SDA; a write drives bit 0 of the line, 1 releases
/// it, a read returns 1111111 & the line) with the DS1307 real-time clock at address #68 (the board's RTC; the other
/// devices on the bus, the ESP/Pi side and the flash, are not modeled). research-fpga-vhdl.md section 19.

#include <cstdint>

class Ds1307
{
public:
    static constexpr uint8_t kAddress = 0x68;

    Ds1307();
    /// 7-bit address match; the slave state machine below runs on the bus lines
    void SetTime(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second);
    uint8_t Reg(unsigned index) const { return _reg[index & 0x3F]; }
    void SetReg(unsigned index, uint8_t value) { _reg[index & 0x3F] = value; }

    /// The bus state after the master's drive of SCL / SDA; returns the SDA level the slave allows (false: pulls low)
    bool OnBus(bool scl, bool sda);
    void Reset();

private:
    enum class State : uint8_t { Idle, Address, Ack, RegisterPointer, WriteData, ReadData, MasterAck };

    uint8_t _reg[64] = {};
    State _state = State::Idle;
    bool _lastScl = true;
    bool _lastSda = true;
    uint8_t _shift = 0;
    unsigned _bits = 0;
    uint8_t _pointer = 0;
    bool _sdaOut = true;
    bool _readMode = false;
    bool _pointerSet = false;
};

class NextI2c
{
public:
    static constexpr uint16_t kPortScl = 0x103B;
    static constexpr uint16_t kPortSda = 0x113B;

    void Reset();
    uint8_t Read(uint16_t port) const;
    void Write(uint16_t port, uint8_t value);
    Ds1307& Rtc() { return _rtc; }
    bool Scl() const { return _scl; }
    bool Sda() const { return _sda && _slaveSda; }

private:
    bool _scl = true;
    bool _sda = true;
    bool _slaveSda = true;
    Ds1307 _rtc;
};
