#pragma once

#include <QWidget>
#include <QImage>

/// Draws the emulated 256x192 frame, optionally surrounded by the border area.
/// Scaling is integer by default; the widget letterboxes what is left over.
class ScreenWidget : public QWidget
{
    Q_OBJECT
public:
    enum ViewMode { WithBorder, Pixel1to1, Fullscreen };

    explicit ScreenWidget(QWidget *parent = nullptr);

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_mode; }

    void setIntegerScaling(bool on);
    bool integerScaling() const { return m_integer; }

    void setBorderColor(const QColor &c);

    /// Replace the framebuffer contents. Expects a 256x192 image;
    /// the emulator core would call this once per frame.
    void setFrame(const QImage &frame);

    QSize sizeHint() const override { return QSize(640, 480); }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage    m_frame;
    QColor    m_border   = QColor(0, 0, 192);
    ViewMode  m_mode     = WithBorder;
    bool      m_integer  = true;
};
