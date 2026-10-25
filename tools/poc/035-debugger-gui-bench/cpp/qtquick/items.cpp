#include "items.h"

#include "../common/layout.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGVertexColorMaterial>

using namespace bench;
namespace L = bench::layout;

// --- Metrics -------------------------------------------------------------------

qreal Metrics::fontPx() const { return L::T().fontPx; }
qreal Metrics::lineH() const { return L::T().lineH; }
qreal Metrics::hexBytesX() const { return L::T().hexBytesX; }
qreal Metrics::hexByteStep() const { return L::T().hexByteStep; }
qreal Metrics::hexChangedW() const { return L::T().hexChangedW; }
qreal Metrics::hexAsciiX() const { return L::T().hexAsciiX; }
qreal Metrics::disAddrX() const { return L::T().disAddrX; }
qreal Metrics::disBytesX() const { return L::T().disBytesX; }
qreal Metrics::disTextX() const { return L::T().disTextX; }
qreal Metrics::disCommentX() const { return L::T().disCommentX; }

int HexModel::rowCount(const QModelIndex&) const { return L::T().hexRows; }
int DisasmModel::rowCount(const QModelIndex&) const { return L::T().disRows; }

// --- HexModel ------------------------------------------------------------------

HexModel::HexModel(const Workload& w, QObject* parent) : QAbstractListModel(parent), _w(w)
{
    for (int r = 0; r < kHexRows; r++)
    {
        _bytes[r] = QStringList(kHexCols, QString());
        _changed[r] = QVariantList(kHexCols, false);
    }
    Refresh();
}

QHash<int, QByteArray> HexModel::roleNames() const
{
    return {{AddrRole, "addr"}, {BytesRole, "bytes"}, {ChangedRole, "changed"}, {AsciiRole, "ascii"}};
}

QVariant HexModel::data(const QModelIndex& index, int role) const
{
    const int r = index.row();
    switch (role)
    {
    case AddrRole: return _addr[r];
    case BytesRole: return _bytes[r];
    case ChangedRole: return _changed[r];
    case AsciiRole: return _ascii[r];
    }
    return {};
}

void HexModel::Refresh()
{
    const uint8_t* mem = _w.Memory();
    for (int r = 0; r < L::T().hexRows; r++)
    {
        bool dirty = false;
        const QString addr = QString::fromLatin1(_w.HexRowAddr(r));
        if (addr != _addr[r])
            _addr[r] = addr, dirty = true;
        for (int c = 0; c < kHexCols; c++)
        {
            const QString b = QString::fromLatin1(Workload::Hex2(mem[(_w.HexBase() + r * kHexCols + c) & 0xFFFF]));
            const bool ch = _w.HexChanged(r, c);
            if (b != _bytes[r][c])
                _bytes[r][c] = b, dirty = true;
            if (ch != _changed[r][c].toBool())
                _changed[r][c] = ch, dirty = true;
        }
        const QString ascii = QString::fromLatin1(_w.HexRowAscii(r));
        if (ascii != _ascii[r])
            _ascii[r] = ascii, dirty = true;
        if (dirty)
            emit dataChanged(index(r), index(r));
    }
}

// --- DisasmModel -----------------------------------------------------------------

DisasmModel::DisasmModel(const Workload& w, QObject* parent) : QAbstractListModel(parent), _w(w) { Refresh(); }

QHash<int, QByteArray> DisasmModel::roleNames() const
{
    return {{AddrRole, "addr"}, {BytesRole, "bytes"}, {TextRole, "text"}, {CommentRole, "comment"}, {PcRole, "pc"}, {BpRole, "bp"}};
}

QVariant DisasmModel::data(const QModelIndex& index, int role) const
{
    const Row& r = _rows[index.row()];
    switch (role)
    {
    case AddrRole: return r.addr;
    case BytesRole: return r.bytes;
    case TextRole: return r.text;
    case CommentRole: return r.comment;
    case PcRole: return r.pc;
    case BpRole: return r.bp;
    }
    return {};
}

void DisasmModel::Refresh()
{
    int i = 0;
    for (const auto& l : _w.Disasm())
    {
        if (i >= L::T().disRows)
            break;
        Row n{QString::fromLatin1(l.addr), QString::fromLatin1(l.bytes), QString::fromLatin1(l.text),
              QString::fromLatin1(l.comment), l.isPc, l.isBreakpoint};
        Row& o = _rows[i];
        if (n.addr != o.addr || n.bytes != o.bytes || n.text != o.text || n.comment != o.comment || n.pc != o.pc || n.bp != o.bp)
        {
            o = std::move(n);
            emit dataChanged(index(i), index(i));
        }
        i++;
    }
}

// --- Bridge ------------------------------------------------------------------------

void Bridge::Refresh()
{
    _ticks.clear();
    for (const auto& t : _w.TimelineTicks())
        _ticks.append(QVariantMap{{"x", t.x}, {"label", QString::fromStdString(t.label)}});
    emit changed();
}

// --- TimelineItem --------------------------------------------------------------------

const Workload* TimelineItem::s_w = nullptr;
const Workload* ImageItem::s_w = nullptr;

TimelineItem::TimelineItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

QSGNode* TimelineItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGGeometryNode*>(old);
    if (!node)
    {
        node = new QSGGeometryNode;
        auto* g = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        g->setDrawingMode(QSGGeometry::DrawTriangles);
        node->setGeometry(g);
        node->setFlag(QSGNode::OwnsGeometry);
        node->setMaterial(new QSGVertexColorMaterial);
        node->setFlag(QSGNode::OwnsMaterial);
    }
    const Workload& w = *s_w;

    // count quads: lane backgrounds, bars, ticks, playhead
    int quads = kTimelineLanes + kTimelineTicks + 1;
    for (int lane = 0; lane < kTimelineLanes; lane++)
        for (int x = 0; x < kTimelineWidth; x++)
            quads += w.TimelineValue(lane, x) > 0.f;

    QSGGeometry* g = node->geometry();
    g->allocate(quads * 6);
    auto* v = g->vertexDataAsColoredPoint2D();
    auto quad = [&v](float x0, float y0, float x1, float y1, uint32_t rgb) {
        const uchar r = (rgb >> 16) & 255, gg = (rgb >> 8) & 255, b = rgb & 255;
        v[0].set(x0, y0, r, gg, b, 255);
        v[1].set(x1, y0, r, gg, b, 255);
        v[2].set(x0, y1, r, gg, b, 255);
        v[3].set(x1, y0, r, gg, b, 255);
        v[4].set(x1, y1, r, gg, b, 255);
        v[5].set(x0, y1, r, gg, b, 255);
        v += 6;
    };
    for (int lane = 0; lane < kTimelineLanes; lane++)
    {
        const float y0 = lane * L::kLaneH, y1 = y0 + L::kLaneH - 4;
        quad(0, y0, kTimelineWidth, y1, L::kLaneBg);
        const uint32_t c = Workload::LaneColour(lane);
        for (int x = 0; x < kTimelineWidth; x++)
        {
            const float val = w.TimelineValue(lane, x);
            if (val > 0.f)
                quad(x, y1 - val * (y1 - y0), x + 1, y1, c);
        }
    }
    const float ry = kTimelineLanes * L::kLaneH;
    for (const auto& t : w.TimelineTicks())
        quad(t.x, ry, t.x + 1, ry + 4, L::kMuted);
    const float px = w.TimelinePlayheadX();
    quad(px - 1, 0, px + 1, ry, L::kPlayhead);
    node->markDirty(QSGNode::DirtyGeometry);
    return node;
}

// --- ImageItem ---------------------------------------------------------------------

ImageItem::ImageItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

QSGNode* ImageItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    if (!node)
    {
        node = new QSGSimpleTextureNode;
        node->setFiltering(QSGTexture::Linear);
    }
    const QImage img(s_w->Image(_index), kTexW, kTexH, QImage::Format_RGBA8888);
    QSGTexture* previous = node->texture();
    node->setTexture(window()->createTextureFromImage(img));  // a new texture per frame: the Qt 6.4 public path
    delete previous;
    node->setRect(boundingRect());
    return node;
}
