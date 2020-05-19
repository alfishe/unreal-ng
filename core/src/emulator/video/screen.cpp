#include "stdafx.h"

#include "common/logger.h"

#include "screen.h"

#include "emulator/cpu/cpu.h"
#include "emulator/cpu/z80.h"

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
		video.mode = M_NUL; return;
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
		video.mode = M_NUL; return;
	}

	video.raster = raster[R_256_192];

	// ATM 3 AlCo modes
	if (config.mem_model == MM_ATM3 && (state.pEFF7 & m))
	{
		if ((state.pEFF7 & m) == EFF7_4BPP) { video.mode = M_P16; return; }
		if ((state.pEFF7 & m) == EFF7_HWMC) { video.mode = M_PMC; return; }

		video.mode = M_NUL;
		return;
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
		return;
	}

	if (config.mem_model == MM_PROFI && (state.pDFFD & 0x80))
	{
		video.raster = raster[R_512_240];
		video.mode = M_PROFI; return;
	}

	if (config.mem_model == MM_GMX && (state.p7EFD & 0x08))
	{
		video.raster = raster[R_320_200];
		video.mode = M_GMX; return;
	}

	// Sinclair
	video.mode = M_ZX;
	//endregion
}

void Screen::InitMemoryCounters()
{
	memset(_vid.memcpucyc, 0, 320 * sizeof(_vid.memcpucyc[0]));
	memset(_vid.memvidcyc, 0, 320 * sizeof(_vid.memvidcyc[0]));
	memset(_vid.memtsscyc, 0, 320 * sizeof(_vid.memtsscyc[0]));
	memset(_vid.memtstcyc, 0, 320 * sizeof(_vid.memtstcyc[0]));
	memset(_vid.memdmacyc, 0, 320 * sizeof(_vid.memdmacyc[0]));
}

void Screen::UpdateScreen()
{
	static Z80& cpu = *_cpu;
	static COMPUTER& state = _context->state;
	static CONFIG& config = _context->config;
	static VideoControl& video = _context->pScreen->_vid;

	// Get Z80 CPU clock cycle counter spent in current frame
	uint32_t cput = (cpu.t >= config.frame) ? (VID_TACTS * VID_LINES) : cpu.t;

	while (video.t_next < cput)
	{
		// Calculate CPU cycles for drawing in current video line
		int n = min(cput - video.t_next, (uint32_t)VID_TACTS - video.line_pos);
		int dram_t = n << 1;

		// Start of new video line
		if (!video.line_pos)
		{
			if (state.ts.vconf != state.ts.vconf_d)
			{
				state.ts.vconf = state.ts.vconf_d;
				InitRaster();
			}
		}

		// Render upper and bottom border
		if (video.line < video.raster.u_brd || video.line >= video.raster.d_brd)
		{
			DrawBorder(n);
			video.line_pos += n;
		}
		else
		{
			// Start of new video line
			if (!video.line_pos)
			{
				video.xctr = 0; // clear X video counter
				video.yctr++;   // increment Y video counter

				if (!state.ts.g_yoffs_updated) // was Y-offset updated?
				{
					// no - just increment old
					video.ygctr++;
					video.ygctr &= 0x1FF;
				}
				else
				{
					// yes - reload Y-offset
					video.ygctr = state.ts.g_yoffs;
					state.ts.g_yoffs_updated = 0;
				}
			}

			// Render left border segment
			if (video.line_pos < video.raster.l_brd)
			{
				uint32_t m = min((uint32_t)n, video.raster.l_brd - video.line_pos);
				DrawScreenBorder(m);
				n -= m;
				video.line_pos += (uint16_t)m;
			}

			// Render pixel graphics on main screen area
			if (n > 0 && video.line_pos < video.raster.r_brd)
			{
				uint32_t m = min((uint32_t)n, video.raster.r_brd - video.line_pos);
				uint32_t t = video.t_next; // store tact of video controller
				uint32_t vptr = video.vptr;

				// Execute render to framebuffer using current video mode renderer
				DrawCallback draw = _drawCallbacks[video.mode];
				if (draw != nullptr)
				{
					(*this.*draw)(m);
				}

				t = video.t_next - t; // calculate tacts used by drawers func
				n -= t;
				video.line_pos += (uint16_t)t;
			}

			// Render right border segment
			if (n > 0)
			{
				uint32_t m = min(n, VID_TACTS - video.line_pos);
				DrawScreenBorder(m);
				n -= m;
				video.line_pos += (uint16_t)m;
			}
		}

		// Calculate busy CPU cycles for the next line
		video.memcyc_lcmd = (video.memcyc_lcmd > dram_t) ? (video.memcyc_lcmd - dram_t) : 0;

		// if line is full, then go to the next line
		if (video.line_pos == VID_TACTS)
		{
			video.line_pos = 0;
			video.line++;
		}
	}
}

void Screen::AllocateFramebuffer(VideoModeEnum mode)
{
	// Deallocate existing framebuffer memory
	DeallocateFramebuffer();

	bool isUnknownVideoMode = false;
	switch (mode)
	{
		M_ZX:
			_framebuffer.width = 111;
			break;
		default:
			LOGWARNING("AllocateFramebuffer: Unknown video mode");

            isUnknownVideoMode = true;
			break;
	}

	if (!isUnknownVideoMode)
	{
        _framebuffer.memoryBufferSize = _framebuffer.width * _framebuffer.height * RGBA_SIZE;
        _framebuffer.memoryBuffer = new uint8_t(_framebuffer.memoryBufferSize);
    }
}

void Screen::DeallocateFramebuffer()
{
	if (_framebuffer.memoryBuffer != nullptr)
	{
		delete _framebuffer.memoryBuffer;
		_framebuffer.memoryBuffer = nullptr;
		_framebuffer.memoryBufferSize = 0;
	}
}

void Screen::GetFramebufferData(uint8_t** buffer, size_t* size)
{

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
		uint32_t p = video.clut[state.ts.border];
		vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] = p;
		vptr += 4;
	}

	video.vptr = vptr;
}

//
// Method called after each CPU operation execution to update
//
void Screen::Draw(VideoModeEnum mode, uint32_t n)
{
	switch (mode)
	{
		case VideoModeEnum::M_BRD:
			DrawBorder(n);
			break;
		case VideoModeEnum::M_NUL:
			DrawNull(n);
			break;
		case VideoModeEnum::M_PMC:
			DrawPMC(n);
			break;
		case VideoModeEnum::M_P16:
			DrawP16(n);
			break;
		case VideoModeEnum::M_P384:
			DrawP384(n);
			break;
		case VideoModeEnum::M_PHR:
			DrawPHR(n);
			break;
		case VideoModeEnum::M_TS16:
			DrawTS16(n);
			break;
		case VideoModeEnum::M_TS256:
			DrawTS256(n);
			break;
		case VideoModeEnum::M_TSTX:
			DrawTSText(n);
			break;
		case VideoModeEnum::M_ATM16:
			DrawATM16(n);
			break;
		case VideoModeEnum::M_ATMHR:
			DrawATMHiRes(n);
			break;
		case VideoModeEnum::M_ATMTX:
			DrawATM2Text(n);
			break;
		case VideoModeEnum::M_ATMTL:
			DrawATM3Text(n);
			break;
		case VideoModeEnum::M_PROFI:
			DrawProfi(n);
			break;
		case VideoModeEnum::M_GMX:
			DrawGMX(n);
			break;
		default:
			DrawNull(n);
			break;
	};
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

	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _vid;

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