#pragma once

#include "stdafx.h"

class IRenderer
{
public:
	virtual ~IRenderer() {};
	virtual void UpdateFrame(void* frameBuffer, uint32_t width, uint32_t height) = 0;
	virtual void Render() = 0;
	virtual void Reset() = 0;
	virtual void SetFullscreenMode(bool fullscreen, void* windowHandle, uint32_t monitorWidth, uint32_t monitorHeight) = 0;

	virtual void Flip() = 0;
};