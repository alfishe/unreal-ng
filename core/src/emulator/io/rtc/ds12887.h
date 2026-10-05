#pragma once

/// @file ds12887.h
/// @brief MC146818 / DS12887 real-time clock with battery-backed RAM, shared by
/// every machine that wires one.
///
/// Users (the machine keeps only how the Z80 reaches the chip):
///   - ATM3 / ZX-Evo BaseConf: Gluk ports #DFF7 / #BFF7 (#DEF7 / #BEF7 in
///     shadow), through EvoAvr (memory/atm/evoavr.h), which serves registers
///     A, C, D and cells 0xF0-0xFF the way the board's AVR firmware does
///   - Profi 1024: #BF / #FF address, #9F / #DF data, extended mode only
///   - Scorpion SMUC: #DFBA address, #FFBA bit 7 data phase (io/rtc/smucnvram.h)
///   - planned: TSConf (Gluk ports as on the ZX-Evo, TSConf technical design
///     §3.4), Sprinter (DS12887A at #DFBD / #BFBD / #FFBD with the century
///     register at #32, Sprinter technical design §4). ZX Next has a DS1307 on
///     I2C - a different chip, not this class.
///
/// Registers (datasheet layout):
///   0x00 / 0x02 / 0x04       seconds / minutes / hours (+ alarms at 0x01 / 0x03 / 0x05)
///   0x06 / 0x07 / 0x08 / 0x09 day of week (1 = Sunday) / day / month / year
///   0x0A (A)                 bit 7 UIP (read-only), bits 6-0 stored
///   0x0B (B)                 bit 7 SET, 6 PIE, 5 AIE, 4 UIE, 3 SQWE, 2 DM (binary), 1 24h, 0 DSE
///   0x0C (C)                 bit 7 IRQF, 6 PF, 5 AF, 4 UF; cleared by the read
///   0x0D (D)                 bit 7 VRT (battery good), always 0x80
///   0x0E and up              general-purpose RAM (up to the configured cell count)
///
/// Deliberate simplifications (no machine here wires the IRQ or SQW pins):
///   - the periodic flag PF is never raised; RS bits are stored only
///   - of the divider bits DV only the reset (11x) acts: it holds the clock,
///     and the first update after it comes half a second later - the datasheet
///     way to start the clock on an exact second. Oscillator-off codes do not
///     stop the clock (Unreal, ZXMAK2 and Xpeccy ignore DV entirely; MAME
///     halts on every code other than 010)
///   - daylight saving (DSE) is stored but never applied
///
/// Time base (hybrid, PLAN #60(c)): the displayed time is a reference clock
/// plus the offset the guest set by writing the time registers.
///   - Host: the reference is the host's local time (normal running)
///   - Emulated: while a TTD session records, the reference is the host time at
///     the moment recording started plus the emulated time elapsed since, so a
///     replay reads exactly what the recording read
///   - Fixed: a frozen instant (tests); the clock never ticks, UF never rises

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "emulator/memory/devicememory.h"

class Ds12887
{
public:
    /// Cell counts of the parts in use: 64 (MC146818 / KR512VI1), 128
    /// (DS12887), 256 (the 256-cell register file ATM3, Profi and SMUC serve)
    static constexpr size_t kMaxCells = 256;

    /// Register indexes
    static constexpr uint8_t kSeconds = 0x00;
    static constexpr uint8_t kSecondsAlarm = 0x01;
    static constexpr uint8_t kMinutes = 0x02;
    static constexpr uint8_t kMinutesAlarm = 0x03;
    static constexpr uint8_t kHours = 0x04;
    static constexpr uint8_t kHoursAlarm = 0x05;
    static constexpr uint8_t kDayOfWeek = 0x06;
    static constexpr uint8_t kDay = 0x07;
    static constexpr uint8_t kMonth = 0x08;
    static constexpr uint8_t kYear = 0x09;
    static constexpr uint8_t kRegA = 0x0A;
    static constexpr uint8_t kRegB = 0x0B;
    static constexpr uint8_t kRegC = 0x0C;
    static constexpr uint8_t kRegD = 0x0D;
    static constexpr uint8_t kFirstRamCell = 0x0E;

    /// Register B bits
    static constexpr uint8_t kBSet = 0x80;
    static constexpr uint8_t kBUpdateIrq = 0x10;
    static constexpr uint8_t kBBinary = 0x04;
    static constexpr uint8_t kB24Hour = 0x02;

    /// Register C bits
    static constexpr uint8_t kCIrq = 0x80;
    static constexpr uint8_t kCAlarm = 0x20;
    static constexpr uint8_t kCUpdateEnded = 0x10;

    /// Power-on control registers without an NVRAM image: 32.768 kHz divider
    /// running, 1024 Hz rate (A = 0x26); BCD, 24-hour (B = 0x02)
    static constexpr uint8_t kPowerOnA = 0x26;
    static constexpr uint8_t kPowerOnB = 0x02;

    /// UIP rises this long before each update (datasheet: "UIP = 0 means no
    /// update for at least 244 us")
    static constexpr int64_t kUipWindowUs = 244;

    enum class TimeMode : uint8_t
    {
        Host = 0,
        Emulated = 1,
        Fixed = 2,
    };

    /// Emulated machine time in microseconds (PortDecoder::EmulatedMicroseconds)
    using EmulatedClock = std::function<uint64_t()>;

public:
    explicit Ds12887(size_t cellCount = 128);
    virtual ~Ds12887() = default;

    /// region <Bus>
    void WriteAddress(uint8_t address) { _address = address; }
    uint8_t GetAddress() const { return _address; }
    uint8_t ReadData() { return ReadRegister(static_cast<uint8_t>(_address & _addressMask)); }
    void WriteData(uint8_t value) { WriteRegister(static_cast<uint8_t>(_address & _addressMask), value); }
    /// endregion </Bus>

    /// region <Registers>
    /// Guest access. Virtual: EvoAvr overrides the registers its firmware serves
    virtual uint8_t ReadRegister(uint8_t index);
    virtual void WriteRegister(uint8_t index, uint8_t value);

    /// Side-effect-free view for debuggers and state reports: register C keeps
    /// its flags, nothing is sampled into UF. Virtual: EvoAvr shows the cells
    /// its firmware serves
    virtual uint8_t PeekRegister(uint8_t index) const;

    /// Part name for reports
    virtual const char* ChipName() const { return "MC146818 / DS12887"; }
    /// Where a front end serves registers its own way (reports); empty = datasheet
    virtual const char* RegistersNote() const { return ""; }
    /// endregion </Registers>

    /// region <Time base>
    /// Frozen instant (Unix seconds, shown as its UTC wall time on every host)
    void SetFixedTime(time_t unixSeconds);
    /// Back to the host clock (keeps any offset the guest set)
    void UseLiveTime();
    void SetEmulatedClock(EmulatedClock clock) { _emulatedClock = std::move(clock); }
    /// The session's wall time (host local civil microseconds, taken once when
    /// a TTD session starts; kNoSessionWall = none): every clock of the machine
    /// anchors at that one instant, so two chips never disagree
    static constexpr int64_t kNoSessionWall = INT64_MIN;
    using SessionWall = std::function<int64_t()>;
    void SetSessionWall(SessionWall wall) { _sessionWall = std::move(wall); }
    /// The host's local civil time now (what Host mode reads), microseconds
    static int64_t HostCivilMicrosNow();
    /// TTD recording starts / stops. Entering anchors the emulated reference at
    /// the session's wall time (else the current reference time); a fixed
    /// clock stays fixed
    void EnterEmulatedTime();
    void LeaveEmulatedTime();
    TimeMode GetTimeMode() const { return _mode; }
    /// Century register index (DS12887A / DS12C887: 0x32); 0 = none
    void SetCenturyRegister(uint8_t index) { _centuryRegister = index; }
    /// endregion </Time base>

    /// region <Battery-backed RAM>
    size_t GetCellCount() const { return _cellCount; }
    /// Raw cell (host side, no clock semantics): NVRAM images and EvoAvr
    uint8_t GetCell(uint8_t index) const { return _cells[index & _addressMask]; }
    void SetCell(uint8_t index, uint8_t value) { _cells[index & _addressMask] = value; }
    /// NVRAM image = the cells. Load keeps the clock registers live (0x00-0x09,
    /// C, D) and takes A, B (SET cleared) and the RAM cells; false keeps the
    /// power-on contents when the file is missing or has the wrong size
    bool LoadNvram(const std::string& path);
    bool SaveNvram(const std::string& path) const;
    /// The cells as the device memory region "cmos" (debugger additions tdd §4): a read is side-effect free
    /// (PeekRegister), a write is a guest write (WriteRegister: the time registers set the clock). A chip with more
    /// memory adds its own regions (EvoAvr: "eeprom")
    virtual void CollectMemoryRegions(std::vector<IDeviceMemoryRegion*>& out) { out.push_back(&_cmosRegion); }
    /// endregion </Battery-backed RAM>

    /// region <TTD>
    /// Complete chip state: cells, address latch, flags, time base
    static constexpr size_t kStateSize = 80 + kMaxCells;
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);
    /// endregion </TTD>

    /// Civil time <-> microseconds since 1970-01-01 00:00 (no time zone)
    struct CivilTime
    {
        int year = 1970;
        int month = 1;  ///< 1-12
        int day = 1;    ///< 1-31
        int hour = 0;
        int minute = 0;
        int second = 0;
    };
    static int64_t ToMicros(const CivilTime& time);
    static CivilTime FromMicros(int64_t micros);

protected:
    /// Current chip time, microseconds since 1970 civil
    int64_t ChipMicros() const;
    int64_t ReferenceMicros() const;
    /// Raise UF (and AF on an alarm match) when a second boundary has passed
    void UpdateFlags();
    uint8_t ReadTimeRegister(uint8_t index) const;
    void WriteTimeRegister(uint8_t index, uint8_t value);
    uint8_t Encode(int value) const;
    int Decode(uint8_t value) const;
    uint8_t EncodeHours(int hours) const;
    int DecodeHours(uint8_t value) const;
    bool IsTimeRegister(uint8_t index) const;
    /// Move the chip to a new time without raising UF for the jump
    void SetChipMicros(int64_t micros);
    /// Move the chip to fields exactly as written (see _rawTime)
    void SetChipTime(const CivilTime& time, int64_t fraction);
    /// The time registers as the guest sees them (held fields while SET)
    CivilTime CurrentTime() const;
    bool IsDividerReset() const;
    /// Hold or release the clock after a write to A or B
    void ApplyHold();

    std::array<uint8_t, kMaxCells> _cells{};
    size_t _cellCount;
    uint8_t _addressMask;
    uint8_t _address = 0;
    uint8_t _flagsC = 0;          ///< pending C flags (UF / AF)
    uint8_t _dayOfWeekOffset = 0; ///< guest day-of-week minus the calendar's, mod 7
    uint8_t _centuryRegister = 0;

    TimeMode _mode = TimeMode::Host;
    int64_t _offsetMicros = 0;    ///< guest-set time minus the reference
    int64_t _fixedMicros = 0;     ///< Fixed: the frozen reference
    int64_t _anchorMicros = 0;    ///< Emulated: reference when recording started
    uint64_t _anchorEmulated = 0; ///< Emulated: emulated clock at that moment
    bool _held = false;           ///< SET bit or divider reset: the clock does not advance
    CivilTime _heldTime;          ///< time registers while held, as written (no normalization)
    int64_t _heldFraction = 0;    ///< sub-second part while held
    /// Fields as the guest last wrote them without SET (the ZX-Evo AVR ignores
    /// SET, so its guests write the date one field at a time): shown as
    /// written until the next update, so "day 31" then "month 12" is the 31st
    /// of December even while the month is still September
    bool _rawValid = false;
    CivilTime _rawTime;
    int64_t _rawSecond = 0;       ///< the chip second the raw fields belong to
    bool _secondValid = false;
    int64_t _lastSecond = 0;      ///< last second UpdateFlags() saw

    SessionWall _sessionWall;
    EmulatedClock _emulatedClock;

private:
    class CmosRegion final : public IDeviceMemoryRegion
    {
    public:
        explicit CmosRegion(Ds12887& chip) : _chip(chip) {}
        const char* Name() const override { return "cmos"; }
        const char* Description() const override
        {
            return "CMOS clock cells as the guest addresses them: #00-#09 time and alarm, #0A-#0D registers A-D, "
                   "#0E and up battery-backed RAM";
        }
        uint32_t Size() const override { return static_cast<uint32_t>(_chip.GetCellCount()); }
        uint32_t PageSize() const override { return Size(); }
        const char* WritePath() const override
        {
            return "Ds12887::WriteRegister, as a guest write: the time registers set the clock, C and D ignore it";
        }
        uint8_t Read(uint32_t offset) const override { return _chip.PeekRegister(static_cast<uint8_t>(offset)); }
        void Write(uint32_t offset, uint8_t value) override { _chip.WriteRegister(static_cast<uint8_t>(offset), value); }

    private:
        Ds12887& _chip;
    };
    CmosRegion _cmosRegion{*this};
};
