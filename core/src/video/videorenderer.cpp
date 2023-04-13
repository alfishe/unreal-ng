#include "stdafx.h"

#include "videorenderer.h"

VideoRenderer::VideoRenderer()
{
	_stopFlag = false;
	StartThread();
}


VideoRenderer::~VideoRenderer()
{
	_stopFlag = true;
	StopThread();
}

void VideoRenderer::StartThread()
{
	if (!_renderThread)
	{
		_stopFlag = false;
		_waitForRender.Reset();

		_renderThread.reset(new std::thread(&VideoRenderer::RenderThread, this));
	}
}

void VideoRenderer::StopThread()
{
	if (_renderThread)
	{
		_renderThread->join();
		_renderThread.reset();
	}
}

void VideoRenderer::RenderThread()
{
	if (_renderer)
	{
		_renderer->Reset();
	}

	while (!_stopFlag.load())
	{
		// Wait until a frame is ready, or until 18ms have passed (to allow UI to run at a minimum of 50fps)
		_waitForRender.Wait(18);

		if (_renderer)
		{
			_renderer->Render();
		}
	}
}

void VideoRenderer::RegisterRenderingDevice(IRenderer *renderer)
{
	_renderer = renderer;
	StartThread();
}

void VideoRenderer::UnregisterRenderingDevice(IRenderer *renderer)
{
	if (_renderer == renderer)
	{
		StopThread();
		_renderer = nullptr;
	}
}