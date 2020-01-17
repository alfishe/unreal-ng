#pragma once
#include "stdafx.h"

class SystemHelper
{
public:
	static void GetCPUID(unsigned cpuInfo[4], unsigned _eax)
	{
		#ifdef _MSC_VER
			__cpuid((int *)cpuInfo, _eax);
		#endif

		#ifdef __GNUC__
			__cpuid(_eax, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
		#endif
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

		return result;
	}
};