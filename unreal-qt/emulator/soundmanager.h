#pragma once

#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>

class WaveGenerator : public QIODevice
{
    Q_OBJECT

private:
public:
    WaveGenerator()
    {
        QIODevice::open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    }

    qint64 readData(char *data, qint64 len)
    {

        return len;
    }

    qint64 writeData(const char *data, qint64 len)
    {
        Q_UNUSED(data);
        Q_UNUSED(len);
        return 0;
    }
};

class AppSoundManager : public QObject
{
    Q_OBJECT

    /// region <Fields>
protected:

    QScopedPointer<QAudioSink> _audioOutput;
    QScopedPointer<QAudioSource> _audioInput;
    QIODevice* _audioDevice;

    uint8_t* _audioBuffer;
    size_t _audioBufferLen;
    /// endregion </Fields>

    /// region <Constructors / destructors>
public:
    AppSoundManager() = default;
    virtual ~AppSoundManager();
    /// endregion </Constructors / destructors>

    /// region <Methods>
public:
    bool init(uint8_t* buffer, size_t len);
    bool init(const QAudioDevice &deviceInfo, uint8_t* buffer, size_t len);
    void deinit();

    void start();
    void stop();

    void pushAudio();

    /// endregion </Methods>

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

#endif // SOUNDMANAGER_H
