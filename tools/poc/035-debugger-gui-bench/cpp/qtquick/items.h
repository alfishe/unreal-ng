#pragma once

// Qt Quick variant, C++ side.
//   HexModel / DisasmModel - QAbstractListModel per view; QML Repeaters with Text delegates bind to them
//                            (the idiomatic QML way); only rows that changed emit dataChanged
//   TimelineItem           - custom QQuickItem: one QSGGeometryNode with per-vertex colours (all bars in one draw)
//   ImageItem              - custom QQuickItem: QSGSimpleTextureNode, a new texture from the frame's QImage each frame
//   Bridge                 - ruler labels and the playhead for QML

#include "../common/workload.h"

#include <QAbstractListModel>
#include <QQuickItem>
#include <QStringList>
#include <QVariantList>

#include <array>

/// Content text metrics for QML (common/layout.h), plus the runtime zoom used by the scale-switch test
class Metrics : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal fontPx READ fontPx NOTIFY changed)
    Q_PROPERTY(qreal lineH READ lineH NOTIFY changed)
    Q_PROPERTY(qreal hexBytesX READ hexBytesX NOTIFY changed)
    Q_PROPERTY(qreal hexByteStep READ hexByteStep NOTIFY changed)
    Q_PROPERTY(qreal hexChangedW READ hexChangedW NOTIFY changed)
    Q_PROPERTY(qreal hexAsciiX READ hexAsciiX NOTIFY changed)
    Q_PROPERTY(qreal disAddrX READ disAddrX NOTIFY changed)
    Q_PROPERTY(qreal disBytesX READ disBytesX NOTIFY changed)
    Q_PROPERTY(qreal disTextX READ disTextX NOTIFY changed)
    Q_PROPERTY(qreal disCommentX READ disCommentX NOTIFY changed)
    Q_PROPERTY(qreal zoom READ zoom NOTIFY changed)
public:
    using QObject::QObject;
    qreal fontPx() const;
    qreal lineH() const;
    qreal hexBytesX() const;
    qreal hexByteStep() const;
    qreal hexChangedW() const;
    qreal hexAsciiX() const;
    qreal disAddrX() const;
    qreal disBytesX() const;
    qreal disTextX() const;
    qreal disCommentX() const;
    qreal zoom() const { return _zoom; }
    void SetZoom(qreal z) { _zoom = z; emit changed(); }
    void Notify() { emit changed(); }
signals:
    void changed();

private:
    qreal _zoom = 1;
};

class HexModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { AddrRole = Qt::UserRole + 1, BytesRole, ChangedRole, AsciiRole };
    explicit HexModel(const bench::Workload& w, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    void ResetRows() { beginResetModel(); endResetModel(); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void Refresh();

private:
    const bench::Workload& _w;
    std::array<QString, bench::kHexRows> _addr, _ascii;
    std::array<QStringList, bench::kHexRows> _bytes;
    std::array<QVariantList, bench::kHexRows> _changed;
};

class DisasmModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { AddrRole = Qt::UserRole + 1, BytesRole, TextRole, CommentRole, PcRole, BpRole };
    explicit DisasmModel(const bench::Workload& w, QObject* parent = nullptr);
    int rowCount(const QModelIndex& = {}) const override;
    void ResetRows() { beginResetModel(); endResetModel(); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void Refresh();

private:
    const bench::Workload& _w;
    struct Row
    {
        QString addr, bytes, text, comment;
        bool pc = false, bp = false;
    };
    std::array<Row, bench::kDisasmLines> _rows;
};

class Bridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList ticks READ ticks NOTIFY changed)
    Q_PROPERTY(qreal playhead READ playhead NOTIFY changed)
public:
    explicit Bridge(const bench::Workload& w, QObject* parent = nullptr) : QObject(parent), _w(w) {}
    QVariantList ticks() const { return _ticks; }
    qreal playhead() const { return _w.TimelinePlayheadX(); }
    void Refresh();
signals:
    void changed();

private:
    const bench::Workload& _w;
    QVariantList _ticks;
};

class TimelineItem : public QQuickItem
{
    Q_OBJECT
public:
    explicit TimelineItem(QQuickItem* parent = nullptr);
    static void SetWorkload(const bench::Workload* w) { s_w = w; }

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    static const bench::Workload* s_w;
};

class ImageItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(int index READ index WRITE setIndex)
public:
    explicit ImageItem(QQuickItem* parent = nullptr);
    static void SetWorkload(const bench::Workload* w) { s_w = w; }
    int index() const { return _index; }
    void setIndex(int i) { _index = i; }

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    static const bench::Workload* s_w;
    int _index = 0;
};
