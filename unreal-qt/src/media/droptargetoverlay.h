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
///
/// A frameless translucent window of its own over the screen area, not a child
/// widget: the GPU screen is a native OpenGL window that a child widget would
/// not show over, and a window of its own takes the drag directly.

#include <QPixmap>
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
    };

    /// @param owner the main window: the overlay stays above it
    explicit DropTargetOverlay(QWidget* owner);

    /// Over `globalArea`: the tiles of `plan`, taking the drag
    void showZones(const QRect& globalArea, const QString& fileName, const MediaPlan& plan);
    /// Over `globalArea`: the tiles of `plan`, taking clicks and keys
    void showChooser(const QRect& globalArea, const QString& fileName, const MediaPlan& plan);
    /// Over `globalArea`: red, the file's name and `reason`. autoHideMs > 0:
    /// gone after that long (a refusal after a drop); 0: until dismissed
    void showRefusal(const QRect& globalArea, const QString& fileName, const QString& reason, int autoHideMs = 0);
    void dismiss();

    Mode mode() const { return _mode; }
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
    void setHovered(int index);
    const QPixmap& iconFor(FileKind kind, int size);

    Mode _mode = Mode::Hidden;
    MediaPlan _plan;
    QString _fileName;
    QString _reason;
    std::vector<QRect> _tiles;
    int _hovered = -1;
    QTimer _autoHide;
    std::map<std::pair<int, int>, QPixmap> _icons;  ///< (kind, size) -> rendered icon
};
