#pragma once
//
// timeline_widget.h — Video-editor-style timeline for TTD frame scrubbing.
//
// Custom QWidget painted via QPainter. Lanes (top to bottom):
//   1. Ruler — adaptive frame ticks + labels
//   2. Section marks — colored spans from sidecar
//   3. User marks — single-frame markers (right-click to add)
//   4. Playhead track — click-to-seek, draggable playhead
//

#include <QWidget>
#include <QTimer>
#include <cstdint>
#include "../model/marks_model.h"

namespace ttd {

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    explicit TimelineWidget(QWidget* parent = nullptr);

    void setSessionRange(uint64_t start, uint64_t end);
    void setFrame(uint64_t frame);
    void setMarksModel(MarksModel* model);
    void zoomIn();
    void zoomOut();

    uint64_t currentFrame() const { return _frame; }
    uint64_t sessionStart() const { return _rangeStart; }
    uint64_t sessionEnd() const { return _rangeEnd; }

    QSize sizeHint() const override { return {400, 120}; }
    QSize minimumSizeHint() const override { return {300, 100}; }

signals:
    void frameChanged(uint64_t frame);
    void userMarkRequested(uint64_t frame);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private slots:
    void onPlayTimer();

private:
    // Layout constants
    static constexpr int kRulerHeight    = 24;
    static constexpr int kSectionHeight  = 16;
    static constexpr int kUserMarksHeight = 16;
    static constexpr int kPlayheadHeight  = 20;
    static constexpr int kPlayheadWidth   = 8;
    static constexpr int kLeftMargin      = 4;
    static constexpr int kRightMargin     = 4;

    // Frame-to-pixel conversions
    int   frameToX(uint64_t frame) const;
    uint64_t xToFrame(int x) const;
    double framesPerPx() const;
    int    pickTickInterval() const;

    // Lane geometry
    QRect rulerRect() const;
    QRect sectionRect() const;
    QRect userMarksRect() const;
    QRect playheadRect() const;
    int   playheadX() const { return frameToX(_frame); }

    // Drawing helpers
    void drawRuler(QPainter& p);
    void drawSections(QPainter& p);
    void drawUserMarks(QPainter& p);
    void drawPlayhead(QPainter& p);
    void drawTimeMarks(QPainter& p);

    // State
    uint64_t _rangeStart = 0;
    uint64_t _rangeEnd   = 0;
    uint64_t _frame      = 0;
    bool     _dragging   = false;

    // Zoom: how many frames per pixel. Lower = zoomed in.
    double   _zoomFactor = 1.0;  // 1.0 = fit entire session

    MarksModel* _marksModel = nullptr;

    // Play/pause timer (~1 FPS for PoC)
    QTimer   _playTimer;
    bool     _playing = false;
};

} // namespace ttd
