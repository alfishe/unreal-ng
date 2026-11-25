/**
 * @file videosyncwindow.h
 * @brief VideoSyncWindow - Tools > Video Synchronization: how the window puts the machine's frames on the display, live.
 *
 * View > Video Synchronization on tries, in order: the display's VRR (each frame at its own time), a display rate that
 * fits the machine's (50 or 100 Hz for 48.83 / 50 Hz), and else smooth motion's interpolation on the fixed rate. This
 * window shows which one is in effect, why the ones before it are not, the clock that times the frames, how the
 * presents land (on the beat, late, dropped, the screen-to-sound offset) and while interpolating what the interpolation
 * does. It reads the window's present stats (the automation's `video.window` `present`).
 *
 * Laid out as the handheld menu's Network page: a head card (the mode in effect, why, the clock and the presenter as
 * chips, a state pill) over tiles in two columns, each a caption, one large value and up to three lines. A tile or a
 * line that judges nothing in the mode in effect is not shown: VRR has no beat, no display rate and no interpolation;
 * low latency no lead and no "late"; a screen without screen times no timing tiles. Every value has a cell of a fixed
 * size, so nothing moves while the values change; the tiles change only with the mode. Values refresh twice a second,
 * the rates per second over the last second.
 */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <QWidget>

#include "emulator/video/windowview.h"

class QCheckBox;
class QFrame;
class QGridLayout;
class QLabel;
class QTimer;
class VideoSyncSparkline;

class VideoSyncWindow : public QWidget
{
    Q_OBJECT

public:
    /// `settings`: the window's present settings now (MainWindow::fillPresentSettings)
    explicit VideoSyncWindow(std::function<WindowDisplaySettings()> settings, QWidget* parent = nullptr);

    /// Reads the settings now (the timer calls it; tests too)
    void refresh();
    /// The text of a value (tests), by its key: "mode", "why", "state", "clock", "presenter", "refresh", "machine",
    /// "display_rate", "shown", "dropped", "late", "off_beat", "error_p50", "error_p99", "error_max", "lead",
    /// "sound_p50", "sound_p99", "cuts", "carry", "pictures", "frames", "interpolated", "blended", "motion_fields",
    /// "held", "smooth". A value not shown in the mode in effect reads "—"
    QString readout(const char* name) const;
    /// Is the tile shown (tests): "display", "frames", "timing", "sound", "smooth", "motion"
    bool tileShown(const char* id) const;
    /// Is the value's line shown (tests): a key of readout that is a tile's line
    bool lineShown(const char* key) const;

signals:
    /// The check box: View > Video Synchronization
    void syncToggled(bool on);

public slots:
    void setSyncChecked(bool on);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct Tile
    {
        std::string id;
        QFrame* frame = nullptr;
        QLabel* caption = nullptr;
    };

    /// A tile: its caption, the large value `bigKey` and a line per {caption, key}
    Tile& addTile(const char* id, const QString& caption, const char* bigKey,
                  const std::vector<std::pair<QString, const char*>>& lines, bool sparkline = false);
    QLabel* chip(const char* key);
    void set(const char* key, const QString& text, const QString& tip = QString());
    /// Shows the tile's line or hides it (its value then reads "—")
    void showLine(const char* key, bool shown);
    /// The tiles shown, in this order, two a row; the window takes their size
    void showTiles(const std::vector<const char*>& ids);
    void applyColors();
    void clearValues();

    std::function<WindowDisplaySettings()> _settings;
    QTimer* _timer = nullptr;
    QCheckBox* _sync = nullptr;
    QFrame* _head = nullptr;
    QLabel* _state = nullptr;  ///< the pill: "Steady" / "Uneven" / "Not measured"
    QGridLayout* _tileGrid = nullptr;
    std::vector<Tile> _tiles;
    std::string _tilesShown;  ///< the ids of showTiles, joined: laid out again only when they change
    std::map<std::string, QLabel*> _cells;
    std::map<std::string, QLabel*> _lineCaptions;  ///< a line's caption by its value's key
    QLabel* _errorCaption = nullptr;  ///< the timing tile's caption: what its error measures
    VideoSyncSparkline* _shownGraph = nullptr;
    bool _uneven = false;

    // The counters a second ago: the rates per second
    WindowPresentStats _last;
    int64_t _lastNs = 0;
};
