/**
 * @file videosyncwindow.h
 * @brief VideoSyncWindow - Tools > Video Synchronization: how the window puts the machine's frames on the display, live.
 *
 * View > Video Synchronization on tries, in order: the display's VRR (each frame at its own time), a display rate that
 * fits the machine's (50 or 100 Hz for 48.83 / 50 Hz), and else smooth motion's interpolation on the fixed rate. This
 * window shows which one is in effect, why the ones before it are not, the clock that times the frames, how the
 * presents land (on the beat, late, dropped, the screen-to-sound offset) and while interpolating what the interpolation
 * does, as the audio settings show the dynamic rate control. It reads the window's present stats (the automation's
 * `video.window` `present`).
 *
 * Laid out as a form of fixed cells: every value has its own label of a fixed width, right aligned for numbers, and
 * the reason text a fixed height, so nothing moves while the values change. Values refresh twice a second, the rates
 * per second over the last second.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include <QWidget>

#include "emulator/video/windowview.h"

class QCheckBox;
class QGridLayout;
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
    /// The text of a value cell (tests), by its key: "mode", "why", "clock", "refresh", "machine", "display_rate",
    /// "presenter", "shown", "dropped", "late", "off_beat", "error_p50", "error_p99", "error_max", "sound_p50",
    /// "sound_p99", "lead", "smooth", "frames", "pictures", "interpolated", "blended", "motion_fields", "held"
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
    enum class Align
    {
        Text,    // left aligned, as wide as the column
        Number,  // right aligned in a cell of a fixed width
    };
    /// A row: its caption in column 0 and value cells from column 1, each `span` columns wide
    QLabel* cell(QGridLayout* grid, const char* key, int row, int column, Align align, int span = 1);
    void set(const char* key, const QString& text);
    void clearValues();

    std::function<WindowDisplaySettings()> _settings;
    QTimer* _timer = nullptr;
    QCheckBox* _sync = nullptr;
    std::map<std::string, QLabel*> _cells;
    int _numberWidth = 0;

    // The counters a second ago: the rates per second
    WindowPresentStats _last;
    int64_t _lastNs = 0;
};
