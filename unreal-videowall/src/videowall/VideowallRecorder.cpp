#include "videowall/VideowallRecorder.h"

#include <QDebug>
#include <QGuiApplication>
#include <QScreen>

VideowallRecorder& VideowallRecorder::instance()
{
    static VideowallRecorder self;
    return self;
}

VideowallRecorder::VideowallRecorder()
{
    // Initialize RecordingManager without binding to a single EmulatorContext
    _recordingManager = std::make_unique<RecordingManager>(nullptr);
    _recordingManager->setFeatureEnabled(true);
    _recordingManager->Init();

    // 50 Hz frame capture timer (20 ms interval)
    connect(&_frameTimer, &QTimer::timeout, this, &VideowallRecorder::captureVideoFrame);
}

VideowallRecorder::~VideowallRecorder()
{
    if (_recordingManager && _recordingManager->IsRecording())
    {
        _recordingManager->StopRecording();
    }
}

void VideowallRecorder::setTargetWidget(QWidget* widget)
{
    _targetWidget = widget;
}

bool VideowallRecorder::startRecording(const std::string& filename,
                                        const std::string& videoCodec,
                                        const std::string& audioCodec,
                                        uint32_t videoBitrate,
                                        uint32_t audioBitrate,
                                        uint32_t targetWidth,
                                        uint32_t targetHeight)
{
    if (!_recordingManager)
        return false;

    _targetWidth = targetWidth;
    _targetHeight = targetHeight;

    bool ok = _recordingManager->StartRecording(filename, videoCodec, audioCodec, videoBitrate, audioBitrate);
    if (ok)
    {
        // Start 50 Hz frame grab timer (20ms)
        _frameTimer.start(20);
        qDebug() << "VideowallRecorder: Started recording to" << QString::fromStdString(filename)
                 << "Target resolution:" << _targetWidth << "x" << _targetHeight;
    }
    else
    {
        qWarning() << "VideowallRecorder: Failed to start recording:"
                   << QString::fromStdString(_recordingManager->GetLastRecordingError());
    }

    return ok;
}

void VideowallRecorder::stopRecording()
{
    _frameTimer.stop();
    if (_recordingManager)
    {
        _recordingManager->StopRecording();
        qDebug() << "VideowallRecorder: Recording stopped.";
    }
}

void VideowallRecorder::pauseRecording()
{
    _frameTimer.stop();
    if (_recordingManager)
    {
        _recordingManager->PauseRecording();
    }
}

void VideowallRecorder::resumeRecording()
{
    if (_recordingManager)
    {
        _recordingManager->ResumeRecording();
        _frameTimer.start(20);
    }
}

bool VideowallRecorder::isRecording() const
{
    return _recordingManager ? _recordingManager->IsRecording() : false;
}

bool VideowallRecorder::isPaused() const
{
    return _recordingManager ? _recordingManager->IsPaused() : false;
}

void VideowallRecorder::captureAudio(const int16_t* samples, size_t sampleCount)
{
    if (_recordingManager && isRecording())
    {
        _recordingManager->CaptureAudio(samples, sampleCount);
    }
}

RecordingManager::RecordingStats VideowallRecorder::getStats() const
{
    return _recordingManager ? _recordingManager->GetStats() : RecordingManager::RecordingStats();
}

void VideowallRecorder::captureVideoFrame()
{
    if (!isRecording() || !_targetWidget)
        return;

    // Grab combined rendered Qt buffer of the tile grid / videowall
    QPixmap pixmap = _targetWidget->grab();
    if (pixmap.isNull())
        return;

    QImage img = pixmap.toImage().convertToFormat(QImage::Format_RGBA8888);
    if (img.isNull())
        return;

    // Handle resolution scaling (e.g. scaling up to 4K 3840x2160 or custom resolution)
    if (_targetWidth > 0 && _targetHeight > 0 &&
        (img.width() != static_cast<int>(_targetWidth) || img.height() != static_cast<int>(_targetHeight)))
    {
        img = img.scaled(static_cast<int>(_targetWidth), static_cast<int>(_targetHeight),
                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    FramebufferDescriptor fb;
    fb.videoMode = M_ZX48;
    fb.width = static_cast<uint16_t>(img.width());
    fb.height = static_cast<uint16_t>(img.height());
    fb.memoryBuffer = const_cast<uint8_t*>(img.constBits());
    fb.memoryBufferSize = img.sizeInBytes();

    _recordingManager->CaptureFrame(fb);
}

#include "moc_VideowallRecorder.cpp"
