#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <vector>

#include "emulator/sound/soundmanager.h"

class EmulatorContext;

/// Row in the Sources mixer section
struct SourceRow
{
    AudioSourceType type;
    QLabel* nameLabel = nullptr;
    QLabel* activityDot = nullptr;
    QCheckBox* muteCheck = nullptr;
    QCheckBox* soloCheck = nullptr;
    QSlider* volumeSlider = nullptr;
    QLabel* volumeLabel = nullptr;
    QProgressBar* meter = nullptr;
};

class AudioSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AudioSettingsWidget(EmulatorContext* context, QWidget* parent = nullptr);
    ~AudioSettingsWidget();

    void setContext(EmulatorContext* context);
    void refreshFromContext();

private slots:
    // Sources section
    void onSourceMuteChanged(Qt::CheckState state);
    void onSourceSoloChanged(Qt::CheckState state);
    void onSourceVolumeChanged(int value);

    // AY controls
    void onAYPunchChanged(Qt::CheckState state);
    void onAYRoomModeChanged(int index);
    void onFirChanged(Qt::CheckState state);
    void onStereoModeChanged(int index);
    void onChipModelChanged(int index);

    // Per-channel controls
    void onChannelMuteChanged(Qt::CheckState state);
    void onChannelVolumeChanged(int value);

    // Beeper controls
    void onBeeperFilterChanged(Qt::CheckState state);
    void onBeeperPunchChanged(Qt::CheckState state);

    // Covox controls
    void onCovoxDCRemovalChanged(Qt::CheckState state);
    void onCovoxChannelMuteChanged(Qt::CheckState state);

    // Meter updates
    void onUpdateMeters();

private:
    void createUI();
    void rebuildSourcesSection();
    void connectSignals();
    void disconnectSignals();
    void updateSoloIndicator();

    EmulatorContext* _context = nullptr;

    // Shown when no emulator is active
    QLabel* _statusLabel = nullptr;
    QWidget* _controlsContainer = nullptr;

    // Sources section (registry-driven)
    QGroupBox* _sourcesGroup = nullptr;
    QWidget* _sourcesContainer = nullptr;
    QLabel* _soloIndicator = nullptr;
    std::vector<SourceRow> _sourceRows;

    // AY/TurboSound section
    QGroupBox* _ayGroup = nullptr;
    QCheckBox* _firCheckbox = nullptr;
    QCheckBox* _ayPunchCheckbox = nullptr;
    QComboBox* _ayRoomCombo = nullptr;
    QComboBox* _stereoModeCombo = nullptr;
    QComboBox* _chipModelCombo = nullptr;

    // Channel Mixer (per-chip: 2 chips x 3 channels)
    QGroupBox* _channelMixerGroup = nullptr;
    QCheckBox* _channelMuteChecks[2][3] = {};   // [chip][channel]
    QSlider* _channelVolumeSliders[2][3] = {};
    QLabel* _channelVolumeLabels[2][3] = {};

    // Beeper section
    QGroupBox* _beeperGroup = nullptr;
    QCheckBox* _beeperFilterCheckbox = nullptr;
    QCheckBox* _beeperPunchCheckbox = nullptr;

    // SOUNDRIVE/COVOX section (shown only when present)
    QGroupBox* _covoxGroup = nullptr;
    QCheckBox* _covoxDCRemovalCheckbox = nullptr;
    QCheckBox* _covoxChannelMute[4] = {};  // LA, LB, RA, RB

    // Meter update timer
    QTimer* _meterTimer = nullptr;

    bool _signalsConnected = false;
};
