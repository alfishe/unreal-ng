#include "stdafx.h"

#include "common/logger.h"

#include "ports.h"
#include <algorithm>

using namespace std;

Ports::Ports(EmulatorContext* context)
{
	_context = context;
}

Ports::~Ports()
{
	_context = nullptr;

	LOGDEBUG("Ports::~Ports()");
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

void Ports::Out(uint16_t port, uint8_t val)
{
	CONFIG& config = _context->config;

	uint8_t p1 = (port & 0xFF);          // lower 8 bits of port address
	uint8_t p2 = ((port >> 8) & 0xFF);   // higher 8 bits of port address
	_brk_port_out = port;
	_brk_port_val = val;


}

uint8_t Ports::In(uint16_t port)
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;

	uint8_t result = 0xFF;

	_brk_port_in = port;

	uint8_t p1 = (port & 0xFF);
	uint8_t p2 = ((port >> 8) & 0xFF);

	if (config.ulaplus)
	{
		if (port == 0xFF3B)
		{
			// ULA+ DATA
			result = (!(state.ulaplus_reg & 0xC0) && (state.ulaplus_mode & 1)) ? state.ulaplus_cram[state.ulaplus_reg] : 0xFF;
		}
	}

	_brk_port_val = result;
	return result;
}
