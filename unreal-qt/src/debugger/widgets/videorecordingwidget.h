#pragma once

#include <QWidget>
#include <QShowEvent>
#include <QHideEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QHash>
#include <QTimer>

#include <atomic>
#include <string>

class EmulatorContext;

class VideoRecordingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoRecordingWidget(EmulatorContext* context, QWidget* parent = nullptr);
    ~VideoRecordingWidget();

    /// Update the emulator context (call when emulator instance changes)
    void setContext(EmulatorContext* context);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onDetectFFmpeg();
    void onBackendChanged();
    void onContainerChanged(int index);
    void onBrowseFile();
    void onStartRecording();
    void onPauseRecording();
    void onResumeRecording();
    void onStopRecording();
    void onUpdateStats();
    void onIncludeAudioChanged(int state);
    void onMultiTrackConfigure();
    void onBenchmark();

private:
    void createUI();
    void connectSignals();
    void refreshFromContext();
    void updateRealtimeEstimate();
    void updateRecordingControls();
    void populateCodecCombos();
    void populateContainers();
    void populateAudioCodecs();
    void updateFileExtension();
    void detectPlatformInfo();
    void runBenchmark();

    /// Verify _context still points to a live emulator; clear it if the
    /// emulator was destroyed (raw pointer can dangle after Release()).
    void validateContext();

    /// Look up the RecordingManager for the emulator we started recording
    /// with. Returns nullptr if that emulator was destroyed.
    /// When not recording, falls back to the active _context.
    EmulatorContext* recordingContext() const;

    // Encoder capability matrix (what the selected backend can actually produce)
    QStringList containersForBackend() const;
    QStringList videoCodecsFor(const QString& container) const;
    QStringList audioCodecsFor(const QString& container) const;
    static QString extensionForContainer(const QString& container);

    // Benchmark results: measured per backend+codec, persisted via QSettings
    // and reused by updateRealtimeEstimate for the currently selected options
    struct BenchmarkEntry
    {
        double msPerFrame = 0.0;
        double achievedFps = 0.0;
    };
    static QString benchmarkKey(const QString& backend, const QString& codec);
    QString currentSelectionBenchmarkKey() const;
    void loadBenchmarkResults();
    void saveBenchmarkResults();

    EmulatorContext* _context = nullptr;
    std::string _contextEmulatorId;     // Emulator ID for _context (for staleness check)
    std::string _recordingEmulatorId;   // Emulator ID we started recording with
    QTimer* _statsTimer = nullptr;

    // Backend
    QButtonGroup* _backendGroup = nullptr;
    QRadioButton* _nativeBackendRadio = nullptr;
    QRadioButton* _ffmpegBackendRadio = nullptr;
    QRadioButton* _autoBackendRadio = nullptr;
    QLabel* _platformInfoLabel = nullptr;
    QLineEdit* _ffmpegPathEdit = nullptr;
    QPushButton* _detectButton = nullptr;

    // Output
    QLineEdit* _filePathEdit = nullptr;
    QPushButton* _browseButton = nullptr;
    QComboBox* _containerCombo = nullptr;
    QComboBox* _videoCodecCombo = nullptr;
    QCheckBox* _includeAudioCheck = nullptr;
    QComboBox* _audioCodecCombo = nullptr;
    QPushButton* _multiTrackButton = nullptr;
    QComboBox* _qualityCombo = nullptr;
    QComboBox* _captureCombo = nullptr;
    QComboBox* _sizeCombo = nullptr;

    // Estimation
    QLabel* _estimateLabel = nullptr;
    QPushButton* _benchmarkButton = nullptr;

    // Controls
    QPushButton* _startButton = nullptr;
    QPushButton* _pauseButton = nullptr;
    QPushButton* _resumeButton = nullptr;
    QPushButton* _stopButton = nullptr;

    // Stats
    QLabel* _recordingIndicator = nullptr;
    QLabel* _durationLabel = nullptr;
    QLabel* _sizeLabel = nullptr;
    QLabel* _fpsLabel = nullptr;

    bool _signalsConnected = false;

    // Cached encoder availability (detected in refreshFromContext / onDetectFFmpeg)
    bool _nativeAvailable = false;
    bool _ffmpegAvailable = false;
    bool _ffmpegHasWebP = false;

    // Measured encoder performance, keyed by benchmarkKey()
    QHash<QString, BenchmarkEntry> _benchmarkResults;

    // Stop runs on a worker thread (encoder finalize can take seconds);
    // UI shows FINALIZING and re-enables when done
    std::atomic<bool> _isStopping{false};

    // Track recording state independently - context may become invalid mid-recording
    bool _wasRecording = false;
};
