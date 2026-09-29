#include "stdafx.h"

#include "ds12887.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <vector>

#include "common/filehelper.h"

namespace
{
    constexpr int64_t kMicrosPerSecond = 1000000;
    constexpr int64_t kSecondsPerDay = 86400;
    constexpr uint8_t kStateVersion = 2;

    /// Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's
    /// days_from_civil, public domain)
    int64_t DaysFromCivil(int64_t y, int m, int d)
    {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const int64_t yoe = y - era * 400;
        const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    void CivilFromDays(int64_t z, int64_t& y, int& m, int& d)
    {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const int64_t doe = z - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
        y = yoe + era * 400 + (m <= 2);
    }

    int64_t FloorDiv(int64_t value, int64_t divisor)
    {
        const int64_t q = value / divisor;
        return (value % divisor != 0 && (value < 0) != (divisor < 0)) ? q - 1 : q;
    }

    /// Host local wall time of a Unix instant, as microseconds since 1970 civil
    int64_t LocalCivilMicros(std::time_t unixSeconds, int64_t fractionMicros)
    {
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &unixSeconds);
#else
        localtime_r(&unixSeconds, &local);
#endif
        Ds12887::CivilTime civil;
        civil.year = local.tm_year + 1900;
        civil.month = local.tm_mon + 1;
        civil.day = local.tm_mday;
        civil.hour = local.tm_hour;
        civil.minute = local.tm_min;
        civil.second = local.tm_sec > 59 ? 59 : local.tm_sec;  // a leap second holds :59
        return Ds12887::ToMicros(civil) + fractionMicros;
    }

    void PutU64(uint8_t* dst, uint64_t value)
    {
        for (int i = 0; i < 8; ++i)
            dst[i] = static_cast<uint8_t>(value >> (8 * i));
    }

    uint64_t GetU64(const uint8_t* src)
    {
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i)
            value |= static_cast<uint64_t>(src[i]) << (8 * i);
        return value;
    }
}  // namespace

/// region <Construction>

Ds12887::Ds12887(size_t cellCount)
{
    // Power-of-two part sizes only: the address decode is a mask
    if (cellCount != 64 && cellCount != 128 && cellCount != 256)
        cellCount = 128;
    _cellCount = cellCount;
    _addressMask = static_cast<uint8_t>(cellCount - 1);

    // Zeroed RAM, not garbage: guests probe cells during boot, and a boot that
    // depends on process memory layout is not reproducible
    _cells.fill(0x00);
    _cells[kRegA] = kPowerOnA;
    _cells[kRegB] = kPowerOnB;
}

/// endregion </Construction>

/// region <Civil time>

int64_t Ds12887::ToMicros(const CivilTime& time)
{
    const int64_t days = DaysFromCivil(time.year, time.month, time.day);
    const int64_t seconds = days * kSecondsPerDay + time.hour * 3600 + time.minute * 60 + time.second;
    return seconds * kMicrosPerSecond;
}

Ds12887::CivilTime Ds12887::FromMicros(int64_t micros)
{
    const int64_t seconds = FloorDiv(micros, kMicrosPerSecond);
    const int64_t days = FloorDiv(seconds, kSecondsPerDay);
    const int64_t secondOfDay = seconds - days * kSecondsPerDay;

    CivilTime time;
    int64_t year = 0;
    CivilFromDays(days, year, time.month, time.day);
    time.year = static_cast<int>(year);
    time.hour = static_cast<int>(secondOfDay / 3600);
    time.minute = static_cast<int>((secondOfDay / 60) % 60);
    time.second = static_cast<int>(secondOfDay % 60);
    return time;
}

/// endregion </Civil time>

/// region <Time base>

int64_t Ds12887::ReferenceMicros() const
{
    switch (_mode)
    {
        case TimeMode::Fixed:
            return _fixedMicros;
        case TimeMode::Emulated:
        {
            const uint64_t now = _emulatedClock ? _emulatedClock() : _anchorEmulated;
            return _anchorMicros + static_cast<int64_t>(now - _anchorEmulated);
        }
        case TimeMode::Host:
        default:
        {
            const auto now = std::chrono::system_clock::now();
            const int64_t unixMicros =
                std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            const int64_t unixSeconds = FloorDiv(unixMicros, kMicrosPerSecond);
            return LocalCivilMicros(static_cast<std::time_t>(unixSeconds), unixMicros - unixSeconds * kMicrosPerSecond);
        }
    }
}

int64_t Ds12887::ChipMicros() const
{
    return _held ? ToMicros(_heldTime) + _heldFraction : ReferenceMicros() + _offsetMicros;
}

Ds12887::CivilTime Ds12887::CurrentTime() const
{
    if (_held)
        return _heldTime;
    const int64_t micros = ChipMicros();
    if (_rawValid && FloorDiv(micros, kMicrosPerSecond) == _rawSecond)
        return _rawTime;  // as written, until the next update
    return FromMicros(micros);
}

void Ds12887::SetChipTime(const CivilTime& time, int64_t fraction)
{
    const int64_t micros = ToMicros(time) + fraction;
    SetChipMicros(micros);
    _rawTime = time;
    _rawSecond = FloorDiv(micros, kMicrosPerSecond);
    _rawValid = true;
}

void Ds12887::SetChipMicros(int64_t micros)
{
    if (_held)
    {
        _heldTime = FromMicros(micros);
        _heldFraction = micros - FloorDiv(micros, kMicrosPerSecond) * kMicrosPerSecond;
    }
    else
    {
        _offsetMicros = micros - ReferenceMicros();
    }

    // A set is not an update: no UF for the jump
    _secondValid = true;
    _lastSecond = FloorDiv(micros, kMicrosPerSecond);
}

void Ds12887::SetFixedTime(time_t unixSeconds)
{
    _rawValid = false;
    _mode = TimeMode::Fixed;
    _fixedMicros = LocalCivilMicros(unixSeconds, 0);
    _secondValid = false;
}

void Ds12887::UseLiveTime()
{
    _rawValid = false;
    _mode = TimeMode::Host;
    _secondValid = false;
}

void Ds12887::EnterEmulatedTime()
{
    if (_mode != TimeMode::Host)
        return;  // already emulated, or fixed (a frozen clock is deterministic as it is)

    _anchorMicros = ReferenceMicros();
    _anchorEmulated = _emulatedClock ? _emulatedClock() : 0;
    _mode = TimeMode::Emulated;
}

void Ds12887::LeaveEmulatedTime()
{
    if (_mode != TimeMode::Emulated)
        return;

    // Back to the wall clock; the offset the guest set survives
    _mode = TimeMode::Host;
    _rawValid = false;
    _secondValid = false;
}

/// endregion </Time base>

/// region <Registers>

bool Ds12887::IsTimeRegister(uint8_t index) const
{
    switch (index)
    {
        case kSeconds:
        case kMinutes:
        case kHours:
        case kDayOfWeek:
        case kDay:
        case kMonth:
        case kYear:
            return true;
        default:
            return _centuryRegister != 0 && index == _centuryRegister;
    }
}

uint8_t Ds12887::Encode(int value) const
{
    if (_cells[kRegB] & kBBinary)
        return static_cast<uint8_t>(value);
    return static_cast<uint8_t>((value % 10) | (((value / 10) % 10) << 4));
}

int Ds12887::Decode(uint8_t value) const
{
    if (_cells[kRegB] & kBBinary)
        return value;
    return (value >> 4) * 10 + (value & 0x0F);
}

uint8_t Ds12887::EncodeHours(int hours) const
{
    if (_cells[kRegB] & kB24Hour)
        return Encode(hours);

    // 12-hour mode: 12 AM, 1-11 AM, 12 PM, 1-11 PM; bit 7 = PM
    const uint8_t pm = hours >= 12 ? 0x80 : 0x00;
    int h = hours % 12;
    if (h == 0)
        h = 12;
    return static_cast<uint8_t>(Encode(h) | pm);
}

int Ds12887::DecodeHours(uint8_t value) const
{
    if (_cells[kRegB] & kB24Hour)
        return Decode(value);

    const bool pm = (value & 0x80) != 0;
    int h = Decode(static_cast<uint8_t>(value & 0x7F)) % 12;
    return pm ? h + 12 : h;
}

uint8_t Ds12887::ReadTimeRegister(uint8_t index) const
{
    const CivilTime time = CurrentTime();
    const int64_t micros = ToMicros(time);

    switch (index)
    {
        case kSeconds:
            return Encode(time.second);
        case kMinutes:
            return Encode(time.minute);
        case kHours:
            return EncodeHours(time.hour);
        case kDayOfWeek:
        {
            // 1970-01-01 was a Thursday; the register counts 1 = Sunday
            const int64_t days = FloorDiv(FloorDiv(micros, kMicrosPerSecond), kSecondsPerDay);
            const int calendar = static_cast<int>(((days + 4) % 7 + 7) % 7);
            return Encode(1 + (calendar + _dayOfWeekOffset) % 7);
        }
        case kDay:
            return Encode(time.day);
        case kMonth:
            return Encode(time.month);
        case kYear:
            return Encode(time.year % 100);
        default:  // century
            return Encode(time.year / 100);
    }
}

void Ds12887::WriteTimeRegister(uint8_t index, uint8_t value)
{
    CivilTime time = CurrentTime();
    const int64_t micros = ChipMicros();
    const int64_t fraction = micros - FloorDiv(micros, kMicrosPerSecond) * kMicrosPerSecond;
    auto calendarDay = [](const CivilTime& t) {
        const int64_t days = FloorDiv(FloorDiv(ToMicros(t), kMicrosPerSecond), kSecondsPerDay);
        return static_cast<int>(((days + 4) % 7 + 7) % 7);  // 0 = Sunday (1970-01-01 was a Thursday)
    };
    const int calendarBefore = calendarDay(time);

    switch (index)
    {
        case kSeconds:
            time.second = Decode(value) % 60;
            break;
        case kMinutes:
            time.minute = Decode(value) % 60;
            break;
        case kHours:
            time.hour = DecodeHours(value) % 24;
            break;
        case kDayOfWeek:
        {
            // The chip's day-of-week counter is independent of the date: keep
            // the guest's value as an offset from the calendar day
            const int wanted = (Decode(value) + 6) % 7;  // 1..7 -> 0..6
            _dayOfWeekOffset = static_cast<uint8_t>((wanted - calendarBefore + 7) % 7);
            return;
        }
        case kDay:
            time.day = Decode(value);
            break;
        case kMonth:
            time.month = Decode(value);
            break;
        case kYear:
            time.year = (time.year / 100) * 100 + Decode(value) % 100;
            break;
        default:  // century
            time.year = Decode(value) * 100 + time.year % 100;
            break;
    }

    // Keep the fields in a range the calendar arithmetic accepts. Day 31 of a
    // 30-day month counts on as the 1st of the next month once the clock runs
    if (time.month < 1 || time.month > 12)
        time.month = 1;
    if (time.day < 1 || time.day > 31)
        time.day = 1;

    // The guest's day-of-week counter does not follow a set, it only counts
    // midnights: re-base the offset on the new calendar day
    const int guestDayOfWeek = (calendarBefore + _dayOfWeekOffset) % 7;
    const int calendarAfter = calendarDay(time);
    _dayOfWeekOffset = static_cast<uint8_t>((guestDayOfWeek - calendarAfter + 7) % 7);

    if (_held)
    {
        // SET: the registers hold exactly what was written until the release
        _heldTime = time;
        return;
    }
    SetChipTime(time, fraction);
}

void Ds12887::UpdateFlags()
{
    if (_held || _mode == TimeMode::Fixed)
        return;

    const int64_t second = FloorDiv(ChipMicros(), kMicrosPerSecond);
    if (!_secondValid)
    {
        _secondValid = true;
        _lastSecond = second;
        return;
    }
    if (second == _lastSecond)
        return;
    _lastSecond = second;

    _flagsC |= kCUpdateEnded;

    // Alarm: each of seconds / minutes / hours matches, or is don't-care (11xxxxxx)
    auto matches = [&](uint8_t alarmIndex, uint8_t timeIndex) {
        const uint8_t alarm = _cells[alarmIndex];
        return (alarm & 0xC0) == 0xC0 || alarm == ReadTimeRegister(timeIndex);
    };
    if (matches(kSecondsAlarm, kSeconds) && matches(kMinutesAlarm, kMinutes) && matches(kHoursAlarm, kHours))
        _flagsC |= kCAlarm;
}

uint8_t Ds12887::ReadRegister(uint8_t index)
{
    index &= _addressMask;

    if (IsTimeRegister(index))
        return ReadTimeRegister(index);

    switch (index)
    {
        case kRegA:
        {
            uint8_t value = static_cast<uint8_t>(_cells[kRegA] & 0x7F);
            if (!_held && _mode != TimeMode::Fixed)
            {
                const int64_t micros = ChipMicros();
                const int64_t fraction = micros - FloorDiv(micros, kMicrosPerSecond) * kMicrosPerSecond;
                if (fraction >= kMicrosPerSecond - kUipWindowUs)
                    value |= 0x80;
            }
            return value;
        }
        case kRegC:
        {
            UpdateFlags();
            uint8_t value = _flagsC;
            if (value & _cells[kRegB] & 0x70)
                value |= kCIrq;
            _flagsC = 0;
            return value;
        }
        case kRegD:
            return 0x80;
        default:
            return _cells[index];  // alarms, B, RAM
    }
}

bool Ds12887::IsDividerReset() const
{
    return (_cells[kRegA] & 0x60) == 0x60;  // DV2-DV1 = 11: countdown chain held in reset
}

void Ds12887::ApplyHold()
{
    const bool hold = (_cells[kRegB] & kBSet) != 0 || IsDividerReset();
    if (hold && !_held)
    {
        // SET or a divider reset stops the update: the time registers hold
        // for the guest to write
        const int64_t micros = ChipMicros();
        _heldTime = CurrentTime();
        _heldFraction = micros - FloorDiv(micros, kMicrosPerSecond) * kMicrosPerSecond;
        _held = true;
    }
    if (_held && IsDividerReset())
        _heldFraction = 0;
    if (!hold && _held)
    {
        // Counting resumes from the held (possibly rewritten) time
        const CivilTime time = _heldTime;
        const int64_t fraction = _heldFraction;
        _held = false;
        SetChipTime(time, fraction);
    }
}

void Ds12887::WriteRegister(uint8_t index, uint8_t value)
{
    index &= _addressMask;

    if (IsTimeRegister(index))
    {
        WriteTimeRegister(index, value);
        return;
    }

    switch (index)
    {
        case kRegA:
        {
            const bool wasReset = IsDividerReset();
            _cells[kRegA] = static_cast<uint8_t>(value & 0x7F);  // UIP is read-only
            if (wasReset && !IsDividerReset())
                _heldFraction = kMicrosPerSecond / 2;  // the first update comes half a second later
            ApplyHold();
            return;
        }
        case kRegB:
            if ((value & kBSet) && !(_cells[kRegB] & kBSet))
                value &= static_cast<uint8_t>(~kBUpdateIrq);  // SET clears UIE (datasheet)
            _cells[kRegB] = value;
            ApplyHold();
            return;
        case kRegC:
        case kRegD:
            return;  // read-only
        default:
            _cells[index] = value;  // alarms, RAM
            return;
    }
}

uint8_t Ds12887::PeekRegister(uint8_t index) const
{
    index &= _addressMask;

    if (IsTimeRegister(index))
        return ReadTimeRegister(index);

    switch (index)
    {
        case kRegA:
            return static_cast<uint8_t>(_cells[kRegA] & 0x7F);
        case kRegC:
            return static_cast<uint8_t>(_flagsC | ((_flagsC & _cells[kRegB] & 0x70) ? kCIrq : 0));
        case kRegD:
            return 0x80;
        default:
            return _cells[index];
    }
}

/// endregion </Registers>

/// region <Battery-backed RAM>

bool Ds12887::LoadNvram(const std::string& path)
{
    if (path.empty())
        return false;

    std::ifstream file(FileHelper::ToFsPath(path), std::ios::binary);
    if (!file)
        return false;

    std::vector<uint8_t> image(_cellCount);
    file.read(reinterpret_cast<char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (file.gcount() != static_cast<std::streamsize>(image.size()))
        return false;

    // The time comes from the time base, C and D are live
    _cells[kRegA] = static_cast<uint8_t>(image[kRegA] & 0x7F);
    _cells[kRegB] = static_cast<uint8_t>(image[kRegB] & ~kBSet);
    _cells[kSecondsAlarm] = image[kSecondsAlarm];
    _cells[kMinutesAlarm] = image[kMinutesAlarm];
    _cells[kHoursAlarm] = image[kHoursAlarm];
    std::memcpy(&_cells[kFirstRamCell], &image[kFirstRamCell], _cellCount - kFirstRamCell);
    ApplyHold();  // a saved divider reset holds the clock, as on the chip
    return true;
}

bool Ds12887::SaveNvram(const std::string& path) const
{
    if (path.empty())
        return false;

    std::vector<uint8_t> image(_cells.begin(), _cells.begin() + static_cast<std::ptrdiff_t>(_cellCount));
    for (uint8_t index = 0; index < kFirstRamCell; ++index)
        image[index] = PeekRegister(index);

    std::ofstream file(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    return static_cast<bool>(file);
}

/// endregion </Battery-backed RAM>

/// region <TTD>

void Ds12887::SaveState(uint8_t* dst) const
{
    std::memset(dst, 0, kStateSize);
    dst[0] = kStateVersion;
    dst[1] = _addressMask;
    dst[2] = _address;
    dst[3] = _flagsC;
    dst[4] = _dayOfWeekOffset;
    dst[5] = _centuryRegister;
    dst[6] = static_cast<uint8_t>(_mode);
    dst[7] = static_cast<uint8_t>((_held ? 0x01 : 0) | (_secondValid ? 0x02 : 0));
    PutU64(dst + 8, static_cast<uint64_t>(_offsetMicros));
    PutU64(dst + 16, static_cast<uint64_t>(_fixedMicros));
    PutU64(dst + 24, static_cast<uint64_t>(_anchorMicros));
    PutU64(dst + 32, _anchorEmulated);
    PutU64(dst + 40, static_cast<uint64_t>(_heldFraction));
    PutU64(dst + 48, static_cast<uint64_t>(_lastSecond));
    dst[56] = static_cast<uint8_t>(_heldTime.year);
    dst[57] = static_cast<uint8_t>(_heldTime.year >> 8);
    dst[58] = static_cast<uint8_t>(_heldTime.month);
    dst[59] = static_cast<uint8_t>(_heldTime.day);
    dst[60] = static_cast<uint8_t>(_heldTime.hour);
    dst[61] = static_cast<uint8_t>(_heldTime.minute);
    dst[62] = static_cast<uint8_t>(_heldTime.second);
    dst[64] = _rawValid ? 1 : 0;
    dst[65] = static_cast<uint8_t>(_rawTime.year);
    dst[66] = static_cast<uint8_t>(_rawTime.year >> 8);
    dst[67] = static_cast<uint8_t>(_rawTime.month);
    dst[68] = static_cast<uint8_t>(_rawTime.day);
    dst[69] = static_cast<uint8_t>(_rawTime.hour);
    dst[70] = static_cast<uint8_t>(_rawTime.minute);
    dst[71] = static_cast<uint8_t>(_rawTime.second);
    PutU64(dst + 72, static_cast<uint64_t>(_rawSecond));
    std::memcpy(dst + 80, _cells.data(), kMaxCells);
}

void Ds12887::LoadState(const uint8_t* src)
{
    // A blob from another part size would scramble the address decode
    if (src[0] != kStateVersion || src[1] != _addressMask)
        return;

    _address = src[2];
    _flagsC = src[3];
    _dayOfWeekOffset = static_cast<uint8_t>(src[4] % 7);
    _centuryRegister = src[5];
    _mode = src[6] <= static_cast<uint8_t>(TimeMode::Fixed) ? static_cast<TimeMode>(src[6]) : TimeMode::Host;
    _held = (src[7] & 0x01) != 0;
    _secondValid = (src[7] & 0x02) != 0;
    _offsetMicros = static_cast<int64_t>(GetU64(src + 8));
    _fixedMicros = static_cast<int64_t>(GetU64(src + 16));
    _anchorMicros = static_cast<int64_t>(GetU64(src + 24));
    _anchorEmulated = GetU64(src + 32);
    _heldFraction = static_cast<int64_t>(GetU64(src + 40));
    _lastSecond = static_cast<int64_t>(GetU64(src + 48));
    _heldTime.year = static_cast<int16_t>(src[56] | (src[57] << 8));
    _heldTime.month = src[58];
    _heldTime.day = src[59];
    _heldTime.hour = src[60];
    _heldTime.minute = src[61];
    _heldTime.second = src[62];
    _rawValid = src[64] != 0;
    _rawTime.year = static_cast<int16_t>(src[65] | (src[66] << 8));
    _rawTime.month = src[67];
    _rawTime.day = src[68];
    _rawTime.hour = src[69];
    _rawTime.minute = src[70];
    _rawTime.second = src[71];
    _rawSecond = static_cast<int64_t>(GetU64(src + 72));
    std::memcpy(_cells.data(), src + 80, kMaxCells);
}

/// endregion </TTD>
