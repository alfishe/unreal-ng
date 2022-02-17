#include "stdafx.h"

#include "common/logger.h"

#include "emulator/io/hdd/hdd.h"

HDD::HDD(EmulatorContext* context)
{
	_context = context;
}

void HDD::Reset()
{
	EmulatorState& state = _context->emulatorState;

	state.ide_hi_byte_r = 0;
	state.ide_hi_byte_w = 0;
	state.ide_hi_byte_w1 = 0;
}