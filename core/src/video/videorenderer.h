#pragma once
#include "stdafx.h"

#include <thread>
#include "../interfaces.h"
#include "../common/autoresetevent.h"

using namespace std;

class VideoRenderer
{
private:
	atomic<bool> _stopFlag;
	AutoResetEvent _waitForRender;
	unique_ptr<std::thread> _renderThread;
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

