#pragma once
#include "stdafx.h"

#include <cassert>

class SystemHelper
{
public:
    static void GetCPUID([[maybe_unused]] unsigned cpuInfo[4], [[maybe_unused]] unsigned _eax)
    {
        (void)cpuInfo;  // Mark as intentionally unused
        (void)_eax;     // Mark as intentionally unused

#ifdef _MSC_VER
        __cpuid((int *)cpuInfo, _eax);
#endif

#if defined(__GNUC__) && defined(__x86_64__)
        __cpuid(_eax, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
#endif
    }

	static unsigned GetCPUID(unsigned _eax, int ext)
	{
		unsigned cpuInfo[4];

		GetCPUID(cpuInfo, _eax);

		return ext ? cpuInfo[3] : cpuInfo[0];
	}

	static void GetCPUString(char dst[49])
	{
		dst[0] = dst[12] = dst[48] = 0;
		unsigned CpuInfo[4];
		unsigned *d = (unsigned *)dst;

		GetCPUID(CpuInfo, 0x80000000);
		if (CpuInfo[0] < 0x80000004)
		{
			GetCPUID(CpuInfo, 0);
			d[0] = CpuInfo[1];
			d[1] = CpuInfo[3];
			d[2] = CpuInfo[2];
			return;
		}

		GetCPUID(CpuInfo, 0x80000002);
		d[0] = CpuInfo[0];
		d[1] = CpuInfo[1];
		d[2] = CpuInfo[2];
		d[3] = CpuInfo[3];

		GetCPUID(CpuInfo, 0x80000003);
		d[4] = CpuInfo[0];
		d[5] = CpuInfo[1];
		d[6] = CpuInfo[2];
		d[7] = CpuInfo[3];

		GetCPUID(CpuInfo, 0x80000004);
		d[8] = CpuInfo[0];
		d[9] = CpuInfo[1];
		d[10] = CpuInfo[2];
		d[11] = CpuInfo[3];
	}

	static uint64_t GetCPUFrequency()
	{
		uint64_t result = 0;

		#if defined _WIN32 && defined MSVC

		LARGE_INTEGER Frequency;
		LARGE_INTEGER Start;
		LARGE_INTEGER Stop;
		uint64_t c1, c2, c3, c4, c;
		timeBeginPeriod(1);
		QueryPerformanceFrequency(&Frequency);
		Sleep(20);

		c1 = rdtsc();
		QueryPerformanceCounter(&Start);
		c2 = rdtsc();
		Sleep(500);
		c3 = rdtsc();
		QueryPerformanceCounter(&Stop);
		c4 = rdtsc();
		timeEndPeriod(1);
		c = c3 - c2 + (c4 - c3) / 2 + (c2 - c1) / 2;
		Start.QuadPart = Stop.QuadPart - Start.QuadPart;

		result = ((c * Frequency.QuadPart) / Start.QuadPart);

		#endif

		#if defined __linux__ && defined __x86_64__

		assert("GetCPUFrequency() not implemented for Linux");


		// See: https://stackoverflow.com/questions/11706563/how-can-i-programmatically-find-the-cpu-frequency-with-c
		unsigned cpuInfo[4];
		GetCPUID(cpuInfo, 0);

		// Intel CPUs with CPUID level >= 0x16 (Skylake, Kabylake)
		if (cpuInfo[0] >= 0x16)
		{
            GetCPUID(cpuInfo, 0x16);

            // Return base frequency in MHz
            result = cpuInfo[0];
		}

        #endif

        #ifdef __APPLE__

        size_t size = sizeof(result);
        sysctlbyname("hw.cpufrequency", &result, &size, nullptr, 0);

        #endif

		return result;
	}
};