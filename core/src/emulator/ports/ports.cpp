#include "stdafx.h"

#include "ports.h"

Ports::Ports(CPU* cpu)
{
	_cpu = cpu;
}

Ports::~Ports()
{
	_cpu = nullptr;
}

// Input: ports 7FFD,1FFD,DFFD,FFF7,FF77,EFF7, flags CF_TRDOS,CF_CACHEON
void Ports::SetBanks()
{
	
}

void Ports::Reset()
{
	// Call reset handler for each registered module
	for_each(_resetHandlers._callbacks.begin(), _resetHandlers._callbacks.end(), [](auto x)
	{

	});
}
