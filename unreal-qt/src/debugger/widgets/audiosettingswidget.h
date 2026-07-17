#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>

class EmulatorContext;

class AudioSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AudioSettingsWidget(EmulatorContext* context, QWidget* parent = nullptr);

    void setContext(EmulatorContext* context);
    void refreshFromContext();

private slots:
    // AY controls
    void onAYPunchChanged(Qt::CheckState state);
    void onAYRoomModeChanged(int index);
    void onFirChanged(Qt::CheckState state);
    void onStereoModeChanged(int index);
    void onChipModelChanged(int index);

    // Per-channel controls
    void onChannelAMuteChanged(Qt::CheckState state);
    void onChannelBMuteChanged(Qt::CheckState state);
    void onChannelCMuteChanged(Qt::CheckState state);
    void onChannelAVolumeChanged(int value);
    void onChannelBVolumeChanged(int value);
    void onChannelCVolumeChanged(int value);

    // Beeper controls
    void onBeeperFilterChanged(Qt::CheckState state);
    void onBeeperPunchChanged(Qt::CheckState state);

    // Volume controls
    void onAYVolumeChanged(int value);
    void onBeeperVolumeChanged(int value);

private:
    void createUI();
    void connectSignals();
    void disconnectSignals();
    void updateChannelVolumeLabel(int channel, int value);

    EmulatorContext* _context = nullptr;

    // Shown when no emulator is active; controls container greys out
    QLabel* _statusLabel = nullptr;
    QWidget* _controlsContainer = nullptr;

    // AY controls
    QCheckBox* _firCheckbox = nullptr;
    QCheckBox* _ayPunchCheckbox = nullptr;
    QComboBox* _ayRoomCombo = nullptr;
    QComboBox* _stereoModeCombo = nullptr;
    QComboBox* _chipModelCombo = nullptr;

    // Per-channel controls
    QCheckBox* _channelAMuteCheckbox = nullptr;
    QCheckBox* _channelBMuteCheckbox = nullptr;
    QCheckBox* _channelCMuteCheckbox = nullptr;
    QSlider* _channelAVolumeSlider = nullptr;
    QSlider* _channelBVolumeSlider = nullptr;
    QSlider* _channelCVolumeSlider = nullptr;
    QLabel* _channelAVolumeLabel = nullptr;
    QLabel* _channelBVolumeLabel = nullptr;
    QLabel* _channelCVolumeLabel = nullptr;

    // Volume controls
    QSlider* _ayVolumeSlider = nullptr;
    QSlider* _beeperVolumeSlider = nullptr;
    QLabel* _ayVolumeLabel = nullptr;
    QLabel* _beeperVolumeLabel = nullptr;

    // Beeper controls
    QCheckBox* _beeperFilterCheckbox = nullptr;
    QCheckBox* _beeperPunchCheckbox = nullptr;

    bool _signalsConnected = false;
};
