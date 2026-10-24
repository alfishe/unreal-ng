#pragma once

/// @file patchcordoverlay.h
/// @brief The patch cords of the controllers dialog (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §2.3): one
/// cord per plug, from a host device's row to the connector card it drives, through the picture: a cord leaves the
/// device row in the gutter between the device list and the picture, enters the picture at its middle, and leaves it
/// on the other side towards the connector's card, so the photo itself is never drawn over. The colour is the
/// connector's, and the connector's tag (K, S1, CUR...) sits at the cord's end (§6); the selected device's cords are drawn full, the others quieter; a cord pulses
/// while its device sends (a control away from rest).
///
/// A transparent child over the whole dialog (mouse events pass through); the dialog hands it the geometry and the
/// cords on its 30 Hz tick, and it repaints only when they changed or while a cord pulses.

#include <QColor>
#include <QElapsedTimer>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <vector>

class QAbstractItemView;
class QFontMetrics;

class PatchCordOverlay : public QWidget
{
    Q_OBJECT

public:
    struct Cord
    {
        QPointF from;  ///< the device row's right edge (overlay coordinates)
        QPointF to;    ///< the connector card's left edge
        QColor colour;
        QString tag;
        bool selected = false;  ///< the device shown in the picture
        bool live = false;      ///< the device sends now: the cord pulses
        bool operator==(const Cord& other) const
        {
            return from == other.from && to == other.to && colour == other.colour && tag == other.tag &&
                   selected == other.selected && live == other.live;
        }
    };

    explicit PatchCordOverlay(QWidget* parent);

    /// @p passage: the picture's rectangle the cords pass behind (overlay coordinates); empty: straight cords
    void setCords(const std::vector<Cord>& cords, const QRectF& passage);
    const std::vector<Cord>& cords() const
    {
        return _cords;
    }
    /// Where a cord meets a row of @p view: its right (a device) or left (a connector) edge in @p parent's
    /// coordinates, the y held inside the visible part of the list (a row scrolled away pins to the edge)
    static QPointF RowAnchor(QWidget* parent, QAbstractItemView* view, const QRect& row, bool right);
    /// The path one cord takes (exposed for the tests): the left run, and the right run when it passes the picture
    std::vector<QPainterPath> pathsOf(size_t index) const;
    /// Where a cord's tag is drawn: just left of its connector end
    static QRectF TagRect(const Cord& cord, const QFontMetrics& metrics);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<Cord> _cords;
    QRectF _passage;
    QElapsedTimer _clock;  ///< the pulse's phase
};
