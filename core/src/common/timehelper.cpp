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

void sleep_ms(uint32_t ms)
{
	std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void sleep_us(uint32_t us)
{
	std::this_thread::sleep_for(std::chrono::microseconds(us));
}

