#pragma once

#include <QWidget>
#include <QPushButton>
#include <QToolButton>
#include <QComboBox>
#include <QLabel>
#include <QTimer>
#include <memory>
#include <string>

#include "widgets/recordingsettings.h"

class MainWindow;
class Emulator;
class EmulatorContext;

class RecordingWidget : public QWidget
{
    Q_OBJECT

public:
    explicit RecordingWidget(MainWindow* mainWindow, QWidget* parent = nullptr);
    ~RecordingWidget() override;

    /// Return the desired pixel height of this widget
    int desiredHeight() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    /// User-intended visibility state (persisted across emulator runs)
    bool isVisibleByUser() const { return _visibleByUser; }
    void setVisibleByUser(bool visible);

    /// Synchronize controls with active emulator context
    void updateState(std::shared_ptr<Emulator> activeEmulator);

    /// Reload settings from QSettings (e.g. after advanced dialog closes)
    void reloadSettings();

    /// Check if currently actively recording
    bool isRecording() const { return _isRecording; }

signals:
    void heightChanged();
    void visibilityChanged(bool visible);
    void advancedSettingsRequested();
    void recordingStateChanged(bool isRecording);

private slots:
    void onRecordToggled();
    void onPauseToggled();
    void onContainerChanged(int index);
    void onBackendChanged(int index);
    void onQualityChanged(int index);
    void onRegionChanged(int index);
    void onUpdateStats();

private:
    void populateBackends();
    void updateBackendTooltip();
    void populateContainers();
    void updateQualityOptions();
    void updateRegionAvailability();
    void applySettingsToUI();
    void updateStatusLabel();
    EmulatorContext* getActiveContext() const;
    QString generateOutputPath(const QString& container) const;

    MainWindow* _mainWindow = nullptr;
    std::shared_ptr<Emulator> _activeEmulator;

    QWidget* _controlContainer = nullptr;
    QPushButton* _recordBtn = nullptr;
    QPushButton* _pauseBtn = nullptr;
    QComboBox* _containerCombo = nullptr;
    QComboBox* _backendCombo = nullptr;
    QComboBox* _qualityCombo = nullptr;
    QComboBox* _regionCombo = nullptr;
    QLabel* _statusLabel = nullptr;
    QToolButton* _advancedBtn = nullptr;
    QToolButton* _closeBtn = nullptr;

    QTimer* _statsTimer = nullptr;
    RecordingSettings _settings;
    int _rowHeight = 26;
    bool _visibleByUser = false;
    bool _isRecording = false;
    bool _isPaused = false;
};
