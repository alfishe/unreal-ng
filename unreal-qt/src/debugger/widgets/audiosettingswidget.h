#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QLabel>

class EmulatorContext;

class AudioSettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AudioSettingsWidget(EmulatorContext* context, QWidget* parent = nullptr);

private slots:
    // AY controls
    void onAYPunchChanged(Qt::CheckState state);
    void onAYRoomModeChanged(int index);
    void onFirChanged(Qt::CheckState state);

    // Beeper controls
    void onBeeperFilterChanged(Qt::CheckState state);
    void onBeeperPunchChanged(Qt::CheckState state);

private:
    EmulatorContext* _context;

    // AY controls
    QCheckBox* _firCheckbox;
    QCheckBox* _ayPunchCheckbox;
    QComboBox* _ayRoomCombo;

    // Beeper controls
    QCheckBox* _beeperFilterCheckbox;
    QCheckBox* _beeperPunchCheckbox;
};
