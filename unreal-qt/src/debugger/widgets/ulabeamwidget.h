#pragma once
#ifndef ULABEAMWIDGET_H
#define ULABEAMWIDGET_H

#include <QImage>
#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "emulator/emulator.h"

class ULABeamWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ULABeamWidget(QWidget* parent = nullptr);
    virtual ~ULABeamWidget();

    void setEmulator(Emulator* emulator);
    void reset();
    void refresh();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void createUI();
    void updateScreenImage();

    Emulator* _emulator = nullptr;
    QLabel* _titleLabel = nullptr;
    QImage _screenImage;

    // Current beam position (in full raster coordinates: pixels × lines)
    int _beamX = 0;   // Pixel position within line (0..pixelsPerLine-1)
    int _beamY = 0;   // Raster line (0..totalLines-1)

    // Raster geometry (set from RasterDescriptor)
    int _totalPixelsPerLine = 448;    // Full line width in pixels
    int _totalLines = 320;            // Total raster lines (VSync + VBlank + visible)
    int _visibleWidth = 352;          // fullFrameWidth (visible portion)
    int _visibleHeight = 288;         // fullFrameHeight (visible portion)
    int _visibleOffsetX = 0;          // HSync+HBlank pixels (visible starts after these)
    int _visibleOffsetY = 0;          // VSync+VBlank lines (visible starts after these)
    int _paperOffsetX = 48;           // screenOffsetLeft within visible area
    int _paperOffsetY = 48;           // screenOffsetTop within visible area
    int _paperWidth = 256;            // screenWidth
    int _paperHeight = 192;           // screenHeight

    uint32_t _currentTstate = 0;
    int _currentLine = 0;
    int _linePosition = 0;
};

#endif  // ULABEAMWIDGET_H
