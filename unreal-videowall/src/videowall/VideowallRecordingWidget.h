#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTabWidget>
#include <QTimer>

#include "videowall/VideowallRecorder.h"

/// @brief Qt Recording Dialog Widget for Videowall (captures combined Qt buffer up to 4K + active tile audio)
class VideowallRecordingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideowallRecordingWidget(QWidget* parent = nullptr);
    ~VideowallRecordingWidget();

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

private:
    void createUI();
    void createVideoTab();
    void createAudioTab();
    void connectSignals();
    void updateRecordingControls();
    void populateCodecCombos();
    void populateContainers();
    void populateAudioCodecs();
    void updateFileExtension();
    void startAsyncDetection();
    bool isAudioOnlyMode() const;
    void getTargetDimensions(uint32_t& width, uint32_t& height) const;

    QStringList containersForBackend() const;
    QStringList videoCodecsFor(const QString& container) const;
    QStringList audioCodecsFor(const QString& container) const;
    static QString extensionForContainer(const QString& container);

    // UI elements
    QTabWidget* _tabWidget = nullptr;
    QRadioButton* _autoBackendRadio = nullptr;
    QRadioButton* _nativeBackendRadio = nullptr;
    QRadioButton* _ffmpegBackendRadio = nullptr;
    QButtonGroup* _backendGroup = nullptr;
    QLabel* _platformInfoLabel = nullptr;
    QLineEdit* _ffmpegPathEdit = nullptr;
    QPushButton* _detectButton = nullptr;

    QLineEdit* _filePathEdit = nullptr;
    QPushButton* _browseButton = nullptr;
    QComboBox* _containerCombo = nullptr;
    QComboBox* _videoCodecCombo = nullptr;
    QComboBox* _qualityCombo = nullptr;
    QComboBox* _sizeCombo = nullptr;

    QCheckBox* _includeAudioCheck = nullptr;
    QComboBox* _audioCodecCombo = nullptr;

    // Audio Tab
    QLineEdit* _audioFilePathEdit = nullptr;
    QPushButton* _audioBrowseButton = nullptr;
    QComboBox* _audioFormatCombo = nullptr;
    QComboBox* _audioQualityCombo = nullptr;

    // Controls & Stats
    QPushButton* _startButton = nullptr;
    QPushButton* _pauseButton = nullptr;
    QPushButton* _resumeButton = nullptr;
    QPushButton* _stopButton = nullptr;

    QLabel* _statusLabel = nullptr;
    QLabel* _durationLabel = nullptr;
    QLabel* _fileSizeLabel = nullptr;
    QLabel* _fpsLabel = nullptr;

    QTimer* _statsTimer = nullptr;
    bool _nativeAvailable = false;
    bool _ffmpegAvailable = false;
};
