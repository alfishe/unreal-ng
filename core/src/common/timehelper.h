#pragma once
#include "stdafx.h"

#include <chrono>
#include <ctime>
#include <functional>
#include <thread>
#include <tuple>
#include <time.h>

typedef std::chrono::high_resolution_clock::time_point chrono_time_t;
typedef std::chrono::high_resolution_clock hiresclock;

class TimeHelper
{
public:
	static chrono_time_t GetPrecisionTime();
	static unsigned GetTimeIntervalNs(chrono_time_t t1, chrono_time_t t2);
	static unsigned GetTimeIntervalUs(chrono_time_t t1, chrono_time_t t2);
	static unsigned GetTimeIntervalMs(chrono_time_t t1, chrono_time_t t2);

    /// region <Low-level timestamps>
    inline static uint64_t GetTimestampNs()
    {
        uint64_t result = 0;

#if not defined _WIN32
        struct timespec ts;

        if (timespec_get(&ts, TIME_UTC))
        {
            result = (uint64_t)ts.tv_sec * 1'000'000'000 + (uint64_t)ts.tv_nsec;
        }
#endif

        return result;
    }

    inline static uint64_t GetTimestampUs()
    {
        struct timeval tv;
        uint64_t result;

        gettimeofday(&tv, NULL);
        result = (uint64_t)tv.tv_sec * 1'000'000 + (uint64_t)tv.tv_usec;

        return result;
    }

    inline static uint64_t GetTimestampMs()
    {
        uint64_t result = 0;
    
    #if defined(WIN32)
        LARGE_INTEGER frequency;
        LARGE_INTEGER ticks;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&ticks);
    
        uint64_t divider = frequency.QuadPart / 1'000'000;   // Convert from milliseconds to nanoseconds
    
        result = ticks.QuadPart / divider;
    #else
        struct timespec ts;
        if (timespec_get(&ts, TIME_UTC))
        {
            result = (uint64_t)ts.tv_sec * 1'000'000'000 + (uint64_t)ts.tv_nsec;
        }
#endif

        return result;
    }
    /// endregion </Low-level timestamps>
};

//
// stl::thread powered cross-platform sleep()
//
void sleep_ms(uint32_t ms);
void sleep_us(uint32_t us);

// Examples:
// auto t1 = measure_ms(normalFunction);
// auto t2 = measure_ms(&X::memberFunction, obj, 4);
// auto t3 = measure_ms(lambda, 2, 3);
template<typename Function, typename... Args>
unsigned measure_ms(Function&& toTime, Args&&... a)
{
	auto t1{std::chrono::steady_clock::now()};
	std::invoke(std::forward<Function>(toTime), std::forward<Args>(a)...);
	auto t2{std::chrono::steady_clock::now()};
	return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
}

// Examples:
// auto t1 = measure_us(normalFunction);
// auto t2 = measure_us(&X::memberFunction, obj, 4);
// auto t3 = measure_us(lambda, 2, 3);
template<typename Function, typename... Args>
unsigned measure_us(Function&& toTime, Args&&... a)
{
	auto t1{ std::chrono::steady_clock::now() };
	std::invoke(std::forward<Function>(toTime), std::forward<Args>(a)...);
	auto t2{ std::chrono::steady_clock::now() };
	return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count());
}

// Original algorithm source: https://howardhinnant.github.io/date_algorithms.html#days_from_civil
// Returns number of days since civil 1970-01-01.  Negative values indicate
//    days prior to 1970-01-01.
// Preconditions:  y-m-d represents a date in the civil (Gregorian) calendar
//                 m is in [1, 12]
//                 d is in [1, last_day_of_month(y, m)]
//                 y is "approximately" in
//                   [numeric_limits<Int>::min()/366, numeric_limits<Int>::max()/366]
//                 Exact range of validity is:
//                 [civil_from_days(numeric_limits<Int>::min()),
//                  civil_from_days(numeric_limits<Int>::max()-719468)]
template <class Int>
constexpr Int days_from_civil(Int y, unsigned m, unsigned d) noexcept
{
	static_assert(std::numeric_limits<unsigned>::digits >= 18,
		"This algorithm has not been ported to a 16 bit unsigned integer");
	static_assert(std::numeric_limits<Int>::digits >= 20,
		"This algorithm has not been ported to a 16 bit signed integer");
	y -= m <= 2;
	const Int era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);      // [0, 399]
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;  // [0, 365]
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;         // [0, 146096]
	return era * 146097 + static_cast<Int>(doe) - 719468;
}

// Returns year/month/day triple in civil calendar
// Preconditions:  z is number of days since 1970-01-01 and is in the range:
//                   [numeric_limits<Int>::min(), numeric_limits<Int>::max()-719468].
template <class Int>
constexpr std::tuple<Int, unsigned, unsigned> civil_from_days(Int z) noexcept
{
	static_assert(std::numeric_limits<unsigned>::digits >= 18,
		"This algorithm has not been ported to a 16 bit unsigned integer");
	static_assert(std::numeric_limits<Int>::digits >= 20,
		"This algorithm has not been ported to a 16 bit signed integer");
	z += 719468;
	const Int era = (z >= 0 ? z : z - 146096) / 146097;
	const unsigned doe = static_cast<unsigned>(z - era * 146097);				// [0, 146096]
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;	// [0, 399]
	const Int y = static_cast<Int>(yoe) + era * 400;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               // [0, 365]
	const unsigned mp = (5 * doy + 2) / 153;									// [0, 11]
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;                            // [1, 31]
	const unsigned m = mp + (mp < 10 ? 3 : -9);									// [1, 12]
	return std::tuple<Int, unsigned, unsigned>(y + (m <= 2), m, d);
}

// Returns day of week in civil calendar [0, 6] -> [Sun, Sat]
// Preconditions:  z is number of days since 1970-01-01 and is in the range:
//                   [numeric_limits<Int>::min(), numeric_limits<Int>::max()-4].
template <class Int>
constexpr unsigned weekday_from_days(Int z) noexcept
{
	return static_cast<unsigned>(z >= -4 ? (z + 4) % 7 : (z + 5) % 7 + 6);
}

template <class To, class Rep, class Period>
To round_down(const std::chrono::duration<Rep, Period>& d)
{
	To t = std::chrono::duration_cast<To>(d);
	if (t > d)
		--t;
	return t;
}

// Original algorithm source: https://stackoverflow.com/questions/16773285/how-to-convert-stdchronotime-point-to-stdtm-without-using-time-t
template <class Duration>
std::tm make_utc_tm(std::chrono::time_point<std::chrono::system_clock, Duration> tp)
{
    typedef unsigned int timeRepresentation;
    typedef std::chrono::duration<timeRepresentation> seconds;
    typedef std::chrono::duration<timeRepresentation, std::ratio<60>> minutes;
    typedef std::chrono::duration<timeRepresentation, std::ratio<3600>> hours;
    typedef std::chrono::duration<timeRepresentation, std::ratio_multiply<hours::period, std::ratio<24>>> days;

    // t is time duration since 1970-01-01
    Duration t = tp.time_since_epoch();

    // d is days since 1970-01-01
    days d = round_down<days>(t);

    // t is now time duration since midnight of day d
    t -= d;

    // break d down into year/month/day
    int year;
    unsigned month;
    unsigned day;
    std::tie(year, month, day) = civil_from_days(d.count());

    // Initialize tm struct with all fields
    std::tm tm = {};  // Zero-initialize all members
    tm.tm_sec = 0;
    tm.tm_min = 0;
    tm.tm_hour = 0;
    tm.tm_mday = static_cast<int>(day);
    tm.tm_mon = static_cast<int>(month) - 1;
    tm.tm_year = year - 1900;
    tm.tm_wday = static_cast<int>(weekday_from_days(static_cast<int>(d.count())));
    tm.tm_yday = static_cast<int>(d.count() - days_from_civil(year, 1, 1));
    tm.tm_isdst = 0;
    #if !defined(_WIN32)
    tm.tm_gmtoff = 0;
    tm.tm_zone = nullptr;
    #endif

    // Fill in the time
    auto hours_count = std::chrono::duration_cast<hours>(t).count();
    tm.tm_hour = static_cast<int>(hours_count);
    t -= hours(hours_count);

    auto minutes_count = std::chrono::duration_cast<minutes>(t).count();
    tm.tm_min = static_cast<int>(minutes_count);
    t -= minutes(minutes_count);

    auto seconds_count = std::chrono::duration_cast<seconds>(t).count();
    tm.tm_sec = static_cast<int>(seconds_count);

    return tm;
}
