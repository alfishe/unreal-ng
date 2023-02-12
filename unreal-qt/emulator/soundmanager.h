#pragma once

#ifndef SOUNDMANAGER_H
#define SOUNDMANAGER_H

#include <QObject>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>

class AppSoundManager : public QIODevice
{
    /// region <Fields>
protected:
    QAudioFormat _audioFormat;
    QScopedPointer<QAudioSink> _audioOutput;
    QScopedPointer<QAudioSource> _audioInput;
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
    void start();
    void stop();
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
