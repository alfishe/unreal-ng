#pragma once

#include <QObject>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <memory>
#include <string>
#include <vector>

#include "recordingmanager.h"
#include "emulator/video/screen.h"

/// @brief Videowall Recorder - Captures combined Qt buffer video and active tile miniaudio output
class VideowallRecorder : public QObject
{
    Q_OBJECT

public:
    static VideowallRecorder& instance();

    /// Attach the target Qt widget (TileGrid / centralWidget) to capture visually
    void setTargetWidget(QWidget* widget);

    /// Start recording to output file
    bool startRecording(const std::string& filename,
                        const std::string& videoCodec = "h264",
                        const std::string& audioCodec = "aac",
                        uint32_t videoBitrate = 0,
                        uint32_t audioBitrate = 0,
                        uint32_t targetWidth = 0,
                        uint32_t targetHeight = 0);

    /// Stop current recording
    void stopRecording();

    /// Pause recording
    void pauseRecording();

    /// Resume recording
    void resumeRecording();

    /// Check if currently recording
    bool isRecording() const;

    /// Check if recording is paused
    bool isPaused() const;

    /// Capture audio samples from final miniaudio output buffer (active tile)
    void captureAudio(const int16_t* samples, size_t sampleCount);

    /// Get recording statistics
    RecordingManager::RecordingStats getStats() const;

    /// Get pointer to internal RecordingManager instance
    RecordingManager* recordingManager() { return _recordingManager.get(); }

private slots:
    void captureVideoFrame();

private:
    VideowallRecorder();
    ~VideowallRecorder();

    std::unique_ptr<RecordingManager> _recordingManager;
    QWidget* _targetWidget = nullptr;
    QTimer _frameTimer;

    uint32_t _targetWidth = 0;
    uint32_t _targetHeight = 0;
};
