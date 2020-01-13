#include "stdafx.h"

#include "screen.h"

Screen::Screen(EmulatorContext* context)
{
	_context = context;
}

void Screen::InitFrame()
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;

	_vid.buf ^= 1;
	_vid.t_next = 0;
	_vid.vptr = 0;
	_vid.yctr = 0;
	_vid.ygctr = state.ts.g_yoffs - 1;
	_vid.line = 0;
	_vid.line_pos = 0;

	state.ts.g_yoffs_updated = 0;
	_vid.flash = state.frame_counter & 0x10;

	//init_raster();
	//init_memcycles();
}

void Screen::UpdateScreen()
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;

	// Get CPU cycles counter (relative to current frame)
	//uint32_t cput = (cpu.t >= config.frame) ? (VID_TACTS * VID_LINES) : cpu.t;
}