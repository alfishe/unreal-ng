#include "soundmanager.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QDebug>
#include <QMediaDevices>

/// region <Methods>

void SoundManager::getDefaultAudioDeviceInfo()
{
    const auto deviceInfo = QMediaDevices::defaultAudioOutput();
    qDebug() << "Device: " << deviceInfo.description();
    qDebug() << "Supported channels: "        << deviceInfo.maximumChannelCount();
    qDebug() << "Supported Sample Rate: "     << deviceInfo.maximumSampleRate();
    qDebug() << "Preferred Device settings:"  << deviceInfo.preferredFormat();
}

void SoundManager::getAudioDevicesInfo()
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
/// endregion </Methods>