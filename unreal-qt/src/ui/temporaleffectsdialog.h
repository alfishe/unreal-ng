#pragma once

#include <QDialog>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QElapsedTimer>
#include <QTimer>

#include <functional>

class DeviceScreenWrapper;
class Emulator;

/// Temporal effects: combine several emulated frames into one displayed frame.
///   Frame blending - the GUI blend of the last 2..5 frames (DeviceScreenWrapper)
///   ZX DLSS        - the core's de-flicker algorithm (Screen::SetTemporalAlgorithm),
///                    run on every emulated frame; video and audio are delayed by its
///                    look-ahead. It needs plane B: the core switches the zxdlss and
///                    screenhq features on with it.
/// Only one of the two runs at a time (blending a de-flickered picture again would
/// smear it).
class TemporalEffectsDialog : public QDialog
{
    Q_OBJECT

public:
    using EmulatorSource = std::function<Emulator*()>;

    explicit TemporalEffectsDialog(DeviceScreenWrapper* screenWrapper, EmulatorSource emulator,
                                   QWidget* parent = nullptr);

signals:
    /// The GUI frame blending was switched on or off here (the View menu's check mark follows)
    void blendingChanged(bool enabled);

public slots:
    void updateFromScreen();

private slots:
    void onEnabledChanged(bool enabled);
    void onEffectChanged(int index);
    void onAlgorithmChanged(int index);
    void onHistorySizeChanged(int value);
    void onWeightModeChanged(int index);
    void onDecayChanged(double value);
    void refreshDlssStatus();

private:
    enum Effect
    {
        EffectBlend = 0,
        EffectDlss = 1,
    };

    void applyEffect();
    void updateGroups();
    void setLed(bool lit, const QString& text);
    void showLastDetection();
    std::string currentAlgorithm() const;

    DeviceScreenWrapper* _screenWrapper;
    EmulatorSource _emulator;

    QCheckBox* _enabledCheck;
    QComboBox* _effectCombo;
    QGroupBox* _blendGroup;
    QSlider* _historySizeSlider;
    QLabel* _historySizeLabel;
    QComboBox* _weightModeCombo;
    QDoubleSpinBox* _decaySpin;
    QLabel* _decayLabel;
    QGroupBox* _dlssGroup;
    QComboBox* _algorithmCombo;
    QLabel* _correctionLed;      // lit while the frame on screen is the algorithm's output
    QLabel* _correctionLabel;
    QLabel* _dlssStatus;
    QLabel* _dlssDetectors;      // the averaging mask of the frame on screen, or the last one
    QString _lastDetection;      // the last averaging mask seen
    QElapsedTimer _lastDetectionAge;
    QTimer _statusTimer;
};
