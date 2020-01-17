#pragma once
#include "stdafx.h"

#include "emulator/emulatorcontext.h"

class EmulatorContext;

extern int spkr_dig;
extern int mic_dig;
extern int covFB_vol;
extern int covDD_vol;
extern int sd_l;
extern int sd_r;
extern int covProfiL, covProfiR;

class Sound
{
protected:
	EmulatorContext* _context;

public:
	Sound() = delete;			// Disable default constructor. C++ 11 feature
	Sound(EmulatorContext* context);

	void Reset();

	void apply_sound();
	void restart_sound();
	void flush_dig_snd();
	void init_snd_frame();
	void flush_snd_frame();
};
