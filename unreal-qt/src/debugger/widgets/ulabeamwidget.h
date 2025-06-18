#pragma once
#ifndef ULABEAMWIDGET_H
#define ULABEAMWIDGET_H

#include <QWidget>
#include <QLabel>
#include <QTimer>

#include "emulator/emulator.h"

class ULABeamWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ULABeamWidget(QWidget *parent = nullptr);
    virtual ~ULABeamWidget();

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
    
    // Screen dimensions (for ZX Spectrum)
    static constexpr int SCREEN_WIDTH = 256;
    static constexpr int SCREEN_HEIGHT = 192;
    static constexpr int BORDER_WIDTH = 48;
    static constexpr int BORDER_HEIGHT = 48;
    
    // Current beam position
    int _beamX = 0;
    int _beamY = 0;
    
    // Scale factor for drawing
    float _scaleX = 1.0f;
    float _scaleY = 1.0f;
};

#endif // ULABEAMWIDGET_H
