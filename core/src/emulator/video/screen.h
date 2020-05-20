#pragma once
#include "stdafx.h"

#include "emulator/platform.h"
#include "emulator/emulatorcontext.h"

#include <algorithm>

using namespace std;

class Z80;
class Renderers;

//region Constants

// Video framebuffer and rendering related
#define MAX_WIDTH_P (64 * 2)
#define MAX_WIDTH 512
#define MAX_HEIGHT 320
#define MAX_BUFFERS 8

#define VID_TACTS	224		// CPU ticks per video scan line (@3.5MHz)
#define VID_LINES	320		// Full screen height in lines for standard ZX-Spectrum
#define VID_WIDTH	448		// Full screen width for standard ZX-Spectrum
#define VID_HEIGHT	320		// Full screen height in pixels (same as in scan lines)

#define MEM_CYCLES (VID_TACTS * 2)

//endregion Constants

//region Enumerations

enum VideoModeEnum : uint8_t
{
    M_NUL = 0,	// Non-existing mode
    M_ZX,		// Sinclair ZX Spectrum
    M_PMC,		// Pentagon Multicolor
    M_P16,		// Pentagon 16c
    M_P384,		// Pentagon 384x304
    M_PHR,		// Pentagon HiRes

	M_TIMEX,     // Timex with 32 x 192 attributes (2 colors per line)

	M_TS16,		// TS 16c
	M_TS256,	// TS 256c
	M_TSTX,		// TS Text
	M_ATM16,	// ATM 16c
	M_ATMHR,	// ATM HiRes
	M_ATMTX,	// ATM Text
	M_ATMTL,	// ATM Text Linear
	M_PROFI,	// Profi
	M_GMX,		// GMX

    M_BRD,  	// Border only

	M_MAX
};

enum RasterModeEnum
{
	R_256_192 = 0,	    // Sinclair
	R_320_200 = 1,		// ATM, TS
	R_320_240 = 2,		// TS
	R_360_288 = 3,		// TS
	R_384_304 = 4,		// AlCo
	R_512_240 = 5,		// Profi

	R_MAX
};

//endregion Enumerations

//region Structures

struct RASTER
{
	RasterModeEnum num;
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
	uint16_t		line_pos;		// current rendered position in line

	uint16_t		ts_pos;			// current rendered position in tsline
	uint8_t			tsline[2][512];	// TS buffers (indexed colors)
	uint16_t		memvidcyc[320];	// Memory cycles used in every video line by video
	uint16_t		memcpucyc[320];	// Memory cycles used in every video line by CPU
	uint16_t		memtsscyc[320];	// Memory cycles used in every video line by TS sprites
	uint16_t		memtstcyc[320];	// Memory cycles used in every video line by TS tiles
	uint16_t		memdmacyc[320]; // Memory cycles used in every video line by DMA
	uint16_t		memcyc_lcmd;	// Memory cycles used in last command
};

struct RasterDescriptor
{
    uint16_t fullFrameWidth;
    uint16_t fullFrameHeight;

    uint16_t screenWidth;
    uint16_t screenHeight;

    uint16_t screenOffsetLeft;
    uint16_t screenOffsetTop;
};

struct FramebufferDescriptor
{
    VideoModeEnum videoMode = M_NUL;

	uint16_t width = 0;
	uint16_t height = 0;

	uint8_t* memoryBuffer = nullptr;
	size_t memoryBufferSize = 0;
};

//endregion Structures

// ULA+ color models:
//
// val  red/grn     blue1       blue2
// 0    00000000    00000000    00000000
// 1    00100100
// 2    01001001
// 3    01101101    01101101    01101101
// 4    10010010                10010010
// 5    10110110    10110110
// 6    11011011
// 7    11111111    11111111    11111111

// ULA+ palette cell select:
// bit5 - FLASH
// bit4 - BRIGHT
// bit3 - 0 - INK / 1 - PAPER
// bits0..2 - INK / PAPER

// Extract colors
#define col_def(a) (((a) << 5) | ((a) << 2) |  ((a) >> 1))
#define col_r(a) (col_def(a) << 16)
#define col_g(a) (col_def(a) << 8)
#define col_b(a) (col_def(a))


typedef void(Screen::* DrawCallback)(uint32_t n);

class Screen
{
public:
	static constexpr uint32_t rb2_offs = MAX_HEIGHT * MAX_WIDTH_P;
	static constexpr uint32_t sizeof_rbuf = rb2_offs * (MAX_BUFFERS + 2);
	static constexpr uint32_t sizeof_vbuf = VID_HEIGHT * VID_WIDTH * 2;

#ifdef CACHE_ALIGNED
	CACHE_ALIGNED uint8_t rbuf[sizeof_rbuf];
	CACHE_ALIGNED uint32_t vbuf[2][sizeof_vbuf];
#else
	uint8_t rbuf[sizeof_rbuf];
	uint32_t vbuf[2][sizeof_vbuf];
#endif

	// Video raster mode descriptors
	const RASTER raster[R_MAX] =
	{
		{ R_256_192, 80, 272, 70, 70 + 128, 198 },  // Genuine ZX-Spectrum screen
		//{ R_256_192, 80, 272, 58, 186, 198 },
		{ R_320_200, 76, 276, 54, 214, 214 },
		{ R_320_240, 56, 296, 54, 214, 214 },
		{ R_360_288, 32, 320, 44, 224, 0 },
		{ R_384_304, 16, 320, 32, 224, 0 },
		{ R_512_240, 56, 296, 70, 198, 0 },
	};

	/// Raster descriptors for each video mode
	const RasterDescriptor rasterDescriptors[M_MAX] =
    {
	    { 0, 0, 0, 0, 0, 0 },           // M_NUL
	    { 352, 312, 256, 192, 48, 32 }  // M_ZX
    };

	// Default color table: 0RRrrrGG gggBBbbb
	uint16_t spec_colors[16] =
	{
		0x0000,
		0x0010,
		0x4000,
		0x4010,
		0x0200,
		0x0210,
		0x4200,
		0x4210,
		0x0000,
		0x0018,
		0x6000,
		0x6018,
		0x0300,
		0x0318,
		0x6300,
		0x6318
	};

	const uint32_t cr[8] =
	{
		col_r(0),
		col_r(1),
		col_r(2),
		col_r(3),
		col_r(4),
		col_r(5),
		col_r(6),
		col_r(7)
	};

	const uint32_t cg[8] =
	{
		col_g(0),
		col_g(1),
		col_g(2),
		col_g(3),
		col_g(4),
		col_g(5),
		col_g(6),
		col_g(7)
	};

	const uint32_t cb[2][4] =
	{
		col_b(0),
		col_b(3),
		col_b(5),
		col_b(7),

		col_b(0),
		col_b(3),
		col_b(4),
		col_b(7)
	};

protected:
	EmulatorContext* _context = nullptr;
	CPU* _system = nullptr;
	Z80* _cpu = nullptr;

	VideoModeEnum _mode;
	FramebufferDescriptor _framebuffer;
	DrawCallback _currentDrawCallback;
	DrawCallback _nullCallback;
    DrawCallback _drawCallback;
	DrawCallback _borderCallback;

	DrawCallback _drawCallbacks[M_MAX] =
	{
		&Screen::DrawNull,
		&Screen::DrawZX,
		&Screen::DrawPMC,
		&Screen::DrawP16,
		&Screen::DrawP384,
		&Screen::DrawPHR,
		&Screen::DrawTimex,
		&Screen::DrawTS16,
		&Screen::DrawTS256,
		&Screen::DrawTSText,
		&Screen::DrawATM16,
		&Screen::DrawATMHiRes,
		&Screen::DrawATM2Text,
		&Screen::DrawATM3Text,
		&Screen::DrawProfi,
		&Screen::DrawGMX,
        &Screen::DrawBorder
	};

public:
	VideoControl _vid;

public:
	Screen() = delete;		            // Disable default constructor; C++ 11 feature
	Screen(EmulatorContext* context);
	virtual ~Screen();

	void Init();

	virtual void InitFrame();
	virtual void InitRaster();
	virtual void InitMemoryCounters();

	virtual void UpdateScreen();

protected:
	void AllocateFramebuffer(VideoModeEnum mode);
	void DeallocateFramebuffer();
	void GetFramebufferData(uint8_t** buffer, size_t* size);
#ifdef _DEBUG
	void DumpFramebufferInfo(char* buffer, size_t len);
#endif

	void DrawScreenBorder(uint32_t n);

	// Draw helpers
public:
    static std::string GetVideoModeName(VideoModeEnum mode);
	void Draw(uint32_t n);

	void DrawNull(uint32_t n);		// Non-existing mode (skip draw)
	void DrawZX(uint32_t n);		// Authentic Sinclair ZX Spectrum
	void DrawPMC(uint32_t n);		// Pentagon Multicolor
	void DrawP16(uint32_t n);		// Pentagon 16c
	void DrawP384(uint32_t n);		// Pentagon 384x304
	void DrawPHR(uint32_t n);		// Pentagon HiRes
	void DrawTimex(uint32_t n);     // Timex
	void DrawTS16(uint32_t n);		// TS 16c
	void DrawTS256(uint32_t n);		// TS 256c
	void DrawTSText(uint32_t n);	// TS Text
	void DrawATM16(uint32_t n);		// ATM 16c
	void DrawATMHiRes(uint32_t n);	// ATM HiRes
	void DrawATM2Text(uint32_t n);	// ATM Text
	void DrawATM3Text(uint32_t n);	// ATM Text linear
	void DrawProfi(uint32_t n);		// Profi
	void DrawGMX(uint32_t n);		// GMX
    void DrawBorder(uint32_t n);	// Border only
};