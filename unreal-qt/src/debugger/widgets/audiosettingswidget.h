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
    void onPunchChanged(Qt::CheckState state);
    void onFirChanged(Qt::CheckState state);
    void onRoomModeChanged(int index);

private:
    EmulatorContext* _context;

    QCheckBox* _punchCheckbox;
    QCheckBox* _firCheckbox;
    QComboBox* _roomCombo;
};
