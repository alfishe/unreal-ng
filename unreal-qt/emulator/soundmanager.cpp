#include "soundmanager.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QDebug>
#include <QMediaDevices>

#include <emulator/sound/soundmanager.h>

/// region <Constructors / destructors>
AppSoundManager::~AppSoundManager()
{
    stop();
}
/// endregion </Constructors / destructors>

/// region <Methods>

bool AppSoundManager::init()
{
    const QAudioDevice &deviceInfo = QMediaDevices::defaultAudioOutput();
    bool result = init(deviceInfo);

    return result;
}

bool AppSoundManager::init(const QAudioDevice &deviceInfo)
{
    bool result = false;

    // Set up audio format: stereo PCM. float samples, 48000 Hz sampling rate
    _audioFormat.setChannelCount(2);
    _audioFormat.setSampleRate(AUDIO_SAMPLING_RATE);
    _audioFormat.setSampleFormat(QAudioFormat::Float);

    _audioOutput.reset(new QAudioSink(deviceInfo, _audioFormat, this));

    return result;
}

void AppSoundManager::start()
{
    _audioOutput->start();
}

void AppSoundManager::stop()
{
    _audioOutput->stop();

    _audioOutput->disconnect();
}
/// endregion </Methods>

/// region <Info methods>

void AppSoundManager::getDefaultAudioDeviceInfo()
{
    const auto deviceInfo = QMediaDevices::defaultAudioOutput();
    qDebug() << "Device: " << deviceInfo.description();
    qDebug() << "Supported channels: "        << deviceInfo.maximumChannelCount();
    qDebug() << "Supported Sample Rate: "     << deviceInfo.maximumSampleRate();
    qDebug() << "Preferred Device settings:"  << deviceInfo.preferredFormat();
}

void AppSoundManager::getAudioDevicesInfo()
{
    const auto deviceInfos = QMediaDevices::audioOutputs();
    for (const QAudioDevice &deviceInfo : deviceInfos)
    {
        qDebug() << "Device: " << deviceInfo.description();
        qDebug() << "Supported channels: "        << deviceInfo.maximumChannelCount();
        qDebug() << "Supported Sample Rate: "     << deviceInfo.maximumSampleRate();
        qDebug() << "Preferred Device settings:"  << deviceInfo.preferredFormat();
    }
}
/// endregion </Info methods>

/// region <Event handlers>
void AppSoundManager::onAudioDeviceChanged(const QAudioDevice &deviceInfo)
{
    stop();
    init(deviceInfo);
}
/// endregion </Event handlers>