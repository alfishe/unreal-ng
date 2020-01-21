#include "stdafx.h"

#include "screen.h"

Screen::Screen(CPU* system, Z80* cpu, EmulatorContext* context)
{
	_system = system;
	_cpu = cpu;
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

	InitRaster();
	//init_memcycles();
}

void Screen::UpdateScreen()
{
	Z80& cpu = *_cpu;
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

	// Get Z80 CPU clock cycle counter spent in current frame
	uint32_t cput = (cpu.t >= config.frame) ? (VID_TACTS * VID_LINES) : cpu.t;

	while (video.t_next < cput)
	{
		// Calculate tacts for drawing in current video line
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

			// TSConf-specific
			// TODO: move to TSConf plugin
			state.ts.g_xoffs = state.ts.g_xoffs_d;	// GFX X offset
			state.ts.vpage = state.ts.vpage_d;		// Video Page
			state.ts.palsel = state.ts.palsel_d;	// Palette Selector

			state.ts.t0gpage[2] = state.ts.t0gpage[1];
			state.ts.t0gpage[1] = state.ts.t0gpage[0];
			state.ts.t1gpage[2] = state.ts.t1gpage[1];
			state.ts.t1gpage[1] = state.ts.t1gpage[0];
			state.ts.t0_xoffs_d = state.ts.t0_xoffs;
			state.ts.t1_xoffs_d = state.ts.t1_xoffs;

			video.ts_pos = 0;

			// set new task for tsu
			state.ts.tsu.state = TSS_INIT;
			// End of TODO: move to TSConf plugin
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

				// Execute render to framebuffer using current videomode renderer
				//_renderers->Draw(video.mode, m);

				if (config.mem_model == MM_TSL)
					TSConf_Draw(vptr);

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

		uint32_t free_t = TSConf_GetAvailableFrameMemcycles(dram_t); // Check if there is budget for TSConf rendering
		free_t = TSConf_Render(free_t);
		TSConf_DMA(free_t);

		// calculate busy tacts for the next line
		video.memcyc_lcmd = (video.memcyc_lcmd > dram_t) ? (video.memcyc_lcmd - dram_t) : 0;

		// if line is full, then go to the next line
		if (video.line_pos == VID_TACTS)
		{
			video.line_pos = 0;
			video.line++;
		}
	}
}

//
// Set appropriate video mode based on ports for current platform
//
void Screen::InitRaster()
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

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
}

void Screen::DrawScreenBorder(uint32_t n)
{
	Z80& cpu = *_cpu;
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

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

uint32_t Screen::TSConf_GetAvailableFrameMemcycles(uint32_t dram_t)
{
	Z80& cpu = *_cpu;
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

	uint32_t result = 0;

	if (video.memcyc_lcmd < dram_t)
	{
		uint32_t memcycles = video.memcpucyc[video.line] + video.memvidcyc[video.line] + video.memtstcyc[video.line] + video.memtsscyc[video.line] + video.memdmacyc[video.line];

		if (memcycles < MEM_CYCLES)
		{
			result = dram_t - video.memcyc_lcmd;
			result = min(result, MEM_CYCLES - memcycles);
		}
	}

	return result;
}

void Screen::TSConf_Draw(uint32_t vptr)
{

}

uint32_t Screen::TSConf_Render(uint32_t tacts)
{
	Z80& cpu = *_cpu;
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

	// save and set toggle bits
	uint8_t old_s_en = state.ts.s_en;
	uint8_t old_t0_en = state.ts.t0_en;
	uint8_t old_t1_en = state.ts.t1_en;
	uint32_t rtn = 0;

	/*
	EnterCriticalSection(&tsu_toggle_cr);
	comp.ts.s_en &= comp.ts.tsu.toggle.s;
	comp.ts.t0_en &= comp.ts.tsu.toggle.t0;
	comp.ts.t1_en &= comp.ts.tsu.toggle.t1;
	LeaveCriticalSection(&tsu_toggle_cr);

	if (comp.ts.tsu.state == TSS_NOP)
	{
		comp.ts.tsu.prev_state = TSS_NOP;
		//return tacts;
		rtn = tacts;
		goto fin;
	}

	// have new TSU state?
	if (comp.ts.tsu.prev_state != comp.ts.tsu.state)
	{
		if (comp.ts.tsu.state == TSS_INIT) // Start of new line
		{
			comp.ts.tsu.tmap_read = ((((u32)vid.line + 17) >= vid.raster.u_brd) && (((u32)vid.line + 9) <= vid.raster.d_brd)); // need to MemoryRead TileMap in this line ?
			comp.ts.tsu.render = ((((u32)vid.line + 1) >= vid.raster.u_brd) && (((u32)vid.line + 1) <= vid.raster.d_brd)); // need render graphic in this line ?

			// Set first state at this line
			if (comp.ts.tsu.tmap_read)
				comp.ts.tsu.state = TSS_TMAP_READ;
			else if (comp.ts.tsu.render)
				comp.ts.tsu.state = TSS_SPR_RENDER;
			else
			{
				comp.ts.tsu.state = TSS_NOP; // set state as no operation in this line
				//return tacts;
				rtn = tacts;
				goto fin;
			}

			comp.ts.tsu.layer = 0;  // start from layer 0
		}

		comp.ts.tsu.prev_state = comp.ts.tsu.state; // save current state
		TSUTask[comp.ts.tsu.state].init_task();   // initialize task for current state

		// process state if changed
		if (comp.ts.tsu.prev_state != comp.ts.tsu.state)
		{
			//return render_ts(tacts);
			rtn = render_ts(tacts);
			goto fin;
		}
	}

	tacts = TSUTask[comp.ts.tsu.state].task(tacts); // do task
	if (comp.ts.tsu.prev_state != comp.ts.tsu.state) // if state changed process it
		tacts = render_ts(tacts);

	rtn = tacts;

	// ugh..gotos =)
fin:
	// restore layer enable bits
	comp.ts.s_en = old_s_en;
	comp.ts.t0_en = old_t0_en;
	comp.ts.t1_en = old_t1_en;
	*/

	return rtn; // return number of CPU cycles available for current frame
}

void Screen::TSConf_DMA(uint32_t tacts)
{
	Z80& cpu = *_cpu;
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

	/*
	if (state.ts.dma.state != DMA_ST_NOP)
	{
		// get new task for dma
		if (state.ts.dma.state == DMA_ST_INIT)
		{
			dma_init();
			if (state.ts.dma.state == DMA_ST_NOP)
				return;
		}

		// do task
		tacts -= DMATask[state.ts.dma.state].task(tacts);
		video.memdmacyc[video.line] += (uint16_t)tacts;
	}
	*/
}


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
	};
}

void Screen::DrawBorder(uint32_t n)
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _context->pScreen->_vid;

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

void Screen::DrawNull(uint32_t n)
{

}

// Genuine Sinclair ZX Spectrum
void Screen::DrawZX(uint32_t n)
{
	COMPUTER& state = _context->state;
	CONFIG& config = _context->config;
	VideoControl& video = _vid;

	uint32_t g = ((video.ygctr & 0x07) << 8) + ((video.ygctr & 0x38) << 2) + ((video.ygctr & 0xC0) << 5) + (video.xctr & 0x1F);
	uint32_t a = ((video.ygctr & 0xF8) << 2) + (video.xctr & 0x1F) + 0x1800;
	uint8_t* zx_screen_mem = _system->GetMemory()->RAMPageAddress(state.ts.vpage);
	uint32_t vptr = video.vptr;
	uint16_t vcyc = video.memvidcyc[video.line];
	uint8_t upmod = config.ulaplus;
	uint8_t tsgpal = state.ts.gpal << 4;

	for (; n > 0; n -= 4, video.t_next += 4, video.xctr++, g++, a++)
	{
		uint32_t color_paper, color_ink;
		uint8_t p = zx_screen_mem[g];	// pixels
		uint8_t c = zx_screen_mem[a];	// attributes

		vcyc++;
		video.memcyc_lcmd++;

		if ((upmod != UPLS_NONE) && state.ulaplus_mode)
		{
			uint32_t psel = (c & 0xC0) >> 2;
			uint32_t ink = state.ulaplus_cram[psel + (c & 7)];
			uint32_t paper = state.ulaplus_cram[psel + ((c >> 3) & 7) + 8];

			color_paper = cr[(paper & 0x1C) >> 2] | cg[(paper & 0xE0) >> 5] | cb[upmod][paper & 0x03];
			color_ink = cr[(ink & 0x1C) >> 2] | cg[(ink & 0xE0) >> 5] | cb[upmod][ink & 0x03];
		}

		else
		{
			if ((c & 0x80) && (state.frame_counter & 0x10))
				p ^= 0xFF;												// Flash attribute for the 8x8 block

			uint32_t b = (c & 0x40) >> 3;
			color_paper = video.clut[tsgpal | b | ((c >> 3) & 0x07)];	// Color for 'PAPER'
			color_ink = video.clut[tsgpal | b | (c & 0x07)];			// Color for 'INK'
		}

		// Write RGBA 8x8 block to framebuffer
		vbuf[video.buf][vptr] = vbuf[video.buf][vptr + 1] = ((p << 1) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 2] = vbuf[video.buf][vptr + 3] = ((p << 2) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 4] = vbuf[video.buf][vptr + 5] = ((p << 3) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 6] = vbuf[video.buf][vptr + 7] = ((p << 4) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 8] = vbuf[video.buf][vptr + 9] = ((p << 5) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 10] = vbuf[video.buf][vptr + 11] = ((p << 6) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 12] = vbuf[video.buf][vptr + 13] = ((p << 7) & 0x100) ? color_ink : color_paper;
		vbuf[video.buf][vptr + 14] = vbuf[video.buf][vptr + 15] = ((p << 8) & 0x100) ? color_ink : color_paper;
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