#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QProgressBar>
#include <QFrame>
#include <QToolButton>
#include <QPushButton>
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

signals:
    /// The user chose another General Sound card (a GSTypeKind): a slot change that restarts the machine (Q10), made
    /// by the main window
    void generalSoundCardRequested(int kind);

private slots:
    // Sources section
    void onSourceMuteChanged(int state);
    void onSourceSoloChanged(int state);
    void onSourceVolumeChanged(int value);

    // AY controls
    void onAYVoicingChanged(int index);
    void onAYPunchChanged(int state);
    void onAYRoomModeChanged(int index);
    void onFirChanged(int state);
    void onStereoModeChanged(int index);
    void onChipModelChanged(int index);

    // TSFM controls
    void onFmTrimChanged(int value);

    // Per-channel controls
    void onChannelMuteChanged(int state);
    void onChannelVolumeChanged(int value);

    // Beeper controls
    void onBeeperPunchChanged(int state);

    // General Sound slot controls
    void onGSCardChanged(int index);
    void onNeoGSInsertSd();
    void onNeoGSEjectSd();
    void onNeoGSStereoModeChanged(int index);

    // Covox controls
    void onCovoxDCRemovalChanged(int state);
    void onCovoxChannelMuteChanged(int state);

    // Meter updates
    void onUpdateMeters();

private:
    void createUI();
    void rebuildSourcesSection();
    void connectSignals();
    void disconnectSignals();
    void updateSoloIndicator();
    void updateDeviceInfo();
    void updatePunchVoicingHint();
    QString buildDeviceDetailText() const;  // Full breakdown for the (i) popup
    void updateGSSection(const GeneralSoundSlot& slot);

    EmulatorContext* _context = nullptr;

    // Shown when no emulator is active
    QLabel* _statusLabel = nullptr;
    QLabel* _deviceInfoLabel = nullptr;   // Compact device/core readout (rarely changes)
    QToolButton* _deviceInfoButton = nullptr;  // (i): detailed latency/AV breakdown popup
    QFrame* _detailPopup = nullptr;            // Live-refreshing breakdown popup
    QLabel* _detailLabel = nullptr;
    QTimer* _detailTimer = nullptr;
    QWidget* _controlsContainer = nullptr;

    // Sources section (registry-driven)
    QGroupBox* _sourcesGroup = nullptr;
    QWidget* _sourcesContainer = nullptr;
    QLabel* _soloIndicator = nullptr;
    std::vector<SourceRow> _sourceRows;

    // TurboSound section - titled "TurboSound FM" when the slot
    // device is the TSFM board (the Sources list and chip labels follow)
    QGroupBox* _ayGroup = nullptr;
    QCheckBox* _firCheckbox = nullptr;
    QComboBox* _ayVoicingCombo = nullptr;  // EQ profile (FilterVoicing voicing): HQ and LQ alike
    QCheckBox* _ayPunchCheckbox = nullptr;
    QLabel* _punchVoicingHint = nullptr;   // "Punch was tuned for Classic voicing" (Flat + punch)
    QComboBox* _ayRoomCombo = nullptr;
    QComboBox* _stereoModeCombo = nullptr;
    QComboBox* _chipModelCombo = nullptr;

    // TSFM-only controls (visible when the device has FM channels, §8.3)
    QWidget* _tsfmControls = nullptr;
    QSlider* _fmTrimSlider = nullptr;   // -12..+12 dB in 0.5 dB steps (half-dB units)
    QLabel* _fmTrimLabel = nullptr;

    // Channel Mixer (per-chip: 2 chips x 3 channels)
    QGroupBox* _channelMixerGroup = nullptr;
    QLabel* _chip1SectionLabel = nullptr;  // "AY1" / "SSG 1"
    QLabel* _chip2SectionLabel = nullptr;  // "AY2" / "SSG 2"
    QCheckBox* _channelMuteChecks[2][3] = {};   // [chip][channel]
    QSlider* _channelVolumeSliders[2][3] = {};
    QLabel* _channelVolumeLabels[2][3] = {};

    // Beeper section
    QGroupBox* _beeperGroup = nullptr;
    QCheckBox* _beeperPunchCheckbox = nullptr;

    // General Sound slot section (shown only when a GS card is fitted). The
    // card is read through SoundManager::generalSoundSlot() - a copy that is
    // safe on this thread - and polled with the meters, so a card switch or
    // an SD change from automation shows up here too
    QGroupBox* _gsGroup = nullptr;
    QComboBox* _gsCardCombo = nullptr;          // classic / lightweight player / NeoGS
    QWidget* _neoGSControls = nullptr;          // SD card row, NeoGS only
    QLabel* _neoGSSdLabel = nullptr;
    QPushButton* _neoGSInsertButton = nullptr;
    QPushButton* _neoGSEjectButton = nullptr;
    QWidget* _neoGSStereoRow = nullptr;         // NeoGS only
    QComboBox* _neoGSStereoCombo = nullptr;     // separated / GS cross-feed / mono
    QLabel* _gsStatusLabel = nullptr;           // switching / refused
    GeneralSoundSlot _shownGSSlot;              // what the section shows

    // SOUNDRIVE/COVOX section (shown only when present)
    QGroupBox* _covoxGroup = nullptr;
    QCheckBox* _covoxDCRemovalCheckbox = nullptr;
    QCheckBox* _covoxChannelMute[4] = {};  // LA, LB, RA, RB

    // Meter update timer
    QTimer* _meterTimer = nullptr;

    bool _signalsConnected = false;
};
