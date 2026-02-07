#pragma once
#ifndef FLOPPYDISKWIDGET_H
#define FLOPPYDISKWIDGET_H

#include <QGridLayout>
#include <QLabel>
#include <QWidget>

#include "../../../core/src/emulator/emulator.h"

class FloppyDiskWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FloppyDiskWidget(QWidget* parent = nullptr);
    virtual ~FloppyDiskWidget();

    void setEmulator(Emulator* emulator);
    void reset();
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void createUI();
    void updateDiskInfo();

    Emulator* _emulator = nullptr;
    QLabel* _titleLabel = nullptr;
    QLabel* _driveLabel = nullptr;
    QLabel* _trackLabel = nullptr;
    QLabel* _headLabel = nullptr;
    QLabel* _sectorLabel = nullptr;
    QLabel* _motorLabel = nullptr;
    QLabel* _rwLabel = nullptr;

    // Current floppy state
    int _currentDrive = 0;
    int _currentTrack = 0;
    int _currentHead = 0;
    int _currentSector = 1;
    bool _motorOn = false;
    bool _isReading = false;
    bool _isWriting = false;
};

#endif  // FLOPPYDISKWIDGET_H
