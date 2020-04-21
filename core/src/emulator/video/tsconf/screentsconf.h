#pragma once
#include "stdafx.h"

#include "common/logger.h"

#include "emulator/video/screen.h"

class ScreenTSConf : public Screen
{
	// Screen class methods override
public:
	void UpdateScreen() override;


public:
	// TSConf specific
	// TODO: move to TSConf plugin
	uint32_t TSConf_GetAvailableFrameMemcycles(uint32_t dram_t);
	void TSConf_Draw(uint32_t vptr);
	uint32_t TSConf_Render(uint32_t t);
	void TSConf_DMA(uint32_t tacts);
};
