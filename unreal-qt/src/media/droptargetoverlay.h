#pragma once

/// @file droptargetoverlay.h
/// @brief The slot chooser over the emulator screen (media-drop-targets design §5):
/// one labeled tile per target, with the device's icon, the slot's name and
/// what it holds now. Three uses:
/// - Zones: a file held over the window for 1.5 s with several targets; the
///   tile under the cursor lights up, a drop on it picks that slot
/// - Chooser: after a quick drop (or File > Insert Medium...) with several
///   targets: click a tile, press its number or Enter; Esc cancels
/// - Refusal: a file no slot takes, red with the reason, at once
/// - Confirm: a quick drop that was placed without asking (the default slot): the tiles,
///   the slot it went to glowing, input-transparent; gone, fading, well under a second
///   after the drop whatever happens
///
/// A frameless translucent window of its own over the screen area, not a child
/// widget: the GPU screen is a native OpenGL window that a child widget would
/// not show over, and a window of its own takes the drag directly.

#include <QPixmap>
#include <QPropertyAnimation>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <map>
#include <vector>

#include "emulator/media/mediatargets.h"

class DropTargetOverlay : public QWidget
{
    Q_OBJECT

public:
    enum class Mode
    {
        Hidden,
        Zones,
        Chooser,
        Refusal,
        Confirm,
    };

    /// @param owner the main window: the overlay stays above it
    explicit DropTargetOverlay(QWidget* owner);

    /// Over `globalArea`: the tiles of `plan`, taking the drag. The zones open under a cursor held still, over
    /// whichever tile lands there: tile `restIndex` (where a quick drop goes) glows instead, and a drop before the
    /// cursor moves kPickSlop away goes to it; after that, to the tile under the cursor
    void showZones(const QRect& globalArea, const QString& fileName, const MediaPlan& plan, int restIndex = -1);

    static constexpr int kPickSlop = 12;  ///< px the cursor moves after the zones open before a tile is picked
    /// Over `globalArea`: the tiles of `plan`, taking clicks and keys
    void showChooser(const QRect& globalArea, const QString& fileName, const MediaPlan& plan);
    /// Over `globalArea`: red, the file's name and `reason`. autoHideMs > 0:
    /// gone after that long (a refusal after a drop); 0: until dismissed
    void showRefusal(const QRect& globalArea, const QString& fileName, const QString& reason, int autoHideMs = 0);
    /// Over `globalArea`: the tiles of `plan` with tile `index` glowing (where the drop went), then
    /// fading out; hidden kConfirmHoldMs + kConfirmFadeMs after the call. Takes no input
    void showConfirm(const QRect& globalArea, const QString& fileName, const MediaPlan& plan, int index);
    void dismiss();

    static constexpr int kConfirmHoldMs = 450;
    static constexpr int kConfirmFadeMs = 400;

    Mode mode() const { return _mode; }
    /// Tile `index`'s place in the overlay (empty when there is none)
    QRect tileRect(int index) const
    {
        return index >= 0 && index < static_cast<int>(_tiles.size()) ? _tiles[static_cast<size_t>(index)] : QRect();
    }
    int hovered() const { return _hovered; }  ///< the glowing tile, -1: none
    const MediaPlan& plan() const { return _plan; }

    /// The icon resource of a device kind (":/icons/media/floppy.svg", ...)
    static QString IconPath(FileKind kind);

signals:
    /// Zones: a drop on a tile. Chooser: a click, its number key or Enter
    void targetChosen(int index);
    /// Zones: a drop away from the tiles (the caller opens the chooser)
    void droppedOutside();
    /// Zones: the drag left the area. Chooser: Esc or a click away from the tiles.
    /// Refusal: shown long enough, or the refused file was dropped
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void open(Mode mode, const QRect& globalArea, const QString& fileName, const MediaPlan& plan);
    void layoutTiles();
    int tileAt(const QPoint& pos) const;
    int zoneAt(const QPoint& pos);  ///< Zones: the tile a drop at `pos` goes to (the rest tile until the cursor moves)
    void setHovered(int index);
    const QPixmap& iconFor(FileKind kind, int size);

    Mode _mode = Mode::Hidden;
    MediaPlan _plan;
    QString _fileName;
    QString _reason;
    std::vector<QRect> _tiles;
    int _hovered = -1;
    int _restIndex = -1;      ///< Zones: the tile a drop goes to before the cursor moves (-1: the one under it)
    bool _restSet = false;    ///< Zones: the cursor's resting place is known (the first drag event after opening)
    bool _picking = false;    ///< Zones: the cursor moved off its resting place: the tile under it is the pick
    QPoint _restPos;
    QTimer _autoHide;
    QTimer _confirmHold;        ///< Confirm: the glow stays this long, then the fade starts
    QPropertyAnimation _fade;   ///< Confirm: window opacity 1 -> 0
    std::map<std::pair<int, int>, QPixmap> _icons;  ///< (kind, size) -> rendered icon
};
