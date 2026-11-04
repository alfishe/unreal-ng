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
#include <QTabWidget>
#include <QHash>
#include <QTimer>

#include <QPointer>

#include "widgets/recordingplan.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

class DeviceScreenWrapper;
class Emulator;
class EmulatorContext;
struct RecordingSettings;
class WindowRecorder;

class VideoRecordingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoRecordingWidget(std::shared_ptr<Emulator> emulator, QWidget* parent = nullptr);
    ~VideoRecordingWidget();

    /// The active machine (call when the emulator instance changes); held, so a removed one stays readable until
    /// the GUI moves on (BUGS.md #11)
    void setEmulator(std::shared_ptr<Emulator> emulator);

    /// The window recorder (recording the window as it draws it, with the CRT shaders) and the window's current
    /// screen: with them the Window source works here exactly as in the recording panel
    void setWindowRecording(WindowRecorder* recorder, std::function<DeviceScreenWrapper*()> screen);
    /// Show a window recording another recording UI (the panel, the WebAPI) started or finished
    void syncWindowRecordingState();

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
    void onProfileChanged();
    void onSourceChanged();
    void onMultiTrackConfigure();
    void onBenchmark();

private:
    void createUI();
    void createVideoTab();
    void createAudioTab();
    void connectSignals();
    void refreshFromContext();
    void updateRealtimeEstimate();
    void updateRecordingControls();

    // The options shared with the recording panel (RecordingSettings): every control saves its change at once and
    // follows the changes of the other UI
    void saveFromControls();
    void applySharedSettings();
    void applyFormatFromSettings(const RecordingSettings& settings);
    void updateSourceAvailability();
    static QString codecIdOf(const QString& codecText);
    QString currentCodecId() const;
    bool windowSourceAvailable() const;
    bool windowSourceChosen() const;  ///< the Window source is selected (and the format takes it)
    void startWindowRecording(const QString& filename, const QString& container, const QString& codec, bool audio);
    void stopWindowRecording();
    void updateWindowRecordingStats();
    void populateCodecCombos();
    void populateContainers();
    void populateAudioCodecs();
    void updateFileExtension();
    void detectPlatformInfo();
    void runBenchmark();
    void startAsyncDetection();
    void startAsyncFFmpegDetection();

    /// Check if audio-only mode is selected (Audio tab active)
    bool isAudioOnlyMode() const;

    /// Drop the held machine once it was removed (retired): the controls go idle
    void validateContext();

    /// Refresh the audio tab's capture-rate readout (core rate = the rate
    /// audio-only recordings capture at; device native rate for reference)
    void updateAudioRateInfo();

    /// The emulator we started recording with (a reference of the caller's own: it stays readable for the call).
    /// nullptr if that emulator was removed. When not recording, the active one
    std::shared_ptr<Emulator> recordingEmulator() const;
    /// The active machine's context, per call (never stored)
    EmulatorContext* context() const;

    // Encoder capability matrix (what the selected backend can actually produce)
    recordingplan::Environment environment() const;
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

    std::shared_ptr<Emulator> _emulator;
    std::string _contextEmulatorId;     // Emulator ID of _emulator
    std::string _recordingEmulatorId;   // Emulator ID we started recording with
    QTimer* _statsTimer = nullptr;

    // Tab widget for Video vs Audio-only modes
    QTabWidget* _tabWidget = nullptr;

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
    QComboBox* _profileCombo = nullptr;  ///< native / 1080p / 1440p / 4K (fixed frame, sharp scale)
    QComboBox* _sourceCombo = nullptr;   ///< Raw frame buffer / Window (as drawn: shaders, temporal processing)

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
    QLabel* _fpsStaticLabel = nullptr;
    QLabel* _fpsLabel = nullptr;

    // Audio-only tab controls
    QComboBox* _audioFormatCombo = nullptr;
    QComboBox* _audioQualityCombo = nullptr;
    QLabel* _audioQualityLabel = nullptr;
    QLabel* _audioRateInfoLabel = nullptr;  // Live core/device sample-rate readout
    QTimer* _rateInfoTimer = nullptr;
    QLineEdit* _audioFilePathEdit = nullptr;
    QPushButton* _audioBrowseButton = nullptr;

    // DSD-specific options (visible only when DSD format selected)
    QGroupBox* _dsdOptionsGroup = nullptr;
    QCheckBox* _dsdNativeCheck = nullptr;
    QCheckBox* _dsdPunchCheck = nullptr;
    QComboBox* _dsdPunchAmountCombo = nullptr;

    bool _signalsConnected = false;

    // Shared options and the window recording
    bool _syncing = false;    ///< the controls are being set from the shared settings: no saving back
    bool _selfSave = false;   ///< this dialog is saving: it does not reload its own change
    QPointer<WindowRecorder> _windowRecorder;
    std::function<DeviceScreenWrapper*()> _windowScreen;
    bool _windowRecording = false;  ///< the running recording is the window's, not the recording manager's

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

    // MessageCenter observer for external recording state changes
    uint64_t _recordingStateObserverId = 0;
    void subscribeRecordingObserver();
    void unsubscribeRecordingObserver();
    void onRecordingStateChanged(bool isRecording, const std::string& emulatorId);
};
