#include "soundmanager.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QDebug>
#include <QMediaDevices>
#include <QBuffer>

#include <common/sound/audiohelper.h>
#include <emulator/sound/soundmanager.h>


/// region <Constructors / destructors>
AppSoundManager::~AppSoundManager()
{
    this->stop();
}
/// endregion </Constructors / destructors>

/// region <Methods>

bool AppSoundManager::init()
{
    const QAudioDevice &deviceInfo = QMediaDevices::defaultAudioOutput();
    return init(deviceInfo);
}

bool AppSoundManager::init(const QAudioDevice &deviceInfo)
{
    bool result = false;

    // Set up audio format: stereo PCM. float samples, 44100 Hz sampling rate
    QAudioFormat audioFormat;

    if (true)
    {
        audioFormat = deviceInfo.preferredFormat();
    }
    else
    {
        audioFormat.setChannelCount(AUDIO_CHANNELS);
        audioFormat.setSampleRate(AUDIO_SAMPLING_RATE);
        audioFormat.setSampleFormat(QAudioFormat::Float);
        audioFormat.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    }

    _audioOutput.reset(new QAudioSink(deviceInfo, audioFormat));
    _audioOutput->setBufferSize(4096);
    _audioDevice.reset(new EmuAudioDevice());

    return result;
}

void AppSoundManager::deinit()
{
    this->stop();
}

void AppSoundManager::start()
{
    // Start playback in pull mode (Audio subsystem will call device's read method when data required)
    if (_audioDevice)
    {
        _audioDevice->start();
        _audioOutput->start(_audioDevice.data());
    }

    // New wave file
    if (_tinyWav.file)
        tinywav_close_write(&_tinyWav);
    std::string filePath = "unreal-qt.wav";
    int res = tinywav_open_write(
            &_tinyWav,
            AUDIO_CHANNELS,
            AUDIO_SAMPLING_RATE,
            TW_INT16,
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
    }
}

void AppSoundManager::pushAudio(const std::vector<uint8_t>& payload)
{
    static float audioBuffer[AUDIO_CHANNELS * SAMPLES_PER_FRAME];

    if (!_audioDevice || payload.empty())
        return;

    if (_audioDevice && _audioDevice->isOpen() && _audioOutput->state() != QAudio::StoppedState)
    {
        if (payload.size() == AUDIO_BUFFER_SIZE_PER_FRAME)
        {
            /// region <Convert audio frame from interleaved Int16 to interleaved Float format>
            if (true)
            {
                // Signed Int16 -> IEEE FLoat32 for all samples
                const int16_t *inputBuffer = (int16_t *)payload.data();
                AudioHelper::convertInt16ToFloat(inputBuffer, (float*)&audioBuffer, AUDIO_CHANNELS * SAMPLES_PER_FRAME);
            }
            else
            {
                // No conversion - just bypass
                //uint8_t* audioBuffer = (uint8_t*)payload.data();
                memcpy(audioBuffer, payload.data(), AUDIO_BUFFER_SIZE_PER_FRAME);
            }
            /// endregion </Convert audio frame from interleaved Int16 to interleaved Float format>

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

void AppSoundManager::writeData(int16_t* samples, size_t numSamples)
{
    static float audioBuffer[AUDIO_CHANNELS * SAMPLES_PER_FRAME];

    if (_audioDevice && numSamples <= AUDIO_CHANNELS * SAMPLES_PER_FRAME)
    {
        if (false)
        {
            // Signed Int16 -> IEEE Float32 for all samples
            const int16_t *inputBuffer = samples;
            AudioHelper::convertInt16ToFloat(inputBuffer, (float *) &audioBuffer, numSamples);

            //applyDCFilters(audioBuffer, numSamples);
            _audioDevice->writeData((const char *) audioBuffer, numSamples * sizeof(float));

            /// region <Write to wave file>

            // Convert length from bytes to samples (stereo sample still counts as single)
            // Save using method with Int16 samples input
            tinywav_write_f(&_tinyWav, (void *)audioBuffer, numSamples);

            /// endregion </Write to wave file>
        }
        else
        {
            // Write samples to ring buffer (will be played back by request from host audio system)
            _audioDevice->writeData((const char *)samples, numSamples * sizeof(int16_t));

            /// region <Write to wave file>

            // Save using method with Int16 samples input
            size_t samplesCount = numSamples / AUDIO_CHANNELS;  // tinywav_write_i requires only sample numbers (per channel)
            tinywav_write_i(&_tinyWav, (void *)samples, samplesCount);

            /// endregion </Write to wave file>
        }
    }
}

void AppSoundManager::audioCallback(void* obj, int16_t* samples, size_t numSamples)
{
    if (obj)
    {
        AppSoundManager* appSoundManager = static_cast<AppSoundManager *>(obj);

        appSoundManager->writeData(samples, numSamples);
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

    init(deviceInfo);
}
/// endregion </Event handlers>