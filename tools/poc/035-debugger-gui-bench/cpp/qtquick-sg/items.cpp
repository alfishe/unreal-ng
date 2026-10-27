#include "items.h"

#include "../common/layout.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGRendererInterface>
#include <QSGTextNode>
#include <QSGVertexColorMaterial>
#include <QTextLayout>

#include <cmath>
#include <cstring>

using namespace bench;
namespace L = bench::layout;

namespace sg
{

static const Workload* s_w = nullptr;
void SetWorkload(const Workload* w) { s_w = w; }
const Workload* W() { return s_w; }

static QColor C(uint32_t rgb) { return QColor::fromRgb((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }

QFont ContentFont()
{
    QFont f(L::kFontFamily);
    f.setPixelSize(int(L::T().fontPx));
    f.setStyleHint(QFont::Monospace);
    return f;
}

static bool IsSoftware(QQuickWindow* w)
{
    return w->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
}

// --- TextRowsItem ----------------------------------------------------------------

TextRowsItem::TextRowsItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

static const uint32_t kInk[3] = {L::kMuted, L::kFg, L::kAccent};

QSGNode* TextRowsItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    if (!s_w)
        return old;
    const int rows = Rows();
    if (old && (int(_rows.size()) != rows || _fontPx != L::T().fontPx))
    {
        delete old;  // font change: the row count and every layout change
        old = nullptr;
    }
    QSGNode* root = old;
    if (!root)
    {
        root = new QSGNode;
        _rows.assign(rows, RowNodes{});
        _fontPx = L::T().fontPx;
        for (auto& r : _rows)
        {
            r.bgParent = new QSGNode;
            root->appendChildNode(r.bgParent);
            for (int i = 0; i < MaxBackgrounds(); i++)
            {
                QSGRectangleNode* rect = window()->createRectangleNode();
                rect->setRect(QRectF());
                r.bgParent->appendChildNode(rect);
            }
            for (int ink = 0; ink < 3; ink++)
            {
                r.text[ink] = window()->createTextNode();
                r.text[ink]->setColor(C(kInk[ink]));
                root->appendChildNode(r.text[ink]);
            }
        }
    }
    const QFont font = ContentFont();
    QTextOption opt;
    opt.setTabArray(Tabs());
    opt.setWrapMode(QTextOption::NoWrap);
    const float lineH = L::T().lineH;
    RowContent next;
    for (int i = 0; i < rows; i++)
    {
        RowNodes& r = _rows[i];
        next = RowContent{};
        Build(i, next);
        if (r.valid && next == r.content)
            continue;
        const float y = i * lineH;
        // backgrounds: a fixed pool per row made with the row (adding and removing nodes later misplaces them in the
        // software adaptation); unused ones get an empty rectangle
        int k = 0;
        for (QSGNode* n = r.bgParent->firstChild(); n; n = n->nextSibling(), k++)
        {
            auto* rect = static_cast<QSGRectangleNode*>(n);
            if (k < int(next.bg.size()))
            {
                rect->setRect(next.bg[k].translated(0, y));
                rect->setColor(C(next.bgColour[k]));
            }
            else
                rect->setRect(QRectF());
        }
        for (int ink = 0; ink < 3; ink++)
        {
            QSGTextNode* t = r.text[ink];
            t->clear();
            if (next.ink[ink].find_first_not_of(" \t") == std::string::npos)
                continue;
            QTextLayout layout(QString::fromLatin1(next.ink[ink]), font);
            layout.setTextOption(opt);
            layout.beginLayout();
            QTextLine line = layout.createLine();
            line.setLineWidth(100000);
            layout.endLayout();
            t->addTextLayout(QPointF(TextX(), y + (lineH - line.height()) * 0.5), &layout);
        }
        r.content = std::move(next);
        r.valid = true;
    }
    return root;
}

/// Puts `text` into ink `ink` of a tab-separated row and blanks it (same length) in the others
static void Field(TextRowsItem* /*unused*/, std::string out[3], int ink, const std::string& text, bool first)
{
    for (int i = 0; i < 3; i++)
    {
        if (!first)
            out[i] += '\t';
        out[i] += (i == ink) ? text : std::string(text.size(), ' ');
    }
}

// --- HexItem -------------------------------------------------------------------------

int HexItem::Rows() const { return L::T().hexRows; }

QList<qreal> HexItem::Tabs() const
{
    const L::TextMetrics& t = L::T();
    QList<qreal> tabs;
    for (int c = 0; c < kHexCols; c++)
        tabs << t.hexBytesX + c * t.hexByteStep;
    tabs << t.hexAsciiX;
    return tabs;
}

void HexItem::Build(int r, RowContent& out) const
{
    const L::TextMetrics& t = L::T();
    const uint8_t* mem = s_w->Memory();
    Field(nullptr, out.ink, 0, s_w->HexRowAddr(r), true);
    for (int c = 0; c < kHexCols; c++)
    {
        const bool ch = s_w->HexChanged(r, c);
        Field(nullptr, out.ink, ch ? 2 : 1, std::string(Workload::Hex2(mem[(s_w->HexBase() + r * kHexCols + c) & 0xFFFF]), 2), false);
        if (ch)
        {
            out.bg.push_back(QRectF(t.hexBytesX + c * t.hexByteStep - 2, 0, t.hexChangedW, t.lineH - 1));
            out.bgColour.push_back(L::kChangedBg);
        }
    }
    Field(nullptr, out.ink, 0, s_w->HexRowAscii(r), false);
}

// --- DisasmItem ----------------------------------------------------------------------

int DisasmItem::Rows() const { return L::T().disRows; }

qreal DisasmItem::TextX() const { return L::T().disAddrX; }

QList<qreal> DisasmItem::Tabs() const
{
    const L::TextMetrics& t = L::T();
    return {t.disBytesX - t.disAddrX, t.disTextX - t.disAddrX, t.disCommentX - t.disAddrX};
}

void DisasmItem::Build(int i, RowContent& out) const
{
    const L::TextMetrics& t = L::T();
    const auto& l = s_w->Disasm()[i];
    if (l.isPc)
    {
        out.bg.push_back(QRectF(0, 0, L::kDisasm.w - 2 * L::kPad, t.lineH));
        out.bgColour.push_back(L::kPcRowBg);
    }
    if (l.isBreakpoint)
    {
        out.bg.push_back(QRectF(1, t.lineH * 0.5 - 4, 8, 8));
        out.bgColour.push_back(L::kBreak);
    }
    Field(nullptr, out.ink, 0, l.addr, true);
    Field(nullptr, out.ink, 0, l.bytes, false);
    Field(nullptr, out.ink, l.isPc ? 2 : 1, l.text, false);
    Field(nullptr, out.ink, 0, l.comment, false);
}

// --- TimelineItem --------------------------------------------------------------------

TimelineItem::TimelineItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

void TimelineItem::Raster()
{
    const int W = kTimelineWidth, H = int(height());
    if (_img.width() != W || _img.height() != H)
        _img = QImage(W, H, QImage::Format_RGB32);
    const int stride = int(_img.bytesPerLine() / 4);
    uint32_t* base = reinterpret_cast<uint32_t*>(_img.bits());
    const int ry = int(std::lround(kTimelineLanes * L::kLaneH));
    const uint32_t panel = 0xFF000000u | L::kPanel, laneBg = 0xFF000000u | L::kLaneBg;
    for (int y = 0; y < H; y++)
        std::fill(base + y * stride, base + y * stride + W, panel);
    for (int lane = 0; lane < kTimelineLanes; lane++)
    {
        const int y0 = int(std::lround(lane * L::kLaneH)), y1 = int(std::lround(lane * L::kLaneH + L::kLaneH - 4));
        const float inner = float(y1 - y0);
        const uint32_t fg = 0xFF000000u | Workload::LaneColour(lane);
        int top[kTimelineWidth];
        for (int x = 0; x < W; x++)
            top[x] = y1 - int(s_w->TimelineValue(lane, x) * inner + 0.5f);
        for (int y = y0; y < y1; y++)
        {
            uint32_t* row = base + y * stride;
            for (int x = 0; x < W; x++)
                row[x] = y >= top[x] ? fg : laneBg;
        }
    }
    for (const auto& t : s_w->TimelineTicks())
        for (int y = ry; y < ry + 4 && y < H; y++)
            base[y * stride + int(t.x)] = 0xFF000000u | L::kMuted;
    const int ph = int(std::lround(s_w->TimelinePlayheadX()));
    for (int y = 0; y < ry; y++)
        for (int x = ph - 1; x < ph + 1; x++)
            if (x >= 0 && x < W)
                base[y * stride + x] = 0xFF000000u | L::kPlayhead;
}

QSGNode* TimelineItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    if (!s_w)
        return old;
    const bool sw = IsSoftware(window());
    QSGNode* root = old;
    QSGNode* bars = nullptr;
    QSGTextNode* names = nullptr;
    QSGTextNode* ticks = nullptr;
    if (!root)
    {
        root = new QSGNode;
        if (sw)
            bars = window()->createImageNode();
        else
        {
            auto* g = new QSGGeometryNode;
            auto* geo = new QSGGeometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
            geo->setDrawingMode(QSGGeometry::DrawTriangles);
            g->setGeometry(geo);
            g->setFlag(QSGNode::OwnsGeometry);
            g->setMaterial(new QSGVertexColorMaterial);
            g->setFlag(QSGNode::OwnsMaterial);
            bars = g;
        }
        root->appendChildNode(bars);
        names = window()->createTextNode();
        names->setColor(C(L::kFg));
        root->appendChildNode(names);
        ticks = window()->createTextNode();
        ticks->setColor(C(L::kMuted));
        root->appendChildNode(ticks);
        const QFont font = ContentFont();
        for (int lane = 0; lane < kTimelineLanes; lane++)
        {
            QTextLayout layout(QString::fromLatin1(Workload::LaneName(lane)), font);
            layout.beginLayout();
            layout.createLine();
            layout.endLayout();
            names->addTextLayout(QPointF(4, lane * L::kLaneH + 2), &layout);
        }
    }
    else
    {
        bars = root->firstChild();
        names = static_cast<QSGTextNode*>(bars->nextSibling());
        ticks = static_cast<QSGTextNode*>(names->nextSibling());
    }

    if (sw)
    {
        Raster();
        auto* img = static_cast<QSGImageNode*>(bars);
        QSGTexture* previous = img->texture();
        img->setTexture(window()->createTextureFromImage(_img));
        img->setRect(QRectF(0, 0, _img.width(), _img.height()));
        delete previous;
    }
    else
    {
        auto* node = static_cast<QSGGeometryNode*>(bars);
        int quads = kTimelineLanes + kTimelineTicks + 1;
        for (int lane = 0; lane < kTimelineLanes; lane++)
            for (int x = 0; x < kTimelineWidth; x++)
                quads += s_w->TimelineValue(lane, x) > 0.f;
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
                const float val = s_w->TimelineValue(lane, x);
                if (val > 0.f)
                    quad(x, y1 - val * (y1 - y0), x + 1, y1, c);
            }
        }
        const float ry = kTimelineLanes * L::kLaneH;
        for (const auto& t : s_w->TimelineTicks())
            quad(t.x, ry, t.x + 1, ry + 4, L::kMuted);
        const float px = s_w->TimelinePlayheadX();
        quad(px - 1, 0, px + 1, ry, L::kPlayhead);
        node->markDirty(QSGNode::DirtyGeometry);
    }

    // ruler labels: one text node, 20 short layouts
    ticks->clear();
    const QFont font = ContentFont();
    const float ry = kTimelineLanes * L::kLaneH;
    for (const auto& t : s_w->TimelineTicks())
    {
        QTextLayout layout(QString::fromLatin1(t.label), font);
        layout.beginLayout();
        layout.createLine();
        layout.endLayout();
        ticks->addTextLayout(QPointF(t.x + 2, ry + 3), &layout);
    }
    return root;
}

// --- ImageItem -----------------------------------------------------------------------

ImageItem::ImageItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

QSGNode* ImageItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGImageNode*>(old);
    if (!node)
    {
        node = window()->createImageNode();
        node->setFiltering(QSGTexture::Linear);
    }
    const QImage img(s_w->Image(_index), kTexW, kTexH, QImage::Format_RGBA8888);
    QSGTexture* previous = node->texture();
    node->setTexture(window()->createTextureFromImage(img));
    node->setRect(boundingRect());
    delete previous;
    return node;
}

}  // namespace sg
