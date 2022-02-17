#include "screentsconf.h"

#include "emulator/cpu/z80.h"

//region Screen class methods override
void ScreenTSConf::UpdateScreen()
{
	Z80& cpu = *_cpu;
	EmulatorState& state = _context->emulatorState;
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
				DrawCallback draw = _drawCallbacks[video.mode];
				if (draw != nullptr)
				{
					(*this.*draw)(m);
				}

				//if (config.mem_model == MM_TSL)
				//	TSConf_Draw(vptr);

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

		//uint32_t free_t = TSConf_GetAvailableFrameMemcycles(dram_t); // Check if there is budget for TSConf rendering
		//free_t = TSConf_Render(free_t);
		//TSConf_DMA(free_t);

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

//endregion

//region TSConf specific methods
uint32_t ScreenTSConf::TSConf_GetAvailableFrameMemcycles(uint32_t dram_t)
{
	Z80& cpu = *_cpu;
	EmulatorState& state = _context->emulatorState;
    const CONFIG& config = _context->config;
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

void ScreenTSConf::TSConf_Draw(uint32_t vptr)
{

}

uint32_t ScreenTSConf::TSConf_Render(uint32_t tacts)
{
	Z80& cpu = *_cpu;
	EmulatorState& state = _context->emulatorState;
    const CONFIG& config = _context->config;
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

void ScreenTSConf::TSConf_DMA(uint32_t tacts)
{
	Z80& cpu = *_cpu;
	EmulatorState& state = _context->emulatorState;
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

//endregion