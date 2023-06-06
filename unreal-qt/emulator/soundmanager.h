#pragma once

#include <QObject>

#include <3rdparty/tinywav/tinywav.h>
#include <3rdparty/miniaudio/miniaudio.h>
#include <common/stringhelper.h>
#include <common/sound/audioringbuffer.h>
#include <common/sound/filters/filter_dc.h>
#include <emulator/sound/soundmanager.h>
#include <emulator/emulatorcontext.h>

class AppSoundManager : QObject
{
    Q_OBJECT

    /// region <Fields>
protected:
    FilterDC<int16_t> _filterDCLeft;
    FilterDC<int16_t> _filterDCRight;

    ma_device _audioDevice;
    AudioRingBuffer<int16_t, AUDIO_BUFFER_SAMPLES_PER_FRAME * 2> _ringBuffer;


    // Save to Wave file
    TinyWav _tinyWav;

    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AppSoundManager() = default;
    virtual ~AppSoundManager();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    bool init();
    void deinit();

    void start();
    void stop();

public:
    static void audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    static void audioCallback(void* obj, int16_t* samples, size_t numSamples);
};
