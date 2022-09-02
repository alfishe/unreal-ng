#pragma once
#include "stdafx.h"


#include "common/autoresetevent.h"
#include "interfaces/interfaces.h"
#include <thread>

class VideoRenderer
{
private:
    std::atomic<bool> _stopFlag;
	AutoResetEvent _waitForRender;
    std::unique_ptr<std::thread> _renderThread;
	IRenderer* _renderer = nullptr;

	void RenderThread();

public:
	VideoRenderer();
	virtual ~VideoRenderer();

	void StartThread();
	void StopThread();

	void UpdateFrame(void *frameBuffer, uint32_t width, uint32_t height);
	void RegisterRenderingDevice(IRenderer *renderer);
	void UnregisterRenderingDevice(IRenderer *renderer);

	/*
	void StartRecording(string filename, VideoCodec codec, uint32_t compressionLevel);
	void AddRecordingSound(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate);
	void StopRecording();
	bool IsRecording();
	*/
};

