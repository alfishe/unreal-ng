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
    uint64_t _frameCounter = 0;  // Current frame number

    // Raster geometry in beam dots (2 per T-state) and lines, renderer line origin
    int _totalPixelsPerLine = 448;    // Full line width
    int _totalLines = 320;            // Total raster lines (VSync + VBlank + visible)
    int _visibleWidth = 352;          // left border + display window + right border
    int _visibleHeight = 288;         // visible lines
    int _visibleOffsetX = 0;          // visible dots start at the line origin
    int _visibleOffsetY = 0;          // VSync+VBlank lines (visible starts after these)
    int _paperOffsetX = 48;           // display window start within the visible area
    int _paperOffsetY = 48;           // display window top within the visible area
    int _paperWidth = 256;            // display window width in beam dots
    int _paperHeight = 192;           // display window height in lines

    // Where the display window sits in the renderer's framebuffer (its own pixels)
    int _fbPaperOffsetX = 48;
    int _fbPaperWidth = 256;

    uint32_t _currentTstate = 0;
    int _currentLine = 0;
    int _linePosition = 0;
};

#endif  // ULABEAMWIDGET_H
