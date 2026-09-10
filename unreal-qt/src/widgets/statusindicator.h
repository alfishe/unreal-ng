#pragma once

#include <QIcon>
#include <QTimer>
#include <QWidget>

/// @brief One monochrome device LED in the status bar (tape, disk, HDD, sound).
///
/// Dimmed while idle, lit while active; optionally blinks while active.
/// The icon is tinted with the palette text colour so it follows the theme.
class StatusIndicator : public QWidget
{
    Q_OBJECT

public:
    StatusIndicator(const QString& iconName, const QString& name, QWidget* parent = nullptr);

    bool isActive() const { return _active; }
    void setActive(bool on);

    /// Blink while active (activity LEDs) or stay lit (state LEDs such as sound)
    void setBlinking(bool blinking);

    /// Extra tooltip detail appended to the indicator name (e.g. "not emulated")
    void setDetail(const QString& detail);

    /// Replace the tooltip text; if the tooltip is currently shown for this widget it is
    /// updated in place (Qt only re-reads toolTip() when the popup is (re)opened)
    void setLiveToolTip(const QString& text);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    QSize sizeHint() const override { return QSize(26, 20); }

private:
    QIcon _icon;
    QString _name;
    QTimer _blink;
    bool _blinking = true;
    bool _active = false;
    bool _phase = false;
};
