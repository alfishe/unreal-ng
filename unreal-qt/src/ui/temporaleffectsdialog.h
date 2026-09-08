#pragma once

#include <QDialog>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QDoubleSpinBox>

class DeviceScreenWrapper;

class TemporalEffectsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TemporalEffectsDialog(DeviceScreenWrapper* screenWrapper, QWidget* parent = nullptr);

private slots:
    void onEnabledChanged(bool enabled);
    void onHistorySizeChanged(int value);
    void onWeightModeChanged(int index);
    void onDecayChanged(double value);

private:
    void updateFromScreen();
    void applySettings();

    DeviceScreenWrapper* _screenWrapper;

    QCheckBox* _enabledCheck;
    QSlider* _historySizeSlider;
    QLabel* _historySizeLabel;
    QComboBox* _weightModeCombo;
    QDoubleSpinBox* _decaySpin;
    QLabel* _decayLabel;
};
