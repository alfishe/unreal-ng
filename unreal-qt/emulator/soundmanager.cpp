#include "soundmanager.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QDebug>
#include <QMediaDevices>

#include <emulator/sound/soundmanager.h>
#include <QFile>
#include <QBuffer>

/// region <Constructors / destructors>
AppSoundManager::~AppSoundManager()
{
    this->stop();
}
/// endregion </Constructors / destructors>

/// region <Methods>

bool AppSoundManager::init(uint8_t* buffer, size_t len)
{
    const QAudioDevice &deviceInfo = QMediaDevices::defaultAudioOutput();
    bool result = init(deviceInfo, buffer, len);

    return result;
}

bool AppSoundManager::init(const QAudioDevice &deviceInfo, uint8_t* buffer, size_t len)
{
    bool result = false;

    _audioBuffer = buffer;
    _audioBufferLen = len;

    // Set up audio format: stereo PCM. float samples, 48000 Hz sampling rate
    QAudioFormat audioFormat;
    audioFormat.setChannelCount(2);
    audioFormat.setSampleRate(AUDIO_SAMPLING_RATE);
    audioFormat.setSampleFormat(QAudioFormat::Int16);
    audioFormat.setChannelConfig(QAudioFormat::ChannelConfigStereo);

    _audioOutput.reset(new QAudioSink(deviceInfo, audioFormat));
    _audioOutput->setBufferSize(65536);

    return result;
}

void AppSoundManager::deinit()
{
    this->stop();
}

void AppSoundManager::start()
{
    if (!_audioDevice)
    {
        _audioDevice = _audioOutput->start();
    }
}

void AppSoundManager::stop()
{
    if (_audioOutput)
    {
        QAudio::State state = _audioOutput->state();

        switch (state)
        {
            case QAudio::ActiveState:
            case QAudio::IdleState:
                _audioOutput->stop();
                break;
            default:
                break;
        }

        _audioOutput->disconnect();

        _audioDevice = nullptr;
    }
}

void AppSoundManager::pushAudio()
{
    if (!_audioDevice || !_audioBuffer || _audioBufferLen == 0)
        return;

    if (_audioDevice && _audioDevice->isOpen() && _audioOutput->state() != QAudio::StoppedState)
    {
        int len = _audioOutput->bytesFree();
        qint64 bytesWritten = _audioDevice->write(reinterpret_cast<const char *>(&_audioBuffer), _audioBufferLen);

        qDebug() << QString::asprintf("Bytes written: %lld Free: %d", bytesWritten, len);
    }
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

    init(deviceInfo, _audioBuffer, _audioBufferLen);
}
/// endregion </Event handlers>