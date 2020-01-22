#include "stdafx.h"

#include "ram.h"

const uint32_t RAM::MaxRAMSize()
{
	static uint32_t result = MAX_RAM_SIZE;

	return result;
}

void RAM::FillRandom()
{
	//uint8_t *ptrMemory = page_ram(0);
	//for (uint32_t i = 0; i < (4096 * 1024); i++)
	//	ptrMemory[i] = rand();
}
