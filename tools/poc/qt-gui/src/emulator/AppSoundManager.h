#pragma once

#include <QObject>

#ifdef HAS_EMULATOR_CORE
// miniaudio.h includes <windows.h> on Windows. winsock2.h MUST be included before
// windows.h to prevent type conflicts.
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#include <3rdparty/miniaudio/miniaudio.h>
#include <common/sound/audioringbuffer.h>
#include <emulator/sound/soundmanager.h>
#endif

class AppSoundManager : public QObject
{
    Q_OBJECT

public:
    explicit AppSoundManager(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~AppSoundManager();

    bool init();
    void deinit();

    void start();
    void stop();

    bool isInitialized() const { return _initialized; }

#ifdef HAS_EMULATOR_CORE
    static void audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);
    static void audioCallback(void* obj, int16_t* samples, size_t numSamples);

protected:
    ma_device _audioDevice;
    AudioRingBuffer<int16_t, AUDIO_BUFFER_SAMPLES_PER_FRAME * 8> _ringBuffer;
#endif

private:
    bool _initialized = false;
};
