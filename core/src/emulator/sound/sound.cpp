#include "stdafx.h"

#include "sound.h"

Sound::Sound(EmulatorContext* context)
{
	_context = context;
}

void Sound::Reset()
{
	CONFIG& config = _context->config;

	/*
	ay[0].reset();
	ay[1].reset();
	Saa1099.reset();
	zxmmoonsound.reset();

	if (config.sound.ay_scheme == AY_SCHEME_CHRV)
		out(0xfffd, 0xff);

#ifdef MOD_GS
	if (config.sound.gsreset)
		reset_gs();
#endif
	*/
}