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

/// Encoder choices of a start (the dialog fills them; the defaults are the old behavior)
struct VideowallRecordingOptions
{
    /// "native" (the grab at its own size) or a fixed frame "1080p" / "1440p" / "4k": the wall is fitted
    /// into it (aspect kept, nearest neighbor, black bars - RecordingManager / FrameScaler), whatever size
    /// the window or the fullscreen buffer is, also when it is resized mid-recording. H.264 / H.265 only
    std::string profile = "native";
    EncoderAcceleration acceleration = EncoderAcceleration::Auto;
    EncoderBackend backend = EncoderBackend::Auto;
    int qualityPreset = 5;  ///< 0..10 (RecordingManager::SetQualityPreset)
};

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
                        uint32_t targetHeight = 0,
                        const VideowallRecordingOptions& options = VideowallRecordingOptions());

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

    /// Set synchronous recording mode (disables internal frame timer)
    void setSynchronousMode(bool enable);

public slots:
    /// Synchronously capture frame (when driven externally instead of by QTimer)
    void captureVideoFrameSync();

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
    
    bool _isSynchronousMode = false;
};
