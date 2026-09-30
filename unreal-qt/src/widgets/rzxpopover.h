#pragma once

/// @file rzxpopover.h
/// @brief The RZX playback popover of the status bar (click on "RZX nn%"):
/// where the playback is and how long the recording is, a slider to seek
/// (back through keyframes, forward by playing on), start / end jumps and stop.
///
/// A seek: the slider first stands at the target (a click on the groove jumps
/// there, not by a page), then the controls are disabled with "Seeking to
/// frame N..." and the seek runs on a worker thread (a far forward seek plays
/// many frames) while the frame it has reached is shown; the GUI never waits
/// for it. Frame times are shown at 50 frames a second (an RZX frame is one
/// interrupt interval).

#include <QFrame>
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
    explicit RzxPopover(QWidget* parent = nullptr);

    /// Show above `anchor` for this emulator's playback
    void popup(QWidget* anchor, std::weak_ptr<Emulator> emulator);

private slots:
    void refresh();
    void onSliderMoved(int value);
    void onSliderReleased();
    void onJumpStart();
    void onJumpEnd();
    void onStop();

private:
    void seekTo(uint64_t frame);
    static QString FrameTime(uint64_t frame);

    std::weak_ptr<Emulator> _emulator;
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
