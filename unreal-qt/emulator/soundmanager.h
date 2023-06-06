#pragma once

#include <QObject>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <3rdparty/tinywav/tinywav.h>
#include <common/stringhelper.h>
#include <common/sound/audioringbuffer.h>
#include <common/sound/filters/filter_dc.h>
#include <emulator/sound/soundmanager.h>

class EmuAudioDevice : public QIODevice
{
    Q_OBJECT

protected:
    AudioRingBuffer<int16_t, AUDIO_BUFFER_SAMPLES_PER_FRAME * AUDIO_CHANNELS * 2> _ringBuffer;

public:
    EmuAudioDevice() = default;
    virtual ~EmuAudioDevice() = default;

public:
    void start()
    {
        open(QIODevice::ReadOnly);
    }

    void stop()
    {
        close();
    }

    /// When the speaker lacks data, it will automatically call this method
    /// @param data Buffer pointer where audio samples in selected format should be copied into
    /// @param maxlen Max data length (determined by audio interface buffer size)
    /// @return Real data length copied
    qint64 readData(char *data, qint64 maxlen) override
    {
        qint64 result = 0;

        int16_t* dataSamples = (int16_t*)data;
        size_t samplesLen = maxlen / sizeof(int16_t);

        result = _ringBuffer.dequeue(dataSamples, samplesLen);
        // We need results in bytes, not in samples
        result *= sizeof(float);

        if (!_ringBuffer.isHalfFull())
        {
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_AUDIO_BUFFER_HALF_FULL);

            // Allow sound generating thread to react with low latency by yielding control
            std::this_thread::yield();
        }

        return result;
    }

    qint64 writeData(const char *data, qint64 len) override
    {
        qint64 result = 0;

        int16_t* dataSamples = (int16_t*)data;
        size_t samplesLen = len / sizeof(int16_t);

        result = _ringBuffer.enqueue(dataSamples, samplesLen);

        return result;
    }

    qint64 bytesAvailable() const override
    {
        qint64 result = _ringBuffer.getAvailableData() * sizeof(int16_t);

        return result;
    }

    qint64 size() const override
    {
        return _ringBuffer.size();
    }
};

class AppSoundManager : public QObject
{
    Q_OBJECT

    /// region <Fields>
protected:
    QScopedPointer<QAudioSink> _audioOutput;
    QScopedPointer<QAudioSource> _audioInput;
    QScopedPointer<EmuAudioDevice> _audioDevice;

    FilterDC<int16_t> _filterDCLeft;
    FilterDC<int16_t> _filterDCRight;

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
    bool init(const QAudioDevice &deviceInfo);
    void deinit();

    void start();
    void stop();

    void pushAudio(const std::vector<uint8_t>& payload);
    void writeData(int16_t* samples, size_t numSamples);
    static void audioCallback(void* obj, int16_t* samples, size_t numSamples);

    /// endregion </Methods>

    /// region <Helper methods>
protected:
    void applyDCFilters(int16_t* buffer, size_t sampleNum);
    /// endregion </Helper methods>

    /// region <Info methods>
public:
    static void getDefaultAudioDeviceInfo();
    static void getAudioDevicesInfo();
    /// endregion </Info methods>

    /// region <Event handlers>
protected:
    void onAudioDeviceChanged(const QAudioDevice &deviceInfo);
    /// endregion </Event handlers>
};
