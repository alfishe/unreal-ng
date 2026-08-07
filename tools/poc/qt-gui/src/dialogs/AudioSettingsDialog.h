#pragma once

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QTimer>
#include <vector>

#ifdef HAS_EMULATOR_CORE
#include "emulator/sound/soundmanager.h"
class EmulatorContext;

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
#endif

class AudioSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AudioSettingsDialog(QWidget* parent = nullptr);
    ~AudioSettingsDialog();

#ifdef HAS_EMULATOR_CORE
    void setContext(EmulatorContext* context);
    void refreshFromContext();
#endif

private slots:
#ifdef HAS_EMULATOR_CORE
    void onSourceMuteChanged(Qt::CheckState state);
    void onSourceSoloChanged(Qt::CheckState state);
    void onSourceVolumeChanged(int value);
    void onAYPunchChanged(Qt::CheckState state);
    void onAYRoomModeChanged(int index);
    void onFirChanged(Qt::CheckState state);
    void onStereoModeChanged(int index);
    void onChipModelChanged(int index);
    void onChannelMuteChanged(Qt::CheckState state);
    void onChannelVolumeChanged(int value);
    void onBeeperFilterChanged(Qt::CheckState state);
    void onBeeperPunchChanged(Qt::CheckState state);
    void onCovoxDCRemovalChanged(Qt::CheckState state);
    void onCovoxChannelMuteChanged(Qt::CheckState state);
    void onUpdateMeters();
#endif

private:
    void createUI();

#ifdef HAS_EMULATOR_CORE
    void rebuildSourcesSection();
    void connectSignals();
    void disconnectSignals();
    void updateSoloIndicator();

    EmulatorContext* _context = nullptr;
    std::vector<SourceRow> _sourceRows;
    bool _signalsConnected = false;
#endif

    QLabel* _statusLabel = nullptr;
    QWidget* _controlsContainer = nullptr;

    QGroupBox* _sourcesGroup = nullptr;
    QWidget* _sourcesContainer = nullptr;
    QLabel* _soloIndicator = nullptr;

    QGroupBox* _ayGroup = nullptr;
    QCheckBox* _firCheckbox = nullptr;
    QCheckBox* _ayPunchCheckbox = nullptr;
    QComboBox* _ayRoomCombo = nullptr;
    QComboBox* _stereoModeCombo = nullptr;
    QComboBox* _chipModelCombo = nullptr;

    QGroupBox* _channelMixerGroup = nullptr;
    QCheckBox* _channelMuteChecks[2][3] = {};
    QSlider* _channelVolumeSliders[2][3] = {};
    QLabel* _channelVolumeLabels[2][3] = {};

    QGroupBox* _beeperGroup = nullptr;
    QCheckBox* _beeperFilterCheckbox = nullptr;
    QCheckBox* _beeperPunchCheckbox = nullptr;

    QGroupBox* _covoxGroup = nullptr;
    QCheckBox* _covoxDCRemovalCheckbox = nullptr;
    QCheckBox* _covoxChannelMute[4] = {};

    QTimer* _meterTimer = nullptr;
};
