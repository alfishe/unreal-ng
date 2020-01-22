#include "stdafx.h"

#include "timehelper.h"

void sleep_ms(uint32_t ms)
{
	std::this_thread::sleep_for(std::chrono::microseconds(ms));
}
