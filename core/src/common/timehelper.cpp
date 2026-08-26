#include "stdafx.h"

#include "timehelper.h"

#ifdef __APPLE__
#include <mach/clock_types.h>
#endif

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

void sleep_ms(uint32_t ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void sleep_us(uint32_t us)
{
	std::this_thread::sleep_for(std::chrono::microseconds(us));
}


/// Precise deadline sleep with abort polling - see timehelper.h for the rationale
bool TimeHelper::WaitUntilPrecise(std::chrono::steady_clock::time_point deadline, const std::function<bool()>& shouldAbort)
{
    using clock = std::chrono::steady_clock;

    // Poll the abort predicate at least this often; also bounds how long a single OS wait can overshoot
    constexpr auto kMaxChunk = std::chrono::milliseconds(4);

#ifdef _WIN32
    // One high-resolution waitable timer per thread (Windows 10 1803+; the plain timer is the fallback and still
    // beats the ~10 ms granularity of the C++ library waits on MinGW)
    thread_local HANDLE timer = []() {
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,
                                          TIMER_ALL_ACCESS);
        if (h == nullptr)
            h = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        return h;
    }();
#endif

    for (;;)
    {
        if (shouldAbort && shouldAbort())
            return false;

        const auto now = clock::now();
        if (now >= deadline)
            return true;

        auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
        if (remaining > kMaxChunk)
            remaining = kMaxChunk;

#ifdef _WIN32
        if (timer != nullptr)
        {
            LARGE_INTEGER due;
            due.QuadPart = -static_cast<LONGLONG>(remaining.count()) * 10;  // 100 ns units, relative
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
            {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
        }
        // No timer at all: coarse Sleep() for the bulk, the loop's time check keeps us from returning early
        if (remaining > std::chrono::milliseconds(2))
            Sleep(1);
        else
            std::this_thread::yield();
#else
        std::this_thread::sleep_until(now + remaining);
#endif
    }
}
