#include "stdafx.h"

#include "proficmos.h"

void ProfiCMOS::SetCMOSType(CMOSTypeEnum type)
{
    _cmos_type = type;
}

void ProfiCMOS::SetCMOSAddress(uint8_t addr)
{
    _cmos_addr = addr;
}

void ProfiCMOS::SetFixedTime(time_t t)
{
    _fixedTime = true;
    _fixedTimeValue = t;
}

void ProfiCMOS::UseLiveTime()
{
    _fixedTime = false;
}

void ProfiCMOS::WriteCMOS(uint8_t val)
{
    uint8_t cur_addr = _cmos_addr;

    if (_cmos_type == Rus512)
        cur_addr = cur_addr & 0x3F;

    _cmos[cur_addr] = val;
}

// Host local time
static tm make_local_tm(std::time_t t)
{
    tm result = {};

#ifdef _WIN32
    localtime_s(&result, &t);
#else
    localtime_r(&t, &result);
#endif

    return result;
}

static tm make_local_tm()
{
    return make_local_tm(std::time(nullptr));
}

uint8_t ProfiCMOS::ReadCMOS()
{
    tm& time = _lastTime;
    bool& UF = _updateFinished;

    uint8_t result = 0;
    uint8_t cur_addr = _cmos_addr;

    if (_cmos_type == Rus512)
        cur_addr = cur_addr & 0x3F;

    // If Time/Date values requested from CMOS - provide current Host system values.
    // The clock is sampled at most twice a second; the Update-Ended Flag is raised
    // when the wall-clock second changes.
    //
    // Deterministic mode (_fixedTime) always serves the frozen instant and skips the
    // sampling throttle entirely - a frozen clock never ticks, so there is nothing
    // for UF to report.
    if ((1 << cur_addr) & ((1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 12)))
    {
        if (_fixedTime)
        {
            time = make_local_tm(_fixedTimeValue);
        }
        else
        {
            auto now = std::chrono::steady_clock::now();
            if (!_timeValid || now - _lastSample >= std::chrono::milliseconds(500))
            {
                _timeValid = true;
                _lastSample = now;
                time = make_local_tm();

                if ((unsigned)time.tm_sec != _seconds)
                {
                    UF = true;
                    _seconds = (unsigned)time.tm_sec;
                }
            }
        }
    }

    switch (cur_addr)
    {
        case CMOSMemoryEnum::Second:
            result = DecodeFromBCD((uint8_t)time.tm_sec);
            break;
        case CMOSMemoryEnum::Minute:
            result = DecodeFromBCD((uint8_t)time.tm_min);
            break;
        case CMOSMemoryEnum::Hour:
            result = DecodeFromBCD((uint8_t)time.tm_hour);
            break;
        case CMOSMemoryEnum::DayOfWeek:
            // original: 1 + ((wDayOfWeek + 8 - conf.cmos) % 7)
            result = 1 + ((time.tm_wday + 8 - (int)_cmos_type) % 7);
            break;
        case CMOSMemoryEnum::Day:
            result = DecodeFromBCD((uint8_t)time.tm_mday);
            break;
        case CMOSMemoryEnum::Month:
            // std::tm::tm_mon is 0-based, Win32 SYSTEMTIME.wMonth is 1-based
            result = DecodeFromBCD((uint8_t)(time.tm_mon + 1));
            break;
        case CMOSMemoryEnum::Year:
            result = DecodeFromBCD(time.tm_year % 100);
            break;
        case CMOSMemoryEnum::Unknown_10:
            result = 0x20 | (_cmos[10] & 0xF); // molodcov_alex
            break;
        case CMOSMemoryEnum::BitFlags:
            result = (_cmos[11] & 4) | 2;
            break;
        case CMOSMemoryEnum::UF:  // [vv] UF
            result = UF ? 0x10 : 0;
            UF = false;
            break;
        case CMOSMemoryEnum::Unknown_13:
            result = 0x80;
            break;
        default:
            result = _cmos[cur_addr];
            break;
    }

    return result;
}

// Helper methods
uint8_t ProfiCMOS::DecodeFromBCD(uint8_t binary)
{
    if (!(_cmos[11] & 0x04))
        binary = (binary % 10) + 0x10 * ((binary / 10) % 10);

    return binary;
}
