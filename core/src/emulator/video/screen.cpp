#include <common/image/imagehelper.h>
#include "stdafx.h"

#include "common/logger.h"

#include "screen.h"

#include "emulator/cpu/cpu.h"
#include "emulator/cpu/z80.h"

/// region <Constructors / Destructors>

Screen::Screen(EmulatorContext* context)
{
	_system = context->pCPU;
	_cpu = _system->GetZ80();
	_context = context;
}

Screen::~Screen()
{
    if (_framebuffer.memoryBuffer != nullptr)
    {
        DeallocateFramebuffer();
    }
}

/// endregion </Constructors / Destructors>

/// region <Initialization>

void Screen::InitFrame()
{
	static COMPUTER& state = _context->state;
	static CONFIG& config = _context->config;

	_vid.buf ^= 0x00000001;						// Swap current video buffer
	_vid.t_next = 0;
	_vid.vptr = 0;
	_vid.yctr = 0;
	_vid.ygctr = state.ts.g_yoffs - 1;
	_vid.line = 0;								// Reset current render line
	_vid.line_pos = 0;							// Reset current render line position

	state.ts.g_yoffs_updated = 0;
	_vid.flash = state.frame_counter & 0x10;	// Flash attribute changes each 

	InitRaster();
	InitMemoryCounters();
}

//
// Set appropriate video mode based on ports for current platform
//
void Screen::InitRaster()
{
	static COMPUTER& state = _context->state;
	static CONFIG& config = _context->config;
	static VideoControl& video = _context->pScreen->_vid;

	VideoModeEnum prevMode = video.mode;

	//region Set current video mode

	// TSconf
	/*
	if (config.mem_model == MM_TSL)
	{
		video.raster = raster[state.ts.rres];
		EnterCriticalSection(&tsu_toggle_cr); // wbcbz7 note: huhuhuhuhuhuh...dirty code :)
		if ((state.ts.nogfx) || (!comp.ts.tsu.toggle.gfx)) { video.mode = M_BRD; LeaveCriticalSection(&tsu_toggle_cr); return; }
		if (state.ts.vmode == 0) { video.mode = M_ZX; LeaveCriticalSection(&tsu_toggle_cr); return; }
		if (state.ts.vmode == 1) { video.mode = M_TS16; LeaveCriticalSection(&tsu_toggle_cr); return; }
		if (state.ts.vmode == 2) { video.mode = M_TS256; LeaveCriticalSection(&tsu_toggle_cr); return; }
		if (state.ts.vmode == 3) { video.mode = M_TSTX; LeaveCriticalSection(&tsu_toggle_cr); return; }
	}
	*/

	uint8_t m = EFF7_4BPP | EFF7_HWMC;

	// ATM 1
	if ((config.mem_model == MM_ATM450) && (((state.aFE >> 5) & 3) != FF77_ZX))
	{
		video.raster = raster[R_320_200];
		if (((state.aFE >> 5) & 3) == aFE_16) { video.mode = M_ATM16; return; }
		if (((state.aFE >> 5) & 3) == aFE_MC) { video.mode = M_ATMHR; return; }
		video.mode = M_NUL;
	}

	// ATM 2 & 3
	if ((config.mem_model == MM_ATM710 || config.mem_model == MM_ATM3) && ((state.pFF77 & 7) != FF77_ZX))
	{
		video.raster = raster[R_320_200];
		if (config.mem_model == MM_ATM3 && (state.pEFF7 & m)) { video.mode = M_NUL; return; }	// EFF7 AlCo bits must be 00, or invalid mode
		if ((state.pFF77 & 7) == FF77_16) { video.mode = M_ATM16; return; }
		if ((state.pFF77 & 7) == FF77_MC) { video.mode = M_ATMHR; return; }
		if ((state.pFF77 & 7) == FF77_TX) { video.mode = M_ATMTX; return; }
		if (config.mem_model == MM_ATM3 && (state.pFF77 & 7) == FF77_TL) { video.mode = M_ATMTL; return; }
		video.mode = M_NUL;
	}

	video.raster = raster[R_256_192];

	// ATM 3 AlCo modes
	if (config.mem_model == MM_ATM3 && (state.pEFF7 & m))
	{
		if ((state.pEFF7 & m) == EFF7_4BPP) { video.mode = M_P16; return; }
		if ((state.pEFF7 & m) == EFF7_HWMC) { video.mode = M_PMC; return; }

		video.mode = M_NUL;
	}

	// Pentagon AlCo modes
	m = EFF7_4BPP | EFF7_512 | EFF7_384 | EFF7_HWMC;
	if (config.mem_model == MM_PENTAGON && (state.pEFF7 & m))
	{
		if ((state.pEFF7 & m) == EFF7_4BPP) { video.mode = M_P16; return; }
		if ((state.pEFF7 & m) == EFF7_HWMC) { video.mode = M_PMC; return; }
		if ((state.pEFF7 & m) == EFF7_512) { video.mode = M_PHR; return; }
		if ((state.pEFF7 & m) == EFF7_384) { video.raster = raster[R_384_304]; video.mode = M_P384; return; }

		video.mode = M_NUL;
	}

	if (config.mem_model == MM_PROFI && (state.pDFFD & 0x80))
	{
		video.raster = raster[R_512_240];
		video.mode = M_PROFI;
	}

	if (config.mem_model == MM_GMX && (state.p7EFD & 0x08))
	{
		video.raster = raster[R_320_200];
		video.mode = M_GMX;
	}

	// Sinclair
	video.mode = M_ZX48;
	//endregion

	// Select renderer for the mode
	if (prevMode != video.mode)
	{
        SetVideoMode(video.mode);
    }

}

void Screen::InitMemoryCounters()
{
	memset(_vid.memcpucyc, 0, 320 * sizeof(_vid.memcpucyc[0]));
	memset(_vid.memvidcyc, 0, 320 * sizeof(_vid.memvidcyc[0]));
	memset(_vid.memtsscyc, 0, 320 * sizeof(_vid.memtsscyc[0]));
	memset(_vid.memtstcyc, 0, 320 * sizeof(_vid.memtstcyc[0]));
	memset(_vid.memdmacyc, 0, 320 * sizeof(_vid.memdmacyc[0]));
}

void Screen::SetVideoMode(VideoModeEnum mode)
{
    _mode = mode;
    _nullCallback = _drawCallbacks[M_NUL];
    _drawCallback = _drawCallbacks[_mode];
    _borderCallback = _drawCallbacks[M_BRD];

    /// region <Calculate raster values>

    /// Note!: all timings are in t-states, although raster descriptor has pixels as UOM. So recalculation is required
    const RasterDescriptor& rasterDescriptor = rasterDescriptors[_mode];
    const uint8_t pixelsPerTState = 2;

    /// region <Frame timings>

    _rasterState.pixelsPerLine = rasterDescriptor.pixelsPerLine;
    _rasterState.tstatesPerLine = _rasterState.pixelsPerLine / pixelsPerTState;
    _rasterState.maxFrameTiming = _rasterState.tstatesPerLine * (rasterDescriptor.vSyncLines + rasterDescriptor.vBlankLines + rasterDescriptor.fullFrameHeight);

    /// endregion </Frame timings>

    /// region <Vertical timings>

    // Invisible blank area on top
    _rasterState.blankAreaStart = 0;
    _rasterState.blankAreaEnd = _rasterState.tstatesPerLine * (rasterDescriptor.vBlankLines + rasterDescriptor.vSyncLines) - 1;

    // Top border
    _rasterState.topBorderAreaStart = _rasterState.blankAreaEnd + 1;
    _rasterState.topBorderAreaEnd = _rasterState.topBorderAreaStart + _rasterState.tstatesPerLine * rasterDescriptor.screenOffsetTop - 1;

    // Screen + side borders
    _rasterState.screenAreaStart = _rasterState.topBorderAreaEnd + 1;
    _rasterState.screenAreaEnd = _rasterState.screenAreaStart + _rasterState.tstatesPerLine * rasterDescriptor.screenHeight - 1;

    // Bottom border
    _rasterState.bottomBorderAreaStart = _rasterState.screenAreaEnd + 1;
    _rasterState.bottomBorderAreaEnd = _rasterState.bottomBorderAreaStart + _rasterState.tstatesPerLine * (rasterDescriptor.fullFrameHeight - rasterDescriptor.screenHeight - rasterDescriptor.screenOffsetTop) - 1;

    /// endregion </Vertical timings>

    /// region <Horizontal timings>

    _rasterState.blankLineAreaStart = 0;
    _rasterState.blankLineAreaEnd = ((rasterDescriptor.hBlankPixels + rasterDescriptor.hSyncPixels) / pixelsPerTState) - 1;

    _rasterState.leftBorderAreaStart = _rasterState.blankLineAreaEnd + 1;
    _rasterState.leftBorderAreaEnd = _rasterState.leftBorderAreaStart + (rasterDescriptor.screenOffsetLeft / pixelsPerTState) - 1;

    _rasterState.screenLineAreaStart = _rasterState.leftBorderAreaEnd + 1;
    _rasterState.screenLineAreaEnd = _rasterState.screenLineAreaStart +  (rasterDescriptor.screenWidth / pixelsPerTState) - 1;

    _rasterState.rightBorderAreaStart = _rasterState.screenLineAreaEnd + 1;
    _rasterState.rightBorderAreaEnd = _rasterState.rightBorderAreaStart + ((rasterDescriptor.fullFrameWidth - rasterDescriptor.screenOffsetLeft - rasterDescriptor.screenWidth) / pixelsPerTState) - 1;

    /// endregion </Horizontal timings>

    /// endregion </Calculate raster values>

    // Allocate framebuffer
    AllocateFramebuffer(_mode);

#ifdef _DEBUG

    LOGINFO("%s", DumpRasterState().c_str());

#endif // _DEBUG
}

/// endregion </Initialization>

void Screen::UpdateScreen()
{
	static Z80& cpu = *_cpu;
	static COMPUTER& state = _context->state;
	static CONFIG& config = _context->config;
	static VideoControl& video = _context->pScreen->_vid;

	// Get Z80 CPU clock cycle counter spent in current frame
	uint32_t cput = (cpu.t >= config.frame) ? (VID_TACTS * VID_LINES) : cpu.t;

	// TODO: Implement timing-accurate rendering (screen + border)
}

///
/// Convert whole ZX-Spectrum screen to RGBA framebuffer
///
void Screen::RenderOnlyMainScreen()
{
    // No default implementation
    return;
}

void Screen::SaveScreen()
{
    ImageHelper::SaveFrameToPNG_Async(_framebuffer.memoryBuffer, _framebuffer.memoryBufferSize, _framebuffer.width, _framebuffer.height);
}

void Screen::SaveZXSpectrumNativeScreen()
{
    static Memory& memory = *_context->pMemory;

    uint8_t* buffer = memory.RemapAddressToCurrentBank(0x4000);

    ImageHelper::SaveZXSpectrumNativeScreen(buffer);
}

void Screen::AllocateFramebuffer(VideoModeEnum mode)
{
    // Buffer already allocated for the selected video mode
    if (_framebuffer.memoryBuffer != nullptr && _framebuffer.videoMode == mode)
        return;

	// Deallocate existing framebuffer memory
	DeallocateFramebuffer();

	bool isUnknownVideoMode = false;
	switch (mode)
	{
		case M_ZX48:
		case M_ZX128:
		case M_PMC:
			break;
		default:
			LOGWARNING("AllocateFramebuffer: Unknown video mode");

            isUnknownVideoMode = true;
			break;
	}

	if (!isUnknownVideoMode)
	{
	    const RasterDescriptor& rasterDescriptor = rasterDescriptors[mode];

        _framebuffer.videoMode = mode;
	    _framebuffer.width = rasterDescriptor.fullFrameWidth;
	    _framebuffer.height = rasterDescriptor.fullFrameHeight;

	    // Calculate required buffer size and allocate memory
        _framebuffer.memoryBufferSize = _framebuffer.width * _framebuffer.height * RGBA_SIZE;
        _framebuffer.memoryBuffer = new uint8_t[_framebuffer.memoryBufferSize];

#ifdef _DEBUG
        LOGINFO("Framebuffer allocated");

        static char videoModeInfo[200];
        DumpFramebufferInfo(videoModeInfo, sizeof(videoModeInfo));
        LOGINFO(videoModeInfo);
#endif
    }
	else
    {
	    LOGERROR("Unable to allocate framebuffer, unknown video mode");
    }
}

void Screen::DeallocateFramebuffer()
{
	if (_framebuffer.memoryBuffer != nullptr)
	{
		delete [] _framebuffer.memoryBuffer;
		_framebuffer.memoryBuffer = nullptr;
		_framebuffer.memoryBufferSize = 0;
	}
}

void Screen::GetFramebufferData(uint32_t** buffer, size_t* size)
{
    if (buffer && size && _framebuffer.memoryBuffer && _framebuffer.memoryBufferSize)
    {
        *buffer = (uint32_t*)_framebuffer.memoryBuffer;
        *size = _framebuffer.memoryBufferSize;
    }
}

std::string Screen::GetVideoModeName(VideoModeEnum mode)
{
    std::string result;

    switch (mode)
    {
        case M_NUL:
            result = "Nul";
            break;
        case M_ZX48:
            result = "ZX";
            break;
        case M_PMC:
            result = "PMC";
            break;
        case M_P16:
            result = "P16";
            break;
        case M_P384:
            result = "P384";
            break;
        case M_PHR:
            result = "PHR";
            break;
        case M_TIMEX:
            result = "Timex";
            break;
        case M_TS16:
            result = "TS16";
            break;
        case M_TS256:
            result = "TS256";
            break;
        case M_TSTX:
            result = "TSTX";
            break;
        case M_ATM16:
            result = "ATM16";
            break;
        case M_ATMHR:
            result = "ATMHR";
            break;
        case M_ATMTX:
            result = "ATMTX";
            break;
        case M_ATMTL:
            result = "ATMTL";
            break;
        case M_PROFI:
            result = "PROFI";
            break;
        case M_GMX:
            result = "GMX";
            break;
        case M_BRD:
            result = "Border";
            break;
        default:
            result = "Unknown";
            break;
    }

    return result;
}

void Screen::DrawScreenBorder(uint32_t n)
{
    static Z80& cpu = *_cpu;
    static COMPUTER& state = _context->state;
    static CONFIG& config = _context->config;
    static VideoControl& video = _context->pScreen->_vid;

    video.t_next += n;
    uint32_t vptr = video.vptr;

    for (; n > 0; n--)
    {
        uint32_t pixelColorRGBA = video.clut[state.ts.border];
        vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] = pixelColorRGBA;
        vptr += 4;
    }

    video.vptr = vptr;
}

//
// Method called after each CPU operation execution to update
//
void Screen::Draw(uint32_t n)
{
    (this->*_currentDrawCallback)(n);
}

// Skip render
void Screen::DrawNull(uint32_t n)
{

}

// Genuine Sinclair ZX Spectrum
void Screen::DrawZX(uint32_t n)
{
	static uint32_t palette[2][8] =
	{
		{ // Brightness OFF
			0x00000000,		// Black
			0x000022C7,		// Blue
			0x00D62816,		// Red
			0x00D433C7,		// Magenta
			0x0000C525,		// Green,
			0x0000C7C9,		// Cyan
			0x00CCC82A,		// Yellow
			0x00CACACA		// White
		},
		{ // Brightness ON
			0x00000000,		// Black
			0x00002BFB,		// Blue
			0x00FF331C,		// Red
			0x00FF40FC,		// Magenta
			0x0000F92F,		// Green
			0x0000FBFE,		// Cyan
			0x00FFFC36,		// Yellow
			0x00FFFFFF		// White
		}
	};

	static COMPUTER& state = _context->state;
	static CONFIG& config = _context->config;
	static VideoControl& video = _vid;

	if (n > sizeof vbuf[0])
	{
		LOGERROR("Standard ZX-Spectrum cannot have more than %d video lines", sizeof vbuf[0]);
		return;
	}

	uint32_t g = ((video.ygctr & 0x07) << 8) + ((video.ygctr & 0x38) << 2) + ((video.ygctr & 0xC0) << 5) + (video.xctr & 0x1F);
	uint32_t a = ((video.ygctr & 0xF8) << 2) + (video.xctr & 0x1F) + 0x1800;
	uint8_t* zx_screen_mem = _system->GetMemory()->RAMPageAddress(state.ts.vpage);
	uint32_t vptr = video.vptr;
	uint16_t vcyc = video.memvidcyc[video.line];
	uint8_t upmod = config.ulaplus;
	uint8_t tsgpal = state.ts.gpal << 4;

	for (int i = n; i > 0; i -= 4, video.t_next += 4, video.xctr++, g++, a++)
	{
		uint32_t color_paper, color_ink;
		uint8_t pixel = zx_screen_mem[g];	// Line of 8 pixels from ZX-Spectrum screen memory (Encoded as bits in single byte)
		uint8_t attrib = zx_screen_mem[a];	// Color attributes for the whole 8x8 character block

		vcyc++;
		video.memcyc_lcmd++;

		if ((upmod != UPLS_NONE) && state.ulaplus_mode)
		{
			// Decode color information as ULA+
			uint32_t psel = (attrib & 0xC0) >> 2;
			uint32_t ink = state.ulaplus_cram[psel + (attrib & 7)];
			uint32_t paper = state.ulaplus_cram[psel + ((attrib >> 3) & 7) + 8];

			color_paper = cr[(paper & 0x1C) >> 2] | cg[(paper & 0xE0) >> 5] | cb[upmod][paper & 0x03];
			color_ink = cr[(ink & 0x1C) >> 2] | cg[(ink & 0xE0) >> 5] | cb[upmod][ink & 0x03];
		}
		else
		{
			// Decode color information as standard ULA
			// Bit 7 - Flash, Bit 6 - Brightness, Bits 5-3 - Paper color, Bits 2-0 - Ink color
			if ((attrib & 0x80) && (state.frame_counter & 0x10))					// Flash attribute for the 8x8 block
				pixel ^= 0xFF;														// Invert every N frames

			uint8_t brightness = (attrib & 0x40) >> 3;								// BRIGHTNESS attribute
			uint8_t paper = (attrib >> 3) & 0x07;									// Color for 'PAPER'
			uint8_t ink = attrib & 0x07;											// Color for 'INK'

			color_paper = palette[brightness][paper];								// Resolve PAPER color to RGB	
			color_ink = palette[brightness][ink];									// Resolve INK color to RGB
		}

		// Write RGBA 1x8 (scaled to 2x16) line to framebuffer
		vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = ((pixel << 1) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] = ((pixel << 2) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 4] = vbuf[video.buf][vptr + 5] = ((pixel << 3) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 6] = vbuf[video.buf][vptr + 7] = ((pixel << 4) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 8] = vbuf[video.buf][vptr + 9] = ((pixel << 5) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 10] = vbuf[video.buf][vptr + 11] = ((pixel << 6) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 12] = vbuf[video.buf][vptr + 13] = ((pixel << 7) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 14] = vbuf[video.buf][vptr + 15] = ((pixel << 8) & 0x100) ? color_ink : color_paper;
		vptr += 16;
	}

	video.vptr = vptr;
	video.memvidcyc[video.line] = vcyc;
}

void Screen::DrawPMC(uint32_t n)
{

}

void Screen::DrawP16(uint32_t n)
{

}

void Screen::DrawP384(uint32_t n)
{

}

void Screen::DrawPHR(uint32_t n)
{

}

void Screen::DrawTimex(uint32_t n)
{

}

void Screen::DrawTS16(uint32_t n)
{

}

void Screen::DrawTS256(uint32_t n)
{

}

void Screen::DrawTSText(uint32_t n)
{

}

void Screen::DrawATM16(uint32_t n)
{

}

void Screen::DrawATMHiRes(uint32_t n)
{

}

void Screen::DrawATM2Text(uint32_t n)
{

}

void Screen::DrawATM3Text(uint32_t n)
{

}

void Screen::DrawProfi(uint32_t n)
{

}

void Screen::DrawGMX(uint32_t n)
{

}

void Screen::DrawBorder(uint32_t n)
{
    static COMPUTER& state = _context->state;
    static CONFIG& config = _context->config;
    static VideoControl& video = _context->pScreen->_vid;

    video.t_next += n;
    uint32_t vptr = video.vptr;

    for (; n > 0; n--)
    {
        uint32_t p = video.clut[state.ts.border];
        vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] = p;
        vptr += 4;
    }

    video.vptr = vptr;
}

/// region <Helper methods

std::string Screen::GetVideoVideoModeName(VideoModeEnum mode)
{
    static char const *videoModeName[] =
    {
        [M_NUL] = "Null",	                // Non-existing mode
        [M_ZX48] = "ZX-Spectrum 48k",		// Sinclair ZX Spectrum 48k
        [M_ZX128] = "ZX-Spectrum 128k",		// Sinclair ZX Spectrum 128k
        [M_PMC] = "Pentagon",		        // Pentagon Multicolor
        [M_P16] = "Pentagon 16c",   		// Pentagon 16c
        [M_P384] = "Pentagon 384x384",		// Pentagon 384x304
        [M_PHR] = "Pentagon HiRes", 		// Pentagon HiRes
        [M_TIMEX] = "Timex ULA+",           // Timex with 32 x 192 attributes (2 colors per line)
        [M_TS16] = "TSConf 16c",	    	// TS 16c
        [M_TS256] = "TSConf 256c",      	// TS 256c
        [M_TSTX] = "TSConf Text",   		// TS Text
        [M_ATM16] = "ATM 16c",          	// ATM 16c
        [M_ATMHR] = "ATM HiRes",        	// ATM HiRes
        [M_ATMTX] = "ATM Text",         	// ATM Text
        [M_ATMTL] = "ATM Text Linear",  	// ATM Text Linear
        [M_PROFI] = "Profi",            	// Profi
        [M_GMX] = "GMX",            		// GMX
        [M_BRD] = "Border only",          	// Border only
        [M_MAX] = ""
    };

    std::string result;

    if (mode < M_MAX)
        result = videoModeName[mode];

    return result;
}

std::string Screen::GetRenderTypeName(RenderTypeEnum type)
{
    static char const *renderTypeName[] =
    {
            [RT_BLANK] = "BLANK",
            [RT_BORDER] = "BORDER",
            [RT_SCREEN] = "SCREEN"
    };

    std::string result;

    if (type <= RT_SCREEN)
        result = renderTypeName[type];

    return result;
}

/// endregion </Helper methods


/// region <Debug methods>

#ifdef _DEBUG

#include <cstdio>
#include <common/stringhelper.h>

std::string Screen::DumpFramebufferInfo()
{
    std::string videoModeName = GetVideoModeName(_framebuffer.videoMode);
    std::string result = StringHelper::Format("VideoMode: %s; Width: %dpx; Height: %dpx; Buffer: %d bytes",
        videoModeName.c_str(),
        _framebuffer.width,
        _framebuffer.height,
        _framebuffer.memoryBufferSize
    );

    return result;
}

void Screen::DumpFramebufferInfo(char* buffer, size_t len)
{
    std::string value = DumpFramebufferInfo();
    snprintf(buffer, len, value.c_str());
}

std::string Screen::DumpRasterState()
{
    const RasterState& state = _rasterState;

    std::string result = StringHelper::Format("RasterState: ");
    result += StringHelper::Format("VideoMode: %s; Frame: %d; Lines: %d; PerLine: %d",
                                   Screen::GetVideoVideoModeName(_mode).c_str(),
                                   state.maxFrameTiming,
                                   state.maxFrameTiming / state.tstatesPerLine,
                                   state.tstatesPerLine
    );

    return result;
}

void Screen::DumpRasterState(char* buffer, size_t len)
{
    std::string value = DumpFramebufferInfo();
    snprintf(buffer, len, value.c_str());
}

#endif // _DEBUG

/// endregion </Debug methods>