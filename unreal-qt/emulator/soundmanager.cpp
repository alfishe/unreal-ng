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

    // Set up audio format: stereo PCM. float samples, 44100 Hz sampling rate
    QAudioFormat audioFormat;
    audioFormat.setChannelCount(2);
    audioFormat.setSampleRate(AUDIO_SAMPLING_RATE);
    audioFormat.setSampleFormat(QAudioFormat::Float);
    audioFormat.setChannelConfig(QAudioFormat::ChannelConfigStereo);

    _audioOutput.reset(new QAudioSink(deviceInfo, audioFormat));
    _audioOutput->setBufferSize(16384);

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

    // New wave file
    if (_tinyWav.file)
        tinywav_close_write(&_tinyWav);
    std::string filePath = "unreal-qt.wav";
    int res = tinywav_open_write(
            &_tinyWav,
            AUDIO_CHANNELS,
            AUDIO_SAMPLING_RATE,
            TW_FLOAT32,
            TW_INTERLEAVED,
            filePath.c_str());
}

void AppSoundManager::stop()
{
    if (_audioOutput)
    {
        if (_tinyWav.file)
            tinywav_close_write(&_tinyWav);

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

void AppSoundManager::pushAudio(const std::vector<uint8_t>& payload)
{
    static float audioBuffer[AUDIO_CHANNELS * SAMPLES_PER_FRAME];

    if (!_audioDevice || !_audioBuffer || _audioBufferLen == 0 || payload.empty())
        return;

    if (_audioDevice && _audioDevice->isOpen() && _audioOutput->state() != QAudio::StoppedState)
    {
        if (payload.size() == AUDIO_BUFFER_SIZE_PER_FRAME)
        {
            /// region <Convert audio frame from interleaved Int16 to interleaved Float format>
            if (true)
            {
                // Signed Int16 -> IEEE FLoat32 for each sample
                int16_t *inputBuffer = (int16_t *)payload.data();

                for (size_t i = 0; i < AUDIO_CHANNELS * SAMPLES_PER_FRAME; i++)
                {
                    audioBuffer[i] = ((float)inputBuffer[i]) / 32767.0f;
                }
            }
            else
            {
                // No conversion - just bypass
                //uint8_t* audioBuffer = (uint8_t*)payload.data();
                memcpy(audioBuffer, payload.data(), AUDIO_BUFFER_SIZE_PER_FRAME);
            }
            /// endregion </Convert audio frame from interleaved Int16 to interleaved Float format>

            /// region <DC removal filter>
            if (false)
            {
                double leftMeanValue = 0.0f;
                double rightMeanValue = 0.0f;

                // Left channel
                for (int i = 0; i < SAMPLES_PER_FRAME; i += 2)
                {
                    leftMeanValue += audioBuffer[i];
                }

                leftMeanValue /= SAMPLES_PER_FRAME;

                for (int i = 0; i < SAMPLES_PER_FRAME; i += 2)
                {
                    audioBuffer[i] -= leftMeanValue;
                }


                // Right channel
                for (int i = 1; i < SAMPLES_PER_FRAME; i += 2)
                {
                    rightMeanValue += audioBuffer[i];
                }

                rightMeanValue /= SAMPLES_PER_FRAME;

                for (int i = 1; i < SAMPLES_PER_FRAME; i += 2)
                {
                    audioBuffer[i] -= rightMeanValue;
                }

                qDebug() << QString::asprintf("DC offsets - left:%f, right:%f", leftMeanValue, rightMeanValue);
            }
            /// endregion </DC removal filter>

            /// region <Write to wave file>

            // Convert length from bytes to samples (stereo sample still counts as single)
            size_t lengthInSamples = SAMPLES_PER_FRAME;
            // Save using method with Int16 samples input
            tinywav_write_f(&_tinyWav, (void *)audioBuffer, lengthInSamples);

            /// endregion </Write to wave file>

            /// region <Write to audio output stream>
            qint64 bytesWritten = _audioDevice->write((const char*)&audioBuffer, AUDIO_CHANNELS * SAMPLES_PER_FRAME * sizeof(audioBuffer[0]));

            int len = _audioOutput->bytesFree();
            //qDebug() << QString::asprintf("Bytes written: %lld Free: %d", bytesWritten, len);
            /// endregion </Write to audio output stream>
        }
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