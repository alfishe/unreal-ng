/**
 * @file videosyncwindow.h
 * @brief VideoSyncWindow - Tools > Video Synchronization: how the window puts the machine's frames on the display, live.
 *
 * View > Video Synchronization on tries, in order: the display's VRR (each frame at its own time), a display rate that
 * fits the machine's (50 or 100 Hz for 48.83 / 50 Hz), and else smooth motion's interpolation on the fixed rate. This
 * window shows which one is in effect, why the ones before it are not, and while interpolating what the interpolation
 * does (renders, motion fields, held flicker per second), as the audio settings show the dynamic rate control. It reads
 * the window's present stats (the automation's `video.window` `present`) four times a second while visible.
 */

#pragma once

#include <cstdint>
#include <functional>

#include <QWidget>

#include "emulator/video/windowview.h"

class QCheckBox;
class QLabel;
class QTimer;

class VideoSyncWindow : public QWidget
{
    Q_OBJECT

public:
    /// `settings`: the window's present settings now (MainWindow::fillPresentSettings)
    explicit VideoSyncWindow(std::function<WindowDisplaySettings()> settings, QWidget* parent = nullptr);

    /// Reads the settings now (the timer calls it; tests too)
    void refresh();
    /// The text of a readout (tests): "mode", "why", "display", "timing", "smooth"
    QString readout(const char* name) const;

signals:
    /// The check box: View > Video Synchronization
    void syncToggled(bool on);

public slots:
    void setSyncChecked(bool on);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    std::function<WindowDisplaySettings()> _settings;
    QTimer* _timer = nullptr;
    QCheckBox* _sync = nullptr;
    QLabel* _mode = nullptr;
    QLabel* _why = nullptr;
    QLabel* _display = nullptr;
    QLabel* _timing = nullptr;
    QLabel* _smooth = nullptr;

    // The counters at the last refresh: the rates per second
    WindowPresentStats _last;
    int64_t _lastNs = 0;
};
