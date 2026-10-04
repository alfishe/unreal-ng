#include "emulator/io/serial/esp/zififattime.h"

#include <cstdio>

// A port of ZiFi-ESP32-S3-Zero 2e5ba83 src/fat_time.cpp (the same arithmetic, the same limits)

namespace zififat
{
namespace
{

constexpr int64_t kSecondsPerDay = 86400;

int64_t DaysFromCivil(int64_t year, unsigned month, unsigned day)
{
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned shiftedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * shiftedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + static_cast<int64_t>(dayOfEra) - 719468;
}

void CivilFromDays(int64_t days, int& year, int& month, int& day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned dayOfEra = static_cast<unsigned>(days - era * 146097);
    const unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const unsigned shiftedMonth = (5 * dayOfYear + 2) / 153;
    day = static_cast<int>(dayOfYear - (153 * shiftedMonth + 2) / 5 + 1);
    month = static_cast<int>(shiftedMonth < 10 ? shiftedMonth + 3 : shiftedMonth - 9);
    year = static_cast<int>(static_cast<int64_t>(yearOfEra) + era * 400 + (month <= 2 ? 1 : 0));
}

bool LeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int DaysInMonth(int year, int month)
{
    static const uint8_t kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2 && LeapYear(year) ? 29 : kDays[month - 1];
}

bool ValidCivil(int year, int month, int day, int hour, int minute, int second)
{
    return year >= 1 && year <= 9999 && month >= 1 && month <= 12 && day >= 1 && day <= DaysInMonth(year, month) &&
           hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
}

int64_t FloorDiv(int64_t value, int64_t divisor)
{
    int64_t quotient = value / divisor;
    if (value % divisor != 0 && (value < 0) != (divisor < 0))
        --quotient;
    return quotient;
}

bool TakeDigits(const char*& cursor, int count, int& value)
{
    value = 0;
    for (int index = 0; index < count; ++index)
    {
        const char digit = cursor[index];
        if (digit < '0' || digit > '9')
            return false;
        value = value * 10 + (digit - '0');
    }
    cursor += count;
    return true;
}

}  // namespace

int64_t CivilToUnix(int year, int month, int day, int hour, int minute, int second)
{
    return DaysFromCivil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * kSecondsPerDay +
           static_cast<int64_t>(hour) * 3600 + minute * 60 + second;
}

void UnixToCivil(int64_t unixSeconds, int& year, int& month, int& day, int& hour, int& minute, int& second)
{
    const int64_t days = FloorDiv(unixSeconds, kSecondsPerDay);
    const int64_t rest = unixSeconds - days * kSecondsPerDay;
    CivilFromDays(days, year, month, day);
    hour = static_cast<int>(rest / 3600);
    minute = static_cast<int>(rest % 3600 / 60);
    second = static_cast<int>(rest % 60);
}

bool StampToCivil(Stamp stamp, int& year, int& month, int& day, int& hour, int& minute, int& second)
{
    if (!stamp.Known())
        return false;
    year = 1980 + (stamp.date >> 9);
    month = (stamp.date >> 5) & 0x0F;
    day = stamp.date & 0x1F;
    hour = stamp.time >> 11;
    minute = (stamp.time >> 5) & 0x3F;
    second = (stamp.time & 0x1F) * 2;
    return ValidCivil(year, month, day, hour, minute, second);
}

bool StampToUnix(Stamp stamp, int32_t timezoneSeconds, int64_t& unixSeconds)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!StampToCivil(stamp, year, month, day, hour, minute, second))
        return false;
    unixSeconds = CivilToUnix(year, month, day, hour, minute, second) - timezoneSeconds;
    return true;
}

bool UnixToStamp(int64_t unixSeconds, int32_t timezoneSeconds, Stamp& stamp)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    UnixToCivil(unixSeconds + timezoneSeconds, year, month, day, hour, minute, second);
    if (year < 1980 || year > 2107)
        return false;
    stamp.date = static_cast<uint16_t>(((year - 1980) << 9) | (month << 5) | day);
    stamp.time = static_cast<uint16_t>((hour << 11) | (minute << 5) | (second / 2));
    return true;
}

bool ParseFtpTimeVal(const char* text, int64_t& unixSeconds, const char** end)
{
    if (text == nullptr)
        return false;
    const char* cursor = text;
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!TakeDigits(cursor, 4, year) || !TakeDigits(cursor, 2, month) || !TakeDigits(cursor, 2, day) ||
        !TakeDigits(cursor, 2, hour) || !TakeDigits(cursor, 2, minute) || !TakeDigits(cursor, 2, second))
        return false;
    if (*cursor == '.')
    {
        ++cursor;
        if (*cursor < '0' || *cursor > '9')
            return false;
        while (*cursor >= '0' && *cursor <= '9')
            ++cursor;
    }
    if (!ValidCivil(year, month, day, hour, minute, second))
        return false;
    unixSeconds = CivilToUnix(year, month, day, hour, minute, second);
    if (end != nullptr)
        *end = cursor;
    return true;
}

void FormatFtpTimeVal(int64_t unixSeconds, char* output)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    UnixToCivil(unixSeconds, year, month, day, hour, minute, second);
    char text[64];
    std::snprintf(text, sizeof(text), "%04d%02d%02d%02d%02d%02d", year, month, day, hour, minute, second);
    for (int i = 0; i < 14; ++i)
        output[i] = text[i];
    output[14] = 0;
}

}  // namespace zififat
