#pragma once

/// @file rzxpopover.h
/// @brief The RZX playback popover of the status bar (click on "RZX nn%"):
/// where the playback is and how long the recording is, a slider to seek
/// (back through keyframes, forward by playing on), start / end jumps and stop.
///
/// A transient popover (as NSPopover): a frameless tool window owned by the
/// main window - a window of its own, so it sits safely above the GPU screen
/// (a QOpenGLWindow in a container) - that only lives while the main window is
/// in front. It closes when the focus goes to another window or application,
/// on a click outside it, on Esc and when the main window is minimized. Where
/// the OS can glue it to the main window (macOS child windows,
/// platform/childwindow.h) it moves with the window by itself; elsewhere it
/// follows the window's move events. It stays above the status bar label on
/// resizes.
/// visibilityChanged() lets the label show the open state (a toggle).
///
/// A seek: the slider first stands at the target (a click on the groove jumps
/// there, not by a page), then the controls are disabled with "Seeking to
/// frame N..." and the seek runs on a worker thread (a far forward seek plays
/// many frames) while the frame it has reached is shown; the GUI never waits
/// for it. Frame times are shown at 50 frames a second (an RZX frame is one
/// interrupt interval).

#include <QFrame>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <memory>

class Emulator;
class QLabel;
class QPushButton;
class QSlider;

class RzxPopover : public QFrame
{
    Q_OBJECT

public:
    /// `parent` is the main window (the owner of the popover window)
    explicit RzxPopover(QWidget* parent = nullptr);

    /// Show above `anchor` for this emulator's playback
    void popup(QWidget* anchor, std::weak_ptr<Emulator> emulator);

signals:
    /// Shown or hidden, for whatever reason (the anchor shows the open state)
    void visibilityChanged(bool visible);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    /// Detach from the main window before the window goes (a hidden child
    /// window must not be brought back by its parent)
    void setVisible(bool visible) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void refresh();
    void onSliderMoved(int value);
    void onSliderReleased();
    void onJumpStart();
    void onJumpEnd();
    void onStop();

private:
    void seekTo(uint64_t frame);
    /// Above the anchor, right-aligned with it, within the main window
    void reposition();
    /// After a focus change settles: closed unless the popover or its main
    /// window is the active one
    void closeUnlessActive();
    static QString FrameTime(uint64_t frame);

    std::weak_ptr<Emulator> _emulator;
    QPointer<QWidget> _anchor;
    bool _attached = false;  ///< glued to the main window by the OS (ChildWindow)
    QLabel* _title = nullptr;
    QLabel* _position = nullptr;
    QLabel* _state = nullptr;
    QLabel* _keyframes = nullptr;
    QSlider* _slider = nullptr;
    QPushButton* _startButton = nullptr;
    QPushButton* _endButton = nullptr;
    QPushButton* _stopButton = nullptr;
    QTimer _timer;
    bool _dragging = false;
    uint64_t _seekTarget = 0;  ///< the frame the running seek goes to
    std::shared_ptr<std::atomic<bool>> _seeking = std::make_shared<std::atomic<bool>>(false);
};
