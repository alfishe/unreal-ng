#pragma once

// Qt Quick, own scene-graph nodes (Qt 6.7+ QSGTextNode). No QML delegates in the hot views:
//   HexItem / DisasmItem - one QSGTextNode per row and ink colour, rebuilt only for rows whose content changed;
//                          backgrounds are QSGRectangleNodes from a pool
//   TimelineItem         - GPU backends: one QSGGeometryNode (all bars in one draw);
//                          software backend: own CPU raster into a QImage -> QSGImageNode (geometry nodes are not drawn
//                          by the software adaptation); labels are text nodes in both
//   ImageItem            - QSGImageNode with a texture made from the frame's image
// The same items run on the RHI (OpenGL here) and on QT_QUICK_BACKEND=software.

#include "../common/workload.h"

#include <QFont>
#include <QImage>
#include <QQuickItem>

#include <array>
#include <string>
#include <vector>

class QSGTextNode;
class QSGRectangleNode;

namespace sg
{

void SetWorkload(const bench::Workload* w);
const bench::Workload* W();

/// Rows of tab-separated text in up to three inks over optional row/cell backgrounds
class TextRowsItem : public QQuickItem
{
    Q_OBJECT
public:
    explicit TextRowsItem(QQuickItem* parent = nullptr);
    /// The main loop calls this after a workload tick
    void Invalidate() { update(); }

protected:
    struct RowContent
    {
        std::string ink[3];        // muted, fg, accent: the same tab layout, characters of other inks blanked
        std::vector<QRectF> bg;    // backgrounds in row coordinates
        std::vector<uint32_t> bgColour;
        bool operator==(const RowContent& o) const
        {
            return ink[0] == o.ink[0] && ink[1] == o.ink[1] && ink[2] == o.ink[2] && bg == o.bg && bgColour == o.bgColour;
        }
    };
    virtual int Rows() const = 0;
    virtual void Build(int row, RowContent& out) const = 0;
    virtual QList<qreal> Tabs() const = 0;
    virtual qreal TextX() const { return 0; }  // where the layout starts; tab stops are relative to it
    virtual int MaxBackgrounds() const = 0;

    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    struct RowNodes
    {
        QSGNode* bgParent = nullptr;
        std::array<QSGTextNode*, 3> text{};
        RowContent content;
        bool valid = false;
    };
    std::vector<RowNodes> _rows;
    float _fontPx = 0;
};

class HexItem : public TextRowsItem
{
    Q_OBJECT
public:
    using TextRowsItem::TextRowsItem;

protected:
    int Rows() const override;
    void Build(int row, RowContent& out) const override;
    QList<qreal> Tabs() const override;
    int MaxBackgrounds() const override { return bench::kHexCols; }
};

class DisasmItem : public TextRowsItem
{
    Q_OBJECT
public:
    using TextRowsItem::TextRowsItem;

protected:
    int Rows() const override;
    void Build(int row, RowContent& out) const override;
    QList<qreal> Tabs() const override;
    qreal TextX() const override;
    int MaxBackgrounds() const override { return 2; }
};

class TimelineItem : public QQuickItem
{
    Q_OBJECT
public:
    explicit TimelineItem(QQuickItem* parent = nullptr);

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    void Raster();   // software path: bars + ruler ticks into _img
    QImage _img;
};

class ImageItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int index READ index WRITE setIndex)
public:
    explicit ImageItem(QQuickItem* parent = nullptr);
    int index() const { return _index; }
    void setIndex(int i) { _index = i; }

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    int _index = 0;
};

QFont ContentFont();

}  // namespace sg
