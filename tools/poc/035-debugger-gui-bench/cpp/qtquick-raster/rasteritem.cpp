#include "rasteritem.h"

#include <QQuickWindow>
#include <QSGImageNode>

bench::Workload* RasterItem::s_w = nullptr;

RasterItem::RasterItem(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

QSGNode* RasterItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    if (!_rv)
    {
        if (_view == "hex")
            _rv = std::make_unique<raster::HexRaster>(*s_w);
        else if (_view == "disasm")
            _rv = std::make_unique<raster::DisasmRaster>(*s_w);
        else if (_view == "timeline")
            _rv = std::make_unique<raster::TimelineRaster>(*s_w);
        else
            _rv = std::make_unique<raster::ImagesRaster>(*s_w);
    }
    auto* node = static_cast<QSGImageNode*>(old);
    const QRegion dirty = _rv->Frame(QSize(int(width()), int(height())), float(window()->effectiveDevicePixelRatio()));
    if (!node)
    {
        node = window()->createImageNode();
        node->setFiltering(QSGTexture::Nearest);
    }
    if (!dirty.isEmpty() || !node->texture())
    {
        QSGTexture* previous = node->texture();
        node->setTexture(window()->createTextureFromImage(_rv->Image()));
        delete previous;
        node->setRect(QRectF(0, 0, width(), height()));
        node->markDirty(QSGNode::DirtyMaterial);
    }
    return node;
}
