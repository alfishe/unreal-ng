#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

#define MAX_WIDTH_P (64*2)
#define MAX_WIDTH 512
#define MAX_HEIGHT 320
#define MAX_BUFFERS 8

#define VID_TACTS	224		// CPU ticks per video scan line (@3.5MHz)
#define VID_LINES	320		// Full screen height in lines for standard ZX-Spectrum
#define VID_WIDTH	448		// Full screen width for standard ZX-Spectrum
#define VID_HEIGHT	320		// Full screen height in pixels (same as in scan lines)

#define MEM_CYCLES VID_TACTS*2

enum VideoModeEnum
{
	M_BRD = 0,	// Border only
	M_NUL,		// Non-existing mode
	M_ZX,		// Sinclair
	M_PMC,		// Pentagon Multicolor
	M_P16,		// Pentagon 16c
	M_P384,		// Pentagon 384x304
	M_PHR,		// Pentagon HiRes
	M_TS16,		// TS 16c
	M_TS256,	// TS 256c
	M_TSTX,		// TS Text
	M_ATM16,	// ATM 16c
	M_ATMHR,	// ATM HiRes
	M_ATMTX,	// ATM Text
	M_ATMTL,	// ATM Text Linear
	M_PROFI,	// Profi
	M_GMX,		// GMX
};

enum RASTER_N
{
	R_256_192 = 0,	// Sinclair
	R_320_200,		// ATM, TS
	R_320_240,		// TS
	R_360_288,		// TS
	R_384_304,		// AlCo
	R_512_240,		// Profi
	R_MAX
};

struct RASTER
{
	RASTER_N num;
	uint32_t u_brd;	// first pixel line
	uint32_t d_brd;	// first lower border line
	uint32_t l_brd;	// first pixel tact
	uint32_t r_brd;	// first right border tact
	uint32_t r_ts;	// tact on which call TS engine draw
};

struct VideoControl
{
	uint32_t 		clut[256];		// TS palette LUT in truecolor
	RASTER			raster;			// raster parameters
	VideoModeEnum	mode;			// renderer mode
	VideoModeEnum	mode_next;		// renderer mode, delayed to the start of the line
	uint32_t 		t_next;			// next tact to be rendered
	uint32_t		vptr;			// address in videobuffer
	uint32_t		xctr;			// videocontroller X counter
	uint32_t		yctr;			// videocontroller absolute Y counter (used for TS)
	uint32_t		ygctr;			// videocontroller graphics Y counter (used for graphics)
	uint32_t 		buf;			// active video buffer
	uint32_t 		flash;			// flash counter
	uint16_t		line;			// current rendered line
	uint16_t		line_pos;	// current rendered position in line
	uint16_t		ts_pos;		// current rendered position in tsline
	uint8_t			tsline[2][512];	// TS buffers (indexed colors)
	uint16_t		memvidcyc[320];	// number of memory cycles used in every video line by video
	uint16_t		memcpucyc[320];	// number of memory cycles used in every video line by CPU
	uint16_t		memtsscyc[320];	// number of memory cycles used in every video line by TS sprites
	uint16_t		memtstcyc[320];	// number of memory cycles used in every video line by TS tiles
	uint16_t		memdmacyc[320]; // number of memory cycles used in every video line by DMA
	uint16_t		memcyc_lcmd;	// number of memory cycles used in last command
};

class Screen
{
protected:
	EmulatorContext* _context;

public:
	VideoControl _vid;

public:
	Screen(EmulatorContext* context);
	void InitFrame();
	void UpdateScreen();
};