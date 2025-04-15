#include "stdafx.h"

#include "common/modulelogger.h"
#include "emulator/platform.h"
#include "ports.h"
#include <algorithm>

Ports::Ports(EmulatorContext* context)
{
	_context = context;
	_logger = _context->pModuleLogger;
}

Ports::~Ports()
{
	_context = nullptr;

	MLOGDEBUG("Ports::~Ports()");
}

// Input: ports 7FFD,1FFD,DFFD,FFF7,FF77,EFF7, flags CF_TRDOS,CF_CACHEON
void Ports::SetBanks()
{
	
}

void Ports::Reset()
{
	// Call reset handler for each registered module
	//for_each(_resetHandlers._callbacks.begin(), _resetHandlers._callbacks.end(), [](auto x)
	//{

   //});
}

void Ports::Out(uint16_t port, uint8_t val)
{
    [[maybe_unused]]
    const CONFIG& config = _context->config;

    [[maybe_unused]] uint8_t p1 = (port & 0xFF);          // lower 8 bits of port address
    [[maybe_unused]] uint8_t p2 = ((port >> 8) & 0xFF);   // higher 8 bits of port address
	_brk_port_out = port;
	_brk_port_val = val;

        MLOGDEBUG_SUBMODULE(PlatformIOSubmodulesEnum::SUBMODULE_IO_OUT, "Port OUT: 0x%04X = 0x%02X", port, val);
}

uint8_t Ports::In(uint16_t port)
{
	EmulatorState& state = _context->emulatorState;
    const CONFIG& config = _context->config;

	uint8_t result = 0xFF;

	_brk_port_in = port;

    [[maybe_unused]] uint8_t p1 = (port & 0xFF);
    [[maybe_unused]] uint8_t p2 = ((port >> 8) & 0xFF);

	if (config.ulaplus)
	{
		if (port == 0xFF3B)
		{
			// ULA+ DATA
			result = (!(state.ulaplus_reg & 0xC0) && (state.ulaplus_mode & 1)) ? state.ulaplus_cram[state.ulaplus_reg] : 0xFF;
		}
	}

	_brk_port_val = result;
	const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_IN;
	MLOGDEBUG("Port IN: 0x%04X = 0x%02X", port, result);
	return result;
}
