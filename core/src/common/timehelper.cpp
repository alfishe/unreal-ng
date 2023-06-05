#include <mach/clock_types.h>
#include "stdafx.h"

#include "timehelper.h"

chrono_time_t TimeHelper::GetPrecisionTime()
{
	return hiresclock::now();
}

unsigned TimeHelper::GetTimeIntervalNs(chrono_time_t t1, chrono_time_t t2)
{
	return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
}

unsigned TimeHelper::GetTimeIntervalUs(chrono_time_t t1, chrono_time_t t2)
{
	return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count());
}

unsigned TimeHelper::GetTimeIntervalMs(chrono_time_t t1, chrono_time_t t2)
{
	return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
}

/// region <Low-level timestamps>
uint64_t TimeHelper::GetTimestampNs()
{
    struct timespec ts;
    uint64_t result = 0;

    if (timespec_get(&ts, TIME_UTC))
    {
        result = (uint64_t) ts.tv_sec * 1'000'000'000 + (uint64_t) ts.tv_nsec;
    }

    return result;
}

uint64_t GetTimestampUs()
{
    struct timeval tv;
    uint64_t result;

    gettimeofday(&tv, NULL);
    result = (uint64_t)tv.tv_sec * 1'000'000 + (uint64_t)tv.tv_usec;

    return result;
}

uint64_t GetTimestampMs()
{
    struct timespec ts;
    uint64_t result = 0;

#if defined(WIN32)
    // TODO: add QueryPerformanceCounter implementation
#else
    if (timespec_get(&ts, TIME_UTC))
    {
        result = (uint64_t) ts.tv_sec * 1'000'000'000 + (uint64_t) ts.tv_nsec;
    }
#endif

    return result;
}
/// endregion </Low-level timestamps>

void sleep_ms(uint32_t ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void sleep_us(uint32_t us)
{
	std::this_thread::sleep_for(std::chrono::microseconds(us));
}

