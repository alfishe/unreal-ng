#pragma once
#ifndef BORDERTIMINGWIDGET_H
#define BORDERTIMINGWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVector>

#include "emulator/emulator.h"

class BorderTimingWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BorderTimingWidget(QWidget *parent = nullptr);
    virtual ~BorderTimingWidget();

    void setEmulator(Emulator* emulator);
    void reset();
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void createUI();
    
    Emulator* _emulator = nullptr;
    QLabel* _titleLabel = nullptr;
    
    // Border timing data
    static constexpr int MAX_HISTORY = 100;
    QVector<int> _borderValues;  // History of border color values
    QVector<int> _tStates;       // T-states for each border change
    
    // Current frame T-states
    int _currentFrameTStates = 0;
    int _totalFrameTStates = 0;
};

#endif // BORDERTIMINGWIDGET_H
